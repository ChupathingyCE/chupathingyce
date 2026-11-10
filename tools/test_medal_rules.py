"""Runs the C tests of the medals' rules (port/linux/src/medal_rules.h) and
the local service record (port/linux/src/service_record.c), with the build
machine's C compiler, as C89 with GNU extensions too (the consoles' game
code). The units have no SDL or game dependency, so they compile anywhere."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCES = [
    ROOT / "port" / "linux" / "tests" / "medal_rules_test.c",
    ROOT / "port" / "linux" / "src" / "service_record.c",
]


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_medal_rules(tmp_path):
    """Builds the rules and the record with their tests, warnings as errors,
    and runs them (saving a record into a scratch folder)."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "medal_rules_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", *map(str, SOURCES), "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary), str(tmp_path / "service_record.txt")], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert (tmp_path / "service_record.txt").read_text().startswith("service_record 1\n")
    assert not (tmp_path / "service_record.txt.tmp").exists()


def test_rules_header_builds_as_gnu89(tmp_path):
    """The game's code (game_stats.c, on the consoles too) is built as gnu89:
    the header must build there, unused, with no warnings."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    source = tmp_path / "unit.c"
    source.write_text('#include "medal_rules.h"\nint unit(void) { return NUMBER_OF_MEDALS; }\n')
    build = subprocess.run(
        [cc, "-std=gnu89", "-Wall", "-Wextra", "-Werror", "-c", "-I", str(ROOT / "port" / "linux" / "src"),
         str(source), "-o", str(tmp_path / "unit.o")],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr


def test_medal_keys_match_the_docs():
    """Every medal's key is in docs/delta.md's list, in the same order."""
    header = (ROOT / "port" / "linux" / "src" / "medal_rules.h").read_text()
    keys = re.search(r"static char const \*const keys\[NUMBER_OF_MEDALS\] = \{(.*?)\};", header, re.S).group(1)
    keys = re.findall(r'"([a-z_]+)"', keys)
    enum = re.search(r"enum medal_kind\s*\{(.*?)NUMBER_OF_MEDALS", header, re.S).group(1)
    assert len(keys) == len(re.findall(r"MEDAL_[A-Z_]+", enum))
    docs = (ROOT / "docs" / "delta.md").read_text()
    section = docs[docs.index("### Medals"):]
    section = section[:section.index("\n### ", 4)]
    assert re.findall(r"`([a-z_]+)`", section)[:len(keys)] == keys


def test_recorders_share_the_rules():
    """The game list's recorder and Delta Stats' log count medals by the
    one set of rules, and the pop-ups are behind display.medals (off)."""
    stats = (ROOT / "port" / "linux" / "game" / "game_stats.c").read_text()
    log = (ROOT / "port" / "linux" / "src" / "event_log.c").read_text()
    for source in (stats, log):
        assert '#include "' in source and "medal_rules.h" in source
        assert "medal_rules_kill(" in source
    assert 'config_boolean("display.medals")' in stats
    config = (ROOT / "port" / "linux" / "src" / "port_config.c").read_text()
    assert re.search(r'\{ "display\.medals", _config_boolean, "false"', config)
