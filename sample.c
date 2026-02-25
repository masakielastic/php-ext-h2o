#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include <uv.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <h2o.h>
#include <h2o/http2.h>
#include <h2o/socket/uv-binding.h> /* h2o_uv_socket_create */

/* ===== h2o global ===== */
static h2o_globalconf_t gconf;
static h2o_context_t ctx;
static h2o_accept_ctx_t accept_ctx;

static void dump_req(FILE *out, h2o_req_t *req)
{
    fprintf(out, "=== h2o request analysis ===\n");

    fprintf(out, "version: HTTP/%d.%d\n", req->version >> 8, req->version & 0xff);

    fprintf(out, "method:    %.*s\n", (int)req->method.len, req->method.base);

    /* scheme: h2o の版で (iovec) or (pointer) があるので pointer 前提で読む */
    if (req->scheme != NULL) {
        /* 多くの版で req->scheme は h2o_url_scheme_t* で name が iovec */
        fprintf(out, "scheme:    %.*s\n",
                (int)req->scheme->name.len, req->scheme->name.base);
    } else {
        fprintf(out, "scheme:    (null)\n");
    }

    fprintf(out, "authority: %.*s\n", (int)req->authority.len, req->authority.base);
    fprintf(out, "path:      %.*s\n", (int)req->path.len, req->path.base);

    fprintf(out, "headers (%zu):\n", req->headers.size);
    for (size_t i = 0; i < req->headers.size; i++) {
        const h2o_header_t *h = req->headers.entries + i;

        /* token があるときだけ安全に名前が取れる（版差が少ない） */
        if (h->name != NULL) {
            fprintf(out, "  %.*s: %.*s\n",
                    (int)h->name->len, h->name->base,
                    (int)h->value.len, h->value.base);
        } else {
            /* token 化されてないヘッダ名は版差が大きいので value だけ表示 */
            fprintf(out, "  (non-token-header): %.*s\n",
                    (int)h->value.len, h->value.base);
        }
    }

    fprintf(out, "body: %zu bytes\n", req->entity.len);
    fprintf(out, "============================\n");
}

static int on_req(h2o_handler_t *self, h2o_req_t *req)
{
    dump_req(stderr, req);

    const char *scheme_base = "(null)";
    int scheme_len = 6;
    if (req->scheme != NULL) {
        scheme_base = req->scheme->name.base;
        scheme_len = (int)req->scheme->name.len;
    }

    char buf[8192];
    int n = snprintf(buf, sizeof(buf),
                     "HTTP/%d.%d\n"
                     "method: %.*s\n"
                     "scheme: %.*s\n"
                     "authority: %.*s\n"
                     "path: %.*s\n"
                     "headers: %zu\n"
                     "body: %zu bytes\n",
                     req->version >> 8, req->version & 0xff,
                     (int)req->method.len, req->method.base,
                     scheme_len, scheme_base,
                     (int)req->authority.len, req->authority.base,
                     (int)req->path.len, req->path.base,
                     req->headers.size,
                     req->entity.len);

    req->res.status = 200;
    req->res.reason = "OK";
    h2o_add_header(&req->pool, &req->res.headers,
                   H2O_TOKEN_CONTENT_TYPE, NULL,
                   H2O_STRLIT("text/plain"));

    if (n < 0) n = 0;
    if ((size_t)n > sizeof(buf)) n = (int)sizeof(buf);

    h2o_send_inline(req, buf, (size_t)n);
    return 0;
}

