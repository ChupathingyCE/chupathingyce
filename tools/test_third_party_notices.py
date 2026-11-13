"""THIRD-PARTY-NOTICES.txt (tools/third_party_notices.py, from
port/notices/components.json): every library in port/third_party, every
bundled font and data set and every license file under port/ has an entry;
the generated file carries each entry's license texts in full; and the
packages carry it and LICENSE.txt, not the separate license files they once
did."""
import json
import re
from pathlib import Path

import pytest

from tools import third_party_notices as notices

ROOT = Path(__file__).resolve().parent.parent
# what the builds embed or ship beside the game that others made: the fonts,
# the prompt icons, the platform logos and the HUD art's notice
BUNDLED_DATA = ["port/assets/fonts/*.ttf", "port/linux/ui/fonts/*.ttf",
                "port/assets/prompts", "port/linux/ui/fonts/platform_icons", "port/assets/hud/NOTICE.md"]
LICENSE_FILE = re.compile(r"^(LICEN[CS]E|COPYING|COPYRIGHT|NOTICE)|OFL|CC0|-LICENSE", re.IGNORECASE)
ALL_FEATURES = list(notices.FEATURES)


def manifest():
    return json.loads((ROOT / "port/notices/components.json").read_text(encoding="utf-8"))


def covered():
    paths = set()
    for entry in manifest()["components"]:
        paths.update(entry["covers"])
        paths.update(entry.get("files", []))
    for text in manifest()["shared_texts"].values():
        paths.add(text["file"])
    return paths


def is_covered(path, paths):
    return any(path == entry or path.startswith(entry.rstrip("/") + "/") for entry in paths)


def normalized(text):
    return " ".join(text.split())


def test_every_third_party_library_has_an_entry():
    paths = covered()
    missing = [folder.relative_to(ROOT).as_posix() for folder in sorted((ROOT / "port/third_party").iterdir())
               if folder.is_dir() and not is_covered(folder.relative_to(ROOT).as_posix(), paths)]
    assert not missing, f"no entry in port/notices/components.json for {missing}"


def test_every_bundled_font_and_data_set_has_an_entry():
    paths = covered()
    found = [path.relative_to(ROOT).as_posix() for pattern in BUNDLED_DATA for path in sorted(ROOT.glob(pattern))]
    assert found, "the bundled data's paths have moved: update BUNDLED_DATA"
    missing = [path for path in found if not is_covered(path, paths)]
    assert not missing, f"no entry in port/notices/components.json for {missing}"


def test_every_license_file_under_port_has_an_entry():
    """a license file anywhere under port/ (a new vendored library's, a font's)
    is listed, so that its text reaches the downloads"""
    paths = covered()
    missing = []
    for path in sorted((ROOT / "port").rglob("*")):
        relative = path.relative_to(ROOT).as_posix()
        if (path.is_file() and LICENSE_FILE.search(path.name) and "/build" not in relative
                and not relative.startswith("port/android/app/build") and not is_covered(relative, paths)):
            missing.append(relative)
    assert not missing, f"license files with no entry in port/notices/components.json: {missing}"


def test_entries_are_complete():
    shared = manifest()["shared_texts"]
    names = set()
    for entry in manifest()["components"]:
        assert entry["name"] not in names, entry["name"]
        names.add(entry["name"])
        for field in ("use", "source", "copyright", "license", "packages", "covers"):
            assert entry.get(field) is not None, f"{entry['name']}: no {field}"
        assert entry["copyright"], entry["name"]
        assert set(entry["packages"]) <= set(notices.PACKAGES), entry["name"]
        assert entry.get("requires") in (None, *notices.FEATURES), entry["name"]
        # (a license text, or a credit for what needs none)
        assert entry.get("files") or entry.get("shared") or entry.get("credit"), entry["name"]
        assert entry.get("shared") in (None, *shared), entry["name"]
        for name in [*entry.get("files", []), *entry["covers"]]:
            assert (ROOT / name).exists(), f"{entry['name']}: {name} is not in the repository"


