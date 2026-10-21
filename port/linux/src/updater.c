/*
UPDATER.C

The desktop ports' self-updater (Linux and Windows; the Android app updates
itself in Java, port/android). On macOS a release's build looks for a new
version the same way, and Yes opens the release's download page instead.

A release's build (HALO_RELEASE_BUILD: built from the release's tag,
v<version>, by ChupathingyCE's release workflow; tools/version.py) knows its
version (HALO_VERSION, 0.5.0b); nightlies and other builds never look for
updates. When update.auto in config.toml is true (the default), the game
asks GitHub for the latest release when it starts, on a thread of its own:
the game starts meanwhile, and nothing happens if the release is not newer
or cannot be reached. If it is newer, the game asks whether to update:

- Yes: the release's list of its update files (updates.json,
  update_manifest.h) is downloaded, with its signature (updates.json.sig)
  checked against the release key the game is built with (update_signature.c,
  update_key.h). The build the list names for this platform and configuration
  is downloaded next to the executable (into update.partial/), its size and
  SHA-256 checked against the list's, and unpacked, its files put in place
  of the running game's (which become <name>.old, deleted at the next start),
  and the new game started (the list's "main" for the file, the executable
  by the name the release gives it, or else the file this one was started
  from); this one quits. A release from before the list
  has none: its chupathingyce-<platform>-<release|debug>.zip is downloaded,
  with that zip's own signature (<zip>.sig) checked, as before.
- No: nothing, until the next start.
- Do not ask again: after the player confirms it, update.auto = false is
  written to config.toml.

The system side (the HTTPS download, the files, starting the new game) is
update.h's: posix_update.c on Linux, win32_update.c on Windows.

A build for the updater's own test (HALO_UPDATE_TEST, with a test key:
tools/test_update_install.py) looks for updates though it is no release's,
at the server HALO_UPDATE_TEST_SERVER names in place of GitHub. No release
is built with it.
*/

#include "platform.h"
#include "port_config.h"
#include "update.h"
#include "update_manifest.h"
#include "update_signature.h"

/* (given for this file by the build: tools/linux_build.py, windows_build.py,
macos_build.py; the Android app's version is its own, build.gradle) */
#ifndef HALO_VERSION
#define HALO_VERSION "dev"
#endif

#ifndef HALO_ANDROID

#include "zlib_prefixed.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef HALO_RELEASE_BUILD
#define HALO_RELEASE_BUILD 0
#endif
#ifdef HALO_UPDATE_TEST
#define UPDATER_TEST 1
#else
#define UPDATER_TEST 0
#endif
#ifdef __APPLE__
/* (the macOS application does not replace itself yet: a release's build
looks for a new version and offers its download page) */
#define UPDATER_DOWNLOAD_PAGE_ONLY 1
#else
#define UPDATER_DOWNLOAD_PAGE_ONLY 0
#endif
/* the latest release's page, where the macOS application is downloaded */
#define UPDATE_RELEASE_PAGE "https://github.com/" UPDATE_REPOSITORY "/releases/latest"
#ifndef HALO_BUILD_FLAVOR
#define HALO_BUILD_FLAVOR "release"
#endif

/* ChupathingyCE's releases */
#define UPDATE_REPOSITORY "ChupathingyCE/chupathingyce"
#define UPDATE_API "https://api.github.com/repos/" UPDATE_REPOSITORY
#define UPDATE_DOWNLOADS "https://github.com/" UPDATE_REPOSITORY "/releases/download"
#if defined(_WIN32) && defined(HALO_64BIT)
/* (the 64-bit Windows build's own download, ninja windows64: tools/ci_build.py) */
#define UPDATE_PLATFORM "windows64"
#define PATH_SEPARATOR "\\"
#elif defined(_WIN32)
#define UPDATE_PLATFORM "windows"
#define PATH_SEPARATOR "\\"
#elif defined(__APPLE__)
#define UPDATE_PLATFORM "macos"
#define PATH_SEPARATOR "/"
#elif defined(HALO_64BIT)
/* (the 64-bit Linux build's own download, ninja linux64: tools/ci_build.py) */
#define UPDATE_PLATFORM "linux64"
#define PATH_SEPARATOR "/"
#else
#define UPDATE_PLATFORM "linux"
#define PATH_SEPARATOR "/"
#endif
#define UPDATE_ASSET "chupathingyce-" UPDATE_PLATFORM "-" HALO_BUILD_FLAVOR ".zip"
/* the release's list of its update files (update_manifest.h) */
#define UPDATE_MANIFEST "updates.json"
#define UPDATE_DIRECTORY "update.partial"
#define MAXIMUM_UPDATE_FILES 32
/* the most a download may be: GitHub's answer about the latest release or a
release's list, a release's zip (about 30 MB), a signature */
#define MAXIMUM_CHECK_SIZE (1024ULL * 1024)
#define MAXIMUM_UPDATE_SIZE (256ULL * 1024 * 1024)
#define MAXIMUM_SIGNATURE_SIZE 4096ULL
/* the game's executable by its names, before and after the rename: a file
new to the folder by one of them is made executable, like one the zip marks
so */
static const char *const updater_executable_names[] = { "halo", "chupathingy" };

enum
{
	_updater_idle,
	_updater_checking,
	_updater_available,
	_updater_handled,
};

