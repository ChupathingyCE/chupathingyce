/*
PLAYLIST_DOWNLOAD.H

A playlist's map set (docs/playlist-maps.md) checked and installed, for the
join flow (port/linux/game/browser_screen.c): the signed manifest fetched
from the game list's site and checked (playlist_manifest.c), the files it
names looked for in their folders, and on the player's yes the missing ones
downloaded from their sources, each checked against its size and SHA-256
before it is moved into place. network.playlist_downloads in config.toml
says whether the game asks first, downloads without asking, or never
checks; it is apart from network.map_downloads (map_download.h).

On a thread of its own; plain types only.
*/

#ifndef PLAYLIST_DOWNLOAD_H
#define PLAYLIST_DOWNLOAD_H

/* (network.playlist_downloads's words: map_archive.h's MAP_DOWNLOADS_*) */
#include "map_archive.h"

enum
{
	PLAYLIST_IDLE,
	/* the manifest fetched and the folders looked through */
	PLAYLIST_CHECKING,
	/* checked: the counts below say what is missing */
	PLAYLIST_READY,
	PLAYLIST_DOWNLOADING,
	PLAYLIST_DONE,
	/* no manifest (or none that checks), or a download failed: message */
	PLAYLIST_FAILED,
	PLAYLIST_CANCELLED,
};

enum
{
	/* the missing files listed for the player, at most */
	PLAYLIST_LINES = 6,
	PLAYLIST_LINE_SIZE = 72,
};

struct playlist_status
{
	int state;
	char playlist[33];
	char name[49];
	/* the maps the playlist plays (entries with a map name) */
	int maps;
	/* the files missing that can be downloaded, and their bytes */
	int missing;
	unsigned long long missing_bytes;
	/* the files that are there in another version (never replaced) */
	int other_versions;
	/* the player's own files missing, and the packs they are of (their
	names, joined) */
	int own_missing;
	char own_packs[128];
	/* a line a missing file ("Abyss (bloodgulch, CE+ X)"), the first of them */
	int line_count;
	char lines[PLAYLIST_LINES][PLAYLIST_LINE_SIZE];
	/* the credits of the packs downloaded from (the first two) */
	char credits[200];
	/* while downloading: files done of total, the current one's name and
	bytes */
	int done;
	int total;
	char current[48];
	unsigned int received;
	unsigned int current_total;
	/* why it failed (or what was skipped once done) */
	char message[160];
};

/* network.playlist_downloads (MAP_DOWNLOADS_*), and set (config.toml) */
int playlist_download_setting(void);
void playlist_download_set_setting(int setting);

/* whether this build checks playlists at all (the game list's HTTPS, not
the dedicated server) and the setting is not "never" */
int playlist_download_possible(void);

/* the playlist's manifest fetched and checked, and its files looked for, on
a thread of its own: 1 if it started (nothing else running) */
int playlist_check_start(const char *playlist);
/* the missing files a check found downloaded: 1 if it started */
int playlist_download_start(void);
void playlist_download_cancel(void);
/* the state, on the game's thread (which marks the Custom Edition maps
installed as downloaded maps: halo_map_families.h) */
void playlist_get(struct playlist_status *status);
/* back to PLAYLIST_IDLE, unless running */
void playlist_reset(void);

#endif
