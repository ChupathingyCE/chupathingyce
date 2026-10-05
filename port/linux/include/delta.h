/*
DELTA.H

Delta, ChupathingyCE's network family (docs/delta.md): what our machines say
to each other and to our services beyond the game protocol OpenCE defines,
which stays OpenCE's byte for byte. This header holds Delta Peer's numbers
(its major, its advertisement flag, the capability registry) and the legacy
number's compatibility table.

Nothing here changes what a machine sends yet: the table documents the
legacy numbers and checks halo_port_limits.h against them
(tools/test_delta.py), and Delta Peer's handshake comes later.
*/

#ifndef HALO_DELTA_H
#define HALO_DELTA_H

/* ---------- Delta Peer */

/* Delta Peer's major: raised only for a breaking change to its own framing
or handshake. Machines of different majors fall back to the legacy protocol
alone. */
#define DELTA_MAJOR 1

/* a host that speaks Delta Peer sets this in its advertisement's flags
(HALO_PORT_ADVERTISED_FLAGS_OFFSET; 0x01 and 0x02 are the port's): OpenCE's
machines ignore it. Not set by any host until the handshake exists. */
#define DELTA_ADVERTISED_FLAG 0x04

/* The capability registry: one bit per optional feature. A bit's number is
never reused, a machine ignores the bits it doesn't know, and adding one
doesn't raise DELTA_MAJOR. */
enum delta_capability
{
	/* each player's platform, for the scoreboard's icons */
	_delta_capability_platform = 0,
	/* a player's ID and profile revision (the profile is the site's) */
	_delta_capability_profile = 1,
	/* a host's messages to its players: welcome, notices, the next map */
	_delta_capability_server_messages = 2,
	/* text chat between Delta players */
	_delta_capability_chat = 3,
	/* Halo PC maps' identity (name and hash) */
	_delta_capability_ce_maps = 4,
	/* HaloMD maps' identity */
	_delta_capability_md_maps = 5,
	/* network co-op beyond OpenCE's */
	_delta_capability_coop = 6,
	/* AI sync beyond OpenCE's */
	_delta_capability_ai_sync = 7,
	/* map and game type votes */
	_delta_capability_vote = 8,
	/* the client is a console of 16 slots (Warthog), for hosts that adapt */
	_delta_capability_console_slots = 9,

	NUMBER_OF_DELTA_CAPABILITIES
};

/* ---------- the legacy number */

/* OpenCE's network versions (HALO_PORT_NETWORK_VERSION in its builds), the
first of its releases with each, and whether the version's change is one the
version before plays multiplayer with as it is (additive: messages a machine
of the older version drops) or not (breaking). OpenCE's clients join only
hosts of their exact version; ours join every version back to the newest
breaking one (HALO_PORT_NETWORK_VERSION_MINIMUM), and our hosts announce the
newest (HALO_PORT_NETWORK_VERSION). One row a version, oldest first; the
command repository's watch adds a row when it follows OpenCE's raise
(tools/follow.py there). Versions 1 to 9 each changed the wire format
(port/linux/NETCODE.md, "Versions"). */
#define DELTA_LEGACY_VERSIONS(X) \
	X(10, "build-73", additive) /* players' pings for the scoreboard */ \
	X(11, "build-76", breaking) /* the gametype's PC options in the settings record */ \
	X(12, "build-118", additive) /* network co-op */ \
	X(13, "build-119", additive) /* co-op's extra enemies */ \
	X(14, "build-123", additive) /* co-op devices' positions, units opening and closing */ \
	X(15, "build-124", additive) /* co-op allegiances, loading zones, falling players */ \
	X(16, "build-125", additive) /* co-op: every machine stays on the host's BSP */ \
	X(17, "build-128", additive) /* followed from OpenCE: additive */

#endif
