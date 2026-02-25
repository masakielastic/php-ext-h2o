#include "php.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <uv.h>

#include <h2o.h>
#include <h2o/http2.h>
#include <h2o/socket/uv-binding.h>

#include "h2o_server.h"

static h2o_globalconf_t gconf;
static h2o_context_t gctx;
static h2o_accept_ctx_t gaccept_ctx;
static uv_loop_t gloop;
static uv_signal_t gsigint;
static int gloop_initialized = 0;
static int gctx_initialized = 0;
static uv_tcp_t gserver;
static int gserver_initialized = 0;
static int gsigint_initialized = 0;

typedef struct _php_h2o_runtime_options {
    const char *response_body;
    size_t response_body_len;
} php_h2o_runtime_options;

static php_h2o_runtime_options gruntime;

#ifdef PHP_H2O_DEBUG
#define PHP_H2O_LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define PHP_H2O_LOG(...) do { } while (0)
#endif

static void php_h2o_dump_req(FILE *out, h2o_req_t *req)
{
#ifdef PHP_H2O_DEBUG
    fprintf(out, "=== h2o request analysis ===\\n");
    fprintf(out, "version: HTTP/%d.%d\\n", req->version >> 8, req->version & 0xff);
    fprintf(out, "method: %.*s\\n", (int)req->method.len, req->method.base);
    fprintf(out, "path: %.*s\\n", (int)req->path.len, req->path.base);
    fprintf(out, "headers: %zu\\n", req->headers.size);
    fprintf(out, "body: %zu bytes\\n", req->entity.len);
    fprintf(out, "============================\\n");
#else
    (void)out;
    (void)req;
#endif
}

static int php_h2o_on_req(h2o_handler_t *self, h2o_req_t *req)
{
    (void)self;

    php_h2o_dump_req(stderr, req);

    req->res.status = 200;
    req->res.reason = "OK";

    h2o_add_header(
        &req->pool,
        &req->res.headers,
        H2O_TOKEN_CONTENT_TYPE,
        NULL,
        H2O_STRLIT("text/plain")
    );

    h2o_send_inline(req, gruntime.response_body, gruntime.response_body_len);
    return 0;
}

static void php_h2o_on_client_close(uv_handle_t *h)
{
    free(h);
}

static void php_h2o_on_uv_connection(uv_stream_t *listener, int status)
{
    uv_tcp_t *client;
    h2o_socket_t *sock;

    if (status != 0) {
        PHP_H2O_LOG("accept callback status=%d\\n", status);
        return;
    }

    client = (uv_tcp_t *)malloc(sizeof(*client));
    if (client == NULL) {
        return;
    }

    uv_tcp_init(listener->loop, client);

    if (uv_accept(listener, (uv_stream_t *)client) != 0) {
        uv_close((uv_handle_t *)client, php_h2o_on_client_close);
        return;
    }

    sock = h2o_uv_socket_create((uv_handle_t *)client, php_h2o_on_client_close);
    h2o_accept(&gaccept_ctx, sock);
}

static void php_h2o_on_sigint(uv_signal_t *handle, int signum)
{
    (void)handle;
    (void)signum;

    PHP_H2O_LOG("received SIGINT, stopping loop\\n");

    if (gserver_initialized && !uv_is_closing((uv_handle_t *)&gserver)) {
        uv_close((uv_handle_t *)&gserver, NULL);
    }
    if (gsigint_initialized && !uv_is_closing((uv_handle_t *)&gsigint)) {
        uv_close((uv_handle_t *)&gsigint, NULL);
    }
    uv_stop(&gloop);
}

static int php_h2o_setup_ssl(const char *cert_file, const char *key_file)
{
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    OPENSSL_init_ssl(0, NULL);
#else
    SSL_load_error_strings();
    SSL_library_init();
    OpenSSL_add_all_algorithms();
#endif

#if defined(TLS_server_method)
    gaccept_ctx.ssl_ctx = SSL_CTX_new(TLS_server_method());
#else
    gaccept_ctx.ssl_ctx = SSL_CTX_new(SSLv23_server_method());
#endif

    if (gaccept_ctx.ssl_ctx == NULL) {
        return -1;
    }

    SSL_CTX_set_options(gaccept_ctx.ssl_ctx, SSL_OP_NO_SSLv2);

    if (SSL_CTX_use_certificate_chain_file(gaccept_ctx.ssl_ctx, cert_file) != 1) {
        return -1;
    }

    if (SSL_CTX_use_PrivateKey_file(gaccept_ctx.ssl_ctx, key_file, SSL_FILETYPE_PEM) != 1) {
        return -1;
    }

#if H2O_USE_NPN
    h2o_ssl_register_npn_protocols(gaccept_ctx.ssl_ctx, h2o_http2_npn_protocols);
#endif
#if H2O_USE_ALPN
    h2o_ssl_register_alpn_protocols(gaccept_ctx.ssl_ctx, h2o_http2_alpn_protocols);
#endif

    return 0;
}

