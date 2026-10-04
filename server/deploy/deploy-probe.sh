#!/bin/sh
# Installs the game list's probe on the dedicated server's host
# (server/README.md): the halo-probe image (the dedicated server's
# Dockerfile, with this game), probe.sh, and the probe user, whose key may
# only probe. Run from the repository:
#   server/deploy/deploy-probe.sh user@host path/to/chupathingyce-server "ssh-ed25519 AAAA... site"
# (the dedicated server program probes too: HALO_PROBE). The dedicated
# server (its image, its service) is left as it is.
set -eu
host=$1
binary=$2
key=$3
here=$(dirname "$0")
# the image's platform: the server's architecture
arch=$("$here/server-arch.sh" "$binary")

ssh "$host" 'sudo mkdir -p /opt/halo-probe/image/bin/'"$arch"' && sudo chown -R "$(id -un)" /opt/halo-probe'
scp "$binary" "$host:/opt/halo-probe/image/bin/$arch/chupathingyce-server"
scp "$here/Dockerfile" "$host:/opt/halo-probe/image/"
scp "$here/probe.sh" "$here/probe-ssh.sh" "$host:/opt/halo-probe/"
printf 'restrict,command="/opt/halo-probe/probe-ssh.sh" %s\n' "$key" > "${TMPDIR:-/tmp}/probe_authorized_keys"
scp "${TMPDIR:-/tmp}/probe_authorized_keys" "$host:/opt/halo-probe/authorized_keys"
ssh "$host" 'set -e
	sudo docker build -q --platform linux/'"$arch"' -t halo-probe /opt/halo-probe/image
	sudo chown root:root /opt/halo-probe /opt/halo-probe/probe.sh /opt/halo-probe/probe-ssh.sh
	sudo chmod 755 /opt/halo-probe/probe.sh /opt/halo-probe/probe-ssh.sh
	id probe >/dev/null 2>&1 || sudo useradd --system --create-home --shell /bin/sh probe
	sudo install -d -o probe -g probe -m 700 ~probe/.ssh
	sudo install -o probe -g probe -m 600 /opt/halo-probe/authorized_keys ~probe/.ssh/authorized_keys
	sudo rm -f /opt/halo-probe/authorized_keys
	echo "probe ALL=(root) NOPASSWD: /opt/halo-probe/probe.sh" | sudo tee /etc/sudoers.d/halo-probe >/dev/null
	sudo chmod 440 /etc/sudoers.d/halo-probe
	sudo visudo -cf /etc/sudoers.d/halo-probe'
