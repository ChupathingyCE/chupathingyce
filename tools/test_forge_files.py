"""Runs the C tests of Forge's layout files (port/linux/game/forge_files.c,
the format and its validator) and their folder (port/linux/src/
forge_store.c) with the build machine's C compiler, in a scratch folder:
the writer and its versions, a round trip of every record kind, each kind
of damaged or hostile file refused with its own result, a fixed-seed fuzz
of the validator, and the store's saves, loads and names
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
    ("if (version > FORGE_FILE_VERSION ||\n", "if (version > 99 ||\n"),
    # (the skipped versions read as if they were known)
    ("(version >= FORGE_FILE_FIRST_SKIPPED_VERSION && version <= FORGE_FILE_LAST_SKIPPED_VERSION))\n\t{\n\t\tsnprintf",
     "0)\n\t{\n\t\tsnprintf"),
    # (an effect's path not checked against its group)
    ("layout->paths[record->path_index].group_tag != (decal ? FORGE_FILE_DECAL_GROUP : FORGE_FILE_EFFECT_GROUP) ||", ""),
    # (another kind's own bytes not looked at)
    ("if (!forge_file_params_zero(record))\n\t\treturn 0;", "(void)forge_file_params_zero;"),
    # (the checksum not looked at)
    ("!= checksum)", "!= checksum && 0)"),
    # (an object's count let past 1)
    ("(record->kind == _forge_record_object && record->count > 1)", "0"),
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
