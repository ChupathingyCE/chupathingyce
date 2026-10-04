#!/bin/sh
# Installs the game list's probe on the dedicated server's host
# (server/README.md): the halo-probe image (the dedicated server's
# Dockerfile, with this game), probe.sh, and the probe user, whose key may
# only probe. Run from the repository:
#   server/deploy/deploy-probe.sh user@host path/to/halo "ssh-ed25519 AAAA... site"
# The dedicated server (its image, its service) is left as it is.
set -eu
host=$1
binary=$2
key=$3
here=$(dirname "$0")
# the image's Debian: the game's word size (byte 5 of its ELF header, its
# class: 2 for the 64-bit game, ninja linux64)
if [ "$(od -An -tu1 -j4 -N1 "$binary" | tr -d ' ')" = 2 ]; then
	base=debian:trixie-slim
else
	base=i386/debian:trixie-slim
fi

ssh "$host" 'sudo mkdir -p /opt/halo-probe/image && sudo chown -R "$(id -un)" /opt/halo-probe'
scp "$binary" "$host:/opt/halo-probe/image/halo"
scp "$here/Dockerfile" "$host:/opt/halo-probe/image/"
scp "$here/probe.sh" "$here/probe-ssh.sh" "$host:/opt/halo-probe/"
printf 'restrict,command="/opt/halo-probe/probe-ssh.sh" %s\n' "$key" > "${TMPDIR:-/tmp}/probe_authorized_keys"
scp "${TMPDIR:-/tmp}/probe_authorized_keys" "$host:/opt/halo-probe/authorized_keys"
ssh "$host" 'set -e
	sudo docker build -q --build-arg BASE='"$base"' -t halo-probe /opt/halo-probe/image
	sudo chown root:root /opt/halo-probe /opt/halo-probe/probe.sh /opt/halo-probe/probe-ssh.sh
	sudo chmod 755 /opt/halo-probe/probe.sh /opt/halo-probe/probe-ssh.sh
	id probe >/dev/null 2>&1 || sudo useradd --system --create-home --shell /bin/sh probe
	sudo install -d -o probe -g probe -m 700 ~probe/.ssh
	sudo install -o probe -g probe -m 600 /opt/halo-probe/authorized_keys ~probe/.ssh/authorized_keys
	sudo rm -f /opt/halo-probe/authorized_keys
	echo "probe ALL=(root) NOPASSWD: /opt/halo-probe/probe.sh" | sudo tee /etc/sudoers.d/halo-probe >/dev/null
	sudo chmod 440 /etc/sudoers.d/halo-probe
	sudo visudo -cf /etc/sudoers.d/halo-probe'
