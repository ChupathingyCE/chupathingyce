/*
STEAM_CLIENT_BRIDGE.H

The Delta Guide's Workshop tab subscribing to Halo: MCC's (app 976730)
Workshop items itself, through the player's own Steam, instead of opening
each item's page in the Steam client (workshop_browser.h). It is opt-in
(game.workshop_steam, off by default) and lives only while the tab is open:
steam_bridge_open when it opens, steam_bridge_close when it closes. All of
its work is on a thread of its own; the game only reads its results. When
it can't work (no Steam, not signed in, MCC not owned, a library without
the functions it needs, a timeout) its status is STEAM_BRIDGE_FAILED and
the tab keeps the steam:// page and the folder watch.

Two backends, behind the same calls, one or both in a session:

- STEAM_BRIDGE_BACKEND_CLIENT: a steam_api library already on the
  player's computer (steam_bridge_find.c: MCC's own steam_api64.dll on
  64-bit Windows; elsewhere one an installed game ships, or
  paths.steam_api), loaded at run time and called through its flat C
  functions (steam_bridge_api.c), which talk to the running Steam client.
  Its app id is MCC's, given in the SteamAppId environment variable before
  it starts; Steam then shows MCC as running while the tab is open. No
  Steamworks SDK is in our sources or builds: the declarations are our
  own, from Valve's public documentation. It subscribes, reads item states
  and progress, and lists MCC's Halo CE items (tag Halo1). Windows, and
  Linux with MCC from Steam (under Proton, Steam downloads its items
  itself). Not on macOS: there Steam.app starts as MCC but refuses MCC's
  Workshop queries and subscriptions, as MCC has no macOS build.
- STEAM_BRIDGE_BACKEND_STEAMCMD (macOS, and Linux without MCC from Steam):
  the player's own steamcmd, run as a child process with their saved
  sign-in only (steam_bridge_cmd.c), as the download step. The game never
  asks for, keeps or passes a password: without a saved sign-in,
  steam_bridge_sign_in_needed, and steam_bridge_sign_in opens a terminal
  running steamcmd for the player to sign in. steamcmd also installs the
  Halo CE parts of MCC (steam_bridge_install_mcc). The Steam client still
  subscribes, through the item's page (workshop_browser.c).

The core (steam_client_bridge.c), the client backend and steamcmd's text
reading need only the steam_bridge_platform_* functions below
(posix_steam_bridge.c, port/windows/src/win32_steam_bridge.c), so their
tests run with a fake library (port/linux/tests/steam_bridge_test.c).
*/

#ifndef STEAM_CLIENT_BRIDGE_H
#define STEAM_CLIENT_BRIDGE_H

enum
{
	STEAM_BRIDGE_OFF = 0,
	STEAM_BRIDGE_CONNECTING,
	STEAM_BRIDGE_READY,
	/* steamcmd has no saved sign-in */
	STEAM_BRIDGE_SIGN_IN,
	STEAM_BRIDGE_FAILED,
};

enum
{
	STEAM_BRIDGE_BACKEND_NONE = 0,
	STEAM_BRIDGE_BACKEND_CLIENT,
	STEAM_BRIDGE_BACKEND_STEAMCMD,
};

/* an item, as the bridge knows it */
enum
{
	STEAM_BRIDGE_ITEM_UNKNOWN = 0,
	STEAM_BRIDGE_ITEM_NONE,
	/* subscribed (or a steamcmd download asked for), not started yet */
	STEAM_BRIDGE_ITEM_QUEUED,
	STEAM_BRIDGE_ITEM_DOWNLOADING,
	STEAM_BRIDGE_ITEM_INSTALLED,
	STEAM_BRIDGE_ITEM_FAILED,
};

enum
{
	STEAM_BRIDGE_MAXIMUM_ITEMS = 72,
	STEAM_BRIDGE_ID_SIZE = 24,
	STEAM_BRIDGE_PATH_SIZE = 512,
	/* SteamAPI_Init (or finding steamcmd) given this long, then the tab falls back */
	STEAM_BRIDGE_CONNECT_TIMEOUT = 10000,
	/* the worker's look at the items this often */
	STEAM_BRIDGE_POLL_INTERVAL = 500,
	/* one steamcmd item download, and the MCC install, at most this long */
	STEAM_BRIDGE_DOWNLOAD_TIMEOUT = 30 * 60 * 1000,
	STEAM_BRIDGE_INSTALL_TIMEOUT = 3 * 60 * 60 * 1000,
};

