# The dedicated server's container (server/README.md): the Linux game built
# with the game list (configure.py --game-browser), on the Debian that
# builds it, so its libraries are the ones it was linked against. The data
# folder (maps, playlists) is mounted at /data.
#
# BASE is the Debian of the game's word size: the 32-bit game (ninja linux)
# by default, the 64-bit one (ninja linux64) with
# --build-arg BASE=debian:trixie-slim (deploy.sh picks it from the game).
ARG BASE=i386/debian:trixie-slim
FROM ${BASE}

RUN apt-get update \
	&& apt-get install -y --no-install-recommends libsdl3-0 ca-certificates \
	&& rm -rf /var/lib/apt/lists/*

COPY halo /opt/halo/halo
WORKDIR /opt/halo

# no updates (the image is the version), the data and saves in /data; the
# run's key in /tmp (it goes with the container, as it did in root's home),
# so the game runs as any user (the services run it as the data folder's
# owner, not root)
ENV HALO_DATA_ROOT=/data \
	HALO_SAVE_ROOT=/data/saves \
	HALO_UPDATE_AUTO=false \
	XDG_RUNTIME_DIR=/tmp

# (the game withdraws its listing and stops on SIGTERM)
STOPSIGNAL SIGTERM
ENTRYPOINT ["/opt/halo/halo"]
