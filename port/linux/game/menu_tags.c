/*
MENU_TAGS.C

The menus written in XML (halo_menus.h, read by port/linux/src/menu_files.c),
built into the tags the game's widgets are made of when ui.map has loaded
(scenario_tags_load), and added to its tag table: a widget definition
('DeLa') for each <widget>, a unicode string list ('ustr') for each
<strings> and each widget's own text, and a bitmap group ('bitm') for each
<bitmap>, whose textures are drawn from their PNGs (menu_art_texture,
xbox_textures.c). The game's widgets then run them as their own:
main_screen_shell_load opens pc_menus_root_name()'s.

Each name the files use is looked up here: our widgets (named pc\<name>),
the map's tags (a path, with its backslashes), the game's event handler and
game data functions, and the port's own (menu_functions.c, from
PC_MENU_FUNCTION_BASE), which include the PC version's (most of which do
nothing yet). Its mouse events are kept, though this engine never sends
them (the port's mouse support clicks with A). Anything that does not resolve, or that the widget
code would stop the game for (a spinner's children, a list from strings
with children), is logged with its file and line and nothing is added: the
game keeps its own menus.

The tag table cannot grow where it is (the tags' names follow it), so it
is copied, with ours after it; every existing tag keeps its index. All of it
is let go in scenario_tags_unload, before the next map's tags load.

A campaign or multiplayer map gets them too, for its in-game pause menu's
SETTINGS (pause_patch): there the map's tags they name that it has not
(ui.map's pictures, sounds and screens) are none, a missing picture drawn
blank, and on a multiplayer map no screen pauses the game (a network game's
clock runs on).
*/

#include "cseries.h"
#include "bitmaps/bitmap_group.h"
#include "tag_files/tag_groups.h"
#include "text/text_group.h"
#include "rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.h"

#include "halo_menus.h"
#include "halo_keyboard.h"
#include "halo_map_packs.h"
#ifdef HALO_GAME_BROWSER
#include "../src/ui_overlay.h"
#ifdef HALO_GAME_BROWSER
#include "gametype_files.h"
#endif
#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* the platform layer's (port/linux/src) */
void platform_log(char const *format, ...);
char const *config_string(char const *name);
int platform_display_resolutions(long *widths, long *heights, int maximum);
int platform_window_sizes(long *widths, long *heights, int maximum);
int platform_audio_device_names(char const **names, int maximum);

/* the game's network server (this machine hosts: network_game_globals.c) */
void *global_network_game_server_get(void);
/* whether the pause menu has CHANGE TEAMS (team_change.c) */
boolean team_change_pause_offered(void);
int config_boolean(char const *name);

/* cache_files.c's (port) */
void *cache_files_tag_instances(long *count);
void cache_files_set_tag_instances(void *instances, long count);

/* the game's (port) */
char *tag_get_name(long tag_index);
char const *ui_widget_event_handler_function_name(long function_index);

void menu_tags_loaded(char const *map_name);
void menu_tags_unloaded(void);
char const *pc_menus_root_name(void);
char const *pc_menu_function_name(long function_index);
char const *pc_menu_game_data_input_name(long function_index);
boolean pc_menu_tag(long tag_index);
boolean pc_menu_frame_placement(struct bitmap_data const *bitmap, short *x, short *y, short *width, short *height,
	short *texel_y);

/* ---------- constants */

#define PC_MENU_TAG_PREFIX "pc\\"
/* the Chupathingy menus' main menu (port/assets/menus/chupathingy) */
#define CHUPATHINGY_ROOT "chupathingy/main_menu"
/* (the Chupathingy menus' prompts' buttons, by their tags' names) */
#define CHUPATHINGY_PROMPTS PC_MENU_TAG_PREFIX "chupathingy\\prompts\\"
/* the multiplayer maps' widget collection, whose pause screens pause_patch
adds to */
#define MULTIPLAYER_COLLECTION "ui\\shell\\multiplayer"
/* a campaign map's pause screen, which pause_patch adds to */
#define SOLO_PAUSE_SCREEN "ui\\shell\\solo_game\\pause_game\\pause_game"
/* the tags pause_patch adds: four buttons and their texts, and the middle
and bottom parts of the stock box's pieces, a widget and a bitmap each (the
campaign's has six pieces) */
#define PAUSE_PATCH_TAGS (8 + 4 * 6)
/* what a frame of ui.map's is drawn as in a game map, which has not got it */
#define BLANK_PNG "ce/port/blank__0.png"
#define UI_WIDGET_DEFINITION_TAG 'DeLa'
#define UNICODE_STRING_LIST_TAG 'ustr'
#define FONT_TAG 'font'
#define SOUND_TAG 'snd!'
#define MAXIMUM_STRINGS 64
#define MAXIMUM_TEXT 2048
/* the size of a menu bitmap's D3D texture, which only stands for its PNG */
#define PLACEHOLDER_SIZE 4

enum
{
	_bitmap_type_2d = 0,
	_bitmap_format_a8r8g8b8 = 11,
	_bitmap_has_power_of_two_dimensions_bit = 0,
};

enum
{
	_event_handler_close_current_widget_bit = 0,
	_event_handler_close_other_widget_bit,
	_event_handler_close_all_widgets_bit,
	_event_handler_open_widget_bit,
	_event_handler_reload_self_bit,
	_event_handler_reload_other_widget_bit,
	_event_handler_give_focus_to_widget_bit,
	_event_handler_run_function_bit,
	_event_handler_replace_self_with_widget_bit,
	_event_handler_go_back_to_previous_widget_bit,
	_event_handler_run_scenario_script_bit,
	_event_handler_try_to_branch_on_failure_bit,
};

enum
{
	_widget_type_container,
	_widget_type_text_box,
	_widget_type_spinner_list,
	_widget_type_column_list,
};

enum
{
	_list_items_generated_from_string_list_tag_bit = 1,
};

enum
{
	_child_widget_use_custom_controller_index_bit = 0,
};

/* ---------- structures */

struct cache_file_tag_instance
{
	long group_tag;
	long parent_group_tags[2];
	long tag_index;
	/* Xbox addresses (as in cache_files.c) */
	XPTR(char) name;
	XPTR(void) base_address;
	unsigned long unused[2];
};

typedef char verify_cache_file_tag_instance_size[
	sizeof(struct cache_file_tag_instance) == 0x20 ? 1 : -1];

/* the widget definition, as ui_widget.c has it */
struct ui_widget_event_handler_reference
{
	long flags;
	short event_type;
	short function;
	struct tag_reference widget_tag;
	struct tag_reference sound_effect;
	char script[32];
};

struct ui_widget_child_reference
{
	struct tag_reference widget_tag;
	char name[32];
	long flags;
	short custom_controller_index;
	short vertical_offset;
	short horizontal_offset;
	byte unknown03A[0x50 - 0x3A];
};

struct ui_widget_conditional_reference
{
	struct tag_reference widget_tag;
	char name[32];
	long flags;
	short custom_controller_index;
	byte unknown036[0x50 - 0x36];
};

struct ui_widget_search_and_replace_reference
{
	char search_string[32];
	short replace_function;
};

struct ui_widget_game_data_input_reference
{
	short function;
	byte unknown002[0x24 - 0x02];
};

struct ui_widget_definition
{
	short type;
	short controller_index;
	char name[32];
	rectangle2d bounds;
	long flags;
	long milliseconds_to_auto_close;
	long auto_close_fade_time;
	struct tag_reference background_bitmap;
	struct tag_block game_data_inputs;
	struct tag_block event_handlers;
	struct tag_block search_and_replace_functions;
	byte unknown06C[0xEC - 0x6C];
	struct tag_reference text_label_string_list;
	struct tag_reference text_font;
	real_argb_color text_color;
	short justification;
	word text_box_flags;
	byte unknown120[0x12E - 0x120];
	short string_list_index;
	short horizontal_offset;
	short vertical_offset;
	byte unknown134[0x150 - 0x134];
	long list_flags;
	struct tag_reference list_header_bitmap;
	struct tag_reference list_footer_bitmap;
	rectangle2d list_header_bounds;
	rectangle2d list_footer_bounds;
	byte unknown184[0x1A4 - 0x184];
	struct tag_reference extended_description_widget;
	byte unknown1B4[0x2D4 - 0x1B4];
	struct tag_block conditional_widgets;
	byte unknown2E0[0x3E0 - 0x2E0];
	struct tag_block child_widgets;
};

typedef char verify_ui_widget_event_handler_reference_size[
	sizeof(struct ui_widget_event_handler_reference) == 0x48 ? 1 : -1];
typedef char verify_ui_widget_child_reference_size[
	sizeof(struct ui_widget_child_reference) == 0x50 ? 1 : -1];
typedef char verify_ui_widget_conditional_reference_size[
	sizeof(struct ui_widget_conditional_reference) == 0x50 ? 1 : -1];
typedef char verify_ui_widget_search_and_replace_reference_size[
	sizeof(struct ui_widget_search_and_replace_reference) == 0x22 ? 1 : -1];
typedef char verify_ui_widget_game_data_input_reference_size[
	sizeof(struct ui_widget_game_data_input_reference) == 0x24 ? 1 : -1];
typedef char verify_ui_widget_definition_text_color_offset[
	offsetof(struct ui_widget_definition, text_color) == 0x10C ? 1 : -1];
typedef char verify_ui_widget_definition_list_flags_offset[
	offsetof(struct ui_widget_definition, list_flags) == 0x150 ? 1 : -1];
typedef char verify_ui_widget_definition_conditional_widgets_offset[
	offsetof(struct ui_widget_definition, conditional_widgets) == 0x2D4 ? 1 : -1];
typedef char verify_ui_widget_definition_size[
	sizeof(struct ui_widget_definition) == 0x3EC ? 1 : -1];

/* a frame of ui.map's that a menu file scales: where in its widget, and at
what size, it is drawn, from which of its rows (pc_menu_frame_placement) */
struct menu_frame_placement
{
	struct bitmap_data const *bitmap;
	short x, y;
	short width, height;
	short texel_y;
};

/* a spinner bound to a setting (menu_functions.c) */
struct pc_menu_setting
{
	long definition_index;
	char const *setting;
	long value_count;
	char const *values[MAXIMUM_STRINGS];
	short loaded_index;
};

/* ---------- globals */

/* the names the files may use, by their values */
static char const *const event_names[] =
{
	"a", "b", "x", "y", "black", "white", "left_trigger", "right_trigger",
	"up", "down", "left", "right", "start", "back", "left_thumb", "right_thumb",
	"stick_up", "stick_down", "stick_left", "stick_right",
	"right_stick_up", "right_stick_down", "right_stick_left", "right_stick_right",
	"created", "deleted",
	/* (the PC version's, which this engine never sends) */
	"get_focus", "lose_focus", "left_mouse", "middle_mouse", "right_mouse", "double_click", "custom_activation",
	"post_render",
};

static char const *const widget_flag_names[] =
{
	"pass_unhandled_to_focused_child", "pause_game", "flash_bitmap", "up_down_tabs_children",
	"left_right_tabs_children", "up_down_tabs_items", "left_right_tabs_items", "no_focused_child",
	"pass_unhandled_to_all_children", "render_any_controller", "pass_handled_to_all_children",
	"main_menu_if_no_history", "tag_controller_index", "nifty_fx", "no_history",
	/* (the PC version's, which this engine has no use for) */
	"force_handle_mouse", "no_widescreen_fill",
};

static char const *const widget_type_names[] = { "container", "text", "spinner", "column_list" };
static char const *const list_flag_names[] = { "items_in_code", "items_from_strings", "one_tooltip", "single_preview" };
static char const *const text_flag_names[] = { "editable", "password", "flashing", "no_focus_test" };
static char const *const align_names[] = { "left", "right", "center" };
/* (build_number and pid are the PC version's, which this engine has not) */
static char const *const replace_names[] = { "none", "controller", "build_number", "pid" };

/* the game data functions, as the tags name them
(ui_widget_game_data_input_functions.c) */
static char const *const game_data_input_names[] =
{
	"NULL", "player settings menu update desc", "unused", "playlist settings menu update desc",
	"gametype select menu update desc", "multiplayer type menu update desc", "solo level select update",
	"difficulty menu update desc", "build number textbox only", "server list update",
	"network pregame status update", "splitscreen pregame status update", "net splitscreen prejoin players",
	"mp profile list update", "3wide player profile list update", "plyr prof edit select menu upd8",
	"player profile small menu update", "game settings lists text update", "solo game objective text",
	"color picker update", "game settings lists pic update", "main menu fake animate",
	"mp level select update", "get active plyr profile name", "get edit plyr profile name",
	"get edit game settings name", "get active plyr profile color", "mp set textbox map name",
	"mp set textbox game ruleset", "mp set textbox teams noteams", "mp set textbox score limit",
	"mp set textbox score limit type", "mp set bitmap for map", "mp set bitmap for ruleset",
	"mp set textbox", "mp edit profile set rule text", "system link status check", "mp game directions",
	"teams no teams bitmap update", "warn if diff will nuke saved game", "dim if no net cable",
};

