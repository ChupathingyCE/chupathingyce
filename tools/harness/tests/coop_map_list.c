/*
COOP_MAP_LIST.C

ONLINE CO-OP's Select Map, the campaign's levels alone (#1736), and what the
menus' map list is once that list is gone: the game's Select Map list
(multiplayer_level_list_initialize), ui_widget_delete, the map list's view
(port/linux/game/ui_map_list.c) and the map lookups the lobby and the
Chupathingy menus make (ui_map_list_lookup, ui_widget_port_multiplayer_maps,
ui_widget_port_multiplayer_map_choose), over a made-up list of two
multiplayer maps and two levels. The co-op view left on after its list
closed had the maps' count at 0: the lookups scanned the map folders again
on every call and every map was refused.
*/

#include "harness.h"
#include <strings.h>
#include <wchar.h>

#define HALO_MAP_LIST
#define HALO_GAME_BROWSER

#undef match_assert
#define match_assert(file, line, condition) CHECK(condition, "%s:%d", file, line)
#define match_vassert(file, line, condition, text) CHECK(condition, "%s:%d: %s", file, line, text)

#define csmemset memset
#define _stricmp strcasecmp
#define PIN(value, low, high) ((value) < (low) ? (low) : (value) > (high) ? (high) : (value))
#define MAXIMUM_NUMBER_OF_LOCAL_PLAYERS 4
#define WIDGET_DELETED_PLAYER_CONTROL_INHIBIT_FLAGS 0
#define _error_silent 0
#define UI_WIDGET_DEFINITION_TAG 0x44654c37
#define ONLINE_GAMES_SCREEN "multiplayer_type_select_screen"
#define MULTIPLAYER_SCREEN_TAG 7
#define MAP_SELECT_TAG 8
#define SPINNER_LIST 2

/* ---- the widgets */

enum { _widget_event_deleted = 9 };
enum { _event_handler_run_function_bit, _event_handler_open_widget_bit };
enum { _ui_widget_type_container, _ui_widget_type_text_box, _ui_widget_type_spinner_list,
	_ui_widget_type_column_list };

struct event_record { short controller_index; };
struct ui_widget_event_handler_reference
{
	unsigned long flags;
	short event_type;
	short function;
	struct { long index; } widget_tag;
};
struct ui_widget_definition
{
	char const *name;
	short type;
	short child_count;
	struct { long count; void *address; } event_handlers;
};

struct widget_instance
{
	long definition_tag_index;
	short type;
	short local_player_index;
	boolean delete_recursion_lock;
	boolean pause_game_time;
	struct widget_instance *parent, *child, *next, *previous, *focused_child;
	union
	{
		struct { wchar_t *text; } text_box;
		struct
		{
			void *list_items;
			short number_of_items;
			short selected_index;
			wchar_t *item_text;
			struct widget_instance *extended_description;
		} list;
	} parameters;
};

#include "under_test_structures.inc"

