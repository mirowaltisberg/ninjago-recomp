#!/bin/bash
# Builds the Simulator app (Unicorn backend), installs it in the booted Simulator, launches it with the game
# files from input/, waits SECONDS (default 25) and saves a screenshot to build/sim-shot.png.
set -euo pipefail
SECS="${1:-25}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
"$ROOT/tools/unicorn/build_unicorn_sim.sh"
(cd "$ROOT/ios" && xcodegen generate --quiet)
DEV=$(xcrun simctl list devices booted | grep -Eo '[0-9A-F-]{36}' | head -1)
[ -n "$DEV" ] || { echo "no booted Simulator"; exit 1; }
xcodebuild -project "$ROOT/ios/Ninjago.xcodeproj" -scheme Ninjago -sdk iphonesimulator -destination "id=$DEV" \
  -derivedDataPath "$ROOT/build/ios-dd" build > "$ROOT/build/ios-build.log" 2>&1 || { grep -E "error:" "$ROOT/build/ios-build.log" | head -20; exit 1; }
xcrun simctl install "$DEV" "$ROOT/build/ios-dd/Build/Products/Debug-iphonesimulator/Ninjago.app"
xcrun simctl terminate "$DEV" ch.mirowaltisberg.ninjago >/dev/null 2>&1 || true
export SIMCTL_CHILD_NJ_SO="$ROOT/input/extracted/lib/armeabi-v7a/libLEGO_M1.so"
export SIMCTL_CHILD_NJ_OBB="$ROOT/input/extracted/Android/Obb/com.lego.ninjago.toe/main.1057.com.lego.ninjago.toe.obb"
xcrun simctl launch --console-pty "$DEV" ch.mirowaltisberg.ninjago > "$ROOT/build/sim-run.log" 2>&1 &
LPID=$!
sleep "$SECS"
xcrun simctl io "$DEV" screenshot "$ROOT/build/sim-shot.png" >/dev/null 2>&1 || true
kill "$LPID" >/dev/null 2>&1 || true
echo "log: build/sim-run.log  screenshot: build/sim-shot.png"