/* the port's event handler functions (menu_functions.c), from
PC_MENU_FUNCTION_BASE: its own, then the PC version's that the Xbox's have
not (as its tags name them), then its own of the Xbox's names, which our
widgets run instead of the Xbox's (those take the Xbox's widgets: a spinner
of levels, the difficulty list itself) */
static char const *const port_function_names[] =
{
	"port quit game", "port setting load", "port setting save", "unwired",
	"exit gracefully to xbox dashboard", "pause game invert pitch", "start new coop game",
	"pause game invert spinner get", "pause game invert spinner set", "main menu quit game",
	"mouse emit accept event", "mouse emit back event", "mouse emit dpad left event",
	"mouse emit dpad right event", "mouse spinner 3wide click", "controls screen init", "video screen init",
	"controls begin binding", "gamespy screen init", "gamespy screen dispose", "gamespy select header",
	"gamespy select item", "gamespy select button", "plr prof init mouse set", "plr prof change mouse set",
	"plr prof init audio set", "plr prof change audio set", "plr prof change video set",
	"controls screen dispose", "controls screen change set", "mouse emit x event", "gamepad screen init",
	"gamepad screen dispose", "gamepad screen change gamepads", "gamepad screen select item",
	"mouse screen defaults", "audio screen defaults", "video screen defaults", "controls screen defaults",
	"profile set edit begin", "profile manager delete", "profile manager select", "gamespy dismiss error",
	"server settings init", "ss edit server name", "ss edit server password", "ss start game",
	"video test dialog init", "video test dialog dispose", "video test dialog accept",
	"gamespy dismiss filters", "gamespy update filter settings", "gamespy back handler",
	"mouse spinner 1wide click", "controls back handler", "controls advanced launch", "controls advanced ok",
	"mp pause menu open", "mp game options open", "mp choose team", "mp prof init vehicle options",
	"mp prof save vehicle options", "single prev cl item activated", "mp prof init teamplay options",
	"mp prof save teamplay options", "mp game options choose", "emit custom activation event",
	"plr prof cancel audio set", "plr prof init network options", "plr prof save network options",
	"difficulty item select",
	"gamespy get patch", "video screen dispose", "campaign menu init", "campaign menu continue",
	"load game menu init", "load game menu dispose", "load game menu activated", "solo menu save checkpoint",
	"mp type set mode", "direct ip connect init",
	"direct ip connect go", "direct ip edit field", "network settings edit a port", "network settings defaults",
	"load game menu delete request", "load game menu delete finish",
	"initialize sp level list solo", "dispose sp level list", "solo level set map", "set difficulty",
	"port settings save", "port settings defaults", "controls binding slot",
	"color picker menu initialize", "color picker menu dispose", "color picker select color",
	"player profile list initialize", "player profile list dispose", "request del player profile",
	"final del player profile",
	"ss copy invite", "join controller to mp game", "mp level list initialize", "mp level list dispose",
	"mp level select", "mp profiles list initialize", "mp profiles list dispose", "mp profile set for game",
	"port lobby preview join",
	"port setup edit",
	"port map list back",
	"port pause end game",
	"port pause change teams",
	"port pause controls", "controller screen init", "controller screen defaults", "controller begin binding",
	"port coop begin", "port coop player 2 list initialize", "port coop player 2",
	"port lobby open", "port lobby add player", "port lobby join", "port lobby leave",
	"port lobby player list initialize", "port lobby player choose",
	"port lobby preview add", "port lobby preview leave",
	/* (the gametype editor's: the Xbox's walk their rows by place, which the
	PC version's screens changed) */
	"mp profile begin editing", "mp profile save changes", "request del playlist profile", "final del playlist profile",
	"mp profile init game engine", "mp profile set game engine", "mp profile init ctf rules",
	"mp profile init koth rules", "mp profile init slayer rules", "mp profile init oddball rules",
	"mp profile init racing rules", "mp profile init player opts", "mp profile init item options",
	"mp profile init indicator opts", "mp profile set ctf rules", "mp profile set koth rules",
	"mp profile set slayer rules", "mp profile set oddball rules", "mp profile set racing rules",
	"mp profile set player options", "mp profile set item options", "mp profile set indicator opts",
	/* (Settings' OK: the Xbox's fails when the profile has no changes, the
	settings' screens having written theirs to config.toml) */
	"player profile save changes",
	/* (the server browser's password screen) */
	"port password init", "port password edit", "port password join", "port password back",
	/* (the Chupathingy menus' GAME SETUP and game type editor) */
	"port host init", "port host row a", "port host row left", "port host row right", "port host defaults",
	"port host edit", "port host back",
	/* (Delta Profile: its sections' A, B, Y and X, and Gearbox's Select
	Profile's DELTA) */
	"port delta profile choose", "port delta profile back", "port delta profile refresh", "port delta profile copies",
	"port delta profile open",
	/* (the settings screens' nested lists, and Controls Setup's) */
	"port settings views init", "port settings view", "port settings back", "port settings accept",
	"port controls init", "port controls defaults", "port controls accept", "port controls closed",
};

/* the PC version's game data functions that the Xbox's have not, from
PC_MENU_FUNCTION_BASE (menu_functions.c) */
static char const *const port_game_data_input_names[] =
{
	"unwired",
	"pause game set textbox inverted", "dim unless two controllers", "controls update menu",
	"video menu update", "gamespy screen update", "common button bar update", "gamepad update menu",
	"server settings update", "audio menu update", "mp prof vehicles update", "solo map list update",
	"mp map list update", "gt select list update", "gt edit list update", "load game list update",
	"direct ip connect update", "network settings update",
	"port settings help", "controller update menu", "color picker update", "3wide player profile list update",
	"port lobby update", "port lobby preview update",
	/* (the gametype editor's: the Xbox's stops the game on the buttons'
	row; the Xbox's read only player_ui's gametype, not Server Setup's) */
	"game settings lists text update", "get edit game settings name", "mp edit profile set rule text",
	"port password update",
	/* (the profile settings' picture: the Xbox's of the button settings,
	on CONTROLS' row) */
	"port gamepad layout preview",
	/* (the Chupathingy menus' settings picture: the same, on its own frames) */
	"chupathingy settings picture",
	/* (its GAME SETUP's and game type editor's rows) */
	"port host update",
	/* (Delta Profile's sections: menu_functions.c) */
	"port delta profile update",
	/* (Controls Setup's rows and help) */
	"port controls update",
};

static struct
{
	/* what is let go when the map unloads */
	void **blocks;
	long block_count;
	struct bitmap_data **bitmaps;
	long bitmap_count;
	struct menu_frame_placement *placements;
	long placement_count;
	struct cache_file_tag_instance *original_instances;
	long original_count;
	struct pc_menu_setting *settings;
	long setting_count;
	boolean loaded;
	/* (display.menus "chupathingy": its main menu, the Xbox's screens after a
	network game) */
	boolean chupathingy;
	char root[300];
#ifdef HALO_64BIT
	/* the name of an empty reference, inside the Xbox address space as the
	map's names are */
	char *empty_name;
#endif
} menu_tags;

/* the build under way */
static struct
{
	struct halo_menus const *menus;
	long first_index;
	long first_salt;
	long next;
	/* each widget's, bitmap's and string list's tag index, and each
	widget's own text's and strings' (NONE: none) */
	long *widget_tags;
	long *bitmap_tags;
	long *strings_tags;
	long *text_tags;
	long *spinner_tags;
	boolean failed;
	/* a game map's build (the pause menu's SETTINGS): what the map has not
	got is none, not a failure */
	boolean game_map;
	/* a campaign map's (its pause screen's): the game stays paused */
	boolean campaign_map;
} build;

/* ---------- private code */

/* (the game's allocator, which malloc and free are here: cseries.h) */
static void *allocate(long size)
{
	void *block = malloc(size > 0 ? size : 1);

	if (!block)
	{
		build.failed = TRUE;
		return NULL;
	}
	memset(block, 0, size > 0 ? size : 1);
	{
		void **blocks = realloc(menu_tags.blocks, (menu_tags.block_count + 1) * sizeof(*menu_tags.blocks));

		if (!blocks)
		{
			free(block);
			build.failed = TRUE;
			return NULL;
		}
		menu_tags.blocks = blocks;
	}
	menu_tags.blocks[menu_tags.block_count++] = block;
	return block;
}

static void problem(char const *file, long line, char const *message, char const *detail)
{
	if (!build.failed)
		halo_menus_log(file, line, message, detail);
	build.failed = TRUE;
}

static long name_index(char const *name, char const *const *names, long count)
{
	long index;

	for (index = 0; name && index < count; index++)
	{
		if (!strcmp(name, names[index]))
			return index;
	}
	return NONE;
}

/* the bits of the space-separated names */
static long flags_parse(char const *text, char const *const *names, long count, char const *file, long line)
{
	long flags = 0;

	while (text && *text)
	{
		char word[64];
		long length, bit;

		text += strspn(text, " \t\r\n");
		length = (long)strcspn(text, " \t\r\n");
		if (!length)
			break;
		if (length >= (long)sizeof(word))
			length = sizeof(word) - 1;
		memcpy(word, text, length);
		word[length] = 0;
		text += strcspn(text, " \t\r\n");
		bit = name_index(word, names, count);
		if (bit == NONE)
			problem(file, line, "there is no flag", word);
		else
			flags |= 1L << bit;
	}
	return flags;
}

static void reference_clear(struct tag_reference *reference, long group_tag)
{
	reference->group_tag = group_tag;
#ifdef HALO_64BIT
	if (!menu_tags.empty_name)
		menu_tags.empty_name = allocate(1);
	reference->name = XBOX_ADDRESS(menu_tags.empty_name);
#else
	reference->name = "";
#endif
	reference->name_length = 0;
	reference->index = NONE;
}

static void reference_set(struct tag_reference *reference, long group_tag, long tag_index)
{
	if (tag_index == NONE)
	{
		reference_clear(reference, group_tag);
		return;
	}
	reference->group_tag = group_tag;
	reference->name = XBOX_ADDRESS(tag_get_name(tag_index));
	reference->name_length = (long)strlen(xbox_pointer(reference->name));
	reference->index = tag_index;
}

static long find(char const *name, long count, char const *(*name_of)(long))
{
	long index;

	for (index = 0; name && index < count; index++)
	{
		if (!strcmp(name_of(index), name))
			return index;
	}
	return NONE;
}

static char const *widget_name(long index) { return build.menus->widgets[index].name; }
static char const *bitmap_name(long index) { return build.menus->bitmaps[index].name; }
static char const *strings_name(long index) { return build.menus->string_lists[index].name; }

static long widget_named(char const *name)
{
	return find(name, build.menus->widget_count, widget_name);
}

static long map_tag(long group_tag, char const *name, char const *file, long line)
{
	long index = tag_loaded(group_tag, name);

	/* (a game map has few of ui.map's: those are none) */
	if (index == NONE && !build.game_map)
		problem(file, line, "the map has no tag", name);
	return index;
}

/* a tag: the map's by its path (with a backslash), else one of ours */
static long tag_named(long group_tag, char const *name, char const *file, long line)
{
	long index = NONE;

	if (strchr(name, '\\'))
		return map_tag(group_tag, name, file, line);
	switch (group_tag)
	{
	case UI_WIDGET_DEFINITION_TAG:
		index = widget_named(name);
		index = index == NONE ? NONE : build.widget_tags[index];
		break;
	case BITMAP_GROUP_TAG:
		index = find(name, build.menus->bitmap_count, bitmap_name);
		index = index == NONE ? NONE : build.bitmap_tags[index];
		break;
	case UNICODE_STRING_LIST_TAG:
		index = find(name, build.menus->string_list_count, strings_name);
		index = index == NONE ? NONE : build.strings_tags[index];
		break;
	}
	if (index == NONE)
		problem(file, line, "there is nothing named", name);
	return index;
}

/* "unwired <name>": a function the PC version has for these widgets, not
yet written for this engine (port/assets/menus/UNWIRED.md), which does
nothing */
static boolean unwired(char const *name)
{
	return !strncmp(name, "unwired ", 8);
}

