#!/usr/bin/env bash
# Builds a dedicated i686-elf cross-compiler (binutils + GCC, C only,
# no host libc linkage), per docs/FOUNDING-PROPOSAL.md §4.2.
#
# This deliberately does NOT use the host's own gcc -m32 to build ManiOS:
# a real i686-elf cross-compiler has no hosted-environment assumptions
# baked into its default specs (no PIE, no stack-protector runtime
# dependency, no host libc/headers reachable by accident), which matters
# more as the kernel grows than it does for a single boot.S file.
#
# Usage: tools/toolchain/build-i686-elf-toolchain.sh
# Installs into tools/toolchain/i686-elf/ (git-ignored). Re-run is safe;
# it skips steps whose output already exists.

set -euo pipefail

BINUTILS_VERSION=2.42
GCC_VERSION=13.2.0
TARGET=i686-elf

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="$SCRIPT_DIR/i686-elf"
BUILD_DIR="$SCRIPT_DIR/build"
SRC_DIR="$BUILD_DIR/src"

export PATH="$PREFIX/bin:$PATH"

mkdir -p "$SRC_DIR"
cd "$SRC_DIR"

fetch() {
  local url="$1" out="$2"
  if [ ! -f "$out" ]; then
    echo "==> Fetching $out"
    curl -fL --retry 4 --retry-delay 2 -o "$out" "$url"
  fi
}

fetch "https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VERSION}.tar.xz" \
      "binutils-${BINUTILS_VERSION}.tar.xz"
fetch "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz" \
      "gcc-${GCC_VERSION}.tar.xz"

[ -d "binutils-${BINUTILS_VERSION}" ] || tar xf "binutils-${BINUTILS_VERSION}.tar.xz"
[ -d "gcc-${GCC_VERSION}" ] || tar xf "gcc-${GCC_VERSION}.tar.xz"

# --- binutils ---
if [ ! -x "$PREFIX/bin/${TARGET}-ld" ]; then
  echo "==> Building binutils ${BINUTILS_VERSION} for ${TARGET}"
  mkdir -p "$BUILD_DIR/binutils-build"
  cd "$BUILD_DIR/binutils-build"
  "$SRC_DIR/binutils-${BINUTILS_VERSION}/configure" \
    --target="$TARGET" --prefix="$PREFIX" \
    --with-sysroot --disable-nls --disable-werror
  make -j"$(nproc)"
  make install
fi

# --- gcc (C only, no libc — matches --without-headers freestanding target) ---
if [ ! -x "$PREFIX/bin/${TARGET}-gcc" ]; then
  echo "==> Building gcc ${GCC_VERSION} for ${TARGET}"
  cd "$SRC_DIR/gcc-${GCC_VERSION}"
  ./contrib/download_prerequisites
  mkdir -p "$BUILD_DIR/gcc-build"
  cd "$BUILD_DIR/gcc-build"
  "$SRC_DIR/gcc-${GCC_VERSION}/configure" \
    --target="$TARGET" --prefix="$PREFIX" \
    --disable-nls --enable-languages=c --without-headers \
    --disable-shared --disable-threads --disable-libssp \
    --disable-libgomp --disable-libquadmath
  make -j"$(nproc)" all-gcc all-target-libgcc
  make install-gcc install-target-libgcc
fi

echo "==> ${TARGET} cross-compiler ready at $PREFIX/bin"
"$PREFIX/bin/${TARGET}-gcc" --version | head -1
