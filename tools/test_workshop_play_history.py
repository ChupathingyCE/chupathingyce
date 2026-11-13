"""The screen history the Delta Guide's Workshop PLAY gives the screen it
opens (source/interface/ui_widget.c's ui_widget_port_history_set, called by
port/linux/game/menu_functions.c's workshop_play_history): back from Create
Game or the level select goes where the menus' own would, down to the main
menu, never to no menu at all (#1133). Each screen in it is on its item
that opens the next, as the menus leave it, a screen of another menu set
found by its name's last part (Gearbox's MULTIPLAYER opens its own
Multiplayer menu). ui_widget.c's real functions, compiled against a few
stand-in widget tags, no maps or game needed."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest
from harness import function

ROOT = Path(__file__).resolve().parents[1]
WIDGET = (ROOT / "source/interface/ui_widget.c").read_text(encoding="latin-1")
MENUS = (ROOT / "port/linux/game/menu_functions.c").read_text(encoding="latin-1")

PRELUDE = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char boolean;
#define TRUE 1
#define FALSE 0
#define NONE -1
#define csstrcmp strcmp
#define xbox_pointer(address) ((void *)(address))
#define match_assert(file, line, condition) ((void)0)
#define match_vwarn(file, line, condition, message) ((void)0)
#define pool_new_pointer(pool, size, file, line) calloc(1, (size))
#define dispose_pointer(pool, pointer) free(pointer)
static int widget_memory_pool;
struct tag_reference { unsigned long group_tag; char *name; long name_length; long index; };
struct tag_block { long count; void *address; void *definition; };
struct ui_widget_event_handler_reference
{
	long flags; short event_type; short function;
	struct tag_reference widget_tag; struct tag_reference sound_effect; char script[32];
};
struct ui_widget_child_reference { struct tag_reference widget_tag; char name[32]; long flags; };
struct ui_widget_definition { struct tag_block event_handlers; struct tag_block child_widgets; };
struct widget_instance { long definition_tag_index; short local_player_index; };
struct widget_stack_data
{
	long previous_widget_tag; long focused_child_parent_widget_tag;
	short focused_child_index; short local_player_index;
};
struct widget_stack_node { struct widget_stack_data data; struct widget_stack_node *next; };
static struct
{
	struct widget_instance *active_widgets[4];
	struct widget_stack_node *widget_stack[4];
} widget_globals;

/* the stand-in tags: their names, and their definitions */
#define TAGS 16
static char const *names[TAGS];
static struct ui_widget_definition definitions[TAGS];
static char const *tag_get_name(long tag) { return tag >= 0 && tag < TAGS ? names[tag] : NULL; }
static struct ui_widget_definition *ui_widget_definition_get(long tag) { return &definitions[tag]; }
long ui_widget_port_active_screen_tag(void);
"""

