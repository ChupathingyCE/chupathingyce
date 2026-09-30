# The dedicated server's container (server/README.md): the 32-bit Linux
# game built with the game list (configure.py --game-browser, ninja linux),
# on the Debian that builds it, so its libraries are the ones it was linked
# against. The data folder (maps, playlists) is mounted at /data.
FROM i386/debian:trixie-slim

RUN apt-get update \
	&& apt-get install -y --no-install-recommends libsdl3-0 ca-certificates \
	&& rm -rf /var/lib/apt/lists/*

COPY halo /opt/halo/halo
WORKDIR /opt/halo

# no updates (the image is the version), the data and saves in /data
ENV HALO_DATA_ROOT=/data \
	HALO_SAVE_ROOT=/data/saves \
	HALO_UPDATE_AUTO=false

# (the game withdraws its listing and stops on SIGTERM)
STOPSIGNAL SIGTERM
ENTRYPOINT ["/opt/halo/halo"]
