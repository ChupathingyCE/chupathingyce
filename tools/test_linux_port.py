"""Tests for the native Linux build tooling (port/linux, tools/linux_*.py)."""

import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from tools import linux_build, linux_link_check, linux_msvc_semantics


# ---------- MSVC semantics header


def test_strip_cplusplus_keeps_c_branches_only():
    text = "\n".join([
        "a",
        "#ifdef __cplusplus", "cpp1", "#else", "c1", "#endif",
        "#if FOO", "b", "#else", "c", "#endif",
        "#ifndef __cplusplus", "c2", "#else", "cpp2", "#endif",
        "#if defined(__cplusplus)", "cpp3", "#if X", "cpp4", "#endif", "#endif",
        "#ifdef _WIN64", "win64", "#endif",
        "end",
    ])
    assert linux_msvc_semantics.strip_cplusplus(text).split() == ["a", "c1", "b", "c", "c2", "end"]


def write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="latin-1")
    return path


def test_scan_finds_prototype_scope_tags_and_inline_functions(tmp_path):
    header = write(tmp_path / "a.h", """
        struct location;
        void f(struct scenario *s, union color *c);
        /* struct commented_out */
        __inline long prototyped(long x) { return x; }
        __inline long private_helper(long x) { return x; }
        long prototyped(long x);
        #ifdef __cplusplus
        __inline long cpp_only(long x) { return x; }
        #endif
    """)
    files = [header]
    tags = linux_msvc_semantics.scan_tags(files)
    assert tags == {"location": {"struct"}, "scenario": {"struct"}, "color": {"union"}}
    assert linux_msvc_semantics.scan_inline_functions(files, all_inlines=False) == {"prototyped"}
    assert linux_msvc_semantics.scan_inline_functions(files, all_inlines=True) == {
        "prototyped", "private_helper",
    }


def test_render_skips_tags_used_as_both_struct_and_union():
    text = linux_msvc_semantics.render(
        {"a": {"struct"}, "b": {"union"}, "mixed": {"struct", "union"}}, {"f"}
    )
    assert "struct a;" in text
    assert "union b;" in text
    assert "mixed" not in text
    assert "#pragma weak f" in text


# ---------- Xbox SDK declarations (port/include/xdk)


XDK_INCLUDE = Path(__file__).resolve().parent.parent / "port" / "include" / "xdk"


def test_xdk_headers_use_the_sdk_spellings():
    # each port gives the SDK's keywords and names its own meaning (its
    # prefix header), so none of one port's must be written into them
    for header in sorted(XDK_INCLUDE.glob("*.h")):
        text = re.sub(r"/\*.*?\*/", "", header.read_text(encoding="utf-8"), flags=re.S)
        assert "__attribute__" not in text, header.name
        assert not re.findall(r"\bhalo_\w+", text), header.name


@pytest.mark.skipif(shutil.which("clang") is None, reason="clang is needed to compile the headers")
def test_xdk_headers_compile_for_the_game(tmp_path):
    root = XDK_INCLUDE.parent.parent.parent
    source = write(tmp_path / "unit.c", "".join(
        f"#include <{name}>\n" for name in ("xtl.h", "xbdm.h", "xkbd.h", "d3d8perf.h")
    ))
    flags = [flag for flag in linux_build.LINUX_ABI_FLAGS + linux_build.GAME_FLAGS if flag != "-w"]
    for defines in ([], ["-DDEBUG_KEYBOARD"], ["-DNOD3D", "-DNODSOUND"]):
        subprocess.run(
            ["clang", *flags, *defines, "-Werror", "-fsyntax-only",
             "-include", str(root / "port/linux/include/halo_linux_prefix.h"),
             "-I", str(root / "port/linux/include"), "-idirafter", str(XDK_INCLUDE), str(source)],
            check=True,
        )


