"""The Delta Guide Workshop's PLAY on a campaign item
(port/linux/game/guide_workshop.c's play): its levels are only found with
game.mcc_campaigns on, so while it is off PLAY asks once whether to turn
it on, and never changes it unasked (#1134). Yes turns it on, looks for
the maps again and opens the level select; no leaves it off and plays what
else the item has; the answer is kept, so it isn't asked again, and the
page then says where the setting is. The real functions, compiled against
stand-ins for the settings, the question box and the menus' PLAY."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest
from harness import function

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "port/linux/game/guide_workshop.c").read_text(encoding="latin-1")
SETTINGS_ROWS = (ROOT / "port/linux/src/settings_rows.c").read_text(encoding="latin-1")
CONFIG = (ROOT / "port/linux/src/port_config.c").read_text(encoding="latin-1")

PRELUDE = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "workshop_browser.h"
typedef unsigned char boolean;
#define TRUE 1
#define FALSE 0

static int campaigns, asked, has_campaign, rescans, plays, closes, asks, found = 1;
static void (*question)(boolean yes);

int config_boolean(char const *name)
{
	return !strcmp(name, "game.mcc_campaigns") ? campaigns : !strcmp(name, "game.mcc_campaigns_asked") ? asked : 0;
}
int config_write_boolean(char const *name, int value)
{
	printf("write: %s=%d\n", name, value);
	if (!strcmp(name, "game.mcc_campaigns"))
		campaigns = value;
	if (!strcmp(name, "game.mcc_campaigns_asked"))
		asked = value;
	return 1;
}
int workshop_browser_item_has_campaign(const char *id) { return has_campaign; }
void mcc_maps_rescan(void) { rescans++; }
void guide_close(void) { closes++; }
void platform_log(char const *format, ...) {}
void invite_prompt_ask(char const *title, char const *question_text, char const *detail,
	void (*answer_function)(boolean yes))
{
	asks++;
	question = answer_function;
	printf("ask: %s\n", question_text);
}
boolean pc_menu_workshop_play(char const *item, char const *title)
{
	plays++;
	printf("play: %s campaigns=%d\n", item, campaigns);
	return found;
}
"""

DRIVER = r"""
static struct workshop_browser_item item;

static void state(char const *label)
{
	printf("%s: asks=%d plays=%d closes=%d rescans=%d campaigns=%d asked=%d\n", label, asks, plays, closes, rescans,
		campaigns, asked);
}

static void reset(void)
{
	campaigns = asked = rescans = plays = closes = asks = 0;
	found = 1;
	question = NULL;
	workshop_play_item.looked[0] = 0;
}

int main(void)
{
	snprintf(item.id, sizeof(item.id), "%s", "9100000001");
	snprintf(item.title, sizeof(item.title), "%s", "Test Campaign");
	item.state = WORKSHOP_ITEM_INSTALLED;

	/* a campaign item, MCC campaigns off, never asked: the question, nothing played yet */
	has_campaign = 1;
	reset();
	play(&item);
	state("asked");
	question(TRUE);
	state("yes");

	reset();
	play(&item);
	question(FALSE);
	state("no");
	/* (asked once: PLAY again doesn't ask) */
	play(&item);
	state("no_again");
	printf("note: %d\n", campaign_hidden(&item));

	/* already on: played at once */
	reset();
	campaigns = 1;
	play(&item);
	state("on");
	printf("note_on: %d\n", campaign_hidden(&item));

	/* a multiplayer item: played at once, never asked */
	has_campaign = 0;
	reset();
	play(&item);
	state("multiplayer");

	/* not installed: nothing */
	has_campaign = 1;
	reset();
	item.state = WORKSHOP_ITEM_NOT_INSTALLED;
	play(&item);
	state("not_installed");
	return 0;
}
"""


def play_state(source):
    match = re.search(r"static struct\s*\{[^}]*\}\s*workshop_play_item;", source)
    assert match, "guide_workshop.c's workshop_play_item is not found"
    return match.group(0)


@pytest.fixture(scope="module")
def output(tmp_path_factory):
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    code = PRELUDE + play_state(SOURCE) + "\n" + "\n".join(function(SOURCE, name) for name in (
        "campaign_hidden", "campaigns_answer", "play")) + DRIVER
    work = tmp_path_factory.mktemp("workshop-play-campaign")
    (work / "campaign.c").write_text(code)
    exe = work / "campaign"
    build = subprocess.run([compiler, "-std=gnu99", "-w", "-I", str(ROOT / "port/linux/src"), str(work / "campaign.c"),
                            "-o", str(exe)], capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
    lines = {}
    for line in run.stdout.splitlines():
        key, value = line.split(":", 1)
        lines.setdefault(key, []).append(value.strip())
    return lines


def fields(text):
    return dict(pair.split("=") for pair in text.split())


def test_a_campaign_item_asks_first_and_changes_nothing(output):
    assert output["ask"][0] == "Turn on MCC campaigns?"
    assert fields(output["asked"][0]) == {"asks": "1", "plays": "0", "closes": "1", "rescans": "0",
                                         "campaigns": "0", "asked": "0"}


def test_yes_turns_it_on_then_opens_the_level_select(output):
    assert fields(output["yes"][0]) == {"asks": "1", "plays": "1", "closes": "1", "rescans": "1",
                                       "campaigns": "1", "asked": "1"}
    # (the maps looked for again with it on, before the menus' PLAY)
    assert output["play"][0] == "9100000001 campaigns=1"


def test_no_keeps_it_off_and_is_remembered(output):
    assert fields(output["no"][0]) == {"asks": "1", "plays": "1", "closes": "1", "rescans": "0",
                                      "campaigns": "0", "asked": "1"}
    assert fields(output["no_again"][0])["asks"] == "1"
    assert fields(output["no_again"][0])["plays"] == "2"
    # (the page's note, where the setting is)
    assert output["note"] == ["1"]


def test_with_campaigns_on_or_a_multiplayer_item_it_plays_at_once(output):
    for label in ("on", "multiplayer"):
        assert fields(output[label][0]) == {"asks": "0", "plays": "1", "closes": "1", "rescans": "0",
                                           "campaigns": "1" if label == "on" else "0", "asked": "0"}
    assert output["note_on"] == ["0"]


def test_an_item_not_installed_does_nothing(output):
    assert fields(output["not_installed"][0])["plays"] == "0"
    assert fields(output["not_installed"][0])["asks"] == "0"


def test_the_setting_has_a_row_and_the_answer_a_setting():
    assert re.search(r'"MCC CAMPAIGNS", "game.mcc_campaigns", _kind_toggle', SETTINGS_ROWS)
    assert '"game.mcc_campaigns_asked", _config_boolean, "false"' in CONFIG