static SDL_AtomicInt updater_state;
static char updater_latest_version[32];
static char updater_directory[1024];
static char updater_executable[1024];
/* what is downloaded: from the release's list, or the zip's name as before
(listed: 0, and its own signature is checked) */
static struct
{
	int listed;
	char tag[160];
	char name[160];
	unsigned long long size;
	char sha256[UPDATE_SHA256_TEXT_SIZE];
	/* (the list's "main", or empty) */
	char main_name[160];
} updater_file;

/* where releases are looked for and downloaded from: GitHub, or a test's
server (HALO_UPDATE_TEST) */
static void updater_url(char *url, size_t size, int api, const char *path)
{
#ifdef HALO_UPDATE_TEST
	const char *server = getenv("HALO_UPDATE_TEST_SERVER");

	if (server && *server)
	{
		snprintf(url, size, "%s/%s/%s", server, api ? "repos/" UPDATE_REPOSITORY : UPDATE_REPOSITORY "/releases/download",
			path);
		return;
	}
#endif
	snprintf(url, size, "%s/%s", api ? UPDATE_API : UPDATE_DOWNLOADS, path);
}

/* ---------- paths */

static void updater_path(char *path, size_t size, const char *name)
{
	snprintf(path, size, "%s" PATH_SEPARATOR "%s", updater_directory, name);
}

static void updater_partial_path(char *path, size_t size, const char *name)
{
	snprintf(path, size, "%s" PATH_SEPARATOR UPDATE_DIRECTORY PATH_SEPARATOR "%s", updater_directory, name);
}

/* ---------- the zip file (the release's), stored or deflated entries */

static unsigned long zip_word(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] | (unsigned long)bytes[1] << 8;
}

static unsigned long zip_long(const unsigned char *bytes)
{
	return zip_word(bytes) | zip_word(bytes + 2) << 16;
}

/* an entry's data, unpacked, to the file at path; why it could not be is
written to reason */
static int zip_extract_entry(SDL_IOStream *zip, unsigned long local_offset, int method,
	unsigned long packed_size, unsigned long size, unsigned long crc, const char *path,
	char *reason, size_t reason_size)
{
	unsigned char header[30];
	unsigned char input[16384], output[16384];
	SDL_IOStream *file;
	z_stream stream;
	unsigned long remaining = packed_size, written = 0, checksum = crc32(0L, Z_NULL, 0);
	int succeeded = 0, ended = 0;

	if (SDL_SeekIO(zip, (Sint64)local_offset, SDL_IO_SEEK_SET) < 0 ||
		SDL_ReadIO(zip, header, sizeof(header)) != sizeof(header) || zip_long(header) != 0x04034b50 ||
		SDL_SeekIO(zip, (Sint64)(zip_word(header + 26) + zip_word(header + 28)), SDL_IO_SEEK_CUR) < 0)
	{
		snprintf(reason, reason_size, "its header at %lu could not be read (%s)", local_offset, SDL_GetError());
		return 0;
	}
	file = SDL_IOFromFile(path, "wb");
	if (!file)
	{
		snprintf(reason, reason_size, "could not create %s (%s)", path, SDL_GetError());
		return 0;
	}
	memset(&stream, 0, sizeof(stream));
	if (method == 8 && inflateInit2(&stream, -MAX_WBITS) != Z_OK)
	{
		snprintf(reason, reason_size, "zlib would not start (%s)", stream.msg ? stream.msg : "no message");
		SDL_CloseIO(file);
		return 0;
	}
	for (;;)
	{
		size_t count = remaining < sizeof(input) ? remaining : sizeof(input);

		if (count && SDL_ReadIO(zip, input, count) != count)
		{
			snprintf(reason, reason_size, "the download ended %lu bytes early (%s)", remaining, SDL_GetError());
			break;
		}
		remaining -= (unsigned long)count;
		if (method == 0)
		{
			/* (no more than the entry says it holds) */
			if (count > size - written)
			{
				snprintf(reason, reason_size, "it unpacks to more than %lu bytes", size);
				break;
			}
			if (SDL_WriteIO(file, input, count) != count)
			{
				snprintf(reason, reason_size, "could not write %s after %lu bytes (%s)", path, written, SDL_GetError());
				break;
			}
			checksum = crc32(checksum, input, (uInt)count);
			written += (unsigned long)count;
		}
		else
		{
			int result = Z_OK;

			stream.next_in = input;
			stream.avail_in = (uInt)count;
			do
			{
				size_t produced;

				stream.next_out = output;
				stream.avail_out = sizeof(output);
				result = inflate(&stream, Z_NO_FLUSH);
				if (result != Z_OK && result != Z_STREAM_END)
					break;
				produced = sizeof(output) - stream.avail_out;
				/* (no more than the entry says it holds) */
				if (produced > size - written)
				{
					snprintf(reason, reason_size, "it unpacks to more than %lu bytes", size);
					result = Z_ERRNO;
					break;
				}
				if (SDL_WriteIO(file, output, produced) != produced)
				{
					snprintf(reason, reason_size, "could not write %s after %lu bytes (%s)", path, written, SDL_GetError());
					result = Z_ERRNO;
					break;
				}
				checksum = crc32(checksum, output, (uInt)produced);
				written += (unsigned long)produced;
			} while (stream.avail_out == 0 && result != Z_STREAM_END);
			if (result == Z_STREAM_END)
				ended = 1;
			else if (result != Z_OK && result != Z_BUF_ERROR)
			{
				if (result != Z_ERRNO)
				{
					snprintf(reason, reason_size, "zlib stopped with %d after %lu of %lu bytes (%s)", result, written, size,
						stream.msg ? stream.msg : "no message");
				}
				break;
			}
		}
		/* (all of the input unpacked is enough, as the size and the CRC are
		checked: the game's zlib 1.1, used here before, could want a byte past
		the end of a raw stream before it said the stream had ended) */
		if (ended || !remaining)
		{
			if (written != size)
				snprintf(reason, reason_size, "it unpacked to %lu bytes, not %lu", written, size);
			else if (checksum != crc)
				snprintf(reason, reason_size, "its CRC is %08lx, not %08lx", checksum, crc);
			else
				succeeded = 1;
			break;
		}
	}
	if (method == 8)
		inflateEnd(&stream);
	if (!SDL_CloseIO(file) && succeeded)
	{
		snprintf(reason, reason_size, "could not finish writing %s (%s)", path, SDL_GetError());
		succeeded = 0;
	}
	return succeeded;
}

