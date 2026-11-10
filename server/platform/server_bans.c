/*
SERVER_BANS.C

Shared ban lists (server_bans.h; docs/delta.md, "Shared ban lists"). What
goes to the site is read from bans.txt (the lines that name an account or
a device, never an address or hardware ID), and sent again only when it
changed since the site last said what it has. What comes from the site is
kept here, under one lock, and in shared_bans.cache by the link
(control_link.c, with the link secret's MAC). Exceptions this server makes
(sv_unban on a shared entry) are shared_unbans.txt's refs.
*/

#include "../src/server_bans.h"
#include "control_protocol.h"

#include "monocypher.h"

/* (the files, on Linux and Windows) */
#include "server_host.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* the platform layer's (xbox_files.c): plain types */
const char *platform_data_root(void);

/* ---------- constants */

#define BANS_FILE "bans.txt"
#define EXCEPTIONS_FILE "shared_unbans.txt"
#define REF_KEY_FILE "shared_bans.key"

enum
{
	LINE_SIZE = 1024,
	MAXIMUM_EXCEPTIONS = 512,
	/* the shared entries' text a poll carries, at most (the rest go when
	there is room: a poll's body stays under what the site reads) */
	SHARE_ENTRIES_MOST = 96 * 1024,
	REF_KEY_BYTES = 32,
	/* bans.txt is read again for the share at most this often, unless it
	changed */
	SHARE_READ_SECONDS = 30,
};

/* ---------- globals */

static pthread_mutex_t bans_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct
{
	int sharing;
	/* the digest the site last said it has, and the one sent last */
	int answered_set;
	char answered[65];
	char sent[65];
	/* the entries taken, their version, whether from the cache, and when */
	struct server_shared_ban shared[SERVER_BAN_SHARED_MOST];
	int shared_count;
	unsigned long long version;
	int cached;
	long long received;
	int changed;
	/* the exceptions, as last read */
	int exceptions_read;
	char exceptions[MAXIMUM_EXCEPTIONS][SERVER_BAN_REF_SIZE];
	int exception_count;
	/* the key a ban's ref is made with (shared_bans.key) */
	int ref_key_read;
	uint8_t ref_key[REF_KEY_BYTES];
} bans;

/* ---------- private code */

static long long now_seconds(void)
{
	return (long long)time(NULL);
}

static void data_file(const char *name, char *path, size_t size)
{
	const char *root = platform_data_root();

	snprintf(path, size, "%s%s%s", root ? root : "", root && root[0] ? "/" : "", name);
}

static int hex_digit(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 :
		-1;
}

/* 32 hex digits (exactly: what follows is the caller's to check) into 16
bytes: 1, else 0 */
static int parse_id(const char *text, unsigned char *id)
{
	int index;

	for (index = 0; index < SERVER_BAN_ID_BYTES; index++)
	{
		int high = hex_digit(text[2 * index]);
		int low = high < 0 ? -1 : hex_digit(text[2 * index + 1]);

		if (low < 0)
			return 0;
		id[index] = (unsigned char)(high << 4 | low);
	}
	return 1;
}

/* a field of a bans.txt line ("name=" at the line's start or after a tab),
up to the next tab or the line's end: 1, else 0 */
static int line_field(const char *line, const char *name, char *text, size_t size)
{
	const char *field = line;
	size_t name_length = strlen(name);
	size_t length = 0;

	if (size)
		text[0] = 0;
	while (field && *field)
	{
		if ((field == line || field[-1] == '\t') && !strncmp(field, name, name_length))
			break;
		field = strchr(field, '\t');
		if (field)
			field++;
	}
	if (!field || !*field)
		return 0;
	for (field += name_length; *field && *field != '\t' && *field != '\r' && *field != '\n' && length + 1 < size;
		field++)
	{
		text[length++] = *field;
	}
	text[length] = 0;
	return 1;
}

/* text kept to printable ASCII (what the JSON and the site take) */
static void printable(char *text)
{
	for (; *text; text++)
	{
		if ((unsigned char)*text < 0x20 || (unsigned char)*text > 0x7E)
			*text = '?';
	}
}

