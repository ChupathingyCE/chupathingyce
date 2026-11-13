"""The map loader's fuzz targets (tools/tests/map_fuzz.c): an Xbox map's tags
through the tag validator, a Custom Edition or HaloMD map through its checks
and then its conversions, and a Custom Edition sound's Ogg Vorbis stream
through its decoder. Each is built from the 64-bit build's own units, with
its flags (from build.ninja: `python3 configure.py` first; the macOS build's
on a Mac, linux64's elsewhere, or MAP_FUZZ_BUILD's) plus libFuzzer,
AddressSanitizer and UndefinedBehaviorSanitizer.

As a test (pytest), each target runs on maps made here (small, whole, of no
game's data) and on the inputs in tools/tests/map_fuzz_regressions, then for
a bounded number of runs from them. Where clang has no libFuzzer, the tests
are skipped (they fail where HALO_SANITIZERS_REQUIRED=1, as in CI:
tools/fuzz_support.py).

For longer runs, on the maps in assets/ (read in place, inflated into a
scratch folder, never copied into the repository):

    python3 tools/test_map_fuzz.py --target xbox --seconds 1800 [--base MAP] [--corpus DIR]
    python3 tools/test_map_fuzz.py --target ce --seconds 1800 --base maps_ce/foo.map --resources maps_ce [--xbox-ui maps/ui.map]
    python3 tools/test_map_fuzz.py --target vorbis --seconds 600 --corpus DIR

With --base, each input is a list of changes to that map (map_fuzz.c), which
reaches far deeper into a real map than whole-file mutations. A crash leaves
its input in the working folder (crash-*); a regression input goes in
tools/tests/map_fuzz_regressions only if it holds no game's data.
"""

import argparse
import os
import struct
import time
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

try:
    import pytest
except ImportError:
    # (the longer runs, from the command line, without pytest: its decorators
    # do nothing)
    class pytest:  # noqa: N801
        @staticmethod
        def fixture(*arguments, **keywords):
            return arguments[0] if arguments and callable(arguments[0]) else (lambda function: function)

        class mark:  # noqa: N801
            @staticmethod
            def parametrize(*arguments, **keywords):
                return lambda function: function

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from tools import fuzz_support  # noqa: E402
from tools.test_network_messages import compile_command, object_command  # noqa: E402

# the build whose units and flags are used (ninja macos, or linux64)
BUILD = os.environ.get("MAP_FUZZ_BUILD", "macos" if sys.platform == "darwin" else "linux64")
HARNESS = ROOT / "tools" / "tests" / "map_fuzz.c"
REGRESSIONS = ROOT / "tools" / "tests" / "map_fuzz_regressions"
SANITIZE = ["-DXBOX_ADDRESS_SPACE_BASE=0x300000000000ULL", "-fsanitize=fuzzer-no-link,address,undefined", "-fno-sanitize-recover=all", "-fno-sanitize=alignment,float-cast-overflow", "-fno-omit-frame-pointer",
            "-O1", "-gline-tables-only"]
FUZZ_RUNS = int(os.environ.get("MAP_FUZZ_RUNS", "3000"))

VALIDATOR = ["port/linux/game/tag_validate.c", *sorted(
    str(path.relative_to(ROOT)) for path in (ROOT / "port/linux/game").glob("tag_schema*.c"))]
CUSTOM_EDITION = [
    "port/linux/game/ce_map_checks.c",
    "port/linux/game/ce_resources.c",
    "port/linux/game/ce_models.c",
    "port/linux/game/ce_bsp.c",
    "port/linux/game/ce_hud.c",
    "port/linux/game/ce_repairs.c",
    "port/linux/game/ce_functions.c",
    "source/bitmaps/bitmaps.c",
    "source/rasterizer/rasterizer_geometry.c",
    "source/rasterizer/rasterizer_swizzle.c",
    # (ce_hud.c's PNG button icons)
    *(f"port/third_party/zlib/{name}.c" for name in ("adler32", "crc32", "inffast", "inflate", "inftrees", "zutil")),
]
TARGETS = {
    "xbox": ("MAP_FUZZ_XBOX", VALIDATOR),
    "ce": ("MAP_FUZZ_CE", CUSTOM_EDITION),
    "vorbis": ("MAP_FUZZ_VORBIS", ["port/linux/game/ce_vorbis.c"]),
}


def libfuzzer_compiler(directory=None):
    """a clang that links libFuzzer with the sanitizers (Apple's does not
    ship it; Homebrew's LLVM and Linux's clang with its runtimes do)"""
    return fuzz_support.libfuzzer_compiler(os.environ.get("MAP_FUZZ_CC"))