DRIVER = r"""
enum
{
	CHUPA_MAIN, CHUPA_MAIN_LIST, CHUPA_CAMPAIGN_ITEM, CHUPA_MULTIPLAYER_ITEM,
	XBOX_MULTIPLAYER, MAP_SELECT, GEARBOX_MAIN, GEARBOX_MAIN_LIST, GEARBOX_MULTIPLAYER_ITEM,
	GEARBOX_MULTIPLAYER, PROFILE, PROFILE_LIST, PROFILE_ITEM, LEVEL_SELECT,
};

static struct ui_widget_child_reference children[TAGS][4];
static struct ui_widget_event_handler_reference handlers[TAGS][2];

static void tag(long index, char const *name)
{
	names[index] = name;
}

static void child(long parent, long index)
{
	struct ui_widget_definition *definition = &definitions[parent];

	children[parent][definition->child_widgets.count++].widget_tag.index = index;
	definition->child_widgets.address = children[parent];
}

static void opens(long item, long screen)
{
	struct ui_widget_definition *definition = &definitions[item];

	handlers[item][definition->event_handlers.count++].widget_tag.index = screen;
	definition->event_handlers.address = handlers[item];
}

static void up(long screen)
{
	static struct widget_instance widget;

	widget.definition_tag_index = screen;
	widget.local_player_index = NONE;
	widget_globals.active_widgets[0] = &widget;
}

static void show(char const *label)
{
	struct widget_stack_node *node;

	printf("%s:", label);
	for (node = widget_globals.widget_stack[0]; node; node = node->next)
		printf(" %s@%ld:%d", names[node->data.previous_widget_tag], node->data.focused_child_parent_widget_tag,
			node->data.focused_child_index);
	printf("\n");
}

long ui_widget_port_active_screen_tag(void)
{
	return widget_globals.active_widgets[0] ? widget_globals.active_widgets[0]->definition_tag_index : NONE;
}

int main(void)
{
	long screens[2];
	struct widget_stack_data data = { XBOX_MULTIPLAYER, 99, 3, NONE };

	tag(CHUPA_MAIN, "pc\\chupathingy\\main_menu");
	tag(CHUPA_MAIN_LIST, "pc\\chupathingy\\main_menu_list");
	tag(CHUPA_CAMPAIGN_ITEM, "pc\\chupathingy\\campaign_item");
	tag(CHUPA_MULTIPLAYER_ITEM, "pc\\chupathingy\\multiplayer_item");
	tag(XBOX_MULTIPLAYER, "ui\\shell\\main_menu\\multiplayer_type_select\\multiplayer_type_select_screen");
	tag(MAP_SELECT, "ui\\shell\\main_menu\\multiplayer_type_select\\connected\\connected_map_select_wrapper");
	tag(GEARBOX_MAIN, "pc\\main_menu\\main_menu");
	tag(GEARBOX_MAIN_LIST, "pc\\main_menu\\main_menu_list");
	tag(GEARBOX_MULTIPLAYER_ITEM, "pc\\main_menu\\main_menu_item_multiplayer");
	tag(GEARBOX_MULTIPLAYER, "pc\\main_menu\\multiplayer_type_select\\multiplayer_type_select_screen");
	tag(PROFILE, "ui\\shell\\main_menu\\load_campaign_player_profile");
	tag(PROFILE_LIST, "ui\\shell\\main_menu\\load_campaign_player_profile_list");
	tag(PROFILE_ITEM, "ui\\shell\\main_menu\\load_campaign_player_profile_item");
	tag(LEVEL_SELECT, "ui\\shell\\main_menu\\solo_level_select\\solo_level_select_screen");
	/* (a main menu's list: CAMPAIGN, then MULTIPLAYER) */
	child(CHUPA_MAIN, CHUPA_MAIN_LIST);
	child(CHUPA_MAIN_LIST, CHUPA_CAMPAIGN_ITEM);
	child(CHUPA_MAIN_LIST, CHUPA_MULTIPLAYER_ITEM);
	opens(CHUPA_CAMPAIGN_ITEM, PROFILE);
	opens(CHUPA_MULTIPLAYER_ITEM, XBOX_MULTIPLAYER);
	child(GEARBOX_MAIN, GEARBOX_MAIN_LIST);
	child(GEARBOX_MAIN_LIST, CHUPA_CAMPAIGN_ITEM);
	child(GEARBOX_MAIN_LIST, GEARBOX_MULTIPLAYER_ITEM);
	opens(GEARBOX_MULTIPLAYER_ITEM, GEARBOX_MULTIPLAYER);
	child(PROFILE, PROFILE_LIST);
	child(PROFILE_LIST, PROFILE_ITEM);
	opens(PROFILE_ITEM, LEVEL_SELECT);

	/* Create Game opened from the main menu: only the Multiplayer menu
	under it (what Online Games' Y pushes), then made the menus' own */
	up(MAP_SELECT);
	push_widget(&widget_globals.widget_stack[0], &data);
	show("before");
	screens[0] = CHUPA_MAIN;
	screens[1] = ui_widget_port_history_top();
	printf("set: %d\n", ui_widget_port_history_set(screens, 2, NONE));
	show("create_game");
	/* (again, with it already there: the same, not doubled) */
	ui_widget_port_history_set(screens, 2, NONE);
	show("again");

	/* Gearbox's main menu: its MULTIPLAYER opens its own Multiplayer menu */
	screens[0] = GEARBOX_MAIN;
	ui_widget_port_history_set(screens, 2, NONE);
	show("gearbox");

	/* the level select: the campaign's screen, then the main menu */
	up(LEVEL_SELECT);
	screens[0] = CHUPA_MAIN;
	screens[1] = PROFILE;
	ui_widget_port_history_set(screens, 2, NONE);
	show("campaign");

	/* the main menu alone (the profile screen PLAY opens first) */
	up(PROFILE);
	ui_widget_port_history_set(screens, 1, NONE);
	show("main_only");

	/* a screen not loaded: the history left as it was */
	screens[1] = NONE;
	printf("missing: %d\n", ui_widget_port_history_set(screens, 2, NONE));
	show("kept");
	return 0;
}
"""


@pytest.fixture(scope="module")
def output(tmp_path_factory):
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    code = PRELUDE + "\n".join(function(WIDGET, name) for name in (
        "push_widget", "dispose_widget_stack", "widget_name_last_part", "widget_definition_opens",
        "widget_definition_find_opener", "ui_widget_port_history_set", "ui_widget_port_history_top")) + DRIVER
    work = tmp_path_factory.mktemp("workshop-play-history")
    (work / "history.c").write_text(code)
    exe = work / "history"
    build = subprocess.run([compiler, "-std=gnu99", "-w", str(work / "history.c"), "-o", str(exe)],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
    return {key: value.split() for key, value in (line.split(":", 1) for line in run.stdout.splitlines())}


MULTIPLAYER = "ui\\shell\\main_menu\\multiplayer_type_select\\multiplayer_type_select_screen"


def test_create_game_had_no_main_menu_under_it(output):
    # (what #1133 left: back from the Multiplayer menu had nothing to go to)
    assert output["before"] == [MULTIPLAYER + "@99:3"]


def test_create_game_goes_back_to_multiplayer_then_the_main_menu(output):
    assert output["set"] == ["1"]
    # (the Multiplayer menu keeps ONLINE GAMES' place; the main menu on MULTIPLAYER, its second item)
    assert output["create_game"] == [MULTIPLAYER + "@99:3", "pc\\chupathingy\\main_menu@1:1"]
    assert output["again"] == output["create_game"]


def test_another_menu_sets_screen_is_found_by_its_name(output):
    assert output["gearbox"] == [MULTIPLAYER + "@99:3", "pc\\main_menu\\main_menu@7:1"]


def test_the_level_select_goes_back_to_the_campaign_screen_then_the_main_menu(output):
    assert output["campaign"] == ["ui\\shell\\main_menu\\load_campaign_player_profile@11:0",
                                  "pc\\chupathingy\\main_menu@1:0"]


def test_a_screen_with_only_the_main_menu_under_it(output):
    assert output["main_only"] == ["pc\\chupathingy\\main_menu@1:0"]


def test_a_screen_not_loaded_leaves_the_history(output):
    assert output["missing"] == ["0"]
    assert output["kept"] == output["main_only"]


def test_every_screen_play_opens_gets_the_history():
    body = function(MENUS, "workshop_play_open")
    opens = len(re.findall(r"browser_screen_create_game\(\)|ui_widget_port_open_screen\(", body))
    assert opens == 4
    assert len(re.findall(r"workshop_play_history\(", body)) == opens
