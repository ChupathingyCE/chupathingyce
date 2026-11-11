"""The setup screen at start (port/linux/src/launcher.c, launcher_at_start).

Under gamescope (the Steam Deck's Game Mode) it opens only when asked for:
Back or Shift held as the game starts, --setup (SETUP in the menus), or a
maps folder that isn't the Xbox maps. The mode question, show_at_start and
"ask each time" play the default there, as before 0.9.1, and the log says
which way it went. Under Steam the held button waits for Steam Input's
controller (held_wait) on the Deck or under gamescope, and the desktop's
start is not slowed.

Text checks of launcher.c, and held_wait compiled on its own."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
LAUNCHER = (ROOT / "port/linux/src/launcher.c").read_text()


def body(signature):
    start = LAUNCHER.index(signature)
    return LAUNCHER[start:LAUNCHER.index("\n}\n", start) + 3]


def test_opens_by_itself_only_off_gamescope():
    assert "return !updater_gamescope();" in body("static int launcher_opens_by_itself(void)")


def test_gamescope_gate_in_launcher_at_start():
    start = body("void launcher_at_start(void)")
    gate = start.index("if ((always || asked || asks) && !launcher_opens_by_itself())")
    cleared = start.index("always = asked = asks = 0;", gate)
    # (the reasons it opens by itself are read before the gate and used after it)
    assert start.index('int always = !strcmp(config_string("launcher.show_at_start"), "always");') < gate
    assert start.index("if (always)", cleared) > cleared
    assert start.index("else if (asked)", cleared) > cleared
    # (what is asked for is not gated: the maps, --setup, a held button)
    assert start.index("if (launcher.open_on_maps)") < gate
    assert start.index("else if (setup)") < gate
    assert start.index('config_boolean("launcher.hold_to_open") && held_at_start()') > cleared
    assert "launcher: under gamescope, not opened by itself (%s): playing %s" in start
    assert 'platform_log("launcher: opened at start under gamescope because %s", because);' in start
    # (nothing still turns the gamescope check off at the top, as 0.9.0d's
    # platform_run_is_quiet did: Back held and --setup open it there)
    assert "platform_run_is_quiet()" not in start


def test_every_way_it_opens_says_why():
    start = body("void launcher_at_start(void)")
    for reason in ("the maps folder isn't the Xbox maps", "of --setup", "Back or Shift was held",
                   "of the mode question", "launcher.mode_choice asks each time", "launcher.show_at_start is always"):
        assert f'because = "{reason}' in start, reason


def test_long_wait_only_on_the_deck_or_gamescope():
    """the 2 s wait for a controller is for gamescope or the Deck, not a
    desktop's Steam, where a player with no controller would wait each
    start"""
    steam = body("static int launcher_started_by_steam(void)")
    assert "updater_gamescope()" in steam and '"SteamDeck"' in steam
    assert "SteamAppId" not in steam and "SteamGameId" not in steam


HELD_WAIT_TEST = r"""
#include <stdio.h>
typedef unsigned long long Uint64;
%s
int main(void)
{
	/* steam, pads, shift_unknown */
	printf("%%llu %%llu %%llu %%llu %%llu %%llu\n", held_wait(1, 0, 0), held_wait(1, 0, 1), held_wait(0, 0, 0),
		held_wait(0, 0, 1), held_wait(0, 1, 0), held_wait(1, 1, 0));
	return 0;
}
"""


def test_held_wait():
    """under Steam with no pad yet: up to 2 s for Steam Input's; off Steam
    with no pad and a keyboard it can read: no wait, as before; a pad there
    already: its first report"""
    cc = shutil.which("clang") or shutil.which("cc")
    if not cc:
        pytest.skip("no C compiler")
    start = LAUNCHER.index("#define HELD_WAIT_FOR_A_PAD_UNDER_STEAM_MS")
    code = LAUNCHER[start:LAUNCHER.index("\n}\n", LAUNCHER.index("static Uint64 held_wait(", start)) + 3]
    assert re.search(r"#define HELD_WAIT_FOR_A_PAD_UNDER_STEAM_MS 2000\b", code)
    source = Path(__import__("tempfile").mkdtemp()) / "held_wait.c"
    source.write_text(HELD_WAIT_TEST % code)
    program = source.with_suffix("")
    subprocess.run([cc, "-std=c99", "-Wall", "-Werror", "-o", str(program), str(source)], check=True)
    out = subprocess.run([str(program)], check=True, capture_output=True, text=True).stdout.split()
    assert out == ["2000", "2000", "0", "250", "120", "120"]


def test_held_at_start_opens_late_pads():
    held = body("static int held_at_start(void)")
    assert "held_wait(steam, count, shift < 0)" in held
    assert "gamepad = SDL_OpenGamepad(pads[index]);" in held
    assert "launcher: a controller showed up %llu ms into the start" in held


def test_bgfx_overlay_inputs_have_no_gap():
    """the overlay's vertex inputs (the setup screen's and DELTA's on bgfx)
    take locations 0 to n-1 with none unused: bgfx's Vulkan renderer gives
    an unused location in the container's list a stand-in instance input
    at locations after the layout's own, which landed on the texture
    coordinates at 10 and 11 and left the setup screen blank on bgfx; and
    the layout gives each location's attribute (bg_attribute_names)"""
    source = (ROOT / "port/linux/src/d3d8_bgfx.inl").read_text()
    names = re.search(r"bg_attribute_names\[XGPU_VERTEX_ATTRIBUTE_COUNT\] =\s*\{(.*?)\};", source, re.S).group(1)
    names = re.findall(r'"(a_\w+)"', names)
    start = source.index("static const char bg_overlay_vertex_source[] =")
    shader = source[start:source.index("static const char bg_overlay_fragment_source[]", start)]
    inputs = {int(location): name for location, name in
              re.findall(r'layout\(location = (\d+)\) in \w+ (a_\w+);', shader)}
    assert sorted(inputs) == list(range(len(inputs)))
    for location, name in inputs.items():
        assert names[location] == name, (location, name)
    attributes = {"a_position": "POSITION", "a_normal": "NORMAL", "a_tangent": "TANGENT",
                  "a_bitangent": "BITANGENT", "a_color0": "COLOR0"}
    prepare = source[source.index("static BOOL bg_overlay_prepare("):]
    prepare = prepare[:prepare.index("bgfx_vertex_layout_end(&bg.overlay_layout);")]
    added = re.findall(r"bgfx_vertex_layout_add\(&bg\.overlay_layout, BGFX_ATTRIB_(\w+),", prepare)
    assert sorted(added) == sorted(attributes[name] for name in inputs.values())
    # (in ui_overlay.c's struct vertex order: position, texture coordinate,
    # shape, edge, color)
    assert added == ["POSITION", "TANGENT", "NORMAL", "BITANGENT", "COLOR0"]
