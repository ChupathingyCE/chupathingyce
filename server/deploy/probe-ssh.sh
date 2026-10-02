#!/bin/sh
# The probe user's only command (its authorized_keys: command=), for
# halo.milenko.org: the invite it asks for, probed (probe.sh, as root).
case "${SSH_ORIGINAL_COMMAND:-}" in
*[!0-9a-f]* | "") echo 'probe: {"ok": false, "error": "not an invite"}'; exit 1 ;;
esac
exec sudo -n /opt/halo-probe/probe.sh "$SSH_ORIGINAL_COMMAND"
