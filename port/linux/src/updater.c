/*
UPDATER.C

The desktop ports' self-updater (Linux and Windows; the Android app updates
itself in Java, port/android). On macOS a release's build looks for a new
version the same way, and Yes opens the release's download page instead.

A release's build (HALO_RELEASE_BUILD: built from the release's tag,
v<version>, by ChupathingyCE's release workflow; tools/version.py) knows its
version (HALO_VERSION, 0.5.0b), and so do the test builds and nightlies
(HALO_UPDATE_CHANNEL testing or nightly: 0.9.1-testing.202611101530); other
builds never look for updates. When update.auto in config.toml is true (the
default), the game asks GitHub for the latest release when it starts, on a
thread of its own: the game starts meanwhile, and nothing happens if the
release is not newer or cannot be reached. If it is newer, the game asks
whether to update:

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

The game is chupathingy (chupathingy.exe) from 0.9.0, and was halo
(halo.exe) before. A release's zip carries both: the game, which the list
names as its "main", and a small halo that starts it (rename_stub.h), for
shortcuts and for the updaters from before the list, which start halo again.
At its start the renamed game removes an old halo beside it that is a game,
never the stub (updater_remove_old_program).
- Do not ask again: after the player confirms it, update.auto = false is
  written to config.toml.

Run as an AppImage (Linux: update_appimage_path), the game is that one file,
mounted read-only: Yes downloads the AppImage the release's signed list
names for this platform and configuration (kind "appimage") beside it (into
update.partial/), checks its size and SHA-256 against the list's, and puts
it in place of the AppImage file, by that file's own name (the old one
becomes <name>.old, deleted at the next start), executable as it was, and
starts it. Nothing is unpacked: a release without a list, or whose list has
no AppImage, is not installed over an AppImage (its zip would only fill the
AppImage's folder with a second copy of the game), and the player is told to
download the new AppImage.

Under gamescope (the Steam Deck's Game Mode: updater_gamescope) a system
dialog crashes the game, so nothing is asked: the main menu's version line
says where to update (UPDATER_GAMESCOPE_WHERE), and no download window or
failure dialog is shown either. The game's own pages install it, there and
everywhere else: the setup screen's UPDATES and Settings > Online >
Updates' INSTALL UPDATE (updater_install_begin) download and put it in
place on a thread of their own, the page showing how it goes and why it
failed (updater_install_status), and the game's thread then starts the new
version. On Linux the new version is this same process (update_start_in_place),
so Steam and gamescope keep the game.

The update channel (update.channel: "release", "testing" or "nightly";
a build's default is its own channel, so a release's is "release"): Release
is offered the latest release alone, as before. Testing is also offered the
testing feed's build, and Nightly the testing and nightly feeds' builds
(update_manifest.h: a fixed list each, testing/updates.json and
nightly/updates.json, signed by the prerelease key or a release key,
update_signature_check_feed), whichever of them is the newest
(update_version_compare). Nothing older than this build is ever offered, so
going back from Testing to Release waits for the next release newer than
the build installed. A feed's list, checked once, is kept, and its file is
installed from that copy (no second download of the list).

A packaged install (packaged_install.h: the AUR's chupathingy-bin, marked
by the build, HALO_PACKAGED or a file named "packaged" beside the
executable) is its package manager's to update: nothing is downloaded or
asked, and a newer release only puts "update Chupathingy with your package
manager" on the main menu's version line and in the log.

The system side (the HTTPS download, the files, starting the new game) is
update.h's: posix_update.c on Linux, win32_update.c on Windows.

A build for the updater's own test (HALO_UPDATE_TEST, with a test key:
tools/test_update_install.py) looks for updates though it is no release's,
at the server HALO_UPDATE_TEST_SERVER names in place of GitHub. No release
is built with it.
*/

#include "rename_stub.h"
#include "platform.h"
#include "port_config.h"
#include "update.h"
#include "update_manifest.h"
#include "update_signature.h"

#include <stdlib.h>
#include <string.h>

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

#ifdef HALO_CONSOLE_SETUP_SCREENS
/* the port's screens, for a console with no dialogs (halo_console_features.h):
one shown while the update downloads, a notice read with the pad, and the
question, answered 1 update, 2 never ask again, 0 not now */
void *console_setup_screen_open(void);
void console_setup_screen_progress(void *screen, float fraction, const char *title, const char *line);
void console_setup_screen_close(void *screen);
void console_setup_notice(const char *first, const char *second);
int console_setup_update_question(const char *latest, const char *current);
#endif

#ifndef HALO_RELEASE_BUILD
#define HALO_RELEASE_BUILD 0
#endif
/* a test build's or a nightly's (tools/version.py gives HALO_UPDATE_CHANNEL,
"testing" or "nightly", only to the builds the test and nightly workflows
make, of a version of that shape): it looks for updates on its channel, as a
release's does. Never a console port's (no channels there) */
#if defined(HALO_UPDATE_CHANNEL) && !defined(HALO_PORT_UPDATE_PLATFORM)
#define UPDATER_PRERELEASE_BUILD 1
#else
#define UPDATER_PRERELEASE_BUILD 0
#endif
/* whether this build looks for updates at all */
#define UPDATER_LOOKS (HALO_RELEASE_BUILD || UPDATER_PRERELEASE_BUILD || UPDATER_TEST)
/* (a release's or a prerelease's build, not a test's, with no update key) */
#define UPDATER_KEYLESS ((HALO_RELEASE_BUILD || UPDATER_PRERELEASE_BUILD) && !UPDATER_TEST && !update_signature_required())
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
/* the latest release's page, where the macOS application is downloaded (a
test build's or a nightly's: its pre-release's page, updater_page) */
#define UPDATE_RELEASE_PAGE "https://github.com/" UPDATE_REPOSITORY "/releases/latest"
#ifndef HALO_BUILD_FLAVOR
#define HALO_BUILD_FLAVOR "release"
#endif

/* ChupathingyCE's releases, or a console port's own (HALO_PORT_UPDATE_API on
its compile line, its own key in its halo_port_update_keys.h: update_key.h),
laid out as GitHub's addresses are */
#ifdef HALO_PORT_UPDATE_API
#define UPDATE_REPOSITORY HALO_PORT_UPDATE_REPOSITORY
#define UPDATE_API HALO_PORT_UPDATE_API
#define UPDATE_DOWNLOADS HALO_PORT_UPDATE_API "/releases/download"
#else
#define UPDATE_REPOSITORY "ChupathingyCE/chupathingyce"
#define UPDATE_API "https://api.github.com/repos/" UPDATE_REPOSITORY
#define UPDATE_DOWNLOADS "https://github.com/" UPDATE_REPOSITORY "/releases/download"
#endif
#if defined(HALO_PORT_UPDATE_PLATFORM)
/* (a console port's own download: on the game's releases, signed with the
release key, unless the port has releases of its own, HALO_PORT_UPDATE_API) */
#define UPDATE_PLATFORM HALO_PORT_UPDATE_PLATFORM
#define PATH_SEPARATOR "/"
#elif defined(_WIN32) && defined(HALO_64BIT)
/* (the 64-bit Windows build's own download, ninja windows64: tools/ci_build.py) */
#define UPDATE_PLATFORM "windows64"
#define PATH_SEPARATOR "\\"
#elif defined(_WIN32)
#define UPDATE_PLATFORM "windows"
#define PATH_SEPARATOR "\\"
#elif defined(__APPLE__)
#define UPDATE_PLATFORM "macos"
#define PATH_SEPARATOR "/"
#elif defined(__FreeBSD__) && defined(HALO_64BIT)
/* (the FreeBSD build's own download, ninja freebsd64: tools/unix64_build.py;
never the Linux one) */
#define UPDATE_PLATFORM "freebsd64"
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
/* the 32-bit Windows build on 64-bit Windows offers the 64-bit build's
download once (update.offer_64bit, updater_poll): its graphics drivers'
32-bit parts crash far more (game issue #833) */
#if defined(_WIN32) && !defined(HALO_64BIT) && !defined(HALO_PORT_UPDATE_PLATFORM)
#define UPDATER_OFFER_64BIT 1
#else
#define UPDATER_OFFER_64BIT 0
#endif
/* (an AppImage's update: the release's list names it, kind "appimage") */
#define UPDATE_APPIMAGE_ASSET "chupathingyce-" UPDATE_PLATFORM "-" HALO_BUILD_FLAVOR ".AppImage"
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
static char updater_latest_version[48];

/* the channels (update.channel), from the most stable */
enum
{
	_channel_release,
	_channel_testing,
	_channel_nightly,
	NUMBER_OF_CHANNELS
};

