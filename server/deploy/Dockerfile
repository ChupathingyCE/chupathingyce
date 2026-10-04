# The ChupathingyCE Dedicated Server's container (server/docs/docker.md):
# the server, one static program (tools/server_build.py), on Alpine Linux,
# whose own files it needs none of but the certificates (its HTTPS to the
# game list) and a shell for whoever looks inside. The data folder (maps,
# playlists, saves) is mounted at /data.
#
# The same file builds every architecture: the context holds each
# architecture's server as bin/<Docker's name for it>/chupathingyce-server
# (bin/386, bin/amd64, bin/arm64), and the platform built picks it. Nothing
# runs while it builds, so buildx makes all three on any machine:
#   docker buildx build --platform linux/386,linux/amd64,linux/arm64 .
# (deploy.sh puts the one server it is given there, and builds for it)
FROM alpine:3.22
# (BuildKit's, from the platform; Docker's legacy builder needs it given:
# --build-arg TARGETARCH=amd64, as deploy.sh does)
ARG TARGETARCH

COPY bin/${TARGETARCH}/chupathingyce-server /opt/chupathingyce/chupathingyce-server
WORKDIR /data

# the data and saves in /data; the run's key in /tmp (it goes with the
# container); no settings file beside the program (the environment's)
ENV HALO_DATA_ROOT=/data \
	HALO_SAVE_ROOT=/data/saves \
	XDG_RUNTIME_DIR=/tmp

# Not root: the server reads packets from anyone who joins. 1000 is the
# first user of most Linux systems, who usually owns the data folder; the
# services (halo-dedicated.service) say so too, and drop every capability
# and the means to gain privileges, and cap memory and processes.
USER 1000:1000

# (the server withdraws its listing and stops on SIGTERM)
STOPSIGNAL SIGTERM
ENTRYPOINT ["/opt/chupathingyce/chupathingyce-server"]
