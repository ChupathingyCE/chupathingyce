"""The Workshop item the Delta Guide's PLAY hands to the menus
(port/linux/game/mcc_maps.c's mcc_maps_set_first_item): its maps lead each
type's rows of mcc_maps_type_index, the rest after them in the catalog's
order; the campaign list opens on the item's first level once
(mcc_maps_take_first_focus); and letting it go (NULL) gives the catalog's
own order back. The catalog's real functions, compiled against a small
stand-in catalog, no maps or game needed."""
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest
from harness import function

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "port/linux/game/mcc_maps.c").read_text(encoding="latin-1")

PRELUDE = r"""
#include <stdio.h>
#include <string.h>
#include <strings.h>
typedef unsigned char boolean;
#define TRUE 1
#define FALSE 0
#define NONE -1
#define MCC_MAPS_LEVEL_PREFIX "mcc_maps\\"
#define _strnicmp strncasecmp
struct mcc_menu_map { char level_name[64]; boolean campaign; };
static struct { short count; struct mcc_menu_map maps[16]; } mcc_menu_catalog;
static void mcc_maps_scan(void) {}
short mcc_maps_type_count(boolean campaign);
short mcc_maps_type_index(short row, boolean campaign);
char const *mcc_maps_level_name(short index);
"""

DRIVER = r"""
static void add(char const *name, boolean campaign)
{
	struct mcc_menu_map *map = &mcc_menu_catalog.maps[mcc_menu_catalog.count++];
	snprintf(map->level_name, sizeof(map->level_name), "%s", name);
	map->campaign = campaign;
}

static void show(char const *label, boolean campaign)
{
	short row;
	printf("%s:", label);
	for (row = 0; row < mcc_maps_type_count(campaign); row++)
		printf(" %s", mcc_maps_level_name(mcc_maps_type_index(row, campaign)));
	printf(" end=%d\n", mcc_maps_type_index(mcc_maps_type_count(campaign), campaign));
}

int main(void)
{
	add("mcc_maps\\1\\aerial", FALSE);
	add("mcc_maps\\2\\crashsite", TRUE);
	add("mcc_maps\\3\\desert", FALSE);
	add("mcc_maps\\3\\dunes", FALSE);
	add("mcc_maps\\3\\oasis", TRUE);
	add("mcc_maps\\33\\elsewhere", FALSE);
	show("plain_mp", FALSE);
	show("plain_campaign", TRUE);
	mcc_maps_set_first_item("3");
	show("first_mp", FALSE);
	show("first_campaign", TRUE);
	{
		/* (in turn: a call's arguments are evaluated in no set order) */
		int first = mcc_maps_take_first_focus(), second = mcc_maps_take_first_focus();

		printf("focus: %d %d\n", first, second);
	}
	mcc_maps_set_first_item(NULL);
	show("reset_mp", FALSE);
	show("reset_campaign", TRUE);
	printf("reset_focus: %d\n", mcc_maps_take_first_focus());
	/* (an item with no campaign level: no focus) */
	mcc_maps_set_first_item("1");
	printf("mp_item_focus: %d\n", mcc_maps_take_first_focus());
	return 0;
}
"""


def first_state(source):
    match = re.search(r"static struct\s*\{[^}]*\}\s*mcc_maps_first;", source)
    assert match, "mcc_maps.c's mcc_maps_first is not found"
    return match.group(0)


