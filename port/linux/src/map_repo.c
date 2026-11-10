/*
MAP_REPO.C

Chupathingy maps' index and mirror list held, refreshed and used
(map_repo.h). The index is kept as its checked document (map_index.c reads
it again for each question: a map looked for, a hash on the blacklist), the
mirror list as read. A new pair replaces the old only once it checks, and
never with a lower serial than the one held: a replayed older index, still
unexpired, would hide the blacklist entries made since.
*/

#include "map_repo.h"
#include "maps_key.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	SIGNATURE_RESPONSE_SIZE = 256,
	/* the mirrors asked for a file, at most */
	MIRRORS_ASKED = 8,
	URL_SIZE = 384,
	/* a failed refresh is tried again no sooner than this */
	RETRY_SECONDS = 300,
};

/* ---------- globals */

static pthread_mutex_t map_repo_lock = PTHREAD_MUTEX_INITIALIZER;

static struct
{
	/* the index (its checked document) and its facts */
	char *index;
	size_t index_size;
	struct map_index_info info;
	/* when it was fetched, and a refresh last failed */
	long long fetched;
	long long failed;
	struct map_mirrors mirrors;
	int has_mirrors;
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

static int index_usable(long long now)
{
	return map_repo.index && now <= map_repo.info.expires;
}

/* a document and its signature fetched: the document's size, or -1 with why */
static long fetch_pair(const struct map_repo_transport *transport, const char *url, char *document, int document_size,
	char *signature, char *why, int why_size)
{
	char signature_url[URL_SIZE + 8], error[160] = "";
	int status;
	size_t size;

	status = transport->get(transport->context, url, document, document_size, error, (int)sizeof(error));
	if (status != 200)
	{
		if (status)
			snprintf(why, (size_t)why_size, "the repository answered %d", status);
		else
			snprintf(why, (size_t)why_size, "the repository did not answer (%s)", error);
		return -1;
	}
	size = strlen(document);
	if (size >= (size_t)document_size - 1)
	{
		snprintf(why, (size_t)why_size, "it is too large");
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
	return (long)size;
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

int map_repo_refresh_due(long long now)
{
	int due;

	pthread_mutex_lock(&map_repo_lock);
	due = !map_repo.index || now - map_repo.fetched >= map_repo.info.refresh || now > map_repo.info.expires;
	/* (not again right after a failure) */
	if (due && map_repo.failed && now - map_repo.failed < RETRY_SECONDS && now >= map_repo.failed)
		due = 0;
	pthread_mutex_unlock(&map_repo_lock);
	return due;
}

int map_repo_refresh(const struct map_repo_transport *transport, const char *base_url, long long now, int force,
	char *why, int why_size)
{
	char url[URL_SIZE], signature[SIGNATURE_RESPONSE_SIZE], detail[160];
	struct map_index_info info;
	struct map_mirrors *mirrors = NULL;
	char *document;
	long size;
	int has_mirrors = 0;

	if (!map_repo_enabled())
		return fail(why, why_size, "this build has no key for it");
	if (!force && !map_repo_refresh_due(now))
	{
		int held;

		pthread_mutex_lock(&map_repo_lock);
		held = index_usable(now);
		pthread_mutex_unlock(&map_repo_lock);
		return held || fail(why, why_size, "its last refresh failed");
	}
	document = (char *)malloc(MAP_INDEX_DOCUMENT_SIZE + 2);
	if (!document)
		return fail(why, why_size, "no memory for it");
	/* (the index) */
	snprintf(url, sizeof(url), "%s/index.json", base_url);
	size = fetch_pair(transport, url, document, MAP_INDEX_DOCUMENT_SIZE + 2, signature, detail, (int)sizeof(detail));
	if (size < 0 || !map_index_check(document, (size_t)size, signature, strlen(signature), now, &info, detail,
		(int)sizeof(detail)))
	{
		free(document);
		pthread_mutex_lock(&map_repo_lock);
		map_repo.failed = now;
		pthread_mutex_unlock(&map_repo_lock);
		return fail(why, why_size, "the index was refused: %s", detail);
	}
	/* (the mirror list: without it, the index's blacklist is still kept) */
	mirrors = (struct map_mirrors *)malloc(sizeof(*mirrors));
	if (mirrors)
	{
		char *text = (char *)malloc(MAP_MIRRORS_DOCUMENT_SIZE + 2);
		long mirrors_size;

		snprintf(url, sizeof(url), "%s/mirrors.json", base_url);
		mirrors_size = text ? fetch_pair(transport, url, text, MAP_MIRRORS_DOCUMENT_SIZE + 2, signature, detail,
			(int)sizeof(detail)) : -1;
		has_mirrors = mirrors_size >= 0 && map_mirrors_check(text, (size_t)mirrors_size, signature, strlen(signature),
			now, mirrors, detail, (int)sizeof(detail));
		free(text);
	}
	pthread_mutex_lock(&map_repo_lock);
	/* (never an older one than held) */
	if (map_repo.index && info.serial < map_repo.info.serial)
	{
		map_repo.failed = now;
		pthread_mutex_unlock(&map_repo_lock);
		free(document);
		free(mirrors);
		snprintf(detail, sizeof(detail), "its serial is %u, older than the %u held", info.serial, map_repo.info.serial);
		return fail(why, why_size, "the index was refused: %s", detail);
	}
	free(map_repo.index);
	map_repo.index = document;
	map_repo.index_size = (size_t)size;
	map_repo.info = info;
	map_repo.fetched = now;
	map_repo.failed = 0;
	if (has_mirrors && (!map_repo.has_mirrors || mirrors->serial >= map_repo.mirrors.serial))
	{
		map_repo.mirrors = *mirrors;
		map_repo.has_mirrors = 1;
	}
	pthread_mutex_unlock(&map_repo_lock);
	free(mirrors);
	if (!has_mirrors && why && why_size > 0)
		snprintf(why, (size_t)why_size, "the mirror list was refused: %s", detail);
	return 1;
}

int map_repo_blocked(const unsigned char *sha256, const unsigned char *map_hash, long long now, char *reason,
	int reason_size)
{
	struct map_index_block block;
	int blocked = 0;

	pthread_mutex_lock(&map_repo_lock);
	if (index_usable(now))
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
	if (index_usable(now) && !map_repo.info.halt && map_repo.has_mirrors)
	{
		found = map_index_find(map_repo.index, map_repo.index_size, name, version, MAP_INDEX_KIND_CE, &entry, why,
			(int)sizeof(why)) && !map_index_blacklisted(map_repo.index, map_repo.index_size, entry.sha256,
			entry.map_hash, NULL);
	}
	pthread_mutex_unlock(&map_repo_lock);
	return found;
}

int map_repo_download(const struct map_repo_transport *transport, const char *name, unsigned int version,
	const char *path, long long now, struct map_index_entry *entry, int *missing, char *why, int why_size)
{
	struct map_index_block block;
	struct map_mirror chosen[MIRRORS_ASKED];
	int order[MIRRORS_ASKED];
	char detail[160] = "";
	int count = 0, index, found = 0, blocked = 0, halted = 0, mirrors = 0;

	*missing = 0;
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
			mirrors = map_repo.has_mirrors;
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
			transport->remove(transport->context, path);
			continue;
		}
		if (memcmp(sha256, entry->sha256, MAP_INDEX_HASH_SIZE))
		{
			snprintf(detail, sizeof(detail), "mirror %s gave a file that isn't the index's (its SHA-256 differs)",
				chosen[index].id);
			transport->remove(transport->context, path);
			continue;
		}
		return 1;
	}
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
	map_repo.has_mirrors = 0;
	pthread_mutex_unlock(&map_repo_lock);
}
