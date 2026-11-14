#!/bin/bash
# tools/split_leave_test.sh (Linux; needs Xvfb, unshare) <halo binary> <map> <split spec> <seconds> [shot-every-seconds]
# A local split screen game (loopback only, Internet play off) whose split screen
# players leave one by one (debug.network_test_split, e.g. "4:60" or "4:60:rejoin"),
# WIN=<w>x<h> sets the window (640x360 default), NULLR=true draws nothing (fast).
# under Xvfb, in a network namespace of its own (only its loopback), playing on for <seconds>. Prints the end of the log and any crash.
set -u
BIN=$(readlink -f "$1"); MAP=$2; SPLIT=$3; SECS=$4; SHOT=${5:-0}
D=:$((150 + RANDOM % 40)); B=$(mktemp -d /tmp/split-leave.XXXX)
mkdir -p $B/home $B/save $B/shots
~/emulators/new-data-root.sh $B/data >/dev/null 2>&1 || ~/halo-maps/new-data-root.sh $B/data >/dev/null
mkdir -p $B/data/logs
Xvfb $D -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 & XP=$!; sleep 1
( cd $B && unshare -rn sh -c 'ip link set lo up && exec "$@"' sh env DISPLAY=$D HOME=$B/home HALO_SAVE_ROOT=$B/save HALO_DATA_ROOT=$B/data \
  HALO_UPDATE_AUTO=false HALO_NET_JOIN_FROM_CLIPBOARD=false HALO_NET_ONLINE=false HALO_NET_LIST_GAMES=false \
  HALO_NET_HOST_PUBLIC=false HALO_NET_PUBLIC_LOBBY=false HALO_NET_ALLOW_UPNP=false HALO_NET_REPORT_GAMES=false \
  HALO_NET_ADDRESS=127.0.0.1 HALO_NET_BROADCAST=127.0.0.1 \
  ${NULLR:+HALO_NULL_RENDERER=1} HALO_DISPLAY_MODE=windowed HALO_WINDOW_SIZE=${WIN:-640x360} HALO_EXIT_AFTER=$SECS SDL_AUDIODRIVER=dummy MESA_SHADER_CACHE_DIR=${MESA_CACHE:-/tmp/split-leave-mesa-cache} \
  HALO_NETWORK_TEST=host:$MAP HALO_NETWORK_TEST_START=5 HALO_SOLO_GAME=1 HALO_NETWORK_TEST_SCORE=999 \
  HALO_NETWORK_TEST_SPLIT=$SPLIT HALO_NETWORK_TEST_KILL=15 HALO_TEST_INPUT=bot:7 \
  HALO_SCREENSHOT_DIR=$B/shots HALO_SCREENSHOT_EVERY=$SHOT \
  ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
  "$BIN" > $B/stdout.log 2>&1 ) & GP=$!
wait $GP; RC=$?; kill $XP 2>/dev/null
echo "exit $RC  log: $B"
grep -E "split screen player|ERROR: AddressSanitizer|SUMMARY|assert|halt|crash|exception" $B/stdout.log $B/data/logs/* $B/save/* 2>/dev/null | head -40
grep "network test: tick" $B/stdout.log | tail -1 | cut -c1-400
