"""Production browser deduplication, including negative controls; no external harness."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[3]
CASES = ["one-game-many-versions", "different-games-kept", "same-version-kept"]
NEGATIVE_CONTROLS = {
    "never-merged": (("for (other = 0; other < kept && ", "for (other = kept; other < kept && "),
                     "merge_listings", "one-game-many-versions"),
    "oldest-kept": (("games[index].version > games[other].version", "games[index].version < games[other].version"),
                    "merge_listings", "one-game-many-versions"),
    "same-version-merged": (("a->version != b->version &&", ""), "same_game", "same-version-kept"),
    "players-ignored": (("a->players == b->players &&", ""), "same_game", "different-games-kept"),
    "map-ignored": (("!strcmp(a->map, b->map) &&", ""), "same_game", "different-games-kept"),
    "roster-ignored": (("!memcmp(a->roster, b->roster, sizeof(a->roster))", "1"),
                       "same_game", "different-games-kept"),
}


def build(directory, fault=None, faulty_function=None):
    cc = shutil.which("clang") or shutil.which("cc")
    if not cc:
        pytest.skip("C compiler required")
    source = (ROOT / "port/linux/src/browser.c").read_text()
    code = []
    for name in ("same_game", "merge_listings"):
        start = source.index("static int " + name + "(")
        function = source[start:source.index("\n}\n", start) + 3]
        if fault and name == faulty_function:
            old, new = fault
            assert function.count(old) == 1, f"negative control must match exactly once: {name}"
            function = function.replace(old, new)
        code.append(function)
    (directory / "under_test.inc").write_text("\n".join(code))
    shutil.copy(ROOT / "port/linux/src/browser.h", directory / "browser.h")
    shutil.copy(Path(__file__).with_name("browser_listings.c.in"), directory / "main.c")
    binary = directory / "listings"
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                    "-fsanitize=address,undefined", str(directory / "main.c"), "-o", str(binary)], check=True)
    return binary


def run(binary, case):
    done = subprocess.run([str(binary), case], capture_output=True, text=True, timeout=30)
    return done.returncode, done.stdout + done.stderr


@pytest.mark.parametrize("case", CASES)
def test_case(tmp_path, case):
    status, output = run(build(tmp_path), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(tmp_path, control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build(tmp_path, fault, faulty_function), case)
    assert status != 0, f"{control}: {case} did not catch it: {output}"