/* whether an entry's name is one of a release's files, which are all at the
zip's top, named with letters, digits and . _ + - (a folder, or a name that
could leave update.partial/ or mean something else to the system, is not;
nor are the game's own folders, data, logs, cache and private: an update
never touches what is in them, port_folders.h) */
static int zip_name_allowed(const char *name)
{
	static const char *const folders[] = { "data", "logs", "cache", "private" };
	const char *c;
	size_t index;

	if (!*name || *name == '.')
		return 0;
	for (index = 0; index < sizeof(folders) / sizeof(*folders); index++)
	{
		if (!SDL_strcasecmp(name, folders[index]))
			return 0;
	}
	for (c = name; *c; c++)
	{
		if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '.' ||
			*c == '_' || *c == '+' || *c == '-'))
		{
			return 0;
		}
	}
	return 1;
}

/* whether name is the game's executable by one of its names */
static int updater_executable_name(const char *name)
{
	size_t index;

	for (index = 0; index < sizeof(updater_executable_names) / sizeof(*updater_executable_names); index++)
	{
		if (!strcmp(name, updater_executable_names[index]))
			return 1;
	}
	return !strcmp(name, updater_file.main_name);
}

/* the zip's files (a flat folder) into update.partial/, their names in names
and whether each is executable (the zip's Unix mode says so, or it is the
game's) in executable */
static int zip_extract(SDL_IOStream *zip, char names[][256], int *executable, int *name_count, char *error,
	size_t error_size)
{
	unsigned char tail[65536 + 22];
	Sint64 size;
	size_t tail_size, index;
	unsigned long entries = 0, directory_offset = 0, entry;
	int found = 0, succeeded = 0;

	*name_count = 0;
	/* the end of the central directory, in the last 64 KB */
	size = SDL_GetIOSize(zip);
	tail_size = size < (Sint64)sizeof(tail) ? (size_t)size : sizeof(tail);
	if (size < 22 || SDL_SeekIO(zip, size - (Sint64)tail_size, SDL_IO_SEEK_SET) < 0 ||
		SDL_ReadIO(zip, tail, tail_size) != tail_size)
	{
		goto done;
	}
	for (index = tail_size - 22 + 1; index-- > 0;)
	{
		if (zip_long(tail + index) == 0x06054b50)
		{
			entries = zip_word(tail + index + 10);
			directory_offset = zip_long(tail + index + 16);
			found = 1;
			break;
		}
	}
	if (!found || SDL_SeekIO(zip, (Sint64)directory_offset, SDL_IO_SEEK_SET) < 0)
		goto done;
	for (entry = 0; entry < entries; entry++)
	{
		unsigned char header[46];
		char name[256];
		unsigned long name_length, extra_length, comment_length;
		Sint64 next;
		char path[1200];
		char reason[512] = "";

		if (SDL_ReadIO(zip, header, sizeof(header)) != sizeof(header) || zip_long(header) != 0x02014b50)
			goto done;
		name_length = zip_word(header + 28);
		extra_length = zip_word(header + 30);
		comment_length = zip_word(header + 32);
		if (name_length >= sizeof(name) || SDL_ReadIO(zip, name, name_length) != name_length)
			goto done;
		name[name_length] = 0;
		next = SDL_TellIO(zip) + (Sint64)(extra_length + comment_length);
		/* (a folder, or a name that would leave the game's folder, is left
		out: the release's files are all at its top) */
		if (memchr(name, 0, name_length))
			goto done;
		if (!zip_name_allowed(name))
		{
			if (name_length && name[name_length - 1] != '/')
				platform_log("update: left out %s (not one of a release's names)", name);
		}
		else if (*name_count < MAXIMUM_UPDATE_FILES)
		{
			int method = (int)zip_word(header + 10);

			if (method != 0 && method != 8)
			{
				snprintf(error, error_size, "the download packs %s in a way this build cannot read", name);
				goto done;
			}
			updater_partial_path(path, sizeof(path), name);
			if (!zip_extract_entry(zip, zip_long(header + 42), method, zip_long(header + 20), zip_long(header + 24),
				zip_long(header + 16), path, reason, sizeof(reason)))
			{
				snprintf(error, error_size, "could not unpack %s: %s", name, reason);
				goto done;
			}
			snprintf(names[*name_count], 256, "%s", name);
			/* (made on Unix, version made by's high byte 3: the mode in the
			external attributes' high half) */
			executable[*name_count] = updater_executable_name(name) ||
				(header[5] == 3 && ((zip_long(header + 38) >> 16) & 0111));
			(*name_count)++;
		}
		if (SDL_SeekIO(zip, next, SDL_IO_SEEK_SET) < 0)
			goto done;
	}
	succeeded = *name_count > 0;

done:
	if (!succeeded && !error[0])
		snprintf(error, error_size, "the download is not a zip file this build can read");
	return succeeded;
}

