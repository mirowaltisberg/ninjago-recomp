#!/bin/bash
# Smoke test on a connected iPhone. Launches the installed app with a save in its temporary folder (NJ_SANDBOX, the
# player's own save is never touched), taps through the menus (NJ_TAPS) and reads the console (NJ_CHEAT_LOG).
# NJ_KEEP_AWAKE keeps the phone from locking during the run; it only has to be unlocked when the test starts.
#   tools/device/smoke.sh [--fight SAVE] [DEVICE_ID] [SECONDS]
#   exit 0 pass, 1 fail, 3 phone locked (before or during the run)
# Default: a fresh save, main menu -> dojo; passes on boot, the dojo's player and >= 55 fps.
# --fight SAVE: SAVE (a savegame.dat past the tutorial, e.g. copied from the Simulator) is copied into the app's
# temporary folder and used as the starting save, Invincible is switched on for this run only (NJ_OPTIONS), and the
# taps go main menu -> map -> character select -> extras -> Chen's Island Arena. Passes when enemies hit the player,
# every hit is refilled, health never reaches 0 and the frame rate is >= 55 fps.
# Tap positions are game pixels for a 2622x1206 screen (iPhone 17); divided by 3 they drive the Simulator through
# tools/sim/run_sim.sh with the same NJ_* variables.
set -uo pipefail
SAVE=""
if [ "${1:-}" = "--fight" ]; then SAVE="${2:-}"; shift 2; [ -f "$SAVE" ] || { echo "no save file $SAVE"; exit 1; }; fi
DEV="${1:-$(xcrun devicectl list devices 2>/dev/null | awk '/physical/ && /available/ {for (i=1;i<=NF;i++) if ($i ~ /^[0-9A-F]{8}-[0-9A-F]{16}$/) print $i}' | head -1)}"
[ -n "$DEV" ] || { echo "no connected iPhone"; exit 1; }
BUNDLE=ch.mirowaltisberg.ninjago
if [ -n "$SAVE" ]; then
  SECS="${2:-100}"
  TAPS="900,1311,1050;1200,1311,1050;1700,2448,1056;2200,2448,1089;2700,2448,1089;3200,1311,582"
  if ! xcrun devicectl device copy to --device "$DEV" --source "$SAVE" --destination tmp/nj-seed.dat \
      --domain-type appDataContainer --domain-identifier "$BUNDLE" --quiet > /dev/null 2>&1; then
    echo "FAIL: could not copy the save to the phone (locked?)"; exit 3
  fi
  ENV="{\"NJ_KEEP_AWAKE\":\"1\",\"NJ_SANDBOX\":\"fight\",\"NJ_SAVE_SEED\":\"nj-seed.dat\",\"NJ_OPTIONS\":\"invincible\",\"NJ_CHEAT_LOG\":\"1\",\"NJ_TAPS\":\"$TAPS\"}"
else
  SECS="${2:-75}"
  TAPS="900,1311,1050;1200,1311,1050;1600,1311,582;1900,1311,582;2300,1311,582"
  ENV="{\"NJ_KEEP_AWAKE\":\"1\",\"NJ_SANDBOX\":\"smoke\",\"NJ_CHEAT_LOG\":\"1\",\"NJ_TAPS\":\"$TAPS\"}"
fi
LOG="$(mktemp -t nj-smoke)"
xcrun devicectl device process launch --device "$DEV" --terminate-existing --console -e "$ENV" "$BUNDLE" > "$LOG" 2>&1 &
PID=$!
for _ in $(seq 1 "$SECS"); do
  grep -q "Locked" "$LOG" && break
  sleep 1
done
kill "$PID" 2>/dev/null
wait "$PID" 2>/dev/null
if grep -q "Locked" "$LOG"; then echo "FAIL: the phone is locked"; rm -f "$LOG"; exit 3; fi
if grep -q "\[ninjago\] paused" "$LOG"; then echo "FAIL: the app went to the background (phone locked during the run)"; rm -f "$LOG"; exit 3; fi
boot=$(grep -o "boot done in [0-9.]* s" "$LOG" | head -1)
player=$(grep -m1 "cheats\] player 0" "$LOG")
fps=$(grep "fps," "$LOG" | tail -1 | sed -E 's/.*\] ([0-9.]+) fps, ([0-9.]+) ms.*/\1 \2/')
echo "boot:   ${boot:-none}"
echo "player: ${player:-none}"
echo "frame:  ${fps:-none} (fps, ms of CPU per frame)"
ok=1
{ [ -n "$boot" ] && [ -n "$player" ] && [ -n "$fps" ] && awk -v f="${fps%% *}" 'BEGIN { exit !(f >= 55) }'; } || ok=0
if [ -n "$SAVE" ]; then
  hits=$(grep -E "cheats\] player [0-9a-f]+ health [0-9]+ of [0-9]+" "$LOG" | awk '$5 != $7' | wc -l | tr -d ' ')
  refills=$(grep -c "cheats\] player health [0-9]* -> " "$LOG")
  zero=$(grep -cE "cheats\] player [0-9a-f]+ health 0 of" "$LOG")
  echo "fight:  $hits hits, $refills refills, $zero times at 0 health"
  { [ "$hits" -gt 0 ] && [ "$hits" -eq "$refills" ] && [ "$zero" -eq 0 ]; } || ok=0
fi
[ "$ok" = 1 ] && { echo "PASS"; rm -f "$LOG"; exit 0; }
echo "FAIL (console log: $LOG)"
exit 1
