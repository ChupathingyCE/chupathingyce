/*
HALO_UI_MAP_LIST.H

The menus' list of multiplayer maps on the native builds
(port/linux/game/ui_map_list.c): the Xbox's thirteen, as they were, then the
Custom Edition maps, named with [CE], with Halo PC's names, descriptions and
pictures of them, then HaloMD's maps, named with [MD] (halo_map_families.h).
The multiplayer map list, its rows and the lobby (source/interface) ask it in
place of the game's fixed thirteen.
*/

#ifndef HALO_UI_MAP_LIST_H
#define HALO_UI_MAP_LIST_H

struct bitmap_data;

/* a row's strings (ui_map_list_string_index) */
enum
{
	/* its name in the map list's narrow boxes: two lines, the name then [CE]
	or [MD] */
	_ui_map_list_string_name,
	_ui_map_list_string_description,
	/* its name on one line, for the lobby */
	_ui_map_list_string_lobby_name,
	/* its game's type in the lobby (a campaign level's: co-op) */
	_ui_map_list_string_game_type,
	/* its name over its picture in the map lists' description pane: the
	lobby's, but an MCC map's plain name, broken between words to fit */
	_ui_map_list_string_pane_name,
	NUMBER_OF_UI_MAP_LIST_STRINGS
};

/* the list anew: the game's thirteen Xbox map names, then those found */
void ui_map_list_refresh(char *const *xbox_maps);

/* the map choosers' view of the multiplayer maps: those of a source (where
they come from), in an order, matching a search; the default view (all, the
list's own order, no search) is the list as it is built. The rows below
count the view: a map it leaves out is still found by name
(ui_map_list_find), after the campaign's levels */
enum
{
	_ui_map_list_source_all,
	_ui_map_list_source_xbox,
	_ui_map_list_source_ce,
	_ui_map_list_source_md,
	_ui_map_list_source_mcc,
	NUMBER_OF_UI_MAP_LIST_SOURCES
};
enum
{
	_ui_map_list_sort_default,
	_ui_map_list_sort_name,
	_ui_map_list_sort_recent,
	NUMBER_OF_UI_MAP_LIST_SORTS
};
/* search (UTF-8; NULL or "" for none) matches a map's name, and an MCC
map's package's ModInfo.json and CampaignInfo.json (its title, author and
words), letters in any case */
void ui_map_list_view_set(short source, short sort, char const *search);
/* whether the lists offer MCC maps now: not while hosting a network game */
boolean ui_map_list_mcc_offered(void);
/* changes with each new view or list, for a list widget to take its rows again */
unsigned long ui_map_list_view_serial(void);
/* whether the list has maps of a source (all: any) */
boolean ui_map_list_source_present(short source);
/* their words, the same in every menu set: "All", "Xbox", "CE", "MD",
"MCC"; "Default order", "A-Z", "Recently added"; and as config has them:
"all", "xbox", "ce", "md", "mcc"; "default", "az", "recent" */
char const *ui_map_list_source_name(short source);
char const *ui_map_list_sort_name(short sort);
char const *ui_map_list_source_key(short source);
char const *ui_map_list_sort_key(short sort);
short ui_map_list_source_from_key(char const *key);
short ui_map_list_sort_from_key(char const *key);
/* the rows that are multiplayer maps */
long ui_map_list_count(void);
/* those and then the campaign's levels, for hosting network co-op from the
Xbox's map list (builds with HALO_GAME_BROWSER; the maps alone on others) */
long ui_map_list_count_with_campaign(void);
boolean ui_map_list_row_is_campaign(long row);
/* every row, those the view leaves out too (ui_map_list_names has them all) */
long ui_map_list_count_all(void);
/* the rows' map names, for the list widget's items */
char **ui_map_list_names(void);
/* the row of a map's name, or NONE */
long ui_map_list_find(char const *map_name);
/* as ui_map_list_find, the list filled anew for a name it lacks
(ui_widget_event_handler_functions.c, which has the Xbox's names) */
long ui_map_list_lookup(char const *map_name);
/* a row's string list index for one of its strings: an Xbox map's own in
ui.map, or one of this list's (ui_map_list_text) */
short ui_map_list_string_index(long row, short kind);
/* a row's bitmap frame: an Xbox map's in ui.map, or one of this list's
(ui_map_list_picture) */
short ui_map_list_picture_index(long row);
/* the bitmap of a frame of this list's, or NULL for ui.map's */
struct bitmap_data *ui_map_list_picture(short frame_index);
/* an MCC catalog map's frame (MCC_MAPS_FIRST_DISPLAY_INDEX and up): its
catalog index, whose own picture (its package's, else Steam's picture of
its Workshop item) mcc_menu_preview_draw draws; NONE for another frame */
short ui_map_list_mcc_picture(short frame_index);
/* the frame of the unknown level's picture, drawn when an MCC map has none */
short ui_map_list_unknown_picture(void);
/* the text of a string list index of this list's, or NULL for ui.map's */
wchar_t const *ui_map_list_text(short string_list_index);

/* a Custom Edition or HaloMD map (a family of halo_map_families.h), by its
file's name (the server browser's): its name (Halo PC's own, HaloMD's mod
list's, or the file's made readable; every build), Halo PC's picture of it
(one of Halo PC's own maps; else NULL), and whether its family's folders have it (builds with
HALO_CUSTOM_EDITION) */
void ui_map_list_family_name(short family, char const *file, wchar_t *name, long size);
struct bitmap_data *ui_map_list_family_picture(short family, char const *file);
boolean ui_map_list_family_present(short family, char const *file);

/* the same by a game's map's name (<file>, <file>@ce, <file>@<pack>: a map
pack's needs its pack's name), and its tag and what the tag stands for (a
pack's map's its pack's: CE+, Halo CE+); builds with HALO_MAP_LIST */
void ui_map_list_map_name(char const *map, wchar_t *name, long size);
boolean ui_map_list_map_present(char const *map);
void ui_map_list_map_tag(char const *map, char *tag, long tag_size, char *title, long title_size);

#endif
