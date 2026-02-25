--TEST--
h2o_server_run returns false on TLS setup failure
--SKIPIF--
<?php
if (!extension_loaded('h2o')) {
    echo 'skip h2o extension not loaded';
}
?>
--INI--
display_errors=1
error_reporting=E_ALL
--FILE--
<?php
var_dump(h2o_server_run([
    'host' => '127.0.0.1',
    'port' => 18080,
    'tls_cert' => '/tmp/does-not-exist.crt',
    'tls_key' => '/tmp/does-not-exist.key',
]));
?>
--EXPECTF--
Warning: h2o_server_run(): TLS setup failed in %s on line %d
bool(false)
