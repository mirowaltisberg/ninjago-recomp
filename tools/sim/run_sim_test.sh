#!/bin/bash
# Builds tests/c/NAME.c (or NAME.m) for the arm64 iOS Simulator (runtime + Unicorn backend) and runs it in the booted Simulator.
set -euo pipefail
NAME="$1"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
"$ROOT/tools/unicorn/build_unicorn_sim.sh"
UC="$ROOT/build/unicorn-sim"
OUT="$ROOT/build/sim-tests"
mkdir -p "$OUT/obj"
CC=(xcrun -sdk iphonesimulator clang -arch arm64 -mios-simulator-version-min=26.0 -O1 -g -DGLES_SILENCE_DEPRECATION=1)
INC=(-I"$ROOT/runtime/include" -I"$ROOT/runtime/third_party" -I"$UC/include" -I"$ROOT/tests/c")
rm -f "$OUT"/obj/*.o "$OUT/librt.a"
for f in "$ROOT"/runtime/src/*.c "$ROOT/runtime/backend/unicorn/uc_backend.c" "$ROOT"/runtime/headless/*.c \
         "$ROOT/runtime/third_party/dlmalloc/malloc.c" "$ROOT/runtime/third_party/minimp3/minimp3_impl.c"; do
  b=$(basename "$f" .c); extra=()
  case "$b" in
    malloc) extra=(-w -DONLY_MSPACES=1 -DMSPACES=1 -DUSE_LOCKS=1 -DHAVE_MMAP=0 -DHAVE_MORECORE=0) ;;
    minimp3_impl|gles_stub) extra=(-w) ;;
  esac
  [ "$b" = gles_stub ] && continue # the Simulator has real OpenGL ES
  "${CC[@]}" -c "$f" -o "$OUT/obj/$b.o" "${INC[@]}" ${extra[@]+"${extra[@]}"}
done
ar rcs "$OUT/librt.a" "$OUT"/obj/*.o
SRC="$ROOT/tests/c/$NAME.c"; objc=()
[ -f "$ROOT/tests/c/$NAME.m" ] && { SRC="$ROOT/tests/c/$NAME.m"; objc=(-fobjc-arc); }
"${CC[@]}" ${objc[@]+"${objc[@]}"} -o "$OUT/$NAME" "$SRC" "$ROOT/tests/c/nj_test_main.c" "${INC[@]}" \
  -DNJ_GUEST_SO_PATH="\"$ROOT/input/extracted/lib/armeabi-v7a/libLEGO_M1.so\"" \
  -DNJ_GUEST_OBB_PATH="\"$ROOT/input/extracted/Android/Obb/com.lego.ninjago.toe/main.1057.com.lego.ninjago.toe.obb\"" \
  "$OUT/librt.a" "$UC/lib/libunicorn.a" "$UC/lib/libarm-softmmu.a" "$UC/lib/libunicorn-common.a" \
  -framework CoreFoundation -framework OpenGLES
codesign -s - -f "$OUT/$NAME" >/dev/null 2>&1
DEV=$(xcrun simctl list devices booted | grep -Eo '[0-9A-F-]{36}' | head -1)
[ -n "$DEV" ] || { echo "no booted Simulator"; exit 1; }
xcrun simctl spawn "$DEV" "$OUT/$NAME"
