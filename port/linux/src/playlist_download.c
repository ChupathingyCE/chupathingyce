/*
PLAYLIST_DOWNLOAD.C

A playlist's map set checked and installed (playlist_download.h;
docs/playlist-maps.md).

The check: the manifest and its signature from the game list's site
(network.browser_url's /v1/playlists/<id>/maps.json and maps.json.sig),
checked (playlist_manifest.c: Delta's keys, the playlist's own); then each
file looked for in its folder: there with the listed size, it is installed;
there with another size, another version, never replaced; not there,
missing. The player's own files (the Xbox's maps, Halo PC's) are never
downloaded: they are named by their pack.

The download, on the player's yes: each missing file in turn, from its
source alone. Chupathingy's mirror (https://dl.chupathingy.org/<path>):
the file itself, received into <file>.download beside its place, no more
than its listed size, hashed as it comes. The community map repositories:
through map_download.c (its zip, unpacked and checked as a Custom Edition
map), then hashed. A file whose size or SHA-256 is not the manifest's is
deleted; one that is, is moved into place, never over a file that is there.
Only the manifest's folders (maps_ce, maps_<pack>) and file names are ever
written, and nothing into maps. The Custom Edition maps installed are then
marked as downloaded (on the game's thread, playlist_get), as the single-map
downloader marks its own.

The log (debug.txt) has the requests (the playlist's id and the files' paths,
no player's), each file's checks and the result.
*/

#include "platform.h"
#include "port_config.h"
#include "map_download.h"
#include "playlist_download.h"
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

/* ---------- constants */

#define PLAYLIST_MIRROR "https://dl.chupathingy.org/"

enum
{
	CHUNK = 64 * 1024,
	PATH_SIZE = 320,
	SIGNATURE_RESPONSE_SIZE = 512,
	/* the repositories' download followed this often */
	POLL_INTERVAL = 100,
};

/* each file's state, as the check found it */
enum
{
	FILE_INSTALLED,
	FILE_MISSING,
	FILE_OTHER_VERSION,
	FILE_OWN_MISSING,
};

/* ---------- globals */

static struct
{
	pthread_mutex_t lock;
	struct playlist_status status;
	volatile int cancel;
	int running;
	/* the manifest checked, and each of its files' state */
	struct playlist_manifest manifest;
	unsigned char file_state[PLAYLIST_MANIFEST_FILES];
	/* Custom Edition maps installed, to be marked as downloaded on the game's
	thread (playlist_get) */
	int remember_count;
	char remember[PLAYLIST_MANIFEST_FILES][PLAYLIST_MANIFEST_MAP_SIZE];
} playlist = { PTHREAD_MUTEX_INITIALIZER };

/* ---------- private code */

#if defined(HALO_GAME_BROWSER) && !defined(HALO_SERVER)

static void finish(int state, const char *message)
{
	pthread_mutex_lock(&playlist.lock);
	playlist.status.state = state;
	snprintf(playlist.status.message, sizeof(playlist.status.message), "%s", message ? message : "");
	playlist.running = 0;
	pthread_mutex_unlock(&playlist.lock);
}

/* a file's place: the Xbox's maps folder (the game's, by its language), or
a folder beside it */
const char *cache_files_map_directory(void);

static void file_path(const struct playlist_file *file, const char *suffix, char *path, size_t size)
{
	if (!strcmp(file->folder, "maps"))
		snprintf(path, size, "%s%s%s", cache_files_map_directory(), file->file, suffix);
	else
		snprintf(path, size, "d:\\%s\\%s%s", file->folder, file->file, suffix);
}

/* the file at path's size; 0 if there is none */
static int file_size(const char *path, unsigned int *size)
{
	HANDLE handle = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD high = 0, low;

	if (handle == INVALID_HANDLE_VALUE)
		return 0;
	low = GetFileSize(handle, &high);
	CloseHandle(handle);
	if (low == INVALID_FILE_SIZE || high)
		return 0;
	*size = (unsigned int)low;
	return 1;
}

/* the file at path's SHA-256: 1, or 0 if it could not be read */
static int file_sha256(const char *path, unsigned char *digest)
{
	struct playlist_sha256 context;
	HANDLE handle = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	char *buffer;
	int ok = 1;

	if (handle == INVALID_HANDLE_VALUE)
		return 0;
	buffer = (char *)malloc(CHUNK);
	if (!buffer)
	{
		CloseHandle(handle);
		return 0;
	}
	playlist_sha256_begin(&context);
	for (;;)
	{
		DWORD read = 0;

		if (playlist.cancel || !ReadFile(handle, buffer, CHUNK, &read, NULL))
		{
			ok = 0;
			break;
		}
		if (!read)
			break;
		playlist_sha256_add(&context, buffer, read);
	}
	free(buffer);
	CloseHandle(handle);
	playlist_sha256_end(&context, digest);
	return ok;
}

