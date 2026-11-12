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
	WORKSHOP_BROWSER_KIND_SIZE = 16,
	WORKSHOP_BROWSER_DESCRIPTION_SIZE = 400,
	/* the largest list document read */
	WORKSHOP_BROWSER_MAXIMUM_DOCUMENT = 128 * 1024,
	WORKSHOP_BROWSER_PREVIEW_WIDTH = 384,
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
	/* the steamcmd row that installs MCC's Halo CE files (not a Workshop item) */
	int mcc_install;
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
/* whether the section shows: MCC is here, or steamcmd is (which can fetch it) */
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
/* UNSUBSCRIBE (the bridge's Steam client backend only) */
int workshop_browser_unsubscribe(int index);
/* the picture of an item for its page, RGBA rows from the top, at most
WORKSHOP_BROWSER_PREVIEW_WIDTH wide: an installed item's own (its first
map's thumbnail), else Steam's (its CDN), fetched only when asked for here,
on a thread, kept in the cache folder by item and update time. NULL while
there is none (yet); serial changes with the picture */
const unsigned char *workshop_browser_preview(int index, int *width, int *height, int *serial);
/* the section went out of view: the Steam bridge stopped */
void workshop_browser_close(void);
/* the Steam bridge for this computer: its backend (STEAM_BRIDGE_BACKEND_*,
NONE where it isn't offered), and its status (STEAM_BRIDGE_OFF when the
setting is off) */
int workshop_browser_bridge_backend(void);
int workshop_browser_bridge_status(void);

#endif

#endif