static long function_index(char const *name, char const *file, long line)
{
	char const *function_name;
	long index;

	if (unwired(name))
		name = "unwired";
	index = name_index(name, port_function_names, NUMBEROF(port_function_names));
	if (index != NONE)
		return PC_MENU_FUNCTION_BASE + index;
	for (index = 0; (function_name = ui_widget_event_handler_function_name(index)) != NULL; index++)
	{
		if (!strcmp(name, function_name))
			return index;
	}
	problem(file, line, "there is no event handler function", name);
	return NONE;
}

static long game_data_input_index(char const *name, char const *file, long line)
{
	long index;

	if (unwired(name))
		name = "unwired";
	/* (ours first, as for the event handlers) */
	index = name_index(name, port_game_data_input_names, NUMBEROF(port_game_data_input_names));
	if (index != NONE)
		return PC_MENU_FUNCTION_BASE + index;
	index = name_index(name, game_data_input_names, NUMBEROF(game_data_input_names));
	if (index != NONE)
		return index;
	problem(file, line, "there is no game data input", name);
	return NONE;
}

static long hex_digit(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return NONE;
}

/* "#RRGGBB" or "#AARRGGBB" */
static boolean parse_color(char const *text, real_argb_color *color)
{
	long length = text ? (long)strlen(text) : 0;
	long components[4] = { 255, 0, 0, 0 };
	long index;

	if ((length != 7 && length != 9) || text[0] != '#')
		return FALSE;
	for (index = 0; index < (length - 1) / 2; index++)
	{
		long high = hex_digit(text[1 + 2 * index]), low = hex_digit(text[2 + 2 * index]);

		if (high == NONE || low == NONE)
			return FALSE;
		components[length == 7 ? index + 1 : index] = high * 16 + low;
	}
	color->alpha = components[0] / 255.0f;
	color->red = components[1] / 255.0f;
	color->green = components[2] / 255.0f;
	color->blue = components[3] / 255.0f;
	return TRUE;
}

/* "top left bottom right" */
static void parse_bounds(char const *text, rectangle2d *bounds, char const *file, long line)
{
	long top, left, bottom, right;
	char extra;

	if (text && sscanf(text, "%ld %ld %ld %ld %c", &top, &left, &bottom, &right, &extra) == 4)
	{
		bounds->y0 = (short)top;
		bounds->x0 = (short)left;
		bounds->y1 = (short)bottom;
		bounds->x1 = (short)right;
	}
	else if (text)
	{
		problem(file, line, "bounds are \"top left bottom right\":", text);
	}
}

/* a 'ustr' of the strings */
static void *string_list_build(char const *const *strings, long count)
{
	struct string_list *list = allocate(sizeof(struct string_list));
	struct string_list_entry *entries = allocate(count * sizeof(struct string_list_entry));
	long index;

	if (!list || !entries)
		return list;
	list->strings.count = count;
	list->strings.address = XBOX_ADDRESS(entries);
	for (index = 0; index < count; index++)
	{
		long length = (long)strlen(strings[index]);
		unsigned short *characters = allocate((length * 2 + 2) * sizeof(unsigned short));
		long written;

		if (!characters)
			return list;
		written = halo_menus_utf16(strings[index], characters, length * 2 + 2);
		entries[index].string.size = written * (long)sizeof(unsigned short);
		entries[index].string.address = XBOX_ADDRESS(characters);
	}
	return list;
}

/* the "|"-separated pieces of text, copied (at most MAXIMUM_STRINGS) */
static long split(char const *text, char const **pieces)
{
	long count = 0;

	while (text && count < MAXIMUM_STRINGS)
	{
		long length = (long)strcspn(text, "|");
		char *piece = allocate(length + 1);

		if (!piece)
			break;
		memcpy(piece, text, length);
		pieces[count++] = piece;
		if (!text[length])
			break;
		text += length + 1;
	}
	return count;
}

/* a spinner's strings, or its values, split: its own, but Video Setup's
sizes are the display's (sdl_platform.c), shown "1920 x 1080" and set
"1920x1080": its resolutions after Resolution's own (NATIVE), and the
window sizes that fit it in place of Window Size's own; Audio Setup's
Output Device has the system's outputs after its own (DEFAULT), by name
(dsound_sdl.c) */
static long spinner_split(struct halo_menu_widget const *widget, boolean values, char const **pieces)
{
	long count = split(values ? widget->values : widget->strings, pieces);
	long widths[MAXIMUM_STRINGS], heights[MAXIMUM_STRINGS];
	long added, index;

	if (widget->setting && !strcmp(widget->setting, "audio.device"))
	{
		char const *names[MAXIMUM_STRINGS];

		added = platform_audio_device_names(names, (int)(MAXIMUM_STRINGS - count));
		for (index = 0; index < added; index++)
		{
			char *piece = allocate((long)strlen(names[index]) + 4);

			if (!piece)
				break;
			strcpy(piece, names[index]);
			/* (shown cut to the spinner's width: 24 letters) */
			if (!values && strlen(piece) > 24)
				strcpy(piece + 21, "...");
			pieces[count++] = piece;
		}
		return count;
	}
	if (widget->setting && !strcmp(widget->setting, "display.resolution"))
		added = platform_display_resolutions(widths, heights, (int)(MAXIMUM_STRINGS - count));
	else if (widget->setting && !strcmp(widget->setting, "display.window_size"))
	{
		added = platform_window_sizes(widths, heights, MAXIMUM_STRINGS);
		if (added > 0)
			count = 0;
	}
	else
		return count;
	for (index = 0; index < added; index++)
	{
		char text[32];
		char *piece;

		snprintf(text, sizeof(text), values ? "%ldx%ld" : "%ld x %ld", widths[index], heights[index]);
		piece = allocate((long)strlen(text) + 1);
		if (!piece)
			break;
		strcpy(piece, text);
		pieces[count++] = piece;
	}
	return count;
}

static void *bitmap_build(struct halo_menu_bitmap const *source, long tag_index)
{
	struct bitmap_group *group = allocate(sizeof(struct bitmap_group));
	struct bitmap_group_sequence *sequence = allocate(sizeof(struct bitmap_group_sequence));
	struct bitmap_data *bitmaps = allocate(source->frame_count * sizeof(struct bitmap_data));
	long frame;

	if (build.failed)
		return group;
	if (!source->frame_count)
	{
		problem(source->file, source->line, "a bitmap has no frames:", source->name);
		return group;
	}
	sequence->first_bitmap_index = 0;
	sequence->bitmap_count = (short)source->frame_count;
	group->sequences.count = 1;
	group->sequences.address = XBOX_ADDRESS(sequence);
	group->bitmaps.count = source->frame_count;
	group->bitmaps.address = XBOX_ADDRESS(bitmaps);
	for (frame = 0; frame < source->frame_count; frame++)
	{
		struct halo_menu_frame const *data = &build.menus->frames[source->first_frame + frame];
		struct bitmap_data *bitmap = &bitmaps[frame];
		char const *png = data->png;
		long width = data->width, height = data->height;

		/* (a frame of the map's: a copy of its bitmap, which the texture cache
		loads from the map as its own; in a game map that has not got it,
		nothing) */
		if (data->map && build.game_map && tag_loaded(BITMAP_GROUP_TAG, data->map) == NONE)
		{
			png = BLANK_PNG;
			width = height = PLACEHOLDER_SIZE;
		}
		else if (data->map)
		{
			long group_index = map_tag(BITMAP_GROUP_TAG, data->map, source->file, source->line);
			struct bitmap_group *group_source;

			if (group_index == NONE)
				return group;
			group_source = bitmap_group_get(group_index);
			if (data->index < 0 || data->index >= group_source->bitmaps.count)
			{
				problem(source->file, source->line, "the map's bitmap has no such frame:", data->map);
				return group;
			}
			memcpy(bitmap, XBOX_POINTER(struct bitmap_data, group_source->bitmaps.address) + data->index, sizeof(*bitmap));
			bitmap->cache_block_index = NONE;
			bitmap->base_address = XBOX_NULL;
			/* (scaled to a size of the file's: the texture stays the map's,
			drawn smaller or larger, ui_widget.c) */
			if (data->width > 0 && data->height > 0 && data->width <= 2048 && data->height <= 2048)
			{
				struct menu_frame_placement *placements = realloc(menu_tags.placements,
					(menu_tags.placement_count + 1) * sizeof(*menu_tags.placements));

				if (placements)
				{
					menu_tags.placements = placements;
					placements[menu_tags.placement_count].bitmap = bitmap;
					placements[menu_tags.placement_count].x = (short)PIN(data->x, -2048, 2048);
					placements[menu_tags.placement_count].y = (short)PIN(data->y, -2048, 2048);
					placements[menu_tags.placement_count].width = (short)data->width;
					placements[menu_tags.placement_count].height = (short)data->height;
					placements[menu_tags.placement_count].texel_y = 0;
					menu_tags.placement_count++;
				}
			}
			continue;
		}

		if (width <= 0 || height <= 0 || width > 2048 || height > 2048)
		{
			problem(source->file, source->line, "a frame is 1 to 2048 wide and high:", png);
			return group;
		}
		/* (its D3D texture is a small one, which its PNG is drawn for: the
		game draws a bitmap in units of its size, to whatever texture it has) */
		bitmap->signature = BITMAP_GROUP_TAG;
		bitmap->width = PLACEHOLDER_SIZE;
		bitmap->height = PLACEHOLDER_SIZE;
		bitmap->depth = 1;
		bitmap->type = _bitmap_type_2d;
		bitmap->format = _bitmap_format_a8r8g8b8;
		bitmap->flags = FLAG(_bitmap_has_power_of_two_dimensions_bit);
		bitmap->tag_index = tag_index;
		bitmap->cache_block_index = NONE;
		if (!rasterizer_bitmap_new(bitmap))
		{
			problem(source->file, source->line, "could not make the texture of bitmap", source->name);
			return group;
		}
		bitmap->width = (short)width;
		bitmap->height = (short)height;
		menu_tags.bitmaps = realloc(menu_tags.bitmaps, (menu_tags.bitmap_count + 1) * sizeof(*menu_tags.bitmaps));
		menu_tags.bitmaps[menu_tags.bitmap_count++] = bitmap;
		/* (none without a renderer: debug.null_renderer) */
		if (bitmap->hardware_format)
			halo_menus_art_register(xbox_pointer(bitmap->hardware_format), png);
	}
	return group;
}

/* adds a conditional widget to the definition, if it has not got it */
static void conditional_add(struct ui_widget_definition *definition, long tag_index, long flags)
{
	struct ui_widget_conditional_reference *conditionals =
		XBOX_POINTER(struct ui_widget_conditional_reference, definition->conditional_widgets.address);
	struct ui_widget_conditional_reference *grown;
	long count = definition->conditional_widgets.count, index;

	for (index = 0; index < count; index++)
	{
		if (conditionals[index].widget_tag.index == tag_index)
			return;
	}
	grown = allocate((count + 1) * sizeof(*grown));
	if (!grown)
		return;
	/* (the game's memcpy asserts on a NULL source, even for nothing) */
	if (count)
		memcpy(grown, conditionals, count * sizeof(*grown));
	reference_set(&grown[count].widget_tag, UI_WIDGET_DEFINITION_TAG, tag_index);
	grown[count].flags = flags;
	definition->conditional_widgets.address = XBOX_ADDRESS(grown);
	definition->conditional_widgets.count = count + 1;
}

