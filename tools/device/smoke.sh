#!/bin/bash
# Smoke test on a connected iPhone: launches the installed app with a fresh save in its temporary folder
# (NJ_SANDBOX, the player's own save is not touched), taps through the main menu into the dojo (NJ_TAPS) and checks
# the console: boot finished, the dojo's player exists (NJ_CHEAT_LOG) and the last frame-rate report is >= 55 fps.
#   tools/device/smoke.sh [DEVICE_ID] [SECONDS]
# The phone must be unlocked. Tap positions are game pixels for a 2622x1206 screen (iPhone 17); the same
# sequence divided by 3 drives the Simulator: NJ_SANDBOX=t NJ_CHEAT_LOG=1 NJ_TAPS=... tools/sim/run_sim.sh 50
set -uo pipefail
DEV="${1:-$(xcrun devicectl list devices 2>/dev/null | awk '/physical/ && /available/ {for (i=1;i<=NF;i++) if ($i ~ /^[0-9A-F]{8}-[0-9A-F]{16}$/) print $i}' | head -1)}"
SECS="${2:-75}"
[ -n "$DEV" ] || { echo "no connected iPhone"; exit 1; }
TAPS="900,1311,1050;1200,1311,1050;1600,1311,582;1900,1311,582;2300,1311,582"
LOG="$(mktemp -t nj-smoke)"
xcrun devicectl device process launch --device "$DEV" --terminate-existing --console \
  -e "{\"NJ_SANDBOX\":\"smoke\",\"NJ_CHEAT_LOG\":\"1\",\"NJ_TAPS\":\"$TAPS\"}" ch.mirowaltisberg.ninjago > "$LOG" 2>&1 &
PID=$!
sleep "$SECS"
kill "$PID" 2>/dev/null
wait "$PID" 2>/dev/null
if grep -q "Locked" "$LOG"; then echo "FAIL: the phone is locked"; exit 1; fi
boot=$(grep -o "boot done in [0-9.]* s" "$LOG" | head -1)
player=$(grep -m1 "cheats\] player 0" "$LOG")
fps=$(grep "fps," "$LOG" | tail -1 | sed -E 's/.*\] ([0-9.]+) fps, ([0-9.]+) ms.*/\1 \2/')
echo "boot:   ${boot:-none}"
echo "player: ${player:-none}"
echo "frame:  ${fps:-none} (fps, ms of CPU per frame)"
[ -n "$boot" ] && [ -n "$player" ] && [ -n "$fps" ] && awk -v f="${fps%% *}" 'BEGIN { exit !(f >= 55) }' \
  && { echo "PASS"; rm -f "$LOG"; exit 0; }
echo "FAIL (console log: $LOG)"
exit 1
