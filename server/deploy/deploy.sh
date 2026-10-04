#!/bin/sh
# Installs or updates the dedicated server on a Linux host with Docker
# (server/README.md). Run from the repository, after building the Linux
# game with the game list, 32-bit or 64-bit:
#   python3 configure.py --game-browser --portable --release && ninja linux
#   server/deploy/deploy.sh user@host [path/to/halo]
# (ninja linux64 and build/linux64/halo for the 64-bit game)
# The host's data folder, /opt/halo-dedicated/data, needs the maps (maps/,
# the multiplayer maps and ui.map); the playlists are copied from server/.
set -eu
host=$1
binary=${2:-build/linux/halo}
here=$(dirname "$0")
# the image's Debian: the game's word size (byte 5 of its ELF header, its
# class: 2 for the 64-bit game, ninja linux64)
if [ "$(od -An -tu1 -j4 -N1 "$binary" | tr -d ' ')" = 2 ]; then
	base=debian:trixie-slim
else
	base=i386/debian:trixie-slim
fi

ssh "$host" 'sudo mkdir -p /opt/halo-dedicated/data/maps /opt/halo-dedicated/data/playlists /opt/halo-dedicated/data/saves /opt/halo-dedicated/image && sudo chown -R "$(id -un)" /opt/halo-dedicated'
scp "$binary" "$host:/opt/halo-dedicated/image/halo"
scp "$here/Dockerfile" "$host:/opt/halo-dedicated/image/"
scp "$here"/../playlists/*.txt "$host:/opt/halo-dedicated/data/playlists/"
# (the settings are kept once there: edit them on the host)
ssh "$host" 'test -f /opt/halo-dedicated/dedicated.env' || scp "$here/dedicated.env" "$host:/opt/halo-dedicated/"
scp "$here/halo-dedicated.service" "$host:/tmp/"
ssh "$host" 'sudo docker build -q --build-arg BASE='"$base"' -t halo-dedicated /opt/halo-dedicated/image \
	&& sudo mv /tmp/halo-dedicated.service /etc/systemd/system/ \
	&& sudo systemctl daemon-reload \
	&& sudo systemctl enable halo-dedicated \
	&& sudo systemctl restart halo-dedicated'