/* an id list ("a,b,c" of 32 hex digits each) into ids, at most most: the
count (an item that is not one is skipped) */
static int parse_ids(const char *text, unsigned char ids[][SERVER_BAN_ID_BYTES], int most)
{
	int count = 0;

	while (*text && count < most)
	{
		size_t length = strcspn(text, ",");

		if (length == 2 * SERVER_BAN_ID_BYTES && parse_id(text, ids[count]))
			count++;
		text += length;
		if (*text == ',')
			text++;
	}
	return count;
}

static void read_exceptions(void)
{
	char path[1024], line[128];
	FILE *file;

	bans.exception_count = 0;
	bans.exceptions_read = 1;
	data_file(EXCEPTIONS_FILE, path, sizeof(path));
	file = fopen(path, "r");
	if (!file)
		return;
	while (fgets(line, sizeof(line), file) && bans.exception_count < MAXIMUM_EXCEPTIONS)
	{
		size_t length = strspn(line, "0123456789abcdef");

		if (line[0] == '#' || length != 2 * SERVER_BAN_ID_BYTES)
			continue;
		memcpy(bans.exceptions[bans.exception_count], line, length);
		bans.exceptions[bans.exception_count][length] = 0;
		bans.exception_count++;
	}
	fclose(file);
}

static int excepted(const char *ref)
{
	int index;

	if (!bans.exceptions_read)
		read_exceptions();
	for (index = 0; index < bans.exception_count; index++)
	{
		if (!strcmp(bans.exceptions[index], ref))
			return 1;
	}
	return 0;
}

/* (the lock held) this server's key for its bans' refs: shared_bans.key in
the data folder, 32 random bytes made the first time. A ref is the line
keyed with it, so it says nothing of the line (its address or hardware
ID) to anyone without the key, and stays the same while the line does.
1, else 0 */
static int ref_key(void)
{
	char path[1024], temporary[1040];
	FILE *file;

	if (bans.ref_key_read)
		return 1;
	data_file(REF_KEY_FILE, path, sizeof(path));
	file = fopen(path, "rb");
	if (file)
	{
		bans.ref_key_read = fread(bans.ref_key, 1, sizeof(bans.ref_key), file) == sizeof(bans.ref_key);
		fclose(file);
		if (bans.ref_key_read)
			return 1;
	}
	if (!host_random_bytes(bans.ref_key, sizeof(bans.ref_key)))
		return 0;
	snprintf(temporary, sizeof(temporary), "%s.new", path);
	file = fopen(temporary, "wb");
	if (!file)
		return 0;
	if (fwrite(bans.ref_key, 1, sizeof(bans.ref_key), file) != sizeof(bans.ref_key) || fclose(file) != 0 ||
		rename(temporary, path) != 0)
	{
		remove(temporary);
		return 0;
	}
	bans.ref_key_read = 1;
	return 1;
}

static void put_id(const unsigned char *id, char *text)
{
	control_hex_text(id, SERVER_BAN_ID_BYTES, text);
}

/* an entry's JSON for the share: its length, or -1 if it does not fit */
static int entry_json(const struct server_shared_ban *ban, char *out, size_t size)
{
	char reason[SERVER_BAN_REASON_SIZE * 6 + 3];
	char id[2 * SERVER_BAN_ID_BYTES + 1];
	size_t length;
	int index;

	if (control_json_string(ban->reason, reason, sizeof(reason)) < 0)
		return -1;
	length = (size_t)snprintf(out, size, "{\"ref\": \"%s\", \"accounts\": [", ban->ref);
	for (index = 0; index < ban->account_count && length < size; index++)
	{
		put_id(ban->accounts[index], id);
		length += (size_t)snprintf(out + length, size - length, "%s\"%s\"", index ? ", " : "", id);
	}
	if (length >= size)
		return -1;
	if (ban->has_device)
	{
		put_id(ban->device, id);
		length += (size_t)snprintf(out + length, size - length, "], \"device\": \"%s\", \"handles\": [", id);
	}
	else
		length += (size_t)snprintf(out + length, size - length, "], \"device\": null, \"handles\": [");
	/* (the handles, as their own strings) */
	{
		const char *cursor = ban->handles;
		int first = 1, handles = 0;

		/* (at most 4, each at most 31 characters: what the site takes) */
		while (*cursor && length < size && handles < 4)
		{
			char one[SERVER_BAN_HANDLES_SIZE], quoted[SERVER_BAN_HANDLES_SIZE * 6 + 3];
			size_t one_length = strcspn(cursor, ",");

			while (one_length && *cursor == ' ')
				cursor++, one_length--;
			if (one_length > 31)
				one_length = 31;
			if (one_length)
			{
				handles++;
				memcpy(one, cursor, one_length);
				one[one_length] = 0;
				if (control_json_string(one, quoted, sizeof(quoted)) < 0)
					return -1;
				length += (size_t)snprintf(out + length, size - length, "%s%s", first ? "" : ", ", quoted);
				first = 0;
			}
			cursor += strcspn(cursor, ",");
			if (*cursor == ',')
				cursor++;
		}
	}
	if (length < size)
	{
		length += (size_t)snprintf(out + length, size - length, "], \"reason\": %s, \"time\": %lld, \"until\": %lld}",
			reason, ban->time, ban->until);
	}
	return length < size ? (int)length : -1;
}