def build(target, directory, cc):
    """the target's fuzzer, built in directory: its path"""
    define, sources = TARGETS[target]
    directory = Path(directory)
    objects = []
    words, _ = object_command(BUILD, "port/linux/game/tag_validate.c")

    def compile_unit(source, output, extra_source=None):
        command = compile_command(BUILD, source, output, {"flags": [f"-D{define}", *SANITIZE],
                                                          **({"source": extra_source} if extra_source else {})})
        # (the build's compiler, after a launcher if it has one: ccache in CI)
        if Path(command[0]).name in ("ccache", "sccache"):
            command = command[1:]
        command[0] = cc
        subprocess.run(command, cwd=ROOT, check=True)

    for source in sources:
        output = directory / (Path(source).stem + ".o")
        compile_unit(source, output)
        objects.append(output)
    # the harness, rewritten for LP64 as the game's units are, compiled as one
    rewritten = directory / "map_fuzz.c"
    subprocess.run([sys.executable, str(ROOT / "tools" / "lp64_rewrite.py"), "--output", str(rewritten),
                    str(HARNESS)], cwd=ROOT, check=True)
    harness = directory / "map_fuzz.o"
    compile_unit("port/linux/game/tag_validate.c", harness, str(rewritten))
    objects.append(harness)
    binary = directory / f"map_fuzz_{target}"
    link_target = [word for word in words if word.startswith("--target=")]
    subprocess.run([cc, *link_target, "-fsanitize=fuzzer,address,undefined", "-o", str(binary),
                    *map(str, objects)], cwd=ROOT, check=True)
    return binary


# ---------- maps made here: whole and small, of no game's data

TAG_CACHE = {"xbox": 0x803A6000, "ce": 0x40440000}


def header(version, file_length, tag_data_offset, tag_data_size, name=b"fuzz"):
    data = bytearray(0x800)
    struct.pack_into("<4sii I ii", data, 0, b"daeh", version, file_length, 0, tag_data_offset, tag_data_size)
    data[0x20:0x20 + len(name)] = name
    data[0x40:0x40 + 12] = b"01.10.12.2276"[:12]
    struct.pack_into("<4s", data, 0x7FC, b"toof")
    return data


def xbox_map(name=b"fuzz"):
    """an Xbox map of one scenario tag, all of its blocks empty"""
    base = TAG_CACHE["xbox"]
    scenario_size = 0x5B0 + 0x200
    tags = bytearray(0x24 + 0x20)
    tags += name + bytes(8 - len(name) % 8)
    root = len(tags)
    tags += bytes(scenario_size)
    struct.pack_into("<IIIiiIiI4s", tags, 0, base + 0x24, 0xE1740000, 0, 1, 0, 0, 0, 0, b"sgat")
    struct.pack_into("<4s4s4sIIIII", tags, 0x24, b"rncs", b"\xff\xff\xff\xff", b"\xff\xff\xff\xff", 0xE1740000,
                     base + 0x44, base + root, 0, 0)
    data = header(5, 0x800 + len(tags), 0x800, len(tags)) + tags
    return bytes(data)


