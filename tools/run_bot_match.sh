#!/bin/sh
# Visible, private, offline repo-binary bot match. Run from any directory.
#   sh tools/run_bot_match.sh                 # 24 bots, Blood Gulch Team Slayer
#   BOT_PRESET=4v4 sh tools/run_bot_match.sh  # you + 7 bots, Rat Race Team Slayer
set -eu
ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
case "${BOT_PRESET:-24}" in
4v4) DEFAULT_MAP=ratrace DEFAULT_COUNT=7 ;;
24) DEFAULT_MAP=bloodgulch DEFAULT_COUNT=24 ;;
*)
	echo "BOT_PRESET must be 24 or 4v4" >&2
	exit 1
	;;
esac
BINARY=${BOT_BINARY:-"$ROOT/build/macos/halo"}
DATA=${BOT_DATA_ROOT:-"$ROOT/build/macos/botrun/data"}
SAVES=${BOT_SAVE_ROOT:-"$ROOT/build/macos/botrun/saves"}
MAP=${BOT_MAP:-$DEFAULT_MAP}
GAME=${BOT_GAME:-team_slayer}
COUNT=${BOT_COUNT:-$DEFAULT_COUNT}
case "$MAP" in *[!a-z0-9_]* | '')
	echo "BOT_MAP must be a map file name such as ratrace" >&2
	exit 1
	;;
esac
case "$GAME" in team_slayer | slayer) ;; *)
	echo "BOT_GAME must be team_slayer or slayer" >&2
	exit 1
	;;
esac
case "$COUNT" in *[!0-9]* | '')
	echo "BOT_COUNT must be 1-31" >&2
	exit 1
	;;
esac
if [ "$COUNT" -lt 1 ] || [ "$COUNT" -gt 31 ]; then
	echo "BOT_COUNT must be 1-31" >&2
	exit 1
fi
if [ ! -x "$BINARY" ]; then
	echo "Build first: python3 configure.py --bots && ninja macos" >&2
	exit 1
fi
if [ ! -f "$DATA/maps/$MAP.map" ] || [ ! -f "$DATA/maps/ui.map" ]; then
	echo "Set BOT_DATA_ROOT to an isolated data root whose maps/ has $MAP.map and ui.map." >&2
	exit 1
fi
mkdir -p "$SAVES"
# Do not truncate debug.txt or erase existing run data; the game owns its log.
unset HALO_NAV_PROBE HALO_BOT_SANDBOX HALO_NETWORK_TEST_PICKUP HALO_NETWORK_TEST_PICKUP_WEAPON
case "${BOT_HIDDEN_WINDOW:-0}" in
0) unset HALO_HIDDEN_WINDOW HALO_NULL_RENDERER ;;
1)
	HALO_HIDDEN_WINDOW=1 HALO_NULL_RENDERER=1
	export HALO_HIDDEN_WINDOW HALO_NULL_RENDERER
	;;
*)
	echo "BOT_HIDDEN_WINDOW must be 0 or 1" >&2
	exit 1
	;;
esac
exec env HALO_DATA_ROOT="$DATA" HALO_SAVE_ROOT="$SAVES" \
	HALO_NET_ONLINE=false HALO_NET_PUBLIC_LOBBY=false \
	HALO_NET_HOST_PUBLIC=false HALO_NET_LIST_GAMES=false \
	HALO_NET_REPORT_GAMES=false HALO_NET_REPORT_EVENTS=false \
	HALO_NET_ALLOW_UPNP=false HALO_SOLO_GAME=1 \
	HALO_NETWORK_TEST="host:$MAP:$GAME" HALO_NETWORK_TEST_START=5 \
	HALO_NETWORK_TEST_LOADOUT="${BOT_LOADOUT:-rifle_pistol}" \
	HALO_BOTS="$COUNT" HALO_BOT_SKILL="${BOT_SKILL:-spartan}" \
	HALO_BOT_DECISIONS=1 HALO_EXIT_AFTER="${BOT_SECONDS:-180}" \
	"$BINARY"