/* ---------- public code */

int server_bans_sharing(void)
{
	int sharing;

	pthread_mutex_lock(&bans_mutex);
	sharing = bans.sharing;
	pthread_mutex_unlock(&bans_mutex);
	return sharing;
}

void server_bans_set_sharing(int sharing)
{
	pthread_mutex_lock(&bans_mutex);
	bans.sharing = sharing ? 1 : 0;
	pthread_mutex_unlock(&bans_mutex);
}

int server_bans_line_entry(const char *line, long long now, struct server_shared_ban *ban)
{
	char accounts[256], device[80], text[256], line_copy[LINE_SIZE];
	unsigned char hash[SERVER_BAN_ID_BYTES];
	size_t length;

	memset(ban, 0, sizeof(*ban));
	if (!line || line[0] == '#')
		return 0;
	/* (its end left out of the ref) */
	length = strcspn(line, "\r\n");
	if (length >= sizeof(line_copy))
		return 0;
	memcpy(line_copy, line, length);
	line_copy[length] = 0;
	if (line_field(line_copy, "account=", accounts, sizeof(accounts)))
		ban->account_count = parse_ids(accounts, ban->accounts, SERVER_BAN_ACCOUNTS);
	if (line_field(line_copy, "device=", device, sizeof(device)) && strlen(device) == 2 * SERVER_BAN_ID_BYTES)
		ban->has_device = parse_id(device, ban->device);
	if (!ban->account_count && !ban->has_device)
		return 0;
	if (line_field(line_copy, "until=", text, sizeof(text)))
	{
		ban->until = strtoll(text, NULL, 10);
		if (ban->until < 0)
			ban->until = 0;
	}
	if (ban->until && ban->until <= now)
		return 0;
	if (line_field(line_copy, "profile=", ban->handles, sizeof(ban->handles)))
		printable(ban->handles);
	if (line_field(line_copy, "reason=", ban->reason, sizeof(ban->reason)))
		printable(ban->reason);
	/* (when it was made: the line's first field, "YYYY-MM-DD HH:MM:SS"; 0
	if it is not one) */
	{
		struct tm parts;

		memset(&parts, 0, sizeof(parts));
		if (sscanf(line_copy, "%4d-%2d-%2d %2d:%2d:%2d", &parts.tm_year, &parts.tm_mon, &parts.tm_mday, &parts.tm_hour,
			&parts.tm_min, &parts.tm_sec) == 6)
		{
			parts.tm_year -= 1900;
			parts.tm_mon -= 1;
			parts.tm_isdst = -1;
			ban->time = (long long)mktime(&parts);
			if (ban->time < 0)
				ban->time = 0;
		}
	}
	/* (its ref: the line keyed with this server's key, never the line's
	plain hash, which anyone could check guesses of its address against) */
	pthread_mutex_lock(&bans_mutex);
	if (!ref_key())
	{
		pthread_mutex_unlock(&bans_mutex);
		return 0;
	}
	crypto_blake2b_keyed(hash, sizeof(hash), bans.ref_key, sizeof(bans.ref_key), (const uint8_t *)line_copy, length);
	pthread_mutex_unlock(&bans_mutex);
	control_hex_text(hash, sizeof(hash), ban->ref);
	return 1;
}

