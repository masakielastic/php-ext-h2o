--TEST--
h2o_server_run requires tls_cert and tls_key together
--SKIPIF--
<?php
if (!extension_loaded('h2o')) {
    echo 'skip h2o extension not loaded';
}
?>
--FILE--
<?php
try {
    h2o_server_run(['tls_cert' => '/tmp/server.crt']);
    echo "NO_EXCEPTION\n";
} catch (ValueError $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}
?>
--EXPECT--
ValueError: Both 'tls_cert' and 'tls_key' must be set together
