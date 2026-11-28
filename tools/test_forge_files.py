# SPDX-FileCopyrightText: 2026 BirchWoodGod
# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Runs the C tests of Forge's layout files (port/linux/game/forge_files.c,
the format and its validator) and their folder (port/linux/src/
forge_store.c) with the build machine's C compiler, in a scratch folder:
the writer and its versions, a round trip of every record kind, each kind
of damaged or hostile file refused with its own result, a fixed-seed fuzz
of the validator, the map's own objects block, and the store's saves,
loads and names
(port/linux/tests/forge_files_test.c). With AddressSanitizer and
UndefinedBehaviorSanitizer when the compiler has them. The store needs the
file helpers of port/linux/src/posix_files.c, so it runs anywhere but
Windows."""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / "port" / "linux" / "tests" / "forge_files_test.c"
FILES = ROOT / "port" / "linux" / "game" / "forge_files.c"
UNITS = [ROOT / "port" / "linux" / "src" / name for name in ("forge_store.c", "port_folders.c", "posix_files.c")]


def compiler():
    """Finds a C compiler on the build machine.

    Returns the path of clang, cc or gcc (the first found), or None.
    """
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def build(cc, binary, files_unit, tmp_path):
    """Builds the test against files_unit (forge_files.c or a changed copy),
    with the sanitizers when they link. Returns the compiler's result."""
    libraries = ["-lpthread"] + (["-ldl"] if sys.platform.startswith("linux") else [])
    command = [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1", "-g", "-D_GNU_SOURCE",
               "-D_FILE_OFFSET_BITS=64", "-I", str(ROOT / "port" / "linux" / "game"), str(TEST), str(files_unit),
               *map(str, UNITS), *libraries, "-o", str(binary)]
    sanitized = subprocess.run(command[:1] + ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
                               + command[1:], capture_output=True, text=True)
    if sanitized.returncode == 0:
        return sanitized
    return subprocess.run(command, capture_output=True, text=True)


@pytest.mark.skipif(sys.platform == "win32", reason="the store's tests use POSIX file helpers")
def test_forge_files(tmp_path):
    """Builds the layout files' units with their tests, warnings as errors, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "forge_files_test"
    scratch = tmp_path / "scratch"
    scratch.mkdir()
    result = build(cc, binary, FILES, tmp_path)
    assert result.returncode == 0, result.stdout + result.stderr
    run = subprocess.run([str(binary), str(scratch), "20000"], capture_output=True, text=True, timeout=600)
    assert run.returncode == 0, run.stdout + run.stderr
    assert " 0 failed" in run.stdout


@pytest.mark.skipif(sys.platform == "win32", reason="the store's tests use POSIX file helpers")
@pytest.mark.parametrize("original, mutated", [
    # (a newer version read as if it were known)
    ("if (version > FORGE_FILE_VERSION ||\n", "if (version > 17 ||\n"),
    # (the Vita build's versions 8 to 10 read as if they were this build's)
    ("if (version > FORGE_FILE_VERSION ||\n\t\t(version >= FORGE_FILE_FIRST_SKIPPED_VERSION && version <= FORGE_FILE_LAST_SKIPPED_VERSION))",
     "if (version > FORGE_FILE_VERSION)"),
    # (a route point's route not checked)
    ("record->team >= FORGE_FILE_MAXIMUM_ROUTES ||", "0 ||"),
    # (more than 256 records let past under a version before 13)
    ("layout->version && layout->version < FORGE_FILE_MORE_RECORDS_VERSION &&", "0 &&"),
    # (a volume's amount not checked against its type)
    ("record->variant == FORGE_FILE_VOLUME_KILL ? amount != 0 :", "0 ? 0 :"),
    # (the fog's start not checked against where it is whole)
    ("(float)start >= (float)opaque * FORGE_FILE_FOG_OPAQUE_UNIT)", "0)"),
    # (an effect's path not checked against its group)
    ("layout->paths[record->path_index].group_tag != (decal ? FORGE_FILE_DECAL_GROUP : FORGE_FILE_EFFECT_GROUP) ||", ""),
    # (another kind's own bytes not looked at)
    ("if (!forge_file_params_zero(record))\n\t\treturn 0;", "(void)forge_file_params_zero;"),
    # (the checksum not looked at)
    ("!= checksum)", "!= checksum && 0)"),
    # (an object's count let past 1)
    ("(record->kind == _forge_record_object && record->count > 1)", "0"),
    # (a name Windows keeps for a device let through)
    ("return !forge_file_name_reserved(file_name);", "return 1;"),
    # (the map's own objects' entries let out of order)
    ("entry[-1].index >= entry->index", "0"),
])
def test_forge_files_negative_controls(tmp_path, original, mutated):
    """The validator with a check taken out must fail the tests: they check
    what they say."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    source = FILES.read_text()
    assert original in source
    changed = tmp_path / "forge_files.c"
    changed.write_text(source.replace(original, mutated, 1))
    binary = tmp_path / "forge_files_test"
    scratch = tmp_path / "scratch"
    scratch.mkdir()
    result = build(cc, binary, changed, tmp_path)
    assert result.returncode == 0, result.stdout + result.stderr
    run = subprocess.run([str(binary), str(scratch), "200"], capture_output=True, text=True, timeout=600)
    assert run.returncode != 0, "the changed validator passed every test"


def test_forge_keys_polled_only_when_forge_is_on():
    """The monitor's keys (controls.forge_*) are read from the keyboard only
    while game.forge is on: off, nothing of Forge runs, its keys too."""
    source = (ROOT / "port" / "linux" / "src" / "xinput_sdl.c").read_text()
    wanted = source[source.index("static BOOL forge_keys_wanted(void)"):]
    wanted = wanted[:wanted.index("\n}\n")]
    assert 'config_boolean("game.forge")' in wanted
    assert "#ifdef HALO_CONSOLE_NO_FORGE" in wanted
    controls = source[source.index("static void keyboard_controls("):]
    controls = controls[:controls.index("\n}\n")]
    poll = controls[controls.index("forge_bindings[action][slot]") - 400:controls.index("forge_bindings[action][slot]")]
    assert "if (forge_keys_wanted())" in poll
    assert controls.count("forge_bindings[") == 1
