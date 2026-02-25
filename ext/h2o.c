#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "ext/standard/info.h"
#include <string.h>

#include "php_h2o.h"
#include "h2o_server.h"

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_h2o_server_run, 0, 0, _IS_BOOL, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, options, IS_ARRAY, 1, "[]")
ZEND_END_ARG_INFO()

static zend_result php_h2o_read_str_option(HashTable *ht, const char *key, size_t key_len, char **out)
{
    zval *zv;
    zend_string *tmp;

    zv = zend_hash_str_find(ht, key, key_len);
    if (zv == NULL || Z_TYPE_P(zv) == IS_NULL) {
        return SUCCESS;
    }

    if (Z_TYPE_P(zv) != IS_STRING) {
        zend_type_error("Option '%s' must be string", key);
        return FAILURE;
    }

    tmp = zval_get_string(zv);
    *out = estrdup(ZSTR_VAL(tmp));
    zend_string_release(tmp);

    return SUCCESS;
}

PHP_FUNCTION(h2o_server_run)
{
    zval *options = NULL;
    HashTable *ht;
    zval *zv;

    php_h2o_server_options run_options;

    char *host_alloc = NULL;
    char *body_alloc = NULL;
    char *tls_cert_alloc = NULL;
    char *tls_key_alloc = NULL;

    run_options.host = "0.0.0.0";
    run_options.port = 8080;
    run_options.response_body = "OK\n";
    run_options.response_body_len = sizeof("OK\n") - 1;
    run_options.tls_cert = NULL;
    run_options.tls_key = NULL;

    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_OR_NULL(options)
    ZEND_PARSE_PARAMETERS_END();

    ht = (options != NULL && Z_TYPE_P(options) == IS_ARRAY) ? Z_ARRVAL_P(options) : NULL;

    if (ht != NULL) {
        zv = zend_hash_str_find(ht, "port", sizeof("port") - 1);
        if (zv != NULL && Z_TYPE_P(zv) != IS_NULL) {
            zend_long port;
            if (Z_TYPE_P(zv) != IS_LONG) {
                zend_type_error("Option 'port' must be int");
                RETURN_THROWS();
            }
            port = Z_LVAL_P(zv);
            if (port < 1 || port > 65535) {
                zend_value_error("Option 'port' must be between 1 and 65535");
                RETURN_THROWS();
            }
            run_options.port = (uint16_t)port;
        }

        if (php_h2o_read_str_option(ht, "host", sizeof("host") - 1, &host_alloc) == FAILURE) {
            RETURN_THROWS();
        }

        if (php_h2o_read_str_option(ht, "response_body", sizeof("response_body") - 1, &body_alloc) == FAILURE) {
            efree(host_alloc);
            RETURN_THROWS();
        }

        if (php_h2o_read_str_option(ht, "tls_cert", sizeof("tls_cert") - 1, &tls_cert_alloc) == FAILURE) {
            efree(host_alloc);
            efree(body_alloc);
            RETURN_THROWS();
        }

        if (php_h2o_read_str_option(ht, "tls_key", sizeof("tls_key") - 1, &tls_key_alloc) == FAILURE) {
            efree(host_alloc);
            efree(body_alloc);
            efree(tls_cert_alloc);
            RETURN_THROWS();
        }
    }

    if (host_alloc != NULL) {
        run_options.host = host_alloc;
    }

    if (body_alloc != NULL) {
        run_options.response_body = body_alloc;
        run_options.response_body_len = strlen(body_alloc);
    }

    run_options.tls_cert = tls_cert_alloc;
    run_options.tls_key = tls_key_alloc;

    if ((run_options.tls_cert == NULL) != (run_options.tls_key == NULL)) {
        efree(host_alloc);
        efree(body_alloc);
        efree(tls_cert_alloc);
        efree(tls_key_alloc);
        zend_value_error("Both 'tls_cert' and 'tls_key' must be set together");
        RETURN_THROWS();
    }

    if (php_h2o_server_run(&run_options) != SUCCESS) {
        efree(host_alloc);
        efree(body_alloc);
        efree(tls_cert_alloc);
        efree(tls_key_alloc);
        RETURN_FALSE;
    }

    efree(host_alloc);
    efree(body_alloc);
    efree(tls_cert_alloc);
    efree(tls_key_alloc);

    RETURN_TRUE;
}

PHP_MINFO_FUNCTION(h2o)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "h2o support", "enabled");
    php_info_print_table_row(2, "Version", PHP_H2O_VERSION);
    php_info_print_table_end();
}

static const zend_function_entry h2o_functions[] = {
    PHP_FE(h2o_server_run, arginfo_h2o_server_run)
    PHP_FE_END
};

zend_module_entry h2o_module_entry = {
    STANDARD_MODULE_HEADER,
    "h2o",
    h2o_functions,
    NULL,
    NULL,
    NULL,
    NULL,
    PHP_MINFO(h2o),
    PHP_H2O_VERSION,
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_H2O
# ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
# endif
ZEND_GET_MODULE(h2o)
#endif