def pdb_writer():
    """An xdk_headers Writer over an empty type table: basic types only."""
    from tools import pdb200_types, xdk_headers
    pdb = object.__new__(pdb200_types.Pdb)
    pdb.types, pdb.aggregates = {}, {}
    return xdk_headers, xdk_headers.Writer(pdb)


def test_xdk_headers_regroups_anonymous_members():
    # the PDB lists an anonymous structure's or union's members in their
    # container, at their offsets
    xdk_headers, writer = pdb_writer()
    member = xdk_headers.Member
    ulong, int64 = 0x22, 0x13
    union = writer.render(writer.group(
        [member("LowPart", ulong, 0), member("HighPart", ulong, 4), member("QuadPart", int64, 0)], True), "", "u")
    assert union == ["struct {", "    unsigned long LowPart;", "    unsigned long HighPart;", "};",
                     "__int64 QuadPart;"]
    structure = writer.render(writer.group(
        [member("Tag", ulong, 0), member("Low", ulong, 4), member("High", ulong, 8),
         member("Whole", int64, 4), member("After", ulong, 12)], False), "", "s")
    assert structure == ["unsigned long Tag;", "union {", "    struct {", "        unsigned long Low;",
                         "        unsigned long High;", "    };", "    __int64 Whole;", "};",
                         "unsigned long After;"]


# ---------- weak reference link check


@pytest.mark.skipif(shutil.which("clang") is None or shutil.which("nm") is None,
                    reason="clang and nm are needed to build test objects")
def test_link_check_rejects_undefined_weak_references(tmp_path):
    def compile_object(name: str, text: str) -> Path:
        source = write(tmp_path / f"{name}.c", text)
        output = tmp_path / f"{name}.o"
        subprocess.run(["clang", "-c", "-o", str(output), str(source)], check=True)
        return output

    caller = compile_object("caller", "#pragma weak helper\nint helper(int);\nint call(void) { return helper(1); }\n")
    local = compile_object("local", "static int helper(int x) { return x; }\nint use(void) { return helper(2); }\n")
    provider = compile_object("provider", "#pragma weak helper\nint helper(int x) { return x; }\n")

    def check(*objects: Path) -> int:
        response = write(tmp_path / "objects.rsp", "\n".join(f"'{o}'" for o in objects))
        return subprocess.run(
            [sys.executable, linux_link_check.__file__, str(response)], capture_output=True
        ).returncode

    # a file-local copy elsewhere does not satisfy the reference
    assert check(caller, local) == 1
    assert check(caller, local, provider) == 0


# ---------- game sources


def test_game_sources_leave_out_the_excluded(tmp_path, monkeypatch):
    write(tmp_path / "source" / "a.c", "")
    write(tmp_path / "source" / "zlib" / "b.c", "")
    write(tmp_path / "source" / "zlib" / "example.c", "")
    write(tmp_path / "source" / "a.h", "")
    monkeypatch.chdir(tmp_path)
    config = {"game": {"root": "source", "exclude": ["source/zlib/example.c"],
                       "defines": ["DEBUG"], "include_dirs": ["source", "source/saved games"]}}
    assert linux_build.game_sources(config) == [Path("source/a.c"), Path("source/zlib/b.c")]
    assert linux_build.game_defines_and_includes(config) == '-DDEBUG -Isource -I"source/saved games"'


# ---------- menus (port/assets/menus)


MENUS = Path(__file__).resolve().parent.parent / "port/assets/menus"
MENU_ATTRIBUTES = {
    "menus": {"root"},
    "bitmap": {"name", "width", "height", "frames", "platform"},
    "frame": {"png", "map", "index", "width", "height", "x", "y", "platform"},
    "strings": {"name", "platform"},
    "string": {"text", "platform"},
    "widget": {"name", "type", "controller", "flags", "bitmap", "text", "strings", "values", "setting", "font",
               "color", "align", "description", "string_list", "list_flags", "text_flags", "header_bitmap",
               "footer_bitmap", "header_bounds", "footer_bounds", "child_controller", "x", "y", "left", "top",
               "width", "height", "text_x", "text_y", "auto_close", "auto_close_fade", "string_index", "platform"},
    "child": {"widget", "controller", "x", "y", "platform"},
    "on": {"event", "run", "script", "open", "replace", "close", "widget", "focus", "reload", "sound", "otherwise",
           "label", "back", "branch", "platform"},
    "data": {"input", "platform"},
    "conditional": {"widget", "if_failed", "platform"},
    "replace": {"search", "function", "platform"},
}


