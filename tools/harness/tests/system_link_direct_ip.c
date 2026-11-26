/*
SYSTEM_LINK_DIRECT_IP.C

System Link's list of games (#1880): the game's server_list_menu_update
(source/interface/ui_widget_game_data_input_functions.c) over a fake list
of nine rows and its description. With the Chupathingy menus
(pc_menus_direct_ip_list names the list) its last row is DIRECT IP, the only
one, and chosen, while no game is found; with the Xbox menus (NONE) the
list is the Xbox's, games only.
*/

#include <stdarg.h>
#include <wchar.h>

#include "harness.h"
#define match_vassert(file, line, condition, text) CHECK(condition, "%s:%d: %s", file, line, text)

#define HALO_GAME_BROWSER
#define CEILING(n, ceiling) ((n) > (ceiling) ? (ceiling) : (n))
#define csmemset memset
#define UNICODE_STRING_LIST_TAG 0x75737472
typedef unsigned short word;

#include "under_test_types.inc"

struct network_game_client { int unused; };
struct ui_widget_definition { short type; long child_count; };

static struct network_game_client client;
static struct network_advertised_game games[MAXIMUM_NETWORK_ADVERTISED_GAMES];
static struct ui_widget_definition list_definition = { _ui_widget_type_column_list, 9 };
static struct ui_widget_definition description_definition = { 0, 5 };
static long direct_ip_list = NONE;
static unsigned long now = 5000;

enum { LIST_TAG = 7, DESCRIPTION_TAG = 8 };

static struct network_game_client *global_network_game_client_get(void) { return &client; }
static struct network_advertised_game *network_game_client_get_available_games(struct network_game_client *c)
{
	(void)c;
	return games;
}
static boolean network_game_client_advertised_game_is_valid(struct network_advertised_game *game)
{
	return game->valid;
}
static struct ui_widget_definition *ui_widget_definition_get(long tag)
{
	return tag == LIST_TAG ? &list_definition : &description_definition;
}
static long pc_menus_direct_ip_list(void) { return direct_ip_list; }
static unsigned long system_milliseconds(void) { return now; }
static void *ui_widget_realloc(void *pointer, long size, char const *file, long line)
{
	(void)file;
	(void)line;
	return realloc(pointer, (size_t)size * sizeof(wchar_t));
}
static void ustrncpy(wchar_t *to, wchar_t const *from, long count) { wcsncpy(to, from, (size_t)count); }
static int usnprintf(wchar_t *buffer, long count, wchar_t const *format, ...)
{
	va_list arguments;
	int result;

	va_start(arguments, format);
	result = vswprintf(buffer, (size_t)count, format, arguments);
	va_end(arguments);
	return result;
}
static long tag_loaded(long group, char const *name) { (void)group; (void)name; return NONE; }
static wchar_t const *unicode_string_list_get_string(long tag, short index) { (void)tag; (void)index; return L"Closed"; }

#include "under_test.inc"

/* ---------- the fake screen */

static struct widget_instance list, rows[9], description;
static struct widget_instance texts[7];

static void link_children(struct widget_instance *parent, struct widget_instance *children, int count)
{
	int index;

	parent->child = &children[0];
	for (index = 0; index < count; index++)
	{
		children[index].parent = parent;
		children[index].next = index + 1 < count ? &children[index + 1] : NULL;
		children[index].previous = index ? &children[index - 1] : NULL;
	}
}

static void screen_new(boolean chupathingy)
{
	static struct widget_instance description_parts[4];
	int index;

	memset(&list, 0, sizeof(list));
	memset(rows, 0, sizeof(rows));
	memset(texts, 0, sizeof(texts));
	memset(games, 0, sizeof(games));
	list.definition_tag_index = LIST_TAG;
	list.type = _ui_widget_type_column_list;
	list.parameters.list.selected_index = 0;
	for (index = 0; index < 9; index++)
		rows[index].type = _ui_widget_type_text_box;
	link_children(&list, rows, 9);
	list.focused_child = &rows[0];
	/* (the description: game type, map, the texts' container, the message) */
	memset(description_parts, 0, sizeof(description_parts));
	link_children(&description, description_parts, 4);
	description.definition_tag_index = DESCRIPTION_TAG;
	for (index = 0; index < 7; index++)
		texts[index].type = _ui_widget_type_text_box;
	link_children(&description_parts[2], texts, 7);
	description_parts[3].type = _ui_widget_type_text_box;
	list.parameters.list.extended_description = &description;
	direct_ip_list = chupathingy ? LIST_TAG : NONE;
}

static struct widget_instance *message_text(void)
{
	return description.child->next->next->next;
}