static struct
{
	boolean initialized;
	boolean sound_paused;
	long pause_game_time_count;
	struct widget_instance *active_widgets[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
	struct widget_stack_node *widget_stack[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
} widget_globals;

static struct { boolean coop; } online_games;

static struct ui_widget_definition screen_definition = { "screen", 0, 1, { 0, NULL } };
static struct ui_widget_definition list_definition = { "multiplayer level list", SPINNER_LIST, 3, { 0, NULL } };
static boolean we_are_at_the_main_menu;
static struct widget_instance *ui_widget_port_error_text_box;
static wchar_t const *ui_widget_port_error_text;
static void *widget_memory_pool;

static struct ui_widget_definition *ui_widget_definition_get(long tag_index)
{
	return tag_index == MAP_SELECT_TAG + 1 ? &list_definition : &screen_definition;
}
static long tag_loaded(long group, char const *name)
{
	(void)group;
	return !strcmp(name, ONLINE_GAMES_SCREEN) ? MULTIPLAYER_SCREEN_TAG : NONE;
}
static void player_control_inhibit_buttons(short local_player_index, unsigned long flags, boolean inhibit)
{
	(void)local_player_index; (void)flags; (void)inhibit;
}
static boolean ui_widget_event_handler_function_invoke(struct widget_instance *widget, struct event_record *event,
	short function, boolean *deleted)
{
	(void)widget; (void)event; (void)function; (void)deleted;
	return FALSE;
}
static boolean game_time_get_paused(void) { return FALSE; }
static void game_time_set_paused(boolean paused) { (void)paused; }
static void main_menu_ensure_player_queues_exist(void) {}
static void game_time_dispose_from_old_map(void) {}
static void game_time_initialize_for_new_map(void) {}
static void game_time_start(void) {}
static void sound_pause(boolean paused) { (void)paused; }
static void error(int kind, char const *format, ...) { (void)kind; (void)format; }
static void ui_mouse_forget_widget(struct widget_instance *widget) { (void)widget; }
static void ui_widget_port_deleted_handler_script(struct ui_widget_event_handler_reference const *handler) { (void)handler; }
static void dispose_pointer(void *pool, void *pointer) { (void)pool; free(pointer); }

/* ---- the map list */

#define MAXIMUM_MAP_LIST 16
#define MAP_NAME_LENGTH 64
#define DISPLAY_NAME_LENGTH 32
enum { _ui_map_list_source_all, _ui_map_list_source_xbox, _ui_map_list_source_ce,
	_ui_map_list_source_md, _ui_map_list_source_mcc, NUMBER_OF_UI_MAP_LIST_SOURCES };
enum { _ui_map_list_sort_default, _ui_map_list_sort_name, _ui_map_list_sort_recent,
	NUMBER_OF_UI_MAP_LIST_SORTS };
struct ui_map_entry { char map_name[MAP_NAME_LENGTH]; short source; long long added; };
static struct ui_map_entry ui_map_list[MAXIMUM_MAP_LIST];
static short ui_map_list_view[MAXIMUM_MAP_LIST];
static char *ui_map_list_names_array[MAXIMUM_MAP_LIST];
static long ui_map_list_count_value, ui_map_list_found_multiplayer, ui_map_list_found_campaign_end;
static long ui_map_list_multiplayer_count, ui_map_list_campaign_end;
#define ROW(row) (&ui_map_list[ui_map_list_view[row]])
static struct
{
	short source, sort;
	char search[48];
	unsigned long serial;
	boolean present[NUMBER_OF_UI_MAP_LIST_SOURCES];
	boolean coop_only;
} view;
static boolean view_mcc_offered = TRUE;
static char view_item[80];
static boolean view_item_only;
static boolean ui_map_list_mcc_offered(void) { return TRUE; }
static char const *mcc_maps_first_item(void) { return ""; }
static boolean mcc_maps_level_first(char const *level) { (void)level; return FALSE; }
static void view_entry_name(struct ui_map_entry const *entry, char *name, long size)
{
	snprintf(name, (size_t)size, "%s", entry->map_name);
}
static char const *view_package_text(char const *level) { (void)level; return ""; }
static long long view_entry_added(struct ui_map_entry *entry) { return entry->added; }
static void view_apply(void);

/* the map folders scanned (the real one reads every map's header) */
static long refreshes;

static void add(char const *name)
{
	struct ui_map_entry *entry = &ui_map_list[ui_map_list_count_value++];

	snprintf(entry->map_name, sizeof(entry->map_name), "%s", name);
	entry->source = _ui_map_list_source_xbox;
}

static void ui_map_list_refresh(char **xbox_levels)
{
	(void)xbox_levels;
	refreshes++;
	ui_map_list_count_value = 0;
	add("bloodgulch");
	add("sidewinder");
	ui_map_list_found_multiplayer = ui_map_list_count_value;
	add("levels\\a10\\a10");
	add("levels\\a30\\a30");
	ui_map_list_found_campaign_end = ui_map_list_count_value;
	view_apply();
}

/* ---- the rest of the game */

static struct { char *multiplayer_levels[13]; } event_handler_functions;
static boolean hosting;
static char chosen_map[MAP_NAME_LENGTH];

void ui_widget_port_xbox_map_search_reset(void) {}
char const *config_string(char const *name) { (void)name; return ""; }
static boolean pc_menus_host_cooperative_shown(void) { return hosting; }
static char const *main_get_solo_level_name(short level) { return level ? "levels\\a30\\a30" : "levels\\a10\\a10"; }
static boolean saved_game_file_retrieve_last_used_multiplayer_map(char *name) { (void)name; return FALSE; }
static void saved_game_file_remember_last_used_multiplayer_map(char const *name) { (void)name; }
static void *global_network_game_server_get(void) { return hosting ? &hosting : NULL; }
static boolean network_game_is_splitscreen_local(void) { return FALSE; }
static boolean cache_files_map_plays_multiplayer(char const *name, char *build)
{
	(void)name; (void)build;
	return TRUE;
}
static void cache_files_show_multiplayer_unavailable(char const *name, char const *build) { (void)name; (void)build; }
static boolean ui_widget_port_map_hostable(char const *name) { (void)name; return TRUE; }
static void main_set_multiplayer_map_name(char const *name) { snprintf(chosen_map, sizeof(chosen_map), "%s", name); }
static void game_engine_override_map_name(char const *name) { (void)name; }
static void network_game_server_change_map_name(void *server, char const *name) { (void)server; (void)name; }

static void ui_widget_delete_children_recursive(struct widget_instance *widget);
static struct widget_instance *ui_widget_launch_widget(struct widget_instance *widget, long tag_index)
{
	(void)widget; (void)tag_index;
	return NULL;
}
void ui_widget_delete(struct widget_instance *widget);
boolean ui_widget_port_map_select_over_coop(void);

#include "under_test.inc"

/* ---- the cases */

/* the Multiplayer menu in the first player's history, on an item */
static struct widget_stack_node history;

static void history_on(short item)
{
	history.data.previous_widget_tag = MULTIPLAYER_SCREEN_TAG;
	history.data.focused_child_parent_widget_tag = NONE;
	history.data.focused_child_index = item;
	history.data.local_player_index = NONE;
	history.next = NULL;
	widget_globals.widget_stack[0] = &history;
}

/* Select Map opened (its screen, the list in it), as the game loads it */
static struct widget_instance *select_map_open(void)
{
	struct widget_instance *screen = calloc(1, sizeof(*screen));
	struct widget_instance *list = calloc(1, sizeof(*list));
	boolean deleted = FALSE;

	screen->definition_tag_index = MAP_SELECT_TAG;
	screen->local_player_index = NONE;
	list->definition_tag_index = MAP_SELECT_TAG + 1;
	list->type = _ui_widget_type_spinner_list;
	list->local_player_index = NONE;
	list->parent = screen;
	screen->child = list;
	CHECK(multiplayer_level_list_initialize(list, NULL, &deleted), "the list didn't open");
	/* (its items aren't the pool's: let go as the game's do, with no warning) */
	list->parameters.list.list_items = NULL;
	return screen;
}

static struct widget_instance *select_map_list(struct widget_instance *screen)
{
	return screen->child;
}

static void check_levels_alone(struct widget_instance *screen, char const *when)
{
	struct widget_instance *list = select_map_list(screen);

	CHECK(ui_map_list_count() == 0, "%s: %ld multiplayer maps listed", when, ui_map_list_count());
	CHECK(list->parameters.list.number_of_items == 2, "%s: %d rows", when, list->parameters.list.number_of_items);
	CHECK(!strcmp(ui_map_list_names()[0], "levels\\a10\\a10") && ui_map_list_row_is_campaign(0),
		"%s: row 0 is %s", when, ui_map_list_names()[0]);
	CHECK(list->parameters.list.selected_index == 0, "%s: on row %d", when, list->parameters.list.selected_index);
}

/* the maps as the lobby and the Chupathingy menus read them each frame,
with no scan of the map folders */
static void check_no_scan(char const *when)
{
	char const *const *names;
	long before = refreshes, bloodgulch = NONE, a10 = NONE;
	short count = 0;
	int call;

	for (call = 0; call < 100; call++)
	{
		bloodgulch = ui_map_list_lookup("bloodgulch");
		a10 = ui_map_list_lookup("levels\\a10\\a10");
		count = ui_widget_port_multiplayer_maps(&names, NULL);
	}
	CHECK(refreshes == before, "%s: the map folders scanned %ld times in 100 frames", when, refreshes - before);
	CHECK(bloodgulch == 0 && a10 == 2, "%s: bloodgulch on row %ld, a10 on %ld", when, bloodgulch, a10);
	CHECK(count == 2 && !strcmp(names[1], "sidewinder"), "%s: the menus' map list has %d maps", when, count);
}

/* every map playable, as the Chupathingy menus' Select Map chooses it */
static void check_choose(char const *when)
{
	CHECK(ui_widget_port_multiplayer_map_choose(0) && !strcmp(chosen_map, "bloodgulch"),
		"%s: the first map refused", when);
	CHECK(ui_widget_port_multiplayer_map_choose(1) && !strcmp(chosen_map, "sidewinder"),
		"%s: the second map refused", when);
}

/* the maps as every other list has them */
static void check_maps_back(char const *when)
{
	check_no_scan(when);
	check_choose(when);
	CHECK(ui_map_list_count() == 2, "%s: %ld multiplayer maps listed", when, ui_map_list_count());
	CHECK(ui_map_list_count_with_campaign() == 4, "%s: %ld rows with the levels", when,
		ui_map_list_count_with_campaign());
}

int main(int argc, char **argv)
{
	char const *test = argc > 1 ? argv[1] : "";
	short coop_item = (short)COOP_ITEM_POSITION;
	struct widget_instance *screen;

	(void)ui_widget_port_error_text;
	widget_globals.initialized = TRUE;
	online_games.coop = TRUE;
	hosting = TRUE;
	ui_map_list_refresh(event_handler_functions.multiplayer_levels);
	if (!strcmp(test, "closed"))
	{
		/* (ONLINE CO-OP, then the list gone: B, a level chosen for GAME
		SETUP, the game started or ended, all close it) */
		history_on(coop_item);
		screen = select_map_open();
		check_levels_alone(screen, "co-op's Select Map");
		ui_widget_delete(screen);
		widget_globals.widget_stack[0] = NULL;
		check_maps_back("after co-op's Select Map");
		return 0;
	}
	if (!strcmp(test, "closed-choose"))
	{
		/* (a map chosen first thing after, in the Chupathingy menus) */
		history_on(coop_item);
		screen = select_map_open();
		ui_widget_delete(screen);
		widget_globals.widget_stack[0] = NULL;
		check_choose("after co-op's Select Map");
		return 0;
	}
	if (!strcmp(test, "list-alone"))
	{
		/* (the list itself deleted, its screen kept) */
		history_on(coop_item);
		screen = select_map_open();
		ui_widget_delete(select_map_list(screen));
		check_maps_back("co-op's list deleted");
		screen->child = NULL;
		ui_widget_delete(screen);
		return 0;
	}
	if (!strcmp(test, "back-from-game-setup"))
	{
		/* (GAME SETUP's B: Select Map made again over the same history) */
		history_on(coop_item);
		screen = select_map_open();
		ui_widget_delete(screen);
		screen = select_map_open();
		check_levels_alone(screen, "co-op's Select Map again");
		ui_widget_delete(screen);
		check_maps_back("after it");
		return 0;
	}
	if (!strcmp(test, "other-select-map"))
	{
		/* (Online Games' CREATE GAME: the maps, the levels after them) */
		history_on((short)ONLINE_GAMES_POSITION);
		screen = select_map_open();
		CHECK(select_map_list(screen)->parameters.list.number_of_items == 4, "Online Games' Select Map: %d rows",
			select_map_list(screen)->parameters.list.number_of_items);
		check_maps_back("Online Games' Select Map");
		ui_widget_delete(screen);
		/* (and not hosting: no co-op list, ONLINE CO-OP's history or not) */
		hosting = FALSE;
		history_on(coop_item);
		screen = select_map_open();
		CHECK(ui_map_list_count() == 2 && select_map_list(screen)->parameters.list.number_of_items == 2,
			"not hosting: %ld maps, %d rows", ui_map_list_count(), select_map_list(screen)->parameters.list.number_of_items);
		ui_widget_delete(screen);
		return 0;
	}
	fprintf(stderr, "no case %s\n", test);
	return 2;
}
