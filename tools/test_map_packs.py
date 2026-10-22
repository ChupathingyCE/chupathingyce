"""Map packs (port/linux/game/map_packs.c, halo_map_packs.h): the files'
fingerprints (the C code's against tools/xbox_mod_maps.py's), the packs'
maplist.txt, the library of known Xbox mod maps (port/assets/maps), the
names a pack's map is given (the library's, then maplist.txt's, then the
file's), the multiplayer maps each maps_<pack> folder lists, and a pack's
own menus' names. map_packs.c and map_families.c are compiled alone with the
build machine's C compiler, the Win32 calls they make stood in for by POSIX
ones over a folder that is the harness's d:\\. The library's own header is
checked to be the JSON's.

With the owner's Halo CE+ X files (HALO_CEX_MAPS, a folder of them; read
only), their fingerprints are checked to be the library's."""
import importlib.util
import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent

spec = importlib.util.spec_from_file_location("xbox_mod_maps", ROOT / "tools" / "xbox_mod_maps.py")
xbox_mod_maps = importlib.util.module_from_spec(spec)
spec.loader.exec_module(xbox_mod_maps)

STUB_CSERIES = """
#include <string.h>
#include <strings.h>
typedef unsigned char boolean;
#define TRUE 1
#define FALSE 0
#define NONE -1
#define NUMBEROF(a) (sizeof(a) / sizeof((a)[0]))
"""
STUB_WINDOWS = r"""
#include <stdio.h>
#include <stdlib.h>
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
typedef void *HANDLE;
typedef struct { unsigned long dwLowDateTime, dwHighDateTime; } FILETIME;
typedef struct { unsigned long dwFileAttributes; char cFileName[260]; } WIN32_FIND_DATAA;
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define INVALID_SET_FILE_POINTER 0xFFFFFFFFUL
#define INVALID_FILE_SIZE 0xFFFFFFFFUL
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define GENERIC_READ 1
#define GENERIC_WRITE 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define FILE_ATTRIBUTE_NORMAL 0
#define FILE_BEGIN 0
#define FILE_END 2
HANDLE CreateFileA(char const *name, int access, int share, void *security, int how, int flags, void *template_file);
int ReadFile(HANDLE file, void *buffer, unsigned long size, unsigned long *count, void *overlapped);
int WriteFile(HANDLE file, void const *buffer, unsigned long size, unsigned long *count, void *overlapped);
unsigned long SetFilePointer(HANDLE file, long offset, long *high, int how);
unsigned long GetFileSize(HANDLE file, unsigned long *high);
int GetFileTime(HANDLE file, FILETIME *created, FILETIME *accessed, FILETIME *written);
HANDLE FindFirstFileA(char const *name, WIN32_FIND_DATAA *data);
int FindNextFileA(HANDLE find, WIN32_FIND_DATAA *data);
int CloseHandle(HANDLE handle);
"""
# (Win32 over POSIX: d:\ is HARNESS_ROOT, backslashes slashes)
STUBS = r"""
#include <dirent.h>
#include <sys/stat.h>
/* (a pattern of one '*', any case: "*.map", "maps_*") */
static int harness_match(const char *pattern, const char *name)
{
	const char *star = strchr(pattern, '*');
	size_t prefix = star ? (size_t)(star - pattern) : strlen(pattern), suffix = star ? strlen(star + 1) : 0;
	size_t length = strlen(name);
	if (!star)
		return !strcasecmp(pattern, name);
	return length >= prefix + suffix && !strncasecmp(name, pattern, prefix) &&
		!strcasecmp(name + length - suffix, star + 1);
}
struct harness_find { DIR *directory; char pattern[256]; char folder[1024]; };
static struct harness_find *finds[64];
static void harness_path(char const *game, char *path, size_t size)
{
	size_t used = (size_t)snprintf(path, size, "%s/", getenv("HARNESS_ROOT"));
	if (!strncasecmp(game, "d:", 2))
		game += 2;
	while (*game == '\\')
		game++;
	for (; *game && used + 1 < size; game++)
		path[used++] = *game == '\\' ? '/' : *game;
	path[used] = 0;
}
HANDLE CreateFileA(char const *name, int access, int share, void *security, int how, int flags, void *template_file)
{
	char path[1024];
	FILE *file;
	(void)access; (void)share; (void)security; (void)how; (void)flags; (void)template_file;
	harness_path(name, path, sizeof(path));
	file = fopen(path, "rb");
	return file ? (HANDLE)file : INVALID_HANDLE_VALUE;
}
int ReadFile(HANDLE file, void *buffer, unsigned long size, unsigned long *count, void *overlapped)
{
	(void)overlapped;
	*count = (unsigned long)fread(buffer, 1, size, (FILE *)file);
	return 1;
}
int WriteFile(HANDLE file, void const *buffer, unsigned long size, unsigned long *count, void *overlapped)
{
	(void)file; (void)buffer; (void)size; (void)overlapped;
	*count = 0;
	return 0;
}
unsigned long SetFilePointer(HANDLE file, long offset, long *high, int how)
{
	(void)high;
	return fseek((FILE *)file, offset, how == FILE_END ? SEEK_END : SEEK_SET) ? INVALID_SET_FILE_POINTER : (unsigned long)offset;
}
unsigned long GetFileSize(HANDLE file, unsigned long *high)
{
	long here = ftell((FILE *)file), size;
	(void)high;
	fseek((FILE *)file, 0, SEEK_END);
	size = ftell((FILE *)file);
	fseek((FILE *)file, here, SEEK_SET);
	return (unsigned long)size;
}
int GetFileTime(HANDLE file, FILETIME *created, FILETIME *accessed, FILETIME *written)
{
	(void)file; (void)created; (void)accessed;
	written->dwLowDateTime = written->dwHighDateTime = 0;
	return 1;
}
static int harness_find_next(struct harness_find *find, WIN32_FIND_DATAA *data)
{
	struct dirent *entry;
	while ((entry = readdir(find->directory)) != NULL)
	{
		char path[1300];
		struct stat status;
		if (!harness_match(find->pattern, entry->d_name))
			continue;
		snprintf(path, sizeof(path), "%s/%s", find->folder, entry->d_name);
		stat(path, &status);
		data->dwFileAttributes = S_ISDIR(status.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : 0;
		snprintf(data->cFileName, sizeof(data->cFileName), "%s", entry->d_name);
		return 1;
	}
	return 0;
}
HANDLE FindFirstFileA(char const *name, WIN32_FIND_DATAA *data)
{
	struct harness_find *find = calloc(1, sizeof(*find));
	char const *separator = strrchr(name, '\\');
	char folder[512];
	int index;
	snprintf(folder, sizeof(folder), "%.*s", (int)(separator - name), name);
	snprintf(find->pattern, sizeof(find->pattern), "%s", separator + 1);
	harness_path(folder, find->folder, sizeof(find->folder));
	find->directory = opendir(find->folder);
	if (!find->directory || !harness_find_next(find, data))
	{
		if (find->directory)
			closedir(find->directory);
		free(find);
		return INVALID_HANDLE_VALUE;
	}
	for (index = 0; index < 63 && finds[index]; index++)
		;
	finds[index] = find;
	return (HANDLE)find;
}
int FindNextFileA(HANDLE find, WIN32_FIND_DATAA *data)
{
	return harness_find_next((struct harness_find *)find, data);
}
int CloseHandle(HANDLE handle)
{
	int index;
	for (index = 0; index < 64; index++)
	{
		if (finds[index] && (HANDLE)finds[index] == handle)
		{
			closedir(finds[index]->directory);
			free(finds[index]);
			finds[index] = NULL;
			return 1;
		}
	}
	fclose((FILE *)handle);
	return 1;
}
"""
HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "halo_map_families.h"
#include "halo_map_packs.h"
char const *config_string(char const *name) { (void)name; return getenv("HARNESS_PACK_MENUS") ? getenv("HARNESS_PACK_MENUS") : ""; }
char const *cache_files_map_directory(void) { return "d:\\maps\\"; }
static void listed(char const *pack, char const *file, void *context) { (void)context; printf("%s %s\n", pack, file); }
int main(int argc, char **argv)
{
	char name[96], title[96], out[96];
	int index;
	if (!strcmp(argv[1], "fingerprint"))
		printf("%08lx\n", map_pack_fingerprint(argv[2], argv[3]));
	else if (!strcmp(argv[1], "names"))
	{
		unsigned long fingerprint = argc > 4 ? strtoul(argv[4], NULL, 16) : 0;
		short source = map_pack_names(argv[2], argv[3], fingerprint, name, sizeof(name), title, sizeof(title));
		printf("%d %s|%s\n", source, name, title);
	}
	else if (!strcmp(argv[1], "maplist"))
	{
		for (index = 3; index < argc; index++)
			printf("%s\n", map_pack_maplist_find(argv[2], argv[index], name, sizeof(name)) ? name : "-");
	}
	else if (!strcmp(argv[1], "list"))
		map_pack_list(listed, NULL);
	else if (!strcmp(argv[1], "path"))
	{
		for (index = 2; index < argc; index++)
			printf("%s\n", map_family_xbox_path(argv[index], out, sizeof(out)) ? out : "-");
	}
	else if (!strcmp(argv[1], "present"))
	{
		for (index = 3; index < argc; index++)
			printf("%d\n", map_pack_present(argv[2], argv[index]));
	}
	else if (!strcmp(argv[1], "listing"))
	{
		char tag[16];
		int listed = map_pack_listing(argv[2], name, sizeof(name), title, sizeof(title), tag, sizeof(tag));
		printf("%d %s|%s|%s\n", listed, name, title, tag);
	}
	else if (!strcmp(argv[1], "tag"))
	{
		map_pack_tag(argv[2], name, sizeof(name));
		map_pack_title(argv[2], title, sizeof(title));
		printf("%s|%s\n", name, title);
	}
	else if (!strcmp(argv[1], "menus"))
	{
		printf("%s %d\n", map_pack_menus() ? map_pack_menus() : "-", map_pack_menus_known());
		for (index = 2; index < argc; index++)
		{
			map_pack_menus_map_name(argv[index], out, sizeof(out));
			printf("%s\n", out);
		}
	}
	return 0;
}
"""


def xbox_header(map_type: int) -> bytes:
    """an Xbox cache file's header (0x800 bytes): 'head', version 5, ...,
    the map's type at 0x60, 'foot'"""
    header = bytearray(0x800)
    header[0:4] = b"daeh"
    header[4:8] = (5).to_bytes(4, "little")
    header[0x60:0x62] = map_type.to_bytes(2, "little")
    header[0x7FC:0x800] = b"toof"
    return bytes(header)


