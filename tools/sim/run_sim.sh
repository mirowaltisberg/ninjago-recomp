#!/bin/bash
# Builds the Simulator app (recompiled guest code), installs it in the booted Simulator and launches it with the game
# files from input/. With SECONDS > 0: waits, saves build/sim-shot.png and stops the app.
# With SECONDS = 0: leaves the game running to play with the mouse (open -a Simulator).
# Optional NJ_TAPS="frame,x,y;frame,x,y" (pixels) injects taps (developer testing); other NJ_* variables are
# passed to the app (see GameViewController.m and hle_gles.c).
set -euo pipefail
SECS="${1:-25}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cmake -S "$ROOT" -B "$ROOT/build" -G Ninja > /dev/null && cmake --build "$ROOT/build" --target nj_guest_aot > /dev/null # gen/
(cd "$ROOT/ios" && xcodegen generate --quiet)
DEV="${SIM_DEVICE:-$(xcrun simctl list devices booted | grep -Eo '[0-9A-F-]{36}' | head -1)}" # SIM_DEVICE=<udid> picks one of several
[ -n "$DEV" ] || { echo "no booted Simulator"; exit 1; }
xcodebuild -project "$ROOT/ios/Ninjago.xcodeproj" -scheme Ninjago -sdk iphonesimulator -destination "id=$DEV" \
  -derivedDataPath "$ROOT/build/ios-dd" build > "$ROOT/build/ios-build.log" 2>&1 || { grep -E "error:" "$ROOT/build/ios-build.log" | head -20; exit 1; }
xcrun simctl install "$DEV" "$ROOT/build/ios-dd/Build/Products/Debug-iphonesimulator/Ninjago.app"
xcrun simctl terminate "$DEV" ch.mirowaltisberg.ninjago >/dev/null 2>&1 || true
export SIMCTL_CHILD_NJ_SO="$ROOT/input/extracted/lib/armeabi-v7a/libLEGO_M1.so"
export SIMCTL_CHILD_NJ_OBB="$ROOT/input/extracted/Android/Obb/com.lego.ninjago.toe/main.1057.com.lego.ninjago.toe.obb"
export SIMCTL_CHILD_NJ_TAPS="${NJ_TAPS:-}"
# pass other NJ_* developer switches (NJ_RENDER_SCALE, NJ_GL_SKIP, NJ_GL_STATS, NJ_SHADER_DUMP_DIR, NJ_TRACE) through
for v in $(compgen -e | grep -E '^NJ_' | grep -vE '^NJ_(TAPS|SO|OBB)$'); do export "SIMCTL_CHILD_$v=${!v}"; done
if [ "$SECS" = 0 ]; then
  xcrun simctl launch "$DEV" ch.mirowaltisberg.ninjago >/dev/null
  open "$(xcode-select -p)/Applications/Simulator.app" 2>/dev/null || open -a Simulator 2>/dev/null || true
  echo "running in the Simulator"
  exit 0
fi
xcrun simctl launch --console-pty "$DEV" ch.mirowaltisberg.ninjago > "$ROOT/build/sim-run.log" 2>&1 &
LPID=$!
sleep "$SECS"
xcrun simctl io "$DEV" screenshot "$ROOT/build/sim-shot.png" >/dev/null 2>&1 || true
kill "$LPID" >/dev/null 2>&1 || true
xcrun simctl terminate "$DEV" ch.mirowaltisberg.ninjago >/dev/null 2>&1 || true
echo "log: build/sim-run.log  screenshot: build/sim-shot.png"
