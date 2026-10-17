/*
BROWSER_PING.C

Online Games' estimated pings (browser_ping.h): the anchors' list, and a
game's estimate from its region. No sockets here: browser.c times the
anchors.
*/

#include "browser_ping.h"

#include <stdlib.h>
#include <string.h>

static int upper(char c)
{
	return c >= 'A' && c <= 'Z';
}

static int host_name_valid(const char *host)
{
	int length = (int)strlen(host);
	int index;

	if (!length || length >= BROWSER_PING_HOST_SIZE || host[0] == '-' || host[0] == '.')
		return 0;
	for (index = 0; index < length; index++)
	{
		char c = host[index];

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.'))
			return 0;
	}
	return 1;
}

int browser_ping_region_valid(const char *region)
{
	return region && upper(region[0]) && upper(region[1]) &&
		(!region[2] || (region[2] == '-' && upper(region[3]) && upper(region[4]) && !region[5]));
}

/* the next tab-separated field of *cursor (NULL past the last) */
static char *field(char **cursor)
{
	char *start = *cursor;
	char *tab;

	if (!start)
		return NULL;
	tab = strchr(start, '\t');
	if (tab)
	{
		*tab = 0;
		*cursor = tab + 1;
	}
	else
	{
		*cursor = NULL;
	}
	return start;
}

int browser_ping_parse_anchors(char *text, struct browser_ping_anchor *anchors, int maximum)
{
	int count = 0;
	char *line = text;

	while (line && count < maximum)
	{
		char *next = strchr(line, '\n');
		char *cursor = line;
		char *kind, *host, *port, *regions;
		struct browser_ping_anchor *anchor = &anchors[count];
		long number;
		char *end;

		if (next)
			*next++ = 0;
		line[strcspn(line, "\r")] = 0;
		kind = field(&cursor);
		host = field(&cursor);
		port = field(&cursor);
		regions = field(&cursor);
		line = next;
		if (!kind || strcmp(kind, "anchor") || !host || !port || !regions || !host_name_valid(host))
			continue;
		number = strtol(port, &end, 10);
		if (*end || number <= 0 || number > 65535)
			continue;
		memset(anchor, 0, sizeof(*anchor));
		strcpy(anchor->host, host);
		anchor->port = (unsigned short)number;
		anchor->ping = -1;
		while (regions && *regions && anchor->region_count < BROWSER_PING_REGIONS)
		{
			char *comma = strchr(regions, ',');

			if (comma)
				*comma++ = 0;
			if (browser_ping_region_valid(regions))
				strcpy(anchor->regions[anchor->region_count++], regions);
			regions = comma;
		}
		if (anchor->region_count)
			count++;
	}
	return count;
}

long browser_ping_estimate(const struct browser_ping_anchor *anchors, int count, const char *region)
{
	char continent[3];
	long best[2] = { -1, -1 };
	int index;

	if (!browser_ping_region_valid(region))
		return -1;
	continent[0] = region[0];
	continent[1] = region[1];
	continent[2] = 0;
	for (index = 0; index < count; index++)
	{
		int named;

		if (anchors[index].ping < 0)
			continue;
		for (named = 0; named < anchors[index].region_count; named++)
		{
			const char *code = anchors[index].regions[named];
			/* 0: the country, 1: the continent */
			int match = region[2] && !strcmp(code, region) ? 0 : !strcmp(code, continent) ? 1 : -1;

			if (match >= 0 && (best[match] < 0 || anchors[index].ping < best[match]))
				best[match] = anchors[index].ping;
		}
	}
	return best[0] >= 0 ? best[0] : best[1];
}

long browser_ping_order(long ping_a, int estimated_a, long ping_b, int estimated_b)
{
	/* 0 measured, 1 estimated, 2 none */
	int rank_a = ping_a < 0 ? 2 : estimated_a ? 1 : 0;
	int rank_b = ping_b < 0 ? 2 : estimated_b ? 1 : 0;

	if (rank_a != rank_b)
		return rank_a - rank_b;
	if (rank_a == 2)
		return 0;
	return ping_a - ping_b;
}
