/*
WORKSHOP_BROWSER.H

The Guide's WORKSHOP section's data (port/linux/game/guide_workshop.c draws
it): Halo: MCC's Steam Workshop items for Halo CE, as our site lists them,
and where each stands in the player's own Steam libraries. No Steamworks:
SUBSCRIBE opens the item's page in the Steam client
(steam://url/CommunityFilePage/<id>), the player subscribes there, and Steam
downloads it into MCC's Workshop folder,
<library>/steamapps/workshop/content/976730/<id>/. That folder is watched:
an item shows INSTALLING while Steam fills it and INSTALLED once its
ModInfo.json is there, and its maps then join the map lists (mcc_maps.c,
steam_workshop.c).

The list is a GET of network.workshop_url (empty: no list, only the items
already installed), read by workshop_browser_read_list:

	{
		"version": 1,
		"items": [
			{
				"id": "2962107814",
				"title": "Blood Gulch Remastered",
				"author": "Someone",
				"kind": "map",
				"size": 48211968,
				"updated": 1730764800,
				"description": "A remake of the classic."
			}
		]
	}

- version: 1. A document of another version is refused whole.
- id: the Workshop item's id (publishedfileid), 1 to 20 digits. Required.
- title: required; author, kind, description optional. Strings are UTF-8
  as it is; \u escapes only for ASCII (json_reader.h). Longer text is cut.
- kind: "map", "maps", "campaign" or "gametypes" (anything else shows as
  an item).
- size (bytes) and updated (Unix seconds): whole numbers, optional.
- Any other field is passed over, for later additions.
At most WORKSHOP_BROWSER_MAXIMUM_ITEMS items are read; the site sends them
in the order the Guide lists them.

The parsing (workshop_list.c) has no game, SDL or file system, so its tests
build anywhere (port/linux/tests/workshop_list_test.c,
tools/test_workshop_list.py).
*/

#ifndef WORKSHOP_BROWSER_H
#define WORKSHOP_BROWSER_H

enum
{
	WORKSHOP_BROWSER_MAXIMUM_ITEMS = 64,
	WORKSHOP_BROWSER_ID_SIZE = 24,
	WORKSHOP_BROWSER_TITLE_SIZE = 64,
	WORKSHOP_BROWSER_AUTHOR_SIZE = 48,
	WORKSHOP_BROWSER_KIND_SIZE = 32,
	WORKSHOP_BROWSER_DESCRIPTION_SIZE = 400,
	/* the largest list document read */
	WORKSHOP_BROWSER_MAXIMUM_DOCUMENT = 128 * 1024,
	WORKSHOP_BROWSER_PREVIEW_WIDTH = 768,
};

/* where an item stands on this computer */
enum
{
	WORKSHOP_ITEM_NOT_INSTALLED = 0,
	/* SUBSCRIBE was pressed this run; Steam hasn't started it yet */
	WORKSHOP_ITEM_WAITING,
	/* its folder is there (or in Steam's downloads), its ModInfo.json not yet */
	WORKSHOP_ITEM_INSTALLING,
	WORKSHOP_ITEM_INSTALLED,
	/* the Steam bridge (steam_client_bridge.h) says Steam is downloading it */
	WORKSHOP_ITEM_DOWNLOADING,
};

/* the setup rows at the top of the list, in order, while one isn't Ready */
enum
{
	WORKSHOP_SETUP_STEAM = 1,
	WORKSHOP_SETUP_SIGN_IN,
	WORKSHOP_SETUP_MCC,
	WORKSHOP_SETUP_FILES,
	/* the row of search, filters and sorts (not a setup step) */
	WORKSHOP_ROW_FIND,
	/* what blocks GET (no Steam link): LINK goes to SETTINGS' STEAM ACCOUNT */
	WORKSHOP_ROW_LINK,
	/* the list's end: Steam's next page (and the one before) */
	WORKSHOP_ROW_PAGE,
};

/* Steam's own tags on Halo CE's items, the ones that are something to play */
enum
{
	WORKSHOP_TAG_MULTIPLAYER = 1,
	WORKSHOP_TAG_CAMPAIGN = 2,
};

