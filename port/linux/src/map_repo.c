/*
MAP_REPO.C

Chupathingy maps' index and mirror list held, refreshed, kept and used
(map_repo.h). The index is held as its checked document (map_index.c reads
it again for each question: a map looked for, a hash on the blacklist), the
mirror list as read.

A floor for each, as for Delta's legacy table (delta.c): the newest serial
taken, and its document's SHA-256. A new document is taken only when its
serial is above the floor, or is the floor's own document again (the same
SHA-256); an older one, or another of the same serial, is refused, so a
replayed index, still unexpired, never hides the blacklist entries made
since. A serial's top byte is its epoch (maps_key.h), so once a
recovery-signed epoch is taken, epoch 0 never is again. Each document taken
is kept in the save root with its signature (MAP_REPO_INDEX_FILE,
MAP_REPO_MIRRORS_FILE), and the floors beside them (MAP_REPO_FLOOR_FILE);
map_repo_load reads them back at start, each document checked again.

An expired index is never used for a download, but its blacklist still
holds until a newer index replaces it: a map once blocked stays blocked
while the repository can't be reached. An expired mirror list is never
used.
*/

#include "map_repo.h"
#include "maps_key.h"
#include "playlist_manifest.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	SIGNATURE_RESPONSE_SIZE = 256,
	SIGNATURE_DIGITS = 128,
	/* the mirrors asked for a file, at most */
	MIRRORS_ASKED = 8,
	URL_SIZE = 384,
	/* a failed refresh is tried again no sooner than this, doubled for each
	failure after it, up to the most */
	RETRY_SECONDS = 300,
	RETRY_SECONDS_MOST = 3600,
	FLOOR_FILE_SIZE = 512,
};

/* the files the index and the mirror list are streamed into as they are
fetched (in the save root, beside what is kept; deleted once read) */
#define INDEX_FETCH_FILE "chupathingy_maps_index.part"
#define MIRRORS_FETCH_FILE "chupathingy_maps_mirrors.part"

/* ---------- globals */

struct document_floor
{
	int has;
	unsigned int serial;
	unsigned char sha256[MAP_INDEX_HASH_SIZE];
};

/* (one refresh or load at a time: it alone frees or replaces the index) */
static pthread_mutex_t map_repo_refresh_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t map_repo_lock = PTHREAD_MUTEX_INITIALIZER;

static struct
{
	/* the index (its checked document) and its facts */
	char *index;
	size_t index_size;
	struct map_index_info info;
	/* when it was fetched (0: kept from an earlier run), and a refresh last
	failed, and the wait after it */
	long long fetched;
	long long failed;
	long long retry;
	/* the last refresh that took an index asked for it alone (no mirror
	list): one that wants the mirror list is due at once */
	int index_only;
	/* when a full refresh last got no mirror list (0: it did, or none was
	asked), and the wait after it (RETRY_SECONDS, doubled for each failure
	after it, up to RETRY_SECONDS_MOST): that alone is asked for again then,
	and only a mirror list that checks ends the waits (an index taken meanwhile
	doesn't) */
	long long mirrors_failed;
	long long mirrors_retry;
	struct map_mirrors mirrors;
	int has_mirrors;
	struct document_floor index_floor, mirrors_floor;
} map_repo;

/* ---------- private code */

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static int fail(char *why, int why_size, const char *format, ...)
{
	va_list arguments;

	if (why && why_size > 0)
	{
		va_start(arguments, format);
		vsnprintf(why, (size_t)why_size, format, arguments);
		va_end(arguments);
	}
	return 0;
}

/* (the lock held) an index unexpired at now: what downloads may use */
static int index_usable(long long now)
{
	return map_repo.index && now <= map_repo.info.expires;
}

/* (the lock held) a mirror list unexpired at now */
static int mirrors_usable(long long now)
{
	return map_repo.has_mirrors && now <= map_repo.mirrors.expires;
}

static void sha256_of(const char *data, size_t size, unsigned char *sha256)
{
	struct playlist_sha256 hash;

	playlist_sha256_begin(&hash);
	playlist_sha256_add(&hash, data, size);
	playlist_sha256_end(&hash, sha256);
}

