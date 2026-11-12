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
	printf("focus: %d %d\n", mcc_maps_take_first_focus(), mcc_maps_take_first_focus());
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

