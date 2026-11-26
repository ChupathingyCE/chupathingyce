#!/bin/sh
# chupathingy-halo-stub 1
#
# The game was called halo before 0.9.0; it is chupathingy now. This halo
# starts chupathingy beside it with the same arguments, so shortcuts,
# Steam entries and scripts made for halo keep working. The game keeps
# this file when it cleans up the old halo (port/linux/src/rename_stub.h).
# TODO(after 0.9.x): remove this stub from the builds.
here=$(dirname "$(readlink -f "$0" 2>/dev/null || printf '%s' "$0")")
# (an update by a build from before 0.8.0, or a zip unpacked by hand, may
# leave chupathingy without its executable bit)
[ -x "$here/chupathingy" ] || chmod +x "$here/chupathingy" 2>/dev/null
exec "$here/chupathingy" "$@"