/* the list's filters (a chip shows only while some item has it) and sorts */
enum
{
	WORKSHOP_FILTER_ALL = 0,
	WORKSHOP_FILTER_MULTIPLAYER,
	WORKSHOP_FILTER_CAMPAIGN,
	WORKSHOP_FILTER_INSTALLED,
	WORKSHOP_FILTER_COUNT,
};

enum
{
	WORKSHOP_SORT_POPULAR = 0,
	WORKSHOP_SORT_NEW,
	WORKSHOP_SORT_TOP_RATED,
	WORKSHOP_SORT_UPDATED,
	WORKSHOP_SORT_NAME,
	WORKSHOP_SORT_COUNT,
};

enum
{
	WORKSHOP_STEP_READY = 0,
	WORKSHOP_STEP_TO_DO,
	WORKSHOP_STEP_WORKING,
	WORKSHOP_STEP_NOT_AVAILABLE,
};

/* the list */
enum
{
	WORKSHOP_LIST_NOT_SET_UP = 0,
	WORKSHOP_LIST_LOADING,
	WORKSHOP_LIST_LOADED,
	WORKSHOP_LIST_FAILED,
};

struct workshop_browser_item
{
	char id[WORKSHOP_BROWSER_ID_SIZE];
	char title[WORKSHOP_BROWSER_TITLE_SIZE];
	char author[WORKSHOP_BROWSER_AUTHOR_SIZE];
	char kind[WORKSHOP_BROWSER_KIND_SIZE];
	char description[WORKSHOP_BROWSER_DESCRIPTION_SIZE];
	long long size;
	long long updated;
	/* WORKSHOP_ITEM_*: the watcher's */
	int state;
	/* found installed, not on the site's list */
	int local;
	/* SUBSCRIBE pressed this run */
	int subscribed;
	/* the Steam bridge's: subscribed in Steam, and a download's bytes (or percent of 100) */
	int steam_subscribed;
	unsigned long long downloaded;
	unsigned long long total;
	/* the download's failure in a word or two ("Not owned"); "" none */
	char steam_error[16];
	/* Steam's tags it has (WORKSHOP_TAG_*) */
	int tags;
	/* a setup row (WORKSHOP_SETUP_*), not a Workshop item: 0 for an item */
	int setup;
	/* a setup row's WORKSHOP_STEP_* */
	int step;
	/* Steam's picture of it (its CDN, https only); "" none */
	char preview_url[200];
};

/* ---------- the list's text (workshop_list.c) */

/* a list document: how many items were written to items, or -1 if it is not
one (not JSON of this form, or another version). An item without an id of
digits or a title is passed over */
int workshop_browser_read_list(const char *text, int length, struct workshop_browser_item *items, int maximum);
/* Steam's answer to ISteamRemoteStorage/GetPublishedFileDetails (no key):
how many Halo CE items of MCC's (consumer app 976730, tag Halo1, result 1)
were written, with their title, kind (campaign / maps), size when Steam
gives one, update time and a plain description; -1 if it is not one */
int workshop_browser_read_details(const char *text, int length, struct workshop_browser_item *items, int maximum);
/* the item ids on a page of Steam's public Workshop browse page (the digits
after each "filedetails/?id="), each once, in the page's order: how many */
int workshop_browser_read_browse_ids(const char *text, int length, char (*ids)[WORKSHOP_BROWSER_ID_SIZE], int maximum);
/* whether text is a Workshop item id: 1 to 20 digits */
int workshop_browser_id_valid(const char *text);
/* the page of an item in the Steam client: steam://url/CommunityFilePage/<id>; 0 for a bad id */
int workshop_browser_steam_url(const char *id, char *url, int size);
/* a kind's word ("Map", "Maps", "Campaign", "Game types", "Item") */
const char *workshop_browser_kind_name(const char *kind);
/* a state's word ("Installed") */
const char *workshop_browser_state_name(int state);
/* a size in bytes as "48 MB" ("" when unknown) */
void workshop_browser_size_text(long long size, char *text, int size_of_text);

/* ---------- the browser (workshop_browser.c) */

#ifndef WORKSHOP_LIST_ONLY

