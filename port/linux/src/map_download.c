/*
MAP_DOWNLOAD.C

Custom Edition maps a game needs, downloaded from the community map
repositories (map_download.h): the public repositories Halo PC's players
have downloaded missing maps from for years, as Chimera uses them
(maps.halonet.net, then its mirrors maps2 and maps1), asked by the map's
name for its zip archive (locator.php?format=zip&map=<name>&type=halom),
over HTTPS; one the locator does not know, by its file
(maps/<name>.zip, the host's name as the host has it: the files' names
keep their case). Never for one of Halo PC's own maps or files (bloodgulch, ui,
...: map_archive_stock_name), which the repositories also have: those are
the player's to copy from their own install.

On a thread of its own: the archive is received into maps_ce (no more than
MAP_ARCHIVE_MAXIMUM_SIZE), the next repository asked when one fails; the
map alone is unpacked from it (map_archive.c), into a file of its own; its
header is checked (a Custom Edition multiplayer map named for the map asked
for), and it must be the host's version of it, when the host says which
(its whole-file hash's first 32 bits from a host of network version 29, its
header checksum from an older one: map_archive_version_matches); and only
then is it renamed <name>.map, never over a file that is there: beside
another version of that name, it is put in <name>_<8 hex digits>.map, its
hash's first digits, as OpenCE's version 29 names a map it downloads beside
another. A hashed name (<base>_<8 hex digits>, the level names older hosts
gave the maps they shared: map_archive.h) is asked for by its base, and its
map must also have the whole-file hash the name ends in; it is put in
<base>.map, or in a file of the hashed name when another version of
<base>.map is there. The game finds every one of them by the host's version
(map_families.c); no file already there is renamed. The archive and
any partial file are deleted, whatever happens. The map is then marked as
downloaded (map_families.c, for good: maps_ce/downloaded_maps.txt), so its
scripts are held to a downloaded map's rules, and the game's own checks read all of it before it is played
(ce_map_checks.c). A map compressed with Chimera's chimera-compress is
expanded right after it is unpacked (map_compressed.c), then checked and
kept as any other.

When none of the community repositories gives the map (they don't have
it, or not the host's version, or it fails its checks), Chupathingy maps
(map_repo.h) is asked next: its signed index finds the map by the host's
name and version, and its mirrors give the file, which must have the
index's size and SHA-256, then the same checks as above. A map from either
whose SHA-256 or map hash is on Chupathingy maps' blacklist is refused, and
no other source is asked. Peer-to-peer downloads from the host aren't here.

The log (debug.txt) has each request's address (the map's name alone, no
player's), the sizes, which checks passed or failed, and the result.
*/

#include "platform.h"
#include "port_config.h"
#include "map_archive.h"
#include "map_compressed.h"
#include "map_download.h"
#include "map_file_hash.h"
#include "map_repo.h"
#include "playlist_manifest.h"
#include "halo_map_families.h"
#ifdef HALO_GAME_BROWSER
#include "browser_http.h"
#include "build_identity.h"
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------- constants */

enum
{
	/* what is received or copied at a time */
	MAP_DOWNLOAD_CHUNK = 64 * 1024,
	MAP_DOWNLOAD_PATH_SIZE = 256,
	/* the repositories debug.map_repositories may name, at most */
	MAP_DOWNLOAD_TEST_REPOSITORIES = 4,
	MAP_DOWNLOAD_URL_SIZE = 128,
};

/* the repositories, in the order they are asked (Chimera's: its default,
then the mirrors its map downloader names) */
static const char *const map_download_repositories[] =
{
	"https://maps.halonet.net",
	"https://maps2.halonet.net",
	"https://maps1.halonet.net",
};

/* ---------- globals */

static struct
{
	pthread_mutex_t lock;
	struct map_download_progress progress;
	unsigned int version;
	/* set by the game's thread, read by the download's */
	volatile int cancel;
	int running;
	/* the map installed is marked as downloaded (map_download_get), and
	the file it was put in (its name's, or beside another version the name
	and its hash's digits; a hashed name's: its base's, or its own) */
	int remembered;
	char installed[48];
} map_download = { PTHREAD_MUTEX_INITIALIZER };

