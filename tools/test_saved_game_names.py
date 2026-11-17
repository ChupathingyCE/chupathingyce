"""Tests that a player profile or a game variant read from a file (source/
saved games/player_profile.c, playlist_profile.c) has its name ended within
it: a file whose checksum is right may come from anyone (the checksum is
not a secret), and a name with all twelve characters set ran on into the
fields after it wherever it was read as text. Each loader's own helper is
built here with the build machine's C compiler, and each place a file's
bytes are taken is checked to call it."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SAVED = ROOT / "source" / "saved games"
PLAYER = (SAVED / "player_profile.c").read_text(encoding="utf-8")
PLAYLIST = (SAVED / "playlist_profile.c").read_text(encoding="utf-8")

TEST = r"""
#include <stdio.h>
#include <string.h>
#include <wchar.h>
enum { MAXIMUM_PLAYER_PROFILE_NAME_LENGTH = 12, MAXIMUM_GAME_VARIANT_NAME_LENGTH = 12 };
struct player_profile { wchar_t player_name[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH]; short primary_color_index; };
struct game_variant { wchar_t human_readable_game_description[12]; long game_engine_index; };

%s

%s

int main(void)
{
	struct player_profile profile;
	struct game_variant variant;
	int failures = 0;

	memset(&profile, 0x41, sizeof(profile));
	memset(&variant, 0x41, sizeof(variant));
	player_profile_loaded(&profile);
	playlist_profile_loaded(&variant);
	if (profile.player_name[11] != 0 || profile.player_name[10] == 0)
		failures++, printf("FAIL: the profile's name is not ended in its last place\n");
	if (variant.human_readable_game_description[11] != 0 || variant.human_readable_game_description[10] == 0)
		failures++, printf("FAIL: the variant's name is not ended in its last place\n");
	return failures != 0;
}
"""


def helper(text, name):
    match = re.search(r"static void " + name + r"\(\n[^{]*\{.*?\n\}\n", text, re.S)
    assert match, f"{name} is missing"
    return match.group(0)


def test_loaded_names_end(tmp_path):
    cc = next((path for path in map(shutil.which, ("clang", "cc", "gcc")) if path), None)
    if not cc:
        pytest.skip("no C compiler")
    source = tmp_path / "saved_game_names_test.c"
    source.write_text(TEST % (helper(PLAYER, "player_profile_loaded"), helper(PLAYLIST, "playlist_profile_loaded")),
                      encoding="utf-8")
    binary = tmp_path / "saved_game_names_test"
    build = subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_every_file_read_ends_the_name():
    # (each copy of a file's block into a profile or a variant is followed
    # by its loader's helper: player_profile_get_from_path and
    # player_profile_read, playlist_profile_get_from_path and
    # playlist_profile_read)
    profile_copies = re.findall(r"csmemcpy\(profile, block, sizeof\(struct player_profile\)\);\n\s*(\S+)\(", PLAYER)
    assert profile_copies == ["player_profile_loaded", "player_profile_loaded"]
    variant_copies = re.findall(r"csmemcpy\(\s*variant,\s*block,\s*sizeof\(struct game_variant\)\);\n\s*(\S+)\(",
                                PLAYLIST)
    assert variant_copies == ["playlist_profile_loaded", "playlist_profile_loaded"]
