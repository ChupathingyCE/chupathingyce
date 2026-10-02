#!/bin/sh
# Adds (or updates) another dedicated server on the host the first one runs
# on (deploy.sh first: the image, maps and playlists): its settings
# (instances/<name>.env here), its data folder, and the halo-dedicated@<name>
# service. Run from the repository:
#   server/deploy/deploy-instance.sh user@host team
set -eu
host=$1
name=$2
here=$(dirname "$0")

test -f "$here/instances/$name.env"
scp "$here"/../playlists/*.txt "$host:/opt/halo-dedicated/data/playlists/"
ssh "$host" "sudo mkdir -p /opt/halo-dedicated/instances/$name && sudo chown -R \"\$(id -un)\" /opt/halo-dedicated/instances"
scp "$here/instances/$name.env" "$host:/opt/halo-dedicated/instances/"
scp "$here/halo-dedicated@.service" "$host:/tmp/"
ssh "$host" "sudo mv /tmp/halo-dedicated@.service /etc/systemd/system/ \
	&& sudo systemctl daemon-reload \
	&& sudo systemctl enable halo-dedicated@$name \
	&& sudo systemctl restart halo-dedicated@$name"
