"""Tests of the profile a multiplayer profile list starts on (source/saved
games/player_profile_list.h) and of the names multiplayer takes
(player_name_valid in source/game/players.c), with the build machine's C
compiler.

A fresh install's profile, made in Settings, was not the one player 1 last
used, so System Link's profile list started on the built-in Default, whose
empty name multiplayer refuses: "Sorry, this profile's name can't be used in
multiplayer. Please rename the profile." The names themselves (New001, a
renamed one) were always fine: the name check is built here alone, with
wchar_t 16 bits wide as the game builds it (-fshort-wchar) and as wide as the
compiler's own."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
PLAYERS = (ROOT / "source" / "game" / "players.c").read_text(encoding="utf-8")
HANDLERS = (ROOT / "source" / "interface" / "ui_widget_event_handler_functions.c").read_text(encoding="utf-8")

NAME_TEST = r"""
#include <stddef.h>
#include <stdio.h>
typedef int boolean;
enum { FALSE, TRUE };
#define NUMBEROF(a) ((long)(sizeof(a) / sizeof((a)[0])))

%s

static int failures;

static void check(wchar_t const *text, boolean expected)
{
	wchar_t name[12] = { 0 };
	long index;

	for (index = 0; index < 11 && text[index]; index++)
		name[index] = text[index];
	if (player_name_valid(name, NUMBEROF(name)) != expected)
	{
		printf("FAIL: name %%d long, expected %%d\n", (int)index, expected);
		failures++;
	}
}

int main(void)
{
	check(L"New001", TRUE);
	check(L"Tester", TRUE);
	check(L"Chief", TRUE);
	check(L"Eleven char", TRUE);
	check(L"", FALSE);
	check(L"   ", FALSE);
	check(L" Lead", FALSE);
	check(L"Trail ", FALSE);
	check(L"a|b", FALSE);
	return failures != 0;
}
"""


def compiler():
    """Finds a C compiler on the build machine.

    Returns the path of clang, cc or gcc (the first found), or None.
    """
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def build_and_run(cc, source, binary, flags=()):
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", *flags, str(source), "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_profile_list_start(tmp_path):
    """Builds the profile list's tests, warnings as errors, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    build_and_run(cc, ROOT / "port" / "linux" / "tests" / "player_profile_list_test.c",
                  tmp_path / "player_profile_list_test")


@pytest.mark.parametrize("flags", [["-fshort-wchar"], []], ids=["wchar16", "wchar-native"])
def test_player_names(tmp_path, flags):
    """player_name_valid takes ordinary names and refuses empty ones."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    start = PLAYERS.index("char player_name_character_ascii(")
    end = PLAYERS.index("/* ---------- private code", start)
    source = tmp_path / "player_names_test.c"
    source.write_text(NAME_TEST % PLAYERS[start:end], encoding="utf-8")
    build_and_run(cc, source, tmp_path / "player_names_test", flags)


def test_handlers_use_the_start():
    # (player_profiles_list_initialize starts the list there, and the
    # refusals tell a built-in profile from one to rename)
    assert '#include "saved games/player_profile_list.h"' in HANDLERS
    assert "player_profile_list_start(widget->parameters.list.list_items," in HANDLERS
    assert len(re.findall(r"player_profile_list_built_in\(", HANDLERS)) == 2
