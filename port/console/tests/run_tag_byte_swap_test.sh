#!/bin/sh
# Host-side test of the console tag byte swap on real maps:
#   port/console/tests/run_tag_byte_swap_test.sh assets/maps/bloodgulch.map ...
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=${TMPDIR:-/tmp}/tag_byte_swap_test
cc -O1 -Wall -DTAG_BYTE_SWAP_EMULATE_BIG_ENDIAN -I"$here/stubs" -I"$here/../include" \
	-o "$out" "$here/tag_byte_swap_test.c" "$here/../game/tag_byte_swap.c" \
	"$here/../game/tag_byte_swap_tables.c" -lz
status=0
for map in "$@"; do
	"$out" "$map" || status=1
done
exit $status
