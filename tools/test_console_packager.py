"""Tests the console packager (tools/console-packager/chupathingy_package.py)
on a tiny disc image the test builds itself: a few made-up files in the
disc's layout, with a Halo title ID and Xbox map headers. No game data is
read or needed.

Covers each console's layout, the language pick, a disc that isn't Halo,
names on the disc or in a build zip that would write outside the package,
and the build checks.
"""
import hashlib
import os
import struct
import sys
import zipfile
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent / "console-packager"))
import chupathingy_package as packager  # noqa: E402

SECTOR = 2048


def fake_xbe(title_id=packager.HALO_TITLE_ID, name="Halo", region=7):
    image = bytearray(0x400)
    image[0:4] = b"XBEH"
    struct.pack_into("<I", image, 0x104, 0x10000)
    struct.pack_into("<I", image, 0x118, 0x10000 + 0x200)
    struct.pack_into("<I", image, 0x200 + 8, title_id)
    encoded = name.encode("utf-16-le")
    image[0x200 + 0x0C:0x200 + 0x0C + len(encoded)] = encoded
    struct.pack_into("<I", image, 0x200 + 0xA0, region)
    return bytes(image)


def fake_map(name, build="01.10.12.2276", version=5):
    header = bytearray(0x800)
    header[0:4] = b"daeh"
    struct.pack_into("<i", header, 4, version)
    header[0x20:0x20 + len(name)] = name.encode()
    header[0x40:0x40 + len(build)] = build.encode()
    header[0x7FC:0x800] = b"toof"
    return bytes(header) + name.encode() * 50


def disc_tree(languages=(), title_id=packager.HALO_TITLE_ID, build="01.10.12.2276"):
    def maps(tag):
        return {"ui.map": fake_map("ui" + tag, build), "bloodgulch.map": fake_map("bloodgulch" + tag, build),
                "loading.tga": b"TGA" + tag.encode()}
    tree = {"default.xbe": fake_xbe(title_id), "maps": maps(""),
            "bink": {"intro.bik": b"BIK" * 10}}
    for language in languages:
        tree["maps_" + language] = maps("_" + language)
    return tree