int server_bans_share_payload(char *out, int size)
{
	static char entries[SHARE_ENTRIES_MOST];
	static long long read_at;
	static long long modified;
	char path[1024], line[LINE_SIZE], digest_text[65];
	uint8_t digest[32];
	size_t length = 0;
	int sharing, count = 0;
	FILE *file;
	struct stat status;
	long long now = now_seconds();

	if (size <= 0)
		return -1;
	out[0] = 0;
	pthread_mutex_lock(&bans_mutex);
	sharing = bans.sharing;
	pthread_mutex_unlock(&bans_mutex);
	entries[0] = 0;
	if (sharing)
	{
		data_file(BANS_FILE, path, sizeof(path));
		/* (the file read when it changed, or now and then: a timed ban runs
		out) */
		if (stat(path, &status) == 0 && (long long)status.st_mtime == modified && now - read_at < SHARE_READ_SECONDS &&
			bans.sent[0])
		{
			pthread_mutex_lock(&bans_mutex);
			count = bans.answered_set && !strcmp(bans.sent, bans.answered);
			pthread_mutex_unlock(&bans_mutex);
			if (count)
				return 0;
		}
		read_at = now;
		modified = stat(path, &status) == 0 ? (long long)status.st_mtime : 0;
		count = 0;
		file = fopen(path, "r");
		while (file && fgets(line, sizeof(line), file) && count < SERVER_BAN_SHARE_MOST)
		{
			struct server_shared_ban ban;
			int written;

			if (!server_bans_line_entry(line, now, &ban))
				continue;
			written = entry_json(&ban, entries + length + (count ? 2 : 0), SHARE_ENTRIES_MOST - length - 2);
			if (written < 0)
				break;
			if (count)
				memcpy(entries + length, ", ", 2);
			length += (size_t)written + (count ? 2 : 0);
			count++;
		}
		if (file)
			fclose(file);
		entries[length] = 0;
	}
	/* (the digest of what is shared; none, an empty one) */
	if (count)
	{
		crypto_blake2b(digest, sizeof(digest), (const uint8_t *)entries, length);
		control_hex_text(digest, sizeof(digest), digest_text);
	}
	else
		digest_text[0] = 0;
	pthread_mutex_lock(&bans_mutex);
	snprintf(bans.sent, sizeof(bans.sent), "%s", digest_text);
	count = bans.answered_set ? !strcmp(digest_text, bans.answered) : !sharing;
	pthread_mutex_unlock(&bans_mutex);
	/* (the site has it already; or never shared, and nothing to share) */
	if (count)
		return 0;
	if ((size_t)snprintf(out, (size_t)size, "\"bans\": {\"digest\": \"%s\", \"entries\": [%s]}", digest_text, entries) >=
		(size_t)size)
	{
		out[0] = 0;
		return -1;
	}
	return (int)strlen(out);
}

void server_bans_share_answered(const char *digest)
{
	pthread_mutex_lock(&bans_mutex);
	snprintf(bans.answered, sizeof(bans.answered), "%s", digest ? digest : "");
	bans.answered_set = 1;
	pthread_mutex_unlock(&bans_mutex);
}

void server_bans_set_shared(const struct server_shared_ban *entries, int count, unsigned long long version, int cached)
{
	if (count < 0)
		count = 0;
	if (count > SERVER_BAN_SHARED_MOST)
		count = SERVER_BAN_SHARED_MOST;
	pthread_mutex_lock(&bans_mutex);
	if (count)
		memcpy(bans.shared, entries, (size_t)count * sizeof(*entries));
	bans.shared_count = count;
	bans.version = version;
	bans.cached = cached;
	bans.received = now_seconds();
	bans.changed = 1;
	pthread_mutex_unlock(&bans_mutex);
}

unsigned long long server_bans_shared_version(void)
{
	unsigned long long version;

	pthread_mutex_lock(&bans_mutex);
	version = bans.version;
	pthread_mutex_unlock(&bans_mutex);
	return version;
}

int server_bans_take_changed(void)
{
	int changed;

	pthread_mutex_lock(&bans_mutex);
	changed = bans.changed;
	bans.changed = 0;
	pthread_mutex_unlock(&bans_mutex);
	return changed;
}