static void handler_build(struct ui_widget_event_handler_reference *handler, struct halo_menu_handler const *source,
	long event_type, struct ui_widget_definition *definition)
{
	char const *targets[5];
	long target_count = 0;

	handler->event_type = (short)event_type;
	reference_clear(&handler->widget_tag, UI_WIDGET_DEFINITION_TAG);
	reference_clear(&handler->sound_effect, SOUND_TAG);
	if (source->run)
	{
		handler->function = (short)function_index(source->run, source->file, source->line);
		SET_FLAG(handler->flags, _event_handler_run_function_bit, TRUE);
	}
	if (source->script)
	{
		strncpy(handler->script, source->script, sizeof(handler->script) - 1);
		SET_FLAG(handler->flags, _event_handler_run_scenario_script_bit, TRUE);
	}
	else if (source->label)
	{
		strncpy(handler->script, source->label, sizeof(handler->script) - 1);
	}
	if (source->open)
	{
		targets[target_count++] = source->open;
		SET_FLAG(handler->flags, _event_handler_open_widget_bit, TRUE);
	}
	if (source->replace)
	{
		targets[target_count++] = source->replace;
		SET_FLAG(handler->flags, _event_handler_replace_self_with_widget_bit, TRUE);
	}
	if (source->focus)
	{
		targets[target_count++] = source->focus;
		SET_FLAG(handler->flags, _event_handler_give_focus_to_widget_bit, TRUE);
	}
	if (source->back)
		SET_FLAG(handler->flags, _event_handler_go_back_to_previous_widget_bit, TRUE);
	if (source->branch)
		SET_FLAG(handler->flags, _event_handler_try_to_branch_on_failure_bit, TRUE);
	if (source->close)
	{
		if (!strcmp(source->close, "current"))
			SET_FLAG(handler->flags, _event_handler_close_current_widget_bit, TRUE);
		else if (!strcmp(source->close, "all"))
			SET_FLAG(handler->flags, _event_handler_close_all_widgets_bit, TRUE);
		else if (!strcmp(source->close, "other") && source->widget)
			SET_FLAG(handler->flags, _event_handler_close_other_widget_bit, TRUE);
		else
			problem(source->file, source->line, "close is \"current\", \"all\" or \"other\" (with widget=)",
				source->close);
	}
	if (source->reload)
	{
		if (!strcmp(source->reload, "self"))
			SET_FLAG(handler->flags, _event_handler_reload_self_bit, TRUE);
		else if (!strcmp(source->reload, "other") && source->widget)
			SET_FLAG(handler->flags, _event_handler_reload_other_widget_bit, TRUE);
		else
			problem(source->file, source->line, "reload is \"self\" or \"other\" (with widget=)", source->reload);
	}
	if (source->widget)
		targets[target_count++] = source->widget;
	if (target_count > 1)
	{
		long index;

		/* (several flags may share the one widget) */
		for (index = 1; index < target_count; index++)
		{
			if (strcmp(targets[index], targets[0]))
				problem(source->file, source->line, "a handler names one widget (open, replace, focus or widget)",
					NULL);
		}
	}
	if (target_count)
	{
		reference_set(&handler->widget_tag, UI_WIDGET_DEFINITION_TAG,
			tag_named(UI_WIDGET_DEFINITION_TAG, targets[0], source->file, source->line));
	}
	if (source->sound)
		reference_set(&handler->sound_effect, SOUND_TAG, map_tag(SOUND_TAG, source->sound, source->file, source->line));
	if (source->otherwise)
	{
		if (!source->run)
			problem(source->file, source->line, "otherwise needs run (the function whose failure it is for)", NULL);
		SET_FLAG(handler->flags, _event_handler_try_to_branch_on_failure_bit, TRUE);
		conditional_add(definition, tag_named(UI_WIDGET_DEFINITION_TAG, source->otherwise, source->file, source->line), 1);
	}
}

static void setting_add(struct halo_menu_widget const *source, long definition_index)
{
	struct pc_menu_setting *setting;

	menu_tags.settings = realloc(menu_tags.settings, (menu_tags.setting_count + 1) * sizeof(*menu_tags.settings));
	setting = &menu_tags.settings[menu_tags.setting_count++];
	memset(setting, 0, sizeof(*setting));
	setting->definition_index = definition_index;
	setting->setting = source->setting;
	setting->loaded_index = NONE;
	setting->value_count = spinner_split(source, TRUE, setting->values);
}

/* a font: large, small, terminal, or the map's by its path */
static long font_tag(char const *font, char const *file, long line)
{
	char const *path = !strcmp(font, "large") ? "ui\\large_ui" :
		!strcmp(font, "small") ? "ui\\small_ui" :
		!strcmp(font, "terminal") ? "ui\\interstate" : font;

	return map_tag(FONT_TAG, path, file, line);
}

static void *widget_build(long widget_index)
{
	struct halo_menus const *menus = build.menus;
	struct halo_menu_widget const *source = &menus->widgets[widget_index];
	struct ui_widget_definition *definition = allocate(sizeof(struct ui_widget_definition));
	char const *leaf = strrchr(source->name, '/') ? strrchr(source->name, '/') + 1 : source->name;
	long child, handler, input, conditional, replace, count;

	if (!definition)
		return NULL;
	strncpy(definition->name, leaf, sizeof(definition->name) - 1);
	definition->type = _widget_type_container;
	if (source->type)
	{
		long type = name_index(source->type, widget_type_names, NUMBEROF(widget_type_names));

		if (type == NONE)
			problem(source->file, source->line, "there is no widget type", source->type);
		else
			definition->type = (short)type;
	}
	definition->controller_index = 4;
	if (source->controller && strcmp(source->controller, "any"))
	{
		if (strlen(source->controller) != 1 || source->controller[0] < '1' || source->controller[0] > '4')
			problem(source->file, source->line, "controller is 1 to 4, or \"any\":", source->controller);
		else
			definition->controller_index = (short)(source->controller[0] - '1');
	}
	definition->bounds.y0 = (short)source->top;
	definition->bounds.x0 = (short)source->left;
	definition->bounds.y1 = (short)(source->top + (source->has_height ? source->height : 480));
	definition->bounds.x1 = (short)(source->left + (source->has_width ? source->width : 640));
	definition->flags = flags_parse(source->flags, widget_flag_names, NUMBEROF(widget_flag_names),
		source->file, source->line);
	/* (no screen pauses the game: in a network game the clock runs on, and
	at the main menu the title's background and music play on through
	Settings and the multiplayer screens, which Halo PC's tags paused; but
	in a campaign game the pause menu's SETTINGS keeps it paused) */
	if (!build.campaign_map)
		definition->flags &= ~FLAG(1); /* pause_game */
	definition->milliseconds_to_auto_close = source->auto_close;
	definition->auto_close_fade_time = source->auto_close_fade;
	reference_clear(&definition->background_bitmap, BITMAP_GROUP_TAG);
	if (source->bitmap)
	{
		reference_set(&definition->background_bitmap, BITMAP_GROUP_TAG,
			tag_named(BITMAP_GROUP_TAG, source->bitmap, source->file, source->line));
	}
	/* the game data inputs */
	for (count = 0, input = source->first_input; input != HALO_MENU_NONE; input = menus->inputs[input].next)
		count++;
	if (count)
	{
		struct ui_widget_game_data_input_reference *inputs = allocate(count * sizeof(*inputs));

		definition->game_data_inputs.count = count;
		definition->game_data_inputs.address = XBOX_ADDRESS(inputs);
		for (count = 0, input = source->first_input; inputs && input != HALO_MENU_NONE; input = menus->inputs[input].next)
		{
			struct halo_menu_input const *data = &menus->inputs[input];

			inputs[count++].function = (short)game_data_input_index(data->input, data->file, data->line);
		}
	}
	/* search and replace */
	for (count = 0, replace = source->first_replace; replace != HALO_MENU_NONE; replace = menus->replaces[replace].next)
		count++;
	if (count)
	{
		struct ui_widget_search_and_replace_reference *replaces = allocate(count * sizeof(*replaces));

		definition->search_and_replace_functions.count = count;
		definition->search_and_replace_functions.address = XBOX_ADDRESS(replaces);
		for (count = 0, replace = source->first_replace; replaces && replace != HALO_MENU_NONE;
			replace = menus->replaces[replace].next)
		{
			struct halo_menu_replace const *item = &menus->replaces[replace];
			long function = item->function ? name_index(item->function, replace_names, NUMBEROF(replace_names)) : 0;

			if (function == NONE)
				problem(item->file, item->line, "a replace function is none or controller:", item->function);
			strncpy(replaces[count].search_string, item->search, sizeof(replaces[count].search_string) - 1);
			/* (the PC version's own are none here) */
			replaces[count++].replace_function = (short)(function == 1 ? 1 : 0);
		}
	}
	/* the text */
	reference_clear(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG);
	reference_clear(&definition->text_font, FONT_TAG);
	if ((source->text != NULL) + (source->strings != NULL) + (source->string_list != NULL) > 1)
		problem(source->file, source->line, "a widget has one of text, strings and string_list:", source->name);
	if (build.text_tags[widget_index] != NONE)
	{
		reference_set(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG, build.text_tags[widget_index]);
	}
	else if (build.spinner_tags[widget_index] != NONE)
	{
		reference_set(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG, build.spinner_tags[widget_index]);
		definition->list_flags |= FLAG(_list_items_generated_from_string_list_tag_bit);
	}
	else if (source->string_list)
	{
		reference_set(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG,
			tag_named(UNICODE_STRING_LIST_TAG, source->string_list, source->file, source->line));
	}
	definition->string_list_index = (short)source->string_index;
	/* (our own text is white in the large font, unless given) */
	if (source->text || source->strings)
	{
		definition->text_color.alpha = definition->text_color.red = 1.0f;
		definition->text_color.green = definition->text_color.blue = 1.0f;
		if (!source->font)
			reference_set(&definition->text_font, FONT_TAG, font_tag("large", source->file, source->line));
	}
	if (source->color && !parse_color(source->color, &definition->text_color))
		problem(source->file, source->line, "a color is #RRGGBB or #AARRGGBB:", source->color);
	if (source->font)
		reference_set(&definition->text_font, FONT_TAG, font_tag(source->font, source->file, source->line));
	if (source->align)
	{
		long align = name_index(source->align, align_names, NUMBEROF(align_names));

		if (align == NONE)
			problem(source->file, source->line, "align is \"left\", \"right\" or \"center\":", source->align);
		else
			definition->justification = (short)align;
	}
	definition->text_box_flags = (word)flags_parse(source->text_flags, text_flag_names, NUMBEROF(text_flag_names),
		source->file, source->line);
	definition->horizontal_offset = (short)source->text_x;
	definition->vertical_offset = (short)source->text_y;
	/* the list */
	definition->list_flags |= flags_parse(source->list_flags, list_flag_names, NUMBEROF(list_flag_names),
		source->file, source->line);
	if (source->strings && (definition->type != _widget_type_spinner_list || source->first_child != HALO_MENU_NONE))
		problem(source->file, source->line, "strings are for a spinner with no children:", source->name);
	if (TEST_FLAG(definition->list_flags, _list_items_generated_from_string_list_tag_bit) &&
		(definition->type != _widget_type_spinner_list || source->first_child != HALO_MENU_NONE ||
			definition->text_label_string_list.index == NONE))
		problem(source->file, source->line, "items_from_strings is for a spinner with strings and no children:",
			source->name);
	if (source->setting)
	{
		if (!source->strings || !source->values)
			problem(source->file, source->line, "a setting needs strings and values:", source->name);
		else
			setting_add(source, build.widget_tags[widget_index]);
	}
	reference_clear(&definition->list_header_bitmap, BITMAP_GROUP_TAG);
	reference_clear(&definition->list_footer_bitmap, BITMAP_GROUP_TAG);
	if (source->header_bitmap)
	{
		reference_set(&definition->list_header_bitmap, BITMAP_GROUP_TAG,
			tag_named(BITMAP_GROUP_TAG, source->header_bitmap, source->file, source->line));
	}
	if (source->footer_bitmap)
	{
		reference_set(&definition->list_footer_bitmap, BITMAP_GROUP_TAG,
			tag_named(BITMAP_GROUP_TAG, source->footer_bitmap, source->file, source->line));
	}
	parse_bounds(source->header_bounds, &definition->list_header_bounds, source->file, source->line);
	parse_bounds(source->footer_bounds, &definition->list_footer_bounds, source->file, source->line);
	reference_clear(&definition->extended_description_widget, UI_WIDGET_DEFINITION_TAG);
	if (source->description)
	{
		if (definition->type != _widget_type_column_list)
			problem(source->file, source->line, "a description is for a column list:", source->name);
		reference_set(&definition->extended_description_widget, UI_WIDGET_DEFINITION_TAG,
			tag_named(UI_WIDGET_DEFINITION_TAG, source->description, source->file, source->line));
	}
	/* the children */
	for (count = 0, child = source->first_child; child != HALO_MENU_NONE; child = menus->children[child].next)
		count++;
	if (definition->type == _widget_type_spinner_list && count != 0 && count != 1 && count != 3)
		problem(source->file, source->line, "a spinner has 0, 1 or 3 children:", source->name);
	if (count)
	{
		struct ui_widget_child_reference *children = allocate(count * sizeof(*children));

		definition->child_widgets.count = count;
		definition->child_widgets.address = XBOX_ADDRESS(children);
		for (count = 0, child = source->first_child; children && child != HALO_MENU_NONE; child = menus->children[child].next)
		{
			struct halo_menu_child const *entry = &menus->children[child];
			long tag_index = entry->nested != HALO_MENU_NONE ? build.widget_tags[entry->nested] :
				tag_named(UI_WIDGET_DEFINITION_TAG, entry->widget, entry->file, entry->line);
			char const *name = entry->nested != HALO_MENU_NONE ? menus->widgets[entry->nested].name : entry->widget;
			char const *child_leaf = strrchr(name, '/') ? strrchr(name, '/') + 1 : name;

			reference_set(&children[count].widget_tag, UI_WIDGET_DEFINITION_TAG, tag_index);
			strncpy(children[count].name, child_leaf, sizeof(children[count].name) - 1);
			children[count].horizontal_offset = (short)entry->x;
			children[count].vertical_offset = (short)entry->y;
			if (entry->controller)
			{
				if (strlen(entry->controller) != 1 || entry->controller[0] < '1' || entry->controller[0] > '4')
					problem(entry->file, entry->line, "a child's controller is 1 to 4:", entry->controller);
				SET_FLAG(children[count].flags, _child_widget_use_custom_controller_index_bit, TRUE);
				children[count].custom_controller_index = (short)(entry->controller[0] - '1');
			}
			count++;
		}
	}
	/* the conditional widgets */
	for (conditional = source->first_conditional; conditional != HALO_MENU_NONE;
		conditional = menus->conditionals[conditional].next)
	{
		struct halo_menu_conditional const *item = &menus->conditionals[conditional];

		conditional_add(definition, tag_named(UI_WIDGET_DEFINITION_TAG, item->widget, item->file, item->line),
			item->if_failed ? 1 : 0);
	}
	/* the event handlers: one for each of each <on>'s events */
	for (count = 0, handler = source->first_handler; handler != HALO_MENU_NONE; handler = menus->handlers[handler].next)
		count++;
	if (count)
	{
		long total = 0;

		/* (count the events) */
		for (handler = source->first_handler; handler != HALO_MENU_NONE; handler = menus->handlers[handler].next)
		{
			char const *text = menus->handlers[handler].event;

			while (text && *text)
			{
				long length;

				text += strspn(text, " \t\r\n");
				length = (long)strcspn(text, " \t\r\n");
				if (!length)
					break;
				total++;
				text += length;
			}
		}
		if (total)
		{
			struct ui_widget_event_handler_reference *handlers = allocate(total * sizeof(*handlers));

			definition->event_handlers.count = total;
			definition->event_handlers.address = XBOX_ADDRESS(handlers);
			for (count = 0, handler = source->first_handler; handlers && handler != HALO_MENU_NONE;
				handler = menus->handlers[handler].next)
			{
				struct halo_menu_handler const *on = &menus->handlers[handler];
				char const *text = on->event;

				while (text && *text)
				{
					char word[64];
					long length, event;

					text += strspn(text, " \t\r\n");
					length = (long)strcspn(text, " \t\r\n");
					if (!length)
						break;
					if (length >= (long)sizeof(word))
						length = sizeof(word) - 1;
					memcpy(word, text, length);
					word[length] = 0;
					text += strcspn(text, " \t\r\n");
					event = name_index(word, event_names, NUMBEROF(event_names));
					if (event == NONE)
						problem(on->file, on->line, "there is no event", word);
					handler_build(&handlers[count++], on, event, definition);
				}
			}
		}
	}
	return definition;
}

