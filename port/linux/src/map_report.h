/*
MAP_REPORT.H

A map's identity and shape, for the site's map catalog: the family, the
file's name, size and BLAKE2b-256 hash (Delta Peer's MAP: delta_maps.c), the
cache header's build string, version and checksum (the game record's
map.version), the scenario's tag path, the game types it has netgame flags
for, its spawn count and its structure BSP's world bounds.

It is a JSON object, built here and sent in a Delta Stats batch as the
game's "map_info", beside its "map" (game_events.c; docs/delta.md, "Map
facts"). Facts about the map only: no pictures, no file contents, nothing
about players or machines. Plain C89 and plain types (the game's units, whose
long 64-bit builds rewrite, fill it in), no game headers, so the tests build
it alone (tools/test_map_report.py).
*/

#ifndef HALO_MAP_REPORT_H
#define HALO_MAP_REPORT_H

#define MAP_REPORT_HASH_SIZE 32
#define MAP_REPORT_FILE_SIZE 64
#define MAP_REPORT_BUILD_SIZE 33
#define MAP_REPORT_SCENARIO_SIZE 128
/* the cache header's size, and where its fields are (cache_files.c's
struct cache_file_header) */
#define MAP_REPORT_HEADER_SIZE 0x800

/* the game types a scenario has netgame flags for */
enum
{
	MAP_REPORT_SLAYER = 1 << 0,
	MAP_REPORT_CTF = 1 << 1,
	MAP_REPORT_ODDBALL = 1 << 2,
	MAP_REPORT_KING = 1 << 3,
	MAP_REPORT_RACE = 1 << 4
};

struct map_report
{
	/* halo_map_families.h's family: 0 the Xbox's, 1 Custom Edition, 2
	HaloMD, 3 Halo PC retail */
	int family;
	char file[MAP_REPORT_FILE_SIZE];
	unsigned int size_low;
	unsigned int size_high;
	int has_hash;
	unsigned char hash[MAP_REPORT_HASH_SIZE];
	int cache_version;
	unsigned int header_checksum;
	char build[MAP_REPORT_BUILD_SIZE];
	char scenario[MAP_REPORT_SCENARIO_SIZE];
	unsigned int gametypes;
	int spawns;
	int has_bounds;
	/* x0, x1, y0, y1, z0, z1 */
	float bounds[6];
};

/* the header's version, build string and checksum into report: 1, or 0 if
header (size bytes) is not a cache file's header ("head" ... "foot") */
int map_report_read_header(const unsigned char *header, int size, struct map_report *report);

/* the report as JSON into out (size bytes, NUL included): its length, or 0
if it does not fit or the report cannot be sent (a family past the known
ones, a file name MAP could not carry, or a map past the Xbox's without its
hash) */
int map_report_json(const struct map_report *report, char *out, int size);

#endif