/* ===== TLS setup: cert/key + cipher + ALPN(NPN) registration ===== */
static int setup_ssl(const char *cert_file, const char *key_file, const char *ciphers)
{
    /* OpenSSL init (h2o example does this in setup_ssl) */
    SSL_load_error_strings();
    SSL_library_init();
    OpenSSL_add_all_algorithms();

    /* server SSL_CTX */
    accept_ctx.ssl_ctx = SSL_CTX_new(SSLv23_server_method());
    SSL_CTX_set_options(accept_ctx.ssl_ctx, SSL_OP_NO_SSLv2);

#ifdef SSL_CTX_set_ecdh_auto
    SSL_CTX_set_ecdh_auto(accept_ctx.ssl_ctx, 1);
#endif

    if (SSL_CTX_use_certificate_chain_file(accept_ctx.ssl_ctx, cert_file) != 1) {
        fprintf(stderr, "failed to load cert: %s\n", cert_file);
        return -1;
    }
    if (SSL_CTX_use_PrivateKey_file(accept_ctx.ssl_ctx, key_file, SSL_FILETYPE_PEM) != 1) {
        fprintf(stderr, "failed to load key: %s\n", key_file);
        return -1;
    }
    if (SSL_CTX_set_cipher_list(accept_ctx.ssl_ctx, ciphers) != 1) {
        fprintf(stderr, "failed to set ciphers\n");
        return -1;
    }

    /* Protocol negotiation: register HTTP/2 protocols (ALPN; and NPN if enabled) */
#if H2O_USE_NPN
    h2o_ssl_register_npn_protocols(accept_ctx.ssl_ctx, h2o_http2_npn_protocols);
#endif
#if H2O_USE_ALPN
    h2o_ssl_register_alpn_protocols(accept_ctx.ssl_ctx, h2o_http2_alpn_protocols);
#endif
    return 0;
}

static void on_client_close(uv_handle_t *h)
{
    free(h);
}

/* ===== libuv accept callback ===== */
static void on_uv_connection(uv_stream_t *listener, int status)
{
    if (status != 0)
        return;

    uv_tcp_t *client = (uv_tcp_t *)malloc(sizeof(*client));
    if (client == NULL)
        return;

    uv_tcp_init(listener->loop, client);

    if (uv_accept(listener, (uv_stream_t *)client) != 0) {
        uv_close((uv_handle_t *)client, on_client_close);
        return;
    }

    /* wrap uv handle into h2o socket, then let h2o accept (HTTP/1.1 or HTTP/2) */
    h2o_socket_t *sock = h2o_uv_socket_create((uv_handle_t *)client, on_client_close);
    h2o_accept(&accept_ctx, sock);
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);

    /* ---- h2o config ---- */
    h2o_config_init(&gconf);

    h2o_hostconf_t *hostconf =
        h2o_config_register_host(&gconf, h2o_iovec_init(H2O_STRLIT("default")), 65535);

    h2o_pathconf_t *pathconf = h2o_config_register_path(hostconf, "/", 0);
    h2o_handler_t *handler = h2o_create_handler(pathconf, sizeof(*handler));
    handler->on_req = on_req;

    /* ---- libuv loop + h2o context ---- */
    uv_loop_t loop;
    uv_loop_init(&loop);
    h2o_context_init(&ctx, &loop, &gconf);

    memset(&accept_ctx, 0, sizeof(accept_ctx));
    accept_ctx.ctx = &ctx;
    accept_ctx.hosts = gconf.hosts;

    /* ---- enable TLS + ALPN (HTTP/2) ---- */
    if (setup_ssl("cert/server.crt", "cert/server.key",
                  "DEFAULT:!MD5:!DSS:!DES:!RC4:!RC2:!SEED:!IDEA:!NULL:!ADH:!EXP:!SRP:!PSK") != 0) {
        fprintf(stderr, "TLS setup failed\n");
        return 1;
    }

    /* ---- listen on 0.0.0.0:8443 ---- */
    uv_tcp_t server;
    uv_tcp_init(&loop, &server);

    struct sockaddr_in addr;
    uv_ip4_addr("0.0.0.0", 8443, &addr);
    uv_tcp_bind(&server, (const struct sockaddr *)&addr, 0);

    int r = uv_listen((uv_stream_t *)&server, 128, on_uv_connection);
    if (r != 0) {
        fprintf(stderr, "listen error: %s\n", uv_strerror(r));
        return 1;
    }

    printf("Listening on https://0.0.0.0:8443 (HTTP/2 via ALPN if client supports)\n");
    uv_run(&loop, UV_RUN_DEFAULT);
    return 0;
}