/* the tag table with count more entries, the existing ones as they are */
static struct cache_file_tag_instance *instances_grow(long count, long *first_index, long *first_salt)
{
	long existing, index, salt = 0;
	struct cache_file_tag_instance *instances = cache_files_tag_instances(&existing);
	struct cache_file_tag_instance *grown;

	if (!instances)
		return NULL;
	/* (every tag's absolute index fits a tag index's 16 bits, short of
	NONE's, and the table's size cannot wrap: the map's count was checked
	as it loaded, cache_files.c, and is again) */
	if (existing < 0 || count < 0 || existing > UNSIGNED_SHORT_MAX - count)
	{
		platform_log("menus: %ld tags and the map's %ld are too many for a tag table", count, existing);
		return NULL;
	}
	grown = allocate((existing + count) * sizeof(*grown));
	if (!grown)
		return NULL;
	memcpy(grown, instances, existing * sizeof(*grown));
	for (index = 0; index < existing; index++)
	{
		long instance_salt = (unsigned long)instances[index].tag_index >> 16;

		if (instance_salt > salt)
			salt = instance_salt;
	}
	menu_tags.original_instances = instances;
	menu_tags.original_count = existing;
	*first_index = existing;
	*first_salt = salt + 1;
	return grown;
}

static long next_tag(void)
{
	long tag_index = ((build.first_salt + build.next) << 16) | (build.first_index + build.next);

	build.next++;
	return tag_index;
}

static void instance_set(struct cache_file_tag_instance *instances, long group_tag, long tag_index, char const *name,
	char const *suffix, void *definition)
{
	struct cache_file_tag_instance *instance = &instances[DATUM_INDEX_TO_ABSOLUTE_INDEX(tag_index)];
	char *copy = allocate((long)strlen(PC_MENU_TAG_PREFIX) + (long)strlen(name) + (long)strlen(suffix) + 1);

	if (copy)
	{
		char *character;

		strcpy(copy, PC_MENU_TAG_PREFIX);
		strcat(copy, name);
		strcat(copy, suffix);
		/* (named as the map's tags are) */
		for (character = copy; *character; character++)
		{
			if (*character == '/')
				*character = '\\';
		}
	}
	instance->group_tag = group_tag;
	instance->parent_group_tags[0] = NONE;
	instance->parent_group_tags[1] = NONE;
	instance->tag_index = tag_index;
	/* (the name is read by tag_loaded: of none, when the copy failed, which
	fails the build) */
#ifdef HALO_64BIT
	if (!copy)
	{
		if (!menu_tags.empty_name)
			menu_tags.empty_name = allocate(1);
		copy = menu_tags.empty_name;
	}
	instance->name = XBOX_ADDRESS(copy);
#else
	instance->name = copy ? copy : "";
#endif
	instance->base_address = XBOX_ADDRESS(definition);
}

static void menu_tags_release(void)
{
	long index;

	for (index = 0; index < menu_tags.bitmap_count; index++)
	{
		if (menu_tags.bitmaps[index]->hardware_format)
			rasterizer_bitmap_delete(menu_tags.bitmaps[index]);
	}
	halo_menus_art_forget();
	if (menu_tags.original_instances)
		cache_files_set_tag_instances(menu_tags.original_instances, menu_tags.original_count);
	/* (the game's free takes no NULL: settings is, with no setting spinners) */
	for (index = 0; index < menu_tags.block_count; index++)
		free(menu_tags.blocks[index]);
	if (menu_tags.blocks)
		free(menu_tags.blocks);
	if (menu_tags.bitmaps)
		free(menu_tags.bitmaps);
	if (menu_tags.placements)
		free(menu_tags.placements);
	if (menu_tags.settings)
		free(menu_tags.settings);
	memset(&menu_tags, 0, sizeof(menu_tags));
}

/* ---------- public code */

/* ---------- the in-game pause menus (a campaign map's, a multiplayer map's)

Their list gets SETTINGS after RESUME GAME, the menus' settings (as the
main menu's SETTINGS opens them), and a multiplayer map's, for the host,
END GAME after it, which ends the game as its time limit would: its players
stay, and the host picks the next map after the carnage report. A team
game's gets CHANGE TEAMS right after RESUME GAME, as Halo PC's had it: the
player asks the host to put them on the other team, and the menu closes
(team_change.c). They are
copies of RESUME GAME (its look), the rows after them moved down. The map's
pause screens grow to fit them: on the Xbox's own (its box of pausebox2
pieces, and the campaign's objectives box beside it), the box is drawn as
much taller, centred where it was, with what is in it, and what is below
the list moved down; on a custom map's, what is below the list moves down. */

#define PAUSE_SETTINGS_SCREEN "main_menu/settings_select/player_setup/player_profile_edit/player_profile_edit_screen"
/* (the Chupathingy menus': their own, in the Xbox's look) */
#define PAUSE_SETTINGS_SCREEN_CHUPATHINGY "chupathingy/settings/profile_edit_screen"
/* CONTROLS': Controls Setup (tools/port_settings.py's CONTROLS_SCREEN), the
Gearbox menus' own */
#define PAUSE_CONTROLS_SCREEN \
	"main_menu/settings_select/player_setup/player_profile_edit/controls_setup/controls_settings_screen"
#define PAUSE_BUTTON_SPACING 35

/* display.menus: the Xbox's menus, Gearbox's Halo PC menus, or Chupathingy's (the
Xbox's, with the PC version's screens that the Xbox has not, in its look:
port/assets/menus/chupathingy) */
enum
{
	_menus_xbox,
	_menus_pc,
	_menus_chupathingy,
};

static short menus_chosen(void);

static long ui_function_named(char const *name)
{
	long index;

	for (index = 0; index < 512; index++)
	{
		char const *function = ui_widget_event_handler_function_name(index);

		if (!function)
			break;
		if (!strcmp(function, name))
			return index;
	}
	return NONE;
}

static boolean tag_name_ends(long tag_index, char const *end)
{
	char const *name = tag_index == NONE ? NULL : tag_get_name(tag_index);
	size_t length = name ? strlen(name) : 0, end_length = strlen(end);

	return name && length >= end_length && !strcmp(name + length - end_length, end);
}

/* the index of the list's button whose tag's name ends so, else NONE */
static long pause_button_named(struct ui_widget_definition const *list, char const *end)
{
	struct ui_widget_child_reference const *children = xbox_pointer(list->child_widgets.address);
	long child;

	for (child = 0; child < list->child_widgets.count; child++)
	{
		if (tag_name_ends(children[child].widget_tag.index, end))
			return child;
	}
	return NONE;
}

/* the index of the list's button that runs the function (LEAVE GAME's),
else NONE */
static long pause_quit_button(struct ui_widget_definition const *list, long quit_function)
{
	struct ui_widget_child_reference const *children = xbox_pointer(list->child_widgets.address);
	long child;

	for (child = 0; child < list->child_widgets.count; child++)
	{
		struct ui_widget_definition const *button;
		struct ui_widget_event_handler_reference const *handlers;
		long handler;

		if (children[child].widget_tag.index == NONE)
			continue;
		button = tag_get(UI_WIDGET_DEFINITION_TAG, children[child].widget_tag.index);
		handlers = xbox_pointer(button->event_handlers.address);
		for (handler = 0; handler < button->event_handlers.count; handler++)
		{
			if (handlers[handler].function == quit_function &&
				TEST_FLAG(handlers[handler].flags, _event_handler_run_function_bit))
			{
				return child;
			}
		}
	}
	return NONE;
}

/* a button like the model (its look), its own text, each of the model's
events (its A and Start) running the function and opening the widget (if
not NONE), or closing the menu (close) */
static void *pause_button(struct cache_file_tag_instance *instances, struct ui_widget_definition const *model,
	char const *name, char const *text, long function, long widget_tag, boolean close)
{
	struct ui_widget_definition *button = allocate(sizeof(*button));
	struct ui_widget_event_handler_reference *handlers =
		allocate(model->event_handlers.count * sizeof(struct ui_widget_event_handler_reference));
	long text_tag = next_tag(), handler;

	if (!button || !handlers)
		return NULL;
	memcpy(button, model, sizeof(*button));
	instance_set(instances, UNICODE_STRING_LIST_TAG, text_tag, name, " text", string_list_build(&text, 1));
	reference_set(&button->text_label_string_list, UNICODE_STRING_LIST_TAG, text_tag);
	button->string_list_index = 0;
	button->game_data_inputs.count = 0;
	button->game_data_inputs.address = XBOX_NULL;
	/* (the game's memcpy asserts on a NULL source, even for nothing) */
	if (model->event_handlers.count)
		memcpy(handlers, xbox_pointer(model->event_handlers.address), model->event_handlers.count * sizeof(*handlers));
	for (handler = 0; handler < model->event_handlers.count; handler++)
	{
		handlers[handler].flags = FLAG(_event_handler_run_function_bit);
		if (close)
			handlers[handler].flags |= FLAG(_event_handler_close_all_widgets_bit);
		handlers[handler].function = (short)function;
		handlers[handler].script[0] = 0;
		reference_set(&handlers[handler].widget_tag, UI_WIDGET_DEFINITION_TAG, widget_tag);
		if (widget_tag != NONE)
		{
			handlers[handler].flags |= FLAG(_event_handler_open_widget_bit) |
				FLAG(_event_handler_try_to_branch_on_failure_bit);
		}
	}
	button->event_handlers.address = XBOX_ADDRESS(handlers);
	return button;
}

