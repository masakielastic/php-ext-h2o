--TEST--
h2o_server_run rejects non-int port
--SKIPIF--
<?php
if (!extension_loaded('h2o')) {
    echo 'skip h2o extension not loaded';
}
?>
--FILE--
<?php
try {
    h2o_server_run(['port' => '8080']);
    echo "NO_EXCEPTION\n";
} catch (TypeError $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}
?>
--EXPECT--
TypeError: Option 'port' must be int
