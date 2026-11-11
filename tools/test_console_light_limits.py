"""Checks the original Xbox's match light rule (HALO_CONSOLE_MATCH_LIGHT_LIMITS:
port/linux/game/match_light_limits.h, source/game/game_engine.c,
source/objects/object_lights.c).

The rule decides only what is drawn. The flashlight's simulation (its toggle,
battery and effect in unit_update, read through
game_engine_allow_integrated_lights) has to run alike on every machine of a
game, a PC host and OpenCE players with an Xbox, so the gate must never reach
it. The decision itself is compiled alone and checked against the retail
game's table."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "port" / "linux" / "game" / "match_light_limits.h"
GAME_ENGINE = ROOT / "source" / "game" / "game_engine.c"
GATE = "HALO_CONSOLE_MATCH_LIGHT_LIMITS"

HARNESS = r"""
#include <stdio.h>
#include "match_light_limits.h"
int main(void)
{
	long players, vehicles;
	for (players = 0; players <= 20; players++)
		for (vehicles = 0; vehicles <= 6; vehicles++)
		{
			struct match_light_limits limits = match_light_limits_for(players, vehicles);
			printf("%ld %ld %d %d\n", players, vehicles, limits.no_dynamic_lights, limits.no_flashlights);
		}
	return 0;
}
"""


def gated_blocks(text):
    """The text of each #ifdef GATE ... #endif (or #else) block."""
    blocks, depth, inside, current = [], 0, False, []
    for line in text.splitlines():
        stripped = line.strip()
        if not inside:
            if stripped.startswith("#ifdef " + GATE) or stripped.startswith("#if defined(" + GATE):
                inside, depth, current = True, 1, []
            continue
        if stripped.startswith("#if"):
            depth += 1
        elif stripped.startswith("#endif") or (stripped.startswith("#else") and depth == 1):
            depth -= 1 if stripped.startswith("#endif") else 0
            if depth == 0 or stripped.startswith("#else"):
                blocks.append("\n".join(current))
                inside = False
                continue
        current.append(line)
    return blocks


def test_retail_table():
    cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    import tempfile
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "harness.c"
        binary = Path(directory) / "harness"
        source.write_text(HARNESS)
        subprocess.run([cc, "-Wall", "-Werror", "-Wno-unused-function", f"-I{HEADER.parent}", str(source), "-o",
                        str(binary)], check=True)
        output = subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout
    for line in output.splitlines():
        players, vehicles, no_dynamic, no_flashlights = (int(field) for field in line.split())
        # the retail game's (game_engine_build_lighting, USA Rev 2)
        retail_dynamic = (players > 8 and vehicles >= 2) or players >= 13
        retail_flashlights = players > 4 or vehicles >= 4 or retail_dynamic
        assert bool(no_dynamic) == retail_dynamic, line
        assert bool(no_flashlights) == retail_flashlights, line


def test_gate_never_sets_the_flashlight_simulation_bit():
    text = GAME_ENGINE.read_text()
    blocks = gated_blocks(text)
    assert blocks, "the gate is gone from game_engine.c"
    for block in blocks:
        assert "_game_engine_disable_integrated_lights_bit" not in block
    # nothing in the file sets that bit at all (the retail rule set it)
    assert not re.search(r"SET_FLAG\([^;]*_game_engine_disable_integrated_lights_bit", text)


def test_simulation_reads_are_ungated():
    """game_engine_allow_integrated_lights, which unit_update reads, has no
    gate in its body, and the gate is in no simulation unit."""
    text = GAME_ENGINE.read_text()
    body = text[text.index("boolean game_engine_allow_integrated_lights("):]
    body = body[:body.index("\n}\n")]
    assert GATE not in body
    assert "flashlight_beams_hidden" not in body
    for directory in ("units", "items", "physics", "ai", "networking"):
        for path in (ROOT / "source" / directory).rglob("*.c"):
            assert GATE not in path.read_text(errors="replace"), path
    allowed = {GAME_ENGINE, ROOT / "source" / "objects" / "object_lights.c"}
    for path in (ROOT / "source").rglob("*.c"):
        if path not in allowed:
            assert GATE not in path.read_text(errors="replace"), path
