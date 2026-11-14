#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
#
# A Chupathingy dedicated server for testing a port against, on 127.0.0.1
# alone: never listed, never public, nothing announced anywhere.
#
#   loopback_host.sh --server BIN --maps DIR [--map NAME] [--gametype NAME]
#                    [--announce N] [--players N] [--address 127.0.0.X]
#                    [--broadcast 127.0.0.Y,...] [--work DIR] [--seconds N]
#
#   --server    the server program (chupathingyce-server from a release, or
#               build/.../chupathingyce-server from this repository)
#   --maps      your data folder: maps/ (with ui.map and the map), and
#               maps_ce/ and the others if you use them. Linked, never copied
#               or written to.
#   --map       the map (bloodgulch); --gametype the game type (slayer)
#   --announce  the OpenCE network version the server announces (default:
#               the build's own); our clients join a range around it, an
#               OpenCE-style client only its exact number
#   --players   the most players (12)
#   --address   the loopback address to host on (127.0.0.1); another of
#               127.0.0.0/8 lets two copies run on one Linux machine
#   --broadcast the loopback addresses the game is announced to, comma
#               separated (--address's by default): where your port's
#               client searches from, such as 127.0.0.2 for a client bound
#               there
#   --work      a folder for the server's saves and logs (a new temporary
#               folder by default, deleted when the server stops)
#   --seconds   stop after this long (default: run until Ctrl-C)
#
# The server's game port is 127.0.0.1:5150 (system link searches on 5151),
# its Delta Peer port 127.0.0.1:5160 (or --address's). Point your port at
# that address and join from its system link list. Nothing else on the
# machine may hold 5150 or 5160 on every address (0.0.0.0) meanwhile, or the
# server can't host. Its log is <work>/logs/debug.txt.
#
# What keeps it to this machine: system link bound to and announced on the
# loopback address alone; internet play, UPnP, the public lobby, the game
# list (Delta List), stats, the site link, GameSpy, map sharing and map
# downloads off; the control panel off; the game list's and the maps
# index's addresses a closed local port, and a local legacy table, so no
# signed table is fetched either. Those settings are what keep it here. As a
# second guard, the script checks the server's sockets every few seconds
# while it runs, and stops it if one is off the loopback address (a socket
# opened and closed between two checks goes unseen).

set -euo pipefail

server="" maps="" map="bloodgulch" gametype="slayer" announce="" players="12" work="" seconds="" address="127.0.0.1" broadcast=""
while [ $# -gt 0 ]; do
	case "$1" in
		--server) server="$2"; shift 2 ;;
		--maps) maps="$2"; shift 2 ;;
		--map) map="$2"; shift 2 ;;
		--gametype) gametype="$2"; shift 2 ;;
		--announce) announce="$2"; shift 2 ;;
		--players) players="$2"; shift 2 ;;
		--address) address="$2"; shift 2 ;;
		--broadcast) broadcast="$2"; shift 2 ;;
		--work) work="$2"; shift 2 ;;
		--seconds) seconds="$2"; shift 2 ;;
		-h|--help) sed -n '6,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "loopback_host.sh: unknown argument $1" >&2; exit 2 ;;
	esac
done
[ -x "$server" ] || { echo "loopback_host.sh: --server must name the server program" >&2; exit 2; }
[ -d "$maps/maps" ] || { echo "loopback_host.sh: --maps must be a folder with maps/ in it" >&2; exit 2; }
case "$address" in
	127.*) ;;
	*) echo "loopback_host.sh: --address must be a loopback address (127.x.x.x)" >&2; exit 2 ;;
esac
case "$address" in
	*[!0-9.]*) echo "loopback_host.sh: --address is an IPv4 address" >&2; exit 2 ;;
esac
broadcast="${broadcast:-$address}"
for each in ${broadcast//,/ }; do
	case "$each" in
		127.*) ;;
		*) echo "loopback_host.sh: --broadcast takes loopback addresses (127.x.x.x) alone" >&2; exit 2 ;;
	esac
	case "$each" in
		*[!0-9.]*) echo "loopback_host.sh: --broadcast takes IPv4 addresses" >&2; exit 2 ;;
	esac
done
case "$map$gametype" in
	*[!A-Za-z0-9_@.-]*) echo "loopback_host.sh: map and game type are plain names" >&2; exit 2 ;;
esac

made_work=0
if [ -z "$work" ]; then
	work="$(mktemp -d "${TMPDIR:-/tmp}/delta-loopback.XXXXXX")"
	made_work=1