static const char *pack_name(const struct playlist_manifest *manifest, const struct playlist_file *file)
{
	return file->pack >= 0 ? manifest->packs[(int)file->pack].name : "";
}

/* the manifest and its signature from the site, checked */
static int fetch_manifest(const char *id, char *error, int error_size)
{
	static char document[PLAYLIST_MANIFEST_DOCUMENT_SIZE + 2];
	char signature[SIGNATURE_RESPONSE_SIZE], url[512], why[160];
	const char *base = config_string("network.browser_url");
	size_t length = strlen(base), size;
	int status;

	while (length && base[length - 1] == '/')
		length--;
	snprintf(url, sizeof(url), "%.*s/v1/playlists/%s/maps.json", (int)length, base, id);
	platform_log("playlist maps: GET %s", url);
	status = posix_browser_request_as(url, NULL, NULL, build_identity_user_agent(), document, (int)sizeof(document), why,
		(int)sizeof(why));
	if (status != 200)
	{
		if (status == 404)
			snprintf(error, (size_t)error_size, "the playlist has no verified map set");
		else if (status)
			snprintf(error, (size_t)error_size, "the site answered %d", status);
		else
			snprintf(error, (size_t)error_size, "the site did not answer (%s)", why);
		return 0;
	}
	size = strlen(document);
	if (size > PLAYLIST_MANIFEST_DOCUMENT_SIZE)
	{
		snprintf(error, (size_t)error_size, "the map set is too large");
		return 0;
	}
	snprintf(url, sizeof(url), "%.*s/v1/playlists/%s/maps.json.sig", (int)length, base, id);
	status = posix_browser_request_as(url, NULL, NULL, build_identity_user_agent(), signature, (int)sizeof(signature), why,
		(int)sizeof(why));
	if (status != 200)
	{
		snprintf(error, (size_t)error_size, "the map set's signature could not be fetched");
		return 0;
	}
	if (!playlist_manifest_check(document, size, signature, strlen(signature), id, &playlist.manifest, why,
		(int)sizeof(why)))
	{
		snprintf(error, (size_t)error_size, "the map set was refused: %s", why);
		return 0;
	}
	platform_log("playlist maps: %s's map set checked (serial %u, %d files)", id, playlist.manifest.serial,
		playlist.manifest.file_count);
	return 1;
}

/* a missing file's line for the player: its title or name, and its pack */
static void missing_line(const struct playlist_file *file, char *line, size_t size)
{
	const char *pack = pack_name(&playlist.manifest, file);
	char name[PLAYLIST_MANIFEST_FILE_SIZE];

	snprintf(name, sizeof(name), "%s", file->file);
	name[strcspn(name, ".")] = 0;
	if (file->title[0])
		snprintf(line, size, "%s (%s%s%s)", file->title, name, pack[0] ? ", " : "", pack);
	else
		snprintf(line, size, "%s%s%s%s", file->file, pack[0] ? " (" : "", pack, pack[0] ? ")" : "");
}