def build(tmp_path_factory, library_header=None):
    """the harness, compiled with the game's library header (or another)"""
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    work = tmp_path_factory.mktemp("packs")
    (work / "cseries").mkdir()
    (work / "cseries.h").write_text(STUB_CSERIES)
    (work / "cseries" / "cseries_windows.h").write_text(STUB_WINDOWS)
    (work / "harness.c").write_text(HARNESS + STUBS)
    for header in ("halo_map_families.h", "halo_map_packs.h"):
        shutil.copy(ROOT / "port/linux/include" / header, work / header)
    if library_header is None:
        shutil.copy(ROOT / "port/linux/game/xbox_mod_maps.h", work / "xbox_mod_maps.h")
    else:
        (work / "xbox_mod_maps.h").write_text(library_header)
    # (copies of the sources: a quoted include looks beside its file first,
    # where the game's own library header is)
    shutil.copy(ROOT / "port/linux/game/map_packs.c", work / "map_packs.c")
    binary = work / "packs"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Werror", "-Wno-unused-function", "-DHALO_MAP_LIST", "-I", str(work),
                    str(work / "harness.c"), str(work / "map_packs.c"),
                    str(ROOT / "port/linux/game/map_families.c"), "-o", str(binary)], check=True)
    return work, binary


def runner(binary, root):
    def run(*args, menus=None):
        environment = dict(os.environ, HARNESS_ROOT=str(root))
        if menus is not None:
            environment["HARNESS_PACK_MENUS"] = menus
        output = subprocess.run([str(binary), *args], check=True, capture_output=True, text=True, env=environment).stdout
        return output.splitlines()
    run.root = root
    return run