fi
mkdir -p "$work/data/playlists" "$work/saves" "$work/home" "$work/logs"
server="$(cd "$(dirname "$server")" && pwd)/$(basename "$server")"
maps="$(cd "$maps" && pwd)"
for entry in "$maps"/*; do
	name="$(basename "$entry")"
	case "$name" in playlists|logs|admin) continue ;; esac
	[ -e "$work/data/$name" ] || ln -s "$entry" "$work/data/$name"
done
printf '%s %s\n' "$map" "$gametype" > "$work/data/playlists/delta-loopback.txt"
: > "$work/brokers-none.txt"
# (a local legacy table with no rows: the build's own numbers, and no signed
# table fetched from the site or GitHub, nor passed on)
printf '{"delta_legacy": 1, "serial": 1, "wires": {}}\n' > "$work/legacy-local.json"

pid=""
# (called by the trap below)
# shellcheck disable=SC2329
stop() {
	if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
		kill "$pid" 2>/dev/null || true
		wait "$pid" 2>/dev/null || true
	fi
	if [ "$made_work" = 1 ]; then
		rm -rf "$work"
	fi
}
trap stop EXIT INT TERM

env -i \
	PATH="/usr/bin:/bin" \
	HOME="$work/home" \
	HALO_DATA_ROOT="$work/data" \
	HALO_SAVE_ROOT="$work/saves" \
	HALO_LOG_ROOT="$work/logs" \
	HALO_DEDICATED="playlists/delta-loopback.txt" \
	HALO_DEDICATED_NAME="Delta loopback" \
	HALO_DEDICATED_MAXIMUM_PLAYERS="$players" \
	HALO_DEDICATED_PUBLIC=false \
	HALO_DEDICATED_CONTROL=off \
	HALO_DEDICATED_CONSOLE=false \
	HALO_DEDICATED_LINK=false \
	HALO_DEDICATED_GAMESPY_LISTING=false \
	HALO_DEDICATED_GAMESPY_MASTERS="" \
	HALO_DEDICATED_CE_JOINS=false \
	HALO_DEDICATED_BAN_SHARE=false \
	HALO_NET_ADDRESS="$address" \
	HALO_NET_BROADCAST="$broadcast" \
	HALO_NET_ONLINE=false \
	HALO_NET_ALLOW_UPNP=false \
	HALO_NET_PUBLIC_LOBBY=false \
	HALO_NET_HOST_PUBLIC=false \
	HALO_NET_LIST_GAMES=false \
	HALO_NET_REPORT_HOSTED=false \
	HALO_NET_REPORT_GAMES=false \
	HALO_NET_BROWSER="http://127.0.0.1:9" \
	HALO_NET_BROKERS_FILE="$work/brokers-none.txt" \
	HALO_NET_STUN="" \
	HALO_NET_MAP_SHARE=false \
	HALO_NET_MAP_DOWNLOADS=never \
	HALO_NET_MAPS_URL="http://127.0.0.1:9" \
	HALO_MAP_REPOSITORIES="http://127.0.0.1:9" \
	HALO_LEGACY_TABLE="$work/legacy-local.json" \
	HALO_NET_PROTOCOL=auto \
	HALO_UPDATE_AUTO=false \
	HALO_DISCORD_APPLICATION="" \
	${announce:+HALO_NET_ANNOUNCE="$announce"} \
	"$server" > "$work/logs/server-output.txt" 2>&1 &
pid=$!

# every socket the server holds must be on the loopback address
check_sockets() {
	local listing
	if command -v ss > /dev/null; then
		listing="$(ss -Hanpu 2>/dev/null; ss -Hanpt 2>/dev/null)"
		listing="$(printf '%s\n' "$listing" | grep "pid=$pid," | awk '{print $4}')" || true
	elif command -v lsof > /dev/null; then
		listing="$(lsof -nP -a -p "$pid" -i 2>/dev/null | awk 'NR > 1 {print $9}')" || true
	else
		echo "loopback_host.sh: neither ss nor lsof to check the sockets with; stopping" >&2
		return 1
	fi
	if [ -z "$listing" ]; then
		echo "loopback_host.sh: the server holds no sockets yet" >&2
		return 2
	fi
	if printf '%s\n' "$listing" | grep -v -E '^(127\.[0-9.]+|\[?::1\]?):' | grep -q .; then
		echo "loopback_host.sh: the server has a socket off the loopback address; stopping it:" >&2
		printf '%s\n' "$listing" | grep -v -E '^(127\.[0-9.]+|\[?::1\]?):' | sed 's/^[^:]*:/(address hidden):/' >&2
		return 1
	fi
	printf '%s\n' "$listing" | sort -u | sed 's/^/  socket /'
	return 0
}

for _ in $(seq 1 30); do
	sleep 1
	kill -0 "$pid" 2>/dev/null || { echo "loopback_host.sh: the server stopped; see $work/logs" >&2; tail -20 "$work/logs/server-output.txt" >&2; exit 1; }
	set +e
	check_sockets
	status=$?
	set -e
	[ "$status" = 0 ] && break
	[ "$status" = 1 ] && exit 1
done
if [ "$status" != 0 ]; then
	echo "loopback_host.sh: the server never opened its sockets: is another program holding port 5150 or" >&2
	echo "5160 (on 0.0.0.0, or on $address)? Its last lines:" >&2
	tail -5 "$work/logs/server-output.txt" >&2
	exit 1
fi
echo "loopback_host.sh: hosting $map ($gametype) on $address:5150, Delta Peer on $address:5160"
echo "loopback_host.sh: log in $work/logs (Ctrl-C stops the server${seconds:+, or after $seconds s})"

if [ -n "$seconds" ]; then
	end=$((SECONDS + seconds))
	while [ $SECONDS -lt $end ] && kill -0 "$pid" 2>/dev/null; do
		sleep 1
		# (and again while it runs: a socket opened later is caught too)
		set +e
		check_sockets > /dev/null 2>&1
		status=$?
		set -e
		[ "$status" = 1 ] && { check_sockets > /dev/null || true; exit 1; }
	done
else
	while kill -0 "$pid" 2>/dev/null; do
		sleep 5
		set +e
		check_sockets > /dev/null 2>&1
		status=$?
		set -e
		[ "$status" = 1 ] && { check_sockets > /dev/null || true; exit 1; }
	done
fi
echo "loopback_host.sh: stopped"
exit 0
