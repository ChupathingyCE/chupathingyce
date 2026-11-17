/*
MAP_REPO.H

Chupathingy maps (maps.chupathingy.org), as the game uses it: the second
place a missing Custom Edition map is downloaded from, after the community
map repositories (map_download.c), and the blacklist every map is held to,
wherever it came from (docs/maps.md, "Chupathingy maps").

map_repo.c keeps the last signed index and mirror list it checked
(map_index.h), in memory and in the save root, read back at start
(map_repo_load); fetches them again when the index says to (an hour), a
failed fetch tried again after 5 minutes, then 10, up to an hour; and never
takes an older one than it has taken, nor another of the same serial (a
floor kept with them, as Delta's legacy table has). An expired index is
never used for a download, but its blacklist holds until a newer index
comes. It finds a map by the host's name and version, and downloads it
from the mirrors in their order until one gives the file of the index's
size and SHA-256. The index's kill switch (halt) stops every download from
it; its blacklist refuses a file by its SHA-256 or its map hash, from any
source, and a map already here when a network game on it is joined or
hosted (map_repo_game.c).

Its requests and files go through a transport: the game's (map_repo_game.c:
the game list's HTTPS and the game's files), or a test's. Plain types only;
any thread.
*/

#ifndef MAP_REPO_H
#define MAP_REPO_H

#include "map_index.h"

/* where the index and mirror list are fetched from (network.maps_url's
default) */
#define MAP_REPO_DEFAULT_URL "https://maps.chupathingy.org"
/* the name players know it by */
#define MAP_REPO_TITLE "Chupathingy maps"

struct map_repo_transport
{
	void *context;
	/* a GET of url, its body into buffer (NUL terminated, cut to size - 1
	bytes): the HTTP status, or 0 with why in why when there was no answer */
	int (*get)(void *context, const char *url, char *buffer, int size, char *why, int why_size);
	/* a GET of url streamed into the file kept as name (made anew, beside
	MAP_REPO_*_FILE in the save root), refused once past maximum bytes, then
	read back into buffer (maximum + 1 bytes, NUL terminated) and the file
	deleted: 1 with the body's size in *size, else 0 with the HTTP status in
	*status (0 for none) and why in why. The index and the mirror list come
	this way, as large as they may be (MAP_*_DOCUMENT_SIZE); get has its
	own limit */
	int (*get_document)(void *context, const char *url, const char *name, char *buffer, long maximum, long *size,
		int *status, char *why, int why_size);
	/* a GET of url into the file at path (made anew), at most maximum bytes:
	1 with the bytes received in *received and their SHA-256 in sha256,
	else 0 with the HTTP status in *status (0 for none) and why in why */
	int (*get_file)(void *context, const char *url, const char *path, unsigned int maximum, unsigned char *sha256,
		unsigned int *received, int *status, char *why, int why_size);
	/* the file at path deleted */
	void (*remove)(void *context, const char *path);
	/* whether to stop (the player cancelled) (NULL: never) */
	int (*stop)(void *context);
	/* the bytes kept as the file name (MAP_REPO_*_FILE, in the save root)
	read into buffer, at most size: their count, or -1 (NULL: nothing is
	kept between runs) */
	long (*load)(void *context, const char *name, char *buffer, long size);
	/* data (size bytes) kept as the file name, replacing it whole */
	void (*save)(void *context, const char *name, const char *data, long size);
};

/* what is kept between runs, in the save root: the index and the mirror
list last taken (each its signature's 128 hex digits, a line feed, then the
document's exact bytes), and the floors ("index <serial> <sha256>" and
"mirrors <serial> <sha256>" lines) */
#define MAP_REPO_INDEX_FILE "chupathingy_maps_index.signed"
#define MAP_REPO_MIRRORS_FILE "chupathingy_maps_mirrors.signed"
#define MAP_REPO_FLOOR_FILE "chupathingy_maps_floor.txt"

/* map_repo_refresh's flags */
enum
{
	/* fetched even if the index held isn't due a refresh */
	MAP_REPO_REFRESH_FORCE = 1 << 0,
	/* the index alone (its blacklist): the mirror list not asked for */
	MAP_REPO_REFRESH_INDEX_ONLY = 1 << 1,
};

/* whether this build has a key for Chupathingy maps (maps_key.h): without
one, nothing here asks it for anything */
int map_repo_enabled(void);