/* ---------- checking */

/* a version's numbers and hotfix letter: [v]<major>.<minor>.<patch>
followed by a letter (0.7.5a) or nothing; 0 if it is not one */
static int updater_parse_version(const char *text, long numbers[3], const char **suffix)
{
	int part;

	if (*text == 'v')
		text++;
	for (part = 0; part < 3; part++)
	{
		char *end;

		if (*text < '0' || *text > '9')
			return 0;
		numbers[part] = strtol(text, &end, 10);
		text = end;
		if (part < 2 && *text++ != '.')
			return 0;
	}
	*suffix = text;
	return 1;
}

/* whether version is newer than this build's: by its numbers, then its
hotfix letter, a version without one first (0.7.5, then 0.7.5a, 0.7.5b) */
static int updater_newer(const char *version)
{
	long latest[3], current[3];
	const char *latest_suffix, *current_suffix;
	int part;

	if (!updater_parse_version(version, latest, &latest_suffix) ||
		!updater_parse_version(HALO_VERSION, current, &current_suffix))
	{
		return 0;
	}
	for (part = 0; part < 3; part++)
	{
		if (latest[part] != current[part])
			return latest[part] > current[part];
	}
	return strcmp(latest_suffix, current_suffix) > 0;
}

/* GitHub's latest release's version (its tag, without the v), into version;
0 if there is none */
static int updater_latest_release(char *version, size_t size)
{
	char path[1200];
	char error[512] = "";
	char url[512];
	size_t length = 0;
	char *text;
	const char *tag;
	int found = 0;

	updater_path(path, sizeof(path), "update-check.json");
	updater_url(url, sizeof(url), 1, "releases/latest");
	if (!update_download(url, path, MAXIMUM_CHECK_SIZE, NULL, NULL, error, sizeof(error)))
	{
		platform_log("update: could not check for a new version: %s", error);
		return 0;
	}
	text = SDL_LoadFile(path, &length);
	update_delete_file(path);
	if (!text)
		return 0;
	/* "tag_name": "v<version>" */
	tag = strstr(text, "\"tag_name\"");
	if (tag)
	{
		tag = strchr(tag + 10, '"');
		if (tag && !strncmp(tag, "\"v", 2))
		{
			size_t end = strcspn(tag + 2, "\"");

			if (end > 0 && end < size)
			{
				memcpy(version, tag + 2, end);
				version[end] = 0;
				/* (a version goes into the download's address: letters,
				digits and . _ + - only) */
				found = zip_name_allowed(version);
			}
		}
	}
	SDL_free(text);
	return found;
}

static int SDLCALL updater_check_thread(void *context)
{
	char latest[sizeof(updater_latest_version)];

	(void)context;
	if (updater_latest_release(latest, sizeof(latest)) && updater_newer(latest))
	{
		platform_log("update: version %s is available (this is %s)", latest, HALO_VERSION);
		snprintf(updater_latest_version, sizeof(updater_latest_version), "%s", latest);
		SDL_SetAtomicInt(&updater_state, _updater_available);
	}
	else
	{
		platform_log("update: this is the latest version (%s)", HALO_VERSION);
		SDL_SetAtomicInt(&updater_state, _updater_handled);
	}
	return 0;
}

/* ---------- updating */

struct updater_download
{
	SDL_Mutex *lock;
	char url[512];
	char zip_path[1200];
	unsigned long long received, total;
	int finished, succeeded;
	char error[512];
};

static void updater_download_progress(void *context, unsigned long long received, unsigned long long total)
{
	struct updater_download *download = context;

	SDL_LockMutex(download->lock);
	download->received = received;
	download->total = total;
	SDL_UnlockMutex(download->lock);
}

static int SDLCALL updater_download_thread(void *context)
{
	struct updater_download *download = context;
	char error[512] = "";
	int succeeded = update_download(download->url, download->zip_path,
		updater_file.listed ? updater_file.size : MAXIMUM_UPDATE_SIZE, updater_download_progress, download, error,
		sizeof(error));

	SDL_LockMutex(download->lock);
	download->succeeded = succeeded;
	snprintf(download->error, sizeof(download->error), "%s", error);
	download->finished = 1;
	SDL_UnlockMutex(download->lock);
	return 0;
}

