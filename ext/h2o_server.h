#ifndef PHP_H2O_SERVER_H
#define PHP_H2O_SERVER_H

#include <stddef.h>
#include <stdint.h>

typedef struct _php_h2o_server_options {
    const char *host;
    uint16_t port;
    const char *response_body;
    size_t response_body_len;
    const char *tls_cert;
    const char *tls_key;
} php_h2o_server_options;

int php_h2o_server_run(const php_h2o_server_options *options);

#endif /* PHP_H2O_SERVER_H */