/* whether the pause menus have CONTROLS (Controls Setup, or the Xbox
menus' Controls screen: controls_screen.c), as display.pause_controls says;
the Chupathingy menus' never have it (their SETTINGS opens Controls) */
static boolean pause_controls_offered(void)
{
#if defined(HALO_GAME_BROWSER) && !defined(HALO_CONSOLE_NO_CONTROLS)
	return menus_chosen() != _menus_chupathingy && config_boolean("display.pause_controls") != 0;
#else
	return FALSE;
#endif
}

/* the list's buttons: a team game's CHANGE TEAMS (change_teams), SETTINGS
(Gearbox's menus: settings), CONTROLS (pause_controls_offered) and the
host's END GAME (end_game) put after RESUME GAME (resume), the rows after
them moved down; returns how far the list grew, 0 if it was not */
static short pause_list_patch(struct cache_file_tag_instance *instances, struct ui_widget_definition *list, long resume,
	boolean change_teams, boolean settings, boolean end_game)
{
	struct ui_widget_child_reference *children = xbox_pointer(list->child_widgets.address);
	long count = list->child_widgets.count, child;
	boolean controls = pause_controls_offered();
	/* (the Xbox's menus have none of ours: the Controls screen, by code) */
	long controls_widget = !controls || !build.menus ? NONE : widget_named(PAUSE_CONTROLS_SCREEN);
	long added = (change_teams != FALSE) + (settings != FALSE) + (controls != FALSE) + (end_game != FALSE);
	struct ui_widget_child_reference *grown = allocate((count + added) * sizeof(struct ui_widget_child_reference));
	struct ui_widget_definition const *model = tag_get(UI_WIDGET_DEFINITION_TAG, children[resume].widget_tag.index);
	long settings_widget = !settings ? NONE :
		widget_named(menus_chosen() == _menus_chupathingy ? PAUSE_SETTINGS_SCREEN_CHUPATHINGY : PAUSE_SETTINGS_SCREEN);
	short spacing = count >= 2 ? (short)(children[count - 1].vertical_offset - children[count - 2].vertical_offset) :
		PAUSE_BUTTON_SPACING;
	void *buttons[4];
	long tags[4];
	char const *names[4];

	if (!grown || !added || (settings && settings_widget == NONE) || spacing <= 0)
		return 0;
	added = 0;
	if (change_teams)
	{
		names[added] = "pause/change_teams_button";
		tags[added] = next_tag();
		buttons[added++] = pause_button(instances, model, "pause/change_teams_button", "CHANGE TEAMS",
			function_index("port pause change teams", "pause", 0), NONE, TRUE);
	}
	if (settings)
	{
		names[added] = "pause/settings_button";
		tags[added] = next_tag();
		buttons[added++] = pause_button(instances, model, "pause/settings_button", "SETTINGS",
			function_index("profile set edit begin", "pause", 0), build.widget_tags[settings_widget], FALSE);
	}
	if (controls)
	{
		names[added] = "pause/controls_button";
		tags[added] = next_tag();
		buttons[added++] = pause_button(instances, model, "pause/controls_button", "CONTROLS",
			function_index("port pause controls", "pause", 0),
			controls_widget == NONE ? NONE : build.widget_tags[controls_widget], FALSE);
	}
	if (end_game)
	{
		names[added] = "pause/end_game_button";
		tags[added] = next_tag();
		buttons[added++] = pause_button(instances, model, "pause/end_game_button", "END GAME",
			function_index("port pause end game", "pause", 0), NONE, FALSE);
	}
	for (child = 0; child < added; child++)
	{
		if (!buttons[child])
			return 0;
	}
	for (child = 0; child < added; child++)
		instance_set(instances, UI_WIDGET_DEFINITION_TAG, tags[child], names[child], "", buttons[child]);
	memcpy(grown, children, (resume + 1) * sizeof(*grown));
	for (child = 0; child < added; child++)
	{
		grown[resume + 1 + child] = children[resume];
		reference_set(&grown[resume + 1 + child].widget_tag, UI_WIDGET_DEFINITION_TAG, tags[child]);
		grown[resume + 1 + child].vertical_offset = (short)(children[resume].vertical_offset + (child + 1) * spacing);
	}
	for (child = resume + 1; child < count; child++)
	{
		grown[child + added] = children[child];
		grown[child + added].vertical_offset = (short)(children[child].vertical_offset + added * spacing);
	}
	list->child_widgets.address = XBOX_ADDRESS(grown);
	list->child_widgets.count = count + added;
	/* (the list draws within its bounds) */
	list->bounds.y1 = (short)(list->bounds.y1 + added * spacing);
	return (short)(added * spacing);
}

/* the Xbox's own box: a widget of pausebox2 pieces (and, the campaign's,
the objectives box's pausebox pieces) */
static boolean pause_box_stock(struct ui_widget_definition const *box)
{
	struct ui_widget_child_reference const *pieces = xbox_pointer(box->child_widgets.address);

	return box->child_widgets.count >= 3 && tag_name_ends(pieces[0].widget_tag.index, "\\pausebox2_left");
}

/* the stock box drawn taller, once in a map (the screens share it): each
piece drawn to PAUSE_BOX_SPLIT rows (below the objectives' header), then its
plain middle, from that row to PAUSE_BOX_KEY, stretched grow rows longer,
then the rest of it from its row PAUSE_BOX_KEY on (the buttons' key), so the
box is as the Xbox draws it with its middle grow longer. (Drawing the top
part grow rows further instead showed the key's line and dark band in the
middle of the box once the menu grew by three rows or more.) */
#define PAUSE_BOX_SPLIT 45
#define PAUSE_BOX_KEY 120

/* a part of a piece: a copy of it, its bitmap drawn from row from, its rows
rows of it drawn height rows tall */
static long pause_box_part(struct cache_file_tag_instance *instances, long piece_tag, short from, short rows,
	short height)
{
	struct ui_widget_definition *part = allocate(sizeof(*part));
	struct bitmap_group *group = allocate(sizeof(*group));
	struct bitmap_data *bitmap = allocate(sizeof(*bitmap));
	struct menu_frame_placement *placements = realloc(menu_tags.placements,
		(menu_tags.placement_count + 1) * sizeof(*menu_tags.placements));
	struct bitmap_group const *source;
	long widget_tag, bitmap_tag;

	if (!part || !group || !bitmap || !placements || rows <= 0 || height <= 0)
		return NONE;
	menu_tags.placements = placements;
	memcpy(part, tag_get(UI_WIDGET_DEFINITION_TAG, piece_tag), sizeof(*part));
	if (part->background_bitmap.index == NONE)
		return NONE;
	source = bitmap_group_get(part->background_bitmap.index);
	if (source->bitmaps.count < 1)
		return NONE;
	memcpy(group, source, sizeof(*group));
	memcpy(bitmap, xbox_pointer(source->bitmaps.address), sizeof(*bitmap));
	group->bitmaps.count = 1;
	group->bitmaps.address = XBOX_ADDRESS(bitmap);
	placements[menu_tags.placement_count].bitmap = bitmap;
	placements[menu_tags.placement_count].x = 0;
	placements[menu_tags.placement_count].y = 0;
	placements[menu_tags.placement_count].width = bitmap->width;
	/* (the whole bitmap's height, as it is drawn: rows rows to height) */
	placements[menu_tags.placement_count].height = (short)((long)height * bitmap->height / rows);
	placements[menu_tags.placement_count].texel_y = from;
	menu_tags.placement_count++;
	bitmap_tag = next_tag();
	instance_set(instances, BITMAP_GROUP_TAG, bitmap_tag, "pause/box_part", "", group);
	reference_set(&part->background_bitmap, BITMAP_GROUP_TAG, bitmap_tag);
	part->bounds.y1 = (short)(part->bounds.y0 + height);
	widget_tag = next_tag();
	instance_set(instances, UI_WIDGET_DEFINITION_TAG, widget_tag, "pause/box_part", "", part);
	return widget_tag;
}

/* the stock box's pieces cut to end where the next begins: the campaign's
pause screen places pausebox2_right 8 units into pausebox2_left_center
(the multiplayer maps' place them edge to edge), and the box's middle,
drawn twice there, showed as a lighter bar down its right side */
static void pause_box_trim(struct ui_widget_definition const *box)
{
	struct ui_widget_child_reference const *pieces = xbox_pointer(box->child_widgets.address);
	long piece;

	for (piece = 0; piece + 1 < box->child_widgets.count; piece++)
	{
		struct ui_widget_definition *definition;
		short end;

		if (pieces[piece].widget_tag.index == NONE || pieces[piece + 1].widget_tag.index == NONE)
			continue;
		definition = tag_get(UI_WIDGET_DEFINITION_TAG, pieces[piece].widget_tag.index);
		end = (short)(pieces[piece + 1].horizontal_offset - pieces[piece].horizontal_offset);
		if (end > definition->bounds.x0 && end < definition->bounds.x1)
			definition->bounds.x1 = end;
	}
}

static void pause_box_grow(struct cache_file_tag_instance *instances, struct ui_widget_definition *box, short grow)
{
	struct ui_widget_child_reference *pieces = xbox_pointer(box->child_widgets.address);
	long count = box->child_widgets.count, piece;
	struct ui_widget_child_reference *grown = allocate(3 * count * sizeof(struct ui_widget_child_reference));
	short split = PAUSE_BOX_SPLIT, key = PAUSE_BOX_KEY;

	if (!grown || count > 6)
		return;
	/* (every piece tall enough first: all of them drawn taller, or none) */
	for (piece = 0; piece < count; piece++)
	{
		struct ui_widget_definition const *definition;

		if (pieces[piece].widget_tag.index == NONE)
			continue;
		definition = tag_get(UI_WIDGET_DEFINITION_TAG, pieces[piece].widget_tag.index);
		if (definition->bounds.y1 - definition->bounds.y0 <= key)
			return;
	}
	memcpy(grown, pieces, count * sizeof(*grown));
	for (piece = 0; piece < count; piece++)
	{
		struct ui_widget_definition const *definition;
		struct ui_widget_child_reference *middle = &grown[count + 2 * piece], *lower = middle + 1;

		*middle = *lower = pieces[piece];
		if (pieces[piece].widget_tag.index == NONE)
			continue;
		definition = tag_get(UI_WIDGET_DEFINITION_TAG, pieces[piece].widget_tag.index);
		reference_set(&middle->widget_tag, UI_WIDGET_DEFINITION_TAG,
			pause_box_part(instances, pieces[piece].widget_tag.index, split, key - split, key - split + grow));
		reference_set(&lower->widget_tag, UI_WIDGET_DEFINITION_TAG,
			pause_box_part(instances, pieces[piece].widget_tag.index, key, definition->bounds.y1 - definition->bounds.y0 - key,
				definition->bounds.y1 - definition->bounds.y0 - key));
		if (middle->widget_tag.index == NONE || lower->widget_tag.index == NONE)
			return;
		middle->vertical_offset = (short)(pieces[piece].vertical_offset + split);
		lower->vertical_offset = (short)(pieces[piece].vertical_offset + key + grow);
	}
	/* (the top parts cut short only once every other part is made) */
	for (piece = 0; piece < count; piece++)
	{
		struct ui_widget_definition *upper;

		if (pieces[piece].widget_tag.index == NONE)
			continue;
		upper = tag_get(UI_WIDGET_DEFINITION_TAG, pieces[piece].widget_tag.index);
		upper->bounds.y1 = (short)(upper->bounds.y0 + split);
	}
	box->child_widgets.address = XBOX_ADDRESS(grown);
	box->child_widgets.count = 3 * count;
	box->bounds.y1 = (short)(box->bounds.y1 + grow);
}