/* downloads the release's zip, showing how far it has got in a window of its
own (drawn in software, clear of the game's OpenGL); 1 when it is there */
static int updater_download_zip(const char *zip_path, char *error, size_t error_size)
{
	static struct updater_download download;
	SDL_Window *window;
	SDL_Renderer *renderer = NULL;
	SDL_Thread *thread;
	int finished = 0;

	memset(&download, 0, sizeof(download));
	download.lock = SDL_CreateMutex();
	{
		char path[400];

		snprintf(path, sizeof(path), "%s/%s", updater_file.tag, updater_file.name);
		updater_url(download.url, sizeof(download.url), 0, path);
	}
	snprintf(download.zip_path, sizeof(download.zip_path), "%s", zip_path);
	thread = SDL_CreateThread(updater_download_thread, "update download", &download);
	if (!thread)
	{
		snprintf(error, error_size, "could not start the download");
		return 0;
	}
	window = SDL_CreateWindow("Chupathingy", 640, 150, 0);
	if (window)
		renderer = SDL_CreateRenderer(window, SDL_SOFTWARE_RENDERER);
	while (!finished)
	{
		SDL_Event event;
		unsigned long long received, total;

		/* (the game's own events wait: its window is not drawn meanwhile) */
		while (SDL_PollEvent(&event))
		{
		}
		SDL_LockMutex(download.lock);
		finished = download.finished;
		received = download.received;
		total = download.total;
		SDL_UnlockMutex(download.lock);
		if (renderer)
		{
			char line[160];
			SDL_FRect bar = { 20.0f, 100.0f, 600.0f, 24.0f };
			float fraction = total ? (float)((double)received / (double)total) : 0.0f;

			SDL_SetRenderDrawColor(renderer, 12, 16, 20, 255);
			SDL_RenderClear(renderer);
			SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
			SDL_SetRenderScale(renderer, 2.0f, 2.0f);
			SDL_RenderDebugText(renderer, 10.0f, 10.0f, "Downloading the update...");
			SDL_SetRenderScale(renderer, 1.0f, 1.0f);
			snprintf(line, sizeof(line), "version %s  (%llu of %llu MB)", updater_latest_version, received >> 20,
				total >> 20);
			SDL_RenderDebugText(renderer, 20.0f, 70.0f, line);
			SDL_SetRenderDrawColor(renderer, 60, 66, 72, 255);
			SDL_RenderFillRect(renderer, &bar);
			bar.w *= fraction;
			SDL_SetRenderDrawColor(renderer, 90, 160, 90, 255);
			SDL_RenderFillRect(renderer, &bar);
			SDL_RenderPresent(renderer);
		}
		SDL_Delay(16);
	}
	SDL_WaitThread(thread, NULL);
	if (renderer)
		SDL_DestroyRenderer(renderer);
	if (window)
		SDL_DestroyWindow(window);
	SDL_DestroyMutex(download.lock);
	if (!download.succeeded)
		snprintf(error, error_size, "%s", download.error);
	return download.succeeded;
}

/* the downloaded zip, checked against the release's list (its size and
SHA-256) or, from a release without one, its own signature, and unpacked
from the copy that was checked; the zip is deleted either way */
static int updater_unpack(const char *zip_path, char names[][256], int *executable, int *name_count, char *error,
	size_t error_size)
{
	char signature_path[1200];
	size_t zip_size = 0, signature_size = 0;
	unsigned char *zip = SDL_LoadFile(zip_path, &zip_size);
	char *signature = NULL;
	SDL_IOStream *stream;
	int succeeded = 0;

	update_delete_file(zip_path);
	if (!zip || zip_size > MAXIMUM_UPDATE_SIZE)
	{
		snprintf(error, error_size, "could not read the download");
		goto done;
	}
	if (updater_file.listed)
	{
		char digest[UPDATE_SHA256_TEXT_SIZE];

		if (zip_size != updater_file.size)
		{
			snprintf(error, error_size, "the download was not installed: it is %llu bytes, not the list's %llu",
				(unsigned long long)zip_size, updater_file.size);
			goto done;
		}
		update_sha256(zip, zip_size, digest);
		if (strcmp(digest, updater_file.sha256))
		{
			snprintf(error, error_size, "the download was not installed: its SHA-256 is not the list's");
			goto done;
		}
		platform_log("update: the download's SHA-256 is the release list's");
	}
	else if (update_signature_required())
	{
		char url[512], reason[256] = "";

		updater_partial_path(signature_path, sizeof(signature_path), UPDATE_ASSET ".sig");
		char path[200];

		snprintf(path, sizeof(path), "v%s/" UPDATE_ASSET ".sig", updater_latest_version);
		updater_url(url, sizeof(url), 0, path);
		if (!update_download(url, signature_path, MAXIMUM_SIGNATURE_SIZE, NULL, NULL, reason, sizeof(reason)))
		{
			snprintf(error, error_size, "the new version's signature could not be downloaded (%s)", reason);
			goto done;
		}
		signature = SDL_LoadFile(signature_path, &signature_size);
		update_delete_file(signature_path);
		if (!update_signature_check(zip, zip_size, UPDATE_ASSET, updater_latest_version, signature,
			signature ? signature_size : 0, reason, sizeof(reason)))
		{
			snprintf(error, error_size, "the download was not installed: %s", reason);
			goto done;
		}
		platform_log("update: the download's signature checks");
	}
	else
	{
		platform_log("update: this build has no release key: the download's signature is not checked");
	}
	stream = SDL_IOFromConstMem(zip, zip_size);
	if (!stream)
	{
		snprintf(error, error_size, "could not read the download");
		goto done;
	}
	succeeded = zip_extract(stream, names, executable, name_count, error, error_size);
	SDL_CloseIO(stream);

done:
	SDL_free(signature);
	SDL_free(zip);
	return succeeded;
}

