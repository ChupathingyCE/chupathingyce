"""The map choosers' Search, Show and Sort (port/linux/game/ui_map_list.c's
view): which maps a view shows and in what order, with the list's own
functions compiled against a small stand-in list, no maps or game needed.
MCC maps are left out of the rows while a network game is hosted. The
Workshop item the Delta Guide's PLAY hands over (mcc_maps_set_first_item)
leads the rows, whatever the view."""
import shutil
import subprocess
from pathlib import Path

import pytest
from harness import function

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "port/linux/game/ui_map_list.c").read_text()

PRELUDE = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define NONE -1
#define MAXIMUM_MAP_LIST 16
#define MAP_NAME_LENGTH 64
#define DISPLAY_NAME_LENGTH 32
#define _stricmp strcasecmp
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
static struct { short source, sort; char search[48]; unsigned long serial;
	boolean present[NUMBER_OF_UI_MAP_LIST_SOURCES]; boolean coop_only; } view;
static boolean hosting, view_mcc_offered;
boolean ui_map_list_mcc_offered(void) { return !hosting; }
static void view_entry_name(struct ui_map_entry const *entry, char *name, long size)
{ snprintf(name, (size_t)size, "%s", entry->map_name); }
static char const *view_package_text(char const *level)
{ return strstr(level, "sidewinder") ? "author: sarge" : ""; }
static long long view_entry_added(struct ui_map_entry *entry) { return entry->added; }
static char const *first_item = "";
static boolean mcc_maps_level_first(char const *level)
{ return *first_item && !strncasecmp(level, first_item, strlen(first_item)); }
static char const *mcc_maps_first_item(void) { return first_item; }
static char view_item[80];
static boolean view_item_only;
"""

DRIVER = r"""
static void add(char const *name, short source, long long added)
{
	struct ui_map_entry *entry = &ui_map_list[ui_map_list_count_value++];
	snprintf(entry->map_name, sizeof(entry->map_name), "%s", name);
	entry->source = source;
	entry->added = added;
}

static void show(char const *label)
{
	long row;
	printf("%s:", label);
	for (row = 0; row < ui_map_list_campaign_end; row++)
		printf(" %s%s", row == ui_map_list_multiplayer_count ? "| " : "", ROW(row)->map_name);
	printf("\n");
}

static void set(short source, short sort, char const *search)
{
	view.source = source;
	view.sort = sort;
	snprintf(view.search, sizeof(view.search), "%s", search);
	view_apply();
}