/* whether a document of serial and this SHA-256 may be taken over floor:
1 if it is newer, 2 if it is the floor's own again, else 0 with why */
static int floor_allows(const struct document_floor *floor, unsigned int serial, const unsigned char *sha256,
	char *why, int why_size)
{
	if (!floor->has || serial > floor->serial)
		return 1;
	if (serial == floor->serial && !memcmp(sha256, floor->sha256, MAP_INDEX_HASH_SIZE))
		return 2;
	if (serial == floor->serial)
		return fail(why, why_size, "its serial is %u, as the one taken, but it differs", serial);
	return fail(why, why_size, "its serial is %u, older than the %u taken", serial, floor->serial);
}

static void floor_set(struct document_floor *floor, unsigned int serial, const unsigned char *sha256)
{
	floor->has = 1;
	floor->serial = serial;
	memcpy(floor->sha256, sha256, MAP_INDEX_HASH_SIZE);
}

/* (the lock held) the floors as their file's text */
static void floor_text(char *text, int size)
{
	char index_sha[MAP_INDEX_HASH_SIZE * 2 + 1] = "", mirrors_sha[MAP_INDEX_HASH_SIZE * 2 + 1] = "";
	int used = 0;

	text[0] = 0;
	if (map_repo.index_floor.has)
	{
		map_index_hash_text(map_repo.index_floor.sha256, index_sha);
		used = snprintf(text, (size_t)size, "index %u %s\n", map_repo.index_floor.serial, index_sha);
	}
	if (map_repo.mirrors_floor.has && used >= 0 && used < size)
	{
		map_index_hash_text(map_repo.mirrors_floor.sha256, mirrors_sha);
		snprintf(text + used, (size_t)(size - used), "mirrors %u %s\n", map_repo.mirrors_floor.serial, mirrors_sha);
	}
}

