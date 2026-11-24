# SPDX-FileCopyrightText: 2026 BirchWoodGod
# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Ragdolls (port/linux/game/ragdolls.c, display.ragdolls) and Jolt Physics
(port/third_party/jolt, port/linux/game/physics_jolt.cpp).

Without a build: the setting is off by default and documented; the feature
is compiled only where Jolt is (HALO_JOLT) and never on a console or the
dedicated server; every hook in the game's sources is behind
HALO_PORT_RAGDOLLS; the C face between the game's ABI and Jolt's passes no
type whose size the two disagree on; the vendored Jolt is the release its
README names, with its license and a notices entry; Jolt out of memory stops
the ragdolls, not the game; set off, the level's mesh goes too; the render
asks for no ragdoll while none is kept.

With a build and the stock maps (HALO_DETERMINISM_BINARY,
HALO_DETERMINISM_MAPS, as tools/test_determinism.py): Blood Gulch with two
split screen players, the second killed every four seconds
(debug.network_test_kill): a game recorded with ragdolls off replays tick
for tick with them on (and ragdolls are made), and one recorded on replays
with them off.
"""
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

HOOKS = {
    "source/units/units.c": "ragdolls_unit_died(",
    "source/objects/damage.c": "ragdolls_note_damage(",
    "source/game/game_time.c": "ragdolls_tick(",
    "source/game/game.c": "ragdolls_new_map(",
    "source/saved games/game_state.c": "ragdolls_reset,",
    "source/render/render_objects.c": "ragdolls_find_rendered(",
    "source/hs/hs.c": "ragdolls_command(",
    "port/linux/game/render_interpolation.c": "ragdolls_pose(",
}


def read(path):
    return (ROOT / path).read_text(encoding="utf-8", errors="replace")


def test_off_by_default_and_documented():
    config = read("port/linux/src/port_config.c")
    match = re.search(r'\{ "display\.ragdolls", _config_boolean, "(\w+)"', config)
    assert match and match.group(1) == "false"
    assert re.search(r'\{ "debug\.ragdolls", _config_integer, "0"', config)
    assert "`display.ragdolls`" in read("docs/settings.md")
    assert "`display.ragdolls`" in read("port/linux/README.md")


def test_menu_row_in_the_chupathingy_menus_only():
    import port_settings
    rows = [row for spec in port_settings.SCREENS.values() for view in spec.get("views", [])
            for row in view[3] if row[1] == "display.ragdolls"]
    assert len(rows) == 1 and rows[0][4] == "desktop"
    assert "op_ragdolls" in read("port/assets/menus/chupathingy/settings_pages.xml")
    assert '"RAGDOLLS"' in read("port/linux/src/settings_rows.c")


def test_compiled_only_with_jolt_and_never_on_consoles_or_servers():
    features = read("port/linux/include/halo_port_features.h")
    block = features[features.index("#if defined(HALO_JOLT)"):]
    block = block[:block.index("#endif")]
    assert "#define HALO_PORT_RAGDOLLS" in block
    for switch in ("!defined(HALO_SERVER)", "!defined(HALO_CONSOLE_NO_RAGDOLLS)", "defined(HALO_CONSOLE)",
                   "defined(HALO_XBOX_CONSOLE)", "defined(HALO_SWITCH_CONSOLE)", "defined(HALO_WIIU_CONSOLE)",
                   "defined(HALO_XBOX360_CONSOLE)"):
        assert switch in block, switch
    assert "#ifdef HALO_PORT_RAGDOLLS" in read("port/linux/game/ragdolls.c")


def test_configure_can_leave_them_out():
    # (configure.py --without-ragdolls: no Jolt in any desktop build, and
    # Video Setup's row hidden, as the setting is compiled out with it)
    assert '"--without-ragdolls"' in read("configure.py")
    assert "with_ragdolls=args.with_ragdolls" in read("configure.py")
    for path in ("tools/linux_build.py", "tools/linux64_build.py", "tools/macos_build.py", "tools/unix64_build.py",
                 "tools/windows_build.py"):
        text = read(path)
        assert 'getattr(sln, "with_ragdolls", True)' in text, path
        assert "jolt=True" not in text, path
    assert '"--without-ragdolls"' in read("tools/ci_build.py")
    menus = read("port/linux/game/menu_functions.c")
    block = menus[menus.index("#ifndef HALO_PORT_RAGDOLLS"):]
    assert '"op_ragdolls"' in block[:block.index("#endif")]


def test_every_hook_is_behind_the_feature():
    for path, call in HOOKS.items():
        text = read(path)
        assert call in text, f"{path}: no {call}"
        for match in re.finditer(re.escape(call), text):
            before = text[:match.start()]
            assert before.rfind("#ifdef HALO_PORT_RAGDOLLS") > before.rfind("#endif"), f"{path}: {call} not behind HALO_PORT_RAGDOLLS"


def test_the_c_face_passes_no_type_the_two_abis_disagree_on():
    """physics_jolt.h joins units built with the Xbox's ABI (-malign-double,
    a 16-bit wchar_t, long rewritten to int on the 64-bit builds) and units
    built with the host's: only ints, floats and pointers"""
    header = read("port/linux/game/physics_jolt.h")
    code = re.sub(r"/\*.*?\*/", "", header, flags=re.S)
    for word in ("long", "double", "wchar_t", "size_t", "bool"):
        assert not re.search(rf"\b{word}\b", code), word
    # (no structure returned by value)
    assert not re.search(r"^struct \w+ physics_jolt_\w+\(", code, flags=re.M)