def c_strings(path: Path, start: str, end: str) -> list:
    """the string literals of a C file between two markers"""
    text = path.read_text(encoding="latin-1")
    text = text[text.index(start):]
    return re.findall(r'"([^"\\]*)"', text[:text.index(end)])


# menu_files.c's attributes read as whole numbers (its tables' { name, NULL, &long })
MENU_INTEGER_ATTRIBUTES = {"auto_close", "auto_close_fade", "height", "index", "left", "string_index", "text_x",
                           "text_y", "top", "width", "x", "y"}


def test_menus_are_well_formed():
    """What port/linux/src/menu_files.c and port/linux/game/menu_tags.c check
    when the game loads them, but the map's own names (paths with backslashes),
    which only the map has."""
    import json
    import xml.etree.ElementTree as ElementTree

    root = MENUS.parent.parent.parent
    tags = root / "port/linux/game/menu_tags.c"
    events = c_strings(tags, "event_names[] =", "};")
    flags = c_strings(tags, "widget_flag_names[] =", "};")
    functions = set(c_strings(root / "source/interface/ui_widget_event_handler_functions.c", '\t{\n\t\t"NULL",', "}"))
    functions |= set(c_strings(tags, "port_function_names[] =", "};"))
    inputs = set(c_strings(tags, "game_data_input_names[] =", "};"))
    inputs |= set(c_strings(tags, "port_game_data_input_names[] =", "};"))
    listed = json.loads((MENUS / "menus.json").read_text())["files"]
    files = sorted(MENUS.rglob("*.xml"))
    assert sorted(path.relative_to(MENUS).as_posix() for path in files) == \
        sorted(name for name in listed if name.endswith(".xml"))
    widgets, bitmaps, strings, references, roots = {}, {}, {}, [], []
    for path in files:
        tree = ElementTree.parse(path)
        assert tree.getroot().tag == "menus", path
        if tree.getroot().get("root"):
            roots.append(tree.getroot().get("root"))
        for element in tree.getroot().iter():
            where = f"{path.name}: <{element.tag} {element.get('name', '')}>"
            assert element.tag in MENU_ATTRIBUTES, where
            assert set(element.attrib) <= MENU_ATTRIBUTES[element.tag], where
            assert element.get("platform") in (None, "desktop", "android"), where
            # (menu_files.c's whole numbers, which the tags keep in shorts,
            # its true/false attributes, and no text but whitespace outside
            # <string>s)
            for attribute in MENU_INTEGER_ATTRIBUTES & set(element.attrib):
                value = element.get(attribute)
                assert re.fullmatch(r"-?[0-9]+", value) and -32768 <= int(value) <= 32767, f"{where} {attribute}"
            for attribute in ("back", "branch", "if_failed"):
                assert element.get(attribute) in (None, "true", "false"), f"{where} {attribute}"
            assert not (element.text or "").strip() and not (element.tail or "").strip(), where
            if element.tag == "bitmap":
                bitmaps[element.get("name")] = element
                for frame in element.iter("frame"):
                    assert (frame.get("png") is None) != (frame.get("map") is None), where
                    if frame.get("map"):
                        assert "\\" in frame.get("map") and int(frame.get("index")) >= 0, where
                        continue
                    assert frame.get("png") in listed, where
                    with (MENUS / frame.get("png")).open("rb") as png:
                        header = png.read(24)
                    width, height = int.from_bytes(header[16:20], "big"), int.from_bytes(header[20:24], "big")
                    logical = int(frame.get("width")), int(frame.get("height"))
                    assert width % logical[0] == 0 and width // logical[0] == height // logical[1], where
            elif element.tag == "strings":
                strings[element.get("name")] = element
            elif element.tag == "widget":
                name = element.get("name")
                assert name and name not in widgets, where
                widgets[name] = element
                assert element.get("type", "container") in ("container", "text", "spinner", "column_list"), where
                assert set(element.get("flags", "").split()) <= set(flags), where
                children = element.findall("widget") + element.findall("child")
                if element.get("type") == "spinner":
                    assert len(children) in (0, 1, 3), where
                if element.get("strings") or "items_from_strings" in element.get("list_flags", ""):
                    assert element.get("type") == "spinner" and not children, where
                if element.get("setting"):
                    assert len(element.get("strings").split("|")) == len(element.get("values").split("|")), where
                for attribute in ("bitmap", "header_bitmap", "footer_bitmap"):
                    references.append((where, bitmaps, element.get(attribute)))
                references.append((where, strings, element.get("string_list")))
                references.append((where, widgets, element.get("description")))
            elif element.tag == "child" or element.tag == "conditional":
                references.append((where, widgets, element.get("widget")))
            elif element.tag == "on":
                assert set(element.get("event").split()) <= set(events), where
                run = element.get("run")
                assert run is None or run in functions or run.startswith("unwired "), where
                assert element.get("otherwise") is None or run, where
                for attribute in ("open", "replace", "focus", "widget", "otherwise"):
                    references.append((where, widgets, element.get(attribute)))
            elif element.tag == "data":
                assert element.get("input") in inputs or element.get("input").startswith("unwired "), where
    assert all(root_name in widgets for root_name in roots)
    for where, names, name in references:
        if name and "\\" not in name:
            assert name in names, f"{where} {name}"


