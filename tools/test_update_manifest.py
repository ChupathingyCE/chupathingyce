"""The self-updater's list of update files and its signatures
(port/linux/src/update_manifest.c, update_signature.c): a list written and
signed by tools/update_sign.py is read by the game's own code, built here
with a test key (tools/update_check.c, HALO_UPDATE_TEST_KEY), and a list that
is changed, signed by another key, another release's, or not the list's
shape is turned away. The zips' own signatures (the path before the list,
for the releases without one) still check as they did.

The key is made for these tests (OpenSSL 3's openssl, as tools/delta_table.py
finds it); without one, the signing tests are skipped."""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import delta_table  # noqa: E402
import update_sign  # noqa: E402

SOURCES = ["port/linux/src/update_manifest.c", "port/linux/src/update_signature.c", "tools/update_check.c"]
MONOCYPHER = ["port/third_party/monocypher/monocypher.c", "port/third_party/monocypher/monocypher-ed25519.c"]
VERSION = "0.8.0"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


@pytest.fixture(scope="module")
def keys(tmp_path_factory):
    """two test key pairs (never a real key): the build's, and another"""
    try:
        os.environ["OPENSSL"] = delta_table.openssl()
    except SystemExit:
        pytest.skip("needs OpenSSL 3's openssl")
    directory = tmp_path_factory.mktemp("update_key")
    return [(directory / f"{name}.pem", delta_table.keygen(directory / f"{name}.pem")) for name in ("key", "other")]


@pytest.fixture(scope="module")
def checker(keys, tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    key = ",".join(f"0x{byte:02x}" for byte in keys[0][1])
    program = tmp_path_factory.mktemp("update_check") / "update_check"
    objects = []
    for source in MONOCYPHER:
        target = program.parent / (Path(source).stem + ".o")
        built = subprocess.run([cc, "-std=c11", "-O1", "-w", "-c", "-o", str(target), source], capture_output=True,
                               text=True, cwd=ROOT)
        assert built.returncode == 0, built.stderr
        objects.append(str(target))
    built = subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", f"-DHALO_UPDATE_TEST_KEY={key}",
                            "-Iport/linux/src", "-Iport/third_party/monocypher", "-o", str(program), *SOURCES,
                            *objects], capture_output=True, text=True, cwd=ROOT)
    assert built.returncode == 0, built.stderr
    return program