static int hex_value(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static int hex_hash(const char *text, unsigned char *hash)
{
	int index;

	for (index = 0; index < MAP_INDEX_HASH_SIZE; index++)
	{
		int high = hex_value(text[index * 2]), low = high < 0 ? -1 : hex_value(text[index * 2 + 1]);

		if (low < 0)
			return 0;
		hash[index] = (unsigned char)(high << 4 | low);
	}
	return 1;
}

/* the floors' file read: each line "<index|mirrors> <serial> <sha256>" */
static void floor_parse(const char *text, struct document_floor *index_floor, struct document_floor *mirrors_floor)
{
	while (*text)
	{
		char name[16], sha[MAP_INDEX_HASH_SIZE * 2 + 1];
		unsigned int serial;
		unsigned char hash[MAP_INDEX_HASH_SIZE];
		const char *line_end = strchr(text, '\n');

		if (sscanf(text, "%15s %u %64s", name, &serial, sha) == 3 && strlen(sha) == MAP_INDEX_HASH_SIZE * 2 &&
			hex_hash(sha, hash))
		{
			struct document_floor *floor = !strcmp(name, "index") ? index_floor :
				!strcmp(name, "mirrors") ? mirrors_floor : NULL;

			if (floor && (!floor->has || serial > floor->serial))
				floor_set(floor, serial, hash);
		}
		if (!line_end)
			break;
		text = line_end + 1;
	}
}

/* a document kept as its signature's digits, a line feed, then its exact
bytes (as Delta's legacy table is kept) */
static void keep(const struct map_repo_transport *transport, const char *name, const char *document, size_t size,
	const char *signature)
{
	char *data;

	if (!transport->save)
		return;
	data = (char *)malloc(SIGNATURE_DIGITS + 1 + size);
	if (!data)
		return;
	memcpy(data, signature, SIGNATURE_DIGITS);
	data[SIGNATURE_DIGITS] = '\n';
	memcpy(data + SIGNATURE_DIGITS + 1, document, size);
	transport->save(transport->context, name, data, (long)(SIGNATURE_DIGITS + 1 + size));
	free(data);
}

static void keep_floors(const struct map_repo_transport *transport)
{
	char text[FLOOR_FILE_SIZE];

	if (!transport->save)
		return;
	pthread_mutex_lock(&map_repo_lock);
	floor_text(text, (int)sizeof(text));
	pthread_mutex_unlock(&map_repo_lock);
	transport->save(transport->context, MAP_REPO_FLOOR_FILE, text, (long)strlen(text));
}

/* a kept document read back: its bytes (allocated, NUL terminated; the
caller frees it) with their count in *size and its signature's digits in
signature, else NULL */
static char *kept(const struct map_repo_transport *transport, const char *name, long maximum, size_t *size,
	char *signature)
{
	char *data;
	long count;

	if (!transport->load)
		return NULL;
	data = (char *)malloc((size_t)maximum + SIGNATURE_DIGITS + 2);
	if (!data)
		return NULL;
	count = transport->load(transport->context, name, data, maximum + SIGNATURE_DIGITS + 1);
	if (count <= SIGNATURE_DIGITS || count > maximum + SIGNATURE_DIGITS + 1 || data[SIGNATURE_DIGITS] != '\n')
	{
		free(data);
		return NULL;
	}
	memcpy(signature, data, SIGNATURE_DIGITS);
	signature[SIGNATURE_DIGITS] = 0;
	*size = (size_t)count - SIGNATURE_DIGITS - 1;
	memmove(data, data + SIGNATURE_DIGITS + 1, *size);
	data[*size] = 0;
	return data;
}

/* a document (at most maximum bytes) and its signature fetched, the
document streamed through the file kept as name: the document's size, or
-1 with why */
static long fetch_pair(const struct map_repo_transport *transport, const char *url, const char *name, char *document,
	long maximum, char *signature, char *why, int why_size)
{
	char signature_url[URL_SIZE + 8], error[160] = "";
	long size = -1;
	int status = 0;

	if (!transport->get_document(transport->context, url, name, document, maximum, &size, &status, error,
		(int)sizeof(error)))
	{
		if (status && status != 200)
			snprintf(why, (size_t)why_size, "the repository answered %d", status);
		else if (status)
			snprintf(why, (size_t)why_size, "%s", error);
		else
			snprintf(why, (size_t)why_size, "the repository did not answer (%s)", error);
		return -1;
	}
	/* (a document is text: no NUL in it) */
	if (size < 0 || size > maximum || strlen(document) != (size_t)size)
	{
		snprintf(why, (size_t)why_size, "it is not a text document");
		return -1;
	}
	snprintf(signature_url, sizeof(signature_url), "%s.sig", url);
	status = transport->get(transport->context, signature_url, signature, SIGNATURE_RESPONSE_SIZE, error,
		(int)sizeof(error));
	if (status != 200)
	{
		snprintf(why, (size_t)why_size, "its signature could not be fetched");
		return -1;
	}
	return size;
}

/* (the lock held) a failed refresh: the next no sooner than its wait,
doubled each time */
static void refresh_failed(long long now)
{
	map_repo.failed = now;
	map_repo.retry = !map_repo.retry ? RETRY_SECONDS :
		map_repo.retry * 2 > RETRY_SECONDS_MOST ? RETRY_SECONDS_MOST : map_repo.retry * 2;
}

/* (the lock held) a mirror list that failed on a full refresh: asked for
again no sooner than its own wait, doubled each time */
static void mirrors_failed(long long now)
{
	map_repo.mirrors_failed = now;
	map_repo.mirrors_retry = !map_repo.mirrors_retry ? RETRY_SECONDS :
		map_repo.mirrors_retry * 2 > RETRY_SECONDS_MOST ? RETRY_SECONDS_MOST : map_repo.mirrors_retry * 2;
}

/* (the lock held) what a refresh with flags would fetch at now: the index
(with the mirror list unless flags ask for the index alone), or the mirror
list alone, its index still good */
static void refresh_wants(long long now, int flags, int *index_due, int *mirrors_due)
{
	*index_due = !map_repo.index || !map_repo.fetched || now - map_repo.fetched >= map_repo.info.refresh ||
		now > map_repo.info.expires;
	*mirrors_due = !(flags & MAP_REPO_REFRESH_INDEX_ONLY) && (map_repo.index_only || (map_repo.mirrors_failed &&
		now >= map_repo.mirrors_failed && now - map_repo.mirrors_failed >= map_repo.mirrors_retry));
	/* (not again before a failure's wait is over) */
	if (map_repo.failed && now - map_repo.failed < map_repo.retry && now >= map_repo.failed)
		*index_due = *mirrors_due = 0;
}

/* ---------- public code */

int map_repo_enabled(void)
{
	int index, byte;

	for (index = 0; index < MAPS_KEY_COUNT; index++)
	{
		for (byte = 0; byte < 32; byte++)
		{
			if (maps_public_keys[index][byte])
				return 1;
		}
	}
	return 0;
}

int map_repo_refresh_due(long long now, int flags)
{
	int index_due, mirrors_due;

	pthread_mutex_lock(&map_repo_lock);
	refresh_wants(now, flags, &index_due, &mirrors_due);
	pthread_mutex_unlock(&map_repo_lock);
	return index_due || mirrors_due;
}

int map_repo_load(const struct map_repo_transport *transport, long long now, char *why, int why_size)
{
	struct document_floor index_floor, mirrors_floor;
	struct map_index_info info;
	struct map_mirrors *mirrors;
	unsigned char sha256[MAP_INDEX_HASH_SIZE];
	char signature[SIGNATURE_DIGITS + 1], detail[160] = "", floors[FLOOR_FILE_SIZE];
	char *document;
	size_t size = 0;
	long count;
	int held = 0, raised = 0;

	if (why && why_size > 0)
		why[0] = 0;
	if (!map_repo_enabled() || !transport->load)
		return 0;
	pthread_mutex_lock(&map_repo_refresh_lock);
	pthread_mutex_lock(&map_repo_lock);
	index_floor = map_repo.index_floor;
	mirrors_floor = map_repo.mirrors_floor;
	pthread_mutex_unlock(&map_repo_lock);
	count = transport->load(transport->context, MAP_REPO_FLOOR_FILE, floors, (long)sizeof(floors) - 1);
	if (count > 0)
	{
		floors[count] = 0;
		floor_parse(floors, &index_floor, &mirrors_floor);
	}
	/* (the index: checked again, its expiry aside, and held unless the
	floor is past it) */
	document = kept(transport, MAP_REPO_INDEX_FILE, MAP_INDEX_DOCUMENT_SIZE, &size, signature);
	if (document)
	{
		int allowed = 0;

		sha256_of(document, size, sha256);
		if (map_index_verify(document, size, signature, SIGNATURE_DIGITS, &info, detail, (int)sizeof(detail)))
			allowed = floor_allows(&index_floor, info.serial, sha256, detail, (int)sizeof(detail));
		if (allowed)
		{
			raised |= allowed == 1;
			floor_set(&index_floor, info.serial, sha256);
			pthread_mutex_lock(&map_repo_lock);
			free(map_repo.index);
			map_repo.index = document;
			map_repo.index_size = size;
			map_repo.info = info;
			map_repo.fetched = 0;
			pthread_mutex_unlock(&map_repo_lock);
			document = NULL;
			held = 1;
			fail(why, why_size, "kept index %u (%s)", info.serial, now > info.expires ? "expired: its blacklist alone "
				"used" : "unexpired");
		}
		else
			fail(why, why_size, "the kept index was dropped: %s", detail);
		free(document);
	}
	/* (the mirror list, the same way) */
	mirrors = (struct map_mirrors *)malloc(sizeof(*mirrors));
	document = mirrors ? kept(transport, MAP_REPO_MIRRORS_FILE, MAP_MIRRORS_DOCUMENT_SIZE, &size, signature) : NULL;
	if (document)
	{
		int allowed = 0;

		sha256_of(document, size, sha256);
		if (map_mirrors_verify(document, size, signature, SIGNATURE_DIGITS, mirrors, detail, (int)sizeof(detail)))
			allowed = floor_allows(&mirrors_floor, mirrors->serial, sha256, detail, (int)sizeof(detail));
		if (allowed)
		{
			raised |= allowed == 1;
			floor_set(&mirrors_floor, mirrors->serial, sha256);
			pthread_mutex_lock(&map_repo_lock);
			map_repo.mirrors = *mirrors;
			map_repo.has_mirrors = 1;
			pthread_mutex_unlock(&map_repo_lock);
		}
		free(document);
	}
	free(mirrors);
	pthread_mutex_lock(&map_repo_lock);
	map_repo.index_floor = index_floor;
	map_repo.mirrors_floor = mirrors_floor;
	pthread_mutex_unlock(&map_repo_lock);
	/* (a document newer than the floors' file: the file catches up) */
	if (raised)
		keep_floors(transport);
	pthread_mutex_unlock(&map_repo_refresh_lock);
	return held;
}

int map_repo_refresh(const struct map_repo_transport *transport, const char *base_url, long long now, int flags,
	char *why, int why_size)
{
	char url[URL_SIZE], signature[SIGNATURE_RESPONSE_SIZE], mirrors_signature[SIGNATURE_RESPONSE_SIZE];
	char detail[160] = "", mirrors_detail[160] = "";
	unsigned char sha256[MAP_INDEX_HASH_SIZE], mirrors_sha256[MAP_INDEX_HASH_SIZE];
	struct map_index_info info;
	struct map_mirrors *mirrors = NULL;
	char *document = NULL, *mirrors_text = NULL;
	long size = 0, mirrors_size = -1;
	int has_mirrors = 0, index_allowed = 0, mirrors_allowed = 0, index_due, mirrors_due, held;

	if (why && why_size > 0)
		why[0] = 0;
	if (!map_repo_enabled())
		return fail(why, why_size, "this build has no key for it");
	if (!(flags & MAP_REPO_REFRESH_FORCE) && !map_repo_refresh_due(now, flags))
	{
		pthread_mutex_lock(&map_repo_lock);
		held = index_usable(now);
		pthread_mutex_unlock(&map_repo_lock);
		return held || fail(why, why_size, "its last refresh failed");
	}
	pthread_mutex_lock(&map_repo_refresh_lock);
	/* (another refresh may have run meanwhile) */
	pthread_mutex_lock(&map_repo_lock);
	refresh_wants(now, flags, &index_due, &mirrors_due);
	if (flags & MAP_REPO_REFRESH_FORCE)
		index_due = 1;
	held = index_usable(now);
	pthread_mutex_unlock(&map_repo_lock);
	if (!index_due && !mirrors_due)
	{
		pthread_mutex_unlock(&map_repo_refresh_lock);
		return held || fail(why, why_size, "its last refresh failed");
	}
	/* (the index, unless only the mirror list is due: one that failed while
	the index is still good is asked for alone) */
	if (index_due)
	{
		document = (char *)malloc(MAP_INDEX_DOCUMENT_SIZE + 2);
		if (!document)
		{
			pthread_mutex_unlock(&map_repo_refresh_lock);
			return fail(why, why_size, "no memory for it");
		}
		snprintf(url, sizeof(url), "%s/index.json", base_url);
		size = fetch_pair(transport, url, INDEX_FETCH_FILE, document, MAP_INDEX_DOCUMENT_SIZE, signature, detail,
			(int)sizeof(detail));
		if (size < 0 || !map_index_check(document, (size_t)size, signature, strlen(signature), now, &info, detail,
			(int)sizeof(detail)))
		{
			free(document);
			pthread_mutex_lock(&map_repo_lock);
			refresh_failed(now);
			pthread_mutex_unlock(&map_repo_lock);
			pthread_mutex_unlock(&map_repo_refresh_lock);
			return fail(why, why_size, "the index was refused: %s", detail);
		}
		sha256_of(document, (size_t)size, sha256);
	}
	/* (the mirror list: without it, the index's blacklist is still kept; not
	asked for at all when only the blacklist is wanted) */
	if (!(flags & MAP_REPO_REFRESH_INDEX_ONLY))
	{
		mirrors = (struct map_mirrors *)malloc(sizeof(*mirrors));
		mirrors_text = mirrors ? (char *)malloc(MAP_MIRRORS_DOCUMENT_SIZE + 2) : NULL;
		snprintf(url, sizeof(url), "%s/mirrors.json", base_url);
		mirrors_size = mirrors_text ? fetch_pair(transport, url, MIRRORS_FETCH_FILE, mirrors_text,
			MAP_MIRRORS_DOCUMENT_SIZE, mirrors_signature, mirrors_detail, (int)sizeof(mirrors_detail)) : -1;
		has_mirrors = mirrors_size >= 0 && map_mirrors_check(mirrors_text, (size_t)mirrors_size, mirrors_signature,
			strlen(mirrors_signature), now, mirrors, mirrors_detail, (int)sizeof(mirrors_detail));
		if (has_mirrors)
			sha256_of(mirrors_text, (size_t)mirrors_size, mirrors_sha256);
	}
	pthread_mutex_lock(&map_repo_lock);
	if (index_due)
	{
		/* (never an older one than taken, nor another of the same serial) */
		index_allowed = floor_allows(&map_repo.index_floor, info.serial, sha256, detail, (int)sizeof(detail));
		if (!index_allowed)
		{
			refresh_failed(now);
			pthread_mutex_unlock(&map_repo_lock);
			pthread_mutex_unlock(&map_repo_refresh_lock);
			free(document);
			free(mirrors);
			free(mirrors_text);
			return fail(why, why_size, "the index was refused: %s", detail);
		}
		free(map_repo.index);
		map_repo.index = document;
		map_repo.index_size = (size_t)size;
		map_repo.info = info;
		map_repo.fetched = now;
		map_repo.failed = 0;
		map_repo.retry = 0;
		floor_set(&map_repo.index_floor, info.serial, sha256);
	}
	map_repo.index_only = (flags & MAP_REPO_REFRESH_INDEX_ONLY) != 0;
	if (has_mirrors)
	{
		mirrors_allowed = floor_allows(&map_repo.mirrors_floor, mirrors->serial, mirrors_sha256, mirrors_detail,
			(int)sizeof(mirrors_detail));
		if (mirrors_allowed)
		{
			map_repo.mirrors = *mirrors;
			map_repo.has_mirrors = 1;
			floor_set(&map_repo.mirrors_floor, mirrors->serial, mirrors_sha256);
		}
	}
	/* (a mirror list that failed is asked for again after its own wait, not
	at the index's next refresh; one that checks ends the waits) */
	if (!(flags & MAP_REPO_REFRESH_INDEX_ONLY))
	{
		if (mirrors_allowed)
		{
			map_repo.mirrors_failed = 0;
			map_repo.mirrors_retry = 0;
		}
		else
			mirrors_failed(now);
	}
	held = index_usable(now);
	pthread_mutex_unlock(&map_repo_lock);
	/* (each document taken anew kept, then the floors: map_repo_load) */
	if (index_allowed == 1)
		keep(transport, MAP_REPO_INDEX_FILE, document, (size_t)size, signature);
	if (mirrors_allowed == 1)
		keep(transport, MAP_REPO_MIRRORS_FILE, mirrors_text, (size_t)mirrors_size, mirrors_signature);
	if (index_allowed == 1 || mirrors_allowed == 1)
		keep_floors(transport);
	pthread_mutex_unlock(&map_repo_refresh_lock);
	free(mirrors);
	free(mirrors_text);
	if (!(flags & MAP_REPO_REFRESH_INDEX_ONLY) && !mirrors_allowed)
		fail(why, why_size, "the mirror list was refused: %s", mirrors_detail);
	/* (an index taken now, or the one held when the mirror list alone was) */
	return index_due || held;
}

int map_repo_index_held(void)
{
	int held;

	pthread_mutex_lock(&map_repo_lock);
	held = map_repo.index != NULL;
	pthread_mutex_unlock(&map_repo_lock);
	return held;
}

int map_repo_blocked(const unsigned char *sha256, const unsigned char *map_hash, long long now, char *reason,
	int reason_size)
{
	struct map_index_block block;
	int blocked = 0;

	/* (an expired index's blacklist too, until a newer replaces it) */
	(void)now;
	pthread_mutex_lock(&map_repo_lock);
	if (map_repo.index)
		blocked = map_index_blacklisted(map_repo.index, map_repo.index_size, sha256, map_hash, &block);
	pthread_mutex_unlock(&map_repo_lock);
	if (blocked && reason && reason_size > 0)
		snprintf(reason, (size_t)reason_size, "%s", block.reason[0] ? block.reason : "it is on the blacklist");
	return blocked;
}

int map_repo_has(const char *name, unsigned int version, long long now)
{
	struct map_index_entry entry;
	char why[8];
	int found = 0;

	pthread_mutex_lock(&map_repo_lock);
	if (index_usable(now) && !map_repo.info.halt && mirrors_usable(now))
	{
		found = map_index_find(map_repo.index, map_repo.index_size, name, version, MAP_INDEX_KIND_CE, &entry, why,
			(int)sizeof(why)) && !map_index_blacklisted(map_repo.index, map_repo.index_size, entry.sha256,
			entry.map_hash, NULL);
	}
	pthread_mutex_unlock(&map_repo_lock);
	return found;
}

int map_repo_download(const struct map_repo_transport *transport, const char *name, unsigned int version,
	const char *path, long long now, struct map_index_entry *entry, int *missing, int *refused, char *why,
	int why_size)
{
	struct map_index_block block;
	struct map_mirror chosen[MIRRORS_ASKED];
	int order[MIRRORS_ASKED];
	char detail[160] = "";
	int count = 0, index, found = 0, blocked = 0, halted = 0, mirrors = 0, mismatched = 0;

	*missing = 0;
	if (refused)
		*refused = MAP_REPO_REFUSED_NONE;
	memset(entry, 0, sizeof(*entry));
	pthread_mutex_lock(&map_repo_lock);
	if (index_usable(now))
	{
		halted = map_repo.info.halt;
		if (halted)
			snprintf(detail, sizeof(detail), "%s", map_repo.info.halt_reason);
		else
		{
			found = map_index_find(map_repo.index, map_repo.index_size, name, version, MAP_INDEX_KIND_CE, entry, detail,
				(int)sizeof(detail));
		}
		if (found)
		{
			blocked = map_index_blacklisted(map_repo.index, map_repo.index_size, entry->sha256, entry->map_hash, &block);
			mirrors = mirrors_usable(now);
			if (!blocked && mirrors)
			{
				count = map_mirrors_choose(&map_repo.mirrors, entry->sha256, order, MIRRORS_ASKED);
				for (index = 0; index < count; index++)
					chosen[index] = map_repo.mirrors.mirrors[order[index]];
			}
		}
	}
	else
		snprintf(detail, sizeof(detail), "%s", "no index that checks");
	pthread_mutex_unlock(&map_repo_lock);
	if (halted)
		return fail(why, why_size, "Chupathingy maps has stopped all downloads for now (%s)",
			detail[0] ? detail : "no reason given");
	if (!found)
	{
		*missing = 1;
		return fail(why, why_size, "%s", detail);
	}
	if (blocked && refused)
		*refused = MAP_REPO_REFUSED_BLOCKED;
	if (blocked)
		return fail(why, why_size, "it is blocked: %s", block.reason[0] ? block.reason : "on the blacklist");
	if (!mirrors)
		return fail(why, why_size, "Chupathingy maps has no mirror list that checks");
	if (!count)
		return fail(why, why_size, "no mirror of Chupathingy maps has it right now");
	/* (each mirror in turn until one gives the index's file: a mirror is
	never trusted, only the file's size and SHA-256) */
	for (index = 0; index < count; index++)
	{
		unsigned char sha256[MAP_INDEX_HASH_SIZE];
		char url[URL_SIZE];
		unsigned int received = 0;
		int status = 0;

		if (transport->stop && transport->stop(transport->context))
			return fail(why, why_size, "cancelled");
		map_mirror_file_url(&chosen[index], entry->sha256, url, sizeof(url));
		if (!transport->get_file(transport->context, url, path, entry->size, sha256, &received, &status, detail,
			(int)sizeof(detail)))
		{
			transport->remove(transport->context, path);
			continue;
		}
		if (received != entry->size)
		{
			snprintf(detail, sizeof(detail), "mirror %s gave %u bytes, not %u", chosen[index].id, received, entry->size);
			mismatched = 1;
			transport->remove(transport->context, path);
			continue;
		}
		if (memcmp(sha256, entry->sha256, MAP_INDEX_HASH_SIZE))
		{
			snprintf(detail, sizeof(detail), "mirror %s gave a file that isn't the index's (its SHA-256 differs)",
				chosen[index].id);
			mismatched = 1;
			transport->remove(transport->context, path);
			continue;
		}
		return 1;
	}
	if (mismatched && refused)
		*refused = MAP_REPO_REFUSED_MISMATCH;
	return fail(why, why_size, "no mirror gave it (%s)", detail);
}

void map_repo_reset(void)
{
	pthread_mutex_lock(&map_repo_lock);
	free(map_repo.index);
	map_repo.index = NULL;
	map_repo.index_size = 0;
	memset(&map_repo.info, 0, sizeof(map_repo.info));
	map_repo.fetched = 0;
	map_repo.failed = 0;
	map_repo.retry = 0;
	map_repo.index_only = 0;
	map_repo.mirrors_failed = 0;
	map_repo.mirrors_retry = 0;
	map_repo.has_mirrors = 0;
	memset(&map_repo.index_floor, 0, sizeof(map_repo.index_floor));
	memset(&map_repo.mirrors_floor, 0, sizeof(map_repo.mirrors_floor));
	pthread_mutex_unlock(&map_repo_lock);
}
