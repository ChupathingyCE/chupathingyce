/*
MAP_INDEX.H

Chupathingy maps' two signed documents (maps.chupathingy.org; the map
repository's docs/index-format.md), checked and read: the index, every map
the repository holds by its SHA-256 with its names, kind, size and map hash,
the blacklist and the kill switch; and the mirror list, where the files can
be downloaded. map_index.c checks a document's signature (maps_key.h, over
its context line and the document's exact bytes), its key's epoch, its
format and its expiry, and reads it strictly: a known field of the wrong
kind or out of its bounds refuses the whole document. Fields it does not
know are passed over, so later versions may add some; a map of a kind or
status it does not know is passed over too.

The index never says where a file is; the mirror list never says what a
file is. A file is taken only when its SHA-256 is the index's, and never
when its SHA-256 or map hash is on the blacklist, from any source.

No file, network or game dependency (Monocypher's Ed25519 alone): the tests
build it by itself (tools/test_map_repo.py). Plain types only.
*/

#ifndef MAP_INDEX_H
#define MAP_INDEX_H

#include <stddef.h>

/* what is signed: the context, then the document's exact bytes */
#define MAP_INDEX_CONTEXT "chupathingy-map-index 1\n"
#define MAP_MIRRORS_CONTEXT "chupathingy-map-mirrors 1\n"

enum
{
	MAP_INDEX_FORMAT = 1,
	/* the largest index read: 8 MB (some 12,000 maps) */
	MAP_INDEX_DOCUMENT_SIZE = 8 * 1024 * 1024,
	MAP_MIRRORS_DOCUMENT_SIZE = 256 * 1024,
	/* a SHA-256, and a map hash (map_archive_map_hash) */
	MAP_INDEX_HASH_SIZE = 32,
	/* a map's file name, less ".map" (map_archive.h's 31 characters) */
	MAP_INDEX_NAME_SIZE = 32,
	MAP_INDEX_ALIASES = 32,
	MAP_INDEX_REASON_SIZE = 161,
	/* a map's largest size: 1 GB (map_archive.h's limit) */
	MAP_INDEX_MAXIMUM_SIZE = 1024 * 1024 * 1024,
	/* how often an index may ask to be fetched again, in seconds */
	MAP_INDEX_REFRESH_MINIMUM = 60,
	MAP_INDEX_REFRESH_MAXIMUM = 7 * 86400,
	MAP_MIRRORS = 64,
	MAP_MIRROR_ID_SIZE = 33,
	MAP_MIRROR_URL_SIZE = 201,
	MAP_MIRROR_SHARDS = 16,
	MAP_MIRROR_SHARD_SIZE = 9,
};

/* a map's kind */
enum
{
	MAP_INDEX_KIND_CE,
	/* Halo PC retail's format (HaloMD's maps too) */
	MAP_INDEX_KIND_PC,
	MAP_INDEX_KIND_XBOX,
};

/* a blacklist entry's hash */
enum
{
	MAP_INDEX_BLOCK_SHA256,
	MAP_INDEX_BLOCK_MAP_HASH,
};

/* a mirror's status */
enum
{
	MAP_MIRROR_OK,
	MAP_MIRROR_STALE,
	MAP_MIRROR_BAD,
	/* (a word this build does not know: never used) */
	MAP_MIRROR_UNKNOWN,
};

struct map_index_info
{
	unsigned int serial;
	long long issued;
	long long expires;
	int refresh;
	/* the kill switch: nothing taken from the repository or its mirrors */
	int halt;
	char halt_reason[MAP_INDEX_REASON_SIZE];
	/* the maps of a kind and status this build reads, and the blacklist's
	entries */
	int map_count;
	int block_count;
};

