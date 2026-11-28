"""Project Ark, an early mode (port/linux/game/ark_mode.c,
port/linux/src/ark_files.c, port/linux/include/ark_names.h).

- Its files are found where they should be and nowhere else: a maps_ark
  folder beside maps, or a folder in MCC's install found by what it holds
  (port/linux/tests/ark_files_test.c, built with the build machine's C
  compiler and port/linux/src/posix_files.c, so anywhere but Windows).
- The mode's name for players is spelled in ark_names.h alone, so renaming it
  is a change to that file.
- Every call the engine and the setup screen make into the mode is behind
  HALO_ARK, so a build without the mode is the game as it was.
"""
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "port" / "linux" / "src"
NAMES = ROOT / "port" / "linux" / "include" / "ark_names.h"
TEST = ROOT / "port" / "linux" / "tests" / "ark_files_test.c"

# the files outside the mode's own that call into it
HOOKED = [
    ROOT / "port" / "linux" / "src" / "launcher.c",
    ROOT / "port" / "linux" / "src" / "xbox_files.c",
    ROOT / "port" / "linux" / "src" / "steam_workshop.c",
    ROOT / "source" / "networking" / "network_client_manager.c",
    ROOT / "source" / "networking" / "network_server_manager.c",
]


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def cache_file(path: Path, version: int) -> None:
    """An MCC cache file's first bytes: 'head' (little-endian) and its version."""
    path.write_bytes(b"daeh" + struct.pack("<I", version) + bytes(0x100))


def ark_folder(path: Path, version: int = 13, textures: bool = True) -> Path:
    path.mkdir(parents=True)
    cache_file(path / "shared.map", version)
    if textures:
        (path / "textures.dat").write_bytes(bytes(16))
    return path


@pytest.fixture(scope="module")
def binary(tmp_path_factory):
    if sys.platform == "win32":
        pytest.skip("the test uses POSIX file helpers")
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    out = tmp_path_factory.mktemp("ark") / "ark_files_test"
    build = subprocess.run(
        [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1", "-D_GNU_SOURCE", "-D_FILE_OFFSET_BITS=64",
         "-DHALO_ARK", f"-I{SRC}", str(TEST), str(SRC / "ark_files.c"), str(SRC / "posix_files.c"),
         "-lpthread", "-o", str(out)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    return out


def found(binary, root: Path, install, expected: str) -> None:
    result = subprocess.run([str(binary), str(root), str(install) if install else "", expected],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_none(binary, tmp_path):
    """No files: nothing found, and so no mode."""
    root = tmp_path / "root"
    root.mkdir()
    found(binary, root, None, "none")


def test_data_root_folder(binary, tmp_path):
    """maps_ark beside maps (any case) with the files is found first."""
    root = tmp_path / "root"
    ark_folder(root / "Maps_Ark")
    install = tmp_path / "mcc"
    ark_folder(install / "game" / "maps")
    found(binary, root, install, "local")


def test_not_the_files(binary, tmp_path):
    """A maps_ark without textures.dat, or with another version's shared.map
    (MCC's Halo 1 maps are version 13 too, but have neither file), is not
    taken."""
    root = tmp_path / "root"
    ark_folder(root / "maps_ark", textures=False)
    found(binary, root, None, "none")
    other = tmp_path / "other"
    ark_folder(other / "maps_ark", version=5)
    found(binary, other, None, "none")


def test_mcc_install(binary, tmp_path):
    """No maps_ark: the folder in MCC's install that holds the files, a game's
    folder or one below it, read where it is."""
    root = tmp_path / "root"
    root.mkdir()
    install = tmp_path / "mcc"
    (install / "halo1" / "maps").mkdir(parents=True)
    cache_file(install / "halo1" / "maps" / "bloodgulch.map", 13)
    folder = ark_folder(install / "game" / "maps")
    found(binary, root, install, str(folder))
    flat = tmp_path / "flat"
    folder = ark_folder(flat / "game")
    found(binary, root, flat, str(folder))


def test_mcc_install_too_deep(binary, tmp_path):
    """Only two folders down: the install is not searched further."""
    root = tmp_path / "root"
    root.mkdir()
    install = tmp_path / "mcc"
    ark_folder(install / "game" / "maps" / "deeper")
    found(binary, root, install, "none")


def test_display_name_in_one_place():
    """The mode's name for players is spelled in ark_names.h alone."""
    name = re.search(r'#define ARK_MODE_DISPLAY_NAME "([^"]+)"', NAMES.read_text()).group(1)
    listed = subprocess.run(["git", "grep", "-l", "-F", name, "--", ".", ":!tools/test_ark_mode.py"],
                            cwd=ROOT, capture_output=True, text=True)
    if listed.returncode > 1:
        pytest.skip("not a git checkout")
    assert listed.stdout.split() == ["port/linux/include/ark_names.h"]


def test_hooks_behind_the_define():
    """Every line of the engine's and the setup screen's that names the mode is
    inside an #ifdef HALO_ARK block."""
    for path in HOOKED:
        depth, inside = 0, []
        for number, line in enumerate(path.read_text().splitlines(), 1):
            stripped = line.strip()
            if stripped.startswith("#if"):
                inside.append("HALO_ARK" in stripped and "!" not in stripped)
            elif stripped.startswith("#else") and inside:
                inside[-1] = False
            elif stripped.startswith("#endif") and inside:
                inside.pop()
            if re.search(r"\b(ark_mode_\w+|ark_files_\w+|ARK_\w+|steam_workshop_mcc_install|translate_ark)\b",
                         line) and not stripped.startswith(("/*", "*")):
                assert any(inside), f"{path.relative_to(ROOT)}:{number} names the mode outside HALO_ARK"
            depth = len(inside)
        assert depth == 0, f"{path.relative_to(ROOT)}: unbalanced #if"
