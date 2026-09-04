#!/bin/bash
# Recompila o mbedTLS 3.6.5 para o Switch com uma fonte de entropia baseada
# em hardware (a stock switch-mbedtls do devkitPro nao tem uma equivalente
# funcional, o que faz o handshake TLS do mpv/ffmpeg travar ao tentar abrir
# um stream HTTPS). Rode dentro de um shell MSYS puro do devkitPro:
#
#   env -i MSYSTEM=MSYS HOME="$HOME" /c/devkitPro/msys2/usr/bin/bash.exe --login
#   bash scripts/switch/mbedtls/build.sh
set -e
cd "$(dirname "$0")"

PKGVER=3.6.5
WORKDIR=/tmp/mbedtls-build
mkdir -p "$WORKDIR"
cd "$WORKDIR"

[ -f "mbedtls-${PKGVER}.tar.bz2" ] || \
    curl -L -o "mbedtls-${PKGVER}.tar.bz2" \
        "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-${PKGVER}/mbedtls-${PKGVER}.tar.bz2"

rm -rf "mbedtls-${PKGVER}"
tar xjf "mbedtls-${PKGVER}.tar.bz2"
cd "mbedtls-${PKGVER}"
patch -Np1 -i "$OLDPWD/../../../scripts/switch/mbedtls/mbedtls-3.6.5.patch"

export PATH=/opt/devkitpro/devkitA64/bin:/opt/devkitpro/portlibs/switch/bin:$PATH
source /opt/devkitpro/switchvars.sh

aarch64-none-elf-cmake \
    -DCMAKE_INSTALL_PREFIX="$PORTLIBS_PREFIX" \
    -DCMAKE_C_FLAGS="$CFLAGS $CPPFLAGS -fzero-init-padding-bits=unions" \
    -DUSE_SHARED_MBEDTLS_LIBRARY=OFF \
    -DUSE_STATIC_MBEDTLS_LIBRARY=ON \
    -DMBEDTLS_FATAL_WARNINGS=OFF \
    -DENABLE_PROGRAMS=OFF \
    -DENABLE_TESTING=OFF \
    -DCMAKE_DEPENDS_USE_COMPILER=OFF \
    .

make -j"$(nproc)"
make install
