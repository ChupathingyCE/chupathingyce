#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Makes a folder to copy to a console: a Chupathingy console build and the
maps from your own Halo: Combat Evolved disc for the original Xbox.

    python3 chupathingy_package.py --disc halo.iso --target xbox \\
        --build chupathingy-xbox-test-20261120.zip --out Chupathingy-Xbox

--disc is an image of your own disc (an xiso, or a whole disc's .iso), or a
folder you already copied off it. --target is xbox (the original Xbox),
x360 (the Xbox 360) or wiiu (the Wii U, on its SD card). --build is the
console build's zip from its GitHub releases, or the unzipped folder.
--language picks a language your disc has (--list shows them). The folder
written has a README.txt that says where it goes.

This is a standalone tool. It isn't part of the game, needs only Python 3
and never connects to the internet: you download the build yourself. When
the build's .sha256 file is next to its zip, the zip is checked against it.

The disc is read as it is and never changed. Only the maps are copied out,
and only into the folder you name: nothing leaves your computer.
"""

import argparse
import hashlib
import os
import re
import shutil
import stat
import struct
import sys
import zipfile
from pathlib import Path

# ---------- the disc's file system (XDVDFS)
#
# 2048-byte sectors. Sector 32 of the game partition holds the volume
# descriptor: "MICROSOFT*XBOX*MEDIA", the root directory's sector and its
# size. A directory is a table of entries, each a 14-byte header and a name:
# left and right subtree offsets (16 bits each, in 4-byte units, 0 for none),
# the first sector (32 bits), the size (32 bits), the attributes (0x10: a
# directory), the name's length and the name. The entries form a binary tree
# from the table's first one. A file is one run of sectors. The game's own
# reader is port/linux/src/xiso.c; this one is the same walk in Python.

SECTOR_SIZE = 2048
VOLUME_SECTOR = 32
VOLUME_MAGIC = b"MICROSOFT*XBOX*MEDIA"
ATTRIBUTE_DIRECTORY = 0x10
# an xiso, an original Xbox disc (XGD1), and the Xbox 360's XGD2 and XGD3
PARTITION_OFFSETS = (0, 0x18300000, 0x0FD90000, 0x02080000)
MAXIMUM_DIRECTORY_SIZE = 4 << 20
MAXIMUM_TREE_DEPTH = 64
COPY_CHUNK = 1 << 20

# ---------- what makes it Halo

# Halo: Combat Evolved's title ID ("MS" 004), in every region's default.xbe
HALO_TITLE_ID = 0x4D530004
XBE_REGION_JAPAN = 0x2
# an Xbox map: "head" (stored backward), cache version 5, "foot" at 0x7fc
MAP_HEADER_SIGNATURE = b"daeh"
MAP_FOOTER_SIGNATURE = b"toof"
MAP_VERSION_XBOX = 5
NTSC_BUILD = "01.10.12.2276"

# the language folders the game reads beside maps (source/cache/cache_files.c)
DISC_LANGUAGES = ("de", "fr", "es", "it")
LANGUAGE_NAMES = {"en": "English", "de": "German", "fr": "French", "es": "Spanish", "it": "Italian",
                  "ja": "Japanese"}

TARGETS = ("xbox", "x360", "wiiu")
TARGET_NAMES = {"xbox": "original Xbox", "x360": "Xbox 360", "wiiu": "Wii U"}

CONFIG_HEADER = ("# Chupathingy uses this line to update the file. Leave it as it is.\n"
                 "config_version = 2\n")


class PackageError(Exception):
    """A plain message for the player."""


def safe_name(name):
    """Whether a name from the disc can be written as one file or folder name:
    no path, no "." or "..", printable ASCII only."""
    if not name or name in (".", ".."):
        return False
    return all(0x20 <= ord(c) <= 0x7E and c not in '/\\:*?"<>|' for c in name)


def check_name(name):
    if not safe_name(name):
        raise PackageError(f"The disc has a file named {name!r}, which isn't a plain file name. "
                           "The image may be damaged or changed. Nothing was written.")


class Entry:
    def __init__(self, name, is_dir, size, sector=None, path=None):
        self.name = name
        self.is_dir = is_dir
        self.size = size
        self.sector = sector
        self.path = path


class XisoDisc:
    """An Xbox disc image, read only."""

    def __init__(self, path):
        self.path = Path(path)
        self.file = open(self.path, "rb")
        self.file_size = os.fstat(self.file.fileno()).st_size
        self.partition = None
        for offset in PARTITION_OFFSETS:
            descriptor = self._read_at(offset + VOLUME_SECTOR * SECTOR_SIZE, SECTOR_SIZE)
            if descriptor is not None and descriptor.startswith(VOLUME_MAGIC):
                self.partition = offset
                sector, size = struct.unpack_from("<II", descriptor, 20)
                self.root = Entry("", True, size, sector)
                break
        if self.partition is None:
            self.file.close()
            raise PackageError(f"{self.path.name} isn't an Xbox disc image.")

    def close(self):
        self.file.close()

    def _read_at(self, position, size):
        if position < 0 or position + size > self.file_size:
            return None
        self.file.seek(position)
        data = self.file.read(size)
        return data if len(data) == size else None

    def _check_extent(self, entry):
        end = self.partition + entry.sector * SECTOR_SIZE + entry.size
        if end > self.file_size:
            raise PackageError(f"{entry.name} runs past the end of the disc image. Is the image complete?")

    def list(self, directory):
        """The entries of a directory, in name order."""
        if not directory.size:
            return []
        if directory.size > MAXIMUM_DIRECTORY_SIZE:
            raise PackageError("The disc image's folders are damaged.")
        self._check_extent(directory)
        table = self._read_at(self.partition + directory.sector * SECTOR_SIZE, directory.size)
        if table is None:
            raise PackageError("The disc image's folders are damaged.")
        entries = []
        seen = set()
        # (the in-order walk, without recursion; a loop or a deep tree is damage)
        stack = []
        offset = 0
        while True:
            while offset is not None:
                if offset in seen or len(stack) > MAXIMUM_TREE_DEPTH:
                    raise PackageError("The disc image's folders are damaged.")
                seen.add(offset)
                position = offset * 4
                if position + 14 > len(table):
                    raise PackageError("The disc image's folders are damaged.")
                left, right, sector, size, attributes, length = struct.unpack_from("<HHIIBB", table, position)
                if not length or position + 14 + length > len(table):
                    raise PackageError("The disc image's folders are damaged.")
                name = table[position + 14:position + 14 + length].decode("latin-1")
                stack.append((name, sector, size, attributes, right))
                offset = left or None
            if not stack:
                break
            name, sector, size, attributes, right = stack.pop()
            check_name(name)
            entries.append(Entry(name, bool(attributes & ATTRIBUTE_DIRECTORY), size, sector))
            offset = right or None
        return entries

    def read_head(self, entry, size):
        self._check_extent(entry)
        return self._read_at(self.partition + entry.sector * SECTOR_SIZE, min(size, entry.size)) or b""

    def copy(self, entry, destination, progress):
        self._check_extent(entry)
        self.file.seek(self.partition + entry.sector * SECTOR_SIZE)
        remaining = entry.size
        with open(destination, "wb") as output:
            while remaining:
                chunk = self.file.read(min(COPY_CHUNK, remaining))
                if not chunk:
                    raise PackageError(f"Couldn't read {entry.name} from the disc image. Is it complete?")
                output.write(chunk)
                remaining -= len(chunk)
                progress(len(chunk))


class FolderDisc:
    """A folder copied off the disc, read only. Links that lead out of it are
    refused."""

    def __init__(self, path):
        self.path = Path(path).resolve()
        self.root = Entry("", True, 0, path=self.path)

    def close(self):
        pass

    def list(self, directory):
        entries = []
        for child in sorted(directory.path.iterdir(), key=lambda p: p.name.lower()):
            check_name(child.name)
            resolved = child.resolve()
            if self.path not in resolved.parents and resolved != self.path:
                raise PackageError(f"{child} leads outside the disc folder. Nothing was written.")
            if child.is_dir():
                entries.append(Entry(child.name, True, 0, path=child))
            elif child.is_file():
                entries.append(Entry(child.name, False, child.stat().st_size, path=child))
        return entries

    def read_head(self, entry, size):
        with open(entry.path, "rb") as file:
            return file.read(size)

    def copy(self, entry, destination, progress):
        with open(entry.path, "rb") as source, open(destination, "wb") as output:
            while True:
                chunk = source.read(COPY_CHUNK)
                if not chunk:
                    break
                output.write(chunk)
                progress(len(chunk))


def open_disc(path):
    path = Path(path).expanduser()
    if path.is_dir():
        return FolderDisc(path)
    if path.is_file():
        return XisoDisc(path)
    raise PackageError(f"There's no disc image or folder at {path}.")


def find(entries, name):
    for entry in entries:
        if entry.name.lower() == name.lower():
            return entry
    return None


# ---------- checking the disc

def read_xbe_certificate(data):
    """(title ID, title name, game region) from the start of an XBE."""
    if len(data) < 0x178 or data[:4] != b"XBEH":
        return None
    base, = struct.unpack_from("<I", data, 0x104)
    certificate_address, = struct.unpack_from("<I", data, 0x118)
    offset = certificate_address - base
    if offset < 0 or offset + 0xA4 > len(data):
        return None
    title_id, = struct.unpack_from("<I", data, offset + 8)
    name = data[offset + 0x0C:offset + 0x0C + 0x50].decode("utf-16-le", "replace").split("\0")[0]
    region, = struct.unpack_from("<I", data, offset + 0xA0)
    return title_id, name, region


def map_build(header):
    """The build string of an Xbox map's header, or None if it isn't one."""
    if len(header) < 0x800 or header[:4] != MAP_HEADER_SIGNATURE or header[0x7FC:0x800] != MAP_FOOTER_SIGNATURE:
        return None
    version, = struct.unpack_from("<i", header, 4)
    if version != MAP_VERSION_XBOX:
        return None
    return header[0x40:0x60].split(b"\0")[0].decode("ascii", "replace")


class DiscContents:
    """What a checked Halo disc has: its maps folders by language."""

    def __init__(self, disc):
        self.disc = disc
        root = disc.list(disc.root)
        not_halo = "This isn't a Halo: Combat Evolved disc for the original Xbox."
        self.region = 0
        xbe = find(root, "default.xbe")
        if xbe and not xbe.is_dir:
            certificate = read_xbe_certificate(disc.read_head(xbe, 64 * 1024))
            if not certificate or certificate[0] != HALO_TITLE_ID:
                raise PackageError(not_halo)
            self.title = certificate[1]
            self.region = certificate[2]
        elif isinstance(disc, FolderDisc):
            self.title = None
        else:
            raise PackageError(not_halo)
        # English is "maps" (on some discs "maps_en"); the others beside it
        self.folders = {}
        english = find(root, "maps") or find(root, "maps_en")
        if english and english.is_dir:
            self.folders["en"] = english
        for language in DISC_LANGUAGES:
            folder = find(root, "maps_" + language)
            if folder and folder.is_dir:
                self.folders[language] = folder
        self.builds = {}
        for language, folder in list(self.folders.items()):
            ui = find(disc.list(folder), "ui.map")
            build = map_build(disc.read_head(ui, 0x800)) if ui and not ui.is_dir else None
            if build is None:
                del self.folders[language]
            else:
                self.builds[language] = build
        if "en" not in self.folders:
            if self.title is None:
                raise PackageError(not_halo + " The folder needs default.xbe, or a maps folder with ui.map.")
            raise PackageError(not_halo)
        # a Japanese disc's maps folder holds the Japanese game
        self.base_language = "ja" if self.region == XBE_REGION_JAPAN else "en"

    def languages(self):
        return [self.base_language] + [language for language in DISC_LANGUAGES if language in self.folders]


# ---------- the console build

def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        for chunk in iter(lambda: file.read(COPY_CHUNK), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check_build_checksum(path, expected):
    """Checks a build zip against expected, or against the .sha256 file beside
    it. Returns what was checked, for the summary."""
    sidecar = path.with_name(path.name + ".sha256")
    if not expected and sidecar.is_file():
        expected = (sidecar.read_text(encoding="ascii", errors="replace").split() or [""])[0]
    if not expected:
        return "not checked (no .sha256 file beside it)"
    expected = expected.strip().lower()
    if not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise PackageError("The SHA-256 to check the build against isn't 64 hex digits.")
    if sha256_file(path) != expected:
        raise PackageError(f"{path.name} doesn't match its SHA-256. Download it again.")
    return "SHA-256 matches"


def safe_member(name):
    """A zip member's path as parts, or None if it isn't a plain relative path."""
    name = name.replace("\\", "/")
    if name.startswith("/") or re.match(r"[A-Za-z]:", name):
        return None
    parts = [part for part in name.split("/") if part]
    if not parts or any(not safe_name(part) for part in parts):
        return None
    return parts


def unpack_build(zip_path, destination):
    """Unpacks a build zip into destination, refusing any member whose path
    would land outside it."""
    with zipfile.ZipFile(zip_path) as archive:
        for member in archive.infolist():
            parts = safe_member(member.filename)
            if parts is None:
                raise PackageError(f"{zip_path.name} has a file named {member.filename!r}. "
                                   "This isn't one of our builds.")
            if stat.S_ISLNK(member.external_attr >> 16):
                raise PackageError(f"{zip_path.name} has a link in it. This isn't one of our builds.")
            target = destination.joinpath(*parts)
            if member.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(member) as source, open(target, "wb") as output:
                shutil.copyfileobj(source, output, COPY_CHUNK)


def file_starts_with(path, magic):
    try:
        with open(path, "rb") as file:
            return file.read(len(magic)) == magic
    except OSError:
        return False


def find_file(root, name):
    """The shallowest file named name below root."""
    matches = sorted((p for p in root.rglob(name) if p.is_file()), key=lambda p: len(p.parts))
    return matches[0] if matches else None


def locate_build(root, target):
    """The build's folder for target: the folder to copy, and where the maps go
    in it. Says so plainly if it's another console's build."""
    xbe = find_file(root, "default.xbe")
    xex = find_file(root, "default.xex")
    rpx = find_file(root, "halo.rpx")
    found = {"xbox": xbe is not None and file_starts_with(xbe, b"XBEH"),
             "x360": xex is not None and file_starts_with(xex, b"XEX2"),
             "wiiu": rpx is not None and file_starts_with(rpx, b"\x7fELF")}
    if not found[target]:
        others = [TARGET_NAMES[name] for name, present in found.items() if present]
        if others:
            raise PackageError(f"This is the {others[0]} build, not the {TARGET_NAMES[target]} one.")
        raise PackageError(f"This isn't a Chupathingy build for the {TARGET_NAMES[target]}.")
    if target == "xbox":
        return xbe.parent, xbe.parent
    if target == "x360":
        return xex.parent, xex.parent
    # the SD card's wiiu folder: wiiu/apps/halo.wuhb and wiiu/apps/halo/
    if rpx.parent.name != "halo" or rpx.parent.parent.name != "apps" or rpx.parent.parent.parent.name != "wiiu":
        raise PackageError("The Wii U build's halo.rpx isn't in wiiu/apps/halo. Use the release's zip as it is.")
    return rpx.parent.parent.parent, rpx.parent


# ---------- config.toml

def set_setting(text, section, key, value):
    """text with [section]'s key set to value (a TOML value as written): its
    line, or the line commented out at its default, replaced; else added at
    the section's end, the section added if missing."""
    lines = text.splitlines(keepends=True)
    current = None
    end = None
    pattern = re.compile(r"\s*#?\s*" + re.escape(key) + r"\s*=")
    for index, line in enumerate(lines):
        header = re.match(r"\s*\[([^\]]+)\]", line)
        if header:
            if current == section:
                break
            current = header.group(1).strip()
            if current == section:
                end = index + 1
            continue
        if current == section:
            if pattern.match(line):
                lines[index] = f"{key} = {value}\n"
                return "".join(lines)
            if line.strip():
                end = index + 1
    if not any(re.match(r"\s*\[" + re.escape(section) + r"\]", line) for line in lines):
        tail = "" if not text or text.endswith("\n") else "\n"
        return text + tail + f"\n[{section}]\n{key} = {value}\n"
    lines.insert(end if end is not None else len(lines), f"{key} = {value}\n")
    return "".join(lines)


# ---------- the package

README = {
    "xbox": """\
Chupathingy for the original Xbox
=================================

Copy the Warthog folder to your Xbox's hard disk, for example to
E:\\Games\\Warthog\\ over FTP. Then start default.xbe from your dashboard.

The maps folder came from your own disc. Keep it to yourself.
{language}
Settings are in config.toml. Help and updates:
https://github.com/ChupathingyCE/warthog-xbox
""",
    "x360": """\
Chupathingy for the Xbox 360
============================

Copy the Warthog folder to your console's hard drive, for example to
Hdd1:\\Games\\Warthog360\\ over FTP. Add it to Aurora's content paths, or
start default.xex from your launcher's file browser.

The maps folder came from your own disc. Keep it to yourself.
{language}
Settings are in config.toml. Help and updates:
https://github.com/ChupathingyCE/warthog-360
""",
    "wiiu": """\
Chupathingy for the Wii U
=========================

Copy the wiiu folder to the root of your SD card. You should then have
wiiu/apps/halo.wuhb and wiiu/apps/halo/maps on the card. Start Chupathingy
from the Wii U Menu. You need Aroma.

The maps folder came from your own disc. Keep it to yourself.
{language}
Settings are in wiiu/apps/halo/config.toml. Help and updates:
https://github.com/ChupathingyCE/warthog-wiiu
""",
}


def language_note(target, languages, base_language):
    picked = [language for language in languages if language != base_language]
    if not picked:
        return ""
    names = ", ".join(LANGUAGE_NAMES[language] for language in picked)
    folders = ", ".join("maps_" + language for language in picked)
    if target == "xbox":
        return (f"\n{folders} holds the {names} game. The game follows your\n"
                "dashboard's language. Set it there to play in that language.\n")
    return f"\n{folders} holds the {names} game. config.toml sets it as the game's language.\n"


def copy_folder(disc, folder, destination, progress):
    """Copies the files of one of the disc's maps folders (not its folders)."""
    destination.mkdir(parents=True, exist_ok=True)
    for entry in disc.list(folder):
        if not entry.is_dir:
            disc.copy(entry, destination / entry.name, progress)


def folder_size(disc, folder):
    return sum(entry.size for entry in disc.list(folder) if not entry.is_dir)


def make_package(disc_path, target, build_path, out, languages=(), sha256=None, log=print):
    """Writes the package; returns a summary for the player."""
    out = Path(out).expanduser()
    build_path = Path(build_path).expanduser()
    if out.exists() and (not out.is_dir() or any(out.iterdir())):
        raise PackageError(f"{out} already has files in it. Pick a new folder.")
    disc = open_disc(disc_path)
    try:
        contents = DiscContents(disc)
        available = contents.languages()
        wanted = []
        for language in languages:
            language = language.lower()
            if language not in available:
                have = ", ".join(f"{code} ({LANGUAGE_NAMES[code]})" for code in available)
                raise PackageError(f"Your disc doesn't have {LANGUAGE_NAMES.get(language, language)}. "
                                   f"It has: {have}.")
            if language not in wanted:
                wanted.append(language)
        folders = [("maps", contents.folders["en"])]
        folders += [("maps_" + language, contents.folders[language]) for language in wanted
                    if language != contents.base_language]

        if not build_path.exists():
            raise PackageError(f"There's no build at {build_path}.")
        checked = "a folder, not checked"
        partial = out.with_name(out.name + ".partial")
        if partial.exists():
            shutil.rmtree(partial)
        partial.mkdir(parents=True)
        try:
            if build_path.is_file():
                if not zipfile.is_zipfile(build_path):
                    raise PackageError(f"{build_path.name} isn't a zip file.")
                checked = check_build_checksum(build_path, sha256)
                build_root = partial / ".build"
                unpack_build(build_path, build_root)
            else:
                build_root = build_path
            top, game = locate_build(build_root, target)
            # (the folder the console README names: Warthog, or the SD card's wiiu)
            package_top = partial / ("wiiu" if target == "wiiu" else "Warthog")
            if build_path.is_file():
                shutil.move(str(top), str(package_top))
                shutil.rmtree(build_root, ignore_errors=True)
            else:
                shutil.copytree(str(top), str(package_top))
            package_game = package_top / game.relative_to(top)

            total = sum(folder_size(disc, folder) for _, folder in folders)
            free = shutil.disk_usage(partial).free
            if free < total + (64 << 20):
                raise PackageError(f"There isn't room: the maps need {total / 1e9:.1f} GB, "
                                   f"and {partial.parent} has {free / 1e9:.1f} GB free.")
            done = [0, -1]

            def progress(count):
                done[0] += count
                percent = done[0] * 100 // max(total, 1)
                if percent // 10 != done[1]:
                    done[1] = percent // 10
                    log(f"  copying maps: {percent}%")

            for name, folder in folders:
                existing = package_game / name
                if existing.exists():
                    shutil.rmtree(existing)
                copy_folder(disc, folder, package_game / name, progress)

            # config.toml: the Xbox's from the build as it is (the dashboard
            # sets the language there); the 360 and Wii U read game.language
            config = package_game / "config.toml"
            if target == "xbox" and not config.is_file():
                config.write_text(CONFIG_HEADER + "\n[console]\nbypass_security = true\n", encoding="utf-8")
            game_language = wanted[0] if wanted else None
            if target != "xbox" and game_language and game_language != "en":
                text = config.read_text(encoding="utf-8") if config.is_file() else CONFIG_HEADER
                config.write_text(set_setting(text, "game", "language", f'"{game_language}"'), encoding="utf-8")

            note = language_note(target, wanted, contents.base_language)
            if target != "xbox" and game_language == "ja":
                note = "\nconfig.toml sets Japanese as the game's language.\n"
            (partial / "README.txt").write_text(README[target].format(language=note), encoding="utf-8")
            if out.exists():
                out.rmdir()
            partial.rename(out)
        except BaseException:
            shutil.rmtree(partial, ignore_errors=True)
            raise
    finally:
        disc.close()
    builds = ", ".join(sorted(set(contents.builds[language] for language in contents.folders)))
    lines = [f"{out}: Chupathingy for the {TARGET_NAMES[target]}",
             f"  disc: {contents.title or 'a Halo disc folder'}, maps {builds}",
             f"  build: {build_path.name}, {checked}",
             f"  maps: {', '.join(name for name, _ in folders)}"]
    if contents.builds["en"] != NTSC_BUILD:
        lines.append(f"  note: online games use the North American maps ({NTSC_BUILD}). "
                     f"Yours are {contents.builds['en']}.")
    lines.append(f"  next: read {out / 'README.txt'}")
    return "\n".join(lines)


def describe_disc(disc_path):
    disc = open_disc(disc_path)
    try:
        contents = DiscContents(disc)
        lines = [f"{contents.title or 'A Halo disc folder'}: maps {contents.builds['en']}", "Languages:"]
        for language in contents.languages():
            lines.append(f"  {language}  {LANGUAGE_NAMES[language]}")
        return "\n".join(lines)
    finally:
        disc.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--disc", required=True, help="your own disc's image, or a folder copied off it")
    parser.add_argument("--target", choices=TARGETS, help="xbox, x360 or wiiu")
    parser.add_argument("--build", help="the console build's zip (or its folder)")
    parser.add_argument("--out", help="the new folder to write")
    parser.add_argument("--language", action="append", default=[], metavar="CODE",
                        help="a language from your disc (de, fr, es, it; ja on a Japanese disc). "
                             "Again for more. The first is the game's language on the 360 and Wii U")
    parser.add_argument("--sha256", help="the build zip's SHA-256, if its .sha256 file isn't beside it")
    parser.add_argument("--list", action="store_true", help="check the disc and show its languages, then stop")
    args = parser.parse_args(argv)
    try:
        if args.list:
            print(describe_disc(args.disc))
            return 0
        missing = [name for name in ("target", "build", "out") if not getattr(args, name)]
        if missing:
            parser.error("needs " + ", ".join("--" + name for name in missing) + " (or --list)")
        print(make_package(args.disc, args.target, args.build, args.out, args.language, args.sha256))
        return 0
    except PackageError as error:
        print(error, file=sys.stderr)
        return 1
    except OSError as error:
        print(f"Couldn't finish: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
