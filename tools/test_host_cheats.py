"""The host's console in a public internet game (network.host_cheats): the
words of hs.c's hs_expression_changes_no_game, compiled from the source with
the build machine's C compiler, and what the C test cannot reach: where the
console refuses, what puts cheats back each tick, when the host counts as
public, the setting and the Delta List bit."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
HS_C = ROOT / "source/hs/hs.c"
CHEATS_C = ROOT / "source/game/cheats.c"
GLOBALS_C = ROOT / "source/networking/network_game_globals.c"
P2P_C = ROOT / "port/linux/src/p2p.c"
BROWSER_C = ROOT / "port/linux/src/browser.c"
CONFIG_C = ROOT / "port/linux/src/port_config.c"
DELTA_H = ROOT / "port/linux/include/delta.h"


def _function(text, name):
    start = text.index(f"static boolean {name}(")
    end = text.index("\n}\n", start) + 3
    return text[start:end]


PRELUDE = """
#include <ctype.h>
#include <stdio.h>
#include <string.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define NUMBEROF(a) (sizeof(a) / sizeof((a)[0]))
#define csstrcmp strcmp
"""

MAIN = """
int main(void)
{
	static char const *const kept[] = {
		"display_framerate 1", "(show_hud false)", "print \\"hello\\"", "cls", "sound_set_gain 0.5",
	};
	static char const *const refused[] = {
		"cheat_all_vehicles", "(cheat_all_weapons)", "set cheat_deathless_player true", "set game_speed 4",
		"object_create_anew warthog", "ai_place x", "game_speed 2", "(print \\"x\\") (cheat_all_powerups)",
	};
	unsigned index;
	int failures = 0;

	for (index = 0; index < NUMBEROF(kept); index++)
	{
		if (!hs_expression_changes_no_game(kept[index]))
		{
			printf("refused, should be kept: %s\\n", kept[index]);
			failures++;
		}
	}
	for (index = 0; index < NUMBEROF(refused); index++)
	{
		if (hs_expression_changes_no_game(refused[index]))
		{
			printf("kept, should be refused: %s\\n", refused[index]);
			failures++;
		}
	}
	if (!failures)
		printf("host_cheats_test: ok\\n");
	return failures != 0;
}
"""


def test_words(tmp_path):
    """what the console of a game's client, or of a public game's host, still
    runs (display alone), and what it refuses (cheats, objects, game speed)"""
    cc = next((shutil.which(name) for name in ("clang", "cc", "gcc") if shutil.which(name)), None)
    if not cc:
        pytest.skip("no C compiler")
    source = tmp_path / "host_cheats_test.c"
    source.write_text(PRELUDE + _function(HS_C.read_text(encoding="utf-8"), "hs_expression_changes_no_game") + MAIN,
                      encoding="utf-8")
    binary = tmp_path / "host_cheats_test"
    build = subprocess.run([cc, "-std=c11", "-Wall", "-Werror", str(source), "-o", str(binary)],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "host_cheats_test: ok" in result.stdout


def test_console_refuses_after_kick_and_ban():
    """the host's kick and ban stay; the refusal comes before any script runs"""
    text = HS_C.read_text(encoding="utf-8")
    body = text[text.index("static boolean hs_compile_and_evaluate_command(\n\tchar const *expression)\n{"):]
    kick = body.index("network_game_server_kick_player(name)")
    refusal = body.index("network_game_host_cheats_refused() && !hs_expression_changes_no_game(expression)")
    evaluate = body.index("csstrncpy(buffer, expression, sizeof(buffer));")
    assert kick < refusal < evaluate


def test_cheats_put_back_each_tick():
    text = CHEATS_C.read_text(encoding="utf-8")
    enforce = text[text.index("void cheats_network_client_enforce("):]
    enforce = enforce[:enforce.index("\n}\n")]
    assert "network_game_host_cheats_refused()" in enforce
    assert "game_time_reset_speed()" in enforce


def test_public_game_only():
    """a public internet game (no password) with another machine, the setting
    off; nothing on a build without internet hosting"""
    text = GLOBALS_C.read_text(encoding="utf-8")
    body = text[text.index("boolean network_game_host_cheats_refused(\n\tvoid)\n{"):]
    body = body[:body.index("\n}\n")]
    assert "#ifdef HALO_GAME_BROWSER" in body and "return FALSE;\n#endif" in body
    assert "machine_count < 2" in body
    assert '!config_boolean("network.host_cheats")' in body
    assert "p2p_hosting_public_game()" in body
    p2p = P2P_C.read_text(encoding="utf-8")
    public = p2p[p2p.index("int p2p_hosting_public_game(void)"):]
    assert "p2p.hosting && p2p_lobby_hosting_open()" in public[:public.index("\n}\n")]


def test_setting_and_list_bit():
    config = CONFIG_C.read_text(encoding="utf-8")
    assert re.search(r'\{ "network.host_cheats", _config_boolean, "false", "HALO_NET_HOST_CHEATS"', config)
    assert re.search(r"_delta_capability_host_cheats = 18,", DELTA_H.read_text(encoding="utf-8"))
    browser = BROWSER_C.read_text(encoding="utf-8")
    assert 'config_boolean("network.host_cheats") ? 1u << _delta_capability_host_cheats' in browser