def ce_map(name=b"fuzz", sound=None, weapon_hud_statics=None, bsp_reference_unused=0):
    """a Custom Edition map of a scenario and one empty structure BSP, its
    scripts' syntax an empty data array; with sound, a list of permutations
    (compression, samples' size, flags, offset), a sound tag of its own (not
    indexed) of one pitch range of them, mono 22 kHz; with weapon_hud_statics,
    a weapon HUD whose one static element is that many bytes from the
    scenario's structure BSP's reference (whose unused word is
    bsp_reference_unused)"""
    base = TAG_CACHE["ce"]
    tag_count = 2 + (1 if sound else 0) + (1 if weapon_hud_statics is not None else 0)
    tags = bytearray(0x28)
    instances = len(tags)
    tags += bytes(0x20 * tag_count)
    names = len(tags)
    tags += name + b"\0"
    bsp_name = len(tags)
    tags += b"fuzz_bsp\0" + bytes((-(len(tags) + 9)) % 4)
    if sound:
        sound_name = len(tags)
        tags += b"fuzz_sound\0" + bytes((-(len(tags) + 11)) % 4)
    scenario = len(tags)
    tags += bytes(0x5B0)
    bsps = len(tags)
    tags += bytes(0x20)
    syntax = len(tags)
    tags += bytes(0x38)
    if weapon_hud_statics is not None:
        # (weapon_hud_interface_definitions.h: as far as its overlays' block)
        weapon_hud = len(tags)
        tags += bytes(0x9C)
        struct.pack_into("<iI", tags, weapon_hud + 0x60, 1, base + bsps + weapon_hud_statics)
        struct.pack_into("<4s4s4sIIIII", tags, instances + 0x20 * (tag_count - 1), b"ihpw", b"\xff\xff\xff\xff",
                         b"\xff\xff\xff\xff", 0xE1740000 + tag_count - 1 + (tag_count - 1 << 16), base + bsp_name,
                         base + weapon_hud, 0, 0)
    if sound:
        # (sound_definitions.h: the header, its pitch range, its permutations)
        sound_header = len(tags)
        tags += bytes(0xA4)
        sound_range = len(tags)
        tags += bytes(0x48)
        sound_permutations = len(tags)
        tags += bytes(0x7C * len(sound))
        struct.pack_into("<h", tags, sound_header + 0x6E, sound[0][0])
        struct.pack_into("<iII", tags, sound_header + 0x98, 1, base + sound_range, 0)
        struct.pack_into("<h", tags, sound_range + 0x2C, len(sound))
        struct.pack_into("<iII", tags, sound_range + 0x3C, len(sound), base + sound_permutations, 0)
        for index, (compression, size, flags, offset) in enumerate(sound):
            permutation = sound_permutations + index * 0x7C
            struct.pack_into("<hh", tags, permutation + 0x28, compression, -1)
            struct.pack_into("<iIIII", tags, permutation + 0x40, size, flags, offset, 0, 0)
        struct.pack_into("<4s4s4sIIIII", tags, instances + 0x40, b"!dns", b"\xff\xff\xff\xff", b"\xff\xff\xff\xff",
                         0xE1760002, base + sound_name, base + sound_header, 0, 0)
    tags += bytes((-len(tags)) % 16)
    tag_data_size = len(tags)
    bsp_offset = 0x800 + tag_data_size
    bsp = bytearray(0x18 + 0x288 + 0x40)
    struct.pack_into("<I", bsp, 0, base + 0x100000 + 0x18)
    struct.pack_into("<4s", bsp, 0x14, b"psbs")
    model_offset = bsp_offset + len(bsp)
    # the tag header: instances, the scenario, checksum, count, model parts,
    # model data offset, model parts, vertex data size, model data size
    struct.pack_into("<IIIIIIIII4s", tags, 0, base + instances, 0xE1740000, 0, tag_count, 0, model_offset, 0, 0, 0,
                     b"sgat")
    struct.pack_into("<4s4s4sIIIII", tags, instances, b"rncs", b"\xff\xff\xff\xff", b"\xff\xff\xff\xff", 0xE1740000,
                     base + names, base + scenario, 0, 0)
    struct.pack_into("<4s4s4sIIIII", tags, instances + 0x20, b"psbs", b"\xff\xff\xff\xff", b"\xff\xff\xff\xff",
                     0xE1750001, base + bsp_name, 0, 0, 0)
    # the scenario's scripts' syntax (a data array of no nodes of 0x14
    # bytes) and its structure BSP
    struct.pack_into("<iIII", tags, scenario + 0x474, 0x38, 0, 0, base + syntax)
    struct.pack_into("<hh", tags, syntax + 0x20, 0, 0x14)
    struct.pack_into("<iII", tags, scenario + 0x5A4, 1, base + bsps, 0)
    struct.pack_into("<iiII4sIiI", tags, bsps, bsp_offset, len(bsp), base + 0x100000, bsp_reference_unused, b"psbs",
                     base + bsp_name, 8, 0xE1750001)
    data = header(609, model_offset, 0x800, tag_data_size) + tags + bsp
    return bytes(data)


def ce_map_bsp_materials(lightmaps, materials, vertex_count=1):
    """ce_map's map with a structure BSP of that many lightmaps, every one of
    them the same block of that many materials, each of one vertex and one
    lightmap vertex (their uncompressed vertices the same 0x4c bytes), or of
    none (vertex_count 0)"""
    base = TAG_CACHE["ce"]
    data = bytearray(ce_map())
    tag_data_size = struct.unpack_from("<i", data, 0x14)[0]
    instances = struct.unpack_from("<I", data, 0x800)[0] - base
    scenario = struct.unpack_from("<I", data, 0x800 + instances + 0x14)[0] - base
    bsps = struct.unpack_from("<I", data, 0x800 + scenario + 0x5A8)[0] - base
    bsp_offset, _, bsp_base = struct.unpack_from("<iiI", data, 0x800 + bsps)
    del data[bsp_offset:]
    # (structure_bsp_definitions.h: the header, the structure (its lightmaps
    # at 0x104), the lightmaps (their materials at 0x14), the materials (their
    # vertices at 0xb0 and 0xc4, their uncompressed vertices at 0xd8))
    structure = 0x18
    lightmap_block = structure + 0x288
    material_block = lightmap_block + 0x20 * lightmaps
    vertices = material_block + 0x100 * materials
    bsp = bytearray(vertices + 0x50)
    struct.pack_into("<I", bsp, 0, bsp_base + structure)
    struct.pack_into("<4s", bsp, 0x14, b"psbs")
    struct.pack_into("<iI", bsp, structure + 0x104, lightmaps, bsp_base + lightmap_block)
    for lightmap in range(lightmaps):
        struct.pack_into("<iI", bsp, lightmap_block + 0x20 * lightmap + 0x14, materials, bsp_base + material_block)
    for material in range(materials):
        at = material_block + 0x100 * material
        struct.pack_into("<hhi", bsp, at + 0xB0, 0, 0, vertex_count)
        struct.pack_into("<hhi", bsp, at + 0xC4, 1, 0, vertex_count)
        struct.pack_into("<iIiI", bsp, at + 0xD8, 0x4C, 0, 0, bsp_base + vertices)
    data += bsp
    struct.pack_into("<i", data, 0x800 + bsps + 4, len(bsp))
    struct.pack_into("<i", data, 8, len(data))
    struct.pack_into("<I", data, 0x800 + 0x14, len(data))
    assert tag_data_size + 0x800 == bsp_offset
    return bytes(data)


