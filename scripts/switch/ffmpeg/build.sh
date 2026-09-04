#!/bin/bash
# Recompila o ffmpeg 7.1.5 para o Switch com decodificacao de video por
# hardware (aceleracao "nvtegra", usando o motor de video dedicado do
# Tegra X1 em vez da CPU) e religado contra o mbedTLS recompilado por
# scripts/switch/mbedtls/build.sh (rode aquele -- e o do curl -- primeiro).
# Rode dentro de um shell MSYS puro do devkitPro.
set -e
cd "$(dirname "$0")"

PKGVER=7.1.5
WORKDIR=/tmp/ffmpeg-build
mkdir -p "$WORKDIR"
cd "$WORKDIR"

[ -f "ffmpeg-${PKGVER}.tar.xz" ] || \
    curl -L -o "ffmpeg-${PKGVER}.tar.xz" "https://ffmpeg.org/releases/ffmpeg-${PKGVER}.tar.xz"

rm -rf "ffmpeg-${PKGVER}"
tar xJf "ffmpeg-${PKGVER}.tar.xz"
cd "ffmpeg-${PKGVER}"

PATCH_DIR="$OLDPWD/../../../scripts/switch/ffmpeg"
patch -Np1 -i "$PATCH_DIR/ffmpeg-7.1.5.patch"
patch -Np1 -i "$PATCH_DIR/getnameinfo.patch"
patch -Np1 -i "$PATCH_DIR/avio.patch"

# devkitPro's own toolchain has no usable host (build-machine) C compiler in
# a plain MSYS shell -- ffmpeg's ./configure needs one anyway for a couple
# of its own build-time tools, and mingw64's gcc works fine for that.
export PATH=/opt/devkitpro/devkitA64/bin:/opt/devkitpro/portlibs/switch/bin:/opt/devkitpro/msys2/mingw64/bin:$PATH
source /opt/devkitpro/switchvars.sh

./configure --prefix="$PORTLIBS_PREFIX" --enable-gpl --disable-shared --enable-static \
    --cross-prefix=aarch64-none-elf- --enable-cross-compile \
    --host-cc=/opt/devkitpro/msys2/mingw64/bin/gcc.exe \
    --arch=aarch64 --cpu=cortex-a57 --target-os=horizon --enable-pic \
    --extra-cflags="-D__SWITCH__ -D_GNU_SOURCE -O2 -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec" \
    --extra-cxxflags="-D__SWITCH__ -D_GNU_SOURCE -O2 -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec" \
    --extra-ldflags="-fPIE -L${PORTLIBS_PREFIX}/lib -L${DEVKITPRO}/libnx/lib" \
    --disable-runtime-cpudetect --disable-programs --disable-debug --disable-doc \
    --enable-asm --enable-neon --disable-autodetect --enable-mbedtls --enable-version3 \
    --disable-avdevice --disable-encoders --disable-muxers \
    --enable-swscale --enable-swresample --enable-network --enable-libssh2 \
    --enable-zlib --enable-bzlib --enable-libass --enable-libdav1d --enable-nvtegra \
    --disable-filters --enable-filter=hflip,vflip,transpose

make -j"$(nproc)"
make install
rm -rf "${PORTLIBS_PREFIX}/share/ffmpeg"