@pytest.fixture(scope="module")
def output(tmp_path_factory):
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    code = PRELUDE + first_state(SOURCE) + "\n" + "\n".join(function(SOURCE, name) for name in (
        "mcc_maps_set_first_item", "mcc_maps_level_first", "mcc_maps_take_first_focus", "mcc_maps_type_count",
        "mcc_maps_type_index", "mcc_maps_level_name")) + DRIVER
    work = tmp_path_factory.mktemp("mcc-maps-first")
    (work / "first.c").write_text(code)
    exe = work / "first"
    build = subprocess.run([compiler, "-std=gnu99", "-w", str(work / "first.c"), "-o", str(exe)],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
    return {key: value.split() for key, value in (line.split(":", 1) for line in run.stdout.splitlines())}


def test_the_catalog_order_with_no_item(output):
    assert output["plain_mp"] == ["mcc_maps\\1\\aerial", "mcc_maps\\3\\desert", "mcc_maps\\3\\dunes",
                                  "mcc_maps\\33\\elsewhere", "end=-1"]
    assert output["plain_campaign"] == ["mcc_maps\\2\\crashsite", "mcc_maps\\3\\oasis", "end=-1"]


def test_the_items_maps_lead_each_type_then_the_rest(output):
    # (its own in their order, then the others; a longer id with the same start is another item)
    assert output["first_mp"] == ["mcc_maps\\3\\desert", "mcc_maps\\3\\dunes", "mcc_maps\\1\\aerial",
                                  "mcc_maps\\33\\elsewhere", "end=-1"]
    assert output["first_campaign"] == ["mcc_maps\\3\\oasis", "mcc_maps\\2\\crashsite", "end=-1"]


def test_the_campaign_list_opens_on_the_item_once(output):
    assert output["focus"] == ["1", "0"]
    assert output["mp_item_focus"] == ["0"]


def test_letting_the_item_go_gives_the_catalog_order_back(output):
    assert output["reset_mp"] == output["plain_mp"]
    assert output["reset_campaign"] == output["plain_campaign"]
    assert output["reset_focus"] == ["0"]


CARD = r"""
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MCC_MAP_DESCRIPTION_MAXIMUM 255
"""

CARD_DRIVER = r"""
int main(int count, char **arguments)
{
	int index;

	for (index = 1; index < count; index++)
	{
		wchar_t description[MCC_MAP_DESCRIPTION_MAXIMUM + 1];
		short used = mcc_map_card_lines(arguments[index], description, 0), at;

		for (at = 0; at < used; at++)
			putchar(description[at] == '\r' ? '|' : description[at] == '\n' ? '/' : (char)description[at]);
		putchar('\n');
	}
	return 0;
}
"""


@pytest.fixture(scope="module")
def card_lines(tmp_path_factory):
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    code = CARD + re.search(r"#define MCC_MAP_CARD_LINE .*?\n#define MCC_MAP_CARD_LINES .*?\n", SOURCE, re.S).group(0) + \
        function(SOURCE, "mcc_map_card_lines") + CARD_DRIVER
    work = tmp_path_factory.mktemp("mcc-card-lines")
    (work / "card.c").write_text(code)
    exe = work / "card"
    build = subprocess.run([compiler, "-std=gnu99", "-w", str(work / "card.c"), "-o", str(exe)],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stderr

    def run(*titles):
        result = subprocess.run([str(exe), *titles], capture_output=True, text=True, check=True)
        return [line.split("|/") for line in result.stdout.splitlines()]
    return run


def test_a_map_cards_item_title_breaks_between_words(card_lines):
    """the cards' lines don't wrap, so the item's title is broken there:
    whole words a line, nothing cut mid-word"""
    lines = card_lines("The Excavated Map Pack", "Desert Warthog")
    assert lines[0] == ["The Excavated Map", "Pack"]
    assert lines[1] == ["Desert Warthog"]


def test_a_long_title_ends_with_dots_in_three_lines(card_lines):
    [lines] = card_lines("HCE Multiplayer Extended The Complete Collection of Every Classic Map Remastered")
    assert len(lines) == 3 and lines[-1].endswith("...")
    assert all(len(line) <= 17 for line in lines)


def test_one_long_word_is_split(card_lines):
    [lines] = card_lines("Supercalifragilisticexpialidocious")
    assert all(len(line) <= 17 for line in lines) and "".join(lines) == "Supercalifragilisticexpialidocious"


# ---------- registering a map file (mcc_map_register): a real catalog entry, or a plain reason

REGISTER = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wchar.h>
#include "mcc_cache_format.h"
typedef unsigned char boolean;
#define TRUE 1
#define FALSE 0
#define csstrcasecmp strcasecmp
#define MCC_MAPS_LEVEL_PREFIX "mcc_maps\\"
#define MCC_MAPS_MAXIMUM 6
#define MCC_WORKSHOP_ID_SIZE 24
static char test_directory[512];
#define MCC_MAPS_DIRECTORY test_directory
struct mcc_workshop_item { char id[MCC_WORKSHOP_ID_SIZE]; };
static int mcc_workshop_local_item(char const *level, struct mcc_workshop_item *item)
{ (void)level; (void)item; return 0; }
"""

REGISTER_DRIVER = r"""
int main(int count, char **arguments)
{
	int index;

	/* (the game's folder is d:\\maps_mcc\\, short: the test runs in its folder) */
	(void)arguments[1];
	test_directory[0] = 0;
	for (index = 2; index < count; index++)
	{
		char const *detail = "", *reason = mcc_map_register(arguments[index], &detail);

		printf("%s=%s|%s|%d\n", arguments[index], reason ? reason : "listed", reason ? detail : "",
			mcc_menu_catalog.count);
	}
	for (index = 0; index < mcc_menu_catalog.count; index++)
		printf("entry=%s|%d\n", mcc_menu_catalog.maps[index].level_name, mcc_menu_catalog.maps[index].campaign);
	return 0;
}
"""


def mcc_header(scenario_type=1, version=13, size=0x900, length=None, tag_offset=0x800, tag_size=0x100):
    import struct
    data = bytearray(size)
    if size >= 0x800:
        struct.pack_into("<4s", data, 0, b"daeh")
        struct.pack_into("<II", data, 4, version, size if length is None else length)
        struct.pack_into("<II", data, 0x10, tag_offset, tag_size)
        data[0x20:0x25] = b"test\0"
        data[0x40:0x45] = b"01.0\0"
        struct.pack_into("<H", data, 0x60, scenario_type)
        struct.pack_into("<4s", data, 0x7FC, b"toof")
    return bytes(data)


@pytest.fixture(scope="module")
def register(tmp_path_factory):
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    package = (ROOT / "port/linux/game/mcc_package.c").read_text(encoding="latin-1")
    code = REGISTER + "\n".join([
        "#define MCC_MAP_FILENAME_MAXIMUM (63 - (sizeof(MCC_MAPS_LEVEL_PREFIX) - 1))",
        "#define MCC_MAP_DESCRIPTION_MAXIMUM 127",
        re.search(r"struct mcc_menu_map\n\{.*?\n\};", SOURCE, re.S).group(0)
            .replace("short mission_order", "short mission_order"),
        re.search(r"static struct\n\{\n\tboolean scanned;.*?\} mcc_menu_catalog;", SOURCE, re.S).group(0),
        function(package, "mcc_package_name"),
        function(SOURCE, "mcc_map_description_read"),
        function(SOURCE, "mcc_map_register")]) + REGISTER_DRIVER
    code = code.replace("char next_mission[64];", "char next_mission[64];")
    work = tmp_path_factory.mktemp("mcc-register")
    (work / "register.c").write_text(code)
    exe = work / "register"
    build = subprocess.run([compiler, "-std=gnu99", "-w", "-DMCC_MANIFEST_MAXIMUM_MISSIONS=64",
                            "-I" + str(ROOT / "port/linux/game"), str(work / "register.c"),
                            str(ROOT / "port/linux/game/mcc_cache_format.c"), "-o", str(exe)],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stderr

    def run(folder, *names):
        result = subprocess.run([str(exe), ".", *names], cwd=folder, capture_output=True, text=True, check=True)
        lines = [line.split("=", 1) for line in result.stdout.splitlines()]
        return {key: value.split("|") for key, value in lines if key != "entry"}, \
            [value for key, value in lines if key == "entry"]
    return run


def test_a_real_map_is_listed_and_each_refusal_says_why(register, tmp_path):
    """#1137/#1140: a valid MCC cache is registered (multiplayer and campaign); each kind of file that
    can't be gets its own plain reason, and none of them stops the next map from listing"""
    (tmp_path / "arena.map").write_bytes(mcc_header())
    (tmp_path / "mission.map").write_bytes(mcc_header(scenario_type=0))
    (tmp_path / "pc.map").write_bytes(mcc_header(version=609))
    (tmp_path / "cut.map").write_bytes(mcc_header(length=0x10000))
    (tmp_path / "short.map").write_bytes(b"head" + bytes(100))
    (tmp_path / "tags.map").write_bytes(mcc_header(tag_offset=0x10))
    (tmp_path / "later.map").write_bytes(mcc_header())
    results, entries = register(tmp_path, "arena", "mission", "arena", "pc", "cut", "short", "tags", "gone",
                                "n" * 60, "trailing.", "later")
    assert results["arena"][0] == "listed" and results["mission"][0] == "listed"
    assert [results[name][0] for name in ("pc", "cut", "short", "tags", "gone", "n" * 60, "trailing.")] == [
        "Not an MCC map", "Map incomplete", "Map damaged", "Map damaged", "Map unreadable", "Name too long",
        "Name not usable"]
    # (the detail is the log's: never empty for a refusal)
    assert all(results[name][1] for name in ("pc", "cut", "short", "tags", "gone"))
    assert results["later"][0] == "listed"
    assert entries == ["mcc_maps\\arena|0", "mcc_maps\\mission|1", "mcc_maps\\later|0"]


def test_a_full_catalog_says_so(register, tmp_path):
    for n in range(7):
        (tmp_path / f"m{n}.map").write_bytes(mcc_header())
    results, entries = register(tmp_path, *[f"m{n}" for n in range(7)])
    assert len(entries) == 6 and results["m6"][0] == "Too many maps"


@pytest.mark.skipif(sys.platform == "win32", reason="a backslash in a file's name")
def test_a_workshop_items_map_is_listed(register, tmp_path):
    """an item's map, mcc_maps\\<item id>\\<name> (Digsite's are numbers)"""
    (tmp_path / "3477809506\\1.map").write_bytes(mcc_header())
    results, entries = register(tmp_path, "3477809506\\1")
    assert results["3477809506\\1"][0] == "listed"
    assert entries == ["mcc_maps\\3477809506\\1|0"]