def ce_map_of_tags(extra):
    """a Custom Edition map of a scenario, one empty structure BSP and the
    tags in extra (each a group and its data's size, zeroed): the map, and
    the offsets in it of its tag index and of each extra tag's data"""
    base = TAG_CACHE["ce"]
    tag_count = 2 + len(extra)
    tags = bytearray(0x28)
    instances = len(tags)
    tags += bytes(0x20 * tag_count)
    names = len(tags)
    tags += b"fuzz\0"
    bsp_name = len(tags)
    tags += b"fuzz_bsp\0" + bytes((-(len(tags) + 9)) % 4)
    scenario = len(tags)
    tags += bytes(0x5B0)
    bsps = len(tags)
    tags += bytes(0x20)
    syntax = len(tags)
    tags += bytes(0x38)
    datas = []
    for group, size in extra:
        datas.append(len(tags))
        tags += bytes(size + (-size) % 16)
    tags += bytes((-len(tags)) % 16)
    tag_data_size = len(tags)
    bsp_offset = 0x800 + tag_data_size
    bsp = bytearray(0x18 + 0x288 + 0x40)
    struct.pack_into("<I", bsp, 0, base + 0x100000 + 0x18)
    struct.pack_into("<4s", bsp, 0x14, b"psbs")
    model_offset = bsp_offset + len(bsp)
    struct.pack_into("<IIIIIIIII4s", tags, 0, base + instances, 0xE1740000, 0, tag_count, 0, model_offset, 0, 0, 0,
                     b"sgat")
    for index, (group, address) in enumerate([(b"scnr", base + scenario), (b"sbsp", 0)] +
                                             [(group, base + data) for (group, _), data in zip(extra, datas)]):
        struct.pack_into("<4s4s4sIIIII", tags, instances + 0x20 * index, group[::-1], b"\xff\xff\xff\xff",
                         b"\xff\xff\xff\xff", ((0xE174 + index) << 16) | index, base + names, address, 0, 0)
    # (the structure BSP's own name)
    struct.pack_into("<I", tags, instances + 0x20 + 0x10, base + bsp_name)
    struct.pack_into("<iIII", tags, scenario + 0x474, 0x38, 0, 0, base + syntax)
    struct.pack_into("<hh", tags, syntax + 0x20, 0, 0x14)
    struct.pack_into("<iII", tags, scenario + 0x5A4, 1, base + bsps, 0)
    struct.pack_into("<iiII4sIiI", tags, bsps, bsp_offset, len(bsp), base + 0x100000, 0, b"psbs", base + bsp_name,
                     8, 0xE1750001)
    data = header(609, model_offset, 0x800, tag_data_size) + tags + bsp
    return bytearray(data), {"instances": 0x800 + instances, "tags": [0x800 + data for data in datas]}


def seeds(target):
    if target == "xbox":
        return {"minimal_xbox.map": xbox_map()}
    if target == "ce":
        return {"minimal_ce.map": ce_map()}
    return {"empty.ogg": b"OggS"}


def inflate(path, output):
    """an Xbox map as the game reads it: its tags inflated after its header"""
    data = path.read_bytes()
    file_length = struct.unpack_from("<i", data, 8)[0]
    if len(data) != file_length:
        data = data[:0x800] + zlib.decompress(data[0x800:])
    output.write_bytes(data)
    return output


# ---------- the tests


@pytest.fixture(scope="module")
def built(tmp_path_factory):
    directory = tmp_path_factory.mktemp("map_fuzz")
    cc = libfuzzer_compiler(directory)
    if not cc:
        fuzz_support.skip("no clang with libFuzzer")
    if not (ROOT / "build.ninja").is_file():
        fuzz_support.skip("needs python3 configure.py")
    return {target: build(target, directory, cc) for target in TARGETS}, directory


def run(binary, inputs, *arguments, environment=None):
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:allocator_may_return_null=1", **(environment or {}))
    return subprocess.run([str(binary), *arguments, *map(str, inputs)], capture_output=True, text=True, env=env,
                          timeout=1800)


@pytest.mark.parametrize("target", TARGETS)
def test_seeds_and_regressions(built, target, tmp_path):
    binaries, _ = built
    inputs = []
    for name, data in seeds(target).items():
        (tmp_path / name).write_bytes(data)
        inputs.append(tmp_path / name)
    inputs += sorted((REGRESSIONS / target).glob("*")) if (REGRESSIONS / target).is_dir() else []
    result = run(binaries[target], inputs)
    assert result.returncode == 0, result.stderr[-4000:]