/* ---------- private code */

static void set_phase(int phase, unsigned int received, unsigned int total)
{
	pthread_mutex_lock(&map_download.lock);
	map_download.progress.phase = phase;
	map_download.progress.received = received;
	map_download.progress.total = total;
	pthread_mutex_unlock(&map_download.lock);
}

static void set_source(int source)
{
	pthread_mutex_lock(&map_download.lock);
	map_download.progress.source = source;
	pthread_mutex_unlock(&map_download.lock);
}

static void finish(int state, const char *message)
{
	pthread_mutex_lock(&map_download.lock);
	map_download.progress.state = state;
	snprintf(map_download.progress.message, sizeof(map_download.progress.message), "%s", message ? message : "");
	map_download.running = 0;
	pthread_mutex_unlock(&map_download.lock);
}

static void paths(const char *name, char *archive, char *partial)
{
	snprintf(archive, MAP_DOWNLOAD_PATH_SIZE, "d:\\maps_ce\\%s.zip.download", name);
	snprintf(partial, MAP_DOWNLOAD_PATH_SIZE, "d:\\maps_ce\\%s.map.download", name);
}

static void final_path(const char *file, char *path)
{
	snprintf(path, MAP_DOWNLOAD_PATH_SIZE, "d:\\maps_ce\\%s.map", file);
}

#ifdef HALO_GAME_BROWSER

/* map_archive_extract's reads (the archive's file) and writes (the map's) */
struct map_files
{
	HANDLE archive;
	HANDLE map;
	unsigned int written;
	unsigned int total;
};

