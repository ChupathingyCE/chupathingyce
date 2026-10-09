#!/bin/sh
# Installs or updates the dedicated server on a Linux host with Docker
# (server/docs/docker.md). Run from the repository, with a server for the
# host's architecture: a release's chupathingyce-server-linux-<arch>, or one
# built here (python3 tools/ci_build.py server-x64 release --alpine):
#   server/deploy/deploy.sh [--image NAME] [--no-restart] user@host path/to/chupathingyce-server
# The host's data folder, /opt/halo-dedicated/data, needs the maps (maps/,
# the multiplayer maps and ui.map); the playlists are copied from server/.
#
# --image NAME builds the image under another name (halo-dedicated-ce, say:
# a 64-bit server for Halo PC maps beside 32-bit ones), from a build folder
# of its own (/opt/halo-dedicated/image-ce for halo-dedicated-ce). The
# halo-dedicated service is left as it is: restart the services that run
# that image yourself.
# --no-restart installs the image and the service without restarting it, to
# restart once nobody is playing (sudo systemctl restart halo-dedicated).
set -eu
image=halo-dedicated
restart=true
while [ $# -gt 0 ]; do
	case "$1" in
	--image) image=$2; shift 2 ;;
	--image=*) image=${1#--image=}; shift ;;
	--no-restart) restart=false; shift ;;
	-*) echo "deploy.sh: unknown option $1" >&2; exit 2 ;;
	*) break ;;
	esac
done
if [ $# -ne 2 ]; then
	echo "usage: deploy.sh [--image NAME] [--no-restart] user@host path/to/chupathingyce-server" >&2
	exit 2
fi
case "$image" in
"" | *[!a-z0-9._-]*) echo "deploy.sh: $image is not an image name (a-z, 0-9, . _ -)" >&2; exit 2 ;;
esac
host=$1
binary=$2
here=$(dirname "$0")
# the image's platform: the server's architecture (an x86 server runs on an
# x86-64 host too)
arch=$("$here/server-arch.sh" "$binary")
# its build folder: image for halo-dedicated, image-ce for halo-dedicated-ce
if [ "$image" = halo-dedicated ]; then
	context=/opt/halo-dedicated/image
else
	context=/opt/halo-dedicated/image-${image#halo-dedicated-}
fi

ssh "$host" 'sudo mkdir -p /opt/halo-dedicated/data/maps /opt/halo-dedicated/data/md_maps /opt/halo-dedicated/data/maps_ce /opt/halo-dedicated/data/maps_md /opt/halo-dedicated/data/maps_pc /opt/halo-dedicated/data/playlists /opt/halo-dedicated/data/saves '"$context/bin/$arch"' && sudo chown -R "$(id -un)" /opt/halo-dedicated'
scp "$binary" "$host:$context/bin/$arch/chupathingyce-server"
scp "$here/Dockerfile" "$host:$context/"
scp "$here"/../playlists/*.txt "$host:/opt/halo-dedicated/data/playlists/"
# (TARGETARCH given: Docker's legacy builder, without buildx, sets none;
# and --pull, or it takes whichever architecture's Alpine it has already)
ssh "$host" 'sudo docker build -q --pull --platform linux/'"$arch"' --build-arg TARGETARCH='"$arch"' -t '"$image $context"
if [ "$image" != halo-dedicated ]; then
	echo "deploy.sh: built $image; restart the services that run it"
	exit 0
fi
# (the settings are kept once there: edit them on the host)
ssh "$host" 'test -f /opt/halo-dedicated/dedicated.env' || scp "$here/dedicated.env" "$host:/opt/halo-dedicated/"
# (and the further servers' template, deploy-instance.sh's, which runs the
# same image: its servers take it as they restart)
scp "$here/halo-dedicated.service" "$here/halo-dedicated@.service" "$host:/tmp/"
ssh "$host" 'sudo mv /tmp/halo-dedicated.service /tmp/halo-dedicated@.service /etc/systemd/system/ \
	&& sudo systemctl daemon-reload \
	&& sudo systemctl enable halo-dedicated'
if [ "$restart" = true ]; then
	ssh "$host" 'sudo systemctl restart halo-dedicated'
else
	echo "deploy.sh: installed; restart halo-dedicated when nobody is playing"
fi
