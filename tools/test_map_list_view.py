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
	boolean present[NUMBER_OF_UI_MAP_LIST_SOURCES]; } view;
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
	ui_map_list_found_multiplayer = ui_map_list_count_value;
	add("a10", _ui_map_list_source_xbox, 0);
	ui_map_list_found_campaign_end = ui_map_list_count_value;
	first_item = "mcc_maps\\7\\";
	set(_ui_map_list_source_all, _ui_map_list_sort_default, "");
	show("first");
	set(_ui_map_list_source_ce, _ui_map_list_sort_name, "timber");
	show("first_narrowed");
	first_item = "";
	set(_ui_map_list_source_all, _ui_map_list_sort_default, "");
	show("first_reset");
	return 0;
}
"""


@pytest.fixture(scope="module")
def output(tmp_path_factory):
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    code = PRELUDE + "\n".join(function(SOURCE, name) for name in (
        "view_lower", "view_contains", "view_matches", "view_compare", "view_apply")) + DRIVER
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


def test_the_item_play_handed_over_leads_the_rows(output):
    # (its maps in their own order, before the rest of the view's)
    assert rows(output, "first") == ["mcc_maps\\7\\beta", "mcc_maps\\7\\alpha", "bloodgulch", "timberland",
                                     "|", "a10"]
    # (shown whatever Show and Search say, in the view's order)
    assert rows(output, "first_narrowed") == ["mcc_maps\\7\\alpha", "mcc_maps\\7\\beta", "timberland", "|", "a10"]


def test_the_item_let_go_leaves_the_list_as_it_was(output):
    assert rows(output, "first_reset") == ["bloodgulch", "timberland", "mcc_maps\\7\\beta", "mcc_maps\\7\\alpha",
                                           "|", "a10"]


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


def test_maps_left_out_of_a_full_list_are_counted(tmp_path):
    """a map with no room left is counted and named for the log (ui_map_list_refresh), never dropped in silence"""
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    prelude = r"""
#include <stdio.h>
#include <string.h>
#include <wchar.h>
typedef int boolean;
#define FALSE 0
#define MAXIMUM_MAP_LIST 3
#define MAP_NAME_LENGTH 64
#define DISPLAY_NAME_LENGTH 32
#define DESCRIPTION_LENGTH 32
struct ui_map_entry { char map_name[MAP_NAME_LENGTH]; wchar_t display_name[DISPLAY_NAME_LENGTH],
	lobby_name[DISPLAY_NAME_LENGTH], pane_name[DISPLAY_NAME_LENGTH], description[DESCRIPTION_LENGTH];
	short xbox_index, picture_index; wchar_t const *game_type; short source; boolean added_known; long long added; };
static struct ui_map_entry ui_map_list[MAXIMUM_MAP_LIST];
static long ui_map_list_count_value, ui_map_list_left_out;
static short ui_map_list_adding_source;
static char ui_map_list_first_left_out[MAP_NAME_LENGTH];
static void wide_copy(wchar_t *out, long size, wchar_t const *text) { wcsncpy(out, text, (size_t)size - 1); }
"""
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
    (tmp_path / "add.c").write_text(prelude + function(SOURCE, "add_entry") + driver)
    build = subprocess.run([compiler, "-std=gnu99", "-w", str(tmp_path / "add.c"), "-o", str(tmp_path / "add")],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    result = subprocess.run([str(tmp_path / "add")], capture_output=True, text=True, check=True)
    assert result.stdout.split() == ["3", "2", "mcc_maps\\3477809506\\1"]