/* which file to download (updater_file): the one the release's list names
for this platform and configuration, once the list's signature checks; a
release without a list (or whose list cannot be downloaded), or one whose
list has no such file, its zip by the name it had before the list. 0, and
why in error, when the list is there but is not to be trusted */
static int updater_find_file(char *error, size_t error_size)
{
	char manifest_path[1200], signature_path[1200], url[512], path[200], reason[256] = "";
	size_t manifest_size = 0, signature_size = 0;
	char *manifest = NULL, *signature = NULL;
	int result, succeeded = 0;

	memset(&updater_file, 0, sizeof(updater_file));
	snprintf(updater_file.tag, sizeof(updater_file.tag), "v%s", updater_latest_version);
	snprintf(updater_file.name, sizeof(updater_file.name), "%s", UPDATE_ASSET);
	updater_partial_path(manifest_path, sizeof(manifest_path), UPDATE_MANIFEST);
	updater_partial_path(signature_path, sizeof(signature_path), UPDATE_MANIFEST ".sig");
	snprintf(path, sizeof(path), "v%s/" UPDATE_MANIFEST, updater_latest_version);
	updater_url(url, sizeof(url), 0, path);
	if (!update_download(url, manifest_path, MAXIMUM_CHECK_SIZE, NULL, NULL, reason, sizeof(reason)))
	{
		/* (its zip's own signature is checked instead) */
		platform_log("update: no list of update files (%s): downloading " UPDATE_ASSET, reason);
		return 1;
	}
	manifest = SDL_LoadFile(manifest_path, &manifest_size);
	update_delete_file(manifest_path);
	if (!manifest)
	{
		snprintf(error, error_size, "could not read the release's list of update files");
		goto done;
	}
	if (update_signature_required())
	{
		snprintf(path, sizeof(path), "v%s/" UPDATE_MANIFEST ".sig", updater_latest_version);
		updater_url(url, sizeof(url), 0, path);
		if (!update_download(url, signature_path, MAXIMUM_SIGNATURE_SIZE, NULL, NULL, reason, sizeof(reason)))
		{
			snprintf(error, error_size, "the release's list of update files has no signature (%s)", reason);
			goto done;
		}
		signature = SDL_LoadFile(signature_path, &signature_size);
		update_delete_file(signature_path);
		if (!update_signature_check_document((const unsigned char *)manifest, manifest_size, signature,
			signature ? signature_size : 0, reason, sizeof(reason)))
		{
			snprintf(error, error_size, "the release's list of update files was not used: %s", reason);
			goto done;
		}
	}
	else
	{
		platform_log("update: this build has no release key: the list's signature is not checked");
	}
	result = update_manifest_find(manifest, manifest_size, updater_latest_version, UPDATE_PLATFORM, HALO_BUILD_FLAVOR,
		"zip", updater_file.tag, sizeof(updater_file.tag), updater_file.name, sizeof(updater_file.name),
		&updater_file.size, updater_file.sha256, sizeof(updater_file.sha256), updater_file.main_name,
		sizeof(updater_file.main_name), reason, sizeof(reason));
	if (result == _update_manifest_found && (!updater_file.size || updater_file.size > MAXIMUM_UPDATE_SIZE))
	{
		snprintf(error, error_size, "the release's list gives %s a size this build does not install (%llu bytes)",
			updater_file.name, updater_file.size);
	}
	else if (result == _update_manifest_found)
	{
		updater_file.listed = 1;
		platform_log("update: the release's list names %s (%s, %llu bytes%s%s)", updater_file.name, updater_file.tag,
			updater_file.size, updater_file.main_name[0] ? ", main " : "", updater_file.main_name);
		succeeded = 1;
	}
	else if (result == _update_manifest_missing)
	{
		/* (updater_file as it was: the zip by its old name) */
		platform_log("update: %s: downloading " UPDATE_ASSET, reason);
		succeeded = 1;
	}
	else
	{
		snprintf(error, error_size, "the release's list of update files was not used: %s", reason);
	}

done:
	SDL_free(signature);
	SDL_free(manifest);
	return succeeded;
}

/* the first count of the new files, already in place, taken back out (to
the update folder) and the old ones (<name>.old) put back, the last first:
the install as it was before the update. A new file that had no old one is
deleted */
static void updater_put_back(char names[][256], int count)
{
	int index, failures = 0;

	for (index = count - 1; index >= 0; index--)
	{
		char path[1200], new_path[1200], old_path[1300];

		updater_path(path, sizeof(path), names[index]);
		updater_partial_path(new_path, sizeof(new_path), names[index]);
		snprintf(old_path, sizeof(old_path), "%s.old", path);
		if (SDL_GetPathInfo(old_path, NULL))
		{
			if (!update_replace_file(path, old_path, new_path))
			{
				platform_log("update: could not put back %s", names[index]);
				failures++;
			}
		}
		else
		{
			update_delete_file(path);
		}
	}
	if (failures)
		platform_log("update: %d of %d replaced files could not be put back", failures, count);
	else
		platform_log("update: the %d replaced files put back: the install is as it was", count);
}