@pytest.fixture(scope="module")
def packs(tmp_path_factory):
    work, binary = build(tmp_path_factory)
    root = work / "d"
    (root / "maps").mkdir(parents=True)
    cex = root / "maps_cex"
    cex.mkdir()
    # (a pack's maps: multiplayer ones, a campaign level, its ui.map and a
    # file that is no map; and a maps_de, the Xbox's German maps, no pack)
    (cex / "bloodgulch.map").write_bytes(xbox_header(1) + bytes(range(256)) * 900)
    (cex / "wizard.map").write_bytes(xbox_header(1) + b"\x07" * 5000)
    (cex / "cex_museum.map").write_bytes(xbox_header(0))
    (cex / "ui.map").write_bytes(xbox_header(2))
    (cex / "notes.map").write_bytes(b"not a map")
    (cex / "maplist.txt").write_bytes(b"bloodgulch - Abyss\r\nwizard- Gothra \r\nputput- chiron extended")
    (root / "maps_foo").mkdir()
    (root / "maps_foo" / "bloodgulch.map").write_bytes(xbox_header(1) + b"\x01" * 100)
    (root / "maps_de").mkdir()
    (root / "maps_de" / "bloodgulch.map").write_bytes(xbox_header(1))
    (root / "maps" / "a10.map").write_bytes(xbox_header(0))

    return runner(binary, root)


