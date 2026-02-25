--TEST--
h2o extension exposes h2o_server_run
--SKIPIF--
<?php
if (!extension_loaded('h2o')) {
    echo 'skip h2o extension not loaded';
}
?>
--FILE--
<?php
var_dump(function_exists('h2o_server_run'));
?>
--EXPECT--
bool(true)
