"""Checks the consoles' structure culling default (HALO_CONSOLE_NO_STRUCTURE_CULLING:
port/linux/src/port_config.c, port/linux/include/halo_console_features.h).

A console whose processor holds its frame rate defines the capability, and
debug.structure_culling then defaults to false; every other build keeps true.
The setting decides only which of the level's surfaces are drawn, so only the
culling walk (port/linux/game/structure_culling.c) may read it: never the
simulation."""
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
PORT_CONFIG = ROOT / "port" / "linux" / "src" / "port_config.c"
FEATURES = ROOT / "port" / "linux" / "include" / "halo_console_features.h"
CULLING = ROOT / "port" / "linux" / "game" / "structure_culling.c"
GATE = "HALO_CONSOLE_NO_STRUCTURE_CULLING"


def default_block():
    """port_config.c's #ifdef GATE ... #endif that defines the default"""
    text = PORT_CONFIG.read_text(encoding="utf-8")
    match = re.search(r"^#ifdef " + GATE + r"\n.*?^#endif\n", text, re.M | re.S)
    assert match, "no #ifdef %s block in port_config.c" % GATE
    return match.group(0)


@pytest.mark.parametrize("defined, expected", [(False, "true"), (True, "false")])
def test_default_follows_the_capability(defined, expected):
    cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "harness.c"
        binary = Path(directory) / "harness"
        source.write_text(default_block() + '#include <stdio.h>\nint main(void) { puts(DEFAULT_STRUCTURE_CULLING); return 0; }\n')
        subprocess.run([cc, "-Wall", "-Werror", *(["-D" + GATE] if defined else []), str(source), "-o", str(binary)],
                       check=True)
        output = subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout.strip()
    assert output == expected


def test_setting_uses_the_default():
    text = PORT_CONFIG.read_text(encoding="utf-8")
    assert re.search(r'\{ "debug\.structure_culling", _config_boolean, DEFAULT_STRUCTURE_CULLING,', text)


def test_capability_is_documented():
    assert re.search(r"^" + GATE + r"$", FEATURES.read_text(encoding="utf-8"), re.M)


def test_only_the_culling_walk_reads_it():
    readers = []
    for path in list((ROOT / "source").rglob("*.c")) + list((ROOT / "port" / "linux").rglob("*.c")):
        if path == PORT_CONFIG:
            continue
        if '"debug.structure_culling"' in path.read_text(encoding="utf-8", errors="replace"):
            readers.append(path)
    assert readers == [CULLING]
