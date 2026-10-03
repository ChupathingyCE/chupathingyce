/*
HALO_UI_MAP_LIST.H

The menus' list of multiplayer maps on the native builds
(port/linux/game/ui_map_list.c): the Xbox's thirteen, as they were, then the
Custom Edition maps in maps\ce, named with [CE], with Halo PC's names,
descriptions and pictures of them. The multiplayer map list, its rows and the
lobby (source/interface) ask it in place of the game's fixed thirteen.
*/

#ifndef HALO_UI_MAP_LIST_H
#define HALO_UI_MAP_LIST_H

struct bitmap_data;

/* a row's strings (ui_map_list_string_index) */
enum
{
	/* its name in the map list's narrow boxes: two lines, the name then [CE] */
	_ui_map_list_string_name,
	_ui_map_list_string_description,
	/* its name on one line, for the lobby */
	_ui_map_list_string_lobby_name,
	NUMBER_OF_UI_MAP_LIST_STRINGS
};

/* the list anew: the game's thirteen Xbox map names, then those found */
void ui_map_list_refresh(char *const *xbox_maps);
long ui_map_list_count(void);
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
/* the text of a string list index of this list's, or NULL for ui.map's */
wchar_t const *ui_map_list_text(short string_list_index);

/* a Custom Edition map, by its file's name (the server browser's): its name
(Halo PC's own, or the file's made readable; every build), Halo PC's
picture of it (or NULL), and whether maps\ce has it (builds with HALO_CUSTOM_EDITION) */
void ui_map_list_ce_name(char const *file, wchar_t *name, long size);
struct bitmap_data *ui_map_list_ce_picture(char const *file);
boolean ui_map_list_ce_present(char const *file);

#endif