def test_library_header_is_the_json():
    packs, maps = xbox_mod_maps.load()
    assert (ROOT / "port/linux/game/xbox_mod_maps.h").read_text(encoding="utf-8") == xbox_mod_maps.header_text(packs, maps)
    assert "cex" in packs and all(row["pack"] in packs for _, row in maps)


def test_library_refuses_bad_entries(tmp_path):
    def library(**fields):
        entry = {"fingerprint": "12345678", "pack": "cex", "file": "bloodgulch", "name": "Abyss"}
        entry.update(fields)
        path = tmp_path / "library.json"
        path.write_text(__import__("json").dumps({"packs": {"cex": {"name": "Halo CE+", "tag": "CE+"}},
                                                  "maps": [entry]}))
        return path
    xbox_mod_maps.load(library())
    for bad in ({"fingerprint": "0"}, {"fingerprint": "00000000"}, {"pack": "zzz"}, {"file": "a.map"},
                {"source": "http://example.com/x.zip"}, {"name": "caf\u00e9"}, {"extra": "1"}):
        with pytest.raises(ValueError):
            xbox_mod_maps.load(library(**bad))


def test_fingerprints_are_the_tools(packs):
    for file in ("bloodgulch", "wizard", "cex_museum"):
        expected = xbox_mod_maps.fingerprint(packs.root / "maps_cex" / f"{file}.map")
        assert packs("fingerprint", "cex", file) == [f"{expected:08x}"]
    assert packs("fingerprint", "cex", "missing") == ["00000000"]
    assert packs("fingerprint", "md", "bloodgulch") == ["00000000"]


def test_maplist_lines(packs):
    text = "carousel - 10k POA\r\nsidewinder - 10k T&R\r\nputput- chiron extended\nbad line\n- nameless\nwizard -   \n"
    assert packs("maplist", text, "carousel", "SIDEWINDER", "putput", "wizard", "bad line", "missing") == [
        "10k POA", "10k T&R", "chiron extended", "-", "-", "-"]


