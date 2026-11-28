"""Runs the C tests of a verified developer's blue flame's rules
(port/linux/src/developer_flames_rules.h: who has one, when it is drawn, its
budget, the blue copy of the map's flame sprite), with the build machine's C
compiler, and checks how the game wires them: the flame is looks only (drawn
from the render, never in the simulation, saved games or the network), it
is read only from cards this copy checked, and display.developer_flames is
a setting a viewer can turn off."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / "port" / "linux" / "tests" / "developer_flames_test.c"
FLAMES = ROOT / "port" / "linux" / "game" / "developer_flames.c"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_rules(tmp_path):
    """Builds the rules with their tests, warnings as errors, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "developer_flames_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-iquote", str(ROOT / "port" / "linux" / "include"),
         str(TEST), "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_rules_header_builds_as_gnu89(tmp_path):
    """The game's code is built as gnu89: the header must build there,
    unused, with no warnings."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    source = tmp_path / "unit.c"
    source.write_text('#include "developer_flames_rules.h"\nint unit(void) { return DEVELOPER_FLAMES_MOST; }\n')
    build = subprocess.run(
        [cc, "-std=gnu89", "-Wall", "-Wextra", "-Werror", "-c", "-I", str(ROOT / "port" / "linux" / "src"),
         str(source), "-o", str(tmp_path / "unit.o")],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr


def test_looks_only():
    """The flames are the render's: their own sprites (not the game's
    particle pool, which is game state), their own random numbers (never the
    game's seed), drawn after the map's particles; nothing of them is sent."""
    flames = FLAMES.read_text()
    for never in ("particle_new(", "global_random", "random_seed", "local_random(", "delta_peer_send",
                  "network_", "game_state"):
        assert never not in flames, never
    render = (ROOT / "source" / "render" / "render.c").read_text()
    assert re.search(r"render_particles\(\);\s*(?:/\*.*?\*/\s*)?#ifdef HALO_DEVELOPER_FLAMES\s*developer_flames_render\(\);",
                     render, re.S)
    determinism = (ROOT / "port" / "linux" / "game" / "determinism_check.c").read_text()
    assert "flame" not in determinism


def test_only_checked_cards():
    """Who has a flame comes from profile_names_developer alone, which reads
    the card this copy checked (delta_peer_game_player_identity) and the
    rules' developer_flames_card_shows."""
    names = (ROOT / "port" / "linux" / "game" / "profile_names.c").read_text()
    body = names[names.index("boolean profile_names_developer("):]
    body = body[:body.index("\n}\n")]
    for needed in ("display.developer_flames", "delta_peer_game_player_identity", "verified_name",
                   "other_network", "identity.bound", "developer_flames_card_shows"):
        assert needed in body, needed
    assert "profile_names_developer(" in FLAMES.read_text()


def test_setting():
    """display.developer_flames is on by default, in the docs, and a row of
    the Guide's display settings."""
    config = (ROOT / "port" / "linux" / "src" / "port_config.c").read_text()
    assert re.search(r'\{ "display\.developer_flames", _config_boolean, "true", "HALO_DEVELOPER_FLAMES"', config)
    assert "`display.developer_flames`" in (ROOT / "docs" / "settings.md").read_text()
    rows = (ROOT / "port" / "linux" / "src" / "settings_rows.c").read_text()
    assert re.search(r'_in_guide, "DEVELOPER FLAMES", "display\.developer_flames", _kind_toggle', rows)
    assert '"display.developer_flames"' in rows[rows.index("display_lines[]"):]
