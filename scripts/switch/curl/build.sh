#!/bin/bash
# Recompila o libcurl 8.16.0 para o Switch contra o mbedTLS recompilado por
# scripts/switch/mbedtls/build.sh (rode aquele primeiro). Sem patches --
# so precisa ser religado contra a versao nova do mbedTLS. Rode dentro de
# um shell MSYS puro do devkitPro (mesmo shell do script do mbedtls).
set -e

PKGVER=8.16.0
WORKDIR=/tmp/curl-build
mkdir -p "$WORKDIR"
cd "$WORKDIR"

[ -f "curl-${PKGVER}.tar.xz" ] || \
    curl -L -o "curl-${PKGVER}.tar.xz" "https://curl.haxx.se/download/curl-${PKGVER}.tar.xz"

rm -rf "curl-${PKGVER}"
tar xJf "curl-${PKGVER}.tar.xz"
cd "curl-${PKGVER}"

export PATH=/opt/devkitpro/devkitA64/bin:/opt/devkitpro/portlibs/switch/bin:$PATH
source /opt/devkitpro/switchvars.sh
LDFLAGS="-specs=${DEVKITPRO}/libnx/switch.specs ${LDFLAGS}"

autoreconf -fi
./configure --prefix="$PORTLIBS_PREFIX" --host=aarch64-none-elf \
    --disable-shared --enable-static --disable-ipv6 --disable-unix-sockets \
    --disable-manual --disable-threaded-resolver --disable-progress-meter \
    --without-zstd --without-brotli --without-libpsl --enable-websockets \
    --with-mbedtls --with-default-ssl-backend=mbedtls
sed -i '/HAVE_SOCKETPAIR/d' lib/curl_config.h

make -C lib -j"$(nproc)"
make -C lib install
make -C include install
make install-binSCRIPTS install-pkgconfigDATA
