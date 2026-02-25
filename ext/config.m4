PHP_ARG_ENABLE([h2o],
  [whether to enable h2o extension],
  [AS_HELP_STRING([--enable-h2o], [Enable h2o extension])],
  [yes])

PHP_ARG_WITH([h2o-dir],
  [for h2o installation prefix],
  [AS_HELP_STRING([--with-h2o-dir=DIR], [h2o installation prefix])],
  [${HOME}/.local],
  [no])

PHP_ARG_WITH([libuv-dir],
  [for libuv installation prefix],
  [AS_HELP_STRING([--with-libuv-dir=DIR], [libuv installation prefix])],
  [/usr],
  [no])

PHP_ARG_WITH([openssl-dir],
  [for OpenSSL installation prefix],
  [AS_HELP_STRING([--with-openssl-dir=DIR], [OpenSSL installation prefix])],
  [/usr],
  [no])

PHP_ARG_ENABLE([h2o-debug],
  [whether to enable h2o debug logs],
  [AS_HELP_STRING([--enable-h2o-debug], [Enable h2o extension debug logs])],
  [no],
  [no])

if test "$PHP_H2O" != "no"; then
  if test -z "$PHP_H2O_DIR" || test "$PHP_H2O_DIR" = "yes"; then
    PHP_H2O_DIR="${HOME}/.local"
  fi

  if test -z "$PHP_LIBUV_DIR" || test "$PHP_LIBUV_DIR" = "yes"; then
    PHP_LIBUV_DIR="/usr"
  fi

  if test -z "$PHP_OPENSSL_DIR" || test "$PHP_OPENSSL_DIR" = "yes"; then
    PHP_OPENSSL_DIR="/usr"
  fi

  PHP_ADD_INCLUDE([$PHP_H2O_DIR/include])
  PHP_ADD_INCLUDE([$PHP_LIBUV_DIR/include])
  PHP_ADD_INCLUDE([$PHP_OPENSSL_DIR/include])
  CPPFLAGS="$CPPFLAGS -I$PHP_H2O_DIR/include -I$PHP_LIBUV_DIR/include -I$PHP_OPENSSL_DIR/include"
  LDFLAGS="$LDFLAGS -L$PHP_H2O_DIR/lib -L$PHP_LIBUV_DIR/lib -L$PHP_OPENSSL_DIR/lib"

  AC_CHECK_HEADER([h2o.h], [], [AC_MSG_ERROR([h2o.h not found under $PHP_H2O_DIR/include])])
  AC_CHECK_HEADER([uv.h], [], [
    AC_CHECK_HEADER([libuv.h], [], [AC_MSG_ERROR([uv.h or libuv.h not found under $PHP_LIBUV_DIR/include])])
  ])
  AC_CHECK_HEADER([openssl/ssl.h], [], [AC_MSG_ERROR([openssl/ssl.h not found under $PHP_OPENSSL_DIR/include])])

  PHP_ADD_LIBRARY_WITH_PATH([h2o], [$PHP_H2O_DIR/lib], [H2O_SHARED_LIBADD])
  PHP_ADD_LIBRARY_WITH_PATH([uv], [$PHP_LIBUV_DIR/lib], [H2O_SHARED_LIBADD])
  PHP_ADD_LIBRARY_WITH_PATH([ssl], [$PHP_OPENSSL_DIR/lib], [H2O_SHARED_LIBADD])
  PHP_ADD_LIBRARY_WITH_PATH([crypto], [$PHP_OPENSSL_DIR/lib], [H2O_SHARED_LIBADD])
  PHP_ADD_LIBRARY([m], [], [H2O_SHARED_LIBADD])

  if test ! -f "$PHP_H2O_DIR/lib/libh2o.a" && test ! -f "$PHP_H2O_DIR/lib/libh2o.so"; then
    AC_MSG_ERROR([libh2o.a or libh2o.so not found under $PHP_H2O_DIR/lib])
  fi

  if test ! -f "$PHP_H2O_DIR/lib/libh2o.so" && test -f "$PHP_H2O_DIR/lib/libh2o.a"; then
    AC_MSG_WARN([libh2o.so is missing; linking a PHP shared extension against static libh2o.a requires a PIC build (-fPIC)])
  fi

  PHP_CHECK_LIBRARY([uv], [uv_run], [],
    [AC_MSG_ERROR([libuv library not found under $PHP_LIBUV_DIR/lib])],
    [-L$PHP_LIBUV_DIR/lib])

  PHP_CHECK_LIBRARY([ssl], [SSL_CTX_new], [],
    [AC_MSG_ERROR([OpenSSL ssl library not found under $PHP_OPENSSL_DIR/lib])],
    [-L$PHP_OPENSSL_DIR/lib])

  PHP_CHECK_LIBRARY([crypto], [OPENSSL_init_crypto], [],
    [AC_MSG_ERROR([OpenSSL crypto library not found under $PHP_OPENSSL_DIR/lib])],
    [-L$PHP_OPENSSL_DIR/lib])

  if test "$PHP_H2O_DEBUG" = "yes"; then
    AC_DEFINE([PHP_H2O_DEBUG], [1], [Enable debug logging for h2o extension])
  fi

  PHP_SUBST([H2O_SHARED_LIBADD])
  PHP_NEW_EXTENSION([h2o], [h2o.c h2o_server.c], [$ext_shared])
fi