static const char *const updater_channel_names[NUMBER_OF_CHANNELS] = { "release", "testing", "nightly" };

/* where the newer version came from: _channel_release (the latest release),
or a feed, whose checked list (updater_feed_list, updater_feed_size) is kept
for the download (updater_find_file) */
static int updater_latest_channel;
static char *updater_feed_list;
static size_t updater_feed_size;
/* (the check's results above, and updater_latest_version, are published by
the check's thread under this lock, all at once at its end; an install
takes its own copy of them under it, and refuses while a check runs) */
static SDL_Mutex *updater_lock;
/* the install's copy of the feed's list (updater_take_results) */
static char *updater_install_list;
static size_t updater_install_size;
static int updater_install_channel;
static char updater_install_version[sizeof(updater_latest_version)];
/* (a feed's build's page, for the macOS download page: its file's tag) */
static char updater_feed_tag[160];
static char updater_directory[1024];
static char updater_executable[1024];
/* whether this runs from an AppImage, updater_executable then being the
AppImage file (update_appimage_path) */
static int updater_appimage;
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

/* the platform whose download is installed: this build's (UPDATE_PLATFORM),
or the 64-bit Windows build's once the player takes it (UPDATER_OFFER_64BIT) */
static const char *updater_platform = UPDATE_PLATFORM;
/* whether to ask about the 64-bit build when the check is answered */
static int updater_offer_64bit;

/* an install from a page of the game's own (the setup screen's UPDATES, the
menus' INSTALL UPDATE: updater_install_begin), on a thread of its own while
the page goes on drawing, with no window or dialog of its own: where it is,
how much is downloaded, why it failed, and the program to start once it is
in place (the game's thread starts it: updater_install_finish) */
enum
{
	_install_idle,
	_install_downloading,
	_install_installing,
	_install_ready,
	_install_failed,
};

static struct
{
	SDL_Mutex *lock;
	int phase;
	unsigned long long received, total;
	char version[sizeof(updater_latest_version)];
	char error[512];
	char start[1200];
} updater_background;

/* whether the install running is the background's (its download reports to
updater_background, not to a window) */
static int updater_in_background;

/* whether the background install is ready, waiting for the game to end
(a game in progress: a host's would end for everyone) */
static int updater_background_waiting;
/* (set with _install_ready: whether updater_poll has anything to finish) */
static SDL_AtomicInt updater_background_ready;

int updater_gamescope(void);
static int updater_background_busy(void);

/* the zip of updater_platform's download, by its name before the list:
chupathingyce-<platform>-<release|debug>.zip */
static const char *updater_asset(void)
{
	static char name[96];

	snprintf(name, sizeof(name), "chupathingyce-%s-" HALO_BUILD_FLAVOR ".zip", updater_platform);
	return name;
}

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

#ifdef HALO_PORT_UPDATE_SCRIPT
	/* (a console port that starts the game from a script, which an update
	replaces too) */
	if (!strcmp(name, HALO_PORT_UPDATE_SCRIPT))
		return 1;
#endif
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

/* whether version is newer than this build's (update_version_compare: by
its numbers, then its hotfix letter, a version without one first, 0.7.5,
then 0.7.5a, 0.7.5b; a release before none of its test builds and
nightlies, which are in the order they were built; never one that is not of
that shape) */
static int updater_newer(const char *version)
{
	return update_version_newer(version, HALO_VERSION);
}

/* the lock, made on the game's thread before the first check starts */
static void updater_make_lock(void)
{
	if (!updater_lock)
		updater_lock = SDL_CreateMutex();
}

static void updater_locked(int lock)
{
	if (updater_lock)
	{
		if (lock)
			SDL_LockMutex(updater_lock);
		else
			SDL_UnlockMutex(updater_lock);
	}
}

/* the channel the player chose (update.channel; this build's own by
default: port_config.c), and on a build without channels (a console port's)
the release channel */
static int updater_chosen_channel(void)
{
	const char *chosen = config_string("update.channel");
	int channel;

#ifdef HALO_PORT_UPDATE_PLATFORM
	return _channel_release;
#endif
	for (channel = 0; channel < NUMBER_OF_CHANNELS; channel++)
	{
		if (!strcmp(chosen, updater_channel_names[channel]))
			return channel;
	}
	return _channel_release;
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
#if defined(HALO_PORT_UPDATE_PLATFORM) && !defined(HALO_PORT_UPDATE_API)
	/* (a port's download on the game's releases: a release counts only once
	its assets name the port's zip, so a release made before the port's
	download is added is not offered) */
	if (tag && !strstr(text, "\"" UPDATE_ASSET "\""))
	{
		platform_log("update: the latest release has no " UPDATE_ASSET " yet");
		tag = NULL;
	}
#endif
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
				/* (the latest release's tag is a release's: never a test
				build's or a nightly's version, whoever marked it latest) */
				if (found && !update_version_release(version))
				{
					platform_log("update: the latest release's tag v%s is not a release's version: not used", version);
					found = 0;
				}
			}
		}
	}
	SDL_free(text);
	return found;
}

/* a channel's feed (testing or nightly: update_manifest.h): its list and
its signature downloaded, the signature checked (by a release key or the
prerelease key: update_signature_check_feed; never unsigned, whatever the
build's keys), and the list's version, when it is the channel's, into
version; the checked list into *list (SDL_free it). 0 if the feed is not
there or not to be used */
static int updater_feed(int channel, char *version, size_t size, char **list, size_t *list_size)
{
	const char *name = updater_channel_names[channel];
	char path[1200], signature_path[1200], url[512], file[64], reason[256] = "";
	size_t length = 0, signature_length = 0;
	char *text = NULL, *signature = NULL;
	int found = 0;

	*list = NULL;
	*list_size = 0;
	snprintf(file, sizeof(file), "update-%s.json", name);
	updater_path(path, sizeof(path), file);
	snprintf(file, sizeof(file), "update-%s.json.sig", name);
	updater_path(signature_path, sizeof(signature_path), file);
	snprintf(file, sizeof(file), "%s/" UPDATE_MANIFEST, name);
	updater_url(url, sizeof(url), 0, file);
	if (!update_download(url, path, MAXIMUM_CHECK_SIZE, NULL, NULL, reason, sizeof(reason)))
	{
		platform_log("update: could not read the %s channel's list (%s)", name, reason);
		return 0;
	}
	text = SDL_LoadFile(path, &length);
	update_delete_file(path);
	snprintf(file, sizeof(file), "%s/" UPDATE_MANIFEST ".sig", name);
	updater_url(url, sizeof(url), 0, file);
	if (!text || !update_download(url, signature_path, MAXIMUM_SIGNATURE_SIZE, NULL, NULL, reason, sizeof(reason)))
	{
		platform_log("update: the %s channel's list has no signature (%s): not used", name, reason);
		goto done;
	}
	signature = SDL_LoadFile(signature_path, &signature_length);
	update_delete_file(signature_path);
	if (!update_signature_check_feed((const unsigned char *)text, length, signature, signature ? signature_length : 0,
		reason, sizeof(reason)))
	{
		platform_log("update: the %s channel's list was not used: %s", name, reason);
		goto done;
	}
	if (update_manifest_about(text, length, name, version, size, reason, sizeof(reason)) != _update_manifest_found ||
		!zip_name_allowed(version))
	{
		platform_log("update: the %s channel's list was not used: %s", name, reason[0] ? reason : "its version");
		goto done;
	}
	*list = text;
	*list_size = length;
	text = NULL;
	found = 1;

done:
	SDL_free(signature);
	SDL_free(text);
	return found;
}

/* the last check's answer, for the setup screen's Updates: 1 a newer
version, 0 this is the latest, -1 none (no answer) */
static SDL_AtomicInt updater_answer;