def check(program, *arguments):
    result = subprocess.run([str(program), *map(str, arguments)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout.strip()


def release(directory):
    """a release's files (small, made here): the game's zips, a server's"""
    directory.mkdir(parents=True, exist_ok=True)
    names = ["chupathingyce-linux64-release.zip", "chupathingyce-linux64-debug.zip",
             "chupathingyce-windows64-release.zip", "chupathingyce-android-release.zip",
             "chupathingyce-linux64-release.AppImage", "chupathingyce-server-linux-x64.zip"]
    for index, name in enumerate(names):
        (directory / name).write_bytes(bytes([index]) * (1000 + index * 77))
    return directory


def write_list(keys, directory, key_index=0, version=VERSION):
    files = sorted(path for path in directory.iterdir() if path.suffix in (".zip", ".AppImage"))
    update_sign.manifest(keys[key_index][0], version, f"v{version}", f"v{version}-debug", "0" * 40, files)
    return directory / "updates.json", directory / "updates.json.sig"


def find(checker, path, platform="linux64", configuration="release", kind="zip", version=VERSION):
    return check(checker, "find", path, version, platform, configuration, kind)


def test_list_written_signed_and_read(keys, checker, tmp_path):
    directory = release(tmp_path / "release")
    listed, signature = write_list(keys, directory)
    assert check(checker, "document", listed, signature) == "signed"
    document = json.loads(listed.read_text())
    assert document["format"] == "chupathingyce-updates 1" and document["version"] == VERSION
    # (the server's zip is not an update file)
    assert sorted(entry["name"] for entry in document["files"]) == sorted(
        path.name for path in directory.iterdir() if path.name.startswith("chupathingyce-")
        and "server" not in path.name and not path.name.startswith("updates"))
    for name, platform, configuration, kind, tag in (
            ("chupathingyce-linux64-release.zip", "linux64", "release", "zip", f"v{VERSION}"),
            ("chupathingyce-linux64-debug.zip", "linux64", "debug", "zip", f"v{VERSION}-debug"),
            ("chupathingyce-windows64-release.zip", "windows64", "release", "zip", f"v{VERSION}"),
            ("chupathingyce-linux64-release.AppImage", "linux64", "release", "appimage", f"v{VERSION}")):
        data = (directory / name).read_bytes()
        assert find(checker, listed, platform, configuration, kind) == \
            f"found {tag} {name} {len(data)} {hashlib.sha256(data).hexdigest()}"


def test_list_missing_a_file(keys, checker, tmp_path):
    """no file for this platform: the zip by its old name (the updater's)"""
    listed, _ = write_list(keys, release(tmp_path / "release"))
    assert find(checker, listed, "windows", "release").startswith("missing: ")
    assert find(checker, listed, "android", "debug").startswith("missing: ")
    assert find(checker, listed, "linux64", "release", "deb").startswith("missing: ")


def test_list_changed_or_signed_by_another_key(keys, checker, tmp_path):
    directory = release(tmp_path / "release")
    listed, signature = write_list(keys, directory)
    changed = tmp_path / "changed.json"
    changed.write_bytes(listed.read_bytes().replace(b'"size": 1000', b'"size": 1001'))
    assert changed.read_bytes() != listed.read_bytes()
    assert check(checker, "document", changed, signature).startswith("not signed: ")
    other = release(tmp_path / "other")
    other_listed, other_signature = write_list(keys, other, key_index=1)
    assert check(checker, "document", other_listed, other_signature).startswith("not signed: ")
    bad = tmp_path / "bad.sig"
    bad.write_text("00" * 63 + "\n")
    assert check(checker, "document", listed, bad).startswith("not signed: ")


def test_list_of_another_release_refused(keys, checker, tmp_path):
    """a signed list is only its own release's (no older list replayed)"""
    listed, _ = write_list(keys, release(tmp_path / "release"), version="0.7.9")
    assert find(checker, listed, version=VERSION).startswith("refused: ")
    assert find(checker, listed, version="0.7.9").startswith("found v0.7.9 ")


def document(**changes):
    entry = {"platform": "linux64", "configuration": "release", "kind": "zip", "tag": f"v{VERSION}",
             "name": "chupathingyce-linux64-release.zip", "size": 1000, "sha256": "ab" * 32}
    entry.update(changes.pop("entry", {}))
    value = {"format": "chupathingyce-updates 1", "version": VERSION, "commit": "", "files": [entry]}
    value.update(changes)
    return json.dumps(value)


SHAPES = [
    (document(), "found"),
    # (a later list's fields and kinds are passed over)
    (document(later={"nested": [1, 2.5e3, None, True, "x\\u00e9"]}, entry={"extra": {"a": [1]}}), "found"),
    (document(files=[{"platform": "linux64", "configuration": "release", "kind": "appimage", "name": "x"}]),
     "missing"),
    (document(format="chupathingyce-updates 2"), "refused"),
    (document(version="0.8.1"), "refused"),
    (document(entry={"tag": "v0.7.0"}), "refused"),
    (document(entry={"tag": f"v{VERSION}x"}), "refused"),
    (document(entry={"name": "../halo"}), "refused"),
    (document(entry={"name": "a/b.zip"}), "refused"),
    (document(entry={"name": "chupathingyce\\u002flinux.zip"}), "refused"),
    (document(entry={"size": -1}), "refused"),
    (document(entry={"size": 1.5}), "refused"),
    (document(entry={"size": "1000"}), "refused"),
    (document(entry={"sha256": "ab" * 31}), "refused"),
    (document(entry={"sha256": "zz" * 32}), "refused"),
    (document(files=[json.loads(document())["files"][0]] * 2), "refused"),
    (document()[:-1], "refused"),
    (document() + "x", "refused"),
    ("[" * 100 + "]" * 100, "refused"),
    ('{"later": ' + "[" * 100 + "]" * 100 + "}", "refused"),
    ("", "refused"),
]


@pytest.mark.parametrize("text, answer", SHAPES)
def test_list_shapes(checker, tmp_path, text, answer):
    path = tmp_path / "updates.json"
    path.write_text(text)
    assert find(checker, path).split(" ")[0].rstrip(":") == answer


def test_list_main(keys, checker, tmp_path):
    """a file's "main" (the executable started after the update): written for
    a zip with the game's later name, read when it is a name, left out
    otherwise; a list without one reads as before"""
    directory = tmp_path / "release"
    directory.mkdir()
    for name, members in (("chupathingyce-linux64-release.zip", ["chupathingy", "halo", "libSDL3.so.0"]),
                          ("chupathingyce-windows64-release.zip", ["halo.exe", "SDL3.dll"])):
        with zipfile.ZipFile(directory / name, "w") as archive:
            for member in members:
                archive.writestr(member, member)
    listed, signature = write_list(keys, directory)
    assert check(checker, "document", listed, signature) == "signed"
    assert find(checker, listed).endswith(" chupathingy")
    assert len(find(checker, listed, "windows64").split(" ")) == 5
    path = tmp_path / "updates.json"
    path.write_text(document(entry={"main": "chupathingy.exe"}))
    assert find(checker, path).split(" ")[5] == "chupathingy.exe"
    for main in ("../halo", "a/b"):
        path.write_text(document(entry={"main": main}))
        answer = find(checker, path)
        assert answer.startswith("found ") and len(answer.split(" ")) == 5, main
    for main in (7, ["x"]):
        path.write_text(document(entry={"main": main}))
        assert find(checker, path).startswith("refused"), main


def test_sha256(checker, tmp_path):
    """the updater's own SHA-256, across its block boundaries"""
    for size in (0, 1, 55, 56, 63, 64, 65, 119, 120, 127, 128, 1000, 65537):
        path = tmp_path / f"data{size}"
        data = bytes((index * 31 + size) & 255 for index in range(size))
        path.write_bytes(data)
        assert check(checker, "sha256", path) == hashlib.sha256(data).hexdigest(), size


def test_zip_signatures_as_before(keys, checker, tmp_path):
    """the path before the list: each zip's own .sig"""
    directory = release(tmp_path / "release")
    zip_path = directory / "chupathingyce-linux64-release.zip"
    update_sign.sign(keys[0][0], VERSION, [zip_path])
    signature = Path(f"{zip_path}.sig")
    assert check(checker, "zip", zip_path, zip_path.name, VERSION, signature) == "signed"
    assert check(checker, "zip", zip_path, zip_path.name, "0.8.1", signature).startswith("not signed: ")
    assert check(checker, "zip", zip_path, "chupathingyce-linux-release.zip", VERSION, signature).startswith(
        "not signed: ")


# ---------- the Android updater's reader (UpdateManifest.java)

ANDROID = ROOT / "port/android/app/src/main/java/com/halo/decomp/UpdateManifest.java"


@pytest.fixture(scope="module")
def java_classes(tmp_path_factory):
    if not shutil.which("javac") or not shutil.which("java"):
        pytest.skip("no Java")
    classes = tmp_path_factory.mktemp("update_manifest_java")
    built = subprocess.run(["javac", "-d", str(classes), str(ANDROID), str(ROOT / "tools/tests/UpdateManifestTest.java")],
                           capture_output=True, text=True)
    assert built.returncode == 0, built.stderr
    return classes


def java(classes, *arguments):
    result = subprocess.run(["java", "-cp", str(classes), "com.halo.decomp.UpdateManifestTest", *map(str, arguments)],
                            capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout.strip()


def test_android_reader(java_classes):
    assert java(java_classes) == "UpdateManifestTest: passed"


def test_android_keys_are_the_games(java_classes):
    """the Android updater's keys are update_key.h's"""
    listed = re.findall(r'"([0-9a-f]{64})"', ANDROID.read_text())
    assert listed == [key.hex() for key in update_sign.header_keys()]


@pytest.mark.parametrize("platform, configuration, kind", [
    ("linux64", "release", "zip"), ("linux64", "debug", "zip"), ("linux64", "release", "appimage"),
    ("android", "release", "zip"), ("windows", "release", "zip")])
def test_android_and_desktop_agree(keys, checker, java_classes, tmp_path, platform, configuration, kind):
    """both readers give the same answer on a list update_sign.py wrote"""
    listed, _ = write_list(keys, release(tmp_path / "release"))
    desktop = find(checker, listed, platform, configuration, kind)
    android = java(java_classes, "find", listed, VERSION, platform, configuration, kind)
    assert android == desktop.split(":")[0]


@pytest.mark.parametrize("index", range(len(SHAPES)))
def test_android_and_desktop_agree_on_shapes(checker, java_classes, tmp_path, index):
    text, answer = SHAPES[index]
    path = tmp_path / "updates.json"
    path.write_text(text)
    assert java(java_classes, "find", path, VERSION, "linux64", "release", "zip").split(" ")[0] == answer
