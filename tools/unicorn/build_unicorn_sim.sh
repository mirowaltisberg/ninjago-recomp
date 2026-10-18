#!/bin/bash
# Builds Unicorn 2.1.4 (ARM guest only) as static libraries for the arm64 iOS Simulator into build/unicorn-sim/.
# Applies tools/unicorn/sim-jit.patch (resolves the JIT write-protect API at runtime in the Simulator).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/unicorn-sim"
VER=2.1.4
SHA=ea8863f095a0136388694e5a6063afd9bb7650e30243dd6251af59c5ce5601f4
[ -f "$OUT/lib/libunicorn.a" ] && [ "${1:-}" != "--force" ] && { echo "unicorn-sim already built: $OUT"; exit 0; }
rm -rf "$OUT" && mkdir -p "$OUT/src" "$OUT/lib"
TAR="$OUT/unicorn-$VER.tar.gz"
curl -fsSL -o "$TAR" "https://github.com/unicorn-engine/unicorn/archive/refs/tags/$VER.tar.gz"
echo "$SHA  $TAR" | shasum -a 256 -c - >/dev/null
tar -xzf "$TAR" -C "$OUT/src"
SRC="$OUT/src/unicorn-$VER"
if [ -f "$ROOT/tools/unicorn/sim-jit.patch" ]; then (cd "$SRC" && patch -p1 -s < "$ROOT/tools/unicorn/sim-jit.patch"); fi
cmake -S "$SRC" -B "$OUT/cmake" -G Ninja -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 -DUNICORN_ARCH=arm -DBUILD_SHARED_LIBS=OFF \
  -DUNICORN_BUILD_TESTS=OFF -DUNICORN_INSTALL=OFF -DCMAKE_BUILD_TYPE=Release > "$OUT/configure.log"
cmake --build "$OUT/cmake" > "$OUT/build.log" 2>&1 || { grep -m3 " error" "$OUT/build.log"; exit 1; }
cp "$OUT"/cmake/libunicorn.a "$OUT"/cmake/libarm-softmmu.a "$OUT"/cmake/libunicorn-common.a "$OUT/lib/"
cp -R "$SRC/include" "$OUT/include"
echo "built $OUT/lib"