/* whether this computer has Halo: MCC from Steam (a library with its
appmanifest_976730.acf), looked at again at most every few seconds: the
WORKSHOP section shows only then */
int workshop_browser_mcc_present(void);
/* whether the section shows: on every desktop (its setup rows say what's missing) */
int workshop_browser_offered(void);
/* the section came into view: the list asked for (once a run unless it
failed, or again with force), the installed items looked for */
void workshop_browser_open(int force);
/* each frame while the section shows: the site's answer taken in, the
folders looked at every second or so. Returns how many items became
installed since the last call (their maps: mcc_maps_rescan) */
int workshop_browser_poll(void);
int workshop_browser_status(void);
int workshop_browser_count(void);
const struct workshop_browser_item *workshop_browser_item(int index);
/* SUBSCRIBE: through the Steam bridge when it is on and ready, else the
item's Steam page opened (platform_open_url); 0 for a bad index. For the
MCC row, steamcmd asked to install MCC's Halo CE files */
int workshop_browser_subscribe(int index);
/* the game's own Steam download is the downloader here (Steam's link is in SETTINGS) */
int workshop_browser_link_offered(void);
/* Steam's browse page shown (1 the first), and the next or the one before
asked for (each kept ten minutes); 0 when there is none that way */
int workshop_browser_page(void);
/* the game's own Steam download, once Steam is linked: turned on (the
link is the player's yes) and connected while the tab is open. Each frame */
void workshop_browser_link_follow(void);
/* an item's page in the Steam client: the way out when the game's own
download can't get it ("Not owned"); 0 for a setup row */
int workshop_browser_open_in_steam(int index);
int workshop_browser_page_next(void);
/* the list failed: asked again now (RETRY), 0 while a busy Steam asked to
be left alone (its seconds left in workshop_browser_busy_seconds, 0 when
it isn't busy) */
int workshop_browser_retry(void);
int workshop_browser_busy_seconds(void);
/* whether the list can go a page on (step 1) or back (-1) */
int workshop_browser_page_offered(int step);
int workshop_browser_page_previous(void);
/* a setup row's button: its word ("SIGN IN"), NULL for none; and pressing it */
const char *workshop_browser_setup_button(int index);
int workshop_browser_setup_press(int index);
/* the list as shown: the setup rows, the find row, then the items the
filter and search keep, in the sort's order. Popular, New, Top rated and
Updated ask Steam's page for that order (and the search's words), once per
change; A-Z sorts what is loaded. The search also keeps, at once, the
loaded and installed items whose title, kind or description has the words */
int workshop_browser_filter(void);
int workshop_browser_sort(void);
const char *workshop_browser_search(void);
/* whether a filter's chip shows: some item has it (All always) */
int workshop_browser_filter_offered(int filter);
void workshop_browser_set_filter(int filter);
void workshop_browser_set_sort(int sort);
void workshop_browser_set_search(const char *text);
const char *workshop_browser_filter_name(int filter);
const char *workshop_browser_sort_name(int sort);
/* a step's word ("Ready", "To do", "Working", "Not available") */
const char *workshop_browser_step_name(int step);
/* the game's own Steam library and steamcmd's folder set (each frame is fine) */
void workshop_browser_own_library(void);
/* an installed item of steamcmd's, in the game's own library: REMOVE deletes
its folder (after a confirm: the Guide's), UPDATE downloads it again (Steam
says it changed since). Steam's own folders are never touched here */
int workshop_browser_removable(int index);
int workshop_browser_remove(int index);
int workshop_browser_update_offered(int index);
int workshop_browser_update(int index);
/* any Workshop item's picture by its id (installed, kept, else fetched from
Steam by its details): RGBA pixels, NULL until there is one */
const unsigned char *workshop_browser_picture_of(const char *id, int *width, int *height, int *serial);
/* the map lists' picture of a Workshop item's map (level mcc_maps\<id>\...)
whose package has none: Steam's picture of the item, when the Workshop tab
has kept one in the cache folder (never fetched for this). Pixels as
mcc_preview_image's, freed with mcc_preview_image_free; 0 if none */
struct mcc_preview_image;
int workshop_browser_cached_picture(const char *level, struct mcc_preview_image *image);

