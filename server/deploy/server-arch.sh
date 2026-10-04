#!/bin/sh
# Prints Docker's name for the architecture of a server binary (386, amd64
# or arm64), from its ELF header's machine (bytes 18 and 19): for the
# deploy scripts, which build the image for it (Dockerfile).
#   server-arch.sh path/to/chupathingyce-server
set -eu
machine=$(od -An -tu2 -j18 -N2 "$1" | tr -d ' ')
case "$machine" in
3) echo 386 ;;
62) echo amd64 ;;
183) echo arm64 ;;
*) echo "server-arch.sh: $1 is not an x86, x86-64 or arm64 Linux program" >&2; exit 1 ;;
esac