static int SDLCALL updater_check_thread(void *context)
{
	char latest[sizeof(updater_latest_version)];
	char candidate[sizeof(updater_latest_version)];
	int found, channel, chosen = updater_chosen_channel(), best = _channel_release, answer, state;
	/* (a channel whose list could not be read or used: nothing newer there
	can be known) */
	int unchecked = -1;
	char *list = NULL, *best_list = NULL, *old_list;
	size_t list_size = 0, best_size = 0;

	(void)context;
	found = updater_latest_release(latest, sizeof(latest));
	/* (Testing and Nightly: each feed more stable than the chosen one too,
	the newest of them all offered) */
	if (chosen != _channel_release)
		platform_log("update: the %s channel", updater_channel_names[chosen]);
	for (channel = _channel_testing; channel <= chosen; channel++)
	{
		int comparable = 1, order;

		if (!updater_feed(channel, candidate, sizeof(candidate), &list, &list_size))
		{
			unchecked = channel;
			continue;
		}
		/* (a latest release whose tag is not a version is never offered:
		the feed's build goes before it) */
		order = found ? update_version_compare(candidate, latest, &comparable) : 1;
		if (!found || !comparable || order > 0)
		{
			snprintf(latest, sizeof(latest), "%s", candidate);
			SDL_free(best_list);
			best_list = list;
			best_size = list_size;
			best = channel;
			found = 1;
		}
		else
		{
			SDL_free(list);
		}
		list = NULL;
	}
	if (best == _channel_release)
	{
		SDL_free(best_list);
		best_list = NULL;
		best_size = 0;
	}
	/* (with nothing newer found, an answer only when every channel was
	checked: "this is the latest" only then) */
	answer = !found ? -1 : updater_newer(latest) ? 1 : unchecked >= 0 ? -1 : 0;
	if (found && updater_newer(latest))
	{
		platform_log("update: version %s is available (this is %s; %s)", latest, HALO_VERSION,
			best == _channel_release ? "the latest release" : best == _channel_testing ? "the testing channel" :
			"the nightly channel");
		state = _updater_available;
	}
	else if (found && updater_offer_64bit && best == _channel_release && !strcmp(latest, HALO_VERSION))
	{
		/* (no newer version, but this one's 64-bit build to offer) */
		platform_log("update: this is the latest version (%s); its 64-bit build is offered", HALO_VERSION);
		state = _updater_available;
	}
	else if (!found)
	{
		/* (neither the latest release nor a channel's list was read) */
		platform_log("update: could not check for a newer version (this is %s)", HALO_VERSION);
		state = _updater_handled;
	}
	else if (unchecked >= 0)
	{
		platform_log("update: could not check the %s channel; nothing newer elsewhere (this is %s)",
			updater_channel_names[unchecked], HALO_VERSION);
		state = _updater_handled;
	}
	else
	{
		platform_log("update: this is the latest version (%s)", HALO_VERSION);
		state = _updater_handled;
	}
	/* (published all at once: an install reads them only under the lock) */
	updater_locked(1);
	old_list = updater_feed_list;
	updater_feed_list = best_list;
	updater_feed_size = best_size;
	updater_feed_tag[0] = 0;
	updater_latest_channel = best;
	if (state == _updater_available)
		snprintf(updater_latest_version, sizeof(updater_latest_version), "%s", latest);
	SDL_SetAtomicInt(&updater_answer, answer);
	SDL_SetAtomicInt(&updater_state, state);
	updater_locked(0);
	SDL_free(old_list);
	return 0;
}

/* the check's results, copied for an install (updater_find_file,
updater_page): 0 while a check runs, which an install never races */
static int updater_take_results(void)
{
	int taken = 0;

	updater_locked(1);
	if (SDL_GetAtomicInt(&updater_state) != _updater_checking)
	{
		SDL_free(updater_install_list);
		updater_install_list = NULL;
		updater_install_size = 0;
		updater_install_channel = updater_latest_channel;
		snprintf(updater_install_version, sizeof(updater_install_version), "%s", updater_latest_version);
		if (updater_feed_list)
		{
			updater_install_list = SDL_malloc(updater_feed_size + 1);
			if (updater_install_list)
			{
				memcpy(updater_install_list, updater_feed_list, updater_feed_size);
				updater_install_list[updater_feed_size] = 0;
				updater_install_size = updater_feed_size;
			}
		}
		taken = !updater_feed_list || updater_install_list;
	}
	updater_locked(0);
	return taken;
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

/* (the background install's download: how far it has got, for its page) */
static void updater_background_progress(void *context, unsigned long long received, unsigned long long total)
{
	(void)context;
	SDL_LockMutex(updater_background.lock);
	updater_background.received = received;
	updater_background.total = total;
	SDL_UnlockMutex(updater_background.lock);
}

/* a download staged last time and left (the player quit while the update
waited for the game to end): reused when it is still the release list's
file, its size and SHA-256 the list's (#1017); the list's signature checked
already (updater_find_file). An unlisted download is never reused */
static int updater_reusable(const char *zip_path)
{
	char digest[UPDATE_SHA256_TEXT_SIZE];
	size_t size = 0;
	unsigned char *data = updater_file.listed ? SDL_LoadFile(zip_path, &size) : NULL;
	int reusable = 0;

	if (data && size == updater_file.size && size <= MAXIMUM_UPDATE_SIZE)
	{
		update_sha256(data, size, digest);
		reusable = !strcmp(digest, updater_file.sha256);
	}
	SDL_free(data);
	if (reusable)
		platform_log("update: the download staged last time is the release list's file: reused, not downloaded again");
	else if (data)
		update_delete_file(zip_path);
	return reusable;
}

/* downloads the release's zip, showing how far it has got in a window of its
own (drawn in software, clear of the game's OpenGL); 1 when it is there. The
background install's (updater_in_background) downloads on its own thread
and tells its page instead; under gamescope (the Steam Deck's Game Mode) no
second window is opened, as gamescope shows one window of the game's */
static int updater_download_zip(const char *zip_path, char *error, size_t error_size)
{
	static struct updater_download download;
	SDL_Window *window;
	SDL_Renderer *renderer = NULL;
	SDL_Thread *thread;
	int finished = 0;
#ifdef HALO_CONSOLE_SETUP_SCREENS
	void *screen;
#endif

	memset(&download, 0, sizeof(download));
	{
		char path[400];

		snprintf(path, sizeof(path), "%s/%s", updater_file.tag, updater_file.name);
		updater_url(download.url, sizeof(download.url), 0, path);
	}
	snprintf(download.zip_path, sizeof(download.zip_path), "%s", zip_path);
	if (updater_in_background)
	{
		char reason[512] = "";
		int succeeded = update_download(download.url, download.zip_path,
			updater_file.listed ? updater_file.size : MAXIMUM_UPDATE_SIZE, updater_background_progress, NULL, reason,
			sizeof(reason));

		if (!succeeded)
			snprintf(error, error_size, "%s", reason);
		else
		{
			/* (checked and unpacked next: the page says it installs) */
			SDL_LockMutex(updater_background.lock);
			updater_background.phase = _install_installing;
			SDL_UnlockMutex(updater_background.lock);
		}
		return succeeded;
	}
	download.lock = SDL_CreateMutex();
	thread = SDL_CreateThread(updater_download_thread, "update download", &download);
	if (!thread)
	{
		snprintf(error, error_size, "could not start the download");
		return 0;
	}
#ifdef HALO_CONSOLE_SETUP_SCREENS
	window = NULL;
	screen = console_setup_screen_open();
#else
	window = updater_gamescope() ? NULL : SDL_CreateWindow("Chupathingy", 640, 150, 0);
	if (window)
		renderer = SDL_CreateRenderer(window, SDL_SOFTWARE_RENDERER);
#endif
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
#ifdef HALO_CONSOLE_SETUP_SCREENS
		if (screen)
		{
			char line[160];

			snprintf(line, sizeof(line), "version %s  (%llu of %llu MB)", updater_install_version, received >> 20,
				total >> 20);
			console_setup_screen_progress(screen, total ? (float)((double)received / (double)total) : 0.0f,
				"Downloading the update...", line);
		}
#else
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
			snprintf(line, sizeof(line), "version %s  (%llu of %llu MB)", updater_install_version, received >> 20,
				total >> 20);
			SDL_RenderDebugText(renderer, 20.0f, 70.0f, line);
			SDL_SetRenderDrawColor(renderer, 60, 66, 72, 255);
			SDL_RenderFillRect(renderer, &bar);
			bar.w *= fraction;
			SDL_SetRenderDrawColor(renderer, 90, 160, 90, 255);
			SDL_RenderFillRect(renderer, &bar);
			SDL_RenderPresent(renderer);
		}
#endif
		SDL_Delay(16);
	}
	SDL_WaitThread(thread, NULL);
#ifdef HALO_CONSOLE_SETUP_SCREENS
	console_setup_screen_close(screen);