static void php_h2o_cleanup(void)
{
    if (gctx_initialized) {
        h2o_context_dispose(&gctx);
        gctx_initialized = 0;
    }

    h2o_config_dispose(&gconf);

    if (gaccept_ctx.ssl_ctx != NULL) {
        SSL_CTX_free(gaccept_ctx.ssl_ctx);
        gaccept_ctx.ssl_ctx = NULL;
    }

    if (gloop_initialized) {
        uv_run(&gloop, UV_RUN_DEFAULT);
        uv_loop_close(&gloop);
        gloop_initialized = 0;
    }
    gserver_initialized = 0;
    gsigint_initialized = 0;
}

int php_h2o_server_run(const php_h2o_server_options *options)
{
    h2o_hostconf_t *hostconf;
    h2o_pathconf_t *pathconf;
    h2o_handler_t *handler;
    struct sockaddr_in addr;
    int rc;

    memset(&gaccept_ctx, 0, sizeof(gaccept_ctx));
    memset(&gruntime, 0, sizeof(gruntime));

    gruntime.response_body = options->response_body;
    gruntime.response_body_len = options->response_body_len;

    signal(SIGPIPE, SIG_IGN);

    h2o_config_init(&gconf);

    hostconf = h2o_config_register_host(&gconf, h2o_iovec_init(H2O_STRLIT("default")), 65535);
    pathconf = h2o_config_register_path(hostconf, "/", 0);
    handler = h2o_create_handler(pathconf, sizeof(*handler));
    handler->on_req = php_h2o_on_req;

    rc = uv_loop_init(&gloop);
    if (rc != 0) {
        php_error_docref(NULL, E_WARNING, "uv_loop_init failed: %s", uv_strerror(rc));
        h2o_config_dispose(&gconf);
        return FAILURE;
    }
    gloop_initialized = 1;

    h2o_context_init(&gctx, &gloop, &gconf);
    gctx_initialized = 1;

    gaccept_ctx.ctx = &gctx;
    gaccept_ctx.hosts = gconf.hosts;

    if (options->tls_cert != NULL && options->tls_key != NULL) {
        if (php_h2o_setup_ssl(options->tls_cert, options->tls_key) != 0) {
            php_error_docref(NULL, E_WARNING, "TLS setup failed");
            php_h2o_cleanup();
            return FAILURE;
        }
    }

    rc = uv_signal_init(&gloop, &gsigint);
    if (rc == 0) {
        gsigint_initialized = 1;
        uv_signal_start(&gsigint, php_h2o_on_sigint, SIGINT);
    }

    rc = uv_tcp_init(&gloop, &gserver);
    if (rc != 0) {
        php_error_docref(NULL, E_WARNING, "uv_tcp_init failed: %s", uv_strerror(rc));
        php_h2o_cleanup();
        return FAILURE;
    }
    gserver_initialized = 1;

    rc = uv_ip4_addr(options->host, options->port, &addr);
    if (rc != 0) {
        php_error_docref(NULL, E_WARNING, "invalid host/port: %s", uv_strerror(rc));
        php_h2o_cleanup();
        return FAILURE;
    }

    rc = uv_tcp_bind(&gserver, (const struct sockaddr *)&addr, 0);
    if (rc != 0) {
        php_error_docref(NULL, E_WARNING, "uv_tcp_bind failed: %s", uv_strerror(rc));
        php_h2o_cleanup();
        return FAILURE;
    }

    rc = uv_listen((uv_stream_t *)&gserver, 128, php_h2o_on_uv_connection);
    if (rc != 0) {
        php_error_docref(NULL, E_WARNING, "uv_listen failed: %s", uv_strerror(rc));
        php_h2o_cleanup();
        return FAILURE;
    }

    php_printf("h2o server listening on %s:%u\\n", options->host, options->port);
    uv_run(&gloop, UV_RUN_DEFAULT);

    if (gserver_initialized && !uv_is_closing((uv_handle_t *)&gserver)) {
        uv_close((uv_handle_t *)&gserver, NULL);
    }
    if (gsigint_initialized && !uv_is_closing((uv_handle_t *)&gsigint)) {
        uv_close((uv_handle_t *)&gsigint, NULL);
    }
    php_h2o_cleanup();

    return SUCCESS;
}