def test_vendored_jolt_is_the_named_release():
    readme = read("port/third_party/jolt/README.md")
    version = re.search(r"release `v(\d+)\.(\d+)\.(\d+)`", readme)
    core = read("port/third_party/jolt/Jolt/Core/Core.h")
    found = tuple(re.search(rf"#define JPH_VERSION_{part} (\d+)", core).group(1)
                  for part in ("MAJOR", "MINOR", "PATCH"))
    assert version and version.groups() == found
    assert (ROOT / "port/third_party/jolt/LICENSE").is_file()
    components = json.loads(read("port/notices/components.json"))["components"]
    entry = [c for c in components if c["name"] == "Jolt Physics"]
    assert entry and "port/third_party/jolt/LICENSE" in entry[0]["files"]
    assert f"release {'.'.join(found)}" in entry[0]["source"]


def test_jolt_units_listed(monkeypatch):
    sys.path.insert(0, str(ROOT))
    from tools.linux_build import jolt_sources
    monkeypatch.chdir(ROOT)
    sources = jolt_sources()
    assert all(source.is_file() for source in (ROOT / s for s in sources))
    names = {source.as_posix() for source in sources}
    assert "port/linux/game/physics_jolt.cpp" in names
    assert not any(name.endswith(("JobSystemThreadPool.cpp", "StateRecorderImpl.cpp")) for name in names)


def test_link_check_lets_only_jolts_units_call_wide_functions():
    """libc++ makes Jolt's std::find over ints a wmemchr call (FreeBSD): the
    link check lets Jolt's units and its C face, built with the host's
    32-bit wchar_t, call it, and no game unit"""
    import linux_link_check

    allowed = linux_link_check.HOST_ABI_OBJECT
    assert allowed.search("build/freebsd64/obj/port/third_party/jolt/Jolt/Geometry/ConvexHullBuilder.o")
    assert allowed.search("build/freebsd64/obj/port/linux/game/physics_jolt.o")
    assert not allowed.search("build/freebsd64/obj/port/linux/game/ragdolls.o")
    assert not allowed.search("build/freebsd64/obj/source/game/game.o")
    assert not allowed.search("build/freebsd64/obj/port/linux/src/posix_files.o")


def test_console_tests_only_in_a_local_game():
    """the ragdolls' test commands (kill, boom, shoot, punch) hurt players:
    never in a network game, hosted or joined"""
    source = read("port/linux/game/ragdolls.c")
    body = source[source.index("boolean ragdolls_command("):]
    gate = body[:body.index('!strcmp(word, "boom")')]
    assert "game_connection() != _game_connection_local" in gate
    assert "_game_connection_network_client" not in gate


def test_out_of_memory_stops_the_ragdolls_not_the_game():
    """a Jolt allocation the C heap refuses is given from the world's
    reserve (Jolt never sees a null), and the ragdolls stop that tick: every
    body let go, the world and the level's mesh gone, off until the setting
    is turned off and on. Only a refusal the reserve cannot take either
    stops the game"""
    face = read("port/linux/game/physics_jolt.cpp")
    allocate = face[face.index("void *raw_allocate(size_t bytes)"):]
    allocate = allocate[:allocate.index("\n}\n")]
    assert "reserve.out_of_memory = true;" in allocate
    # (the only abort: after the reserve, with no room in it either)
    assert allocate.count("std::abort()") == 1
    assert allocate.index("reserve.out_of_memory = true;") < allocate.index("std::abort()")
    assert "bytes > RESERVE_SIZE - start" in allocate[:allocate.index("std::abort()")]
    # (every free goes through raw_free, which knows the reserve's blocks)
    frees = face[face.index("void counted_free"):face.index("/* ---------- layers")]
    assert "std::free" not in frees and frees.count("raw_free(") == 2
    # (nothing more added or stepped once out of memory)
    for entry in ("int physics_jolt_add_mesh(", "int physics_jolt_add_moving_mesh(", "int physics_jolt_add_moving_hull(",
                  "int physics_jolt_rig_create(", "void physics_jolt_step("):
        body = face[face.index(entry):]
        body = body[:body.index("\n}\n")]
        assert "reserve.out_of_memory" in body, entry
    ragdolls = read("port/linux/game/ragdolls.c")
    tick = ragdolls[ragdolls.index("void ragdolls_tick("):]
    tick = tick[:tick.index("\n}\n")]
    assert tick.count("ragdolls_out_of_memory()") == 2
    stop = ragdolls[ragdolls.index("static boolean ragdolls_out_of_memory("):]
    stop = stop[:stop.index("\n}\n")]
    for step in ("physics_jolt_out_of_memory()", "ragdoll_release(slot", "ragdoll_world_release()",
                 "physics_jolt_forget_meshes()", "ragdolls_stopped = TRUE"):
        assert step in stop, step
    enabled = ragdolls[ragdolls.index("static boolean ragdolls_enabled("):]
    enabled = enabled[:enabled.index("\n}\n")]
    assert "return enabled && !ragdolls_stopped;" in enabled
    assert "ragdolls_stopped = FALSE;" in enabled