static void *check_thread(void *argument)
{
	char id[PLAYLIST_MANIFEST_ID_SIZE], error[160] = "";
	struct playlist_status status;
	int index;

	(void)argument;
	pthread_mutex_lock(&playlist.lock);
	snprintf(id, sizeof(id), "%s", playlist.status.playlist);
	pthread_mutex_unlock(&playlist.lock);
	if (!fetch_manifest(id, error, (int)sizeof(error)))
	{
		platform_log("playlist maps: %s: %s", id, error);
		finish(PLAYLIST_FAILED, error);
		return NULL;
	}
	memset(&status, 0, sizeof(status));
	for (index = 0; index < playlist.manifest.file_count; index++)
	{
		const struct playlist_file *file = &playlist.manifest.files[index];
		char path[PATH_SIZE];
		unsigned int size = 0;
		int there;

		file_path(file, "", path, sizeof(path));
		there = file_size(path, &size);
		if (file->map[0])
			status.maps++;
		if (there && (!file->size || size == file->size || file->source == PLAYLIST_SOURCE_OWN))
			playlist.file_state[index] = FILE_INSTALLED;
		else if (there)
		{
			playlist.file_state[index] = FILE_OTHER_VERSION;
			status.other_versions++;
			platform_log("playlist maps: %s/%s is another version (%u bytes, the set's %u): left as it is",
				file->folder, file->file, size, file->size);
		}
		else if (file->source == PLAYLIST_SOURCE_OWN)
		{
			const char *pack = pack_name(&playlist.manifest, file);

			playlist.file_state[index] = FILE_OWN_MISSING;
			if (!status.own_missing++ || !strstr(status.own_packs, pack))
			{
				size_t used = strlen(status.own_packs);

				snprintf(status.own_packs + used, sizeof(status.own_packs) - used, "%s%s", used ? ", " : "", pack);
			}
		}
		else
		{
			playlist.file_state[index] = FILE_MISSING;
			status.missing++;
			status.missing_bytes += file->size;
			if (status.line_count < PLAYLIST_LINES)
				missing_line(file, status.lines[status.line_count++], PLAYLIST_LINE_SIZE);
			/* (the packs downloaded from: their credits, shown with the offer) */
			if (file->pack >= 0)
			{
				const struct playlist_pack *pack = &playlist.manifest.packs[(int)file->pack];
				const char *credit = pack->credits[0] ? pack->credits : pack->author;
				size_t used = strlen(status.credits);

				if (credit[0] && !strstr(status.credits, credit))
					snprintf(status.credits + used, sizeof(status.credits) - used, "%s%s", used ? " " : "", credit);
			}
		}
	}
	platform_log("playlist maps: %s: %d maps; %d files missing (%llu bytes), %d of the player's own, %d other versions",
		id, status.maps, status.missing, status.missing_bytes, status.own_missing, status.other_versions);
	pthread_mutex_lock(&playlist.lock);
	snprintf(status.playlist, sizeof(status.playlist), "%s", id);
	snprintf(status.name, sizeof(status.name), "%s", playlist.manifest.name);
	status.state = PLAYLIST_READY;
	playlist.status = status;
	playlist.running = 0;
	pthread_mutex_unlock(&playlist.lock);
	return NULL;
}

static void set_progress(const char *name, unsigned int received, unsigned int total)
{
	pthread_mutex_lock(&playlist.lock);
	snprintf(playlist.status.current, sizeof(playlist.status.current), "%s", name);
	playlist.status.received = received;
	playlist.status.current_total = total;
	pthread_mutex_unlock(&playlist.lock);
}