int server_bans_entry_matches(const struct server_shared_ban *ban, const char *identity)
{
	unsigned char accounts[SERVER_BAN_ACCOUNTS * 2][SERVER_BAN_ID_BYTES];
	unsigned char device[SERVER_BAN_ID_BYTES];
	char text[512];
	int count = 0, has_device = 0, index, other;

	if (!identity || !ban)
		return 0;
	if (line_field(identity, "account=", text, sizeof(text)))
		count = parse_ids(text, accounts, (int)(sizeof(accounts) / sizeof(accounts[0])));
	if (line_field(identity, "device=", text, sizeof(text)) && strlen(text) == 2 * SERVER_BAN_ID_BYTES)
		has_device = parse_id(text, device);
	if (has_device && ban->has_device && !memcmp(device, ban->device, SERVER_BAN_ID_BYTES))
		return 1;
	for (index = 0; index < count; index++)
	{
		for (other = 0; other < ban->account_count; other++)
		{
			if (!memcmp(accounts[index], ban->accounts[other], SERVER_BAN_ID_BYTES))
				return 1;
		}
	}
	return 0;
}

int server_bans_shared_match(const char *identity, long long now, char *list_name, int list_name_size, char *ref,
	int ref_size)
{
	int index, found = 0;

	if (!identity || !identity[0])
		return 0;
	pthread_mutex_lock(&bans_mutex);
	for (index = 0; index < bans.shared_count && !found; index++)
	{
		const struct server_shared_ban *ban = &bans.shared[index];

		if ((ban->until && ban->until <= now) || excepted(ban->ref) || !server_bans_entry_matches(ban, identity))
			continue;
		found = 1;
		if (list_name && list_name_size > 0)
			snprintf(list_name, (size_t)list_name_size, "%s", ban->list_name[0] ? ban->list_name : ban->list);
		if (ref && ref_size > 0)
			snprintf(ref, (size_t)ref_size, "%s", ban->ref);
	}
	pthread_mutex_unlock(&bans_mutex);
	return found;
}

int server_bans_shared_count(void)
{
	int count;

	pthread_mutex_lock(&bans_mutex);
	count = bans.shared_count;
	pthread_mutex_unlock(&bans_mutex);
	return count;
}

int server_bans_shared_entry(int index, struct server_shared_ban *ban, int *is_excepted)
{
	int found = 0;

	pthread_mutex_lock(&bans_mutex);
	if (index >= 0 && index < bans.shared_count)
	{
		*ban = bans.shared[index];
		if (is_excepted)
			*is_excepted = excepted(ban->ref);
		found = 1;
	}
	pthread_mutex_unlock(&bans_mutex);
	return found;
}

int server_bans_except(const char *ref, int except)
{
	char path[1024], temporary[1040];
	FILE *file;
	int index, ok = 1;

	if (!ref || strlen(ref) != 2 * SERVER_BAN_ID_BYTES || strspn(ref, "0123456789abcdef") != 2 * SERVER_BAN_ID_BYTES)
		return 0;
	pthread_mutex_lock(&bans_mutex);
	read_exceptions();
	if (except && !excepted(ref) && bans.exception_count >= MAXIMUM_EXCEPTIONS)
	{
		/* (full: the exceptions for entries no list has now go first) */
		int kept = 0, entry;

		for (index = 0; index < bans.exception_count; index++)
		{
			for (entry = 0; entry < bans.shared_count && strcmp(bans.shared[entry].ref, bans.exceptions[index]); entry++)
				;
			if (entry < bans.shared_count)
				memmove(bans.exceptions[kept++], bans.exceptions[index], sizeof(bans.exceptions[0]));
		}
		bans.exception_count = kept;
		if (bans.exception_count >= MAXIMUM_EXCEPTIONS)
		{
			pthread_mutex_unlock(&bans_mutex);
			return 0;
		}
	}
	if (except && !excepted(ref))
		snprintf(bans.exceptions[bans.exception_count++], SERVER_BAN_REF_SIZE, "%s", ref);
	else if (!except)
	{
		for (index = 0; index < bans.exception_count; index++)
		{
			if (!strcmp(bans.exceptions[index], ref))
			{
				memmove(bans.exceptions[index], bans.exceptions[index + 1],
					(size_t)(bans.exception_count - index - 1) * sizeof(bans.exceptions[0]));
				bans.exception_count--;
				break;
			}
		}
	}
	data_file(EXCEPTIONS_FILE, path, sizeof(path));
	snprintf(temporary, sizeof(temporary), "%s.new", path);
	file = fopen(temporary, "w");
	if (!file)
		ok = 0;
	else
	{
		fprintf(file, "# Shared bans this server lets in anyway (sv_unban on a shared entry): their refs, one a line.\n");
		for (index = 0; index < bans.exception_count; index++)
			fprintf(file, "%s\n", bans.exceptions[index]);
		ok = fclose(file) == 0 && rename(temporary, path) == 0;
	}
	bans.changed = 1;
	pthread_mutex_unlock(&bans_mutex);
	return ok;
}