#endif
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
from the copy that was checked. A listed zip that unpacked is kept in the
update folder until the swap, for a next start to reuse (updater_reusable);
an unlisted one, or one that failed, is deleted */
static int updater_unpack(const char *zip_path, char names[][256], int *executable, int *name_count, char *error,
	size_t error_size)
{
	char signature_path[1200];
	size_t zip_size = 0, signature_size = 0;
	unsigned char *zip = SDL_LoadFile(zip_path, &zip_size);
	char *signature = NULL;
	SDL_IOStream *stream;
	int succeeded = 0;

	if (!updater_file.listed)
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
	else if (!update_version_release(updater_install_version))
	{
		/* (a zip's own .sig is a release's alone: a test build's or a
		nightly's comes only with its feed's list) */
		snprintf(error, error_size, "the download was not installed: version %s is not a release's",
			updater_install_version);
		goto done;
	}
	else if (update_signature_required())
	{
		char url[512], reason[256] = "";

		char signature_name[120];
		char path[200];

		snprintf(signature_name, sizeof(signature_name), "%s.sig", updater_asset());
		updater_partial_path(signature_path, sizeof(signature_path), signature_name);
		snprintf(path, sizeof(path), "v%s/%s", updater_install_version, signature_name);
		updater_url(url, sizeof(url), 0, path);
		if (!update_download(url, signature_path, MAXIMUM_SIGNATURE_SIZE, NULL, NULL, reason, sizeof(reason)))
		{
			snprintf(error, error_size, "the new version's signature could not be downloaded (%s)", reason);
			goto done;
		}
		signature = SDL_LoadFile(signature_path, &signature_size);
		update_delete_file(signature_path);
		if (!update_signature_check(zip, zip_size, updater_asset(), updater_install_version, signature,
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
	if (!succeeded)
		update_delete_file(zip_path);
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
	/* (a feed's build: from the list checked when it was found, which must
	name this platform's file; there is no zip by an older name to fall back
	on) */
	if (updater_install_channel != _channel_release)
	{
		const char *channel = updater_channel_names[updater_install_channel];

		if (!updater_install_list)
		{
			snprintf(error, error_size, "the %s channel's list is gone: look again at the next start", channel);
			return 0;
		}
		result = update_manifest_find_channel(updater_install_list, updater_install_size, channel, updater_install_version,
			updater_platform, HALO_BUILD_FLAVOR, updater_appimage ? "appimage" : "zip", updater_file.tag,
			sizeof(updater_file.tag), updater_file.name, sizeof(updater_file.name), &updater_file.size,
			updater_file.sha256, sizeof(updater_file.sha256), updater_file.main_name, sizeof(updater_file.main_name),
			reason, sizeof(reason));
		if (result != _update_manifest_found)
		{
			snprintf(error, error_size, "the %s channel's list has no update for this build: %s", channel, reason);
			return 0;
		}
		if (!updater_file.size || updater_file.size > MAXIMUM_UPDATE_SIZE)
		{
			snprintf(error, error_size, "the %s channel's list gives %s a size this build does not install (%llu bytes)",
				channel, updater_file.name, updater_file.size);
			return 0;
		}
		updater_file.listed = 1;
		platform_log("update: the %s channel's list names %s (%s, %llu bytes%s%s)", channel, updater_file.name,
			updater_file.tag, updater_file.size, updater_file.main_name[0] ? ", main " : "", updater_file.main_name);
		return 1;
	}
	/* (the release path: a release's version only, never a test build's or a
	nightly's, with the list or with its zip's own .sig) */
	if (!update_version_release(updater_install_version))
	{
		snprintf(error, error_size, "version %s is not a release's: not installed from the release path",
			updater_install_version);
		return 0;
	}
	snprintf(updater_file.tag, sizeof(updater_file.tag), "v%s", updater_install_version);
	snprintf(updater_file.name, sizeof(updater_file.name), "%s", updater_asset());
	updater_partial_path(manifest_path, sizeof(manifest_path), UPDATE_MANIFEST);
	updater_partial_path(signature_path, sizeof(signature_path), UPDATE_MANIFEST ".sig");
	snprintf(path, sizeof(path), "v%s/" UPDATE_MANIFEST, updater_install_version);
	updater_url(url, sizeof(url), 0, path);
	if (!update_download(url, manifest_path, MAXIMUM_CHECK_SIZE, NULL, NULL, reason, sizeof(reason)))
	{
		/* (an AppImage updates only from the list: the zip is not one) */
		if (updater_appimage)
		{
			snprintf(error, error_size, "this release has no list of update files (%s), and an AppImage updates "
				"only from one: download the new AppImage from the release's page", reason);
			return 0;
		}
		/* (its zip's own signature is checked instead) */
		platform_log("update: no list of update files (%s): downloading %s", reason, updater_asset());
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
		snprintf(path, sizeof(path), "v%s/" UPDATE_MANIFEST ".sig", updater_install_version);
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
	result = update_manifest_find(manifest, manifest_size, updater_install_version, updater_platform, HALO_BUILD_FLAVOR,
		updater_appimage ? "appimage" : "zip", updater_file.tag, sizeof(updater_file.tag), updater_file.name, sizeof(updater_file.name),
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
	else if (result == _update_manifest_missing && updater_appimage)
	{
		snprintf(error, error_size, "this release has no AppImage update (%s): download the new AppImage from the "
			"release's page", reason);
	}
	else if (result == _update_manifest_missing)
	{
		/* (updater_file as it was: the zip by its old name) */
		platform_log("update: %s: downloading %s", reason, updater_asset());
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

/* the downloaded AppImage, checked against the release's list (its size and
SHA-256), and written again from the copy that was checked, ready to be put
in place (updater_swap); 1 when it is there */
static int updater_check_appimage(const char *download_path, char *error, size_t error_size)
{
	char digest[UPDATE_SHA256_TEXT_SIZE];
	size_t size = 0;
	unsigned char *data = SDL_LoadFile(download_path, &size);
	int succeeded = 0;

	update_delete_file(download_path);
	if (!data || size > MAXIMUM_UPDATE_SIZE)
	{
		snprintf(error, error_size, "could not read the download");
		goto done;
	}
	/* (always from the list, whose signature checked: updater_find_file) */
	if (!updater_file.listed || size != updater_file.size)
	{
		snprintf(error, error_size, "the download was not installed: it is %llu bytes, not the list's %llu",
			(unsigned long long)size, updater_file.size);
		goto done;
	}
	update_sha256(data, size, digest);
	if (strcmp(digest, updater_file.sha256))
	{
		snprintf(error, error_size, "the download was not installed: its SHA-256 is not the list's");
		goto done;
	}
	platform_log("update: the download's SHA-256 is the release list's");
	if (!SDL_SaveFile(download_path, data, size))
	{
		snprintf(error, error_size, "could not write the new AppImage beside the old one");
		goto done;
	}
	succeeded = 1;

done:
	SDL_free(data);
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

/* what is downloaded, checked and unpacked, waiting to be put in place
(updater_fetch, then updater_swap): the AppImage's file, or the zip's files
in the update folder */
static struct
{
	char download[1200];
	char names[MAXIMUM_UPDATE_FILES][256];
	int executable[MAXIMUM_UPDATE_FILES];
	int count;
} updater_staged;

/* downloads, checks and unpacks the new build into the update folder,
nothing of the game's changed yet: 1 when it is ready for updater_swap, else
0 and why in error. Nothing is shown here but the download's window
(updater_download_zip). It can run on a thread of its own */
static int updater_fetch(char *error, size_t error_size)
{
	char partial[1200];
	/* (the check's results, copied once: the version, the channel and the
	feed's list stay these for the whole install) */
	int taken = updater_take_results();

	memset(&updater_staged, 0, sizeof(updater_staged));
	updater_path(partial, sizeof(partial), UPDATE_DIRECTORY);
	updater_partial_path(updater_staged.download, sizeof(updater_staged.download),
		updater_appimage ? UPDATE_APPIMAGE_ASSET : updater_asset());
	if (taken)
		platform_log("update: downloading version %s", updater_install_version);
	if (!taken)
	{
		snprintf(error, error_size, "a check for updates is running: try again when it is done");
		return 0;
	}
	if (!update_make_directory(partial))
	{
		snprintf(error, error_size, "could not make %s (is the %s folder read-only?)", partial,
			updater_appimage ? "AppImage's" : "game's");
		return 0;
	}
	if (updater_find_file(error, error_size) &&
		(updater_reusable(updater_staged.download) || updater_download_zip(updater_staged.download, error, error_size)) &&
		(updater_appimage ? updater_check_appimage(updater_staged.download, error, error_size) :
		updater_unpack(updater_staged.download, updater_staged.names, updater_staged.executable,
			&updater_staged.count, error, error_size)))
	{
		return 1;
	}
	update_delete_file(updater_staged.download);
	update_delete_file(partial);
	return 0;
}

/* what updater_fetch made ready put in place of the running game's files:
the AppImage file this runs from (which becomes <name>.old), executable as
it was, or the zip's files (each old one <name>.old). 1 when it is there,
start then the program to start (the AppImage file by the name it had, or
the zip's game); else 0, the game as it was, and why in error. On the
game's thread, so the files never change under a game that is quitting */
static int updater_swap(char *start, size_t start_size, char *error, size_t error_size)
{
	char partial[1200];

	updater_path(partial, sizeof(partial), UPDATE_DIRECTORY);
	if (updater_appimage)
	{
		char old_path[1300];

		snprintf(old_path, sizeof(old_path), "%s.old", updater_executable);
		if (!update_replace_file(updater_executable, updater_staged.download, old_path))
		{
			update_delete_file(updater_staged.download);
			update_delete_file(partial);
			snprintf(error, error_size, "could not replace %s", updater_executable);
			return 0;
		}
		update_delete_file(partial);
		snprintf(start, start_size, "%s", updater_executable);
		return 1;
	}
	if (!updater_replace_files(updater_staged.names, updater_staged.executable, updater_staged.count, error,
		error_size))
	{
		return 0;
	}
	/* (the zip kept for a reuse: not needed once the files are in place) */
	update_delete_file(updater_staged.download);
	update_delete_file(partial);
	updater_new_executable(updater_staged.names, updater_staged.count, start, start_size);
	return 1;
}

/* downloads, unpacks and puts in place the new build: updater_fetch, then
updater_swap */
static int updater_put_in_place(char *start, size_t start_size, char *error, size_t error_size)
{
	return updater_fetch(error, error_size) && updater_swap(start, start_size, error, error_size);
}

/* the new version, in place, started in this one's place (start: the
program updater_put_in_place gave): on Linux this very process becomes it
as it exits (update_start_in_place: the same process, so Steam and gamescope
keep the game, in Game Mode too), elsewhere it is started and this one
quits. Returns only if it cannot be started, with why in error */
static void updater_restart(const char *start, char *error, size_t error_size)
{
	char reason[256] = "";

	platform_log("update: starting version %s (%s)", updater_install_version,
		strrchr(start, PATH_SEPARATOR[0]) ? strrchr(start, PATH_SEPARATOR[0]) + 1 : start);
#ifdef HALO_PORT_UPDATE_RESTART_EXIT_CODE
	/* (the port's script starts the new version itself when the game exits
	with this code; an AppImage is no port's) */
	if (!updater_appimage)
		exit(HALO_PORT_UPDATE_RESTART_EXIT_CODE);
#endif
#ifndef HALO_PORT_UPDATE_PLATFORM
	/* (checked before this one goes: a folder that runs no programs, noexec,
	would leave the player with no game at all. A console port's system side
	is its own, and starts the new version as before) */
	if (!update_can_start(start, reason, (int)sizeof(reason)))
	{
		snprintf(error, error_size, "the new version is in place, but %s: start it again", reason);
		return;
	}
	if (update_start_in_place(start))
	{
		platform_log("update: restarting as version %s in this same process as the game exits (should it end "
			"first, the new version is started for it)", updater_install_version);
		exit(EXIT_SUCCESS);
	}
#else
	(void)reason;
#endif
	if (update_launch(start))
		exit(EXIT_SUCCESS);
	snprintf(error, error_size, "the new version is in place, but could not be started: start it again");
}

/* downloads, unpacks and puts in place the new build, and starts it; returns
only if something failed, which is shown in a dialog (never under gamescope,
where a dialog crashes the game: the log has it, and the page that began the
install shows it, updater_install_status) */
static void updater_update(void)
{
	char start[1200], error[512] = "";

	if (updater_put_in_place(start, sizeof(start), error, sizeof(error)))
		updater_restart(start, error, sizeof(error));
	platform_log("update: failed: %s", error);
#ifdef HALO_CONSOLE_SETUP_SCREENS
	console_setup_notice("The update failed, and the game goes on as it was:", error);
#else
	if (updater_gamescope())
	{
		platform_log("update: not showing the failure in a dialog under gamescope (Steam Deck Game Mode)");
	}
	else
	{
		char message[800];

		snprintf(message, sizeof(message), "The update failed:\n\n%s", error);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Chupathingy", message, NULL);
	}
#endif
}

/* (the background install's thread: updater_fetch, the new build ready to
be put in place by the game's thread, updater_install_finish) */
static int SDLCALL updater_background_thread(void *context)
{
	char error[512] = "";
	int succeeded;

	(void)context;
	succeeded = updater_fetch(error, sizeof(error));
	if (!succeeded)
		platform_log("update: failed: %s", error);
	else
		platform_log("update: version %s is downloaded and checked, ready to put in place", updater_install_version);
	SDL_LockMutex(updater_background.lock);
	updater_in_background = 0;
	if (succeeded)
	{
		updater_background.phase = _install_ready;
		SDL_SetAtomicInt(&updater_background_ready, 1);
	}
	else
	{
		snprintf(updater_background.error, sizeof(updater_background.error), "%s", error);
		updater_background.phase = _install_failed;
	}
	SDL_UnlockMutex(updater_background.lock);
	return 0;
}

#ifndef HALO_CONSOLE_SETUP_SCREENS
/* whether the game may restart now: no game in progress, only the menus
(menu_functions.c: the main menu, or the setup screen before the game) */
int updater_restart_allowed(void);
#endif


/* on the game's thread (updater_poll every frame, the setup screen): the
background install's new build, once it is ready, put in place and started,
or why not kept for its page. Not while a game is in progress (allowed 0):
it waits, the page and the version line saying it restarts when the player
leaves the game */
static void updater_install_finish(int allowed)
{
#ifndef HALO_CONSOLE_SETUP_SCREENS
	char start[1200], error[512] = "";

	if (!updater_background.lock)
		return;
	SDL_LockMutex(updater_background.lock);
	if (updater_background.phase != _install_ready)
	{
		SDL_UnlockMutex(updater_background.lock);
		return;
	}
	if (!allowed)
	{
		SDL_UnlockMutex(updater_background.lock);
		if (!updater_background_waiting)
		{
			updater_background_waiting = 1;
			platform_log("update: version %s is ready; it is put in place when the game in progress ends",
				updater_install_version);
			build_identity_watermark_note(UPDATER_WAITING_NOTE);
		}
		return;
	}
	updater_background.phase = _install_installing;
	SDL_UnlockMutex(updater_background.lock);
	SDL_SetAtomicInt(&updater_background_ready, 0);
	updater_background_waiting = 0;
	/* (here, on the game's thread, the files put in place; returns only if
	the new version could not be put in place or started) */
	if (updater_swap(start, sizeof(start), error, sizeof(error)))
		updater_restart(start, error, sizeof(error));
	platform_log("update: failed: %s", error);
	SDL_LockMutex(updater_background.lock);
	snprintf(updater_background.error, sizeof(updater_background.error), "%s", error);
	updater_background.phase = _install_failed;
	SDL_UnlockMutex(updater_background.lock);
#else
	(void)allowed;
#endif
}

/* the setup screen's (launcher.c), every frame: no game runs before it */
void updater_install_restart(void)
{
	updater_install_finish(1);
}

/* the game's program from before the rename, beside the renamed one: halo
beside chupathingy, halo.exe beside chupathingy.exe (rename_stub.h), removed,
unless it is the small stub that starts this one, which stays for the
shortcuts and scripts that start halo */
static void updater_remove_old_program(void)
{
	const char *running = strrchr(updater_executable, PATH_SEPARATOR[0]);
	const char *old_name = rename_stub_old_name(running ? running + 1 : updater_executable);
	unsigned char data[RENAME_STUB_MAXIMUM_SIZE > 65536 ? 65536 : RENAME_STUB_MAXIMUM_SIZE];
	char path[1300];
	SDL_PathInfo info;
	SDL_IOStream *file;
	size_t size = 0;
	Sint64 file_size;

	if (!old_name)
		return;
	updater_path(path, sizeof(path), old_name);
	if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE)
		return;
	file = SDL_IOFromFile(path, "rb");
	if (!file)
		return;
	file_size = SDL_GetIOSize(file);
	if (file_size >= 0 && (Uint64)file_size <= RENAME_STUB_MAXIMUM_SIZE)
	{
		/* (a stub has its mark near its start, or, a Windows one, in its
		data: read it whole, in pieces, and look at each with the last
		mark's length of the one before it) */
		size_t keep = sizeof(RENAME_STUB_MARK) - 2, got;

		while ((got = SDL_ReadIO(file, data + size, sizeof(data) - size)) > 0)
		{
			size += got;
			if (rename_stub_is_stub(data, size, (unsigned long long)file_size))
			{
				SDL_CloseIO(file);
				return;
			}
			if (size > keep)
			{
				memmove(data, data + size - keep, keep);
				size = keep;
			}
		}
	}
	SDL_CloseIO(file);
	platform_log("update: removing %s, the game from before its rename to %s", old_name,
		running ? running + 1 : updater_executable);
	update_delete_file(path);
}

/* the separate license files the downloads carried before 0.9.1, which
THIRD-PARTY-NOTICES.txt holds now: an update leaves them where they were, as
it leaves every file the new zip lacks. Only these names, only files (never a
folder), only in the game's own folder, and only once that folder has the new
LICENSE.txt and THIRD-PARTY-NOTICES.txt; each one removed is logged */
static const char *const updater_old_license_files[] =
{
	"SDL3-LICENSE.txt", "mbedtls-LICENSE.txt", "miniupnpc-LICENSE.txt", "Overpass-OFL.txt",
	"CyborgInterfaceGothic-OFL.txt", "ChupathingyTitles-OFL.txt", "NotoSans-OFL.txt",
	"Kenney-Input-Prompts-CC0.txt", "expat-COPYING.txt", "lzma-sdk-LICENSE.txt", "zstd-LICENSE.txt",
	"qrcodegen-LICENSE.txt", "steam-link-MIT.txt", "bcdec-LICENSE.txt", "bgfx-LICENSE.txt", "bx-LICENSE.txt",
	"bimg-LICENSE.txt", "glslang-LICENSE.txt", "SPIRV-Cross-LICENSE.txt",
};

static int updater_is_file(const char *path)
{
	SDL_PathInfo info;

	return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

static void updater_remove_old_license_files(void)
{
	char path[1300];
	size_t index;

	updater_path(path, sizeof(path), "LICENSE.txt");
	if (!updater_is_file(path))
		return;
	updater_path(path, sizeof(path), "THIRD-PARTY-NOTICES.txt");
	if (!updater_is_file(path))
		return;
	for (index = 0; index < sizeof(updater_old_license_files) / sizeof(*updater_old_license_files); index++)
	{
		updater_path(path, sizeof(path), updater_old_license_files[index]);
		if (!updater_is_file(path))
			continue;
		update_delete_file(path);
		if (updater_is_file(path))
			platform_log("update: could not remove %s (now in THIRD-PARTY-NOTICES.txt)", updater_old_license_files[index]);
		else
			platform_log("update: removed %s (now in THIRD-PARTY-NOTICES.txt)", updater_old_license_files[index]);
	}
}

/* the files a previous update left behind */
static void updater_clean_up(void)
{
	static const char *const names[] =
	{
		"halo.old", "halo.exe.old", "chupathingy.old", "chupathingy.exe.old", "SDL3.dll.old", "libSDL3.so.0.old",
		"SDL3-LICENSE.txt.old", "mbedtls-LICENSE.txt.old", "LICENSE.txt.old", "THIRD-PARTY-NOTICES.txt.old",
	};
	char path[1300], signature_name[120];
	size_t index;

	/* (an AppImage's folder is the player's, not the game's: only the
	AppImage's own old file) */
	if (updater_appimage)
	{
		snprintf(path, sizeof(path), "%s.old", updater_executable);
		update_delete_file(path);
		updater_partial_path(path, sizeof(path), UPDATE_APPIMAGE_ASSET);
		update_delete_file(path);
	}
	else
	{
		for (index = 0; index < sizeof(names) / sizeof(*names); index++)
		{
			updater_path(path, sizeof(path), names[index]);
			update_delete_file(path);
		}
		updater_remove_old_program();
		updater_remove_old_license_files();
	}
	updater_partial_path(path, sizeof(path), updater_asset());
	update_delete_file(path);
	snprintf(signature_name, sizeof(signature_name), "%s.sig", updater_asset());
	updater_partial_path(path, sizeof(path), signature_name);
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
	int packaged = platform_packaged();

#ifdef HALO_CONSOLE_SETUP_SCREENS
	/* (asked before the game's window opens, as the console has no dialogs:
	updater_console_check, from the port's console_setup_after_data) */
	return;
#endif

	/* (the macOS application only offers the download page, and a packaged
	install the package manager: no files of their own to find or clean up) */
	if (UPDATER_DOWNLOAD_PAGE_ONLY || packaged)
	{
		/* (the check's answer is kept in the save root while it is read) */
		snprintf(updater_directory, sizeof(updater_directory), "%s", platform_save_root());
	}
	else
	{
		/* (an AppImage: its file is the one replaced) */
		updater_appimage = update_appimage_path(updater_executable, sizeof(updater_executable));
		if (!updater_appimage && !update_executable_path(updater_executable, sizeof(updater_executable)))
			return;
		if (updater_appimage)
			platform_log("update: running as an AppImage (%s)", updater_executable);
		snprintf(updater_directory, sizeof(updater_directory), "%s", updater_executable);
		slash = strrchr(updater_directory, PATH_SEPARATOR[0]);
		if (!slash)
			return;
		*slash = 0;
		updater_clean_up();
	}
	/* (not for builds other than a release's, the player's no, or runs nobody
	is watching, but for a test with its answer; a packaged install's
	update.auto is off, and its no is the one the file or the environment
	gave: config_packaged_update_check) */
	/* (a release's build with no update key, all zero in update_key.h, would
	install unsigned updates: it never looks for any. configure.py refuses
	to make one; this is for a build made some other way) */
	if (UPDATER_KEYLESS)
	{
		platform_log("update: this release build has no update key, so it does not look for updates");
		return;
	}
	if (!UPDATER_LOOKS ||
		!(packaged ? config_packaged_update_check() : config_boolean("update.auto")) ||
		(!config_string("debug.update_answer")[0] && (config_boolean("debug.hidden_window") ||
			config_real("debug.exit_after") > 0.0 || config_string("debug.network_test")[0])))
	{
		return;
	}
#if UPDATER_OFFER_64BIT
	/* (asked once, with the check's answer: updater_poll) */
	updater_offer_64bit = config_boolean("update.offer_64bit") && update_windows_runs_64bit();
	if (updater_offer_64bit)
		platform_log("update: this is the 32-bit build on 64-bit Windows: the 64-bit build will be offered");
#endif
	updater_make_lock();
	SDL_SetAtomicInt(&updater_state, _updater_checking);
	{
		SDL_Thread *thread = SDL_CreateThread(updater_check_thread, "update check", NULL);

		if (thread)
			SDL_DetachThread(thread);
		else
			SDL_SetAtomicInt(&updater_state, _updater_handled);
	}
}

#ifdef HALO_CONSOLE_SETUP_SCREENS
/* at start-up, before the game's window (the port's console_setup_after_data):
a new version looked for, for as long as a player would not notice (no network
must not hold the game up), and the player asked on the port's screen, with
the pad. Downloaded, checked and put in place as on the desktop */
void updater_console_check(void)
{
	/* (a slow console's first TLS takes seconds: its certificates, one core) */
	const Uint64 patience = 8000;
	Uint64 start = SDL_GetTicks();
	const char *test_answer = config_string("debug.update_answer");
	SDL_Thread *thread;
	char *slash;
	int answer = 0;

	if (!update_executable_path(updater_executable, sizeof(updater_executable)))
		return;
	snprintf(updater_directory, sizeof(updater_directory), "%s", updater_executable);
	slash = strrchr(updater_directory, PATH_SEPARATOR[0]);
	if (!slash)
		return;
	*slash = 0;
	updater_clean_up();
	if (UPDATER_KEYLESS)
	{
		platform_log("update: this release build has no update key, so it does not look for updates");
		return;
	}
	if (!UPDATER_LOOKS || !config_boolean("update.auto"))
		return;
	updater_make_lock();
	SDL_SetAtomicInt(&updater_state, _updater_checking);
	thread = SDL_CreateThread(updater_check_thread, "update check", NULL);
	if (!thread)
	{
		SDL_SetAtomicInt(&updater_state, _updater_handled);
		return;
	}
	SDL_DetachThread(thread);
	while (SDL_GetAtomicInt(&updater_state) == _updater_checking && SDL_GetTicks() - start < patience)
		SDL_Delay(50);
	if (SDL_GetAtomicInt(&updater_state) != _updater_available)
	{
		if (SDL_GetAtomicInt(&updater_state) == _updater_checking)
			platform_log("update: no answer in time; the game starts without waiting");
		return;
	}
	SDL_SetAtomicInt(&updater_state, _updater_handled);
	if (test_answer[0])
		answer = !strcmp(test_answer, "yes") ? 1 : !strcmp(test_answer, "never") ? 2 : 0;
	else
		answer = console_setup_update_question(updater_latest_version, HALO_VERSION);
	if (answer == 1)
	{
		updater_update();
	}
	else if (answer == 2)
	{
		if (config_write_boolean("update.auto", 0))
			platform_log("update: update.auto = false written to config.toml");
	}
}
#endif

/* the newer version's download page: the latest release's, or a feed's
build's pre-release (its file's tag in the checked list: macOS) */
static const char *updater_page(void)
{
	static char page[300];

	if (updater_take_results() && updater_install_channel != _channel_release && updater_install_list)
	{
		char name[160], sha256[UPDATE_SHA256_TEXT_SIZE], main_name[160], reason[256];
		unsigned long long size = 0;

		if (update_manifest_find_channel(updater_install_list, updater_install_size,
			updater_channel_names[updater_install_channel], updater_install_version, updater_platform,
			HALO_BUILD_FLAVOR, "zip", updater_feed_tag, sizeof(updater_feed_tag), name, sizeof(name), &size, sha256,
			sizeof(sha256), main_name, sizeof(main_name), reason, sizeof(reason)) == _update_manifest_found)
		{
			snprintf(page, sizeof(page), "https://github.com/" UPDATE_REPOSITORY "/releases/tag/%s", updater_feed_tag);
			return page;
		}
	}
	return UPDATE_RELEASE_PAGE;
}

/* ---------- the setup screen's Updates (launcher.c) */

/* the check started now, whatever update.auto says: 0 when this build never
looks (not a release's) or a check is running */
int updater_check_now(void)
{
	SDL_Thread *thread;

	if (!UPDATER_LOOKS || UPDATER_KEYLESS)
		return 0;
	if (SDL_GetAtomicInt(&updater_state) == _updater_checking || updater_background_busy())
		return 0;
	if (!updater_directory[0])
		snprintf(updater_directory, sizeof(updater_directory), "%s", platform_save_root());
	updater_make_lock();
	SDL_SetAtomicInt(&updater_state, _updater_checking);
	thread = SDL_CreateThread(updater_check_thread, "update check", NULL);
	if (!thread)
	{
		SDL_SetAtomicInt(&updater_state, _updater_handled);
		return 0;
	}
	SDL_DetachThread(thread);
	return 1;
}

/* where the check is: UPDATER_CHECKING, UPDATER_NEWER (latest: its
version), UPDATER_LATEST, UPDATER_UNREACHABLE, or UPDATER_NOT_CHECKED. A
newer version shown here is not asked about again when the game starts */
int updater_check_state(char *latest, int size)
{
	int state = SDL_GetAtomicInt(&updater_state);

	if (latest && size > 0)
		latest[0] = 0;
	if (state == _updater_checking)
		return UPDATER_CHECKING;
	if (state == _updater_idle)
		return UPDATER_NOT_CHECKED;
	switch (SDL_GetAtomicInt(&updater_answer))
	{
	case 1:
		if (latest && size > 0)
			snprintf(latest, (size_t)size, "%s", updater_latest_version);
		SDL_SetAtomicInt(&updater_state, _updater_handled);
		return UPDATER_NEWER;
	case 0:
		return UPDATER_LATEST;
	default:
		return UPDATER_UNREACHABLE;
	}
}

/* the newer version installed, as Yes does: its download page on macOS,
nothing in a packaged install (0), else downloaded and started (this one
then quits) */
int updater_install(void)
{
	if (SDL_GetAtomicInt(&updater_answer) != 1 || platform_packaged() ||
		SDL_GetAtomicInt(&updater_state) == _updater_checking)
	{
		return 0;
	}
	if (UPDATER_DOWNLOAD_PAGE_ONLY)
		return SDL_OpenURL(updater_page());
	if (!updater_executable[0])
	{
		updater_appimage = update_appimage_path(updater_executable, sizeof(updater_executable));
		if (!updater_appimage && !update_executable_path(updater_executable, sizeof(updater_executable)))
			return 0;
	}
	updater_update();
	return 1;
}

/* whether an install from a page is running (no second, and no check) */
static int updater_background_busy(void)
{
	int busy;

	if (!updater_background.lock)
		return 0;
	SDL_LockMutex(updater_background.lock);
	busy = updater_background.phase == _install_downloading || updater_background.phase == _install_installing ||
		updater_background.phase == _install_ready;
	SDL_UnlockMutex(updater_background.lock);
	return busy;
}

/* whether a newer version can be installed now (latest: its version), as
updater_check_state's UPDATER_NEWER, but changing nothing: the menus' page
asks it every frame, and the start's question (updater_poll) is still
asked */
int updater_ready(char *latest, int size)
{
	int ready = SDL_GetAtomicInt(&updater_answer) == 1 && SDL_GetAtomicInt(&updater_state) != _updater_checking &&
		SDL_GetAtomicInt(&updater_state) != _updater_idle && !platform_packaged();

	if (latest && size > 0)
		snprintf(latest, (size_t)size, "%s", ready ? updater_latest_version : "");
	return ready;
}

/* the newer version installed from a page of the game's own (the setup
screen's UPDATE, the menus' INSTALL UPDATE), on a thread of its own: no
window or dialog, the page shows how it goes (updater_install_status), and
the game's thread starts the new version once it is in place. 1 when it
began (or one is running); 0 when there is nothing to install. On macOS the
download page opens instead, and a console's install is its own screens' */
int updater_install_begin(void)
{
	SDL_Thread *thread;

	if (updater_background_busy())
		return 1;
#ifdef HALO_CONSOLE_SETUP_SCREENS
	return updater_install();
#else
	if (UPDATER_DOWNLOAD_PAGE_ONLY)
		return updater_install();
	if (SDL_GetAtomicInt(&updater_answer) != 1 || platform_packaged() ||
		SDL_GetAtomicInt(&updater_state) == _updater_checking)
	{
		return 0;
	}
	if (!updater_executable[0])
	{
		updater_appimage = update_appimage_path(updater_executable, sizeof(updater_executable));
		if (!updater_appimage && !update_executable_path(updater_executable, sizeof(updater_executable)))
			return 0;
	}
	/* (asked here, the start's question is not asked as well) */
	SDL_SetAtomicInt(&updater_state, _updater_handled);
	if (!updater_background.lock)
		updater_background.lock = SDL_CreateMutex();
	if (!updater_background.lock)
		return 0;
	SDL_LockMutex(updater_background.lock);
	updater_background.phase = _install_downloading;
	updater_background.received = updater_background.total = 0;
	updater_background.error[0] = updater_background.start[0] = 0;
	snprintf(updater_background.version, sizeof(updater_background.version), "%s", updater_latest_version);
	SDL_UnlockMutex(updater_background.lock);
	updater_in_background = 1;
	platform_log("update: installing version %s from the game's own page%s", updater_latest_version,
		updater_gamescope() ? " (under gamescope: no window or dialog)" : "");
	thread = SDL_CreateThread(updater_background_thread, "update install", NULL);
	if (!thread)
	{
		updater_in_background = 0;
		SDL_LockMutex(updater_background.lock);
		snprintf(updater_background.error, sizeof(updater_background.error), "could not start the download");
		updater_background.phase = _install_failed;
		SDL_UnlockMutex(updater_background.lock);
		return 1;
	}
	SDL_DetachThread(thread);
	return 1;
#endif
}

/* where the install from a page is (UPDATER_INSTALL_*), and line, what the
page shows of it: how much is downloaded, or why it failed. On the game's
thread: an install in place is started from here (or updater_poll) */
int updater_install_status(char *line, int size)
{
	int phase;

	if (line && size > 0)
		line[0] = 0;
	if (!updater_background.lock)
		return UPDATER_INSTALL_IDLE;
	SDL_LockMutex(updater_background.lock);
	phase = updater_background.phase;
	if (line && size > 0)
	{
		if (phase == _install_downloading && updater_background.total)
		{
			snprintf(line, (size_t)size, "Downloading %s: %llu of %llu MB.", updater_background.version,
				updater_background.received >> 20, (updater_background.total + (1 << 20) - 1) >> 20);
		}
		else if (phase == _install_downloading)
			snprintf(line, (size_t)size, "Downloading %s...", updater_background.version);
		else if (phase == _install_ready && updater_background_waiting)
			snprintf(line, (size_t)size, "%s is ready. Chupathingy restarts when you leave the game.",
				updater_background.version);
		else if (phase == _install_installing || phase == _install_ready)
			snprintf(line, (size_t)size, "Installing %s. Chupathingy restarts in a moment.", updater_background.version);
		else if (phase == _install_failed)
			snprintf(line, (size_t)size, "The update failed: %s.", updater_background.error);
	}
	SDL_UnlockMutex(updater_background.lock);
	switch (phase)
	{
	case _install_downloading:
		return UPDATER_INSTALL_DOWNLOADING;
	case _install_ready:
		return updater_background_waiting ? UPDATER_INSTALL_WAITING : UPDATER_INSTALL_INSTALLING;
	case _install_installing:
		return UPDATER_INSTALL_INSTALLING;
	case _install_failed:
		return UPDATER_INSTALL_FAILED;
	default:
		return UPDATER_INSTALL_IDLE;
	}
}

/* how much the install from a page has downloaded, for a row's value */
const char *updater_install_amount(char *text, int size)
{
	unsigned long long received = 0, total = 0;

	if (updater_background.lock)
	{
		SDL_LockMutex(updater_background.lock);
		received = updater_background.received;
		total = updater_background.total;
		SDL_UnlockMutex(updater_background.lock);
	}
	if (total)
		snprintf(text, (size_t)size, "%llu / %llu MB", received >> 20, (total + (1 << 20) - 1) >> 20);
	else
		snprintf(text, (size_t)size, "Starting");
	return text;
}

/* the channel this build is on: a release's, a nightly, or a build of
one's own */
const char *updater_channel(void)
{
	if (HALO_RELEASE_BUILD)
		return "release";
#if UPDATER_PRERELEASE_BUILD
	return HALO_UPDATE_CHANNEL;
#else
	return strstr(HALO_VERSION, "nightly") ? "nightly" : "dev";
#endif
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
	char message[600];
	int answer = 0;
	int fullscreen;
	const char *test_answer = config_string("debug.update_answer");

#ifdef HALO_CONSOLE_SETUP_SCREENS
	/* (an answer too late for updater_console_check waits for the next start:
	the console has no dialogs) */
	return;
#endif
	/* (an install from a page, once it is ready, wherever the player went
	meanwhile; not during a game) */
#ifndef HALO_CONSOLE_SETUP_SCREENS
	/* (the game asked only once the install is ready) */
	if (updater_background.lock && SDL_GetAtomicInt(&updater_background_ready))
		updater_install_finish(updater_restart_allowed());
#endif
	if (SDL_GetAtomicInt(&updater_state) != _updater_available)
		return;
	SDL_SetAtomicInt(&updater_state, _updater_handled);
	/* (a packaged install: nothing asked or downloaded, whatever a test
	answers) */
	if (platform_packaged())
	{
		platform_log("update: %s is out (this is %s); a packaged install: update Chupathingy with your "
			"package manager", updater_latest_version, HALO_VERSION);
		build_identity_watermark_update(updater_latest_version, "Chupathingy with your package manager");
		return;
	}
	/* (an automated test's answer: debug.update_answer) */
	if (test_answer[0])
	{
		/* (a test does not answer the 64-bit offer: only a newer version) */
		if (!updater_newer(updater_latest_version))
			return;
		platform_log("update: answering %s (debug.update_answer)", test_answer);
		if (!strcmp(test_answer, "yes") && UPDATER_DOWNLOAD_PAGE_ONLY)
			platform_log("update: would open %s", updater_page());
		else if (!strcmp(test_answer, "yes"))
			updater_update();
		else if (!strcmp(test_answer, "never"))
			config_write_boolean("update.auto", 0);
		return;
	}
	/* (no question under gamescope, where a dialog crashes the game: the main
	menu's version line says the new version is out, and the player installs
	it from the game's own pages, Settings' Updates or the setup screen's, which
	show how it goes without a window or dialog: updater_install_begin) */
	if (updater_gamescope())
	{
		platform_log("update: %s is out (this is %s); not asking under gamescope (Steam Deck Game Mode)",
			updater_latest_version, HALO_VERSION);
		build_identity_watermark_update(updater_latest_version, UPDATER_GAMESCOPE_WHERE);
		return;
	}
	/* (a dialog cannot show above a fullscreen game) */
	fullscreen = window && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN);
	if (fullscreen)
		SDL_SetWindowFullscreen(window, false);
#if UPDATER_OFFER_64BIT
	/* the 32-bit build on 64-bit Windows: the 64-bit build offered, once.
	Yes installs it as an update would (the new version's, or this one's);
	No is written to update.offer_64bit, and a newer version is then asked
	about as usual */
	if (updater_offer_64bit)
	{
		static const SDL_MessageBoxButtonData offer_buttons[] =
		{
			{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" },
			{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Keep 32-bit" },
		};
		int newer = updater_newer(updater_latest_version);

		updater_offer_64bit = 0;
		if (newer)
		{
			snprintf(message, sizeof(message),
				"Chupathingy %s is out. You have %s.\n\n"
				"This PC runs 64-bit Windows, and you have the 32-bit build. The 64-bit build runs better here "
				"and crashes less in graphics drivers.\n\n"
				"Update to the 64-bit build? Your saves and settings stay. The game will restart.",
				updater_latest_version, HALO_VERSION);
		}
		else
		{
			snprintf(message, sizeof(message),
				"This PC runs 64-bit Windows, and you have the 32-bit build of Chupathingy. The 64-bit build "
				"runs better here and crashes less in graphics drivers.\n\n"
				"Switch to the 64-bit build? Your saves and settings stay. The game will restart.");
		}
		{
			SDL_MessageBoxData offer = { SDL_MESSAGEBOX_INFORMATION, window, "Chupathingy: 64-bit build", message,
				2, offer_buttons, NULL };

			if (!SDL_ShowMessageBox(&offer, &answer))
				answer = -1;
		}
		if (answer == 1)
		{
			/* (returns only if it failed: asked again at the next start) */
			platform_log("update: switching to the 64-bit build");
			updater_platform = "windows64";
			updater_update();
			updater_platform = UPDATE_PLATFORM;
			if (fullscreen)
				SDL_SetWindowFullscreen(window, true);
			return;
		}
		if (answer == 0)
		{
			if (config_write_boolean("update.offer_64bit", 0))
				platform_log("update: keeping the 32-bit build; update.offer_64bit = false written to config.toml");
			else
				platform_log("update: keeping the 32-bit build; could not write update.offer_64bit to config.toml");
		}
		if (!newer)
		{
			if (fullscreen)
				SDL_SetWindowFullscreen(window, true);
			return;
		}
		answer = 0;
	}
#endif
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
		platform_log("update: opening %s", updater_page());
		if (!SDL_OpenURL(updater_page()))
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

int updater_check_now(void)
{
	return 0;
}

int updater_check_state(char *latest, int size)
{
	if (latest && size > 0)
		latest[0] = 0;
	return UPDATER_NOT_CHECKED;
}

int updater_install(void)
{
	return 0;
}

int updater_ready(char *latest, int size)
{
	if (latest && size > 0)
		latest[0] = 0;
	return 0;
}

int updater_install_begin(void)
{
	return 0;
}

int updater_install_status(char *line, int size)
{
	if (line && size > 0)
		line[0] = 0;
	return UPDATER_INSTALL_IDLE;
}

const char *updater_install_amount(char *text, int size)
{
	if (text && size > 0)
		text[0] = 0;
	return text;
}

void updater_install_restart(void)
{
}

const char *updater_channel(void)
{
	return "release";
}

#endif

/* whether the game runs under gamescope (the Steam Deck's Game Mode), where
a system dialog such as SDL's message box crashes the game: every dialog
(sdl_platform.c, updater_poll) is left out there, its question kept for a
run on the desktop. gamescope sets GAMESCOPE_WAYLAND_DISPLAY for the programs
it runs and Game Mode's session XDG_CURRENT_DESKTOP; Steam sets SteamDeck and
SteamGamepadUI there. Wine passes them on to the Windows build under Proton */
int updater_gamescope(void)
{
	const char *wayland_display = getenv("GAMESCOPE_WAYLAND_DISPLAY");
	const char *desktop = getenv("XDG_CURRENT_DESKTOP");
	const char *steam_deck = getenv("SteamDeck");
	const char *gamepad_ui = getenv("SteamGamepadUI");

	return (wayland_display && wayland_display[0]) || (desktop && strstr(desktop, "gamescope")) ||
		(steam_deck && !strcmp(steam_deck, "1") && gamepad_ui && !strcmp(gamepad_ui, "1"));
}

/* this build's version (HALO_VERSION: 0.5.0b, 0.5.0b-nightly.42, 0.5.0b-dev),
for the window's title (sdl_platform.c) */
const char *updater_version(void)
{
	return HALO_VERSION;
}
