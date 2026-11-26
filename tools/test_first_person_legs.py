# SPDX-FileCopyrightText: 2026 BirchWoodGod
# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""First-person legs (display.first_person_legs, port/linux/game/
first_person_body.c): your own body below the first-person view.

The C tests of the rules (port/linux/game/first_person_body_rules.c,
port/linux/tests/first_person_body_test.c: when the body is drawn, which of
a Spartan's nodes are, the knee near the eye folding the thigh, how far the
body moves back), built with the build machine's C compiler; and what the
sources must keep that the C tests cannot reach: the setting off by default
and documented, its rows in Video Setup and the Guide, the render's hooks
(the view's own unit only, without what it holds, its shadow whole, mirrors
as before), the map's own legs (SPV3, or a map whose Lua scripts pose
objects) leaving it off, and nothing written to the game's state.

The game itself, with a build and the stock maps: tools/test_determinism.py
--first-person-legs (a game recorded with the legs off replays tick for tick
with them on)."""
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
GAME = ROOT / "port" / "linux" / "game"
RULES_C = GAME / "first_person_body_rules.c"
BODY_C = GAME / "first_person_body.c"
TEST_C = ROOT / "port" / "linux" / "tests" / "first_person_body_test.c"
RENDER_OBJECTS_C = ROOT / "source" / "render" / "render_objects.c"
SETTING = "display.first_person_legs"


def read(path):
    return (ROOT / path).read_text(encoding="utf-8", errors="replace")


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def function(source, signature):
    """a function's text, from its signature to its closing brace (its
    definition, after any prototype)"""
    start = source.rindex("\n" + signature)
    end = re.search(r"\n\}\n", source[start:])
    return source[start:start + end.end()]


@pytest.mark.parametrize("standard", ["c11", "gnu89"])
def test_rules(tmp_path, standard):
    """Builds the rules with their tests, warnings as errors, as C11 and as
    the game's gnu89, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "first_person_body_test"
    build = subprocess.run(
        [cc, f"-std={standard}", "-Wall", "-Wextra", "-Werror", "-O1", str(TEST_C), str(RULES_C), "-o", str(binary),
         "-lm"],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "all passed" in result.stdout


def test_off_by_default_and_documented():
    config = read("port/linux/src/port_config.c")
    match = re.search(r'\{ "display\.first_person_legs", _config_boolean, "(\w+)", "HALO_FIRST_PERSON_LEGS"', config)
    assert match and match.group(1) == "false"
    settings = read("docs/settings.md")
    readme = read("port/linux/README.md")
    for text in (settings, readme):
        assert f"`{SETTING}`" in text
        assert "SPV3" in text[text.index(f"`{SETTING}`"):][:3000]
    assert "first-person legs" in read("README.md").lower()


def test_rows_in_video_setup_and_the_guide():
    import port_settings
    rows = [row for spec in port_settings.SCREENS.values() for view in spec.get("views", [])
            for row in view[3] if row[1] == SETTING]
    assert len(rows) == 1
    assert rows[0][2] == port_settings.ON_OFF and rows[0][4] is None
    view = next(view for view in port_settings.SCREENS["video_settings"]["views"] if view[0] == "view")
    assert any(row[1] == SETTING for row in view[3])
    # (the menus' files are port_settings.py's, as written)
    assert "op_first_person_legs" in read("port/assets/menus/chupathingy/settings_pages.xml")
    assert f'setting="{SETTING}"' in read(
        "port/assets/menus/ce/main_menu.settings_select.player_setup.player_profile_edit.video_settings.xml")
    rows_c = read("port/linux/src/settings_rows.c")
    assert re.search(r'\{ _in_display \| _in_guide, "FIRST-PERSON LEGS", "display\.first_person_legs", _kind_toggle',
                     rows_c)
    display = re.search(r"static const char \*const display_lines\[\] =\s*\{(.*?)\};", rows_c, re.S).group(1)
    assert f'"{SETTING}"' in display


def test_render_hooks():
    """render_objects.c draws the view's own unit's body with the node
    matrices made for it, without its widgets and what it holds, and casts
    its shadow whole; a mirror draws it as before."""
    source = RENDER_OBJECTS_C.read_text(encoding="utf-8")
    assert '#include "first_person_body.h"' in source
    listing = function(source, "static void render_object_list(")
    assert "first_person_body = first_person_body_shown(object_index);" in listing
    assert "if (!render.camera.mirrored && object_is_first_person_camera(object_index))" in listing
    assert "if (!object_is_first_person_camera(object_index) || render.camera.mirrored || first_person_body)" in listing
    assert "? first_person_body_node_matrices(object_index)" in listing
    assert "object->object.first_widget_index != NONE && !first_person_body" in listing
    assert "first_child_object_index != NONE && (data->shadow || !first_person_body)" in listing
    # (the shadow pass: the body's matrices are for the view, not the shadow)
    shadow = listing[listing.index("level_of_detail_pixels * 0.3f"):]
    assert "first_person_body_node_matrices" not in shadow[:400]
    single = function(source, "static void render_object(")
    assert "(!render.camera.mirrored && first_person_body_shown(data->object_index))" in single


def test_rules_of_the_body():
    """The view's own unit alone, and each case that hides it, as the
    issue asks: the map's own legs, dead, out of first person, a
    cinematic, a vehicle, zoomed, Forge's monitor."""
    source = BODY_C.read_text(encoding="utf-8")
    shown = function(source, "boolean first_person_body_shown(")
    assert "if (unit_index != object_index)" in shown
    for line in (
        "state.map_draws_its_own = spv3_mode_active() || lua_scripting_map_poses_objects();",
        "state.dead = TEST_FLAG(unit->object.damage_flags, _object_dead_bit);",
        "state.first_person = director_get_perspective(local_player_index) == _director_perspective_first_person;",
        "state.cinematic = cinematic_in_progress();",
        "state.in_vehicle = unit->object.parent_object_index != NONE;",
        "state.zoomed = unit->unit.current_zoom_level != NONE;",
        "state.forge_monitor = forge_monitor_hides_prompt(local_player_index);",
    ):
        assert line in shown, line
    assert "render.local_player_index" in shown
    assert f'config_boolean("{SETTING}")' in source


def test_map_scripts_that_pose_objects():
    """A map whose Lua scripts pose an object (SPV3's first-person legs)
    says so for the rest of the map, and a new map starts over."""
    source = read("port/linux/game/lua_scripting.c")
    note = function(source, "void lua_scripting_note_posed(")
    assert "lua_map_poses_objects = TRUE;" in note
    assert "lua_map_poses_objects = FALSE;" in function(source, "void lua_scripting_map_load(")
    assert "lua_map_poses_objects = FALSE;" in function(source, "void lua_scripting_map_unload(")
    header = read("port/linux/include/lua_scripting.h")
    assert "boolean lua_scripting_map_poses_objects(void);" in header
    # (a build without Lua: never)
    assert "#define lua_scripting_map_poses_objects() ((boolean)0)" in header


def test_drawing_only():
    """Nothing in the body's code writes to an object, a unit, a player or
    the game's state: it copies the pose into its own matrices."""
    source = BODY_C.read_text(encoding="utf-8") + RULES_C.read_text(encoding="utf-8")
    code = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    for pattern in (r"object->object\.[a-z_.]+\s*=[^=]", r"unit->[a-z_.]+\s*=[^=]", r"player->[a-z_.]+\s*=[^=]",
                    r"object_set_", r"unit_set_", r"game_state", r"random"):
        assert not re.search(pattern, code), pattern
    matrices = function(BODY_C.read_text(encoding="utf-8"), "real_matrix4x3 const *first_person_body_node_matrices(")
    assert "memcpy(matrices, pose," in matrices


def test_license_headers():
    for path in (RULES_C, BODY_C, GAME / "first_person_body.h", GAME / "first_person_body_rules.h", TEST_C):
        text = path.read_text(encoding="utf-8")
        assert "SPDX-License-Identifier: GPL-3.0-or-later" in text, path
        assert "SPDX-FileCopyrightText: 2026 BirchWoodGod" in text, path