def test_minimal_maps_are_accepted(built, tmp_path):
    """the maps made here pass the checks (else the fuzzing starts from
    nothing)"""
    binaries, _ = built
    for target, data in (("xbox", xbox_map()), ("ce", ce_map())):
        path = tmp_path / f"{target}.map"
        path.write_bytes(data)
        result = run(binaries[target], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
        assert result.returncode == 0, result.stderr[-4000:]
        assert "refused" not in result.stderr and "damaged" not in result.stderr, result.stderr[-4000:]


def test_long_tag_names(built, tmp_path):
    """a tag's name longer than a tag's path is (256 with its terminator):
    an Xbox map's is corrected (the tag is given no name), a Custom Edition
    map's refused"""
    binaries, _ = built
    for target, data, expected in (("xbox", xbox_map(b"x" * 300), "has no name"),
                                   ("ce", ce_map(b"x" * 300), "longer than 255 characters")):
        path = tmp_path / f"{target}.map"
        path.write_bytes(data)
        result = run(binaries[target], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
        assert result.returncode == 0, result.stderr[-4000:]
        assert expected in result.stderr, result.stderr[-4000:]
    for target, data in (("xbox", xbox_map(b"x" * 254)), ("ce", ce_map(b"x" * 255))):
        path = tmp_path / f"{target}.map"
        path.write_bytes(data)
        result = run(binaries[target], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
        assert "has no name" not in result.stderr and "longer than" not in result.stderr, result.stderr[-4000:]


def test_room_counted_without_wrapping(built, tmp_path):
    """a structure BSP whose lightmaps all name one block of materials: each
    material's vertex buffers counted every time (the room the conversions
    take of the tag cache), until that is more than the tag cache holds,
    when the map is refused then and there (the count, 32 bits on the 32-bit
    builds, once wrapped around for such a map, and the check went on through
    every lightmap)"""
    binaries, _ = built
    path = tmp_path / "shared_materials.map"
    path.write_bytes(ce_map_bsp_materials(2700, 341))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "vertex buffers need more than the tag cache's" in result.stderr, result.stderr[-4000:]
    path.write_bytes(ce_map_bsp_materials(4, 3))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert "fuzz: checked" in result.stderr and "3 BSP materials' vertices compressed" in result.stderr, \
        result.stderr[-4000:]


def test_empty_materials_walk_capped(built, tmp_path):
    """the materials of every lightmap together are capped, far above any
    real map's (#1089): a map past the cap is refused at once"""
    binaries, _ = built
    path = tmp_path / "empty_materials.map"
    path.write_bytes(ce_map_bsp_materials(0x10000, 0x1000, vertex_count=0))
    started = time.monotonic()
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "name more than 1048576 materials in all" in result.stderr, result.stderr[-4000:]
    assert time.monotonic() - started < 20
    # (a few empty materials are fine)
    path.write_bytes(ce_map_bsp_materials(4, 3, vertex_count=0))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert "fuzz: checked" in result.stderr and "materials in all" not in result.stderr, result.stderr[-4000:]


def test_scenario_of_another_group(built, tmp_path):
    """a scenario whose instance names another group (a protected map's:
    ce_repairs.c), here a sound's, its pitch ranges' block outside the tags:
    given the scenario's group before anything reads the tags as they load,
    as it is before they are checked, and never read as a sound"""
    binaries, _ = built
    data = bytearray(ce_map())
    instance = 0x800 + 0x28
    data[instance:instance + 4] = b"!dns"
    scenario = struct.unpack_from("<I", data, instance + 0x14)[0] - TAG_CACHE["ce"] + 0x800
    struct.pack_into("<iI", data, scenario + 0x98, 1, 0x50000000)
    path = tmp_path / "scenario_sound.map"
    path.write_bytes(bytes(data))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "fuzz: checked" in result.stderr, result.stderr[-4000:]
    assert "scenario given the scenario's group" in result.stderr, result.stderr[-4000:]


def test_bsp_tag_address_left_by_a_tool(built, tmp_path):
    """a structure BSP's tag given an address by a map tool (cursed-bloodgulch's),
    which Custom Edition sets as the BSP loads whatever was there: the address
    cleared before the map is checked and as it loads, the map played, never
    read where the tool said; and a header giving the map's length as 0
    (Invader's), read as the file's"""
    binaries, _ = built
    for address in (TAG_CACHE["ce"] + 0x100000, 0x50000000, 0xFFFFFFFF):
        data = bytearray(ce_map())
        struct.pack_into("<I", data, 0x800 + 0x28 + 0x20 + 0x14, address)
        struct.pack_into("<i", data, 8, 0)
        path = tmp_path / "bsp_tag_address.map"
        path.write_bytes(bytes(data))
        result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
        assert result.returncode == 0, result.stderr[-4000:]
        assert "fuzz: checked" in result.stderr, result.stderr[-4000:]
        assert "1 structure BSP tags' addresses cleared" in result.stderr, result.stderr[-4000:]
        assert "refused" not in result.stderr, result.stderr[-4000:]


def test_bsp_reference_written_over(built, tmp_path):
    """a block a conversion writes in place that lies over the scenario's
    structure BSP's reference (a weapon HUD's static element whose placement
    is the reference, flagged for the high resolution scale, which halves
    its scale: the BSP's size and where it loads): the BSP is not loaded
    from a reference other than the one checked, which would read it below
    the tag cache"""
    binaries, _ = built
    path = tmp_path / "hud_over_bsp.map"
    path.write_bytes(ce_map(weapon_hud_statics=-0x24, bsp_reference_unused=4))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "fuzz: checked" in result.stderr, result.stderr[-4000:]
    assert "not where it was checked" in result.stderr, result.stderr[-4000:]


def test_tags_checked_again_as_they_load(built, tmp_path):
    """a map whose tags the conversions change after they were checked: what
    the game reads of them trusting the checks is checked again once they have
    loaded, and the map refused"""
    binaries, _ = built
    base = TAG_CACHE["ce"]
    data = bytearray(ce_map(sound=[(1, 36, 0, 0x800)]))
    instances = struct.unpack_from("<I", data, 0x800)[0] - base
    scenario = struct.unpack_from("<I", data, 0x800 + instances + 0x14)[0] - base
    syntax = struct.unpack_from("<I", data, 0x800 + scenario + 0x474 + 12)[0]
    data[0x800 + instances + 0x40:0x800 + instances + 0x44] = b"!tpj"
    struct.pack_into("<I", data, 0x800 + instances + 0x40 + 0x14, syntax + 0x22 - 0x38)
    path = tmp_path / "damage_over_scripts.map"
    path.write_bytes(bytes(data))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "fuzz: checked" in result.stderr, result.stderr[-4000:]
    assert "refused as it loaded: its scripts' syntax" in result.stderr, result.stderr[-4000:]


def test_conversions_read_only_the_tags(built, tmp_path):
    """an object whose functions' block lies past the map's tags, where its
    structure BSP loads, or in the room above the tag cache: the conversions
    read and write only where the map's tags are, and pass it over"""
    binaries, _ = built
    base = TAG_CACHE["ce"]
    for address in (base + 0x100000, base + 0x1700000):
        data = bytearray(ce_map(sound=[(1, 36, 0, 0x800)]))
        instances = struct.unpack_from("<I", data, 0x800)[0] - base
        tag = struct.unpack_from("<I", data, 0x800 + instances + 0x40 + 0x14)[0] - base
        # (the sound's tag made an object, its functions' block, 0x158 in,
        # one function there)
        data[0x800 + instances + 0x40:0x800 + instances + 0x44] = b"ejbo"
        struct.pack_into("<iI", data, 0x800 + tag + 0x158, 1, address)
        path = tmp_path / "functions_past_tags.map"
        path.write_bytes(bytes(data))
        result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
        assert result.returncode == 0, result.stderr[-4000:]
        assert "fuzz: checked" in result.stderr, result.stderr[-4000:]
        assert "0 periodic and 0 transition functions" in result.stderr, result.stderr[-4000:]


def test_sound_samples_in_sounds_map(built, tmp_path):
    """a Custom Edition map's own sound (not indexed): each permutation's
    samples read from the file Halo PC's flag on them names, sounds.map's or
    the map's, whatever the map's version, and checked to lie in it (Super
    Campaign Refined's c10 has 912 such permutations, which were read from
    the map at sounds.map's offsets); one with samples in both is copied
    whole into the decoded sounds' file"""
    binaries, _ = built
    adpcm, external = 1, 1
    map_size = len(ce_map(sound=[(adpcm, 36, 0, 0)]))
    resources = tmp_path / "resources"
    resources.mkdir()
    # (an empty resource map of sounds, past the map's end)
    (resources / "sounds.map").write_bytes(struct.pack("<IIII", 2, 16, 16, 0) + bytes(map_size + 0x2000))
    in_sounds_map = map_size + 0x1000
    cases = (
        ("in_sounds_map", [(adpcm, 36, external, in_sounds_map)], None, "refused"),
        ("in_map", [(adpcm, 36, 0, 0x800)], None, "refused"),
        ("both", [(adpcm, 36, 0, 0x800), (adpcm, 36, external, in_sounds_map)],
         "1 with samples in both the map and sounds.map copied", "refused"),
        ("unflagged_past_map", [(adpcm, 36, 0, in_sounds_map)], "not in the map", None),
        ("past_sounds_map", [(adpcm, 36, external, map_size + 0x3000)], "not in sounds.map", None),
    )
    for name, permutations, expected, unexpected in cases:
        path = tmp_path / f"{name}.map"
        path.write_bytes(ce_map(sound=permutations))
        result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1",
                                                          "MAP_FUZZ_CE_RESOURCES": str(resources)})
        assert result.returncode == 0, result.stderr[-4000:]
        if expected:
            assert expected in result.stderr, (name, result.stderr[-4000:])
        if unexpected:
            assert unexpected not in result.stderr, (name, result.stderr[-4000:])


WIDE_FORMAT_TEST = r"""
#include <stdio.h>
#include <string.h>
typedef unsigned short wide;
int msvc_snwprintf(wide *buffer, size_t count, const wide *format, ...);
unsigned int D3D__RenderState[512];
static void widen(wide *to, const char *from) { while ((*to++ = (unsigned char)*from++)) ; }
static void show(const wide *text) { while (*text) putchar((char)*text++); putchar(10); }
int main(void)
{
    wide format[400], buffer[300];
    char flags[300];
    int count = 12345;

    memset(flags, '0', 250); flags[0] = '%'; flags[250] = 'd'; flags[251] = 0;
    widen(format, flags); msvc_snwprintf(buffer, 300, format, 42); show(buffer);
    widen(format, "a%nb"); msvc_snwprintf(buffer, 300, format, &count); show(buffer); printf("%d\n", count);
    widen(format, "%99999999999d|"); msvc_snwprintf(buffer, 300, format, 7); printf("%d\n", (int)buffer[299]);
    widen(format, "%5d|%-4s|%.2f"); { wide s[3] = { 'h', 'i', 0 }; msvc_snwprintf(buffer, 300, format, 42, s, 3.14159); }
    show(buffer);
    return 0;
}
"""


def test_conversions_write_nothing_over_the_tag_index(built, tmp_path):
    """a tag placed over the tag index, so that a conversion's write (a
    damage effect's transition made a valid one) lands on another tag's
    address: the write is passed over, and the other tag (an extended
    chicago shader) is still found where it is and converted"""
    binaries, _ = built
    base = TAG_CACHE["ce"]
    data, offsets = ce_map_of_tags([(b"jpt!", 0x100), (b"scex", 0x200)])
    damage_effect, shader = offsets["tags"]
    # (the shader's type, Halo PC's extended chicago)
    struct.pack_into("<h", data, shader + 0x24, 7)
    # (the damage effect's screen flash fade, at 0x38, over the high half of
    # the shader's instance's address: 0x4044, no transition there is one of)
    shader_address = offsets["instances"] + 3 * 0x20 + 0x14
    struct.pack_into("<I", data, offsets["instances"] + 2 * 0x20 + 0x14, base + (shader_address + 2 - 0x38 - 0x800))
    path = tmp_path / "functions_over_index.map"
    path.write_bytes(bytes(data))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "refused" not in result.stderr, result.stderr[-4000:]
    assert "over its tag header or tag index" in result.stderr, result.stderr[-4000:]
    assert "1 extended chicago shaders made chicago ones" in result.stderr, result.stderr[-4000:]


WIDE_FORMAT_TEST = r"""
#include <stdio.h>
#include <string.h>
typedef unsigned short wide;
int msvc_snwprintf(wide *buffer, size_t count, const wide *format, ...);
unsigned int D3D__RenderState[512];
static void widen(wide *to, const char *from) { while ((*to++ = (unsigned char)*from++)) ; }
static void show(const wide *text) { while (*text) putchar((char)*text++); putchar(10); }
int main(void)
{
    wide format[400], buffer[300];
    char flags[300];
    int count = 12345;

    memset(flags, '0', 250); flags[0] = '%'; flags[250] = 'd'; flags[251] = 0;
    widen(format, flags); msvc_snwprintf(buffer, 300, format, 42); show(buffer);
    widen(format, "a%nb"); msvc_snwprintf(buffer, 300, format, &count); show(buffer); printf("%d\n", count);
    widen(format, "%99999999999d|"); msvc_snwprintf(buffer, 300, format, 7); printf("%d\n", (int)buffer[299]);
    widen(format, "%5d|%-4s|%.2f"); { wide s[3] = { 'h', 'i', 0 }; msvc_snwprintf(buffer, 300, format, 42, s, 3.14159); }
    show(buffer);
    return 0;
}
"""


def test_wide_format(tmp_path):
    """the wide printf a map's text is a format for (its string lists'):
    any number of flags, no %n, widths bounded, the usual conversions as
    they were"""
    if BUILD != "macos" or sys.platform != "darwin" or not (ROOT / "build.ninja").is_file():
        pytest.skip("macOS's build, after python3 configure.py")
    _, target = object_command(BUILD, "port/linux/src/msvc_wide.c")
    source = tmp_path / "wide_format_test.c"
    source.write_text(WIDE_FORMAT_TEST)
    binary = tmp_path / "wide_format_test"
    subprocess.run(["clang", "-fshort-wchar", "-Wl,-undefined,dynamic_lookup", "-o", str(binary), str(source),
                    str(ROOT / target)], check=True)
    lines = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60, check=True).stdout.splitlines()
    assert lines == ["42", "ab", "12345", "0", "   42|hi  |3.14"]



