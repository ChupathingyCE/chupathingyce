"""The update channels (update.channel: release, testing, nightly;
port/linux/src/update_manifest.h): the versions' order, the testing and
nightly feeds' lists and their signatures, read by the game's own code
(tools/update_check.c, built here with test keys: HALO_UPDATE_TEST_KEY for
the release key, HALO_UPDATE_TEST_PRERELEASE_KEY for the prerelease key)
and by the Android updater's (UpdateManifest.java), and a list with the new
"channel" field read by the updater from before the channels.

The keys are made for these tests (OpenSSL 3's openssl); never a real key."""
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import delta_table  # noqa: E402
import port_settings  # noqa: E402
import update_sign  # noqa: E402
import version as version_tool  # noqa: E402
from test_update_manifest import MONOCYPHER, SOURCES, check, compiler, java_classes, java  # noqa: E402,F401

BASE = "0.9.1"
TESTING = "0.9.1-testing.202611101530"
NIGHTLY = "0.9.1-nightly.202611110400"


@pytest.fixture(scope="module")
def keys(tmp_path_factory):
    """the test release key, the test prerelease key, and one neither"""
    try:
        import os
        os.environ["OPENSSL"] = delta_table.openssl()
    except SystemExit:
        pytest.skip("needs OpenSSL 3's openssl")
    directory = tmp_path_factory.mktemp("channel_keys")
    return {name: (directory / f"{name}.pem", delta_table.keygen(directory / f"{name}.pem"))
            for name in ("release", "prerelease", "other")}


def build(cc, sources, defines, program, root=ROOT, include="port/linux/src"):
    objects = []
    for source in MONOCYPHER:
        target = program.parent / (Path(source).stem + ".o")
        if not target.exists():
            built = subprocess.run([cc, "-std=c11", "-O1", "-w", "-c", "-o", str(target), str(ROOT / source)],
                                   capture_output=True, text=True)
            assert built.returncode == 0, built.stderr
        objects.append(str(target))
    built = subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", *defines, f"-I{include}",
                            f"-I{ROOT / 'port/third_party/monocypher'}", "-o", str(program), *sources, *objects],
                           capture_output=True, text=True, cwd=root)
    assert built.returncode == 0, built.stderr
    return program


def key_define(key) -> str:
    return ",".join(f"0x{byte:02x}" for byte in key)