/* ---------- a join's missing Workshop map (browser_screen.c's offer) */

enum
{
	WORKSHOP_JOIN_IDLE = 0,
	WORKSHOP_JOIN_CONNECTING,
	WORKSHOP_JOIN_DOWNLOADING,
	WORKSHOP_JOIN_INSTALLED,
	WORKSHOP_JOIN_FAILED,
	/* waiting on Steam's link (the Guide's QR, scanned with the Steam app) */
	WORKSHOP_JOIN_LINKING,
};

/* network.workshop_downloads: WORKSHOP_JOIN_ASK, ALWAYS or NEVER */
enum { WORKSHOP_JOIN_ASK = 0, WORKSHOP_JOIN_ALWAYS, WORKSHOP_JOIN_NEVER };
int workshop_join_setting(void);
void workshop_join_set_setting(int setting);
/* the item fetched through the player's Steam (the client: subscribed;
steamcmd: downloaded), the bridge turned on for it (the player's yes):
0 with why (one or two words and what to do) when it can't be */
/* WORKSHOP_CHECK_*: the item asked of Steam (its details, no key) before
the player is asked: OK with Steam's own title and size for a Halo CE item
of MCC's (consumer app 976730, tag Halo1); REFUSED otherwise; FAILED
without an answer. workshop_join_check starts it (0: a bad id) */
enum { WORKSHOP_CHECK_NONE = 0, WORKSHOP_CHECK_ASKING, WORKSHOP_CHECK_OK, WORKSHOP_CHECK_REFUSED, WORKSHOP_CHECK_FAILED };
int workshop_join_check(const char *id);
int workshop_join_checked(char *title, int title_size, long long *size);
int workshop_join_start(const char *id, char *why, int size);
/* where it stands (WORKSHOP_JOIN_*), its bytes when Steam says, and why it
failed; INSTALLED once its folder has its ModInfo.json (the maps read
again: the join then checks the host's map as for any map) */
int workshop_join_poll(unsigned long long *done, unsigned long long *total, char *why, int size);
void workshop_join_cancel(void);

/* UNSUBSCRIBE (the bridge's Steam client backend only) */
int workshop_browser_unsubscribe(int index);
/* the picture of an item for its page, RGBA rows from the top, at most
WORKSHOP_BROWSER_PREVIEW_WIDTH wide and as high: an installed item's own (its
first map's thumbnail), else Steam's (its CDN), fetched only when asked for
here, on a thread, kept in the cache folder by item and update time. NULL
while there is none (yet); serial changes with the picture */
const unsigned char *workshop_browser_preview(int index, int *width, int *height, int *serial);
/* 1 when the item's picture can't be had this run (none kept, Steam's
wouldn't come; debug.txt says why): its page says "No picture" */
int workshop_browser_preview_missing(int index);
/* the section went out of view: the Steam bridge stopped */
void workshop_browser_close(void);
/* the Steam bridge for this computer: its backend (STEAM_BRIDGE_BACKEND_*,
NONE where it isn't offered), and its status (STEAM_BRIDGE_OFF when the
setting is off) */
int workshop_browser_bridge_backend(void);
int workshop_browser_bridge_status(void);

#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): no Workshop,
the shared code's calls doing nothing, their arguments unevaluated */
#define workshop_browser_offered() 0
#define workshop_browser_picture_of(a0, a1, a2, a3) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (const unsigned char *)0)
#define workshop_browser_cached_picture(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), 0)
#define workshop_join_setting() WORKSHOP_JOIN_NEVER
#define workshop_join_set_setting(a0) ((void)sizeof(a0), (void)0)
#define workshop_join_start(a0, a1, a2) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), 0)
#define workshop_join_check(a0) ((void)sizeof(a0), 0)
#define workshop_join_checked(a0, a1, a2) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), WORKSHOP_CHECK_FAILED)
#define workshop_join_poll(a0, a1, a2, a3) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), WORKSHOP_JOIN_FAILED)
#define workshop_join_cancel() ((void)0)
#endif

#endif

#endif