def test_menu_settings_exist():
    """Every setting a menu's spinner sets is one of config.toml's
    (port/linux/src/port_config.c) or of the profile (menu_functions.c), and
    the keyboard's controls are the same in the settings, in the controls'
    screen and in the input code."""
    root = MENUS.parent.parent.parent
    config = (root / "port/linux/src/port_config.c").read_text()
    functions = (root / "port/linux/game/menu_functions.c").read_text()
    known = set(re.findall(r'^\t\{ "([a-z_]+\.[a-z_]+)", _config_', config, re.M))
    profile = set(re.findall(r'\{ "(profile\.[a-z_]+)", \d', functions))
    for path in (MENUS / "ce").glob("*.xml"):
        for setting in re.findall(r'setting="([^"]+)"', path.read_text()):
            assert setting in known | profile, f"{path.name}: {setting}"
    controls = set(re.findall(r'"(controls\.[a-z_]+)"', (root / "port/linux/src/xinput_sdl.c").read_text()))
    assert controls == {name for name in known if name.startswith("controls.")}
    assert controls == set(re.findall(r'\{ "(controls\.[a-z_]+)", L"', functions))


def test_default_brokers_are_brokers_txt():
    """The brokers a game uses with no brokers.txt beside its config.toml
    (p2p_signal.c's DEFAULT_BROKERS) are port/assets/network/brokers.txt's."""
    root = MENUS.parent.parent.parent
    listed = [line.split("#", 1)[0].strip()
              for line in (root / "port/assets/network/brokers.txt").read_text().splitlines()]
    default = re.search(r'^#define DEFAULT_BROKERS "([^"]*)"',
                        (root / "port/linux/src/p2p_signal.c").read_text(), re.M).group(1)
    assert default.split(",") == [line for line in listed if line]


