/* h2_tag_dump.h: a test-only dump of the Halo 2 tag fields this port reads
(the offsets its readers use, as a table) for every tag of the chosen groups
in a map, one TSV file a tag, to compare against the H2EK's field names and
values (a local checker script). HALO_H2_TAG_DUMP=<dir>
turns it on; compiled out of release builds. See h2_tag_dump.c. */
#ifndef H2_TAG_DUMP_H
#define H2_TAG_DUMP_H

#include "cseries.h"

struct h2_cache;

/* the map's dump, if HALO_H2_TAG_DUMP names a folder (once a map name) */
void h2_tag_dump_cache(
	struct h2_cache *cache);

#endif