MODEL_DATA_TEST = r"""
#include "cseries.h"
#include "ce_map_checks.h"

/* (what the unit's Xbox headers name, never used here) */
unsigned int D3D__RenderState[512];

/* (the unit's printf and memset are the game's: none here, and the answer
is the exit status, 0, or 1 + the check that went wrong) */
int main(void)
{
    static byte bytes[64];
    struct ce_model_data data = { 0 };
    /* (offset, size, and where the bytes are: -1 for refused) */
    long checks[][3] = {
        { 0, 64, 0 }, { 60, 4, 60 }, { 64, 0, 64 },
        { 60, 5, -1 }, { 65, 0, -1 }, { 8, 0xFFFFFFFC, -1 }, { 0xFFFFFFF0, 0x20, -1 },
    };
    int index;

    data.whole = bytes;
    data.size = sizeof(bytes);
    for (index = 0; index < (int)(sizeof(checks) / sizeof(checks[0])); index++)
    {
        byte const *at = ce_model_data_at(&data, (unsigned long)checks[index][0], (unsigned long)checks[index][1]);

        if ((at ? at - bytes : -1) != checks[index][2])
            return 1 + index;
    }
    return 0;
}
"""


def test_model_data_reads_stay_within_it(tmp_path):
    """the model data held whole gives out only what lies within it: an
    offset and size past its end, or wrapping past it, are refused"""
    if BUILD != "macos" or sys.platform != "darwin" or not (ROOT / "build.ninja").is_file():
        pytest.skip("macOS's build, after python3 configure.py")
    source = "port/linux/game/ce_models.c"
    _, target = object_command(BUILD, source)
    written = tmp_path / "model_data_test.c"
    written.write_text(MODEL_DATA_TEST)
    rewritten = tmp_path / "model_data_test_lp64.c"
    subprocess.run([sys.executable, str(ROOT / "tools" / "lp64_rewrite.py"), "--output", str(rewritten),
                    str(written)], cwd=ROOT, check=True)
    command = compile_command(BUILD, source, tmp_path / "model_data_test.o", {"source": str(rewritten)})
    if Path(command[0]).name in ("ccache", "sccache"):
        command = command[1:]
    subprocess.run(command, cwd=ROOT, check=True)
    binary = tmp_path / "model_data_test"
    subprocess.run(["clang", "-Wl,-undefined,dynamic_lookup", "-o", str(binary), str(tmp_path / "model_data_test.o"),
                    str(ROOT / target)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result

@pytest.mark.parametrize("target", TARGETS)
def test_fuzz(built, target, tmp_path):
    binaries, _ = built
    corpus = tmp_path / "corpus"
    corpus.mkdir()
    for name, data in seeds(target).items():
        (corpus / name).write_bytes(data)
    result = run(binaries[target], [corpus], f"-runs={FUZZ_RUNS}", "-seed=1", "-max_len=65536",
                 f"-artifact_prefix={tmp_path}/")
    assert result.returncode == 0, result.stderr[-6000:]


# ---------- longer runs


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--target", choices=TARGETS, required=True)
    parser.add_argument("--seconds", type=int, default=600)
    parser.add_argument("--base", type=Path, help="a map to change (patch mode)")
    parser.add_argument("--resources", type=Path, help="Custom Edition's resource maps' folder")
    parser.add_argument("--xbox-ui", type=Path, help="the Xbox's ui.map, whose button icons HUDs are given")
    parser.add_argument("--corpus", type=Path, help="a corpus folder (kept)")
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--build-dir", type=Path, help="where the fuzzer is built (kept)")
    parser.add_argument("--only-build", action="store_true")
    parser.add_argument("inputs", nargs="*", type=Path, help="run these inputs once instead")
    options = parser.parse_args()

    scratch = Path(tempfile.mkdtemp(prefix="map_fuzz_"))
    build_dir = options.build_dir or scratch
    build_dir.mkdir(parents=True, exist_ok=True)
    cc = libfuzzer_compiler(build_dir)
    if not cc:
        sys.exit("no clang with libFuzzer (brew install llvm, or MAP_FUZZ_CC)")
    binary = build(options.target, build_dir, cc)
    if options.only_build:
        print(binary)
        return
    environment = {}
    if options.base:
        base = options.base
        if options.target == "xbox":
            base = inflate(options.base, scratch / options.base.name)
        environment["MAP_FUZZ_BASE"] = str(base)
    if options.resources:
        environment["MAP_FUZZ_CE_RESOURCES"] = str(options.resources)
    if options.xbox_ui:
        environment["MAP_FUZZ_XBOX_UI"] = str(options.xbox_ui)
    if options.inputs:
        result = run(binary, options.inputs, environment=environment)
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        sys.exit(result.returncode)
    corpus = options.corpus or scratch / "corpus"
    corpus.mkdir(parents=True, exist_ok=True)
    if not options.base:
        for name, data in seeds(options.target).items():
            (corpus / name).write_bytes(data)
    elif not any(corpus.iterdir()):
        (corpus / "nothing").write_bytes(b"")
    arguments = [f"-max_total_time={options.seconds}", "-print_final_stats=1", "-timeout=120",
                 "-rss_limit_mb=8192", f"-max_len={4096 if options.base else 1 << 20}"]
    if options.jobs > 1:
        arguments += [f"-jobs={options.jobs}", f"-workers={options.jobs}"]
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:allocator_may_return_null=1", **environment)
    sys.exit(subprocess.run([str(binary), *arguments, str(corpus)], env=env).returncode)


if __name__ == "__main__":
    main()