struct map_index_entry
{
	/* the file's name, less ".map" (lowercase) */
	char name[MAP_INDEX_NAME_SIZE];
	unsigned char sha256[MAP_INDEX_HASH_SIZE];
	unsigned char map_hash[MAP_INDEX_HASH_SIZE];
	/* what a host of network version 29 announces: the map hash's first 32
	bits, or 1 for 0 (map_archive_hash_version) */
	unsigned int version;
	/* what an older host announces: its header's checksum */
	unsigned int header_checksum;
	unsigned int size;
	int kind;
};

struct map_index_block
{
	unsigned char hash[MAP_INDEX_HASH_SIZE];
	int kind;
	char name[MAP_INDEX_REASON_SIZE];
	char reason[MAP_INDEX_REASON_SIZE];
};

struct map_mirror
{
	char id[MAP_MIRROR_ID_SIZE];
	/* (no '/' ending it) */
	char base_url[MAP_MIRROR_URL_SIZE];
	long long priority;
	int status;
	/* the SHA-256 prefixes (lowercase hex) it holds; none: every file */
	int shard_count;
	char shards[MAP_MIRROR_SHARDS][MAP_MIRROR_SHARD_SIZE];
};

struct map_mirrors
{
	unsigned int serial;
	long long issued;
	long long expires;
	int count;
	struct map_mirror mirrors[MAP_MIRRORS];
};

/* the index (size bytes) and its signature's text (128 hex digits, white
space after them allowed), checked at now (seconds since 1970): the
signature one of maps_key.h's keys' over MAP_INDEX_CONTEXT and the
document, the key allowed the serial's epoch, the document read whole, and
not expired. 1 with its facts in info, else 0 with why in why */
int map_index_check(const char *document, size_t size, const char *signature, size_t signature_length, long long now,
	struct map_index_info *info, char *why, int why_size);
/* the same, its expiry not checked (an index kept from an earlier run,
whose blacklist outlives it: map_repo.c) */
int map_index_verify(const char *document, size_t size, const char *signature, size_t signature_length,
	struct map_index_info *info, char *why, int why_size);
/* the index alone read (its signature and expiry not checked: the tests'
and map_index_check's) */
int map_index_parse(const char *document, size_t size, struct map_index_info *info, char *why, int why_size);

/* a map of kind in a checked index: the one named name (its name or one of
its aliases, any case) whose version (or header checksum, an older host's)
is version; version 0 (the host didn't say), the only one of that name. 1
with it in entry, else 0 with why in why */
int map_index_find(const char *document, size_t size, const char *name, unsigned int version, int kind,
	struct map_index_entry *entry, char *why, int why_size);

/* whether a file of this SHA-256 or map hash (either NULL when not known)
is on a checked index's blacklist: 1 with its entry in block (NULL: not
wanted), else 0 */
int map_index_blacklisted(const char *document, size_t size, const unsigned char *sha256,
	const unsigned char *map_hash, struct map_index_block *block);

/* the mirror list, checked as map_index_check checks the index (over
MAP_MIRRORS_CONTEXT) */
int map_mirrors_check(const char *document, size_t size, const char *signature, size_t signature_length, long long now,
	struct map_mirrors *mirrors, char *why, int why_size);
int map_mirrors_verify(const char *document, size_t size, const char *signature, size_t signature_length,
	struct map_mirrors *mirrors, char *why, int why_size);
int map_mirrors_parse(const char *document, size_t size, struct map_mirrors *mirrors, char *why, int why_size);

/* the mirrors to ask for the file of this SHA-256, in the order to ask
them: status "ok", holding it (its shards), by priority (then as listed).
Their indexes in order (at most maximum); their count */
int map_mirrors_choose(const struct map_mirrors *mirrors, const unsigned char *sha256, int *order, int maximum);

/* the file's address on a mirror: <base_url>/maps/<first 2 hex>/<sha256>.map */
void map_mirror_file_url(const struct map_mirror *mirror, const unsigned char *sha256, char *url, size_t size);

/* a hash's text: 64 lowercase hex digits and a terminator */
void map_index_hash_text(const unsigned char *hash, char *text);

#endif
