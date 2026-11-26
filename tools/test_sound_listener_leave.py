"""A split screen player who leaves a game under way (#1788's ADD PLAYERS
makes it common) takes their sound listener with them. A sound they were
the nearest listener of kept that listener's index, and the sound code's
next use of it asserted the listener valid (source/sound/sound_manager.c:
update_channels, source_distance_squared), a halt in debug builds once
another sound looked for a channel (another machine joining, say).
sounds_leave_invalid_listeners moves such sounds to a listener still valid
when the listeners change, and moves nothing when none is valid.

The function is taken from the game's source and compiled with stand-ins."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOUND = (ROOT / "source" / "sound" / "sound_manager.c").read_text(encoding="utf-8")


def function(source, name):
    match = re.search(r"^(?:static )?[a-z][^;\n]*\b" + name + r"\((?:[^;{]*?)\)\s*\n\{", source, re.M | re.S)
    assert match, name
    return source[match.start():source.index("\n}\n", match.end()) + 3]


HARNESS = r"""
#include <stdio.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define NONE -1
#define MAXIMUM_NUMBER_OF_LOCAL_PLAYERS 4
struct sound_listener { boolean valid; };
struct sound_datum { short listener_index; };
static struct sound_listener listeners[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
static struct sound_datum sounds[3];
static int sound_data;
static struct sound_listener *listener_get(short index) { return &listeners[index]; }
static struct sound_datum *sound_get(long index) { return &sounds[index]; }
static long data_next_index(int data, long index) { (void)data; return index + 1 < 3 ? index + 1 : NONE; }
@FUNCTION@
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
	/* two players; sounds heard by player 1 and player 2, one unspatialized */
	listeners[0].valid = listeners[1].valid = TRUE;
	sounds[0].listener_index = 0;
	sounds[1].listener_index = 1;
	sounds[2].listener_index = 1;
	sounds_leave_invalid_listeners();
	CHECK(sounds[1].listener_index == 1 && sounds[2].listener_index == 1);
	/* player 2 leaves: their sounds go to player 1's listener */
	listeners[1].valid = FALSE;
	sounds_leave_invalid_listeners();
	CHECK(sounds[0].listener_index == 0 && sounds[1].listener_index == 0 && sounds[2].listener_index == 0);
	/* player 1 left too (the game over): nothing to move them to, nothing moved */
	sounds[1].listener_index = 1;
	listeners[0].valid = FALSE;
	sounds_leave_invalid_listeners();
	CHECK(sounds[1].listener_index == 1);
	/* player 2's controller plays on alone (player 1 left first) */
	listeners[1].valid = TRUE;
	sounds[0].listener_index = 0;
	sounds_leave_invalid_listeners();
	CHECK(sounds[0].listener_index == 1);
	printf("ok\n");
	return 0;
}
"""


def test_sounds_leave_a_departed_players_listener(tmp_path):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    source = tmp_path / "listener.c"
    source.write_text(HARNESS.replace("@FUNCTION@", function(SOUND, "sounds_leave_invalid_listeners")))
    binary = tmp_path / "listener"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Wno-unused-function", str(source), "-o", str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0 and "ok" in result.stdout, result.stdout + result.stderr


def test_called_as_the_listeners_are_refreshed():
    refresh = function(SOUND, "refresh_listener")
    assert refresh.index("listener->valid = FALSE;") < refresh.index("sounds_leave_invalid_listeners();") < refresh.index(
        "set_listener_properties")
