"""MCC maps' score hint ('Hold "%s" for score', port/linux/game/mcc_hud.c)
names one key: the controller's BACK, which the game's HUD swaps for the
keyboard's scoreboard key while the keyboard was used last
(input_abstraction_port_back_word, source/input/input_abstraction.c). It
showed "Tab / Tab" with the keyboard and "Tab / BACK" with a controller."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
MCC_HUD = ROOT / "port" / "linux" / "game" / "mcc_hud.c"
INPUT = ROOT / "source" / "input" / "input_abstraction.c"

HARNESS = r"""
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define NUMBEROF(a) ((long)(sizeof(a) / sizeof((a)[0])))
enum { _port_icon_back_button = 1 };
static wchar_t const *keyboard_key; /* the shown key, "[Tab]", or none: a controller */
static boolean input_abstraction_port_icon_key(short player, short icon, boolean wide, wchar_t *text, long count)
{
	(void)player; (void)icon; (void)wide;
	if (!keyboard_key)
		return FALSE;
	wcsncpy(text, keyboard_key, (size_t)count);
	return TRUE;
}
%(mcc)s
%(back)s
static void hint(wchar_t const *key)
{
	static char const text[] = "Hold \"%%s\" for score";
	uint16_t source[sizeof(text)], out[64];
	wchar_t message[64];
	int index, used;
	for (index = 0; index < (int)sizeof(text); index++)
		source[index] = (unsigned char)text[index];
	used = mcc_hud_score_text(source, sizeof(text), NULL, 0);
	if (used <= 0 || used > 64 || mcc_hud_score_text(source, sizeof(text), out, (uint32_t)used) != used)
	{
		printf("failed\n");
		return;
	}
	for (index = 0; index < used; index++)
		message[index] = out[index];
	keyboard_key = key;
	input_abstraction_port_back_word(message, NUMBEROF(message));
	printf("%%ls\n", message);
}
int main(void)
{
	hint(NULL);
	hint(L"[Tab]");
	hint(L"[Caps Lock]");
	return 0;
}
"""


def function(path, name):
    """a function's definition in a C file, its comment left out"""
    text = path.read_text()
    match = re.search(r"^(?:static )?(?:int|void) " + name + r"\(.*?^\}\n", text, re.S | re.M)
    assert match, f"{name} is not in {path.name}"
    return match.group(0)


def test_score_hint_names_one_key(tmp_path):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    source = tmp_path / "hint.c"
    source.write_text(HARNESS % {"mcc": function(MCC_HUD, "mcc_hud_score_text"),
                                 "back": function(INPUT, "input_abstraction_port_back_word")})
    binary = tmp_path / "hint"
    subprocess.run([cc, "-Wall", "-Werror", str(source), "-o", str(binary)], check=True)
    lines = subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout.splitlines()
    assert lines == ['Hold "BACK" for score', 'Hold "Tab" for score', 'Hold "Caps Lock" for score']