static void game_add(int index, wchar_t const *name)
{
	games[index].valid = TRUE;
	games[index].open = TRUE;
	games[index].platform = _network_game_platform_xbox;
	games[index].engine_type = game_engine_slayer;
	games[index].player_count = 3;
	wcsncpy(games[index].game_name, name, 15);
	strcpy(games[index].map_name, "levels\\test\\bloodgulch\\bloodgulch");
}

static void update(void)
{
	server_list_menu_update(&list);
}

static boolean row_is(int index, wchar_t const *text)
{
	return rows[index].parameters.text_box.text && !wcscmp(rows[index].parameters.text_box.text, text);
}

/* ---------- the cases */

static void case_xbox(void)
{
	screen_new(FALSE);
	update();
	CHECK(list.parameters.list.number_of_items == 0, "%d", list.parameters.list.number_of_items);
	CHECK(list.parameters.list.selected_index == -1, "%d", list.parameters.list.selected_index);
	CHECK(message_text()->visible && message_text()->parameters.text_box.string_list_index == 1,
		"the Xbox's message: %d", message_text()->parameters.text_box.string_list_index);
	game_add(0, L"Red");
	game_add(1, L"Blue");
	update();
	CHECK(list.parameters.list.number_of_items == 2, "%d", list.parameters.list.number_of_items);
	CHECK(row_is(0, L"Red") && row_is(1, L"Blue"), "the games");
	CHECK(!rows[2].parameters.text_box.text, "no third row");
	CHECK(!message_text()->visible, "no message with games");
	/* (nine games: all nine) */
	for (int index = 2; index < 9; index++)
		game_add(index, L"Game");
	update();
	CHECK(list.parameters.list.number_of_items == 9, "%d", list.parameters.list.number_of_items);
	for (int index = 0; index < 9; index++)
		CHECK(!row_is(index, L"DIRECT IP"), "row %d", index);
}

static void case_empty(void)
{
	screen_new(TRUE);
	update();
	CHECK(list.parameters.list.number_of_items == 1, "%d", list.parameters.list.number_of_items);
	CHECK(row_is(0, L"DIRECT IP"), "the row");
	CHECK(((void **)list.parameters.list.list_items)[0] == NULL, "no game behind it");
	CHECK(list.parameters.list.selected_index == 0, "chosen: %d", list.parameters.list.selected_index);
	CHECK(message_text()->visible && message_text()->parameters.text_box.string_list_index == 0,
		"only \"Looking for games...\": %d", message_text()->parameters.text_box.string_list_index);
}

static void case_games(void)
{
	screen_new(TRUE);
	update();
	game_add(0, L"Red");
	game_add(1, L"Blue");
	game_add(2, L"Green");
	update();
	CHECK(list.parameters.list.number_of_items == 4, "%d", list.parameters.list.number_of_items);
	CHECK(row_is(0, L"Red") && row_is(1, L"Blue") && row_is(2, L"Green"), "the games");
	CHECK(row_is(3, L"DIRECT IP"), "the row last");
	/* (DIRECT IP was chosen with no games: it stays chosen) */
	CHECK(list.parameters.list.selected_index == 3, "%d", list.parameters.list.selected_index);
	CHECK(!message_text()->visible, "no message with games");
	/* a game chosen: its description; DIRECT IP's row is kept chosen as a
	game goes */
	list.parameters.list.selected_index = 1;
	update();
	CHECK(list.parameters.list.selected_index == 1, "%d", list.parameters.list.selected_index);
	CHECK(texts[3].parameters.text_box.string_list_index == 12 || texts[3].parameters.text_box.string_list_index == 13,
		"described");
	list.parameters.list.selected_index = 3;
	games[2].valid = FALSE;
	update();
	CHECK(list.parameters.list.number_of_items == 3, "%d", list.parameters.list.number_of_items);
	CHECK(row_is(2, L"DIRECT IP"), "the row last");
	CHECK(list.parameters.list.selected_index == 2, "%d", list.parameters.list.selected_index);
}

static void case_full(void)
{
	int index;

	screen_new(TRUE);
	for (index = 0; index < 9; index++)
		game_add(index, L"Game");
	update();
	CHECK(list.parameters.list.number_of_items == 9, "%d", list.parameters.list.number_of_items);
	CHECK(row_is(8, L"DIRECT IP"), "eight games, then the row");
	CHECK(((void **)list.parameters.list.list_items)[8] == NULL, "no game behind it");
}

int main(int argc, char **argv)
{
	CHECK(argc == 2, "a case");
	if (!strcmp(argv[1], "xbox"))
		case_xbox();
	else if (!strcmp(argv[1], "empty"))
		case_empty();
	else if (!strcmp(argv[1], "games"))
		case_games();
	else if (!strcmp(argv[1], "full"))
		case_full();
	else
		CHECK(FALSE, "no case %s", argv[1]);
	return 0;
}