def test_names(packs):
    # (no library entry for these files: maplist.txt's, then the file's)
    assert packs("names", "cex", "bloodgulch") == ["1 Abyss|Halo CE+"]
    assert packs("names", "cex", "wizard") == ["1 Gothra|Halo CE+"]
    assert packs("names", "cex", "big_map_v2") == ["0 Big Map V2|Halo CE+"]
    assert packs("names", "foo", "bloodgulch") == ["0 Bloodgulch|the maps_foo pack"]
    # (a host's fingerprint the library knows names its map, for a player
    # without the pack)
    fingerprint, row = next((value, row) for value, row in xbox_mod_maps.load()[1] if row["file"] == "bloodgulch")
    assert packs("names", "cex", "bloodgulch", f"{fingerprint:08x}") == [f"2 {row['name']}|Halo CE+"]
    # (... only for its own pack's file of that name)
    assert packs("names", "foo", "bloodgulch", f"{fingerprint:08x}") == ["0 Bloodgulch|the maps_foo pack"]


def test_list(packs):
    # (each pack's multiplayer maps; not its ui.map, campaign level or other
    # files, nor maps_de's)
    assert sorted(packs("list")) == ["cex bloodgulch", "cex wizard", "foo bloodgulch"]


def test_listing(packs):
    # (a host's announcement of a pack's map: its name, pack and tag; none
    # for another family's)
    assert packs("listing", "bloodgulch@cex") == ["1 Abyss|Halo CE+|CE+"]
    assert packs("listing", "bloodgulch@foo") == ["1 Bloodgulch|maps_foo|FOO"]
    assert packs("listing", "bloodgulch") == ["0 ||"]
    assert packs("listing", "timberland@ce") == ["0 ||"]


def test_tags(packs):
    assert packs("tag", "cex") == ["CE+|Halo CE+"]
    assert packs("tag", "foo") == ["FOO|the maps_foo pack"]


def test_pack_menus(packs):
    assert packs("menus", "cex_museum", menus="") == ["- 0", "cex_museum"]
    # (the pack's menus: a level only the pack has is the pack's; one the
    # game's maps have stays theirs)
    assert packs("menus", "cex_museum", "a10", "levels\\a10\\a10", "nothing", menus="cex") == [
        "cex 0", "cex_museum@cex", "a10", "levels\\a10\\a10", "nothing"]
    assert packs("menus", menus="foo") == ["- 0"]
    assert packs("menus", menus="de") == ["- 0"]


@pytest.mark.skipif(not os.environ.get("HALO_CEX_MAPS"), reason="HALO_CEX_MAPS (a folder of Halo CE+ X's maps) not set")
def test_owners_cex_files_are_the_librarys():
    folder = Path(os.environ["HALO_CEX_MAPS"])
    _, maps = xbox_mod_maps.load()
    known = {(row["pack"], row["file"]): value for value, row in maps}
    for path in sorted(folder.glob("*.map")):
        assert known[("cex", path.stem)] == xbox_mod_maps.fingerprint(path), path.name


# ---------- a pack is not its folder's name

def test_library_aliases_are_checked(tmp_path):
    def library(aliases):
        path = tmp_path / "library.json"
        path.write_text(__import__("json").dumps({"packs": {"cex": {"name": "Halo CE+", "tag": "CE+", "aliases": aliases},
                                                            "foo": {"name": "Foo", "tag": "FOO"}}, "maps": []}))
        return path
    assert xbox_mod_maps.load(library(["ceplus", "ce+"]))[0]["cex"]["aliases"] == "ceplus,ce+"
    for bad in ("ceplus", ["Ce Plus"], ["ce/plus"], ["ce"], ["foo"], ["x" * 17], [1]):
        with pytest.raises(ValueError):
            xbox_mod_maps.load(library(bad))


def test_the_games_cex_knows_its_folders():
    packs, _ = xbox_mod_maps.load()
    assert packs["cex"]["aliases"].split(",") == ["ceplus", "ce+"]


