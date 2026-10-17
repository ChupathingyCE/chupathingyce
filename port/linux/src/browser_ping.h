/*
BROWSER_PING.H

Online Games' estimated pings (browser_ping.c): where no host answers a
probe (p2p_probe.c), a game's ping is estimated from its region, as the
list gives it ("EU-DE": continent and country), and this machine's round
trip to the anchors the list names (GET /v1/anchors.txt), timed once a run.
Shown with a tilde ("~85"). No address is in any of it but the anchors'
own host names. docs/delta.md, "Pings before joining".
*/

#ifndef __HALO_LINUX_BROWSER_PING_H
#define __HALO_LINUX_BROWSER_PING_H

enum
{
	BROWSER_PING_ANCHORS = 16,
	BROWSER_PING_HOST_SIZE = 64,
	BROWSER_PING_REGIONS = 16,
	/* "EU-DE", and its end */
	BROWSER_PING_REGION_SIZE = 8,
};

struct browser_ping_anchor
{
	char host[BROWSER_PING_HOST_SIZE];
	unsigned short port;
	/* the regions it stands for: continents ("EU") or countries ("EU-DE") */
	int region_count;
	char regions[BROWSER_PING_REGIONS][BROWSER_PING_REGION_SIZE];
	/* this machine's round trip to it in milliseconds, -1 not known */
	long ping;
};

/* /v1/anchors.txt's lines ("anchor<TAB>host<TAB>port<TAB>EU,EU-DE"; other
lines, and those not so made, passed over) into anchors; returns their
count. text is changed */
int browser_ping_parse_anchors(char *text, struct browser_ping_anchor *anchors, int maximum);

/* whether a region as the list gives it is one ("NA", "NA-US"): an
uppercase continent, a dash and an uppercase country */
int browser_ping_region_valid(const char *region);

/* a game's estimated ping for its region: the lowest timed anchor naming
its country, else its continent; -1 none */
long browser_ping_estimate(const struct browser_ping_anchor *anchors, int count, const char *region);

/* the Ping column's order of two games: measured first (lowest first), then
estimated (lowest first), then none: negative, 0 or positive */
long browser_ping_order(long ping_a, int estimated_a, long ping_b, int estimated_b);

#endif
