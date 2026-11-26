"""The lobby's crown is the host's own player's (port/linux/game/
lobby_screen.c's host_player_controller): a split-screen guest on the host's
machine has none (#1640). The function is taken from the game's source and
compiled with stand-ins."""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from harness import function, read  # noqa: E402

SOURCE = read("port/linux/game/lobby_screen.c")

HARNESS = r"""
#include <stdio.h>
#define NONE (-1)
#define NUMBEROF(a) (sizeof(a) / sizeof((a)[0]))
typedef int boolean;
struct network_player { short machine_index; short controller_index; boolean valid; };
struct network_game { struct network_player players[16]; };
static boolean network_player_is_valid(struct network_player const *player) { return player->valid; }
@FUNCTIONS@
static void add(struct network_game *game, int index, short machine, short controller)
{
	game->players[index].valid = 1;
	game->players[index].machine_index = machine;
	game->players[index].controller_index = controller;
}
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
	struct network_game game = { 0 };

	/* the host (machine 0) with a guest on its second controller, and a
	client with two players: the crown is controller 0's on machine 0 */
	add(&game, 0, 0, 0);
	add(&game, 1, 0, 1);
	add(&game, 2, 1, 0);
	add(&game, 3, 1, 1);
	CHECK(host_player_controller(&game, 0) == 0);
	/* (the guest listed first changes nothing) */
	add(&game, 0, 0, 1);
	add(&game, 1, 0, 0);
	CHECK(host_player_controller(&game, 0) == 0);
	/* the host plays on its third controller: that one's */
	add(&game, 0, 0, 3);
	add(&game, 1, 0, 2);
	CHECK(host_player_controller(&game, 0) == 2);
	/* a host with no player (a dedicated server): none */
	CHECK(host_player_controller(&game, 5) == NONE);
	printf("ok\n");
	return 0;
}
"""


def test_lobby_host_crown(tmp_path):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    source = tmp_path / "crown.c"
    source.write_text(HARNESS.replace("@FUNCTIONS@", function(SOURCE, "host_player_controller")))
    binary = tmp_path / "crown"
    subprocess.run([cc, "-std=gnu99", "-Wall", str(source), "-o", str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0 and "ok" in result.stdout, result.stdout + result.stderr