def build_xiso(tree, partition=0, loop=False):
    """An XDVDFS image of tree (name: bytes for a file, a dict for a folder).
    Each table chains its entries through their right subtrees."""
    image = bytearray()
    next_sector = [VOLUME_SECTOR_END]

    def allocate(size):
        sector = next_sector[0]
        next_sector[0] += max(1, (size + SECTOR - 1) // SECTOR)
        return sector

    def put(sector, data):
        end = sector * SECTOR + len(data)
        if len(image) < end:
            image.extend(b"\0" * (end - len(image)))
        image[sector * SECTOR:end] = data

    def place(node):
        if isinstance(node, bytes):
            sector = allocate(len(node))
            put(sector, node)
            return sector, len(node), 0
        names = list(node)
        sizes = [(14 + len(name.encode("latin-1")) + 3) & ~3 for name in names]
        table_size = sum(sizes)
        table_sector = allocate(table_size) if names else 0
        table = bytearray(b"\xff" * table_size)
        offset = 0
        for index, name in enumerate(names):
            sector, size, attributes = place(node[name])
            right = (offset + sizes[index]) // 4 if index + 1 < len(names) else 0
            encoded = name.encode("latin-1")
            struct.pack_into("<HHIIBB", table, offset, 0, right, sector, size, attributes, len(encoded))
            table[offset + 14:offset + 14 + len(encoded)] = encoded
            offset += sizes[index]
        if loop and len(names) > 2:
            # the last entry's right subtree is the second entry: a loop
            struct.pack_into("<H", table, offset - sizes[-1] + 2, sizes[0] // 4)
        if names:
            put(table_sector, bytes(table))
        return table_sector, table_size, 0x10

    root_sector, root_size, _ = place(tree)
    descriptor = bytearray(SECTOR)
    descriptor[0:20] = b"MICROSOFT*XBOX*MEDIA"
    struct.pack_into("<II", descriptor, 20, root_sector, root_size)
    put(32, bytes(descriptor))
    return bytes(image), partition


VOLUME_SECTOR_END = 33


def write_xiso(path, tree, partition=0, loop=False):
    image, partition = build_xiso(tree, partition, loop)
    with open(path, "wb") as file:
        if partition:
            file.seek(partition)
        file.write(image)
    return path


def write_zip(path, members):
    with zipfile.ZipFile(path, "w") as archive:
        for name, data in members.items():
            archive.writestr(name, data)
    return path


XBOX_BUILD = {"Warthog/default.xbe": b"XBEH" + b"\0" * 64,
              "Warthog/config.toml": "config_version = 2\n\n[console]\nbypass_security = true\n",
              "Warthog/README.md": "readme"}
X360_BUILD = {"Warthog/default.xex": b"XEX2" + b"\0" * 64, "Warthog/shader_cache.bin": b"cache",
              "Warthog/shaders/0001.psh": b"psh", "Warthog/README.md": "readme"}
WIIU_BUILD = {"wiiu/apps/halo.wuhb": b"WUHB" + b"\0" * 16, "wiiu/apps/halo/halo.rpx": b"\x7fELF" + b"\0" * 16}


@pytest.fixture
def disc(tmp_path):
    return write_xiso(tmp_path / "disc.iso", disc_tree(languages=("de", "fr")))


def quiet(*_):
    pass


def test_xbox_layout(tmp_path, disc):
    build = write_zip(tmp_path / "chupathingy-xbox-test.zip", XBOX_BUILD)
    out = tmp_path / "out"
    summary = packager.make_package(disc, "xbox", build, out, log=quiet)
    game = out / "Warthog"
    assert (game / "default.xbe").read_bytes().startswith(b"XBEH")
    assert (game / "maps" / "ui.map").read_bytes() == fake_map("ui")
    assert (game / "maps" / "loading.tga").read_bytes() == b"TGA"
    assert sorted(p.name for p in (game / "maps").iterdir()) == ["bloodgulch.map", "loading.tga", "ui.map"]
    config = (game / "config.toml").read_text()
    assert "bypass_security = true" in config and "language" not in config
    assert not (game / "bink").exists() and not (game / "maps_de").exists()
    assert "E:\\Games\\Warthog" in (out / "README.txt").read_text()
    assert sorted(p.name for p in out.iterdir()) == ["README.txt", "Warthog"]
    assert not (tmp_path / "out.partial").exists()
    assert "not checked" in summary


def test_xbox_language_follows_dashboard(tmp_path, disc):
    build = write_zip(tmp_path / "build.zip", XBOX_BUILD)
    out = tmp_path / "out"
    packager.make_package(disc, "xbox", build, out, languages=["de", "fr"], log=quiet)
    game = out / "Warthog"
    assert (game / "maps_de" / "ui.map").read_bytes() == fake_map("ui_de")
    assert (game / "maps_fr" / "ui.map").read_bytes() == fake_map("ui_fr")
    assert "language" not in (game / "config.toml").read_text()
    assert "dashboard" in (out / "README.txt").read_text()


def test_x360_layout_and_language(tmp_path, disc):
    build = write_zip(tmp_path / "build.zip", X360_BUILD)
    out = tmp_path / "out"
    packager.make_package(disc, "x360", build, out, languages=["fr"], log=quiet)
    game = out / "Warthog"
    assert (game / "default.xex").is_file() and (game / "shaders" / "0001.psh").is_file()
    assert (game / "shader_cache.bin").is_file()
    assert (game / "maps" / "ui.map").read_bytes() == fake_map("ui")
    assert (game / "maps_fr" / "ui.map").read_bytes() == fake_map("ui_fr")
    assert not (game / "maps_de").exists()
    config = (game / "config.toml").read_text()
    assert "config_version = 2" in config and '[game]\nlanguage = "fr"' in config
    assert "Aurora" in (out / "README.txt").read_text()


def test_x360_english_writes_no_config(tmp_path, disc):
    build = write_zip(tmp_path / "build.zip", X360_BUILD)
    out = tmp_path / "out"
    packager.make_package(disc, "x360", build, out, log=quiet)
    assert not (out / "Warthog" / "config.toml").exists()


def test_wiiu_layout(tmp_path, disc):
    build = write_zip(tmp_path / "build.zip", WIIU_BUILD)
    out = tmp_path / "out"
    packager.make_package(disc, "wiiu", build, out, languages=["de"], log=quiet)
    halo = out / "wiiu" / "apps" / "halo"
    assert (out / "wiiu" / "apps" / "halo.wuhb").is_file() and (halo / "halo.rpx").is_file()
    assert (halo / "maps" / "bloodgulch.map").read_bytes() == fake_map("bloodgulch")
    assert (halo / "maps_de" / "ui.map").is_file()
    assert 'language = "de"' in (halo / "config.toml").read_text()
    assert "SD card" in (out / "README.txt").read_text()


def test_unpacked_build_folder(tmp_path, disc):
    folder = tmp_path / "unzipped"
    for name, data in XBOX_BUILD.items():
        (folder / name).parent.mkdir(parents=True, exist_ok=True)
        (folder / name).write_bytes(data if isinstance(data, bytes) else data.encode())
    out = tmp_path / "out"
    packager.make_package(disc, "xbox", folder, out, log=quiet)
    assert (out / "Warthog" / "maps" / "ui.map").is_file()
    assert (folder / "Warthog" / "default.xbe").is_file()


def test_build_zip_without_a_folder(tmp_path, disc):
    flat = {name.split("/", 1)[1]: data for name, data in X360_BUILD.items()}
    build = write_zip(tmp_path / "build.zip", flat)
    out = tmp_path / "out"
    packager.make_package(disc, "x360", build, out, log=quiet)
    assert sorted(p.name for p in out.iterdir()) == ["README.txt", "Warthog"]
    assert (out / "Warthog" / "default.xex").is_file() and (out / "Warthog" / "maps" / "ui.map").is_file()


def test_extracted_disc_folder(tmp_path):
    folder = tmp_path / "disc"
    folder.mkdir()
    for name, node in disc_tree(languages=("it",)).items():
        if isinstance(node, dict):
            (folder / name).mkdir(parents=True)
            for child, data in node.items():
                (folder / name / child).write_bytes(data)
        else:
            (folder / name).write_bytes(node)
    build = write_zip(tmp_path / "build.zip", WIIU_BUILD)
    out = tmp_path / "out"
    packager.make_package(folder, "wiiu", build, out, languages=["it"], log=quiet)
    assert (out / "wiiu" / "apps" / "halo" / "maps_it" / "ui.map").read_bytes() == fake_map("ui_it")


def test_whole_disc_image(tmp_path):
    # (a sparse file: the game partition starts 0x18300000 bytes in)
    disc = write_xiso(tmp_path / "whole.iso", disc_tree(), partition=0x18300000)
    assert "en" in packager.describe_disc(disc)


def test_list_shows_languages(disc):
    text = packager.describe_disc(disc)
    assert "Halo: maps 01.10.12.2276" in text
    assert "de  German" in text and "fr  French" in text and "it  Italian" not in text


def test_language_not_on_disc(tmp_path, disc):
    build = write_zip(tmp_path / "build.zip", XBOX_BUILD)
    with pytest.raises(packager.PackageError, match="doesn't have Italian"):
        packager.make_package(disc, "xbox", build, tmp_path / "out", languages=["it"], log=quiet)
    assert not (tmp_path / "out").exists() and not (tmp_path / "out.partial").exists()


def test_japanese_disc(tmp_path):
    tree = disc_tree()
    tree["default.xbe"] = fake_xbe(region=packager.XBE_REGION_JAPAN)
    disc = write_xiso(tmp_path / "disc.iso", tree)
    build = write_zip(tmp_path / "build.zip", X360_BUILD)
    packager.make_package(disc, "x360", build, tmp_path / "out", languages=["ja"], log=quiet)
    assert 'language = "ja"' in (tmp_path / "out" / "Warthog" / "config.toml").read_text()
    assert not (tmp_path / "out" / "Warthog" / "maps_ja").exists()


@pytest.mark.parametrize("tree", [
    disc_tree(title_id=0x4D530064),
    {**disc_tree(), "default.xbe": b"MZ not an xbe" * 100},
    {"default.xbe": fake_xbe(), "maps": {"ui.map": fake_map("ui", version=7)}},
    {"default.xbe": fake_xbe(), "media": {"a.bin": b"x"}},
], ids=["other title", "not an xbe", "halo pc map", "no maps"])
def test_refuses_other_discs(tmp_path, tree):
    disc = write_xiso(tmp_path / "disc.iso", tree)
    with pytest.raises(packager.PackageError, match="isn't a Halo: Combat Evolved disc"):
        packager.describe_disc(disc)


def test_refuses_a_file_that_isnt_a_disc(tmp_path):
    path = tmp_path / "random.iso"
    path.write_bytes(os.urandom(200 * 1024))
    with pytest.raises(packager.PackageError, match="isn't an Xbox disc image"):
        packager.describe_disc(path)


@pytest.mark.parametrize("name", ["..", "../../evil.map", "a\\..\\b.map", "C:evil.map", "bad\x01.map"])
def test_refuses_path_traversal_in_the_image(tmp_path, name):
    tree = disc_tree()
    tree["maps"][name] = b"payload"
    disc = write_xiso(tmp_path / "disc.iso", tree)
    build = write_zip(tmp_path / "build.zip", XBOX_BUILD)
    with pytest.raises(packager.PackageError, match="isn't a plain file name"):
        packager.make_package(disc, "xbox", build, tmp_path / "out", log=quiet)
    assert not (tmp_path / "out").exists() and not (tmp_path / "out.partial").exists()
    assert not (tmp_path / "evil.map").exists()


def test_refuses_a_directory_loop(tmp_path):
    disc = write_xiso(tmp_path / "disc.iso", disc_tree(), loop=True)
    with pytest.raises(packager.PackageError, match="damaged"):
        packager.describe_disc(disc)


def test_refuses_a_file_past_the_end(tmp_path):
    tree = disc_tree()
    del tree["bink"]
    disc = write_xiso(tmp_path / "disc.iso", tree)
    # (the maps folder's last file is the image's last)
    disc.write_bytes(disc.read_bytes()[:-2])
    build = write_zip(tmp_path / "build.zip", XBOX_BUILD)
    with pytest.raises(packager.PackageError, match="Is the image complete"):
        packager.make_package(disc, "xbox", build, tmp_path / "out", log=quiet)


@pytest.mark.skipif(os.name == "nt", reason="symbolic links")
def test_refuses_a_link_out_of_the_disc_folder(tmp_path):
    folder = tmp_path / "disc"
    (folder / "maps").mkdir(parents=True)
    (folder / "default.xbe").write_bytes(fake_xbe())
    (folder / "maps" / "ui.map").write_bytes(fake_map("ui"))
    (tmp_path / "secret").write_text("x")
    (folder / "maps" / "secret.map").symlink_to(tmp_path / "secret")
    with pytest.raises(packager.PackageError, match="leads outside"):
        packager.describe_disc(folder)


@pytest.mark.parametrize("member", ["../evil.txt", "/abs/evil.txt", "Warthog/../../evil.txt", "C:/evil.txt"])
def test_refuses_a_build_zip_that_writes_outside(tmp_path, disc, member):
    build = write_zip(tmp_path / "build.zip", {**XBOX_BUILD, member: "x"})
    with pytest.raises(packager.PackageError, match="isn't one of our builds"):
        packager.make_package(disc, "xbox", build, tmp_path / "out", log=quiet)
    assert not (tmp_path / "evil.txt").exists() and not (tmp_path / "out.partial").exists()


def test_refuses_another_consoles_build(tmp_path, disc):
    build = write_zip(tmp_path / "build.zip", X360_BUILD)
    with pytest.raises(packager.PackageError, match="This is the Xbox 360 build"):
        packager.make_package(disc, "wiiu", build, tmp_path / "out", log=quiet)


def test_checks_the_sha256_file(tmp_path, disc):
    build = write_zip(tmp_path / "build.zip", XBOX_BUILD)
    digest = hashlib.sha256(build.read_bytes()).hexdigest()
    sidecar = tmp_path / "build.zip.sha256"
    sidecar.write_text(f"{digest}  build.zip\n")
    summary = packager.make_package(disc, "xbox", build, tmp_path / "good", log=quiet)
    assert "SHA-256 matches" in summary
    sidecar.write_text("0" * 64 + "  build.zip\n")
    with pytest.raises(packager.PackageError, match="doesn't match its SHA-256"):
        packager.make_package(disc, "xbox", build, tmp_path / "bad", log=quiet)


def test_refuses_a_folder_with_files(tmp_path, disc):
    build = write_zip(tmp_path / "build.zip", XBOX_BUILD)
    (tmp_path / "out").mkdir()
    (tmp_path / "out" / "keep.txt").write_text("mine")
    with pytest.raises(packager.PackageError, match="already has files"):
        packager.make_package(disc, "xbox", build, tmp_path / "out", log=quiet)
    assert (tmp_path / "out" / "keep.txt").read_text() == "mine"


def test_command_line(tmp_path, disc, capsys):
    build = write_zip(tmp_path / "build.zip", WIIU_BUILD)
    assert packager.main(["--disc", str(disc), "--list"]) == 0
    assert packager.main(["--disc", str(tmp_path / "missing.iso"), "--list"]) == 1
    assert "no disc image" in capsys.readouterr().err
    assert packager.main(["--disc", str(disc), "--target", "wiiu", "--build", str(build),
                          "--out", str(tmp_path / "out"), "--language", "de"]) == 0
    assert (tmp_path / "out" / "wiiu" / "apps" / "halo" / "maps_de").is_dir()