/* a screen's pause list, found by its RESUME GAME, else the button before
the one running quit_function (a custom map's), patched
once (the screens share it), and the screen moved to fit it */
static void pause_screen_patch(struct cache_file_tag_instance *instances, long screen_tag, long quit_function,
	boolean change_teams, boolean settings, boolean end_game, long *patched_list, short *grow, boolean *box_grown)
{
	struct ui_widget_definition *definition = tag_get(UI_WIDGET_DEFINITION_TAG, screen_tag);
	struct ui_widget_child_reference *children = xbox_pointer(definition->child_widgets.address);
	struct ui_widget_definition const *list;
	long child, list_child = NONE, box_child = NONE;
	short list_top, list_bottom;

	for (child = 0; child < definition->child_widgets.count && list_child == NONE; child++)
	{
		struct ui_widget_definition *candidate;
		long resume;

		if (children[child].widget_tag.index == NONE)
			continue;
		candidate = tag_get(UI_WIDGET_DEFINITION_TAG, children[child].widget_tag.index);
		if (candidate->type != _widget_type_column_list)
			continue;
		if (children[child].widget_tag.index == *patched_list)
		{
			list_child = child;
			continue;
		}
		resume = pause_button_named(candidate, "\\resume_game_button");
		if (resume == NONE && quit_function != NONE && (resume = pause_quit_button(candidate, quit_function)) != NONE)
			resume--;
		if (resume < 0 || resume == NONE || *patched_list != NONE)
			continue;
		*grow = pause_list_patch(instances, candidate, resume, change_teams, settings, end_game);
		if (!*grow)
			return;
		*patched_list = children[child].widget_tag.index;
		list_child = child;
	}
	if (list_child == NONE)
		return;
	for (child = 0; child < definition->child_widgets.count; child++)
	{
		if (child != list_child && children[child].widget_tag.index != NONE &&
			pause_box_stock(tag_get(UI_WIDGET_DEFINITION_TAG, children[child].widget_tag.index)))
		{
			box_child = child;
		}
	}
	/* the stock box: taller, centred where it was with what is in it, and
	what is below the list (the buttons' key) moved down; else only what
	is below the list's top */
	/* (the list's last button's bottom, before it grew: the list's bounds
	may reach further) */
	list = tag_get(UI_WIDGET_DEFINITION_TAG, children[list_child].widget_tag.index);
	list_top = children[list_child].vertical_offset;
	list_bottom = list_top;
	{
		struct ui_widget_child_reference const *buttons = xbox_pointer(list->child_widgets.address);
		long button;

		for (button = 0; button < list->child_widgets.count; button++)
		{
			if (buttons[button].widget_tag.index != NONE)
			{
				list_bottom = (short)MAX(list_bottom, list_top + buttons[button].vertical_offset +
					((struct ui_widget_definition const *)tag_get(UI_WIDGET_DEFINITION_TAG,
						buttons[button].widget_tag.index))->bounds.y1 - *grow);
			}
		}
	}
	for (child = 0; child < definition->child_widgets.count; child++)
	{
		if (box_child == NONE)
		{
			if (children[child].vertical_offset > list_top)
				children[child].vertical_offset += *grow;
		}
		else if (child != box_child && child != list_child && children[child].vertical_offset >= list_bottom)
			children[child].vertical_offset += *grow - *grow / 2;
		else
			children[child].vertical_offset -= *grow / 2;
	}
	if (box_child != NONE && !*box_grown)
	{
		/* (trimmed first: the box's taller parts are copies of its pieces) */
		pause_box_trim(tag_get(UI_WIDGET_DEFINITION_TAG, children[box_child].widget_tag.index));
		pause_box_grow(instances, tag_get(UI_WIDGET_DEFINITION_TAG, children[box_child].widget_tag.index), *grow);
		*box_grown = TRUE;
	}
}

/* settings: Gearbox's menus' SETTINGS too (the Xbox's menus have only a
team game's CHANGE TEAMS and the host's END GAME) */
static void pause_patch(struct cache_file_tag_instance *instances, boolean settings)
{
	long collection = tag_loaded('Soul', MULTIPLAYER_COLLECTION);
	long solo_screen = tag_loaded(UI_WIDGET_DEFINITION_TAG, SOLO_PAUSE_SCREEN);
	long quit_function = ui_function_named("mp game player quit");
	boolean host = global_network_game_server_get() != NULL;
	boolean change_teams = solo_screen == NONE && team_change_pause_offered();
	long patched_list = NONE;
	boolean box_grown = FALSE;
	short grow = 0;

	/* the campaign's (a network co-op game's too): no END GAME, which is
	the multiplayer game engine's */
	if (solo_screen != NONE)
	{
		if (settings || pause_controls_offered())
			pause_screen_patch(instances, solo_screen, NONE, FALSE, settings, FALSE, &patched_list, &grow, &box_grown);
	}
	else if (collection != NONE)
	{
		struct tag_block const *screens = tag_get('Soul', collection);
		long screen;

		for (screen = 0; screen < screens->count; screen++)
		{
			long screen_tag = XBOX_POINTER(struct tag_reference const, screens->address)[screen].index;

			if (screen_tag != NONE)
			{
				pause_screen_patch(instances, screen_tag, quit_function, change_teams, settings, host, &patched_list, &grow,
					&box_grown);
			}
		}
	}
	if (patched_list != NONE)
	{
		platform_log("menus: the pause menu has%s%s%s%s", change_teams ? " CHANGE TEAMS" : "", settings ? " SETTINGS" : "",
			pause_controls_offered() ? " CONTROLS" : "", host && solo_screen == NONE ? " END GAME" : "");
	}
}

/* the Xbox's menus: a multiplayer map's pause menu gets a team game's CHANGE
TEAMS and the host's END GAME alone, and every map's CONTROLS (its own
tags, as the Gearbox menus' pause_patch makes them; let go by
menu_tags_unloaded) */
static void pause_patch_xbox(void)
{
	struct cache_file_tag_instance *instances;
	long index;

	memset(&build, 0, sizeof(build));
	build.game_map = TRUE;
	instances = instances_grow(PAUSE_PATCH_TAGS, &build.first_index, &build.first_salt);
	if (!instances)
	{
		menu_tags_release();
		memset(&build, 0, sizeof(build));
		return;
	}
	/* (nothing a name finds, until pause_patch makes them) */
	for (index = build.first_index; index < build.first_index + PAUSE_PATCH_TAGS; index++)
	{
		instances[index].group_tag = NONE;
		instances[index].tag_index = NONE;
#ifdef HALO_64BIT
		if (!menu_tags.empty_name)
			menu_tags.empty_name = allocate(1);
		instances[index].name = XBOX_ADDRESS(menu_tags.empty_name);
#else
		instances[index].name = "";
#endif
	}
	cache_files_set_tag_instances(instances, build.first_index + PAUSE_PATCH_TAGS);
	pause_patch(instances, FALSE);
	if (build.failed)
	{
		platform_log("menus: the pause menu's buttons not added");
		menu_tags_release();
	}
	else
		cache_files_set_tag_instances(instances, build.first_index + build.next);
	memset(&build, 0, sizeof(build));
}


/* which display.menus asks for, in any case ("gearbox" or the older "pc"
for Gearbox's; a value that is none of them is said once in the log, and is
the Xbox's) */
static short menus_chosen(void)
{
	static boolean said;
	char const *value = config_string("display.menus");
	char lower[16];
	size_t index;

	/* (a map pack's own menus, game.pack_menus, are its ui.map's as they
	are: none of ours are built over them, map_packs.c) */
	if (map_pack_menus())
		return _menus_xbox;

	for (index = 0; index + 1 < sizeof(lower) && value[index]; index++)
		lower[index] = (char)(value[index] >= 'A' && value[index] <= 'Z' ? value[index] - 'A' + 'a' : value[index]);
	lower[index] = 0;
	if (value[index])
		lower[0] = 0;
	if (!strcmp(lower, "gearbox") || !strcmp(lower, "pc"))
		return _menus_pc;
	if (!strcmp(lower, "chupathingy"))
		return _menus_chupathingy;
	if (strcmp(lower, "xbox") && !said)
	{
		platform_log("menus: display.menus \"%s\" is not \"xbox\", \"gearbox\" or \"chupathingy\": the Xbox's", value);
		said = TRUE;
	}
	return _menus_xbox;
}

/* which menus display.menus asks for: 0 the Xbox's, 1 Gearbox's, 2 the
Chupathingy menus (for what follows them outside the menus: the
scoreboard's platform logos) */
short pc_menus_set(
	void)
{
	short menus = menus_chosen();

	return menus == _menus_pc ? 1 : menus == _menus_chupathingy ? 2 : 0;
}

/* whether the Chupathingy menus are up */
boolean pc_menus_chupathingy(
	void)
{
	return menu_tags.loaded && menu_tags.chupathingy;
}

/* ---------- the Chupathingy menus' GAME SETUP

The host's gametype, chosen on the Xbox's Select Gametype for a new game
(System Link's and Online Games'), goes on to GAME SETUP
(chupathingy/host_setup.xml, menu_functions.c) rather than straight to the
lobby, which its START GAME opens. */

#define HOST_GAMETYPE_SELECT "ui\\shell\\main_menu\\multiplayer_type_select\\connected\\gametype_select_screen_wrapper"
#define HOST_LOBBY "ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\connected_pregame_screen"
#define HOST_SETUP PC_MENU_TAG_PREFIX "chupathingy\\host\\setup_screen"

static void host_setup_patch(void)
{
	long select = tag_loaded(UI_WIDGET_DEFINITION_TAG, HOST_GAMETYPE_SELECT);
	long lobby = tag_loaded(UI_WIDGET_DEFINITION_TAG, HOST_LOBBY);
	long setup = tag_loaded(UI_WIDGET_DEFINITION_TAG, HOST_SETUP);
	long function = ui_function_named("mp profile set for game");
	struct ui_widget_definition *definition;
	struct ui_widget_event_handler_reference *handlers;
	long index;

	if (select == NONE || lobby == NONE || setup == NONE || function == NONE)
		return;
	definition = tag_get(UI_WIDGET_DEFINITION_TAG, select);
	handlers = xbox_pointer(definition->event_handlers.address);
	for (index = 0; index < definition->event_handlers.count; index++)
	{
		if (handlers[index].function == function && handlers[index].widget_tag.index == lobby)
			reference_set(&handlers[index].widget_tag, UI_WIDGET_DEFINITION_TAG, setup);
	}
}