int main(void)
{
	add("bloodgulch", _ui_map_list_source_xbox, 1);
	add("timberland", _ui_map_list_source_ce, 5);
	add("mcc_maps\\1\\sidewinder_x", _ui_map_list_source_mcc, 9);
	add("Damnation", _ui_map_list_source_xbox, 2);
	ui_map_list_found_multiplayer = ui_map_list_count_value;
	add("a10", _ui_map_list_source_xbox, 0);
	add("mcc_maps\\2\\windfall", _ui_map_list_source_mcc, 0);
	ui_map_list_found_campaign_end = ui_map_list_count_value;
	set(_ui_map_list_source_all, _ui_map_list_sort_default, "");
	show("default");
	set(_ui_map_list_source_all, _ui_map_list_sort_name, "");
	show("az");
	set(_ui_map_list_source_all, _ui_map_list_sort_recent, "");
	show("recent");
	set(_ui_map_list_source_mcc, _ui_map_list_sort_default, "");
	show("mcc");
	set(_ui_map_list_source_all, _ui_map_list_sort_default, "GULCH");
	show("search");
	set(_ui_map_list_source_all, _ui_map_list_sort_default, "sarge");
	show("author");
	hosting = TRUE;
	set(_ui_map_list_source_all, _ui_map_list_sort_default, "");
	show("hosting");
	hosting = FALSE;
	ui_map_list_count_value = 0;
	add("bloodgulch", _ui_map_list_source_xbox, 1);
	add("timberland", _ui_map_list_source_ce, 5);
	add("mcc_maps\\7\\beta", _ui_map_list_source_mcc, 3);
	add("mcc_maps\\7\\alpha", _ui_map_list_source_mcc, 4);
	add("mcc_maps\\8\\other", _ui_map_list_source_mcc, 5);
	ui_map_list_found_multiplayer = ui_map_list_count_value;
	add("a10", _ui_map_list_source_xbox, 0);
	add("mcc_maps\\7\\gamma", _ui_map_list_source_mcc, 0);
	ui_map_list_found_campaign_end = ui_map_list_count_value;
	first_item = "mcc_maps\\7\\";
	set(_ui_map_list_source_all, _ui_map_list_sort_default, "");
	show("first");
	set(_ui_map_list_source_ce, _ui_map_list_sort_name, "timber");
	show("first_narrowed");
	/* (an item with no multiplayer maps here: the list as it was) */
	first_item = "mcc_maps\\9\\";
	set(_ui_map_list_source_all, _ui_map_list_sort_default, "");
	show("first_campaign_only");
	/* (hosting a network game, MCC maps not offered: the list as it is then) */
	first_item = "mcc_maps\\7\\";
	hosting = TRUE;
	set(_ui_map_list_source_all, _ui_map_list_sort_default, "");
	show("first_hosting");
	hosting = FALSE;
	first_item = "";
	set(_ui_map_list_source_all, _ui_map_list_sort_default, "");
	show("first_reset");
	/* (a co-op game's Select Map, ONLINE CO-OP's: the levels alone, #1736;
	then any other's, the maps again) */
	view.coop_only = TRUE;
	set(_ui_map_list_source_all, _ui_map_list_sort_default, "");
	show("coop");
	view.coop_only = FALSE;
	set(_ui_map_list_source_all, _ui_map_list_sort_default, "");
	show("coop_after");
	return 0;
}
"""


@pytest.fixture(scope="module")
def output(tmp_path_factory):
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    code = PRELUDE + "\n".join(function(SOURCE, name) for name in (
        "view_lower", "view_contains", "view_item_entry", "view_matches", "view_compare", "view_campaign_row", "view_apply")) + DRIVER
    work = tmp_path_factory.mktemp("map-list-view")
    (work / "view.c").write_text(code)
    exe = work / "view"
    build = subprocess.run([compiler, "-std=gnu99", "-w", str(work / "view.c"), "-o", str(exe)],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
    return dict(line.split(":", 1) for line in run.stdout.splitlines())


def rows(output, label):
    return output[label].split()


def test_default_keeps_the_list_order(output):
    assert rows(output, "default") == ["bloodgulch", "timberland", "mcc_maps\\1\\sidewinder_x", "Damnation",
                                       "|", "a10", "mcc_maps\\2\\windfall"]


def test_sort_by_name_and_by_recently_added(output):
    assert rows(output, "az")[:4] == ["bloodgulch", "Damnation", "mcc_maps\\1\\sidewinder_x", "timberland"]
    assert rows(output, "recent")[:4] == ["mcc_maps\\1\\sidewinder_x", "timberland", "Damnation", "bloodgulch"]


def test_show_and_search_narrow_the_multiplayer_rows(output):
    assert rows(output, "mcc")[:2] == ["mcc_maps\\1\\sidewinder_x", "|"]
    assert rows(output, "search")[:2] == ["bloodgulch", "|"]
    # (a Workshop item's author, from its package's text)
    assert rows(output, "author")[:2] == ["mcc_maps\\1\\sidewinder_x", "|"]
    # (the campaign's rows stay, whatever the view)
    assert rows(output, "search")[-2:] == ["a10", "mcc_maps\\2\\windfall"]


def test_no_mcc_maps_while_hosting_a_network_game(output):
    assert rows(output, "hosting") == ["bloodgulch", "timberland", "Damnation", "|", "a10"]


def test_the_item_play_handed_over_is_all_the_rows(output):
    """#1301: PLAY's map select listed every installed map, the item's first; it lists the item's maps alone"""
    # (its maps in their own order, its campaign level after them)
    assert rows(output, "first") == ["mcc_maps\\7\\beta", "mcc_maps\\7\\alpha", "|", "mcc_maps\\7\\gamma"]
    # (whatever Show and Search say, in the view's order)
    assert rows(output, "first_narrowed") == ["mcc_maps\\7\\alpha", "mcc_maps\\7\\beta", "|", "mcc_maps\\7\\gamma"]