/* what an earlier run kept read back through transport's load (once, at
start): the floors, and the index and mirror list, each held if its
signature checks and the floor isn't past it (expired or not). 1 if an
index is held after it; what happened in why either way (empty: nothing
was kept) */
int map_repo_load(const struct map_repo_transport *transport, long long now, char *why, int why_size);
/* the index and mirror list fetched from base_url and checked at now
(seconds since 1970), unless it isn't due (map_repo_refresh_due) or a
failed refresh's wait isn't over (flags: MAP_REPO_REFRESH_*); when
only the mirror list is due (it failed, or was never asked for, and the
index is still good) it alone is fetched. Each taken is kept through transport's save. 1 if an unexpired index that
checks is held after it, else 0 with why in why */
int map_repo_refresh(const struct map_repo_transport *transport, const char *base_url, long long now, int flags,
	char *why, int why_size);
/* whether the index held is due a refresh at now (none held: 1), for a
refresh with flags (MAP_REPO_REFRESH_*): without MAP_REPO_REFRESH_INDEX_ONLY,
also when the last one asked for the index alone (map downloads turned back
on: the mirror list fetched then, not an hour later) */
int map_repo_refresh_due(long long now, int flags);

/* whether a file of this SHA-256 or map hash (either NULL when not known)
is on the blacklist of the index held, expired or not (now unused): 1 with
the entry's reason in reason (size bytes), else 0 */
int map_repo_blocked(const unsigned char *sha256, const unsigned char *map_hash, long long now, char *reason,
	int reason_size);

/* whether an index is held, expired or not: its blacklist is in force (a
map from a game's host is never downloaded without one) */
int map_repo_index_held(void);

/* whether the index held (unexpired, not halted) has the Custom Edition map
name of the host's version (map_index_find) */
int map_repo_has(const char *name, unsigned int version, long long now);

/* what map_repo_download refused a map for, beside why's words */
enum
{
	MAP_REPO_REFUSED_NONE,
	/* on the blacklist */
	MAP_REPO_REFUSED_BLOCKED,
	/* a mirror's file wasn't the index's size or SHA-256 */
	MAP_REPO_REFUSED_MISMATCH,
};

/* the Custom Edition map name of the host's version (map_index_find) from
the mirrors into the file at path: 1 with the index's entry for it in
entry, else 0 with why in why, *missing 1 when the index doesn't have it
(not a failure of the download) and *refused what it was refused for
(MAP_REPO_REFUSED_*; NULL for none) */
int map_repo_download(const struct map_repo_transport *transport, const char *name, unsigned int version,
	const char *path, long long now, struct map_index_entry *entry, int *missing, int *refused, char *why,
	int why_size);

/* everything held forgotten (the tests') */
void map_repo_reset(void);

/* ---------- the game's (map_repo_game.c) */

/* what a download through the game's transport reports to, and asks */
struct map_repo_game_hooks
{
	/* the bytes received of total (NULL: not told) */
	void (*progress)(unsigned int received, unsigned int total);
	/* whether to stop (NULL: never) */
	int (*stop)(void *context);
};

/* the game's transport (the game list's HTTPS, the game's d:\ files), its
downloads told to hooks (NULL: none) */
void map_repo_game_transport(struct map_repo_transport *transport, const struct map_repo_game_hooks *hooks);
/* network.maps_url, without a '/' ending it: empty when the player emptied
it, which turns Chupathingy maps off (nothing fetched, nothing refused) */
void map_repo_game_url(char *url, int size);
/* the index refreshed on a thread of its own when it is due (and the
build has a key, and network.maps_url isn't empty); with
network.map_downloads "never", the index alone (its blacklist). The first
call reads back what an earlier run kept (map_repo_load) */
void map_repo_game_refresh_soon(void);
/* the same, then waited for at most milliseconds (the dedicated server's
start: never held up long by a repository that doesn't answer) */
void map_repo_game_refresh_wait(int milliseconds);
/* the index refreshed on this thread when due: 1 if one is held after */
int map_repo_game_refresh_now(void);
/* whether the map file at path (a game path, d:\...) may not be played in
another host's game, its map hash on the blacklist: 1 with what the player
is told in message (size bytes), naming the map (map_name), else 0 */
int map_repo_file_refused(const char *path, const char *map_name, char *message, int size);
/* the same for a network game this machine would host on it (the menus' or
the dedicated server's): 1 with what the host is told in message, else 0.
Playing it alone or in split screen isn't asked about: the player's own copy
is theirs to play */
int map_repo_host_refused(const char *path, const char *map_name, char *message, int size);

#endif