@pytest.fixture(scope="module")
def renamed(tmp_path_factory):
    """a library of pack tst (aliases tstplus and ts+), whose maps a, b, c are
    in folders of other names, with files of the library's fingerprints"""
    root_work = tmp_path_factory.mktemp("renamed")
    root = root_work / "d"
    files = {"a": b"\x0a" * 5000, "b": b"\x0b" * 6000, "c": b"\x0c" * 7000, "e": b"\x0e" * 8000, "ui": b"\x0f" * 9000}
    for folder in ("maps", "maps_tstplus", "maps_ts+", "maps_weird", "maps_tst", "maps_other", "maps_longfoldername"):
        (root / folder).mkdir(parents=True)
    entries = []
    # (a in an alias folder, b in a folder of any name, c unknown to the
    # library in the other alias, e in the pack's own folder and an alias, ui
    # in a folder of any name)
    for name, folder, kind in (("a", "maps_tstplus", 1), ("b", "maps_weird", 1), ("c", "maps_ts+", 1),
                               ("e", "maps_tst", 1), ("ui", "maps_weird", 2)):
        path = root / folder / f"{name}.map"
        path.write_bytes(xbox_header(kind) + files[name])
        if name in "abe" or name == "ui":
            entries.append((xbox_mod_maps.fingerprint(path), name))
    (root / "maps_tstplus" / "e.map").write_bytes(xbox_header(1) + files["e"])
    (root / "maps_other" / "d.map").write_bytes(xbox_header(1) + b"\x0d" * 100)
    (root / "maps_longfoldername" / "f.map").write_bytes(xbox_header(1) + b"\x0e" * 100)
    header = ('static struct xbox_mod_pack const xbox_mod_packs[] =\n{\n\t{ "tst", "tstplus,ts+", "Test Pack", "TST", "T", '
              '"Credits", "" },\n};\nstatic struct xbox_mod_map const xbox_mod_maps[] =\n{\n' +
              "".join(f'\t{{ 0x{value:08x}UL, "tst", "{name}", "Map {name.upper()}", "1", "T", "" }},\n'
                      for value, name in entries) + "};\n")
    work, binary = build(tmp_path_factory, header)
    run = runner(binary, root)
    run.fingerprints = dict((name, value) for value, name in entries)
    return run


def test_a_pack_is_found_in_any_folder(renamed):
    # (the pack's own folder, its aliases, and any maps_* folder by the
    # library's fingerprint; a file the library doesn't know only in the
    # pack's folders)
    assert sorted(renamed("list")) == ["other d", "tst a", "tst b", "tst c", "tst e"]
    assert renamed("present", "tst", "a", "b", "c", "e", "d", "zzz") == ["1", "1", "1", "1", "0", "0"]


def test_a_pack_map_has_one_name_whatever_the_folder(renamed):
    for file in ("a", "b", "c", "e"):
        assert renamed("path", f"{file}@tst") != ["-"]
    assert renamed("path", "a@tst", "b@tst", "c@tst") == [
        "d:\\maps_tstplus\\a.map", "d:\\maps_weird\\b.map", "d:\\maps_ts+\\c.map"]
    # (the pack's own folder first)
    assert renamed("path", "e@tst") == ["d:\\maps_tst\\e.map"]
    # (not the library's map by another file name)
    assert renamed("path", "d@tst") == ["d:\\maps_tst\\d.map"]
    assert renamed("fingerprint", "tst", "b") == [f"{renamed.fingerprints['b']:08x}"]


def test_a_pack_map_is_announced_by_the_library_pack(renamed):
    assert renamed("listing", "b@tst") == ["1 Map B|Test Pack|TST"]
    assert renamed("names", "tst", "b") == ["2 Map B|Test Pack"]
    # (a folder the library doesn't know keeps its name)
    assert renamed("listing", "d@other") == ["1 D|maps_other|OTHER"]


def test_pack_menus_found_by_alias_and_fingerprint(renamed):
    assert renamed("menus", menus="tst") == ["tst 1"]
    assert renamed("menus", menus="tstplus") == ["tst 1"]
    assert renamed("menus", menus="other") == ["- 0"]
    assert renamed("path", "ui", menus="tstplus") == ["d:\\maps_weird\\ui.map"]
