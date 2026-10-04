#!/bin/sh
# Installs or updates the dedicated server on a Linux host with Docker
# (server/docs/docker.md). Run from the repository, with a server for the
# host's architecture: a release's chupathingyce-server-linux-<arch>, or one
# built here (python3 tools/ci_build.py server-x64 release --alpine):
#   server/deploy/deploy.sh user@host path/to/chupathingyce-server
# The host's data folder, /opt/halo-dedicated/data, needs the maps (maps/,
# the multiplayer maps and ui.map); the playlists are copied from server/.
set -eu
host=$1
binary=$2
here=$(dirname "$0")
# the image's platform: the server's architecture (an x86 server runs on an
# x86-64 host too)
arch=$("$here/server-arch.sh" "$binary")

ssh "$host" 'sudo mkdir -p /opt/halo-dedicated/data/maps /opt/halo-dedicated/data/md_maps /opt/halo-dedicated/data/playlists /opt/halo-dedicated/data/saves /opt/halo-dedicated/image/bin/'"$arch"' && sudo chown -R "$(id -un)" /opt/halo-dedicated'
scp "$binary" "$host:/opt/halo-dedicated/image/bin/$arch/chupathingyce-server"
scp "$here/Dockerfile" "$host:/opt/halo-dedicated/image/"
scp "$here"/../playlists/*.txt "$host:/opt/halo-dedicated/data/playlists/"
# (the settings are kept once there: edit them on the host)
ssh "$host" 'test -f /opt/halo-dedicated/dedicated.env' || scp "$here/dedicated.env" "$host:/opt/halo-dedicated/"
scp "$here/halo-dedicated.service" "$host:/tmp/"
ssh "$host" 'sudo docker build -q --platform linux/'"$arch"' -t halo-dedicated /opt/halo-dedicated/image \
	&& sudo mv /tmp/halo-dedicated.service /etc/systemd/system/ \
	&& sudo systemctl daemon-reload \
	&& sudo systemctl enable halo-dedicated \
	&& sudo systemctl restart halo-dedicated'
