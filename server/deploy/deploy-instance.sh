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
scp "$here/../blocked_names.txt" "$host:/opt/halo-dedicated/instances/$name/"
# (through a folder of this run's own on the host, made by mktemp: no name
# in a shared /tmp another user could take first; installed as root's)
remote_folder=$(ssh "$host" 'mktemp -d')
case "$remote_folder" in
"" | [!/]* | *[!A-Za-z0-9/._-]*) echo "deploy-instance.sh: no folder of its own on the host" >&2; exit 1 ;;
esac
scp "$here/halo-dedicated@.service" "$host:$remote_folder/"
ssh "$host" "sudo install -o root -g root -m 644 $remote_folder/halo-dedicated@.service /etc/systemd/system/ \
	&& rm -rf $remote_folder \
	&& sudo systemctl daemon-reload \
	&& sudo systemctl enable halo-dedicated@$name \
	&& sudo systemctl restart halo-dedicated@$name"
