/*
MAP_DOWNLOAD.H

Custom Edition maps a game needs, downloaded from the community map
repositories (map_download.c), as the Online Games screen offers them
(port/linux/game/browser_screen.c). network.map_downloads in config.toml
says whether the game asks first, downloads without asking, or never does.

When neither the community repositories nor Chupathingy maps give it, the
game's host may (map_download.c, "From the host"): only as
network.map_downloads_from_host says, which is off unless the player turns
it on. The transfer is map_share_client.c's, on the game's thread; the
file it writes waits in a quarantine folder until it passes the quick
checks (map_quick_check.h) and the blacklist, and is moved into maps_ce
here.

Plain types only: the game's units (64-bit builds rewrite their long) and
the platform's both call it.
*/

#ifndef MAP_DOWNLOAD_H
#define MAP_DOWNLOAD_H

/* (network.map_downloads's MAP_DOWNLOADS_*) */
#include "map_archive.h"

/* a download's state */
enum
{
	MAP_DOWNLOAD_IDLE,
	MAP_DOWNLOAD_RUNNING,
	MAP_DOWNLOAD_DONE,
	MAP_DOWNLOAD_FAILED,
	MAP_DOWNLOAD_CANCELLED,
};

/* where a download comes from */
enum
{
	/* the community map repositories (HaloNet's) */
	MAP_DOWNLOAD_SOURCE_COMMUNITY,
	/* Chupathingy maps (map_repo.h), when they don't have it */
	MAP_DOWNLOAD_SOURCE_CHUPATHINGY,
	/* the game's host, when neither has it and the player allows it
	(network.map_downloads_from_host) */
	MAP_DOWNLOAD_SOURCE_HOST,
};

/* what a running download is doing */
enum
{
	MAP_DOWNLOAD_CONNECTING,
	MAP_DOWNLOAD_RECEIVING,
	MAP_DOWNLOAD_UNPACKING,
	MAP_DOWNLOAD_CHECKING,
};

struct map_download_progress
{
	int state;
	int phase;
	/* MAP_DOWNLOAD_SOURCE_* */
	int source;
	/* the bytes received, of total (0 when not known) */
	unsigned int received;
	unsigned int total;
	/* the map's file name */
	char name[32];
	/* why it failed */
	char message[160];
	/* it failed because the map is on Chupathingy maps' blacklist: no other
	source is asked, the host neither */
	int blocked;
};

/* network.map_downloads_from_host */
enum
{
	MAP_HOST_DOWNLOADS_OFF,
	MAP_HOST_DOWNLOADS_ASK,
	MAP_HOST_DOWNLOADS_ALWAYS,
};

/* what a player is told before a map comes from a game's host (the
Online Games screen's offer, docs/maps.md, the setting's help): two lines */
#define MAP_HOST_DISCLAIMER_FIRST "This map comes straight from the host, not from us,"
#define MAP_HOST_DISCLAIMER_SECOND "and we haven't checked it."
/* (the setting's, where it is turned on) */
#define MAP_HOST_DISCLAIMER_SETTING "Maps from hosts come straight from them, not from us, and we haven't checked " \
	"them. Turn this on only if you're OK with that."

/* what follows a download that ended (map_download_host_step) */
enum
{
	/* nothing: the map is here, or the host isn't asked */
	MAP_HOST_STEP_NONE,
	/* the player asked whether to download it from the host */
	MAP_HOST_STEP_ASK,
	/* downloaded from the host without asking */
	MAP_HOST_STEP_START,
};

/* network.map_downloads (MAP_DOWNLOADS_*), and set (written to
config.toml) */
int map_download_setting(void);
void map_download_set_setting(int setting);

/* whether this build downloads maps at all (not without the game list's
HTTPS, nor the dedicated server) and network.map_downloads is not "never",
and name (a Custom Edition map's file name) is one the repositories are
asked for */
int map_download_possible(const char *name);

/* the Custom Edition map name (its file's name) downloaded into maps_ce, on
a thread of its own, from the community map repositories, or when they
don't have it (of the host's version), from Chupathingy maps (map_repo.h);
a map on Chupathingy maps' blacklist is refused, whichever gave it; version, when not 0, the host's (its whole-file
hash's first 32 bits, or from a host older than network version 29 its
header's checksum: map_archive_version_matches), which the file must have.
1 if it started (none was running) */
int map_download_start(const char *name, unsigned int version);
/* the running download stopped (its partial files deleted) */
void map_download_cancel(void);
void map_download_get(struct map_download_progress *progress);
/* the same, from any thread, the map not marked as downloaded (the
playlist downloader's: playlist_download.c, whose caller marks it) */
void map_download_peek(struct map_download_progress *progress);
/* a finished download's result forgotten (back to MAP_DOWNLOAD_IDLE) */
void map_download_reset(void);

/* ---------- from the host (map_download.c, "From the host") */

/* network.map_downloads_from_host (MAP_HOST_DOWNLOADS_*: "off", "ask" or
"always"; anything else is off), and its words */
int map_download_host_setting_from(const char *text);
const char *map_download_host_setting_word(int setting);
/* the setting, on a build that downloads from hosts (else off), and set
(written to config.toml) */
int map_download_host_setting(void);
void map_download_host_set_setting(int setting);
/* the most a map from a host may be, in bytes
(network.map_downloads_from_host_limit, in MB: map_quick_check_limit) */
unsigned int map_download_host_limit(void);
/* what follows a download that ended as progress says, for a setting
(MAP_HOST_STEP_*): the host is asked only after the repositories failed
without the blacklist refusing the map, and never twice */
int map_download_host_step(int host_setting, const struct map_download_progress *progress);
/* the same with the setting this build has */
int map_download_host_next(const struct map_download_progress *progress);

/* a download from the host begun, for the map name (its file's name; a
hashed name's map is checked and kept as the repositories' are) of the
host's version: 1, else 0 (another is running). Its progress is
map_download_get's, its source MAP_DOWNLOAD_SOURCE_HOST */
int map_download_host_begin(const char *name, unsigned int version);
/* how it goes (MAP_DOWNLOAD_CONNECTING, MAP_DOWNLOAD_RECEIVING) */
void map_download_host_progress(int phase, unsigned int received, unsigned int total);
/* whether the player cancelled it (map_download_cancel) */
int map_download_host_cancelled(void);
/* whether a file of that map hash (the host's offer's) is on the
blacklist, before any of it is downloaded: 1 with the reason in reason */
int map_download_host_refused(const unsigned char *map_hash, char *reason, int reason_size);
/* the quarantine folder's files for name: the one being written, and the
one it is renamed to once whole (game paths, d:\...) */
void map_download_host_paths(const char *name, char *part, char *whole, int size);
/* the file whole in the quarantine folder (map_download_host_paths's
whole), with the offered map hash: checked and moved into maps_ce on a
thread of its own (the quick checks, the host's version, the blacklist
again, by its SHA-256 and map hash), else deleted; the progress then DONE
or FAILED */
void map_download_host_received(const unsigned char *map_hash);
/* it ended without the file: why (cancelled: the player's cancel;
blocked: the blacklist refused the map the host offered) */
void map_download_host_failed(const char *message, int cancelled, int blocked);
/* whether a map may come from a host at all: Chupathingy maps' blacklist
must be held (map_repo_index_held), else 0 with why in why */
int map_download_host_allowed(char *why, int why_size);
/* the quarantine folder emptied of what an earlier run left (once, at
start) */
void map_download_host_sweep(void);

#endif