def test_an_item_without_multiplayer_maps_keeps_the_list(output):
    assert rows(output, "first_campaign_only") == ["bloodgulch", "timberland", "mcc_maps\\7\\beta",
                                                   "mcc_maps\\7\\alpha", "mcc_maps\\8\\other", "|", "a10",
                                                   "mcc_maps\\7\\gamma"]
    assert rows(output, "first_hosting") == ["bloodgulch", "timberland", "|", "a10"]


def test_the_item_let_go_leaves_the_list_as_it_was(output):
    assert rows(output, "first_reset") == ["bloodgulch", "timberland", "mcc_maps\\7\\beta", "mcc_maps\\7\\alpha",
                                           "mcc_maps\\8\\other", "|", "a10", "mcc_maps\\7\\gamma"]


def test_coop_game_lists_the_levels_alone(output):
    """#1736: ONLINE CO-OP's Select Map listed the multiplayer maps before the campaign's levels"""
    assert rows(output, "coop") == ["|", "a10", "mcc_maps\\7\\gamma"]
    assert rows(output, "coop_after") == rows(output, "first_reset")


def test_desktop_list_has_room_for_the_mcc_catalog():
    """#1137: the MCC catalog's maps come after the Custom Edition, HaloMD and Halo PC ones, its campaign maps
    last of all; a list of 256 rows left a large Workshop collection and every campaign map out. A computer's
    list holds the whole catalog beside a large Custom Edition folder, and every row's string index fits"""
    import re
    block = SOURCE[SOURCE.index("#ifdef HALO_CONSOLE_MAP_LIST_ROWS\n\tMAXIMUM_MAP_LIST"):]
    desktop = int(re.search(r"#else[^#]*MAXIMUM_MAP_LIST = (\d+),", block).group(1))
    header = (ROOT / "port/linux/game/mcc_maps.h").read_text()
    catalog = int(re.search(r"#define MCC_MAPS_MAXIMUM (\d+)", header).group(1))
    assert desktop >= 13 + catalog + 1024
    kinds = (ROOT / "port/linux/include/halo_ui_map_list.h").read_text()
    per_row = len(re.findall(r"^\s*_ui_map_list_string_\w+,", kinds, re.M))
    assert per_row == 5
    assert 0x4000 + desktop * per_row <= 0x7FFF


ADD_ENTRY_PRELUDE = r"""
#include <stdio.h>
#include <string.h>
#include <wchar.h>
typedef int boolean;
#define FALSE 0
#define TRUE 1
#define csmemcpy memcpy
#define MAXIMUM_MAP_LIST 3
#define MAP_NAME_LENGTH 64
#define DISPLAY_NAME_LENGTH 32
#define DESCRIPTION_LENGTH 32
#ifndef MAP_NAME_POOL
#define MAP_NAME_POOL (MAXIMUM_MAP_LIST * MAP_NAME_LENGTH)
#endif
#ifndef TEXT_POOL
#define TEXT_POOL (MAXIMUM_MAP_LIST * (3 * DISPLAY_NAME_LENGTH + DESCRIPTION_LENGTH))
#endif
struct ui_map_entry { char *map_name; wchar_t const *display_name, *lobby_name, *pane_name, *description;
	short xbox_index, picture_index; wchar_t const *game_type; short source; boolean added_known; long long added; };
static struct ui_map_entry ui_map_list[MAXIMUM_MAP_LIST];
static long ui_map_list_count_value, ui_map_list_left_out;
static short ui_map_list_adding_source;
static char ui_map_list_first_left_out[MAP_NAME_LENGTH];
static char ui_map_list_name_pool[MAP_NAME_POOL];
static wchar_t ui_map_list_text_pool[TEXT_POOL];
static long ui_map_list_name_pool_used, ui_map_list_text_pool_used, ui_map_list_descriptions_dropped;
"""


def add_entry_code() -> str:
    return "\n".join(function(SOURCE, name) for name in (
        "wide_length", "text_pool_add", "text_same", "add_entry_with_pane", "add_entry"))


