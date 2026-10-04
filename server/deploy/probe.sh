#!/bin/sh
# The game list's probe on the dedicated server's host (server/README.md,
# "Probing a game"): the game that an invite leads to, read by the game
# (HALO_PROBE) in a container of its own, which goes when it is done:
#   probe.sh <invite>
# prints the game's "probe: {...}" line. Runs as root (sudo, from
# probe-ssh.sh); the invite must be one, its hexadecimal digits only. The
# container's data folder is in memory (the saves in it: the game's scratch
# drive wants about 33 MB), and goes with it.
set -eu
invite=${1:-}
case "$invite" in
*[!0-9a-f]* | "") echo 'probe: {"ok": false, "error": "not an invite"}'; exit 1 ;;
esac
if [ ${#invite} -ne 64 ] && [ ${#invite} -ne 44 ]; then
	echo 'probe: {"ok": false, "error": "not an invite"}'
	exit 1
fi
# (no more than a few at once: the rest wait their turn)
exec 9>/run/halo-probe.lock
flock -w 60 9
# (not root, without capabilities: it reads what a stranger's host sends)
exec timeout 40 docker run --rm --network bridge --memory 512m --cpus 1 --pids-limit 128 \
	--user 65534:65534 --cap-drop ALL --security-opt no-new-privileges \
	--tmpfs /data:size=256m \
	-v /opt/halo-dedicated/data/maps/ui.map:/data/maps/ui.map:ro \
	-e HALO_PROBE="$invite" \
	halo-probe 2>/dev/null | grep '^probe: '