/* the new files in place of the old ones, each old one kept as <name>.old
(and as it was made: an executable stays one, update_replace_file), a new
executable made one; if one cannot be, the ones before it put back as they
were, and 0 */
static int updater_replace_files(char names[][256], const int *executable, int name_count, char *error,
	size_t error_size)
{
	int index;

	for (index = 0; index < name_count; index++)
	{
		char path[1200], new_path[1200], old_path[1300];
		int existed;

		updater_path(path, sizeof(path), names[index]);
		updater_partial_path(new_path, sizeof(new_path), names[index]);
		snprintf(old_path, sizeof(old_path), "%s.old", path);
		existed = SDL_GetPathInfo(path, NULL);
		if (!update_replace_file(path, new_path, old_path))
		{
			/* (that one is as it was: update_replace_file) */
			snprintf(error, error_size, "could not replace %s", path);
			if (index > 0)
				updater_put_back(names, index);
			return 0;
		}
		if (!existed && executable[index] && !update_make_executable(path))
		{
			snprintf(error, error_size, "could not make %s executable", path);
			updater_put_back(names, index + 1);
			return 0;
		}
	}
	return 1;
}

/* the executable to start once the new files are in place: the list's main,
if the zip had it, else the one this is */
static void updater_new_executable(char names[][256], int name_count, char *path, size_t size)
{
	int index;

	snprintf(path, size, "%s", updater_executable);
	if (!updater_file.main_name[0])
		return;
	for (index = 0; index < name_count; index++)
	{
		if (!strcmp(names[index], updater_file.main_name))
		{
			updater_path(path, size, names[index]);
			return;
		}
	}
	platform_log("update: the list's main, %s, is not in the download: starting %s", updater_file.main_name, path);
}

/* downloads, unpacks and puts in place the new build, and starts it; returns
only if something failed */
static void updater_update(void)
{
	char names[MAXIMUM_UPDATE_FILES][256];
	int executable[MAXIMUM_UPDATE_FILES];
	char zip_path[1200], partial[1200], start[1200], error[512] = "";
	int name_count = 0;

	updater_path(partial, sizeof(partial), UPDATE_DIRECTORY);
	updater_partial_path(zip_path, sizeof(zip_path), UPDATE_ASSET);
	platform_log("update: downloading version %s", updater_latest_version);
	if (!update_make_directory(partial))
	{
		snprintf(error, sizeof(error), "could not make %s (is the game's folder read-only?)", partial);
	}
	else if (updater_find_file(error, sizeof(error)) && updater_download_zip(zip_path, error, sizeof(error)) &&
		updater_unpack(zip_path, names, executable, &name_count, error, sizeof(error)))
	{
		if (updater_replace_files(names, executable, name_count, error, sizeof(error)))
		{
			update_delete_file(partial);
			updater_new_executable(names, name_count, start, sizeof(start));
			platform_log("update: starting version %s (%s)", updater_latest_version,
				strrchr(start, PATH_SEPARATOR[0]) ? strrchr(start, PATH_SEPARATOR[0]) + 1 : start);
			if (update_launch(start))
				exit(EXIT_SUCCESS);
			snprintf(error, sizeof(error), "the new version is in place, but could not be started: start it again");
		}
	}
	platform_log("update: failed: %s", error);
	{
		char message[800];

		snprintf(message, sizeof(message), "The update failed:\n\n%s", error);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Chupathingy", message, NULL);
	}
}

/* the files a previous update left behind */
static void updater_clean_up(void)
{
	static const char *const names[] =
	{
		"halo.old", "halo.exe.old", "chupathingy.old", "chupathingy.exe.old", "SDL3.dll.old", "libSDL3.so.0.old",
		"SDL3-LICENSE.txt.old", "mbedtls-LICENSE.txt.old", "LICENSE.txt.old",
	};
	char path[1200];
	size_t index;

	for (index = 0; index < sizeof(names) / sizeof(*names); index++)
	{
		updater_path(path, sizeof(path), names[index]);
		update_delete_file(path);
	}
	updater_partial_path(path, sizeof(path), UPDATE_ASSET);
	update_delete_file(path);
	updater_partial_path(path, sizeof(path), UPDATE_ASSET ".sig");
	update_delete_file(path);
	updater_partial_path(path, sizeof(path), UPDATE_MANIFEST);
	update_delete_file(path);
	updater_partial_path(path, sizeof(path), UPDATE_MANIFEST ".sig");
	update_delete_file(path);
	updater_path(path, sizeof(path), UPDATE_DIRECTORY);
	update_delete_file(path);
}

/* ---------- the game's */

/* at start-up (sdl_platform.c): looks for a new version, in the background */
void updater_start(void)
{
	char *slash;

	/* (the macOS application only offers the download page: no files of its
	own to find or clean up) */
	if (UPDATER_DOWNLOAD_PAGE_ONLY)
	{
		/* (the check's answer is kept in the save root while it is read) */
		snprintf(updater_directory, sizeof(updater_directory), "%s", platform_save_root());
	}
	else
	{
		if (!update_executable_path(updater_executable, sizeof(updater_executable)))
			return;
		snprintf(updater_directory, sizeof(updater_directory), "%s", updater_executable);
		slash = strrchr(updater_directory, PATH_SEPARATOR[0]);
		if (!slash)
			return;
		*slash = 0;
		updater_clean_up();
	}
	/* (not for builds other than a release's, the player's no, or runs nobody
	is watching, but for a test with its answer) */
	if (!(HALO_RELEASE_BUILD || UPDATER_TEST) || !config_boolean("update.auto") ||
		(!config_string("debug.update_answer")[0] && (config_boolean("debug.hidden_window") ||
			config_real("debug.exit_after") > 0.0 || config_string("debug.network_test")[0])))
	{
		return;
	}
	SDL_SetAtomicInt(&updater_state, _updater_checking);
	{
		SDL_Thread *thread = SDL_CreateThread(updater_check_thread, "update check", NULL);

		if (thread)
			SDL_DetachThread(thread);
		else
			SDL_SetAtomicInt(&updater_state, _updater_handled);
	}
}

