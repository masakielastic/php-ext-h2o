# php-ext-h2o

Minimal PHP extension that embeds an `h2o`-based HTTP server.

## Current status
- Extension name: `h2o`
- Main API: `h2o_server_run(array $options = null): bool`
- Build path for PIE: `ext`

## Requirements
- PHP development tools (`phpize`, headers)
- `h2o` shared library installed under `$HOME/.local`
  - required: `$HOME/.local/include/h2o.h`
  - required: `$HOME/.local/lib/libh2o.so`
- libuv and OpenSSL from Debian packages
  - headers: `/usr/include/uv.h` (or `/usr/include/libuv.h`)
  - libs: `/usr/lib/x86_64-linux-gnu/libuv.so`, `libssl.so`, `libcrypto.so`

## Build (manual)
```bash
cd ext
phpize
./configure --enable-h2o --with-h2o-dir=$HOME/.local --with-libuv-dir=/usr --with-openssl-dir=/usr
make -j"$(nproc)"
```

## Load extension
```bash
php -n -d extension=$(pwd)/ext/modules/h2o.so -r 'var_dump(function_exists("h2o_server_run"));'
```

## API
```php
h2o_server_run(array $options = null): bool
```

Supported options:
- `host` (string, default `0.0.0.0`)
- `port` (int, default `8080`)
- `response_body` (string, default `"OK\\n"`)
- `tls_cert` (string, optional)
- `tls_key` (string, optional)

Notes:
- `tls_cert` and `tls_key` must be set together.
- The function blocks while the server loop is running.
- Stop with `Ctrl+C` (SIGINT).

## PIE
```bash
pie install masakielastic/h2o
```

`composer.json` is configured as `type: php-ext` with `php-ext.build-path: ext`.

In non-interactive environments, PIE can finish build but fail at the final enable step if `sudo` prompt is required.

## Tests
```bash
cd ext
phpize
./configure --enable-h2o --with-h2o-dir=$HOME/.local
make -j"$(nproc)"
REPORT_EXIT_STATUS=1 NO_INTERACTION=1 make test TESTS='tests/*.phpt'
```