@pytest.mark.parametrize("package", notices.PACKAGES)
def test_generated_file_carries_every_license_text(package, tmp_path):
    output = notices.write(tmp_path / notices.NAME, package, ALL_FEATURES)
    text = normalized(output.read_text(encoding="utf-8"))
    entries = notices.components(package, ALL_FEATURES)
    assert entries
    for entry in entries:
        assert normalized(entry["name"]) in text
        for line in entry["copyright"]:
            assert normalized(line) in text, f"{entry['name']}: {line}"
        for name in entry.get("files", []):
            assert normalized(notices.read_text(name)) in text, f"{entry['name']}: {name} not carried in full"
        if entry.get("shared"):
            shared = manifest()["shared_texts"][entry["shared"]]["file"]
            assert normalized(notices.read_text(shared)) in text, f"{entry['name']}: {shared}"
    assert "https://github.com/ChupathingyCE/chupathingyce" in text
    assert "LICENSE.txt" in text


def test_fetched_licenses_match_ours():
    """a build's downloaded libraries (SDL3, bgfx's) whose license is in the
    build folder: the same text as port/notices' copy (nothing to compare
    without a build)"""
    assert notices.check_fetched() == []


def test_packages_and_features_choose_entries():
    game = {entry["name"] for entry in notices.components("game", [])}
    server = {entry["name"] for entry in notices.components("server", [])}
    desktop = {entry["name"] for entry in notices.components("game", ALL_FEATURES)}
    assert "Opus" in game and "Opus" not in server
    assert "SDL3" not in game and "SDL3" in desktop
    assert "SPIRV-Cross" in desktop and "glslang" in desktop and "glslang" not in game
    assert "Mbed TLS" in server and "Cyborg Interface Gothic" in server


@pytest.fixture
def fresh_commit():
    """(tools/version.py keeps the commit it found)"""
    notices.commit.cache_clear()
    yield
    notices.commit.cache_clear()


def test_appimage_and_vulkan_entries(tmp_path):
    """the AppImage's runtime only in the AppImage's notices (with the musl,
    Zstandard and zlib sections its libfuse entry points to); the Vulkan
    headers only with the Vulkan back end"""
    linux64 = ["sdl3", "bgfx", "vulkan"]
    names = {entry["name"] for entry in notices.components("game", linux64)}
    assert "Vulkan headers" in names and "AppImage runtime" not in names
    assert "Vulkan headers" not in {entry["name"] for entry in notices.components("game", ["sdl3", "bgfx"])}
    appimage = {entry["name"] for entry in notices.components("game", [*linux64, "appimage"])}
    for name in ("AppImage runtime", "libfuse (in the AppImage runtime)", "squashfuse (in the AppImage runtime)",
                 "mimalloc (in the AppImage runtime)", "musl", "Zstandard", "zlib"):
        assert name in appimage, name
    # (tools/linux_appimage.py reads the linux64 build's features back)
    written = notices.write(tmp_path / notices.NAME, "game", linux64)
    assert notices.features_in(written) == linux64
    notices.write(written, "game", [*notices.features_in(written), "appimage"])
    assert notices.features_in(written) == [*linux64, "appimage"]
    assert "GNU LESSER GENERAL PUBLIC LICENSE" in written.read_text(encoding="utf-8")


def test_header_names_the_source(monkeypatch, fresh_commit):
    monkeypatch.setenv("HALO_VERSION", "1.2.3")
    monkeypatch.setenv("HALO_RELEASE_BUILD", "1")
    monkeypatch.setenv("HALO_COMMIT", "0123abcd")
    text = notices.render("game", [])
    assert "https://github.com/ChupathingyCE/chupathingyce/tree/v1.2.3" in text
    monkeypatch.delenv("HALO_RELEASE_BUILD")
    monkeypatch.delenv("HALO_UPDATE_CHANNEL", raising=False)
    monkeypatch.setenv("HALO_VERSION", "1.2.3-nightly.4")
    assert "https://github.com/ChupathingyCE/chupathingyce/tree/0123abcd" in notices.render("game", [])


def test_packages_carry_only_the_two_files():
    """tools/ci_build.py copies no license file but LICENSE.txt and writes
    the notices; the Android builds put both in the APK"""
    ci_build = (ROOT / "tools/ci_build.py").read_text(encoding="utf-8")
    assert "third_party_notices" in ci_build
    copied = re.findall(r"""["']([^"'\s]*(?:LICEN[CS]E|COPYING|OFL|CC0)[^"'\s]*)["']""", ci_build)
    assert set(copied) <= {"LICENSE", "LICENSE.txt"}, copied
    for build in ("tools/android_build.py", "tools/android64_build.py"):
        text = (ROOT / build).read_text(encoding="utf-8")
        assert "ninja_notices(n, " in text, build
        assert "bcdec-LICENSE" not in text, build