/* whether the game runs under gamescope (the Steam Deck's Game Mode), where
a system dialog such as SDL's message box crashes the game. gamescope sets
GAMESCOPE_WAYLAND_DISPLAY for the programs it runs and Game Mode's session
XDG_CURRENT_DESKTOP; Steam sets SteamDeck and SteamGamepadUI there. Wine
passes them on to the Windows build under Proton. */
static int updater_gamescope(void)
{
	const char *wayland_display = getenv("GAMESCOPE_WAYLAND_DISPLAY");
	const char *desktop = getenv("XDG_CURRENT_DESKTOP");
	const char *steam_deck = getenv("SteamDeck");
	const char *gamepad_ui = getenv("SteamGamepadUI");

	return (wayland_display && wayland_display[0]) || (desktop && strstr(desktop, "gamescope")) ||
		(steam_deck && !strcmp(steam_deck, "1") && gamepad_ui && !strcmp(gamepad_ui, "1"));
}

/* every frame, on the game's thread (sdl_platform.c): asks the player once a
new version is found */
void updater_poll(SDL_Window *window)
{
	static const SDL_MessageBoxButtonData question_buttons[] =
	{
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" },
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No" },
		{ 0, 2, "Do not ask again" },
	};
	static const SDL_MessageBoxButtonData confirm_buttons[] =
	{
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" },
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No" },
	};
	char message[400];
	int answer = 0;
	int fullscreen;
	const char *test_answer = config_string("debug.update_answer");

	if (SDL_GetAtomicInt(&updater_state) != _updater_available)
		return;
	SDL_SetAtomicInt(&updater_state, _updater_handled);
	/* (an automated test's answer: debug.update_answer) */
	if (test_answer[0])
	{
		platform_log("update: answering %s (debug.update_answer)", test_answer);
		if (!strcmp(test_answer, "yes") && UPDATER_DOWNLOAD_PAGE_ONLY)
			platform_log("update: would open " UPDATE_RELEASE_PAGE);
		else if (!strcmp(test_answer, "yes"))
			updater_update();
		else if (!strcmp(test_answer, "never"))
			config_write_boolean("update.auto", 0);
		return;
	}
	/* (no question under gamescope: the player updates from the desktop, or
	with debug.update_answer) */
	if (updater_gamescope())
	{
		platform_log("update: %s is out (this is %s); not asking under gamescope (Steam Deck Game Mode)",
			updater_latest_version, HALO_VERSION);
		return;
	}
	/* (a dialog cannot show above a fullscreen game) */
	fullscreen = window && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN);
	if (fullscreen)
		SDL_SetWindowFullscreen(window, false);
	if (UPDATER_DOWNLOAD_PAGE_ONLY)
	{
		snprintf(message, sizeof(message),
			"Chupathingy %s is out. You have %s.\n\n"
			"Open the download page?",
			updater_latest_version, HALO_VERSION);
	}
	else
	{
		snprintf(message, sizeof(message),
			"Chupathingy %s is out. You have %s.\n\n"
			"Update now? The game will restart.",
			updater_latest_version, HALO_VERSION);
	}
	{
		SDL_MessageBoxData question = { SDL_MESSAGEBOX_INFORMATION, window, "Chupathingy: new version", message,
			3, question_buttons, NULL };

		if (!SDL_ShowMessageBox(&question, &answer))
			answer = 0;
	}
	if (answer == 2)
	{
		SDL_MessageBoxData confirm = { SDL_MESSAGEBOX_WARNING, window, "Chupathingy: new version",
			"Stop asking about new versions?\n\n"
			"Turn Check for Updates back on in Settings to ask again.",
			2, confirm_buttons, NULL };
		int confirmed = 0;

		if (SDL_ShowMessageBox(&confirm, &confirmed) && confirmed == 1)
		{
			if (config_write_boolean("update.auto", 0))
				platform_log("update: update.auto = false written to config.toml");
			else
				platform_log("update: could not write update.auto to config.toml");
		}
	}
	else if (answer == 1 && UPDATER_DOWNLOAD_PAGE_ONLY)
	{
		platform_log("update: opening " UPDATE_RELEASE_PAGE);
		if (!SDL_OpenURL(UPDATE_RELEASE_PAGE))
			platform_log("update: could not open the download page: %s", SDL_GetError());
	}
	else if (answer == 1)
	{
		updater_update();
	}
	if (fullscreen)
		SDL_SetWindowFullscreen(window, true);
}

#else

void updater_start(void)
{
}

#endif

/* this build's version (HALO_VERSION: 0.5.0b, 0.5.0b-nightly.42, 0.5.0b-dev),
for the window's title (sdl_platform.c) */
const char *updater_version(void)
{
	return HALO_VERSION;
}