static int archive_read(void *context, unsigned int offset, void *buffer, unsigned int size)
{
	struct map_files *files = (struct map_files *)context;
	DWORD read = 0;

	if (SetFilePointer(files->archive, (LONG)offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		return 0;
	return ReadFile(files->archive, buffer, size, &read, NULL) && read == size;
}

static int map_write(void *context, const void *buffer, unsigned int size)
{
	struct map_files *files = (struct map_files *)context;
	DWORD written = 0;

	if (!WriteFile(files->map, buffer, size, &written, NULL) || written != size)
		return 0;
	files->written += size;
	set_phase(MAP_DOWNLOAD_UNPACKING, files->written, files->total);
	return 1;
}

static int stop_requested(void *context)
{
	(void)context;
	return map_download.cancel;
}

/* the archive from one repository into the file at path: 1, or 0 with why
in error and the HTTP status in *status (cancelled: map_download.cancel) */
static int receive(const char *url, const char *path, int *status, char *error, int error_size)
{
	char *buffer;
	HANDLE file;
	unsigned int received = 0;
	int length = -1;
	int download;
	int ok = 1;

	set_phase(MAP_DOWNLOAD_CONNECTING, 0, 0);
	*status = 0;
	download = posix_browser_download_open(url, build_identity_user_agent(), status, &length, error, error_size);
	if (!download)
		return 0;
	if (length > MAP_ARCHIVE_MAXIMUM_SIZE)
	{
		posix_browser_download_close(download);
		snprintf(error, (size_t)error_size, "the archive is %d MB (at most %d MB)", length >> 20,
			MAP_ARCHIVE_MAXIMUM_SIZE >> 20);
		return 0;
	}
	buffer = (char *)malloc(MAP_DOWNLOAD_CHUNK);
	file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (!buffer || file == INVALID_HANDLE_VALUE)
	{
		posix_browser_download_close(download);
		free(buffer);
		if (file != INVALID_HANDLE_VALUE)
			CloseHandle(file);
		snprintf(error, (size_t)error_size, "could not write into maps_ce");
		return 0;
	}
	set_phase(MAP_DOWNLOAD_RECEIVING, 0, length > 0 ? (unsigned int)length : 0);
	for (;;)
	{
		int count;
		DWORD written = 0;

		if (map_download.cancel)
		{
			snprintf(error, (size_t)error_size, "cancelled");
			ok = 0;
			break;
		}
		count = posix_browser_download_read(download, buffer, MAP_DOWNLOAD_CHUNK);
		if (count == 0)
			break;
		if (count < 0)
		{
			snprintf(error, (size_t)error_size, "the connection failed after %u bytes", received);
			ok = 0;
			break;
		}
		if ((unsigned int)count > (unsigned int)MAP_ARCHIVE_MAXIMUM_SIZE - received)
		{
			snprintf(error, (size_t)error_size, "the archive is over %d MB", MAP_ARCHIVE_MAXIMUM_SIZE >> 20);
			ok = 0;
			break;
		}
		if (!WriteFile(file, buffer, (DWORD)count, &written, NULL) || written != (DWORD)count)
		{
			snprintf(error, (size_t)error_size, "could not write into maps_ce (is the disk full?)");
			ok = 0;
			break;
		}
		received += (unsigned int)count;
		set_phase(MAP_DOWNLOAD_RECEIVING, received, length > 0 ? (unsigned int)length : 0);
	}
	posix_browser_download_close(download);
	free(buffer);
	CloseHandle(file);
	if (ok && length >= 0 && received != (unsigned int)length)
	{
		snprintf(error, (size_t)error_size, "the archive was cut short (%u of %d bytes)", received, length);
		ok = 0;
	}
	if (ok)
		platform_log("map download: received %u bytes", received);
	return ok;
}

#ifdef HALO_COMPRESSED_MAPS
/* the map unpacked into partial_path (its file open in *map), compressed
with Chimera's chimera-compress, expanded in its place (map_compressed.c):
the file reopened in *map, its header in header and its size in *map_size.
1, or 0 with why in error */
static int expand_download(const char *file, const char *partial_path, HANDLE *map, unsigned char *header,
	unsigned int *map_size, char *error, int error_size)
{
	char expanded[MAP_DOWNLOAD_PATH_SIZE + 16];
	DWORD high = 0, read = 0;
	int ok;

	platform_log("map download: %s.map is compressed (Chimera's format): expanding it", file);
	set_phase(MAP_DOWNLOAD_UNPACKING, 0, 0);
	CloseHandle(*map);
	*map = INVALID_HANDLE_VALUE;
	snprintf(expanded, sizeof(expanded), "%s.expanded", partial_path);
	ok = map_compressed_file_expand(partial_path, expanded, error, error_size);
	DeleteFileA(partial_path);
	if (ok && !MoveFileA(expanded, partial_path))
	{
		snprintf(error, (size_t)error_size, "could not write into maps_ce");
		ok = 0;
	}
	if (!ok)
	{
		DeleteFileA(expanded);
		return 0;
	}
	*map = CreateFileA(partial_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
		NULL);
	ok = *map != INVALID_HANDLE_VALUE && ReadFile(*map, header, MAP_ARCHIVE_HEADER_SIZE, &read, NULL) &&
		read == MAP_ARCHIVE_HEADER_SIZE;
	if (ok)
	{
		*map_size = (unsigned int)GetFileSize(*map, &high);
		ok = !high && *map_size != INVALID_FILE_SIZE;
	}
	if (!ok)
		snprintf(error, (size_t)error_size, "the map could not be read back");
	else
		platform_log("map download: %s.map expanded, %u bytes", file, *map_size);
	return ok;
}
#endif

/* the map in the file at partial_path checked: its header (a Custom Edition
multiplayer map named file), its version, the host's when it said which
(map_download.version), and for a hashed name (hashed) its whole-file hash,
which must begin with prefix: 1, or 0 with why in error */
static int check_map(const char *file, int hashed, unsigned int prefix, const char *partial_path, char *error,
	int error_size)
{
	unsigned char header[MAP_ARCHIVE_HEADER_SIZE];
	unsigned int map_size;
	DWORD high = 0, read = 0;
	HANDLE map;
	int ok;

	set_phase(MAP_DOWNLOAD_CHECKING, 0, 0);
	map = CreateFileA(partial_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	ok = map != INVALID_HANDLE_VALUE && ReadFile(map, header, sizeof(header), &read, NULL) && read == sizeof(header);
	if (ok)
	{
		map_size = (unsigned int)GetFileSize(map, &high);
		ok = !high && map_size != INVALID_FILE_SIZE;
	}
	if (map != INVALID_HANDLE_VALUE)
		CloseHandle(map);
	if (!ok)
	{
		snprintf(error, (size_t)error_size, "the map could not be read back");
		return 0;
	}
	if (!map_archive_check_header(header, map_size, file, 0, error, error_size))
		return 0;
	platform_log("map download: %s.map is a Custom Edition multiplayer map named %s, checksum %08x", file, file,
		map_archive_header_checksum(header));
	/* (the host's version: its hash's from a host of network version 29,
	its checksum from an older one) */
	if (map_download.version)
	{
		unsigned int checksum = map_archive_header_checksum(header);
		unsigned int hash_version = 0;

		if (!map_archive_version_matches(map_download.version, checksum, 0) &&
			!map_file_hash_version(partial_path, &hash_version))
		{
			snprintf(error, (size_t)error_size, "the map could not be read back");
			return 0;
		}
		if (!map_archive_version_matches(map_download.version, checksum, hash_version))
		{
			snprintf(error, (size_t)error_size,
				"its version is %08x (checksum %08x), the host's %08x: another version of the map", hash_version,
				checksum, map_download.version);
			return 0;
		}
		platform_log("map download: %s.map is the host's version, %08x", file, map_download.version);
	}
	/* (a hashed name's map is the host's file to the byte) */
	if (hashed)
	{
		unsigned int found = 0;

		if (!map_file_hash_prefix(partial_path, &found))
		{
			snprintf(error, (size_t)error_size, "the map could not be read back");
			return 0;
		}
		if (found != prefix)
		{
			snprintf(error, (size_t)error_size, "its hash begins %08x, the host's %08x: another version of the map",
				found, prefix);
			return 0;
		}
		platform_log("map download: %s.map's hash begins %08x, the host's", file, found);
	}
	return 1;
}

/* the map file (its name in the archive, and inside it) unpacked from the
archive at archive_path into partial_path, and checked (check_map): 1, or 0
with why in error */
static int unpack_and_check(const char *file, int hashed, unsigned int prefix, const char *archive_path,
	const char *partial_path, char *error, int error_size)
{
	struct map_archive_io io;
	struct map_files files;
	unsigned int map_size = 0;
	DWORD high = 0;
	int ok;

	memset(&files, 0, sizeof(files));
	files.archive = CreateFileA(archive_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (files.archive == INVALID_HANDLE_VALUE)
	{
		snprintf(error, (size_t)error_size, "the archive could not be read back");
		return 0;
	}
	files.map = CreateFileA(partial_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
		NULL);
	if (files.map == INVALID_HANDLE_VALUE)
	{
		CloseHandle(files.archive);
		snprintf(error, (size_t)error_size, "could not write into maps_ce");
		return 0;
	}
	memset(&io, 0, sizeof(io));
	io.context = &files;
	io.archive_size = GetFileSize(files.archive, &high);
	io.read = archive_read;
	io.write = map_write;
	io.stop = stop_requested;
	if (high || io.archive_size == INVALID_FILE_SIZE)
		io.archive_size = 0xffffffffu;
	set_phase(MAP_DOWNLOAD_UNPACKING, 0, 0);
	ok = map_archive_extract(&io, file, &map_size, error, error_size);
	CloseHandle(files.archive);
	if (ok)
		platform_log("map download: unpacked %s.map, %u bytes (size and CRC-32 checked)", file, map_size);
#ifdef HALO_COMPRESSED_MAPS
	if (ok)
	{
		unsigned char header[MAP_ARCHIVE_HEADER_SIZE];
		DWORD read = 0;

		ok = SetFilePointer(files.map, 0, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
			ReadFile(files.map, header, sizeof(header), &read, NULL) && read == sizeof(header);
		if (!ok)
			snprintf(error, (size_t)error_size, "the map could not be read back");
		else if (map_compressed_version(header))
			ok = expand_download(file, partial_path, &files.map, header, &map_size, error, error_size);
	}
#endif
	if (files.map != INVALID_HANDLE_VALUE)
		CloseHandle(files.map);
	return ok && check_map(file, hashed, prefix, partial_path, error, error_size);
}

/* map_archive_map_hash's reads of a file, in order, each also added to its
SHA-256 */
struct hashed_file
{
	HANDLE file;
	unsigned int next;
	struct playlist_sha256 sha256;
};

static int hashed_read(void *context, unsigned int offset, void *buffer, unsigned int size)
{
	struct hashed_file *hashed = (struct hashed_file *)context;
	DWORD read = 0;

	if (offset != hashed->next || !ReadFile(hashed->file, buffer, size, &read, NULL) || read != size)
		return 0;
	hashed->next += size;
	playlist_sha256_add(&hashed->sha256, buffer, size);
	return 1;
}

/* the file at path's SHA-256 and map hash, in one read: 1, else 0 */
static int file_hashes(const char *path, unsigned char *sha256, unsigned char *map_hash)
{
	struct hashed_file hashed;
	DWORD high = 0;
	unsigned int size;
	int ok;

	memset(&hashed, 0, sizeof(hashed));
	hashed.file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hashed.file == INVALID_HANDLE_VALUE)
		return 0;
	size = (unsigned int)GetFileSize(hashed.file, &high);
	playlist_sha256_begin(&hashed.sha256);
	ok = !high && size != INVALID_FILE_SIZE && map_archive_map_hash(hashed_read, NULL, &hashed, size, map_hash) &&
		hashed.next == size;
	CloseHandle(hashed.file);
	playlist_sha256_end(&hashed.sha256, sha256);
	return ok;
}

/* Chupathingy maps' downloads' progress and cancel (map_repo_game.c) */
static void repo_progress(unsigned int received, unsigned int total)
{
	set_phase(MAP_DOWNLOAD_RECEIVING, received, total);
}

static int repo_stop(void *context)
{
	(void)context;
	return map_download.cancel;
}

/* the community map repositories, in the order they are asked
(debug.map_repositories's, a test's, or HaloNet's): their count */
static int repositories(char urls[][MAP_DOWNLOAD_URL_SIZE], int maximum)
{
	const char *setting = config_string("debug.map_repositories");
	int count = 0;

	while (setting && *setting && count < maximum)
	{
		size_t length;

		setting += strspn(setting, " ");
		length = strcspn(setting, " ");
		if (length && length < MAP_DOWNLOAD_URL_SIZE)
			snprintf(urls[count++], MAP_DOWNLOAD_URL_SIZE, "%.*s", (int)length, setting);
		setting += length;
	}
	if (count)
		return count;
	for (count = 0; count < (int)(sizeof(map_download_repositories) / sizeof(map_download_repositories[0])) &&
		count < maximum; count++)
	{
		snprintf(urls[count], MAP_DOWNLOAD_URL_SIZE, "%s", map_download_repositories[count]);
	}
	return count;
}

/* the map from the community map repositories into partial_path, unpacked
and checked: 1, or 0 with why in error and *missing 1 when every one said
it has no such map */
static int from_community(const char *name, const char *asked, int hashed, unsigned int prefix,
	const char *archive_path, const char *partial_path, int *missing, char *error, int error_size)
{
	char urls[MAP_DOWNLOAD_TEST_REPOSITORIES][MAP_DOWNLOAD_URL_SIZE];
	int count = repositories(urls, MAP_DOWNLOAD_TEST_REPOSITORIES);
	int received = 0, status = 0, index;

	*missing = 1;
	for (index = 0; index < count && !received && !map_download.cancel; index++)
	{
		char url[384];

		snprintf(url, sizeof(url), "%s/halonet/locator.php?format=zip&map=%s&type=halom", urls[index], asked);
		platform_log("map download: GET %s", url);
		received = receive(url, archive_path, &status, error, error_size);
		if (!received)
		{
			platform_log("map download: %s", error);
			DeleteFileA(archive_path);
			*missing = *missing && status == 404;
		}
	}
	/* (a map the locator doesn't know may still be among the repositories'
	files, as <repository>/maps/<name>.zip, a name they match by its case:
	asked for by the host's name as the host has it, its base for a hashed
	name. HaloNet's BattleCraft.zip is one: the locator has no battlecraft) */
	if (!received && *missing && !map_download.cancel)
	{
		char direct[MAP_DOWNLOAD_URL_SIZE];

		snprintf(direct, sizeof(direct), "%.*s", (int)strlen(asked), name);
		for (index = 0; index < count && !received && !map_download.cancel; index++)
		{
			char url[384];

			snprintf(url, sizeof(url), "%s/maps/%s.zip", urls[index], direct);
			platform_log("map download: GET %s", url);
			received = receive(url, archive_path, &status, error, error_size);
			if (!received)
			{
				platform_log("map download: %s", error);
				DeleteFileA(archive_path);
				*missing = *missing && status == 404;
			}
		}
	}
	if (!received)
	{
		if (*missing && !map_download.cancel)
			snprintf(error, (size_t)error_size, "the community map repositories don't have %s.map", asked);
		return 0;
	}
	*missing = 0;
	if (!unpack_and_check(hashed ? asked : name, hashed, prefix, archive_path, partial_path, error, error_size))
	{
		platform_log("map download: %s.map from the community map repositories refused: %s", name, error);
		received = 0;
	}
	DeleteFileA(archive_path);
	return received;
}

/* the map from Chupathingy maps into partial_path, by the host's version
(a hashed name's by its hash's), and checked: 1, or 0 with why in error and
*missing 1 when its index doesn't have it */
static int from_chupathingy(const char *name, const char *asked, int hashed, unsigned int prefix,
	const char *partial_path, int *missing, char *error, int error_size)
{
	static const struct map_repo_game_hooks hooks = { repo_progress, repo_stop };
	struct map_repo_transport transport;
	struct map_index_entry entry;
	char why[160];
	unsigned int version = hashed ? (prefix ? prefix : 1) : map_download.version;

	/* (no index that checks: not asked, the community repositories' answer
	kept) */
	*missing = 0;
	if (!map_repo_enabled() || !map_repo_game_refresh_now())
		return 0;
	set_source(MAP_DOWNLOAD_SOURCE_CHUPATHINGY);
	set_phase(MAP_DOWNLOAD_CONNECTING, 0, 0);
	map_repo_game_transport(&transport, &hooks);
	platform_log("map download: asking %s for %s.map (version %08x)", MAP_REPO_TITLE, asked, version);
	if (!map_repo_download(&transport, asked, version, partial_path, (long long)time(NULL), &entry, missing, why,
		(int)sizeof(why)))
	{
		platform_log("map download: %s: %s", MAP_REPO_TITLE, why);
		if (!*missing)
			snprintf(error, (size_t)error_size, "%s: %s", MAP_REPO_TITLE, why);
		DeleteFileA(partial_path);
		return 0;
	}
	platform_log("map download: %s.map from %s, %u bytes, its SHA-256 the index's", asked, MAP_REPO_TITLE, entry.size);
	if (!check_map(hashed ? asked : name, hashed, prefix, partial_path, error, error_size))
	{
		platform_log("map download: %s.map from %s refused: %s", name, MAP_REPO_TITLE, error);
		DeleteFileA(partial_path);
		return 0;
	}
	return 1;
}

static void *download_thread(void *argument)
{
	char name[sizeof(map_download.progress.name)];
	char asked[sizeof(name)];
	char archive_path[MAP_DOWNLOAD_PATH_SIZE], partial_path[MAP_DOWNLOAD_PATH_SIZE], path[MAP_DOWNLOAD_PATH_SIZE];
	char error[160] = "";
	const char *installed = NULL;
	char beside[sizeof(map_download.installed)];
	unsigned int prefix = 0;
	size_t index;
	int received = 0;
	int hashed;
	/* (whether the sources said they have no such map) */
	int missing = 1, missing_ours = 1;
	/* (refused by the blacklist: no other source is asked) */
	int blocked = 0;

	(void)argument;
	pthread_mutex_lock(&map_download.lock);
	snprintf(name, sizeof(name), "%s", map_download.progress.name);
	pthread_mutex_unlock(&map_download.lock);
	/* (asked for in lowercase, as the repositories name their maps; a hashed
	name by its base, the repositories' file: map_archive.h) */
	hashed = map_archive_hashed_name(name, asked, sizeof(asked), &prefix);
	if (!hashed)
		snprintf(asked, sizeof(asked), "%s", name);
	for (index = 0; asked[index]; index++)
		asked[index] = (char)(asked[index] >= 'A' && asked[index] <= 'Z' ? asked[index] + ('a' - 'A') : asked[index]);
	if (hashed)
		platform_log("map download: %s is a hashed name: %s.map, its hash beginning %08x", name, asked, prefix);
	paths(name, archive_path, partial_path);
	CreateDirectoryA("d:\\maps_ce", NULL);
	DeleteFileA(archive_path);
	DeleteFileA(partial_path);
	/* (Chupathingy maps' blacklist, fresh when it is due: every map is held
	to it, from wherever it comes) */
	map_repo_game_refresh_now();

	/* the community map repositories first */
	set_source(MAP_DOWNLOAD_SOURCE_COMMUNITY);
	received = from_community(name, asked, hashed, prefix, archive_path, partial_path, &missing, error,
		(int)sizeof(error));
	if (received)
	{
		unsigned char sha256[MAP_INDEX_HASH_SIZE], map_hash[MAP_INDEX_HASH_SIZE];
		char reason[MAP_INDEX_REASON_SIZE];

		set_phase(MAP_DOWNLOAD_CHECKING, 0, 0);
		if (map_repo_enabled() && file_hashes(partial_path, sha256, map_hash) &&
			map_repo_blocked(sha256, map_hash, (long long)time(NULL), reason, (int)sizeof(reason)))
		{
			platform_log("map download: %s.map from the community map repositories is on %s' blacklist: %s", name,
				MAP_REPO_TITLE, reason);
			snprintf(error, sizeof(error), "%s.map is blocked by %s: %s", asked, MAP_REPO_TITLE, reason);
			DeleteFileA(partial_path);
			received = 0;
			blocked = 1;
		}
	}
	/* then Chupathingy maps, for a map they don't have (or not of the host's
	version, or that failed) */
	if (!received && !blocked && !map_download.cancel && map_repo_enabled())
	{
		char ours[160] = "";

		received = from_chupathingy(name, asked, hashed, prefix, partial_path, &missing_ours, ours, (int)sizeof(ours));
		if (!received && !missing_ours && ours[0])
			snprintf(error, sizeof(error), "%s", ours);
		else if (!received && missing && missing_ours)
			snprintf(error, sizeof(error), "neither the community map repositories nor %s have %s.map", MAP_REPO_TITLE,
				asked);
	}
	DeleteFileA(archive_path);
	/* (in place, never over a file that is there: MoveFileA refuses one. A
	map is put in its own name's file, as the repositories name it; if
	another version of it is there, in a file of its name and its hash's
	first digits (a hashed name's: that name), so both are kept: the game
	finds the host's version by its hash, map_families.c) */
	if (received)
	{
		installed = hashed ? asked : name;
		final_path(installed, path);
		if (!MoveFileA(partial_path, path))
		{
			unsigned int found = 0;

			installed = NULL;
			if (hashed)
				snprintf(beside, sizeof(beside), "%s", name);
			else if (map_download.version && map_file_hash_prefix(partial_path, &found))
				snprintf(beside, sizeof(beside), "%s_%08x", name, found);
			else
				beside[0] = 0;
			if (beside[0])
			{
				platform_log("map download: maps_ce/%s.map is there already, another version: putting this one in "
					"%s.map", hashed ? asked : name, beside);
				installed = beside;
				final_path(installed, path);
				if (!MoveFileA(partial_path, path))
					installed = NULL;
			}
		}
		if (!installed)
		{
			snprintf(error, sizeof(error), "maps_ce/%s.map could not be put in place (is it there already?)", name);
			platform_log("map download: %s", error);
			received = 0;
		}
	}
	if (!received)
	{
		DeleteFileA(partial_path);
		if (map_download.cancel)
		{
			platform_log("map download: %s.map cancelled", name);
			finish(MAP_DOWNLOAD_CANCELLED, "cancelled");
		}
		else
		{
			platform_log("map download: %s.map failed", name);
			finish(MAP_DOWNLOAD_FAILED, error);
		}
		return NULL;
	}
	pthread_mutex_lock(&map_download.lock);
	snprintf(map_download.installed, sizeof(map_download.installed), "%s", installed);
	pthread_mutex_unlock(&map_download.lock);
	if (installed != name)
		platform_log("map download: %s installed in maps_ce as %s.map", name, installed);
	else
		platform_log("map download: %s.map installed in maps_ce", name);
	finish(MAP_DOWNLOAD_DONE, NULL);
	return NULL;
}

#endif

/* ---------- public code */

int map_download_setting(void)
{
	return map_archive_setting(config_string("network.map_downloads"));
}

void map_download_set_setting(int setting)
{
	config_write("network.map_downloads", map_archive_setting_word(setting));
}

int map_download_possible(const char *name)
{
#if defined(HALO_GAME_BROWSER) && !defined(HALO_SERVER)
	/* (never one of Halo PC's own maps: the player's own copy is played) */
	return map_download_setting() != MAP_DOWNLOADS_NEVER && map_archive_name_valid(name) &&
		!map_archive_stock_name(name);
#else
	(void)name;
	return 0;
#endif
}

int map_download_start(const char *name, unsigned int version)
{
#if defined(HALO_GAME_BROWSER) && !defined(HALO_SERVER)
	pthread_t thread;

	if (!map_archive_name_valid(name))
		return 0;
	/* Halo PC's own maps are the player's to copy from their install, never
	asked of a repository (map_archive.h) */
	if (map_archive_stock_name(name))
	{
		platform_log("map download: %s is one of the game's own maps; not downloaded", name);
		return 0;
	}
	pthread_mutex_lock(&map_download.lock);
	if (map_download.running)
	{
		pthread_mutex_unlock(&map_download.lock);
		return 0;
	}
	memset(&map_download.progress, 0, sizeof(map_download.progress));
	map_download.progress.state = MAP_DOWNLOAD_RUNNING;
	snprintf(map_download.progress.name, sizeof(map_download.progress.name), "%s", name);
	map_download.version = version;
	map_download.cancel = 0;
	map_download.running = 1;
	map_download.remembered = 0;
	map_download.installed[0] = 0;
	pthread_mutex_unlock(&map_download.lock);
	platform_log("map download: %s.map from the community map repositories (host's version %08x)", name, version);
	if (pthread_create(&thread, NULL, download_thread, NULL) != 0)
	{
		finish(MAP_DOWNLOAD_FAILED, "could not start the download");
		return 0;
	}
	pthread_detach(thread);
	return 1;
#else
	(void)name;
	(void)version;
	return 0;
#endif
}

void map_download_cancel(void)
{
	map_download.cancel = 1;
}

void map_download_get(struct map_download_progress *progress)
{
	pthread_mutex_lock(&map_download.lock);
	*progress = map_download.progress;
	pthread_mutex_unlock(&map_download.lock);
	/* (a map just installed marked as downloaded, on the game's thread, which
	reads the marks) */
	if (progress->state == MAP_DOWNLOAD_DONE && !map_download.remembered)
	{
		char map_name[64];

		map_download.remembered = 1;
		snprintf(map_name, sizeof(map_name), "%s@ce", progress->name);
		map_downloaded_remember(map_name);
		/* (and the file it is in, when that is named otherwise: a hashed
		name's base, which is played by that name too) */
		pthread_mutex_lock(&map_download.lock);
		snprintf(map_name, sizeof(map_name), "%s@ce", map_download.installed);
		pthread_mutex_unlock(&map_download.lock);
		if (strcmp(map_name, "@ce"))
			map_downloaded_remember(map_name);
	}
}

void map_download_peek(struct map_download_progress *progress)
{
	pthread_mutex_lock(&map_download.lock);
	*progress = map_download.progress;
	pthread_mutex_unlock(&map_download.lock);
}

void map_download_reset(void)
{
	pthread_mutex_lock(&map_download.lock);
	if (!map_download.running)
		memset(&map_download.progress, 0, sizeof(map_download.progress));
	pthread_mutex_unlock(&map_download.lock);
}