def test_p2p_signatures_and_listings(tmp_path):
    """internet play's Ed25519 (RFC 8032), the X25519 key of a seed, and the
    server browser's listings from host to browser (tools/p2p_lobby_check.c),
    built with the flags ninja gives the platform layer"""
    import shlex

    if not shutil.which("clang") or not shutil.which("ninja") or not Path("build.ninja").is_file():
        pytest.skip("needs clang, ninja and a configured build")
    command = subprocess.run(["ninja", "-t", "commands", "build/linux/obj/port/linux/src/p2p_crypto.o"],
                             capture_output=True, text=True, check=True).stdout.strip().splitlines()[-1]
    words = shlex.split(command)
    # (the compiler, and whatever runs it, ccache in CI: up to the first flag)
    while words and not words[0].startswith("-"):
        words = words[1:]
    flags = []
    skip = False
    for word in words:
        if skip:
            skip = False
        elif word in ("-MF", "-o", "-c"):
            skip = True
        elif word == "-MMD" or word.startswith("-flto") or word.startswith("-fprofile-use"):
            continue
        else:
            flags.append(word)
    program = tmp_path / "p2p_lobby_check"
    built = subprocess.run(["clang", *flags, "-O1", "-no-pie", "-Wl,--unresolved-symbols=ignore-all", "-o",
                            str(program), "tools/p2p_lobby_check.c", "port/linux/src/p2p_crypto.c",
                            "port/linux/src/p2p_lobby.c", "port/third_party/monocypher/monocypher.c",
                            "port/third_party/monocypher/monocypher-ed25519.c"],
                           capture_output=True, text=True)
    assert built.returncode == 0, built.stderr[-4000:]
    result = subprocess.run([str(program)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout
    assert "PASS" in result.stdout


# ---------- the tag validator (port/linux/game/tag_validate.c)

RETAIL_MAPS = Path("assets/maps")
MAP_VALIDATE = Path("build/linux/map_validate")


def _retail_maps():
    maps = sorted(RETAIL_MAPS.glob("*.map"))
    if not maps or not MAP_VALIDATE.is_file():
        pytest.skip("needs the retail maps in assets/maps and ninja linux's build/linux/map_validate")
    return maps


def test_retail_maps_need_no_corrections():
    """every retail map in assets/maps passes the tag validator, each of its
    structure bsps too, with no correction: the schemas (tag_schema_*.c)
    say what the game's own maps hold"""
    maps = _retail_maps()
    result = subprocess.run([str(MAP_VALIDATE), "--strict", *map(str, maps)], capture_output=True, text=True,
                            timeout=1800)
    assert result.returncode == 0, result.stdout[-6000:]


def test_tag_validator_survives_damaged_maps():
    """retail maps with a few of their tags' words changed at random: the
    validator never crashes or hangs, and a map it lets through is clean
    when checked again"""
    maps = _retail_maps()
    chosen = [path for path in maps if path.stem in ("bloodgulch", "a10", "ui")] or maps[:1]
    result = subprocess.run([str(MAP_VALIDATE), "--fuzz", "300", "--seed", "1", *map(str, chosen)],
                            capture_output=True, text=True, timeout=1800)
    assert result.returncode == 0, (result.stdout + result.stderr)[-6000:]



# ---------- the disc image importer (port/linux/src/xiso.c)


# The importer is built for 32-bit Windows and Linux, but its disc reading is
# plain POSIX file I/O, so it can be compiled for the test host with a small
# harness. platform.h is a stub: the importer only uses platform_log from it,
# and that keeps the Xbox SDK headers out of the test.
XISO_HARNESS = r"""
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "posix.h"
#include "xiso.h"

void platform_log(const char *format, ...)
{
	(void)format;
}

int posix_seek(int descriptor, posix_long offset_low, posix_long offset_high, int whence,
	posix_ulong *position_low, posix_ulong *position_high)
{
	off_t offset = (off_t)(unsigned int)offset_low | ((off_t)(unsigned int)offset_high << 32);
	off_t result = lseek(descriptor, offset, whence);

	if (result < 0)
		return -1;
	*position_low = (posix_ulong)result;
	*position_high = (posix_ulong)((unsigned long long)result >> 32);
	return 0;
}

int posix_fstat(int descriptor, struct posix_file_information *information)
{
	struct stat status;

	if (fstat(descriptor, &status) != 0)
		return -1;
	memset(information, 0, sizeof(*information));
	information->size_low = (posix_ulong)((unsigned long long)status.st_size & 0xFFFFFFFFu);
	information->size_high = (posix_ulong)((unsigned long long)status.st_size >> 32);
	return 0;
}

int posix_make_directory(const char *path)
{
	return mkdir(path, 0755);
}

int main(int argc, char **argv)
{
	if (argc < 3)
		return 2;
	if (!strcmp(argv[1], "probe"))
	{
		struct xiso_probe_result result;
		int read = xiso_probe(argv[2], &result);

		printf("read=%d halo=%d maps=%d data=%llu file=%llu complete=%d\n",
			read, result.halo, result.map_count, result.data_size, result.file_size, result.complete);
		return 0;
	}
	if (!strcmp(argv[1], "extract"))
	{
		char error[512];
		int ok;

		memset(error, 0, sizeof(error));
		ok = xiso_extract_maps(argv[2], argv[3], NULL, NULL, error, sizeof(error));
		printf("ok=%d error=%s\n", ok, error);
		return ok ? 0 : 1;
	}
	return 2;
}
"""

REPOSITORY = Path(__file__).resolve().parent.parent
XISO_SOURCE = REPOSITORY / "port" / "linux" / "src" / "xiso.c"


@pytest.fixture(scope="module")
def xiso_harness(tmp_path_factory):
    """The importer compiled for this host, or a skip where there is no clang."""
    if shutil.which("clang") is None:
        pytest.skip("clang is needed to compile the disc image importer")
    directory = tmp_path_factory.mktemp("xiso")
    (directory / "platform.h").write_text(
        "#ifndef TEST_PLATFORM_H\n#define TEST_PLATFORM_H\n"
        "void platform_log(const char *format, ...);\n#endif\n",
        encoding="latin-1",
    )
    shutil.copy2(XISO_SOURCE, directory / "xiso_under_test.c")
    (directory / "harness.c").write_text(XISO_HARNESS, encoding="latin-1")
    binary = directory / "xiso_harness"
    built = subprocess.run(
        [
            "clang", "-std=gnu11", "-O0", "-g", "-Wall",
            "-I", str(directory),
            "-I", str(REPOSITORY / "port" / "linux" / "src"),
            "-o", str(binary), str(directory / "harness.c"), str(directory / "xiso_under_test.c"),
        ],
        capture_output=True, text=True,
    )
    if built.returncode != 0:
        # some hosts (a macOS SDK newer than their clang's linker) cannot
        # link an executable although they compile the source; the tests then
        # have nothing to run. CI's Arch Linux container links it.
        pytest.skip("cannot link the disc image harness on this host:\n" + built.stderr.strip())
    return binary


SECTOR_SIZE = 2048


def directory_table(entries) -> bytes:
    """An XDVDFS directory: entries (sector, size, attributes, name) chained
    through their right-subtree offsets, as the Xbox disc format stores them."""
    offsets = []
    offset = 0
    for _, _, _, name in entries:
        offsets.append(offset)
        offset += (14 + len(name) + 3) & ~3
    table = bytearray(offset)
    for index, (sector, size, attributes, name) in enumerate(entries):
        at = offsets[index]
        table[at + 4:at + 8] = sector.to_bytes(4, "little")
        table[at + 8:at + 12] = size.to_bytes(4, "little")
        table[at + 12] = attributes
        table[at + 13] = len(name)
        table[at + 14:at + 14 + len(name)] = name.encode("ascii")
        if index + 1 < len(offsets):
            table[at + 2:at + 4] = (offsets[index + 1] // 4).to_bytes(2, "little")
    return bytes(table)


def halo_image(files=None, maps_name="maps", ui=True) -> bytes:
    """A small but valid XDVDFS image holding a maps folder."""
    if files is None:
        files = {"ui.map": b"ui" * 16, "a10.map": b"a10" * 32}
    if not ui:
        files = {name: data for name, data in files.items() if name != "ui.map"}
    payloads = list(files.items())
    sectors = []
    cursor = 35
    for _, payload in payloads:
        sectors.append(cursor)
        cursor += max(1, (len(payload) + SECTOR_SIZE - 1) // SECTOR_SIZE)
    maps_table = directory_table(
        [(sectors[i], len(payloads[i][1]), 0, payloads[i][0]) for i in range(len(payloads))]
    )
    root_table = directory_table([(34, len(maps_table), 0x10, maps_name)])
    assert len(root_table) <= SECTOR_SIZE
    image = bytearray(cursor * SECTOR_SIZE)
    descriptor = 32 * SECTOR_SIZE
    image[descriptor:descriptor + 20] = b"MICROSOFT*XBOX*MEDIA"
    image[descriptor + 0x7EC:descriptor + 0x7EC + 20] = b"MICROSOFT*XBOX*MEDIA"
    image[descriptor + 20:descriptor + 24] = (33).to_bytes(4, "little")
    image[descriptor + 24:descriptor + 28] = len(root_table).to_bytes(4, "little")
    image[33 * SECTOR_SIZE:33 * SECTOR_SIZE + len(root_table)] = root_table
    image[34 * SECTOR_SIZE:34 * SECTOR_SIZE + len(maps_table)] = maps_table
    for (_, payload), sector in zip(payloads, sectors):
        image[sector * SECTOR_SIZE:sector * SECTOR_SIZE + len(payload)] = payload
    return bytes(image)


def probe(xiso_harness, path):
    result = subprocess.run([xiso_harness, "probe", path], capture_output=True, text=True, check=True)
    return {key: int(value) for key, value in (part.split("=") for part in result.stdout.split())}


def test_xiso_probe_recognises_a_halo_disc(xiso_harness, tmp_path):
    image = tmp_path / "halo.iso"
    image.write_bytes(halo_image())
    fields = probe(xiso_harness, image)
    assert fields["read"] == 1
    assert fields["halo"] == 1
    assert fields["maps"] == 2
    assert fields["complete"] == 1
    assert fields["file"] == image.stat().st_size


def test_xiso_probe_rejects_a_disc_without_halo_maps(xiso_harness, tmp_path):
    image = tmp_path / "other.iso"
    image.write_bytes(halo_image(maps_name="content"))
    fields = probe(xiso_harness, image)
    assert fields["read"] == 1
    assert fields["halo"] == 0


def test_xiso_probe_requires_ui_map(xiso_harness, tmp_path):
    image = tmp_path / "nohud.iso"
    image.write_bytes(halo_image(ui=False))
    assert probe(xiso_harness, image)["halo"] == 0


def test_xiso_probe_marks_a_truncated_image_incomplete(xiso_harness, tmp_path):
    image = tmp_path / "cut.iso"
    image.write_bytes(halo_image()[:-SECTOR_SIZE])
    fields = probe(xiso_harness, image)
    assert fields["halo"] == 1
    assert fields["complete"] == 0


def test_xiso_probe_ignores_a_file_that_is_not_a_disc(xiso_harness, tmp_path):
    image = tmp_path / "notes.txt"
    image.write_bytes(b"not an Xbox disc image" * 64)
    fields = probe(xiso_harness, image)
    assert fields["read"] == 1
    assert fields["halo"] == 0


def test_xiso_extracts_the_maps_folder(xiso_harness, tmp_path):
    files = {"ui.map": b"ui-data", "a10.map": b"a10-data"}
    image = tmp_path / "halo.iso"
    image.write_bytes(halo_image(files=files))
    destination = tmp_path / "data"
    destination.mkdir()
    subprocess.run([xiso_harness, "extract", image, destination], check=True)
    for name, payload in files.items():
        assert (destination / "maps" / name).read_bytes() == payload
    assert not (destination / "maps.partial").exists()
