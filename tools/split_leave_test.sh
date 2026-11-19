#!/bin/bash
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
#
# tools/split_leave_test.sh <halo binary> <map> <players> <leave> <seconds> [shot-every-seconds]
# (Linux; needs Xvfb and unshare)
#
# A split screen game hosted on one machine, <players> on it (2 to 4,
# debug.network_test_local_players), whose players leave one by one, the last
# first, every <leave> seconds (debug.network_test_split_leave: "60", or
# "60:rejoin" to have each join again half that later). It plays on for
# <seconds> under Xvfb, in a network namespace of its own (only its loopback,
# Internet play off), with its own HOME, save and data folders. Prints the
# end of the log and any crash or assertion.
# WIN=<w>x<h> sets the window (640x360 default); NULLR=true draws nothing (fast).
# DATA_ROOT_SCRIPT names the script that makes a data folder with the maps
# (default ~/halo-maps/new-data-root.sh).
set -u
if [ $# -lt 5 ]; then
	sed -n '5,17p' "$0"
	exit 2
fi
BIN=$(readlink -f "$1"); MAP=$2; PLAYERS=$3; LEAVE=$4; SECS=$5; SHOT=${6:-0}
D=:$((150 + RANDOM % 40)); B=$(mktemp -d /tmp/split-leave.XXXX)
mkdir -p "$B/home" "$B/save" "$B/shots"
if ! "${DATA_ROOT_SCRIPT:-$HOME/halo-maps/new-data-root.sh}" "$B/data" >/dev/null; then
	echo "$0: no data folder (DATA_ROOT_SCRIPT)" >&2
	exit 2
fi
mkdir -p "$B/data/logs"
Xvfb $D -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 & XP=$!
sleep 1
( cd "$B" && unshare -rn sh -c 'ip link set lo up && exec "$@"' sh env DISPLAY=$D HOME="$B/home" \
  HALO_SAVE_ROOT="$B/save" HALO_DATA_ROOT="$B/data" \
  HALO_UPDATE_AUTO=false HALO_NET_JOIN_FROM_CLIPBOARD=false HALO_NET_ONLINE=false HALO_NET_LIST_GAMES=false \
  HALO_NET_HOST_PUBLIC=false HALO_NET_PUBLIC_LOBBY=false HALO_NET_ALLOW_UPNP=false HALO_NET_REPORT_GAMES=false \
  HALO_NET_ADDRESS=127.0.0.1 HALO_NET_BROADCAST=127.0.0.1 \
  ${NULLR:+HALO_NULL_RENDERER=1} HALO_DISPLAY_MODE=windowed HALO_WINDOW_SIZE=${WIN:-640x360} HALO_EXIT_AFTER=$SECS \
  SDL_AUDIODRIVER=dummy MESA_SHADER_CACHE_DIR="$B/mesa-cache" \
  HALO_NETWORK_TEST=host:$MAP HALO_NETWORK_TEST_START=8 HALO_SOLO_GAME=1 HALO_NETWORK_TEST_SCORE=999 \
  HALO_NETWORK_TEST_LOCAL_PLAYERS=$PLAYERS HALO_NETWORK_TEST_SPLIT_LEAVE=$LEAVE \
  HALO_NETWORK_TEST_KILL=15 HALO_TEST_INPUT=bot:7 \
  HALO_SCREENSHOT_DIR="$B/shots" HALO_SCREENSHOT_EVERY=$SHOT \
  "$BIN" > "$B/stdout.log" 2>&1 ) & GP=$!
wait $GP; RC=$?
kill $XP 2>/dev/null
echo "exit $RC  log: $B"
grep -h -E "split screen player|assert|halt|crash|exception|signal" "$B/stdout.log" "$B"/data/logs/* "$B"/save/* 2>/dev/null | head -40
grep "network test: tick" "$B/stdout.log" | tail -1 | cut -c1-400
exit $RC
