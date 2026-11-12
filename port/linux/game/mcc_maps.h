/* Halo 1 MCC map discovery. This list never includes the Xbox or CE folders. */
#ifndef __MCC_MAPS_H
#define __MCC_MAPS_H

/* port: MCC maps go in maps_mcc, beside maps, maps_ce, maps_pc and maps_md;
GulchCE's mcc_maps is read when there is no maps_mcc (mcc_maps_directory) */
#define MCC_MAPS_FOLDER "d:\\maps_mcc\\"
#define MCC_MAPS_FALLBACK_FOLDER "d:\\mcc_maps\\"
#define MCC_MAPS_DIRECTORY mcc_maps_directory()
#define MCC_MAPS_LEVEL_PREFIX "mcc_maps\\"
#define MCC_MAPS_FIRST_DISPLAY_INDEX 0x1000
#define MCC_MAPS_MAXIMUM 1024

char const *mcc_maps_directory(void);
char const *mcc_maps_folder(void);
void mcc_maps_rescan(void);
short mcc_maps_count(boolean multiplayer_only);
short mcc_maps_index(short row, boolean multiplayer_only);
/* Exact scenario-type lists; display indices still use the full catalog. */
short mcc_maps_type_count(boolean campaign);
short mcc_maps_type_index(short row, boolean campaign);
short mcc_maps_find(char const *level_name);
char const *mcc_maps_level_name(short index);
wchar_t const *mcc_maps_name(short index);
wchar_t const *mcc_maps_description(short index);
boolean mcc_maps_campaign(short index);
boolean mcc_maps_level_campaign(char const *level_name);
/* 0 standalone/disabled, 1 next mission, 2 completed, -1 unavailable next
 * mission. Only the explicit manifest supplies an order. */
int mcc_maps_next_mission(char const *level_name, char next[64]);
/* the Workshop item the Delta Guide's PLAY handed to the menus (NULL: none):
its maps lead mcc_maps_type_index's lists (and so the map lists), and the
campaign list opens once on its first (mcc_maps_take_first_focus) */
void mcc_maps_set_first_item(char const *item_id);
boolean mcc_maps_level_first(char const *level_name);
boolean mcc_maps_take_first_focus(void);
/* The title of the Steam Workshop item with this id when it is subscribed here and has one. */
boolean mcc_maps_workshop_title(char const *item_id, char *title, unsigned size);

/* Menu-only views. Identities, not catalog indices, survive a rescan. */
#include "../src/workshop_collections.h"
#define MCC_MAP_BROWSER_MAXIMUM (MCC_MAPS_MAXIMUM * WORKSHOP_COLLECTION_MAXIMUM_PER_ITEM)
struct mcc_map_browser
{
	/* the open folder: a package, or a Workshop collection (`collection`, its number in group); "" at the top */
	char package[64], selected[64];
	short entries[MCC_MAP_BROWSER_MAXIMUM];
	/* a folder row's collection (workshop_collections.h), or NONE for a package's */
	short groups[MCC_MAP_BROWSER_MAXIMUM];
	boolean folders[MCC_MAP_BROWSER_MAXIMUM];
	short count, chosen, group, selected_group;
	boolean campaign, collection, selected_folder;
};

void mcc_map_browser_open(struct mcc_map_browser *browser, boolean campaign);
void mcc_map_browser_select(struct mcc_map_browser *browser, short row);
boolean mcc_map_browser_enter(struct mcc_map_browser *browser, short row);
boolean mcc_map_browser_back(struct mcc_map_browser *browser);
short mcc_map_browser_index(struct mcc_map_browser const *browser, short row);
void mcc_map_browser_text(struct mcc_map_browser const *browser, short row, wchar_t text[64]);

#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_maps_rescan() ((void)0)
#define mcc_maps_campaign(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_maps_description(a0) ((void)sizeof(a0), (wchar_t const *)L"")
#define mcc_maps_directory() ((char const *)0)
#define mcc_maps_find(a0) ((void)sizeof(a0), (short)NONE)
#define mcc_maps_level_campaign(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_maps_level_name(a0) ((void)sizeof(a0), (char const *)0)
#define mcc_maps_name(a0) ((void)sizeof(a0), (wchar_t const *)L"")
#define mcc_maps_type_count(a0) ((void)sizeof(a0), (short)0)
#define mcc_maps_type_index(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (short)NONE)
#define mcc_maps_set_first_item(a0) ((void)sizeof(a0))
#define mcc_maps_level_first(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_maps_take_first_focus() ((boolean)0)
#endif

#endif