struct steam_bridge_item
{
	int state;
	/* subscribed in Steam (the client backend's) */
	int subscribed;
	unsigned long long downloaded;
	unsigned long long total;
};

/* a Workshop item the Steam client listed (browse or search) */
struct steam_bridge_listing
{
	char id[STEAM_BRIDGE_ID_SIZE];
	char title[64];
	char description[400];
};

enum
{
	STEAM_BRIDGE_MAXIMUM_LISTINGS = 50,
	/* listings: still waiting on Steam */
	STEAM_BRIDGE_LISTINGS_PENDING = -2,
	STEAM_BRIDGE_LISTINGS_FAILED = -1,
};

/* ---------- the interface the Workshop tab uses */

/* game.workshop_steam */
int steam_bridge_enabled(void);
/* whether this computer could have a bridge at all (looked for once, cheap
after): Windows, MCC's steam_api64.dll; elsewhere steamcmd, or a Steam
client to try (paths.steam_api, or a steam_api library a game installed) */
int steam_bridge_backend(void);
/* the tab opened (and the setting is on): the worker started. With
use_steamcmd, steamcmd does the downloads (macOS: Steam.app subscribes to
MCC items but never downloads them; Linux without MCC from Steam) */
void steam_bridge_open(int use_steamcmd);
/* the tab closed: the worker told to stop (it shuts Steam down) */
void steam_bridge_close(void);
/* STEAM_BRIDGE_*: READY when either backend is; FAILED once a connect has
taken too long or neither connected */
int steam_bridge_status(void);
/* which backends connected: STEAM_BRIDGE_BACKEND_CLIENT and/or STEAMCMD bits (1 << kind) */
int steam_bridge_connected(void);
/* steamcmd needs a sign-in before it can download */
int steam_bridge_sign_in_needed(void);
/* what the bridge knows of an item, which it watches from now on: 0 (and
UNKNOWN) when it isn't ready or the id is bad */
int steam_bridge_item(const char *id, struct steam_bridge_item *item);
/* subscribe (client: SubscribeItem and a download at once; steamcmd: a
download queued; both on macOS); 0 when not ready */
int steam_bridge_subscribe(const char *id);
/* client only */
int steam_bridge_unsubscribe(const char *id);
int steam_bridge_can_unsubscribe(void);
/* client only: MCC's Halo CE Workshop items (tag Halo1) asked of Steam,
most subscribed first, matching search when it isn't empty; 0 without
the client */
int steam_bridge_browse(const char *search);
/* the answer: how many were written, or STEAM_BRIDGE_LISTINGS_PENDING / FAILED */
int steam_bridge_listings(struct steam_bridge_listing *listings, int maximum);
/* steamcmd: a terminal opened running steamcmd's sign-in */
int steam_bridge_sign_in(void);
/* steamcmd: the Halo CE depots of MCC fetched and placed as MCC's install
in steamcmd's library; its state as STEAM_BRIDGE_ITEM_* */
int steam_bridge_install_mcc(void);
int steam_bridge_mcc_state(void);

/* ---------- the backends (steam_bridge_api.c, steam_bridge_cmd.c) */

struct steam_bridge_backend
{
	/* on the worker: STEAM_BRIDGE_READY, SIGN_IN or FAILED */
	int (*connect)(void);
	void (*disconnect)(void);
	/* item ids as numbers; 1 if asked */
	int (*subscribe)(unsigned long long id);
	int (*unsubscribe)(unsigned long long id);
	/* the item's state now; 1 if known */
	int (*query)(unsigned long long id, struct steam_bridge_item *item);
	/* each turn of the worker (callbacks, a download's output) */
	void (*tick)(void);
	/* steamcmd: a sign-in is needed now (a download said so) */
	int (*needs_sign_in)(void);
	/* steamcmd: MCC's Halo CE depots fetched and placed; its STEAM_BRIDGE_ITEM_* */
	int (*install_mcc)(void);
	int (*mcc_state)(void);
	/* client: a query asked (on the worker), and its answer as steam_bridge_listings */
	int (*browse)(const char *search);
	int (*listings)(struct steam_bridge_listing *listings, int maximum);
};