/* a file from Chupathingy's mirror into partial (its size and hash checked
as it comes): 1, or 0 with why */
static int receive_mirror(const struct playlist_file *file, const char *partial, char *error, int error_size)
{
	struct playlist_sha256 context;
	unsigned char digest[PLAYLIST_MANIFEST_SHA256_SIZE];
	char url[256], *buffer;
	unsigned int received = 0;
	HANDLE handle;
	int status = 0, length = -1, download, ok = 1;

	snprintf(url, sizeof(url), PLAYLIST_MIRROR "%s", file->path);
	platform_log("playlist maps: GET %s", url);
	set_progress(file->file, 0, file->size);
	download = posix_browser_download_open(url, build_identity_user_agent(), &status, &length, error, error_size);
	if (!download)
		return 0;
	if (length >= 0 && (unsigned int)length != file->size)
	{
		posix_browser_download_close(download);
		snprintf(error, (size_t)error_size, "the mirror's %s is %d bytes, the set's %u", file->file, length, file->size);
		return 0;
	}
	buffer = (char *)malloc(CHUNK);
	handle = CreateFileA(partial, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (!buffer || handle == INVALID_HANDLE_VALUE)
	{
		posix_browser_download_close(download);
		free(buffer);
		if (handle != INVALID_HANDLE_VALUE)
			CloseHandle(handle);
		snprintf(error, (size_t)error_size, "could not write into %s", file->folder);
		return 0;
	}
	playlist_sha256_begin(&context);
	for (;;)
	{
		DWORD written = 0;
		int count;

		if (playlist.cancel)
		{
			snprintf(error, (size_t)error_size, "cancelled");
			ok = 0;
			break;
		}
		count = posix_browser_download_read(download, buffer, CHUNK);
		if (!count)
			break;
		if (count < 0)
		{
			snprintf(error, (size_t)error_size, "the connection failed after %u bytes", received);
			ok = 0;
			break;
		}
		if ((unsigned int)count > file->size - received)
		{
			snprintf(error, (size_t)error_size, "%s is longer than the set says", file->file);
			ok = 0;
			break;
		}
		if (!WriteFile(handle, buffer, (DWORD)count, &written, NULL) || written != (DWORD)count)
		{
			snprintf(error, (size_t)error_size, "could not write into %s (is the disk full?)", file->folder);
			ok = 0;
			break;
		}
		playlist_sha256_add(&context, buffer, (size_t)count);
		received += (unsigned int)count;
		set_progress(file->file, received, file->size);
	}
	posix_browser_download_close(download);
	free(buffer);
	CloseHandle(handle);
	if (ok && received != file->size)
	{
		snprintf(error, (size_t)error_size, "%s was cut short (%u of %u bytes)", file->file, received, file->size);
		ok = 0;
	}
	if (ok)
	{
		playlist_sha256_end(&context, digest);
		if (memcmp(digest, file->sha256, sizeof(digest)))
		{
			snprintf(error, (size_t)error_size, "%s is not the set's file (its SHA-256 differs)", file->file);
			ok = 0;
		}
	}
	return ok;
}

/* a Custom Edition map from the community repositories (map_download.c),
installed in maps_ce, then hashed: 1, or 0 with why (the file deleted) */
static int receive_repositories(const struct playlist_file *file, char *error, int error_size)
{
	struct map_download_progress progress;
	unsigned char digest[PLAYLIST_MANIFEST_SHA256_SIZE];
	char name[PLAYLIST_MANIFEST_FILE_SIZE], path[PATH_SIZE];
	unsigned int size = 0;

	snprintf(name, sizeof(name), "%s", file->file);
	if (strlen(name) < 5 || strcmp(name + strlen(name) - 4, ".map"))
	{
		snprintf(error, (size_t)error_size, "the repositories have maps alone");
		return 0;
	}
	name[strlen(name) - 4] = 0;
	set_progress(file->file, 0, file->size);
	if (!map_download_start(name, 0))
	{
		snprintf(error, (size_t)error_size, "%s could not be asked for (another download is running?)", file->file);
		return 0;
	}
	for (;;)
	{
		map_download_peek(&progress);
		if (progress.state != MAP_DOWNLOAD_RUNNING)
			break;
		if (playlist.cancel)
			map_download_cancel();
		set_progress(file->file, progress.received, progress.total ? progress.total : file->size);
		Sleep(POLL_INTERVAL);
	}
	map_download_reset();
	if (progress.state != MAP_DOWNLOAD_DONE)
	{
		snprintf(error, (size_t)error_size, "%s", progress.state == MAP_DOWNLOAD_CANCELLED ? "cancelled" :
			progress.message[0] ? progress.message : "the repositories did not answer");
		return 0;
	}
	file_path(file, "", path, sizeof(path));
	if (!file_size(path, &size) || size != file->size || !file_sha256(path, digest) ||
		memcmp(digest, file->sha256, sizeof(digest)))
	{
		DeleteFileA(path);
		snprintf(error, (size_t)error_size, "the repositories' %s is not the set's file (%u bytes); deleted", file->file,
			size);
		return 0;
	}
	return 1;
}

static void *download_thread(void *argument)
{
	char error[160] = "", skipped[160] = "";
	int index, done = 0, failed = 0;

	(void)argument;
	for (index = 0; index < playlist.manifest.file_count && !playlist.cancel; index++)
	{
		const struct playlist_file *file = &playlist.manifest.files[index];
		char path[PATH_SIZE], partial[PATH_SIZE], folder[PATH_SIZE];
		int ok;

		if (playlist.file_state[index] != FILE_MISSING)
			continue;
		error[0] = 0;
		file_path(file, "", path, sizeof(path));
		snprintf(folder, sizeof(folder), "d:\\%s", file->folder);
		CreateDirectoryA(folder, NULL);
		if (file->source == PLAYLIST_SOURCE_CHUPATHINGY)
		{
			file_path(file, ".download", partial, sizeof(partial));
			DeleteFileA(partial);
			ok = receive_mirror(file, partial, error, (int)sizeof(error));
			/* (in place, never over a file that is there: MoveFileA refuses one) */
			if (ok && !MoveFileA(partial, path))
			{
				snprintf(error, sizeof(error), "%s/%s could not be put in place (is it there already?)", file->folder,
					file->file);
				ok = 0;
			}
			if (!ok)
				DeleteFileA(partial);
		}
		else if (file->source == PLAYLIST_SOURCE_HALONET)
			ok = receive_repositories(file, error, (int)sizeof(error));
		else
			ok = 0;
		if (ok)
		{
			platform_log("playlist maps: %s/%s installed (size and SHA-256 checked)", file->folder, file->file);
			playlist.file_state[index] = FILE_INSTALLED;
			/* (a Custom Edition map: marked as downloaded on the game's thread) */
			if (!strcmp(file->folder, "maps_ce") && file->map[0])
			{
				pthread_mutex_lock(&playlist.lock);
				snprintf(playlist.remember[playlist.remember_count++], PLAYLIST_MANIFEST_MAP_SIZE, "%s", file->map);
				pthread_mutex_unlock(&playlist.lock);
			}
			done++;
		}
		else if (!playlist.cancel)
		{
			platform_log("playlist maps: %s/%s refused: %s", file->folder, file->file, error);
			if (!failed++)
				snprintf(skipped, sizeof(skipped), "%s", error);
		}
		pthread_mutex_lock(&playlist.lock);
		playlist.status.done = done + failed;
		pthread_mutex_unlock(&playlist.lock);
	}
	if (playlist.cancel)
	{
		platform_log("playlist maps: cancelled after %d files", done);
		finish(PLAYLIST_CANCELLED, "cancelled");
	}
	else if (failed)
	{
		char message[160];

		snprintf(message, sizeof(message), "%d of %d not installed: %s", failed, done + failed, skipped);
		finish(done ? PLAYLIST_DONE : PLAYLIST_FAILED, message);
	}
	else
	{
		platform_log("playlist maps: %d files installed", done);
		finish(PLAYLIST_DONE, NULL);
	}
	return NULL;
}

static int start(void *(*procedure)(void *))
{
	pthread_t thread;

	if (pthread_create(&thread, NULL, procedure, NULL) != 0)
	{
		finish(PLAYLIST_FAILED, "could not start");
		return 0;
	}
	pthread_detach(thread);
	return 1;
}

#endif

/* ---------- public code */

int playlist_download_setting(void)
{
	return map_archive_setting(config_string("network.playlist_downloads"));
}

void playlist_download_set_setting(int setting)
{
	config_write("network.playlist_downloads", map_archive_setting_word(setting));
}

int playlist_download_possible(void)
{
#if defined(HALO_GAME_BROWSER) && !defined(HALO_SERVER)
	return playlist_download_setting() != MAP_DOWNLOADS_NEVER;
#else
	return 0;
#endif
}

int playlist_check_start(const char *id)
{
#if defined(HALO_GAME_BROWSER) && !defined(HALO_SERVER)
	if (!playlist_manifest_id_valid(id))
		return 0;
	pthread_mutex_lock(&playlist.lock);
	if (playlist.running)
	{
		pthread_mutex_unlock(&playlist.lock);
		return 0;
	}
	memset(&playlist.status, 0, sizeof(playlist.status));
	playlist.status.state = PLAYLIST_CHECKING;
	snprintf(playlist.status.playlist, sizeof(playlist.status.playlist), "%s", id);
	playlist.cancel = 0;
	playlist.running = 1;
	pthread_mutex_unlock(&playlist.lock);
	return start(check_thread);
#else
	(void)id;
	return 0;
#endif
}

int playlist_download_start(void)
{
#if defined(HALO_GAME_BROWSER) && !defined(HALO_SERVER)
	pthread_mutex_lock(&playlist.lock);
	if (playlist.running || playlist.status.state != PLAYLIST_READY || !playlist.status.missing)
	{
		pthread_mutex_unlock(&playlist.lock);
		return 0;
	}
	playlist.status.state = PLAYLIST_DOWNLOADING;
	playlist.status.done = 0;
	playlist.status.total = playlist.status.missing;
	playlist.cancel = 0;
	playlist.running = 1;
	pthread_mutex_unlock(&playlist.lock);
	platform_log("playlist maps: downloading %s's %d missing files", playlist.status.playlist, playlist.status.total);
	return start(download_thread);
#else
	return 0;
#endif
}

void playlist_download_cancel(void)
{
	playlist.cancel = 1;
}

void playlist_get(struct playlist_status *status)
{
	char remember[PLAYLIST_MANIFEST_FILES][PLAYLIST_MANIFEST_MAP_SIZE];
	int count, index;

	pthread_mutex_lock(&playlist.lock);
	*status = playlist.status;
	count = playlist.remember_count;
	memcpy(remember, playlist.remember, sizeof(remember[0]) * (size_t)count);
	playlist.remember_count = 0;
	pthread_mutex_unlock(&playlist.lock);
	for (index = 0; index < count; index++)
		map_downloaded_remember(remember[index]);
}

void playlist_reset(void)
{
	pthread_mutex_lock(&playlist.lock);
	if (!playlist.running)
		memset(&playlist.status, 0, sizeof(playlist.status));
	pthread_mutex_unlock(&playlist.lock);
}
