"""The release packages' guard (tools/package_check.py, #2029).

v0.9.2a's 64-bit Windows and Linux zips held a config.toml: the bgfx shader
check ran the game in the dist/ folder, and the game writes its settings
beside itself there. The guard fails a folder, zip, tarball or AppImage
holding a player's own file; the Build workflow runs it on the packages,
tools/ci_build.py on each dist/ folder, tools/linux_appimage.py on the
AppDir; and the shader check runs a copy of the build.
"""
import sys
import tarfile
import zipfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools import bgfx_shader_check, package_check  # noqa: E402

RELEASE = ["chupathingy", "chupathingy.exe", "SDL3.dll", "libSDL3.so.0", "brokers.txt", "LICENSE.txt",
           "THIRD-PARTY-NOTICES.txt", "README.txt", "blocked_names.txt", "playlists/slayer.txt",
           "ChupathingyCE.app/Contents/MacOS/chupathingy", "ChupathingyCE.app/Contents/Info.plist",
           "chupathingyce-macos-release/ChupathingyCE.app/Contents/Resources/icon.icns",
           "chupathingyce-android64-release.apk", "usr/bin/chupathingy", "AppRun"]
PLAYERS = ["config.toml", "Config.TOML", "config.toml.old", "config.toml.new", "debug.txt", "halo.log",
           "logs/debug.txt", "u/000000000000/save.dat", "cache/x.bin", "private/key", "screenshots/a.png",
           "update.partial/chupathingy", "chupathingy.old", "crashes/1.dmp", "maps/bloodgulch.map",
           "usr/bin/config.toml", "chupathingyce-macos-release/ChupathingyCE.app/Contents/MacOS/config.toml"]


@pytest.mark.parametrize("name", RELEASE)
def test_a_release_s_files_pass(name):
    assert not package_check.player_file(name)


@pytest.mark.parametrize("name", PLAYERS)
def test_a_player_s_files_do_not(name):
    assert package_check.player_file(name)


def zip_of(path, names):
    with zipfile.ZipFile(path, "w") as archive:
        for name in names:
            archive.writestr(name, b"x")
    return path


def test_the_v092a_zip_fails_and_v092_passes(tmp_path, capsys):
    bad = zip_of(tmp_path / "chupathingyce-windows64-release.zip",
                 ["brokers.txt", "chupathingy.exe", "config.toml", "LICENSE.txt", "SDL3.dll",
                  "THIRD-PARTY-NOTICES.txt"])
    good = zip_of(tmp_path / "chupathingyce-windows-release.zip",
                  ["brokers.txt", "chupathingy.exe", "LICENSE.txt", "SDL3.dll", "THIRD-PARTY-NOTICES.txt"])
    assert package_check.main([str(good)]) == 0
    assert package_check.main([str(bad)]) == 1
    assert "config.toml" in capsys.readouterr().err


def test_folders_and_tarballs(tmp_path):
    dist = tmp_path / "dist"
    (dist / "ChupathingyCE.app/Contents/MacOS").mkdir(parents=True)
    (dist / "ChupathingyCE.app/Contents/MacOS/chupathingy").write_bytes(b"x")
    assert package_check.check([dist]) == []
    tar = tmp_path / "chupathingyce-macos-release.tar"
    with tarfile.open(tar, "w") as archive:
        archive.add(dist, arcname="chupathingyce-macos-release")
    assert package_check.check([tar]) == []
    (dist / "ChupathingyCE.app/Contents/MacOS/config.toml").write_text("config_version = 2\n")
    assert len(package_check.check([dist])) == 1
    with tarfile.open(tar, "w") as archive:
        archive.add(dist, arcname="chupathingyce-macos-release")
    assert len(package_check.check([tar])) == 1


def test_not_a_package_is_an_error(tmp_path):
    other = tmp_path / "notes.txt"
    other.write_text("x")
    assert package_check.main([str(other)]) == 1


def test_appimage_offset_is_the_end_of_its_runtime(tmp_path):
    import struct
    header = bytearray(64)
    header[:4] = b"\x7fELF"
    header[4], header[5] = 2, 1
    struct.pack_into("<Q", header, 0x28, 1000)
    struct.pack_into("<HH", header, 0x3A, 64, 3)
    path = tmp_path / "game.AppImage"
    path.write_bytes(bytes(header))
    assert package_check.appimage_offset(path) == 1000 + 64 * 3


def test_the_shader_check_runs_a_copy(tmp_path):
    dist = tmp_path / "dist/chupathingyce-linux64-release"
    dist.mkdir(parents=True)
    (dist / "chupathingy").write_bytes(b"game")
    (dist / "libSDL3.so.0").write_bytes(b"sdl")
    copy = bgfx_shader_check.game_copy(dist / "chupathingy", tmp_path / "scratch")
    assert copy != dist / "chupathingy" and copy.read_bytes() == b"game"
    assert (copy.parent / "libSDL3.so.0").is_file()
    app = tmp_path / "dist/chupathingyce-macos-release/ChupathingyCE.app/Contents/MacOS"
    app.mkdir(parents=True)
    (app / "chupathingy").write_bytes(b"mac")
    (app.parent / "Info.plist").write_text("plist")
    copy = bgfx_shader_check.game_copy(app / "chupathingy", tmp_path / "scratch2")
    assert copy.parts[-4:] == ("ChupathingyCE.app", "Contents", "MacOS", "chupathingy")
    assert (copy.parent.parent / "Info.plist").is_file()


def test_the_builds_run_the_guard():
    workflow = (ROOT / ".github/workflows/build.yml").read_text()
    for package in ("dist/chupathingyce-linux64-release", "dist/chupathingyce-windows64-release",
                    "chupathingyce-macos-release.tar"):
        line = next(line for line in workflow.splitlines() if "tools/package_check.py" in line and package in line)
        assert line
    # (after the shader check, before the upload, in each job that runs the game)
    for shader, guard in (("--binary dist/chupathingyce-linux64-release/", "dist/chupathingyce-linux64-release"),
                          ("--binary dist/chupathingyce-windows64-release/", "dist/chupathingyce-windows64-release")):
        at = workflow.index(shader)
        assert workflow.index("tools/package_check.py", at) < workflow.index("name: chupathingyce-", at)
    ci_build = (ROOT / "tools/ci_build.py").read_text()
    assert ci_build.count("return no_player_files(dist)") == 2
    assert "package_check.check([app])" in (ROOT / "tools/linux_appimage.py").read_text()