extern const struct steam_bridge_backend steam_bridge_api_backend;
#ifndef _WIN32
extern const struct steam_bridge_backend steam_bridge_cmd_backend;
#endif

/* ---------- the client backend's library (steam_bridge_api.c) */

enum { STEAM_BRIDGE_MAXIMUM_LIBRARIES = 8 };

/* where the client backend's library may be, in the order to try:
paths.steam_api, else (Windows) MCC's own steam_api64.dll in a Steam
library, else (elsewhere, with search) the steam_api libraries installed
games ship (one built for this process's architecture loads; the others
don't). How many were written. With search 0 only the cheap places are
looked at (steam_bridge_backend, on the game's thread) */
int steam_bridge_api_library_paths(char (*paths)[STEAM_BRIDGE_PATH_SIZE], int maximum, int search);
/* whether a Steam folder is on this computer at all (a client to try) */
int steam_bridge_steam_present(void);

/* the head of ISteamUGC's SteamUGCDetails_t (id, result, file type, apps,
title[129], description[8000]), the same with Windows' and the others'
packing: a listing from it; 0 if it is not a usable item */
int steam_bridge_read_details(const unsigned char *details, struct steam_bridge_listing *listing);

/* ---------- steamcmd's text (steam_bridge_cmd.c; no files, no processes) */

enum
{
	STEAMCMD_LINE_NOTHING = 0,
	/* "Success. Downloaded item <id> to "<path>"" */
	STEAMCMD_LINE_ITEM_DONE,
	/* "ERROR! Download item <id> failed" */
	STEAMCMD_LINE_ITEM_FAILED,
	/* "Depot download complete : "<path>"" */
	STEAMCMD_LINE_DEPOT_DONE,
	/* a password, Steam Guard code or a cached sign-in was asked for or missing */
	STEAMCMD_LINE_SIGN_IN,
	/* "progress: 12.34" or "(12%)" */
	STEAMCMD_LINE_PROGRESS,
	STEAMCMD_LINE_ERROR,
};

struct steamcmd_line
{
	int kind;
	char id[STEAM_BRIDGE_ID_SIZE];
	char path[STEAM_BRIDGE_PATH_SIZE];
	/* PROGRESS: 0 to 100 */
	int percent;
};

void steamcmd_read_line(const char *line, struct steamcmd_line *out);
/* the first account name in steamcmd's config.vdf ("Accounts" { "<name>" ... }):
1 and the name, 0 if none */
int steamcmd_read_account(const char *text, int length, char *name, int size);
/* whether text is a Steam account name we would pass on a command line */
int steamcmd_account_valid(const char *name);

/* ---------- the platform (posix_steam_bridge.c, win32_steam_bridge.c) */

void *steam_bridge_platform_library_open(const char *path);
void *steam_bridge_platform_library_symbol(void *library, const char *name);
/* SteamAppId (and SteamGameId) in this process's environment */
void steam_bridge_platform_set_app_id(const char *app_id);
void steam_bridge_platform_sleep(int milliseconds);
#ifndef _WIN32
/* steamcmd: its path (paths.steamcmd, PATH, the usual places); 0 if none */
int steam_bridge_platform_find_steamcmd(char *path, int size);
/* steamcmd's own folder (where it keeps config/config.vdf and steamapps) */
int steam_bridge_platform_steamcmd_root(const char *steamcmd, char *root, int size);
/* a child with arguments (NULL-ended), stdin empty, stdout and stderr read
through steam_bridge_platform_child_line; NULL if it could not start */
void *steam_bridge_platform_child_start(const char *const *arguments);
/* a line of its output within milliseconds: 1 with a line, 0 none yet, -1 it ended */
int steam_bridge_platform_child_line(void *child, char *line, int size, int milliseconds);
/* ended (killed if it still runs) and freed */
void steam_bridge_platform_child_end(void *child);
/* a terminal window running steamcmd's sign-in */
int steam_bridge_platform_open_sign_in(const char *steamcmd);
/* from's files moved into to, folders merged (MCC's depots into its install) */
int steam_bridge_platform_merge_folder(const char *from, const char *to);
#endif

#endif