void server_bans_lists_text(int json, char *out, int size)
{
	char lists[16][SERVER_BAN_LIST_NAME_SIZE];
	int counts[16];
	int list_count = 0, index, other;
	size_t length = 0;
	long long ago;

	if (size <= 0)
		return;
	out[0] = 0;
	pthread_mutex_lock(&bans_mutex);
	for (index = 0; index < bans.shared_count; index++)
	{
		const char *name = bans.shared[index].list_name[0] ? bans.shared[index].list_name : bans.shared[index].list;

		for (other = 0; other < list_count && strcmp(lists[other], name); other++)
			;
		if (other == list_count && list_count < 16)
		{
			snprintf(lists[list_count], sizeof(lists[0]), "%s", name);
			counts[list_count++] = 0;
		}
		if (other < list_count)
			counts[other]++;
	}
	ago = bans.received ? now_seconds() - bans.received : -1;
	if (json)
	{
		length = (size_t)snprintf(out, (size_t)size, "{\"sharing\": %s, \"version\": %llu, \"cached\": %s, "
			"\"received_seconds_ago\": %lld, \"lists\": [", bans.sharing ? "true" : "false", bans.version,
			bans.cached ? "true" : "false", ago);
		for (index = 0; index < list_count && length < (size_t)size; index++)
		{
			char name[SERVER_BAN_LIST_NAME_SIZE * 6 + 3];

			if (control_json_string(lists[index], name, sizeof(name)) < 0)
				snprintf(name, sizeof(name), "\"\"");
			length += (size_t)snprintf(out + length, (size_t)size - length, "%s{\"name\": %s, \"entries\": %d}",
				index ? ", " : "", name, counts[index]);
		}
		if (length < (size_t)size)
			snprintf(out + length, (size_t)size - length, "]}");
	}
	else
	{
		length = (size_t)snprintf(out, (size_t)size, "sharing this server's bans: %s\n", bans.sharing ? "on" : "off");
		if (!list_count && length < (size_t)size)
			length += (size_t)snprintf(out + length, (size_t)size - length, "no shared lists taken%s\n",
				bans.received ? "" : " (link the server, then pick lists on the site's My servers)");
		for (index = 0; index < list_count && length < (size_t)size; index++)
			length += (size_t)snprintf(out + length, (size_t)size - length, "%s: %d bans\n", lists[index], counts[index]);
		if (bans.received && length < (size_t)size)
			snprintf(out + length, (size_t)size - length, "version %llu, %s %lld seconds ago\n", bans.version,
				bans.cached ? "from the cache, read" : "from the site,", ago);
	}
	pthread_mutex_unlock(&bans_mutex);
}

int server_bans_shared_find(const char *prefix, struct server_shared_ban *ban, int *is_excepted)
{
	size_t length = prefix ? strlen(prefix) : 0;
	int index, found = -1, matches = 0;

	if (length < SERVER_BAN_REF_SHOWN || length > 2 * SERVER_BAN_ID_BYTES || strspn(prefix, "0123456789abcdef") != length)
		return 0;
	pthread_mutex_lock(&bans_mutex);
	for (index = 0; index < bans.shared_count; index++)
	{
		if (!strncmp(bans.shared[index].ref, prefix, length))
		{
			found = index;
			matches++;
		}
	}
	if (matches == 1)
	{
		*ban = bans.shared[found];
		if (is_excepted)
			*is_excepted = excepted(ban->ref);
	}
	pthread_mutex_unlock(&bans_mutex);
	return matches == 1;
}
