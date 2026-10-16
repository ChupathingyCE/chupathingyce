#!/bin/sh
# Run the built macOS app once, scripted and offline, against a temporary data
# root (the real maps symlinked in; the user's config.toml, saves and logs
# untouched). Usage:
#   tools/metal_scripted_run.sh <output-folder> <seconds> [VAR=value ...]
# Extra VAR=value pairs are passed to the game (e.g. HALO_ANTI_ALIASING=fxaa).
# The game's log is <output-folder>/debug.txt; stdout/stderr go to run.log.
#
# It launches the game on this Mac's desktop, so it runs only when asked:
# HALO_ALLOW_LAUNCH=1. The game runs as a background app
# (SDL_MAC_BACKGROUND_APP: no Dock icon, never made active, so it takes
# neither focus nor the mouse from the apps in use) in a hidden window.
set -eu
if [ "${HALO_ALLOW_LAUNCH:-}" != 1 ]; then
	echo "not launching the game: set HALO_ALLOW_LAUNCH=1 to allow it" >&2
	exit 75
fi
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=$1
seconds=$2
shift 2
app=${HALO_APP:-$repo/build/macos/ChupathingyCE.app/Contents/MacOS/halo}
case "$app" in /*) ;; *) app="$PWD/$app" ;; esac
source_root=${HALO_SOURCE_DATA:-"$HOME/Library/Application Support/ChupathingyCE"}
[ -x "$app" ] || {
	echo "no game at $app (ninja macos)" >&2
	exit 2
}
[ -f "$source_root/maps/bloodgulch.map" ] || {
	echo "no game data in $source_root" >&2
	exit 77
}
mkdir -p "$out/data"
out=$(CDPATH= cd -- "$out" && pwd)
ln -sfn "$source_root/maps" "$out/data/maps"
[ -d "$source_root/maps_ce" ] && ln -sfn "$source_root/maps_ce" "$out/data/maps_ce"
rm -f "$out/data/debug.txt"
(
	cd "$out"
	env HALO_DATA_ROOT="$out/data" HALO_SAVE_ROOT="$out/data" \
		SDL_MAC_BACKGROUND_APP=1 \
		HALO_HIDDEN_WINDOW=1 \
		HALO_NETWORK_TEST=${HALO_NETWORK_TEST:-host:bloodgulch} \
		HALO_SOLO_GAME=1 \
		HALO_NETWORK_TEST_START=${HALO_NETWORK_TEST_START:-2} \
		HALO_TEST_INPUT=${HALO_TEST_INPUT:-bot:1} \
		HALO_EXIT_AFTER="$seconds" \
		HALO_NET_ONLINE=false HALO_NET_ALLOW_UPNP=false HALO_NET_PUBLIC_LOBBY=false \
		HALO_NET_HOST_PUBLIC=false HALO_NET_JOIN_FROM_CLIPBOARD=false \
		HALO_NET_REPORT_GAMES=false HALO_NET_REPORT_EVENTS=false \
		"$@" \
		perl -e 'alarm shift; exec { $ARGV[0] } @ARGV or die "exec: $!\n"' $((seconds + 90)) "$app"
) >"$out/run.log" 2>&1 || status=$?
cp "$out/data/debug.txt" "$out/debug.txt" 2>/dev/null || true
exit ${status:-0}