def build_add_entry(tmp_path, code: str, defines=()):
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    (tmp_path / "add.c").write_text(code)
    build = subprocess.run([compiler, "-std=gnu99", "-w", *defines, str(tmp_path / "add.c"), "-o", str(tmp_path / "add")],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    return subprocess.run([str(tmp_path / "add")], capture_output=True, text=True, check=True).stdout


def test_maps_left_out_of_a_full_list_are_counted(tmp_path):
    """a map with no room left is counted and named for the log (ui_map_list_refresh), never dropped in silence"""
    driver = r"""
int main(void)
{
	static char const *const names[] = { "a", "b", "c", "mcc_maps\\3477809506\\1", "mcc_maps\\2886943774\\a10" };
	int index;
	for (index = 0; index < 5; index++)
		add_entry(names[index], L"n", L"n", L"d", -1, 0);
	printf("%ld %ld %s\n", ui_map_list_count_value, ui_map_list_left_out, ui_map_list_first_left_out);
	return 0;
}
"""
    result = build_add_entry(tmp_path, ADD_ENTRY_PRELUDE + add_entry_code() + driver)
    assert result.split() == ["3", "2", "mcc_maps\\3477809506\\1"]


def test_rows_text_is_pooled(tmp_path):
    """a row's names and description are held once at their own length: the pane's name is the lobby's unless it
    differs, a description an earlier row's when the same, and a pool too full for a description drops only that,
    for names the row (counted as left out)"""
    driver = r"""
int main(void)
{
	add_entry("a", L"Alpha", L"Alpha [CE]", L"same", -1, 0);
	add_entry_with_pane("b", L"Beta", L"Beta [MCC]", L"Be-\r\nta", L"same", -1, 0);
	printf("%d %d %d\n", ui_map_list[0].pane_name == ui_map_list[0].lobby_name,
		ui_map_list[1].description == ui_map_list[0].description, !wcscmp(ui_map_list[1].pane_name, L"Be-\r\nta"));
	printf("%ld %s %s\n", ui_map_list_text_pool_used, ui_map_list[0].map_name, ui_map_list[1].map_name);
	/* (room for these names, not the description) */
	add_entry("c", L"Gam", L"Gam", L"a long description", -1, 0);
	printf("%ld %ld %d %ld\n", ui_map_list_count_value, ui_map_list_descriptions_dropped,
		ui_map_list[2].description[0], ui_map_list_left_out);
	ui_map_list_count_value = 2;
	ui_map_list_text_pool_used = 48;
	add_entry("d", L"Delta", L"Delta", L"", -1, 0);
	printf("%ld %ld %s %ld\n", ui_map_list_count_value, ui_map_list_left_out, ui_map_list_first_left_out,
		ui_map_list_text_pool_used);
	return 0;
}
"""
    # (the pool: Alpha 6 + Alpha [CE] 11 + same 5 + Beta 5 + Beta [MCC] 11 + Be-\r\nta 8 = 46, then Gam 4 twice: 54, full)
    result = build_add_entry(tmp_path, ADD_ENTRY_PRELUDE + add_entry_code() + driver, ["-DTEXT_POOL=54"])
    assert result.splitlines() == ["1 1 1", "46 a b", "3 1 0 0", "2 1 d 48"]


def test_pack_maps_left_out_of_a_full_list_are_counted(tmp_path):
    """warthog-xbox #145: a pack's map with no room left is counted and named for the log too"""
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    prelude = r"""
#include <stdio.h>
#include <string.h>
typedef int boolean;
#define MAXIMUM_MAP_LIST 2
#define MAP_NAME_LENGTH 64
struct found_files { char names[MAXIMUM_MAP_LIST][MAP_NAME_LENGTH]; long count; };
static long ui_map_list_left_out;
static char ui_map_list_first_left_out[MAP_NAME_LENGTH];
static boolean map_pack_map_name(char const *pack, char const *file, char *map, long size)
{
	snprintf(map, (size_t)size, "%s@%s", file, pack);
	return 1;
}
"""
    driver = r"""
int main(void)
{
	static struct found_files found;
	static char const *const files[] = { "bigass", "dustbowl", "hydrolysis", "coldsnap" };
	int index;
	for (index = 0; index < 4; index++)
		pack_map_found("nhe", files[index], &found);
	printf("%ld %ld %s\n", found.count, ui_map_list_left_out, ui_map_list_first_left_out);
	return 0;
}
"""
    (tmp_path / "pack.c").write_text(prelude + function(SOURCE, "pack_map_found") + driver)
    build = subprocess.run([compiler, "-std=gnu99", "-w", str(tmp_path / "pack.c"), "-o", str(tmp_path / "pack")],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    result = subprocess.run([str(tmp_path / "pack")], capture_output=True, text=True, check=True)
    assert result.stdout.split() == ["2", "2", "hydrolysis@nhe"]
