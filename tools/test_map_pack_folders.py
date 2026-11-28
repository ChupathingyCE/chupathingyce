"""The map packs' folders the setup screen's "Make the folders" makes (#1881,
port/linux/src/map_pack_folders.c): maps_cex, maps_spv3, maps_mcc and maps_os, each
with a README.txt, in a scratch folder (port/linux/tests/
map_pack_folders_test.c, built with the build machine's C compiler and the
file helpers of port/linux/src/posix_files.c, so anywhere but Windows).
Also checks that both ways of making the folders make them, that each
README.txt is one to three plain sentences whose link is a section of
docs/maps.md, and that maps_stubbs is not among them."""
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "port" / "linux" / "src"
UNIT = SRC / "map_pack_folders.c"
TEST = ROOT / "port" / "linux" / "tests" / "map_pack_folders_test.c"
DOCS = ROOT / "docs" / "maps.md"
DOCS_URL = "https://github.com/ChupathingyCE/chupathingyce/blob/main/docs/maps.md"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


@pytest.mark.skipif(sys.platform == "win32", reason="the test uses POSIX file helpers")
def test_map_pack_folders(tmp_path):
    """Builds the unit with its tests, warnings as errors, and runs them in an
    empty scratch folder."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "map_pack_folders_test"
    build = subprocess.run(
        [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1", "-D_GNU_SOURCE", "-D_FILE_OFFSET_BITS=64",
         str(TEST), str(UNIT), str(SRC / "posix_files.c"), "-lpthread", "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    scratch = tmp_path / "root"
    scratch.mkdir()
    result = subprocess.run([str(binary), str(scratch)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def readmes():
    """{folder: README.txt's text}, as the unit has them (C string pieces joined)."""
    text = UNIT.read_text()
    table = text[text.index("folders[MAP_PACK_FOLDER_COUNT] ="):text.index("};", text.index("folders[MAP_PACK"))]
    found = {}
    for entry in re.finditer(r'\{ "(maps_\w+)",\s*\{[^}]*\},\s*((?:"(?:[^"\\]|\\.)*"\s*|DOCS\s*)+)\}', table):
        pieces = re.findall(r'"((?:[^"\\]|\\.)*)"|(DOCS)', entry.group(2))
        found[entry.group(1)] = "".join(DOCS_URL if docs else piece.replace("\\n", "\n").replace('\\"', '"')
                                        for piece, docs in pieces)
    return found


def anchors():
    """docs/maps.md's section anchors, as GitHub makes them."""
    result = set()
    for line in DOCS.read_text().splitlines():
        if line.startswith("#"):
            title = line.lstrip("#").strip().lower()
            result.add(re.sub(r"[^\w\- ]", "", title).replace(" ", "-"))
    return result


def test_readmes():
    """Each README.txt is one to three short sentences and a link to its section
    of docs/maps.md; no maps_stubbs (an early test, unlisted)."""
    found = readmes()
    assert sorted(found) == ["maps_cex", "maps_mcc", "maps_os", "maps_spv3"]
    sections = anchors()
    for folder, text in found.items():
        lines = text.strip().split("\n")
        link = lines[-1]
        assert link.startswith("More: " + DOCS_URL + "#"), folder
        assert link.split("#", 1)[1] in sections, (folder, link)
        sentences = [line for line in lines[:-1] if line]
        assert 1 <= len(sentences) <= 3, folder
        assert all(line.endswith(".") for line in sentences), folder
    assert "maps_stubbs" not in UNIT.read_text()


def test_both_ways_make_them():
    """The setup screen (launcher.c) and the message boxes (sdl_platform.c)
    both make the packs' folders with the others."""
    launcher = (SRC / "launcher.c").read_text()
    body = launcher[launcher.index("static void make_folders(void)"):]
    assert "map_pack_folders_make(launcher.root);" in body[:body.index("\n}\n")]
    platform = (SRC / "sdl_platform.c").read_text()
    body = platform[platform.index("static BOOL data_make_folders(const char *destination)"):]
    body = body[:body.index("\n}\n")]
    assert "map_pack_folders_make(destination);" in body
    for folder in ("maps_cex", "maps_spv3", "maps_mcc", "maps_os"):
        assert folder in body
