"""A console's players: the original Xbox hosts 16 in the game itself
(HALO_CONSOLE_HOST_PLAYERS, halo_console_features.h; network_server_manager.c
and network_game_manager.c), whatever Delta and its settings say, and joins
as many as its build's tables hold (delta_peer_local_key's clamp,
delta_peer_game.c). The desktop builds, the Xbox 360 and the Wii U host as
before."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "port" / "linux" / "include"
FEATURES_H = INCLUDE / "halo_console_features.h"
SERVER_C = ROOT / "source" / "networking" / "network_server_manager.c"
GAME_C = ROOT / "source" / "networking" / "network_game_manager.c"
DELTA_PEER_GAME_C = ROOT / "port" / "linux" / "src" / "delta_peer_game.c"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def _body(text, signature):
    start = text.index(signature)
    return text[start:text.index("\n}\n", start)]


def _defines(cc, *defines):
    run = subprocess.run([cc, "-E", "-dM", "-x", "c", *(f"-D{define}" for define in defines), str(FEATURES_H)],
                         capture_output=True, text=True, check=True)
    return run.stdout


@pytest.mark.parametrize("console, host_players", [
    ("HALO_XBOX_CONSOLE", "16"),
    ("HALO_XBOX360_CONSOLE", None),
    ("HALO_WIIU_CONSOLE", None),
    ("HALO_SWITCH_CONSOLE", None),
    (None, None),
])
def test_only_the_xbox_has_a_hosting_limit(console, host_players):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    defines = _defines(cc, *([console] if console else []))
    found = re.search(r"^#define HALO_CONSOLE_HOST_PLAYERS (\S+)$", defines, re.M)
    assert (found.group(1) if found else None) == host_players


def test_hosted_games_held_to_it():
    source = SERVER_C.read_text(encoding="utf-8")
    # (the hosted maximum: the console's limit, or the tables' size)
    assert re.search(r"#if defined\(HALO_CONSOLE_HOST_PLAYERS\) && HALO_CONSOLE_HOST_PLAYERS < "
                     r"HALO_PORT_MAXIMUM_NETWORK_PLAYERS\n\tMAXIMUM_HOSTED_PLAYER_COUNT = HALO_CONSOLE_HOST_PLAYERS,",
                     source)
    # (every place a host sets its game's most players: the setup, the
    # settings, co-op; so the advertisement and the list show it)
    sets = re.findall(r"server->game\.maximum_players = [^;]*;", source, re.S)
    assert len(sets) == 3
    assert all("MAXIMUM_HOSTED_PLAYER_COUNT" in line and "MAXIMUM_NETWORK_PLAYER_COUNT" not in line
               for line in sets)
    # (and a free slot is never past it, with or without Delta)
    free = _body(GAME_C.read_text(encoding="utf-8"), "boolean network_game_has_free_player_slot(")
    cap = free.index("game->player_count >= HALO_CONSOLE_HOST_PLAYERS")
    assert free.index("#ifdef HALO_CONSOLE_HOST_PLAYERS") < cap < free.index("delta_peer_host_player_limit()")


HARNESS = r"""
#include <stdio.h>
struct delta_platform_key { unsigned char host_players, join_players, join_players_opt_in; };
static void clamp(struct delta_platform_key *key)
{
%s
}
int main(void)
{
	struct delta_platform_key key = { 16, 32, 128 };
	clamp(&key);
	printf("%%d %%d %%d\n", key.host_players, key.join_players, key.join_players_opt_in);
	return 0;
}
"""


@pytest.mark.parametrize("players, expected", [
    (None, "16 32 128"),
    ("16", "16 16 128"),
    ("24", "24 24 128"),
    ("32", "32 32 128"),
])
def test_local_key_joins_what_the_build_holds(tmp_path, players, expected):
    """the Xbox row (hosts 16, joins 32) through delta_peer_local_key's
    console clamp: a stock build of 16 claims 16, a build of 32 claims 32
    (its host claim held to the row's 16 by every host)"""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    key = _body(DELTA_PEER_GAME_C.read_text(encoding="utf-8"), "void delta_peer_local_key(")
    start = key.index("#if defined(HALO_CONSOLE_PLAYERS) && HALO_CONSOLE_PLAYERS > 16")
    block = key[start:key.index("#endif", start) + len("#endif")]
    assert "#elif defined(HALO_CONSOLE_PLAYERS)" in block
    source = tmp_path / "clamp.c"
    source.write_text(HARNESS % block, encoding="utf-8")
    binary = tmp_path / "clamp"
    subprocess.run([cc, "-std=c99", "-Wall", "-Werror", *([f"-DHALO_CONSOLE_PLAYERS={players}"] if players else []),
                    str(source), "-o", str(binary)], check=True, capture_output=True, text=True)
    assert subprocess.run([str(binary)], capture_output=True, text=True, check=True).stdout.strip() == expected