void menu_tags_loaded(
	char const *map_name)
{
	struct halo_menus const *menus;
	struct cache_file_tag_instance *instances;
	long widget_count, own_lists = 0, total, index;

	boolean game_map = strcmp(map_name, "ui") != 0;

#ifdef HALO_GAME_BROWSER
	/* (the save folder's game type files, in every menus' gametype lists:
	gametype_files.c) */
	if (!game_map)
		gametype_files_import();
#endif

	/* (ui.map, and a multiplayer map: its pause menu's SETTINGS) */
	if (game_map && tag_loaded('Soul', MULTIPLAYER_COLLECTION) == NONE &&
		tag_loaded(UI_WIDGET_DEFINITION_TAG, SOLO_PAUSE_SCREEN) == NONE)
	{
		return;
	}
	/* the Xbox's menus: only a multiplayer game's CHANGE TEAMS and the host's
	END GAME, and CONTROLS */
	if (menus_chosen() == _menus_xbox)
	{
		if (game_map && (pause_controls_offered() || (tag_loaded(UI_WIDGET_DEFINITION_TAG, SOLO_PAUSE_SCREEN) == NONE &&
			(global_network_game_server_get() != NULL || team_change_pause_offered()))))
		{
			pause_patch_xbox();
		}
		return;
	}
	menus = halo_menus_load();
	if (!menus)
		return;
	memset(&build, 0, sizeof(build));
	build.menus = menus;
	build.game_map = game_map;
	build.campaign_map = game_map && tag_loaded(UI_WIDGET_DEFINITION_TAG, SOLO_PAUSE_SCREEN) != NONE;
	widget_count = menus->widget_count;
	build.widget_tags = malloc((widget_count + 1) * sizeof(long));
	build.text_tags = malloc((widget_count + 1) * sizeof(long));
	build.spinner_tags = malloc((widget_count + 1) * sizeof(long));
	build.bitmap_tags = malloc((menus->bitmap_count + 1) * sizeof(long));
	build.strings_tags = malloc((menus->string_list_count + 1) * sizeof(long));
	if (!build.widget_tags || !build.text_tags || !build.spinner_tags || !build.bitmap_tags || !build.strings_tags)
		goto failed;
	for (index = 0; index < widget_count; index++)
	{
		own_lists += (menus->widgets[index].text != NULL) + (menus->widgets[index].strings != NULL);
		/* (names are unique) */
		if (widget_named(menus->widgets[index].name) != index)
			problem(menus->widgets[index].file, menus->widgets[index].line, "two widgets are named",
				menus->widgets[index].name);
	}
	total = widget_count + own_lists + menus->string_list_count + menus->bitmap_count +
		(game_map ? PAUSE_PATCH_TAGS : 0);
	instances = build.failed ? NULL : instances_grow(total, &build.first_index, &build.first_salt);
	if (!instances)
		goto failed;
	/* each new tag's index */
	for (index = 0; index < widget_count; index++)
	{
		build.widget_tags[index] = next_tag();
		build.text_tags[index] = menus->widgets[index].text ? next_tag() : NONE;
		build.spinner_tags[index] = menus->widgets[index].strings ? next_tag() : NONE;
	}
	for (index = 0; index < menus->string_list_count; index++)
		build.strings_tags[index] = next_tag();
	for (index = 0; index < menus->bitmap_count; index++)
		build.bitmap_tags[index] = next_tag();
	/* the tags: the bitmaps and strings first, which the widgets name; the
	widgets' names in the table before any is built, which they find there */
	for (index = 0; index < menus->bitmap_count && !build.failed; index++)
	{
		instance_set(instances, BITMAP_GROUP_TAG, build.bitmap_tags[index], menus->bitmaps[index].name, "",
			bitmap_build(&menus->bitmaps[index], build.bitmap_tags[index]));
	}
	for (index = 0; index < menus->string_list_count && !build.failed; index++)
	{
		struct halo_menu_strings const *list = &menus->string_lists[index];

		instance_set(instances, UNICODE_STRING_LIST_TAG, build.strings_tags[index], list->name, "",
			string_list_build(menus->strings + list->first, list->count));
	}
	for (index = 0; index < widget_count && !build.failed; index++)
	{
		struct halo_menu_widget const *widget = &menus->widgets[index];

		if (widget->text)
		{
			instance_set(instances, UNICODE_STRING_LIST_TAG, build.text_tags[index], widget->name, " text",
				string_list_build(&widget->text, 1));
		}
		if (widget->strings)
		{
			char const *pieces[MAXIMUM_STRINGS];
			long count = spinner_split(widget, FALSE, pieces);

			instance_set(instances, UNICODE_STRING_LIST_TAG, build.spinner_tags[index], widget->name, " strings",
				string_list_build(pieces, count));
		}
		instance_set(instances, UI_WIDGET_DEFINITION_TAG, build.widget_tags[index], widget->name, "", NULL);
	}
	if (build.failed)
		goto failed;
	/* (pause_patch's, until it makes them: nothing a name finds) */
	for (index = build.first_index + build.next; index < build.first_index + total; index++)
	{
		instances[index].group_tag = NONE;
		instances[index].tag_index = NONE;
#ifdef HALO_64BIT
		if (!menu_tags.empty_name)
			menu_tags.empty_name = allocate(1);
		instances[index].name = XBOX_ADDRESS(menu_tags.empty_name);
#else
		instances[index].name = "";
#endif
	}
	cache_files_set_tag_instances(instances, build.first_index + total);
	for (index = 0; index < widget_count && !build.failed; index++)
		instances[DATUM_INDEX_TO_ABSOLUTE_INDEX(build.widget_tags[index])].base_address = XBOX_ADDRESS(widget_build(index));
	if (build.failed)
		goto failed;
	if (game_map)
	{
		pause_patch(instances, TRUE);
		if (build.failed)
			goto failed;
		/* (those it made) */
		cache_files_set_tag_instances(instances, build.first_index + build.next);
	}
	else if (widget_named(menus_chosen() == _menus_chupathingy ? CHUPATHINGY_ROOT : menus->root) != NONE)
	{
		snprintf(menu_tags.root, sizeof(menu_tags.root), "%s%s", PC_MENU_TAG_PREFIX,
			menus_chosen() == _menus_chupathingy ? CHUPATHINGY_ROOT : menus->root);
		for (index = 0; menu_tags.root[index]; index++)
		{
			if (menu_tags.root[index] == '/')
				menu_tags.root[index] = '\\';
		}
	}
	else
	{
		platform_log("menus: there is no widget named %s, the main menu", menus->root);
	}
	menu_tags.loaded = TRUE;
	menu_tags.chupathingy = menus_chosen() == _menus_chupathingy;
	if (menu_tags.chupathingy && !game_map)
		host_setup_patch();
	platform_log("menus: %ld widgets, %ld string lists and %ld bitmaps added to the map's %ld tags",
		widget_count, own_lists + menus->string_list_count, menus->bitmap_count, build.first_index);
	goto done;

failed:
	platform_log("menus: not added; using the game's own menus");
	menu_tags_release();

done:
	free(build.widget_tags);
	free(build.text_tags);
	free(build.spinner_tags);
	free(build.strings_tags);
	free(build.bitmap_tags);
	memset(&build, 0, sizeof(build));
}

void menu_tags_unloaded(
	void)
{
	if (menu_tags.loaded || menu_tags.block_count)
		menu_tags_release();
}

char const *pc_menus_root_name(
	void)
{
	/* (debug.menu_open: a screen to see, a profile being edited for the
	settings' screens) */
	char const *open = config_string("debug.menu_open");

	if (menu_tags.loaded && *open && build.menus == NULL)
	{
		static char name[300];
		long index;

		snprintf(name, sizeof(name), "%s%s", PC_MENU_TAG_PREFIX, open);
		/* (after a #, what the screen shows: Controls Setup's "#rebind") */
		name[strcspn(name, "#")] = 0;
		for (index = 0; name[index]; index++)
		{
			if (name[index] == '/')
				name[index] = '\\';
		}
		if (tag_loaded(UI_WIDGET_DEFINITION_TAG, name) != NONE)
		{
			extern boolean pc_menu_profile_edit_begin(void);

			pc_menu_profile_edit_begin();
			return name;
		}
		platform_log("menus: debug.menu_open: there is no widget named %s", open);
	}
	return menu_tags.loaded && menu_tags.root[0] ? menu_tags.root : "ui\\shell\\main_menu\\main_menu";
}

boolean pc_menu_tag(
	long tag_index)
{
	return menu_tags.loaded && tag_index != NONE &&
		DATUM_INDEX_TO_ABSOLUTE_INDEX(tag_index) >= menu_tags.original_count;
}

/* where in its widget, and how large, a frame of ui.map's that a menu file
scales is drawn (<frame map=... width= height= x= y=>); FALSE for any other
bitmap, drawn as it is (ui_widget.c) */
boolean pc_menu_frame_placement(
	struct bitmap_data const *bitmap,
	short *x,
	short *y,
	short *width,
	short *height,
	short *texel_y)
{
	long index;

	for (index = 0; index < menu_tags.placement_count; index++)
	{
		if (menu_tags.placements[index].bitmap == bitmap)
		{
			*x = menu_tags.placements[index].x;
			*y = menu_tags.placements[index].y;
			*width = menu_tags.placements[index].width;
			*height = menu_tags.placements[index].height;
			*texel_y = menu_tags.placements[index].texel_y;
			return TRUE;
		}
	}
	return FALSE;
}

char const *pc_menu_function_name(
	long function_index)
{
	return function_index >= 0 && function_index < (long)NUMBEROF(port_function_names) ?
		port_function_names[function_index] : NULL;
}

/* a widget definition's string_index (port settings help: its label's) */
short pc_menu_string_index(
	long definition_index)
{
	struct ui_widget_definition *definition = tag_get(UI_WIDGET_DEFINITION_TAG, definition_index);

	return definition ? definition->string_list_index : NONE;
}

/* the screen the engine opens by name after a network game (the Xbox's),
or ours where the PC version's menus are up: the host's map select, the
lobby (the Chupathingy menus keep the Xbox's) */
char const *pc_menus_screen(
	char const *name)
{
	static struct
	{
		char const *game_name;
		char const *menu_name;
	} const screens[] =
	{
		{ "ui\\shell\\main_menu\\multiplayer_type_select\\connected\\connected_map_select_postgame_wrapper",
			"pc\\main_menu\\multiplayer_type_select\\connected\\connected_map_select_wrapper" },
		{ "ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\connected_pregame_screen",
			"pc\\main_menu\\multiplayer_type_select\\lobby\\lobby_screen" },
	};
	short index;

	if (!menu_tags.loaded || menu_tags.chupathingy)
		return name;
	for (index = 0; index < NUMBEROF(screens); index++)
	{
		if (!strcmp(name, screens[index].game_name))
			return screens[index].menu_name;
	}
	return name;
}

char const *pc_menu_game_data_input_name(
	long function_index)
{
	return function_index >= 0 && function_index < (long)NUMBEROF(port_game_data_input_names) ?
		port_game_data_input_names[function_index] : NULL;
}

struct pc_menu_setting *pc_menu_setting_get(
	long definition_index)
{
	long index;

	for (index = 0; index < menu_tags.setting_count; index++)
	{
		if (menu_tags.settings[index].definition_index == definition_index)
			return &menu_tags.settings[index];
	}
	return NULL;
}

/* ---------- the menus' button pictures as the keyboard's keys

ui.map's pictures of the controller's buttons in the menus' prompts ("A =
SELECT", "B = BACK": ui\shell\bitmaps\a_butn and the rest, and their
smaller _sm), drawn as the keyboard's key caps while the keyboard or mouse
was used last (halo_keyboard_prompts: xinput_sdl.c's platform_prompt_scheme,
which every prompt follows), and with the Chupathingy menus always
as the device last used names them (an Xbox-like pad's by snapchipper's
icons, a PlayStation or Nintendo pad's own), as Online Games draws its
prompts (ui_overlay.c: the keys xinput_sdl.c presses those buttons with), ending
where the picture ends, so that the words after it stay clear. */

#ifdef HALO_GAME_BROWSER
/* the button (UI_BUTTON_A...) a prompt's picture is drawn as a key cap
for, or -1 when it is drawn as it is */
static int pc_menu_key_cap_button(
	long bitmap_tag_index)
{
	static struct
	{
		char const *name;
		int button;
	} const buttons[] =
	{
		{ "a_butn", UI_BUTTON_A },
		{ "b_butn", UI_BUTTON_B },
		{ "x_butn", UI_BUTTON_X },
		{ "y_butn", UI_BUTTON_Y },
		{ "black_butn", UI_BUTTON_RIGHT_SHOULDER },
		{ "white_butn", UI_BUTTON_LEFT_SHOULDER },
		{ "start_butn", UI_BUTTON_START },
		{ "back_butn", UI_BUTTON_BACK },
	};
	char const *name;
	char const *last;
	long index;

	/* (the Chupathingy menus: always the modern glyphs of the device last
	used, a pad's too) */
	boolean modern = menu_tags.loaded && menu_tags.chupathingy;

	if (bitmap_tag_index == NONE || (!halo_keyboard_prompts() && !modern))
		return -1;
	name = tag_get_name(bitmap_tag_index);
	if (!name)
		return -1;
	/* (ui.map's, and the Chupathingy menus' own: chupathingy/prompts.xml) */
	if (!strncmp(name, "ui\\shell\\bitmaps\\", 17))
		last = name + 17;
	else if (modern && !strncmp(name, CHUPATHINGY_PROMPTS, strlen(CHUPATHINGY_PROMPTS)))
		last = name + strlen(CHUPATHINGY_PROMPTS);
	else
		return -1;
	for (index = 0; index < (long)(sizeof(buttons) / sizeof(buttons[0])); index++)
	{
		size_t length = strlen(buttons[index].name);

		if (!strncmp(last, buttons[index].name, length) && (!last[length] || !strcmp(last + length, "_sm")))
			return buttons[index].button;
	}
	return -1;
}
#endif

boolean pc_menu_button_key_cap(
	long bitmap_tag_index,
	short x0,
	short y0,
	short x1,
	short y1,
	float alpha)
{
#ifdef HALO_GAME_BROWSER
	int button = pc_menu_key_cap_button(bitmap_tag_index);

	(void)x0;
	if (button >= 0)
	{
		float size = (float)(y1 - y0) * 0.62f;
		float width = ui_overlay_button_width(button, size);
		unsigned int shade = (unsigned int)(alpha < 0.f ? 0.f : alpha > 1.f ? 255.f : alpha * 255.f);

		ui_overlay_button(button, size, (float)x1 - width,
			(float)y0 + ((float)(y1 - y0) - size) * 0.5f, 0xFFFFFF00 | shade);
		return TRUE;
	}
#else
	(void)bitmap_tag_index;
	(void)x0;
	(void)y0;
	(void)x1;
	(void)y1;
	(void)alpha;
#endif
	return FALSE;
}

/* the left edge of the key cap pc_menu_button_key_cap draws for a prompt
in these bounds (a wide one, ENTER's, reaches left of them), or x0 when
the picture is drawn as it is */
short pc_menu_button_key_cap_left(
	long bitmap_tag_index,
	short x0,
	short y0,
	short x1,
	short y1)
{
#ifdef HALO_GAME_BROWSER
	int button = pc_menu_key_cap_button(bitmap_tag_index);

	if (button >= 0)
	{
		float left = (float)x1 - ui_overlay_button_width(button, (float)(y1 - y0) * 0.62f);

		return (short)floorf(left);
	}
#else
	(void)bitmap_tag_index;
	(void)y0;
	(void)x1;
	(void)y1;
#endif
	return x0;
}
