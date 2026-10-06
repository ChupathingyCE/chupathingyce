/*
DELTA.C

The legacy number in use (docs/delta.md, "The legacy table as config"): the
OpenCE network version a host announces, and the range of hosts' versions a
client joins. For now these are the numbers this build was made with
(halo_port_limits.h); a signed legacy table will only ever widen them.
*/

#include "halo_port_limits.h"

int delta_legacy_announce(void)
{
	return HALO_PORT_NETWORK_VERSION;
}

int delta_legacy_minimum(void)
{
	return HALO_PORT_NETWORK_VERSION_MINIMUM;
}

int delta_legacy_maximum(void)
{
	return HALO_PORT_NETWORK_VERSION_MAXIMUM;
}