@pytest.fixture(scope="module")
def checker(keys, tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    program = tmp_path_factory.mktemp("channel_check") / "update_check"
    return build(cc, SOURCES, [f"-DHALO_UPDATE_TEST_KEY={key_define(keys['release'][1])}",
                               f"-DHALO_UPDATE_TEST_PRERELEASE_KEY={key_define(keys['prerelease'][1])}"], program)


@pytest.fixture(scope="module")
def old_checker(keys, tmp_path_factory):
    """the updater from before the channels (0.9.0's update_manifest.c,
    update_signature.c and update_check.c, from git), with the release key"""
    cc = compiler()
    if not cc or not shutil.which("git"):
        pytest.skip("no C compiler or git")
    old = tmp_path_factory.mktemp("old_updater")
    revision = "v0.9.0"
    for path in ("port/linux/src/update_manifest.c", "port/linux/src/update_manifest.h",
                 "port/linux/src/update_signature.c", "port/linux/src/update_signature.h",
                 "port/linux/src/update_key.h", "tools/update_check.c"):
        shown = subprocess.run(["git", "-C", str(ROOT), "show", f"{revision}:{path}"], capture_output=True)
        if shown.returncode != 0:
            pytest.skip(f"no {revision} in this checkout")
        (old / Path(path).name).write_bytes(shown.stdout)
    return build(cc, [str(old / name) for name in ("update_manifest.c", "update_signature.c", "update_check.c")],
                 [f"-DHALO_UPDATE_TEST_KEY={key_define(keys['release'][1])}"], old / "update_check", root=old,
                 include=str(old))


def builds(directory: Path) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    for index, name in enumerate(["chupathingyce-linux64-release.zip", "chupathingyce-windows64-release.zip",
                                  "chupathingyce-linux64-release.AppImage", "chupathingyce-android64-release.zip"]):
        (directory / name).write_bytes(bytes([index + 7]) * (2000 + index))
    return directory


def feed(keys, directory, version, channel, tag, key="prerelease"):
    files = sorted(path for path in builds(directory).iterdir() if path.suffix in (".zip", ".AppImage"))
    update_sign.manifest(keys[key][0], version, tag, f"{tag}-debug", "0" * 40, files, channel)
    return directory / "updates.json", directory / "updates.json.sig"


def listed(**fields):
    entry = {"platform": "linux64", "configuration": "release", "kind": "zip", "tag": "v0.9.1-testing",
             "name": "chupathingyce-linux64-release.zip", "size": 2000, "sha256": "cd" * 32}
    entry.update(fields.pop("entry", {}))
    document = {"format": "chupathingyce-updates 1", "version": TESTING, "channel": "testing", "commit": "",
                "files": [entry]}
    document.update(fields)
    return json.dumps(document)


# ---------- the order


ORDER = [
    # (a, b, a against b)
    ("0.9.1-testing.202611101200", "0.9.1", "older"),
    ("0.9.1", "0.9.1-testing.202611101200", "newer"),
    ("0.9.2-testing.202611200900", "0.9.1", "newer"),
    ("0.9.1", "0.9.2-testing.202611200900", "older"),
    # (prereleases of one version by their serial, whatever their label)
    ("0.9.1-nightly.202611100400", "0.9.1-testing.202611101200", "older"),
    ("0.9.1-testing.202611101200", "0.9.1-nightly.202611110400", "older"),
    ("0.9.1-nightly.202612010400", "0.9.1-testing.202611300000", "newer"),
    ("0.9.1-testing.202611101200", "0.9.1-nightly.202611101200", "same"),
    ("0.9.1-testing.202611101200", "0.9.1-testing.202611101200", "same"),
    # (a release beats every prerelease of its version, however late)
    ("0.9.1", "0.9.1-nightly.209912312359", "newer"),
    # (the hotfix letters, as before the channels)
    ("0.9.0d", "0.9.0c", "newer"),
    ("0.9.0", "0.9.0a", "older"),
    ("0.9.0d", "0.9.1-nightly.202611100400", "older"),
    ("0.9.0d-testing.202611100400", "0.9.0d", "older"),
    ("0.9.0d-testing.202611100400", "0.9.0c", "newer"),
    ("v0.9.1", "0.9.1", "same"),
    ("0.10.0", "0.9.9", "newer"),
    ("999999999.0.0", "0.9.9", "newer"),
    # (what is not a version is never newer, nor older: one grammar, the
    # serial exactly 12 digits and a time, only the two labels)
    ("0.9.1-dev", "0.9.0", "not versions"),
    ("0.9.1-testing", "0.9.0", "not versions"),
    ("0.9.1-testing.5", "0.9.0", "not versions"),
    ("0.9.1-nightly.500", "0.9.0", "not versions"),
    ("0.9.1-testing.2026111012000", "0.9.0", "not versions"),
    ("0.9.1-testing.20261110120", "0.9.0", "not versions"),
    ("0.9.1-testing.202511101200", "0.9.0", "not versions"),
    ("0.9.1-testing.210011101200", "0.9.0", "not versions"),
    ("0.9.1-testing.202613101200", "0.9.0", "not versions"),
    ("0.9.1-testing.202611001200", "0.9.0", "not versions"),
    ("0.9.1-testing.202611322400", "0.9.0", "not versions"),
    ("0.9.1-testing.202611102400", "0.9.0", "not versions"),
    ("0.9.1-testing.202611101260", "0.9.0", "not versions"),
    ("0.9.1-custom.202611101200", "0.9.0", "not versions"),
    ("0.9.1-testing.x", "0.9.0", "not versions"),
    ("0.9.1-Testing.202611101200", "0.9.0", "not versions"),
    ("0.9.1-testing.202611101200.6", "0.9.0", "not versions"),
    ("0.9.1+meta", "0.9.0", "not versions"),
    ("0.9", "0.8.0", "not versions"),
    ("1234567890.0.0", "0.9.0", "not versions"),
    ("0.9.1abcdefghijklmnop", "0.9.0", "not versions"),
    ("", "0.9.0", "not versions"),
]


@pytest.mark.parametrize("a, b, answer", ORDER)
def test_version_order(checker, a, b, answer):
    assert check(checker, "compare", a, b) == answer


@pytest.mark.parametrize("a, b, answer", ORDER)
def test_android_version_order(java_classes, a, b, answer):
    assert java(java_classes, "compare", a, b) == answer


def test_never_a_downgrade(checker):
    """from each build, only what is newer: going back to Release from a test
    build waits for the next release, and a nightly never steps back to an
    older test build"""
    newest = {"0.9.1-testing.202611101530": ["0.9.1"], "0.9.1-nightly.202611110400": ["0.9.1"],
              "0.9.1": ["0.9.2-testing.202611200900", "0.9.2"]}
    every = ["0.9.0d", "0.9.1-nightly.202611100400", "0.9.1-testing.202611101530", "0.9.1-nightly.202611110400",
             "0.9.1", "0.9.2-testing.202611200900", "0.9.2"]
    for installed, expected in newest.items():
        offered = [candidate for candidate in every if check(checker, "compare", candidate, installed) == "newer"]
        assert expected[0] in offered and all(every.index(c) > every.index(installed) for c in offered), installed


# ---------- the feeds' lists


def test_feed_written_signed_and_read(keys, checker, tmp_path):
    path, signature = feed(keys, tmp_path / "testing", TESTING, "testing", "v0.9.1-testing")
    document = json.loads(path.read_text())
    assert list(document)[:3] == ["format", "version", "channel"] and document["channel"] == "testing"
    assert check(checker, "feed", path, signature) == "signed"
    assert check(checker, "about", path, "testing") == f"version {TESTING}"
    answer = check(checker, "channel", path, "testing", TESTING, "linux64", "release", "zip")
    assert answer.startswith("found v0.9.1-testing chupathingyce-linux64-release.zip 2000 ")
    assert check(checker, "channel", path, "testing", TESTING, "linux64", "release", "appimage").startswith("found ")
    assert check(checker, "channel", path, "testing", TESTING, "freebsd64", "release", "zip").startswith("missing:")
    # (a feed's list is not a release's, though it is signed)
    assert check(checker, "find", path, TESTING, "linux64", "release", "zip").startswith("refused:")
    # (nor the other channel's)
    assert check(checker, "about", path, "nightly").startswith("refused:")


def test_nightly_feed(keys, checker, tmp_path):
    path, signature = feed(keys, tmp_path / "nightly", NIGHTLY, "nightly", "nightly")
    assert check(checker, "feed", path, signature) == "signed"
    assert check(checker, "about", path, "nightly") == f"version {NIGHTLY}"
    assert check(checker, "channel", path, "nightly", NIGHTLY, "windows64", "release", "zip").startswith(
        "found nightly chupathingyce-windows64-release.zip ")
    assert check(checker, "about", path, "testing").startswith("refused:")


def test_feed_keys(keys, checker, tmp_path):
    """the prerelease key signs a feed's list and nothing on the release
    channel; a release key signs both; another key neither"""
    by_prerelease, prerelease_signature = feed(keys, tmp_path / "a", TESTING, "testing", "v0.9.1-testing")
    assert check(checker, "feed", by_prerelease, prerelease_signature) == "signed"
    assert check(checker, "document", by_prerelease, prerelease_signature).startswith("not signed:")
    by_release, release_signature = feed(keys, tmp_path / "b", TESTING, "testing", "v0.9.1-testing", key="release")
    assert check(checker, "feed", by_release, release_signature) == "signed"
    assert check(checker, "document", by_release, release_signature) == "signed"
    by_other, other_signature = feed(keys, tmp_path / "c", TESTING, "testing", "v0.9.1-testing", key="other")
    assert check(checker, "feed", by_other, other_signature).startswith("not signed:")
    # (a release's list signed by the prerelease key is no release's: the
    # release path checks the release keys alone)
    release_list, release_list_signature = feed(keys, tmp_path / "d", BASE, "release", f"v{BASE}", key="prerelease")
    assert check(checker, "document", release_list, release_list_signature).startswith("not signed:")
    # (a changed list, or no signature at all)
    changed = tmp_path / "changed.json"
    changed.write_bytes(by_prerelease.read_bytes().replace(b'"size": 2000', b'"size": 2001'))
    assert check(checker, "feed", changed, prerelease_signature).startswith("not signed:")
    empty = tmp_path / "empty.sig"
    empty.write_text("")
    assert check(checker, "feed", by_prerelease, empty).startswith("not signed:")


def test_feed_never_offers_a_release(keys, checker, tmp_path):
    """a feed's list whose version is a release's (or another channel's) is
    not used, so the prerelease key cannot put a release before anyone"""
    for version, channel in ((BASE, "testing"), ("9.9.9", "testing"), (NIGHTLY, "testing"), (TESTING, "nightly")):
        path, signature = feed(keys, tmp_path / f"{version}-{channel}", version, channel,
                               "nightly" if channel == "nightly" else "v0.9.1-testing")
        assert check(checker, "feed", path, signature) == "signed"
        assert check(checker, "about", path, channel).startswith("refused:"), (version, channel)


SHAPES = [
    (listed(), "found"),
    (listed(channel="release"), "refused"),
    (listed(channel="nightly"), "refused"),
    (listed(channel="testing!"), "refused"),
    ({k: v for k, v in json.loads(listed()).items() if k != "channel"}, "refused"),
    (listed(version=BASE), "refused"),
    (listed(version="0.9.1-testing"), "refused"),
    (listed(version="0.9.2-testing.1"), "refused"),
    (listed(entry={"tag": "v0.9.1"}), "refused"),
    (listed(entry={"tag": "nightly"}), "refused"),
    (listed(entry={"tag": "../v0.9.1-testing"}), "refused"),
    (listed(entry={"tag": "v0.9.1-testing-debug"}), "found"),
    (listed(entry={"tag": "v0.9.1-testing2"}), "found"),
    (listed(entry={"tag": "v0.9.1-testing2-debug"}), "found"),
    (listed(entry={"tag": "nightly-testing"}), "refused"),
    (listed(entry={"tag": "v0.9.0-testing"}), "refused"),
    (listed(entry={"tag": "v0.9.1a-testing"}), "refused"),
    (listed(entry={"tag": "v0.9.1-testing-extra"}), "refused"),
    (listed(entry={"tag": "v0.9.1-testingx"}), "refused"),
    (listed(entry={"tag": "xv0.9.1-testing"}), "refused"),
    (listed(entry={"name": "../chupathingy"}), "refused"),
    (listed(entry={"size": -1}), "refused"),
    (listed(entry={"sha256": "cd" * 31}), "refused"),
    (listed(files=[]), "missing"),
    (listed() + "x", "refused"),
]


def shape_text(text):
    return text if isinstance(text, str) else json.dumps(text)


@pytest.mark.parametrize("text, answer", SHAPES)
def test_feed_shapes(checker, tmp_path, text, answer):
    path = tmp_path / "updates.json"
    path.write_text(shape_text(text))
    assert check(checker, "channel", path, "testing", TESTING, "linux64", "release", "zip").split(" ")[0].rstrip(
        ":") == answer


@pytest.mark.parametrize("index", range(len(SHAPES)))
def test_android_and_desktop_agree_on_feeds(checker, java_classes, tmp_path, index):
    text, answer = SHAPES[index]
    path = tmp_path / "updates.json"
    path.write_text(shape_text(text))
    assert java(java_classes, "channel", path, "testing", TESTING, "linux64", "release", "zip").split(" ")[0] == answer


def test_nightly_tags(checker, java_classes, tmp_path):
    path = tmp_path / "updates.json"
    for tag, answer in (("nightly", "found"), ("nightly-debug", "found"), ("v0.9.1-testing", "refused"),
                        ("v0.9.1", "refused"), ("nightly-testing", "refused"), ("nightly-2", "refused")):
        path.write_text(listed(version=NIGHTLY, channel="nightly", entry={"tag": tag}))
        assert check(checker, "channel", path, "nightly", NIGHTLY, "linux64", "release", "zip").split(" ")[0].rstrip(
            ":") == answer, tag
        assert java(java_classes, "channel", path, "nightly", NIGHTLY, "linux64", "release", "zip").split(" ")[0] == \
            answer, tag


# ---------- the release channel, old and new updaters


def test_release_list_with_a_channel(keys, checker, old_checker, tmp_path):
    """a release's list from 0.9.1 on says "channel": "release": the updater
    from before the channels reads it exactly as before, and so does this one"""
    path, signature = feed(keys, tmp_path / "release", BASE, "release", f"v{BASE}", key="release")
    assert json.loads(path.read_text())["channel"] == "release"
    for program in (old_checker, checker):
        assert check(program, "document", path, signature) == "signed"
        assert check(program, "find", path, BASE, "linux64", "release", "zip").startswith(
            f"found v{BASE} chupathingyce-linux64-release.zip 2000 ")
    # (and a list from before the channels, without the field, still reads)
    path.write_text(path.read_text().replace('  "channel": "release",\n', ""))
    assert "channel" not in json.loads(path.read_text())
    assert check(checker, "find", path, BASE, "linux64", "release", "zip").startswith(f"found v{BASE} ")


def test_old_updater_never_takes_a_feed(keys, old_checker, tmp_path):
    """the updater from before the channels never reads a feed (it asks for
    the latest release alone), and a feed's list is never one it would take
    for a release: its version is no release's, and the prerelease key is
    not one it trusts"""
    path, signature = feed(keys, tmp_path / "testing", TESTING, "testing", "v0.9.1-testing")
    assert check(old_checker, "document", path, signature).startswith("not signed:")
    assert check(old_checker, "find", path, BASE, "linux64", "release", "zip").startswith("refused:")


# ---------- the builds and the settings


@pytest.mark.parametrize("version, release, channel", [
    ("0.9.1", "1", ""), ("0.9.1-testing.202611101530", "0", "testing"), ("0.9.0d-nightly.202611100400", "0", "nightly"),
    ("0.9.1-nightly.42", "0", ""), ("0.9.1-dev", "0", ""), ("0.9.1-testing", "0", ""),
    ("0.9.1-testing.202611101530", "1", ""), ("0.9.1-custom.202611101530", "0", ""),
    ("0.9.1-testing.202613101530", "0", "")])
def test_build_update_channel(monkeypatch, version, release, channel):
    """only the nightly and test workflows' builds look on their channels
    (HALO_UPDATE_CHANNEL), and port_config.c's default follows it"""
    monkeypatch.setenv("HALO_VERSION", version)
    monkeypatch.setenv("HALO_RELEASE_BUILD", release)
    assert version_tool.update_channel() == channel
    defines = version_tool.identity_defines()
    assert (f'-DHALO_UPDATE_CHANNEL=\\"{channel}\\"' in defines) == bool(channel)
    assert "port_config.c" in version_tool.VERSION_SOURCES


def test_the_channel_setting():
    config = (ROOT / "port/linux/src/port_config.c").read_text()
    assert '{ "update.channel", _config_string, UPDATE_CHANNEL_DEFAULT,' in config
    assert '{ "update.channel", { "release", "testing", "nightly", NULL }' in config
    assert '#define UPDATE_CHANNEL_DEFAULT "\\"release\\""' in config
    update_h = (ROOT / "port/linux/src/update.h").read_text()
    assert f'#define UPDATE_CHANNEL_HELP "{port_settings.UPDATE_CHANNEL_HELP}"' in update_h
    rows = (ROOT / "port/linux/src/settings_rows.c").read_text()
    assert '"CHANNEL", "update.channel", _kind_words' in rows
    menus = (ROOT / "port/assets/menus/ce/main_menu.settings_select.player_setup.player_profile_edit."
             "network_setup.xml").read_text()
    assert 'setting="update.channel" values="release|testing|nightly"' in menus
    # (the Android app's prerelease key is the desktop's)
    android = (ROOT / "port/android/app/src/main/java/com/halo/decomp/UpdateManifest.java").read_text()
    header = (ROOT / "port/linux/src/update_prerelease_key.h").read_text()
    pinned = android[android.index("PRERELEASE_KEYS"):].split("};", 1)[0]
    android_keys = re.findall(r'"([0-9a-f]{64})"', pinned)
    header_keys = [bytes(int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", block)).hex()
                   for block in re.findall(r"\{([^{}]*)\}", header.split("#elif defined(HALO_UPDATE_TEST_PRERELEASE_KEY)")[0]
                                           .split("#elif !defined(HALO_UPDATE_TEST_KEY)", 1)[1])
                   if len(re.findall(r"0x([0-9a-fA-F]{2})", block)) == 32]
    assert android_keys == header_keys
    # (never a release key pinned as the prerelease key)
    assert not set(header_keys) & {key.hex() for key in update_sign.header_keys()}


# ---------- the release path, and the checks the review asked for


@pytest.mark.parametrize("version, answer", [
    ("0.9.1", "release"), ("0.9.0d", "release"), ("0.9.1-testing.202611101530", "not a release"),
    ("0.9.1-nightly.202611110400", "not a release"), ("0.9.1-testing.5", "not a release"),
    ("0.9.1-nightly", "not a release")])
def test_release_path_versions(checker, java_classes, version, answer):
    """the latest release's tag, its list and its zip's .sig are a release's
    only: a test build's or a nightly's version (of any shape) never is"""
    assert check(checker, "release", version) == answer
    assert java(java_classes, "release", version) == answer


def test_release_list_of_a_prerelease_version_refused(keys, checker, tmp_path):
    """a release-key-signed list whose version is a test build's is not taken
    on the release path"""
    path, signature = feed(keys, tmp_path / "r", TESTING, "release", "v0.9.1-testing", key="release")
    assert check(checker, "document", path, signature) == "signed"
    assert check(checker, "find", path, TESTING, "linux64", "release", "zip").startswith("refused:")


@pytest.fixture(scope="module")
def zero_checker(keys, tmp_path_factory):
    """a build whose prerelease key is all zero (none made yet), as today's"""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    program = tmp_path_factory.mktemp("zero_check") / "update_check"
    return build(cc, SOURCES, [f"-DHALO_UPDATE_TEST_KEY={key_define(keys['release'][1])}"], program)


def test_zero_prerelease_key_is_none(keys, zero_checker, tmp_path):
    """an all-zero prerelease key is no key: a feed signed by another key
    (the stand-in prerelease key here) is refused; one signed by the release
    key, the interim, is taken"""
    by_other, other_signature = feed(keys, tmp_path / "a", TESTING, "testing", "v0.9.1-testing")
    assert check(zero_checker, "feed", by_other, other_signature).startswith("not signed:")
    by_release, release_signature = feed(keys, tmp_path / "b", TESTING, "testing", "v0.9.1-testing", key="release")
    assert check(zero_checker, "feed", by_release, release_signature) == "signed"


def test_the_shipped_prerelease_key_is_zero_or_pinned():
    """until the key is made, update_prerelease_key.h's key is all zero, and
    Android's too (UpdateManifest's filter skips it: UpdateManifestTest)"""
    header = (ROOT / "port/linux/src/update_prerelease_key.h").read_text()
    shipped = header.split("#elif !defined(HALO_UPDATE_TEST_KEY)", 1)[1].split("#elif", 1)[0]
    values = re.findall(r"0x([0-9a-fA-F]{2})", shipped)
    assert len(values) % 32 == 0 and values


def test_grammar_is_the_same_everywhere():
    """C (update_manifest.c), Java (UpdateManifest.VERSION), gradle and
    tools/version.py share one grammar"""
    java_text = (ROOT / "port/android/app/src/main/java/com/halo/decomp/UpdateManifest.java").read_text()
    assert '"v?(\\\\d{1,9})\\\\.(\\\\d{1,9})\\\\.(\\\\d{1,9})([a-z]{0,15})(?:-(testing|nightly)\\\\.(\\\\d{12}))?"' in java_text
    gradle = (ROOT / "port/android/app/build.gradle").read_text()
    assert r"/^\d{1,9}\.\d{1,9}\.\d{1,9}[a-z]{0,15}-(testing|nightly)\.((\d{4})(\d{2})(\d{2})(\d{2})(\d{2}))$/" in gradle
    assert version_tool.PRERELEASE_VERSION.pattern == \
        r"^\d{1,9}\.\d{1,9}\.\d{1,9}[a-z]{0,15}-(testing|nightly)\.((\d{4})(\d{2})(\d{2})(\d{2})(\d{2}))$"