def test_level_mesh_let_go_when_turned_off():
    """display.ragdolls set off: no world, and the level's mesh kept for the
    next world let go too"""
    ragdolls = read("port/linux/game/ragdolls.c")
    tick = ragdolls[ragdolls.index("void ragdolls_tick("):]
    off = tick[tick.index("if (!ragdolls_enabled())"):]
    off = off[:off.index("return;")]
    assert "ragdoll_world_release()" in off
    assert "physics_jolt_forget_meshes()" in off


def test_no_ragdoll_asked_for_while_none_is_kept():
    """render_interpolation_object_node_matrices asks for a ragdoll's pose
    only in a frame some ragdoll is kept in: with none, it is as before"""
    source = read("port/linux/game/render_interpolation.c")
    assert "frame_ragdolls = ragdolls_frame_begin();" in source
    body = source[source.index("real_matrix4x3 *render_interpolation_object_node_matrices("):]
    body = body[:body.index("\n}\n")]
    assert body.count("ragdolls_drawn_pose(") == 1
    gate = body[:body.index("ragdolls_drawn_pose(")]
    assert "if (frame_ragdolls &&" in gate
    ragdolls = read("port/linux/game/ragdolls.c")
    begin = ragdolls[ragdolls.index("boolean ragdolls_frame_begin("):]
    begin = begin[:begin.index("\n}\n")]
    assert "return ragdoll_swapped.count > 0;" in begin


def test_ragdolls_on_and_off_replay_alike(tmp_path):
    """the same game, ragdolls off and on: identical tick for tick"""
    import pytest
    import test_determinism as determinism

    binary = os.environ.get("HALO_DETERMINISM_BINARY")
    maps = os.environ.get("HALO_DETERMINISM_MAPS")
    if not binary or not maps or not os.path.exists(binary) or \
            not os.path.exists(os.path.join(maps, f"{determinism.DEFAULT_MAP}.map")):
        pytest.skip("no build or maps (HALO_DETERMINISM_BINARY, HALO_DETERMINISM_MAPS)")
    work = str(tmp_path)
    # (two split screen players, the host killing the second every four
    # seconds on the game's clock: deaths for ragdolls to be made of)
    deaths = {"HALO_NETWORK_TEST_KILL": "4"}
    off = {"HALO_RAGDOLLS": "false", **deaths}
    on = {"HALO_RAGDOLLS": "true", "HALO_RAGDOLLS_DEBUG": "1", **deaths}
    ticks = determinism.DEFAULT_TICKS
    game_map = determinism.DEFAULT_MAP
    players = 2
    inputs_off = os.path.join(work, "off.bin")
    inputs_on = os.path.join(work, "on.bin")
    runs = {
        "record-off": determinism.play(binary, maps, work, "record-off", "record", inputs_off, game_map, ticks, 1, players,
                                       extra_env=off),
        "replay-on": determinism.play(binary, maps, work, "replay-on", "replay", inputs_off, game_map, ticks, 1, players,
                                      extra_env=on),
        "record-on": determinism.play(binary, maps, work, "record-on", "record", inputs_on, game_map, ticks, 1, players,
                                      extra_env=on),
        "replay-off": determinism.play(binary, maps, work, "replay-off", "replay", inputs_on, game_map, ticks, 1, players,
                                       extra_env=off),
    }
    hashes = {name: determinism.read_hashes(path) for name, path in runs.items()}
    with open(os.path.join(work, "replay-on", "out.log"), errors="replace") as output:
        assert re.search(r"ragdolls: \S+ \(\S+\) started", output.read()), "no ragdoll was made"
    problems = determinism.compare("record-off", hashes["record-off"], "replay-on", hashes["replay-on"])
    problems += determinism.compare("record-on", hashes["record-on"], "replay-off", hashes["replay-off"])
    assert not problems, "\n".join(problems)
