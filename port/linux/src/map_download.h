/*
MAP_DOWNLOAD.H

Custom Edition maps a game needs, downloaded from the community map
repositories (map_download.c), as the Online Games screen offers them
(port/linux/game/browser_screen.c). network.map_downloads in config.toml
says whether the game asks first, downloads without asking, or never does.

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
	/* the bytes received, of total (0 when not known) */
	unsigned int received;
	unsigned int total;
	/* the map's file name */
	char name[32];
	/* why it failed */
	char message[160];
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
a thread of its own; version, when not 0, the host's (its whole-file
hash's first 32 bits, or from a host older than network version 29 its
header's checksum: map_archive_version_matches), which the file must have.
1 if it started (none was running) */
int map_download_start(const char *name, unsigned int version);
/* the running download stopped (its partial files deleted) */
void map_download_cancel(void);
void map_download_get(struct map_download_progress *progress);
/* a finished download's result forgotten (back to MAP_DOWNLOAD_IDLE) */
void map_download_reset(void);

#endif
