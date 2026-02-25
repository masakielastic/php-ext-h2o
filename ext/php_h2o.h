#ifndef PHP_H2O_H
#define PHP_H2O_H

extern zend_module_entry h2o_module_entry;
#define phpext_h2o_ptr &h2o_module_entry

#define PHP_H2O_VERSION "0.1.0"

PHP_FUNCTION(h2o_server_run);

#endif /* PHP_H2O_H */
