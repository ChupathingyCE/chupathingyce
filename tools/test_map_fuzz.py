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

A Custom Edition resource map can be the input instead, read with a map
that takes from it (MAP_FUZZ_CE_RESOURCE names which, MAP_FUZZ_CE_MAP the map;
test_resource_maps_fuzzed).

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
    # (Halo PC's tag schema, which the tags go through as they load)
    *VALIDATOR,
    # (what the checks read, hashed: ce_map_checks.c)
    "port/linux/game/content_sha256.c",
    "source/bitmaps/bitmaps.c",
    "source/rasterizer/rasterizer_geometry.c",
    "source/rasterizer/rasterizer_swizzle.c",
    # (ce_hud.c's PNG button icons)
    *(f"port/third_party/zlib/{name}.c" for name in ("adler32", "crc32", "inffast", "inflate", "inftrees", "zutil")),
]
TARGETS = {
    "xbox": ("MAP_FUZZ_XBOX", VALIDATOR),
    "ce": ("MAP_FUZZ_CE", CUSTOM_EDITION),
    "vorbis": ("MAP_FUZZ_VORBIS", ["port/linux/game/content_vorbis.c"]),
    # (a campaign map read for Forge's AI characters: forge_ai_tags.c)
    "forge_ai": ("MAP_FUZZ_FORGE_AI", [
        "port/linux/game/forge_ai_tags.c",
        *VALIDATOR,
        "source/bitmaps/bitmaps.c",
        "source/rasterizer/rasterizer_geometry.c",
        "source/rasterizer/rasterizer_swizzle.c",
        *(f"port/third_party/zlib/{name}.c" for name in ("adler32", "crc32", "inffast", "inflate", "inftrees", "zutil")),
    ]),
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


def ce_bsp(bsp_base, extra=0x40):
    """a Custom Edition structure BSP that loads at bsp_base: its header, its
    structure (0x288 bytes, its blocks empty but its collision BSP), extra
    bytes, then its collision BSP of one node and one plane, which every
    structure BSP has (Halo PC's tag schema refuses one without:
    tag_schema_collision.c)"""
    structure = 0x18
    collision = structure + 0x288 + extra
    bsp = bytearray(collision + 0x60 + 0x0C + 0x10)
    struct.pack_into("<I", bsp, 0, bsp_base + structure)
    struct.pack_into("<4s", bsp, 0x14, b"psbs")
    struct.pack_into("<iI", bsp, structure + 0xB0, 1, bsp_base + collision)
    # (its bsp3d nodes, then its planes: the node's plane 0, no children)
    struct.pack_into("<iI", bsp, collision + 0x00, 1, bsp_base + collision + 0x60)
    struct.pack_into("<iI", bsp, collision + 0x0C, 1, bsp_base + collision + 0x6C)
    struct.pack_into("<iii", bsp, collision + 0x60, 0, -1, -1)
    struct.pack_into("<ffff", bsp, collision + 0x6C, 0.0, 0.0, 1.0, 0.0)
    return bsp


def ce_map(name=b"fuzz", sound=None, weapon_hud_statics=None, bsp_reference_unused=0, sound_flags=0, sound_next=-1):
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
        struct.pack_into("<I", tags, sound_header, sound_flags)
        struct.pack_into("<iII", tags, sound_header + 0x98, 1, base + sound_range, 0)
        struct.pack_into("<h", tags, sound_range + 0x2C, len(sound))
        struct.pack_into("<iII", tags, sound_range + 0x3C, len(sound), base + sound_permutations, 0)
        for index, (compression, size, flags, offset) in enumerate(sound):
            permutation = sound_permutations + index * 0x7C
            struct.pack_into("<hh", tags, permutation + 0x28, compression, sound_next)
            struct.pack_into("<iIIII", tags, permutation + 0x40, size, flags, offset, 0, 0)
        struct.pack_into("<4s4s4sIIIII", tags, instances + 0x40, b"!dns", b"\xff\xff\xff\xff", b"\xff\xff\xff\xff",
                         0xE1760002, base + sound_name, base + sound_header, 0, 0)
    tags += bytes((-len(tags)) % 16)
    tag_data_size = len(tags)
    bsp_offset = 0x800 + tag_data_size
    bsp = ce_bsp(base + 0x100000)
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
    bsp = ce_bsp(bsp_base, vertices + 0x50 - lightmap_block)
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
    bsp = ce_bsp(base + 0x100000)
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
    if target == "forge_ai":
        return {"minimal_xbox.map": xbox_map()}
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
    # (one block of materials shared by every lightmap, which no tool makes:
    # Halo PC's tag schema walks it as overlapping, as HaloMD plays such
    # maps, and each material is compressed once; the conversion alone,
    # without it, does the same)
    path.write_bytes(ce_map_bsp_materials(4, 3))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "refused" not in result.stderr, result.stderr[-4000:]
    assert "3 BSP materials' vertices compressed" in result.stderr, result.stderr[-4000:]
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1", "MAP_FUZZ_CE_SCHEMA": "0"})
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
    # (Halo PC's tag schema refuses it, the HUD's root shorter than a weapon
    # HUD's; without it, the BSP's load does)
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "refused by Halo PC's tag schema" in result.stderr, result.stderr[-4000:]
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1", "MAP_FUZZ_CE_SCHEMA": "0"})
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
    # (Halo PC's tag schema refuses it first, the damage effect's root over
    # the scripts' syntax; without it, the check after the conversions does)
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "refused by Halo PC's tag schema" in result.stderr, result.stderr[-4000:]
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1", "MAP_FUZZ_CE_SCHEMA": "0"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "fuzz: checked" in result.stderr, result.stderr[-4000:]
    assert "refused as it loaded: its scripts' syntax" in result.stderr, result.stderr[-4000:]


def test_pc_schema_walks_overlapping_blocks(built, tmp_path):
    """a block of a group no conversion reads (an item collection's
    permutations) laid over another tag's root: before Halo PC's tag schema,
    nothing checked it and the map loaded; now it is given a copy of its own
    past the tags and checked there, and the map loads, as HaloMD plays such
    maps: a Custom Edition map's and a HaloMD map's alike"""
    binaries, _ = built
    base = TAG_CACHE["ce"]
    data, offsets = ce_map_of_tags([(b"itmc", 0x5C), (b"itmc", 0x5C)])
    first, second = offsets["tags"]
    # (one permutation of 0x54 bytes, from 4 bytes into the second
    # collection's root: its item reference, 0x24 in, none and named as the
    # map's tags are)
    name = struct.unpack_from("<I", data, offsets["instances"] + 0x10)[0]
    struct.pack_into("<iI", data, first, 1, base + second + 4 - 0x800)
    struct.pack_into("<4sIiI", data, second + 4 + 0x24, b"\xff\xff\xff\xff", name, 0, 0xFFFFFFFF)
    path = tmp_path / "collection_over_collection.map"
    for version in (609, 7):
        struct.pack_into("<i", data, 4, version)
        path.write_bytes(bytes(data))
        result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
        assert result.returncode == 0, result.stderr[-4000:]
        assert "refused" not in result.stderr, result.stderr[-4000:]
        assert "1 roots, blocks and data overlapping others' given their own copies" in result.stderr, result.stderr[-4000:]
        assert "the map loaded" in result.stderr, result.stderr[-4000:]


def test_pc_schema_takes_gbxmodels(built, tmp_path):
    """an object whose model is a gbxmodel (Halo PC's, which the Xbox's
    schema has no group for): Halo PC's tag schema takes it where a model is
    asked for, and walks the gbxmodel's own layout, its parts 0x84 bytes"""
    binaries, _ = built
    base = TAG_CACHE["ce"]
    data, offsets = ce_map_of_tags([(b"scen", 0x1FC), (b"mod2", 0xE8 + 0x30 + 2 * 0x84 + 0x9C)])
    scenery, model = offsets["tags"]
    # (scenery: its object type, its model; the model's tag index is the
    # fourth tag's)
    struct.pack_into("<h", data, scenery, 6)
    struct.pack_into("<4sIiI", data, scenery + 0x28, b"2dom", 0, 0, ((0xE174 + 3) << 16) | 3)
    # (the gbxmodel: one geometry of two parts, each its shader none and its
    # centroid's nodes none: Halo PC's part layout, local nodes at its end)
    geometry = model + 0xE8
    parts = geometry + 0x30
    struct.pack_into("<iI", data, model + 0xC4, 1, base + geometry - 0x800)
    struct.pack_into("<iI", data, geometry + 0x24, 2, base + parts - 0x800)
    for part in range(2):
        struct.pack_into("<Ihbbhh", data, parts + part * 0x84, 0, -1, -1, -1, -1, -1)
    # (and one node, the root of its tree)
    node = parts + 2 * 0x84
    struct.pack_into("<iI", data, model + 0xB8, 1, base + node - 0x800)
    struct.pack_into("<hhh", data, node + 0x20, -1, -1, -1)
    path = tmp_path / "gbxmodel.map"
    path.write_bytes(bytes(data))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "refused" not in result.stderr, result.stderr[-4000:]
    assert "not a tag of mode" not in result.stderr, result.stderr[-4000:]
    assert "the map loaded" in result.stderr, result.stderr[-4000:]


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
        # (Halo PC's tag schema refuses the map; the conversions, without
        # it, pass the block over)
        result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
        assert result.returncode == 0, result.stderr[-4000:]
        assert "refused by Halo PC's tag schema" in result.stderr, result.stderr[-4000:]
        result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1", "MAP_FUZZ_CE_SCHEMA": "0"})
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


def halo_pc_resource_map(kind, resources, size):
    """a resource map of Halo PC retail's layout (kind 1 bitmaps, 2 sounds), size
    bytes long: its header, then each resource's path, then its table (a path's
    offset, a size and an offset each), of resources (path, size, offset); none
    of a game's data"""
    paths = bytearray()
    table = bytearray()
    for path, resource_size, offset in resources:
        table += struct.pack("<III", len(paths), resource_size, offset)
        paths += path + b"\0"
    data = bytearray(struct.pack("<IIII", kind, 16, 16 + len(paths), len(resources))) + paths + table
    assert len(data) <= size
    return bytes(data + bytes(size - len(data)))


def test_retail_sounds_from_halo_pc_sounds_map(built, tmp_path):
    """a HaloMD map's (Halo PC retail's, version 7) stock sound, whose samples
    are at an offset in Halo PC's own sounds.map: read from there when the
    player has it in maps_pc and it has them (a resource there from that
    offset, no shorter); else found in Custom Edition's sounds.map as before
    (here, not there: the sound is silent), with lines on the player's HUD
    that name the file to add. A Custom Edition sounds.map put in maps_pc, or a
    resource past its file's end, is not taken"""
    binaries, _ = built
    adpcm, external = 1, 1
    offset = 0x400
    retail = bytearray(ce_map(sound=[(adpcm, 36, external, offset)]))
    struct.pack_into("<i", retail, 4, 7)
    path = tmp_path / "retail.map"
    path.write_bytes(retail)
    custom = tmp_path / "custom"
    custom.mkdir()
    (custom / "sounds.map").write_bytes(struct.pack("<IIII", 2, 16, 16, 0))
    (custom / "bitmaps.map").write_bytes(struct.pack("<IIII", 1, 16, 16, 0))
    read = "0 bitmaps and 1 sounds read from Halo PC's own"
    cases = (
        ("halo_pc", [(b"fuzz_sound__0__0", 36, offset)], 0x1000, read, "HUD 0"),
        ("longer", [(b"fuzz_sound__0__0", 72, offset)], 0x1000, read, None),
        ("shorter", [(b"fuzz_sound__0__0", 30, offset)], 0x1000, "1 sounds silent", "HUD 0"),
        ("elsewhere", [(b"fuzz_sound__0__0", 36, offset + 4)], 0x1000, "1 sounds silent", None),
        ("past_its_end", [(b"fuzz_sound__0__0", 36, offset)], offset + 20, "1 sounds silent", None),
        ("custom_edition", [(b"fuzz_sound__permutations", 36, offset)], 0x1000,
         "HUD 0: The sounds.map in maps_pc isn't Halo PC's.", read),
        ("missing", None, 0, "HUD 0: Copy Halo PC's sounds.map to maps_pc.\nHUD 0: Some sounds in this "
         "map are silent.", read),
    )
    for name, resources, size, expected, unexpected in cases:
        environment = {"MAP_FUZZ_VERBOSE": "1", "MAP_FUZZ_CE_RESOURCES": str(custom)}
        if resources is not None:
            folder = tmp_path / name
            folder.mkdir()
            (folder / "sounds.map").write_bytes(halo_pc_resource_map(2, resources, size))
            environment["MAP_FUZZ_PC_RESOURCES"] = str(folder)
        result = run(binaries["ce"], [path], environment=environment)
        assert result.returncode == 0, (name, result.stderr[-4000:])
        assert "refused" not in result.stderr, (name, result.stderr[-4000:])
        assert expected in result.stderr, (name, result.stderr[-4000:])
        if unexpected:
            assert unexpected not in result.stderr, (name, result.stderr[-4000:])


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
    # (Halo PC's tag schema gives the damage effect's root, over the tag
    # index, a copy of its own past the tags, which its corrections are
    # made in, and the map loads; the conversions, without the schema,
    # write nothing there)
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "has its own copy made: tag 'fuzz' (jpt!)" in result.stderr, result.stderr[-4000:]
    assert "the map loaded" in result.stderr, result.stderr[-4000:]
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1", "MAP_FUZZ_CE_SCHEMA": "0"})
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



def ce_map_with_model_data_and_string(model_data_size=0x40):
    """ce_map_of_tags's map with model data no model reads (model_data_size
    bytes after its structure BSP) and an indexed unicode string list (its
    resource in loc.map, by its path): the map, and a loc.map holding that
    resource last, as its last 12 bytes"""
    data, offsets = ce_map_of_tags([(b"ustr", 0x10)])
    tags = 0x800
    instance = offsets["instances"] + 0x20 * 2
    # (indexed: its resource is the first in loc.map, whose path is its name)
    struct.pack_into("<II", data, instance + 0x14, 0, 1)
    model_offset = len(data)
    data += bytes(range(256)) * (model_data_size // 256) + bytes(model_data_size % 256)
    struct.pack_into("<i", data, 8, len(data))
    struct.pack_into("<III", data, tags + 0x14, model_offset, 0, 0)
    struct.pack_into("<II", data, tags + 0x1C, 0, model_data_size)
    # (loc.map: its header, the path, the table of one resource, the resource:
    # a string list of no strings)
    name = b"fuzz\0\0\0\0"
    table = 16 + len(name)
    resource = table + 12
    loc = struct.pack("<IIII", 3, 16, table, 1) + name + struct.pack("<III", 0, 12, resource) + bytes(12)
    return bytes(data), loc


def test_map_changed_after_the_checks(built, tmp_path):
    """a map whose file changes between its checks and its load (#1088): its
    tag data, its model data, its structure BSP, or a resource map's tag
    copied in, each found not to be what was checked as the load reads it,
    and the map refused; unchanged, the same map loads"""
    binaries, _ = built
    data, loc = ce_map_with_model_data_and_string()
    resources = tmp_path / "resources"
    resources.mkdir()
    (resources / "loc.map").write_bytes(loc)
    path = tmp_path / "changed.map"
    path.write_bytes(data)
    environment = {"MAP_FUZZ_VERBOSE": "1", "MAP_FUZZ_CE_RESOURCES": str(resources)}
    result = run(binaries["ce"], [path], environment=environment)
    assert result.returncode == 0, result.stderr[-4000:]
    assert "fuzz: checked" in result.stderr and "1 indexed tags copied in" in result.stderr, result.stderr[-4000:]
    assert "changed since" not in result.stderr, result.stderr[-4000:]
    for part, what in (("tags", "its tag data"), ("resources", "a resource map's tags"),
                       ("model", "its model data"), ("bsp", "a structure BSP")):
        result = run(binaries["ce"], [path], environment={**environment, "MAP_FUZZ_CE_CHANGE": part})
        assert result.returncode == 0, (part, result.stderr[-4000:])
        assert "fuzz: checked" in result.stderr, (part, result.stderr[-4000:])
        assert f"refused as it loaded: {what} changed since the map was checked" in result.stderr, \
            (part, result.stderr[-4000:])
        assert f"the load found its {part} changed" in result.stderr, (part, result.stderr[-4000:])


def resource_map_cases():
    """(name, the map, the resource map that is the input, its seeds): a
    Custom Edition map whose sound's samples are in sounds.map, one whose
    string is a tag indexed in loc.map, and a HaloMD map whose stock sound is
    in Halo PC's own sounds.map; none of a game's data"""
    adpcm, external = 1, 1
    sounds_map = ce_map(sound=[(adpcm, 36, external, 0x400)])
    loc_map, loc = ce_map_with_model_data_and_string()
    retail = bytearray(ce_map(sound=[(adpcm, 36, external, 0x400)]))
    struct.pack_into("<i", retail, 4, 7)
    return (
        ("sounds", sounds_map, {"sounds.map": struct.pack("<IIII", 2, 16, 16, 0) + bytes(0x1000)}),
        ("loc", loc_map, {"loc.map": loc}),
        ("pc_sounds", bytes(retail), {
            "sounds.map": halo_pc_resource_map(2, [(b"fuzz_sound__0__0", 36, 0x400)], 0x1000),
            "other.map": halo_pc_resource_map(2, [(b"fuzz_sound__0__0", 72, 0x400), (b"a", 4, 0x20)], 0x800),
        }),
    )


@pytest.mark.parametrize("name", [case[0] for case in resource_map_cases()])
def test_resource_maps_fuzzed(built, name, tmp_path):
    """a resource map as the input (MAP_FUZZ_CE_RESOURCE), read with a map
    that takes from it, through the checks and the load: each seed, then a
    bounded run of fuzzing from them"""
    binaries, _ = built
    _, data, resource_seeds = next(case for case in resource_map_cases() if case[0] == name)
    path = tmp_path / "map.map"
    path.write_bytes(data)
    corpus = tmp_path / "corpus"
    corpus.mkdir()
    for seed_name, seed in resource_seeds.items():
        (corpus / seed_name).write_bytes(seed)
    environment = {"MAP_FUZZ_CE_RESOURCE": name, "MAP_FUZZ_CE_MAP": str(path)}
    result = run(binaries["ce"], sorted(corpus.iterdir()), environment={**environment, "MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "fuzz: checked" in result.stderr, result.stderr[-4000:]
    result = run(binaries["ce"], [corpus], f"-runs={FUZZ_RUNS}", "-seed=1", "-max_len=16384",
                 f"-artifact_prefix={tmp_path}/", environment=environment)
    assert result.returncode == 0, result.stderr[-6000:]


def test_halomd_tag_data_size_given_as_file_length(built, tmp_path):
    """a HaloMD map (version 7) whose header gives its file's length as its
    tag data's size, as a HaloMD tool wrote nine of the archive's maps: read
    as the rest of the file after its tag data's offset, and loaded. Not a
    Custom Edition map's (version 609), which stays refused"""
    binaries, _ = built
    data = bytearray(ce_map())
    file_length = struct.unpack_from("<i", data, 8)[0]
    struct.pack_into("<i", data, 0x14, file_length)
    for version, loads in ((7, True), (609, False)):
        struct.pack_into("<i", data, 4, version)
        path = tmp_path / f"size_{version}.map"
        path.write_bytes(data)
        result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
        assert result.returncode == 0, result.stderr[-4000:]
        assert ("the header's tag data size is the file's length" in result.stderr) == loads, result.stderr[-4000:]
        assert ("map_fuzz: the map loaded" in result.stderr) == loads, result.stderr[-4000:]


def test_halomd_last_tag_at_the_end_of_its_tag_data(built, tmp_path):
    """a HaloMD map whose last tag, a sound environment or a wind, begins
    exactly where its tag data ends (ten of the archive's maps): read as an
    empty one, zeroes after the tag data, and the map loaded. Any other
    group there, a tag that is not the last, or a Custom Edition map's, is
    still not in its tag data"""
    binaries, _ = built
    base = TAG_CACHE["ce"]
    cases = (((b"snde",), 7, True), ((b"wind",), 7, True), ((b"snde",), 609, False), ((b"ligh",), 7, False),
             ((b"snde", b"wind"), 7, False))
    for index, (groups, version, loads) in enumerate(cases):
        data, offsets = ce_map_of_tags([(group, 0x48) for group in groups])
        tag_data_size = struct.unpack_from("<i", data, 0x14)[0]
        struct.pack_into("<i", data, 4, version)
        # (the first extra tag's data at the end of the tag data: the last
        # tag, unless there is another after it)
        struct.pack_into("<I", data, offsets["instances"] + 0x20 * 2 + 0x14, base + tag_data_size)
        path = tmp_path / f"end_{index}.map"
        path.write_bytes(data)
        result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
        assert result.returncode == 0, (groups, version, result.stderr[-4000:])
        assert ("begins where its tag data ends: read as an empty one" in result.stderr) == loads, \
            (groups, version, result.stderr[-4000:])
        assert ("map_fuzz: the map loaded" in result.stderr) == loads, (groups, version, result.stderr[-4000:])
        if not loads:
            assert "is not in its tag data" in result.stderr, (groups, version, result.stderr[-4000:])


def test_stubbs_image_effects_and_vegetation_checked(built, tmp_path):
    """Stubbs's image effects (imef) and vegetation (vege), which Stubbs mode
    reads, go through the schema: an image effect's stages past the 8 drawn
    are cut, a stage's bitmap or a vegetation's model of another group is
    none, and stages outside the tags refuse the map"""
    binaries, _ = built
    base = TAG_CACHE["ce"]
    data, offsets = ce_map_of_tags([(b"imef", 0x1AC + 9 * 0xA4), (b"vege", 0x58)])
    effect, vegetation = offsets["tags"]
    struct.pack_into("<iI", data, effect + 0x1A0, 9, base + effect - 0x800 + 0x1AC)
    # (the first stage's bitmap and the vegetation's model name the scenario)
    struct.pack_into("<4sIII", data, effect + 0x1AC + 0x3C, b"mtib", 0, 0, 0xE1740000)
    struct.pack_into("<4sIII", data, vegetation + 0x40, b"edom", 0, 0, 0xE1740000)
    path = tmp_path / "stubbs.map"
    path.write_bytes(data)
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "stages has 9 elements, more than the game's 8: cut to 8" in result.stderr, result.stderr[-4000:]
    assert "(imef): stages[0].bitmap names e1740000, not a tag of bitm: none" in result.stderr, \
        result.stderr[-4000:]
    assert "(vege): model names e1740000" in result.stderr, result.stderr[-4000:]
    assert "map_fuzz: the map loaded" in result.stderr, result.stderr[-4000:]
    struct.pack_into("<iI", data, effect + 0x1A0, 2, base + 0x7FFFF0)
    path.write_bytes(data)
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "(imef): stages has 2 elements" in result.stderr and "outside the tags" in result.stderr, \
        result.stderr[-4000:]


def ustr_map_sharing_strings(version):
    """two unicode string lists of one string each, the second's string
    starting inside the first's ("Kill penalty: Off" and "Off", as Halo PC
    retail's tools left the retail UI's strings), and its offsets"""
    base = TAG_CACHE["ce"]
    data, offsets = ce_map_of_tags([(b"ustr", 0x80), (b"ustr", 0x80)])
    first, second = offsets["tags"]
    text = "Kill penalty: Off\0".encode("utf-16-le")
    for tag in (first, second):
        # (the list's one entry, 0x10 in, a data of 20 bytes)
        struct.pack_into("<iII", data, tag, 1, base + tag + 0x10 - 0x800, 0)
    struct.pack_into("<iiiII", data, first + 0x10, len(text), 0, 0, base + first + 0x24 - 0x800, 0)
    data[first + 0x24:first + 0x24 + len(text)] = text
    struct.pack_into("<iiiII", data, second + 0x10, 8, 0, 0, base + first + 0x24 + 14 * 2 - 0x800, 0)
    struct.pack_into("<i", data, 4, version)
    return data, offsets


def test_pc_strings_sharing_bytes_given_their_own(built, tmp_path):
    """a Halo PC map's string whose bytes are inside another tag's string
    (66 HaloMD and Custom Edition maps' var_kill_penalty and HUD strings):
    given a copy of its own past the tags, not made none, in a Custom
    Edition map and a HaloMD map alike; the map loads, and its tags as
    corrected are clean when checked again (map_fuzz.c)"""
    binaries, _ = built
    for version in (609, 7):
        data, _ = ustr_map_sharing_strings(version)
        path = tmp_path / f"strings_{version}.map"
        path.write_bytes(bytes(data))
        result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
        assert result.returncode == 0, result.stderr[-4000:]
        assert "strings[0].string has 8 bytes at" in result.stderr, result.stderr[-4000:]
        assert "has its own copy made" in result.stderr, result.stderr[-4000:]
        assert "overlapping another's: none" not in result.stderr, result.stderr[-4000:]
        assert "1 roots, blocks and data overlapping others' given their own copies (8 bytes)" in result.stderr, \
            result.stderr[-4000:]
        assert "the map loaded" in result.stderr, result.stderr[-4000:]


def test_pc_block_over_a_root_and_its_string_given_their_own(built, tmp_path):
    """the same, and an item collection's permutation laid over the second
    list's root and entry, which it reads as another structure: the
    permutation is given a copy of its own first, then the string (whose
    pointer is now in bytes only the list reads), and the map loads"""
    binaries, _ = built
    base = TAG_CACHE["ce"]
    data, offsets = ce_map_of_tags([(b"ustr", 0x80), (b"itmc", 0x5C), (b"ustr", 0x80)])
    first, collection, second = offsets["tags"]
    text = "Kill penalty: Off\0".encode("utf-16-le")
    for tag in (first, second):
        struct.pack_into("<iII", data, tag, 1, base + tag + 0x10 - 0x800, 0)
    struct.pack_into("<iiiII", data, first + 0x10, len(text), 0, 0, base + first + 0x24 - 0x800, 0)
    data[first + 0x24:first + 0x24 + len(text)] = text
    struct.pack_into("<iiiII", data, second + 0x10, 8, 0, 0, base + first + 0x24 + 14 * 2 - 0x800, 0)
    # (one permutation of 0x54 bytes over the second list's root and entry:
    # its item reference, 0x24 in, none and named as the map's tags are)
    name = struct.unpack_from("<I", data, offsets["instances"] + 0x10)[0]
    struct.pack_into("<iI", data, collection, 1, base + second - 0x800)
    struct.pack_into("<4sIiI", data, second + 0x24, b"\xff\xff\xff\xff", name, 0, 0xFFFFFFFF)
    path = tmp_path / "strings_in_walked_root.map"
    path.write_bytes(bytes(data))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "(itmc): permutations has 84 bytes at" in result.stderr, result.stderr[-4000:]
    assert "(ustr): strings[0].string has 8 bytes at" in result.stderr, result.stderr[-4000:]
    assert "2 roots, blocks and data overlapping others' given their own copies (92 bytes)" in result.stderr, \
        result.stderr[-4000:]
    assert "another's: none" not in result.stderr, result.stderr[-4000:]
    assert "the map loaded" in result.stderr, result.stderr[-4000:]


def test_pc_string_over_another_tags_root_copied(built, tmp_path):
    """a string list's entry, shared by two lists, whose string's bytes are
    the next tag's root: its check, which terminates the string, wrote in
    that root's block after the root was checked, and the next check read
    the block's elements from what it wrote (a crash). The string is given
    a copy of its own, as it is read as data and the root as a structure,
    and the map loads"""
    binaries, _ = built
    base = TAG_CACHE["ce"]
    data, offsets = ce_map_of_tags([(b"ustr", 0x40), (b"ustr", 0x40)])
    first, second = offsets["tags"]
    struct.pack_into("<iII", data, first, 1, base + first + 0x10 - 0x800, 0)
    struct.pack_into("<iII", data, second, 1, base + first + 0x10 - 0x800, 0)
    # (8 bytes at the second list's root, which a terminator would end)
    struct.pack_into("<iiiII", data, first + 0x10, 8, 0, 0, base + second - 0x800, 0)
    path = tmp_path / "string_over_root.map"
    path.write_bytes(bytes(data))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "strings[0].string has 8 bytes at" in result.stderr, result.stderr[-4000:]
    assert "has its own copy made" in result.stderr, result.stderr[-4000:]
    assert "the map loaded" in result.stderr, result.stderr[-4000:]


def ce_map_long_ogg(frames, channels=1, rate=22050, size=0x48000, short_at=0, sound_flags=0, sound_next=-1):
    """ce_map's map with one Ogg Vorbis permutation of its own of size bytes,
    after the map's end: a stand-in stream map_fuzz.c decodes to that many
    frames of a ramp (a 22 kHz mono sound's), a read stopping short at
    frame short_at if it is not 0"""
    data = ce_map(sound=[(3, size, 0, 0)], sound_flags=sound_flags, sound_next=sound_next)
    data = bytearray(ce_map(sound=[(3, size, 0, len(data))], sound_flags=sound_flags, sound_next=sound_next))
    fake = b"MAP_FUZZ_PCM" + struct.pack("<iiii", channels, rate, frames, short_at)
    data += fake + bytes(size - len(fake))
    struct.pack_into("<i", data, 8, len(data))
    return bytes(data)


def test_long_ogg_sounds_split(built, tmp_path):
    """a long Ogg Vorbis permutation (a multiplayer map's song, 47 to 102 MB
    decoded) is decoded a piece at a time and split into permutations the
    sound cache holds, played one after another; past the 256 permutations
    a pitch range may have the chunks are larger; past what a permutation
    may decode to it is not decoded, and the map still loads"""
    binaries, _ = built
    cases = (
        (3000000, "1 long Ogg Vorbis permutations decoded a piece at a time, 1 permutations of more samples",
         "6000000 bytes of samples played as 16 permutations of 393216 bytes"),
        # (past 256 permutations of 384 KB: chunks of 768 KB)
        (70000000, "1 long Ogg Vorbis permutations decoded a piece at a time, 1 permutations of more samples",
         "140000000 bytes of samples played as 179 permutations of 786432 bytes"),
        # (a short one: written whole, not split)
        (100000, "1 long Ogg Vorbis permutations decoded a piece at a time, 0 permutations of more samples", None),
        # (400 MB: more than a permutation may decode to)
        (200000000, "a long Ogg Vorbis permutation could not be decoded", None),
    )
    for frames, expected, split in cases:
        path = tmp_path / f"long_{frames}.map"
        path.write_bytes(ce_map_long_ogg(frames))
        result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
        assert result.returncode == 0, result.stderr[-4000:]
        assert expected in result.stderr, (frames, result.stderr[-4000:])
        assert "the map loaded" in result.stderr, (frames, result.stderr[-4000:])
        assert (split is not None) == ("bytes of samples played as" in result.stderr), (frames, result.stderr[-4000:])
        if split:
            assert split in result.stderr, (frames, result.stderr[-4000:])


def test_long_pcm_sounds_split(built, tmp_path):
    """a 16-bit PCM permutation of more samples than the sound cache holds
    (#1516), whether its samples are in the map, in sounds.map, or the sound
    has samples in both (copied whole into the decoded sounds' file), is
    split where its samples lie into permutations played one after another,
    as a long Ogg Vorbis one is; one the cache holds is left whole, and an
    Xbox ADPCM one as it was"""
    binaries, _ = built
    pcm, adpcm, external = 0, 1, 1
    long_size, short_size = 6000000, 100000
    # (each map's samples after its tags, at the same place in every case)
    samples_at = 0x2000
    assert len(ce_map(sound=[(pcm, long_size, 0, 0), (pcm, short_size, 0, 0)])) < samples_at
    resources = tmp_path / "resources"
    resources.mkdir()
    # (a resource map of sounds whose samples lie past the test maps' ends)
    in_sounds_map = samples_at + long_size + 0x1000
    (resources / "sounds.map").write_bytes(struct.pack("<IIII", 2, 16, 16, 0) +
                                           bytes(in_sounds_map + long_size + 0x1000))
    split = "6000000 bytes of samples played as 16 permutations of 393216 bytes"
    cases = (
        ("in_map", [(pcm, long_size, 0, samples_at)], split),
        ("in_sounds_map", [(pcm, long_size, external, in_sounds_map)], split),
        ("both", [(pcm, long_size, 0, samples_at), (pcm, short_size, external, in_sounds_map)], split),
        ("short", [(pcm, short_size, 0, samples_at)], None),
        ("adpcm", [(adpcm, long_size - long_size % 36, 0, samples_at)], None),
    )
    for name, permutations, expected in cases:
        data = bytearray(ce_map(sound=permutations))
        data += bytes(samples_at - len(data)) + bytes(long_size)
        struct.pack_into("<i", data, 8, len(data))
        path = tmp_path / f"pcm_{name}.map"
        path.write_bytes(bytes(data))
        result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1",
                                                          "MAP_FUZZ_CE_RESOURCES": str(resources)})
        assert result.returncode == 0, result.stderr[-4000:]
        assert "the map loaded" in result.stderr, (name, result.stderr[-4000:])
        assert (expected is not None) == ("bytes of samples played as" in result.stderr), (name, result.stderr[-4000:])
        if expected:
            assert expected in result.stderr, (name, result.stderr[-4000:])
            # (not cut to the sound cache's size by the tag checks, as it was)
            assert "more than the game's 4194304" not in result.stderr, (name, result.stderr[-4000:])


def test_long_ogg_sound_short_read_doubled(built, tmp_path):
    """a long 11 kHz Ogg Vorbis permutation of a 22 kHz sound, whose decoder
    gives a short piece before the stream's end (stb_vorbis does where it
    skips a page it cannot read): the piece, doubled, is not read into again
    (it was smaller than a piece, a heap overflow), and every frame is
    written, twice over"""
    binaries, _ = built
    for short_at in (1000, 65536 + 30000):
        path = tmp_path / f"short_{short_at}.map"
        path.write_bytes(ce_map_long_ogg(3000000, rate=11025, short_at=short_at))
        result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
        assert result.returncode == 0, result.stderr[-4000:]
        assert "12000000 bytes of samples played as 31 permutations of 393216 bytes" in result.stderr, \
            (short_at, result.stderr[-4000:])
        assert "the map loaded" in result.stderr, (short_at, result.stderr[-4000:])

def xbox_map_of_tags(extra):
    """an Xbox map of a scenario and the tags in extra (each a group and its
    data's size, zeroed), uncompressed: its tags, their base address and the
    offsets in them of each extra tag's data"""
    base = TAG_CACHE["xbox"]
    count = 1 + len(extra)
    tags = bytearray(0x24 + 0x20 * count)
    name = len(tags)
    tags += b"fuzz\0\0\0\0"
    root = len(tags)
    tags += bytes(0x5B0 + 0x200)
    datas = []
    for group, size in extra:
        datas.append(len(tags))
        tags += bytes(size + (-size) % 16)
    struct.pack_into("<IIIiiIiI4s", tags, 0, base + 0x24, 0xE1740000, 0, count, 0, 0, 0, 0, b"sgat")
    for index, (group, address) in enumerate([(b"scnr", root)] + [(group, data) for (group, _), data in
                                                                     zip(extra, datas)]):
        struct.pack_into("<4s4s4sIIIII", tags, 0x24 + 0x20 * index, group[::-1], b"\xff\xff\xff\xff",
                         b"\xff\xff\xff\xff", ((0xE174 + index) << 16) | index, base + name, base + address, 0, 0)
    return tags, base, datas


def test_xbox_string_over_another_tags_root_refused(built, tmp_path):
    """the same string list over the next tag's root in an Xbox map: shared
    there as data and as a structure, its check's terminator ended in the
    other list's block after that was checked (a crash). Bytes shared as
    another structure (as Halo CE+'s maps share a few) are checked again
    after every check: changed, the map is refused before a check reads
    through them"""
    binaries, _ = built
    tags, base, (first, second) = xbox_map_of_tags([(b"ustr", 0x40), (b"ustr", 0x40)])
    struct.pack_into("<iII", tags, first, 1, base + first + 0x10, 0)
    struct.pack_into("<iII", tags, second, 1, base + first + 0x10, 0)
    struct.pack_into("<iiiII", tags, first + 0x10, 8, 0, 0, base + second, 0)
    path = tmp_path / "xbox_string_over_root.map"
    path.write_bytes(header(5, 0x800 + len(tags), 0x800, len(tags)) + bytes(tags))
    result = run(binaries["xbox"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "reads them as another structure, and a correction changed them" in result.stderr, result.stderr[-4000:]


def xbox_map_particle_system(rotational, sprite_counts, sequence_index=0):
    """an Xbox map of a particle system of one type and one particle state,
    drawn from a bitmap of sequences of sprite_counts sprites"""
    pctl_size, bitm_size = 0x68 + 0x80 + 0x178, 0x6C + 0x40 * len(sprite_counts) + 0x20
    tags, base, (pctl, bitm) = xbox_map_of_tags([(b"pctl", pctl_size), (b"bitm", bitm_size)])
    bitmap_id = ((0xE174 + 2) << 16) | 2
    type_, state = pctl + 0x68, pctl + 0x68 + 0x80
    struct.pack_into("<i", tags, pctl + 0x44, -1)
    struct.pack_into("<iII", tags, pctl + 0x5C, 1, base + type_, 0)
    struct.pack_into("<h", tags, type_ + 0x28, 1 if rotational else 0)
    struct.pack_into("<iII", tags, type_ + 0x74, 1, base + state, 0)
    struct.pack_into("<4sIII", tags, state + 0x30, b"mtib", 0, 0, bitmap_id)
    struct.pack_into("<h", tags, state + 0x40, sequence_index)
    struct.pack_into("<i", tags, state + 0x84 + 0xC, -1)
    struct.pack_into("<i", tags, state + 0xB8 + 0x4C + 0xC, -1)
    sequences, sprites = bitm + 0x6C, bitm + 0x6C + 0x40 * len(sprite_counts)
    struct.pack_into("<iII", tags, bitm + 0x54, len(sprite_counts), base + sequences, 0)
    for index, count in enumerate(sprite_counts):
        struct.pack_into("<iII", tags, sequences + 0x40 * index + 0x34, count, base + sprites if count else 0, 0)
    return header(5, 0x800 + len(tags), 0x800, len(tags)) + bytes(tags)


def test_rotational_particles_without_an_edge_on_sequence_kept(built, tmp_path):
    """a rotational particle state draws its edge-on sprites from the
    sequence after its own. Halo PC's burning flame bitmap (the Custom
    Edition flamethrower's flames) has no such sequence: drawn face-on from
    its own then, the particle system is kept. One whose own sequence is
    missing or has no sprites, or whose edge-on sequence is there with none,
    is still disabled"""
    binaries, _ = built
    disabled = "which has no sprites: disabled"
    for name, rotational, sprite_counts, sequence_index, expected in (
            ("rotational_one_sequence", True, [1], 0, False),
            ("rotational_two_sequences", True, [1, 1], 0, False),
            ("simple_one_sequence", False, [1], 0, False),
            ("rotational_no_sprites", True, [0], 0, True),
            ("rotational_edge_on_no_sprites", True, [1, 0], 0, True),
            ("rotational_past_the_sequences", True, [1], 1, True),
            ("rotational_negative_sequence", True, [1], -1, True),
            ("simple_past_the_sequences", False, [1], 1, True),
            ("rotational_no_sequences", True, [], 0, True)):
        path = tmp_path / f"{name}.map"
        path.write_bytes(xbox_map_particle_system(rotational, sprite_counts, sequence_index))
        result = run(binaries["xbox"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
        assert result.returncode == 0, (name, result.stderr[-4000:])
        assert "refused" not in result.stderr, (name, result.stderr[-4000:])
        assert (disabled in result.stderr) == expected, (name, result.stderr[-4000:])


def test_pc_copies_the_same_whatever_the_room(built, tmp_path):
    """what a Halo PC map has copied apart depends on its tags alone, not on
    the room past them a platform has (a console's conversion room is none):
    with room, as little as the copies take, or none, it has the same copies
    or is refused, never loaded without them"""
    binaries, _ = built
    base = TAG_CACHE["ce"]
    data, offsets = ce_map_of_tags([(b"ustr", 0x40), (b"ustr", 0x40)])
    first, second = offsets["tags"]
    struct.pack_into("<iII", data, first, 1, base + first + 0x10 - 0x800, 0)
    struct.pack_into("<iII", data, second, 1, base + first + 0x10 - 0x800, 0)
    struct.pack_into("<iiiII", data, first + 0x10, 8, 0, 0, base + second - 0x800, 0)
    path = tmp_path / "string_over_root.map"
    path.write_bytes(bytes(data))
    copies = {}
    for room in (None, 16, 15, 0):
        environment = {"MAP_FUZZ_VERBOSE": "1"}
        if room is not None:
            environment["MAP_FUZZ_COPY_ROOM"] = str(room)
        result = run(binaries["ce"], [path], environment=environment)
        assert result.returncode == 0, result.stderr[-4000:]
        made = [line for line in result.stderr.splitlines() if "has its own copy made" in line]
        if room is None or room >= 16:
            assert "the map loaded" in result.stderr, (room, result.stderr[-4000:])
            copies[room] = made
        else:
            assert "no room in the tag cache to copy them apart" in result.stderr, (room, result.stderr[-4000:])
            assert "the map loaded" not in result.stderr, (room, result.stderr[-4000:])
        assert "another's: none" not in result.stderr, (room, result.stderr[-4000:])
    assert copies[None] and copies[None] == copies[16]


def ce_map_block_over_an_entry(copies):
    """a Custom Edition map whose first tags make copies past the tags (a
    string list of copies entries, each a string inside the first list's
    long one), then a string list's entry and an item collection whose one
    permutation lies over that entry, its item reference's name where the
    entry's string pointer is"""
    base = TAG_CACHE["ce"]
    text = 0x10000
    data, offsets = ce_map_of_tags([(b"ustr", 0x10 + 0x14 + text), (b"ustr", 0x10 + copies * 0x14),
                                    (b"ustr", 0x40 + 0x200), (b"itmc", 0x5C)])
    first, second, third, collection = offsets["tags"]

    def address(offset):
        return base + offset - 0x800

    struct.pack_into("<iII", data, first, 1, address(first + 0x10), 0)
    string = first + 0x10 + 0x14
    struct.pack_into("<iiiII", data, first + 0x10, text, 0, 0, address(string), 0)
    data[string:string + text] = b"Q" * text
    if copies:
        struct.pack_into("<iII", data, second, copies, address(second + 0x10), 0)
    for index in range(copies):
        struct.pack_into("<iiiII", data, second + 0x10 + index * 0x14, 2, 0, 0,
                         address(string + 2 + 2 * (index % (text // 2 - 4))), 0)
    entry = third + 0x10
    struct.pack_into("<iII", data, third, 1, address(entry), 0)
    struct.pack_into("<iiiII", data, entry, 0x200, 0, 0, address(third + 0x40), 0)
    data[third + 0x40:third + 0x240] = b"R" * 0x200
    # (the permutation's reference name, 0x28 in, is the entry's string
    # pointer, 0x0C in)
    struct.pack_into("<iII", data, collection, 1, address(entry + 0x0C - 0x28), 0)
    return data


@pytest.mark.parametrize("copies", [0, 4096])
def test_pc_block_over_an_entry_never_walked_where_it_writes(built, tmp_path, copies):
    """a block over another tag's bytes that cannot be copied apart (here,
    every copy is spent) is walked where it is only if the walk writes
    nothing there: a block with a reference, index, enum or string is made
    none instead, as its correction would change bytes the other tag was
    checked to read. With copies to spare, it gets a copy of its own"""
    binaries, _ = built
    path = tmp_path / f"block_over_an_entry_{copies}.map"
    path.write_bytes(bytes(ce_map_block_over_an_entry(copies)))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "the map loaded" in result.stderr, result.stderr[-4000:]
    if copies:
        assert "(itmc): permutations has 1 elements of 84 bytes at" in result.stderr, result.stderr[-4000:]
        assert "which overlap another's: none" in result.stderr, result.stderr[-4000:]
        assert "permutations[0].item" not in result.stderr, result.stderr[-4000:]
        assert f"{copies} roots, blocks and data overlapping others' given their own copies" in result.stderr, \
            result.stderr[-4000:]
    else:
        assert "(itmc): permutations has 84 bytes at" in result.stderr, result.stderr[-4000:]
        assert "1 roots, blocks and data overlapping others' given their own copies (84 bytes)" in result.stderr, \
            result.stderr[-4000:]


def test_pc_bsp_block_over_its_collision_never_walked_where_it_writes(built, tmp_path):
    """the same in a structure BSP, where nothing is copied apart: its
    collision BSP's vertices laid over the collision BSP itself, a vertex's
    edge index where the planes' pointer is, are made none, not walked
    where an index correction would change that pointer after the planes
    were checked; the BSP as corrected needs no more correction (map_fuzz.c)"""
    binaries, _ = built
    base = TAG_CACHE["ce"]
    data = bytearray(ce_map())
    instances = struct.unpack_from("<I", data, 0x800)[0] - base
    scenario = struct.unpack_from("<I", data, 0x800 + instances + 0x14)[0] - base
    bsps = struct.unpack_from("<I", data, 0x800 + scenario + 0x5A8)[0] - base
    bsp_offset, _, bsp_base = struct.unpack_from("<iiI", data, 0x800 + bsps)
    # (ce_bsp's collision BSP: its vertices block at 0x54, its planes'
    # pointer at 0x10, a vertex's edge index 0x0C in)
    collision = 0x18 + 0x288 + 0x40
    struct.pack_into("<iI", data, bsp_offset + collision + 0x54, 1, bsp_base + collision + 0x10 - 0x0C)
    path = tmp_path / "bsp_vertices_over_collision.map"
    path.write_bytes(bytes(data))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "collision_bsp[0].vertices has 1 elements of 16 bytes at" in result.stderr, result.stderr[-4000:]
    assert "which overlap another's: none" in result.stderr, result.stderr[-4000:]
    assert "first_edge_index" not in result.stderr, result.stderr[-4000:]
    assert "not clean when checked again" not in result.stderr, result.stderr[-4000:]


def test_pc_bsp_block_its_check_corrects_never_walked_where_it_writes(built, tmp_path):
    """a block with no fields of its own is still written to by its
    element's check: a collision BSP's leaves, laid over the collision BSP's
    nodes block, a leaf's bsp2d references there, which the collision check
    corrects. They are made none, not walked where that correction would
    change the nodes' pointer after the nodes were checked; the BSP as
    corrected needs no more correction (map_fuzz.c)"""
    binaries, _ = built
    base = TAG_CACHE["ce"]
    data = bytearray(ce_map())
    instances = struct.unpack_from("<I", data, 0x800)[0] - base
    scenario = struct.unpack_from("<I", data, 0x800 + instances + 0x14)[0] - base
    bsps = struct.unpack_from("<I", data, 0x800 + scenario + 0x5A8)[0] - base
    bsp_offset, _, bsp_base = struct.unpack_from("<iiI", data, 0x800 + bsps)
    # (ce_bsp's collision BSP: its nodes block at 0, its leaves block at
    # 0x18; a leaf 2 bytes in has its bsp2d reference count and first
    # reference in the nodes' pointer)
    collision = 0x18 + 0x288 + 0x40
    struct.pack_into("<iI", data, bsp_offset + collision + 0x18, 1, bsp_base + collision + 0x02)
    path = tmp_path / "bsp_leaves_over_nodes.map"
    path.write_bytes(bytes(data))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "collision_bsp[0].leaves has 1 elements of 8 bytes at" in result.stderr, result.stderr[-4000:]
    assert "which overlap another's: none" in result.stderr, result.stderr[-4000:]
    assert "bsp2d references from" not in result.stderr, result.stderr[-4000:]
    assert "not clean when checked again" not in result.stderr, result.stderr[-4000:]


def test_split_flag_is_the_loaders_alone(built, tmp_path):
    """the flag the loader gives a sound it split (bit 31 of its flags), which
    an impulse sound then plays through: a map's own is cleared as it loads,
    a Halo PC map's and an Xbox map's alike, so that a sound whose permutation
    follows itself is never played for ever; and a long sound split whose
    chain comes back round has its chunks end it"""
    binaries, _ = built
    # (a short sound, its one permutation followed by itself, the flag set)
    path = tmp_path / "flagged.map"
    path.write_bytes(ce_map_long_ogg(1000, sound_flags=0x80000002, sound_next=0))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "1 sounds had the loader's split flag set: cleared" in result.stderr, result.stderr[-4000:]
    assert "bytes of samples played as" not in result.stderr, result.stderr[-4000:]
    # (a long one, followed by itself: split, its last chunk ending it)
    path = tmp_path / "looped.map"
    path.write_bytes(ce_map_long_ogg(3000000, sound_flags=0x80000002, sound_next=0))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "1 sounds had the loader's split flag set: cleared" in result.stderr, result.stderr[-4000:]
    assert "a long permutation's chain comes back round; its chunks end it" in result.stderr, result.stderr[-4000:]
    assert "6000000 bytes of samples played as 16 permutations" in result.stderr, result.stderr[-4000:]
    assert "the map loaded" in result.stderr, result.stderr[-4000:]
    # (an Xbox map's sound with the flag: cleared by its checks)
    tags, base, (sound,) = xbox_map_of_tags([(b"snd!", 0xA4)])
    struct.pack_into("<I", tags, sound, 0x80000000)
    path = tmp_path / "xbox_flagged.map"
    path.write_bytes(header(5, 0x800 + len(tags), 0x800, len(tags)) + bytes(tags))
    result = run(binaries["xbox"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "has the flag only the loader gives a sound it split: cleared" in result.stderr, result.stderr[-4000:]


def test_extended_chicago_with_only_two_stage_maps(built, tmp_path):
    """an extended chicago shader (Halo PC's scex) is made a chicago one with
    its four-stage maps; one with none, only two-stage maps, which Halo PC
    draws then on any card, is given its two-stage maps (as MCC's are,
    mcc_tags.c), not left with nothing to draw"""
    binaries, _ = built
    base = TAG_CACHE["ce"]
    for four_stage, given in ((0, 1), (1, 0)):
        data, offsets = ce_map_of_tags([(b"scex", 0x78 + 2 * 0xDC)])
        shader = offsets["tags"][0]
        maps = base + shader + 0x78 - 0x800
        struct.pack_into("<h", data, shader + 0x24, 7)
        struct.pack_into("<iII", data, shader + 0x54, four_stage, maps + 0xDC if four_stage else 0, 0)
        struct.pack_into("<iII", data, shader + 0x60, 1, maps, 0)
        for element in (0, 1):
            # (each map's bitmap none: 0x6C in, a tag reference)
            struct.pack_into("<4sIiI", data, shader + 0x78 + element * 0xDC + 0x6C, b"mtib", 0, 0, 0xFFFFFFFF)
        path = tmp_path / f"scex_{four_stage}.map"
        path.write_bytes(bytes(data))
        result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
        assert result.returncode == 0, result.stderr[-4000:]
        assert "1 extended chicago shaders made chicago ones" in result.stderr, result.stderr[-4000:]
        assert ("%d extended chicago shaders with no four-stage maps drawn with their two-stage maps" % given) in \
            result.stderr, result.stderr[-4000:]
        assert "the map loaded" in result.stderr, result.stderr[-4000:]


def test_pc_netgame_flags_and_equipment_past_200(built, tmp_path):
    """a Halo PC map's netgame flags and equipment past the tools' 200 (four
    of HaloMD's maps have 202 to 317 flags, one 212 equipment): kept, as
    Halo PC's engine read them, up to a short's most; more than that cut"""
    binaries, _ = built
    base = TAG_CACHE["ce"]
    for count, cut in ((250, False), (0x7FFF + 1, True)):
        # (a count past a short's is cut before its elements are looked at:
        # room for 250 of them)
        data, offsets = ce_map_of_tags([(b"zzzz", 250 * 0x94), (b"zzzz", 250 * 0x90)])
        scenario = struct.unpack_from("<I", data, offsets["instances"] + 0x14)[0] - base + 0x800
        struct.pack_into("<iII", data, scenario + 0x378, count, base + offsets["tags"][0] - 0x800, 0)
        struct.pack_into("<iII", data, scenario + 0x384, count, base + offsets["tags"][1] - 0x800, 0)
        path = tmp_path / f"netgame_{count}.map"
        path.write_bytes(data)
        result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
        assert result.returncode == 0, result.stderr[-4000:]
        assert ("netgame_flags has 32768 elements, more than the game's 32767: cut to 32767" in result.stderr) == cut, \
            (count, result.stderr[-4000:])
        assert "more than the game's 200" not in result.stderr, (count, result.stderr[-4000:])
        assert ("map_fuzz: the map loaded" in result.stderr) != cut, (count, result.stderr[-4000:])


def test_byte_swapped_functions_turned_back(built, tmp_path):
    """a light's gel functions written in the other byte order by a HaloMD
    tool (256 for 1, 512 for 2): turned back before the schema checks them,
    not made the first function; a value that is no
    function either way still is"""
    binaries, _ = built
    data, offsets = ce_map_of_tags([(b"ligh", 0x400)])
    light = offsets["tags"][0]
    # (its yaw, roll and pitch functions: ce_functions.c)
    for offset, value in ((0x8E, 256), (0x96, 512), (0x9E, 0x1234)):
        struct.pack_into("<h", data, light + offset, value)
    path = tmp_path / "functions.map"
    path.write_bytes(data)
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "names periodic function 256, made 1" in result.stderr, result.stderr[-4000:]
    assert "names periodic function 512, made 2" in result.stderr, result.stderr[-4000:]
    assert "names periodic function 4660, made 0" in result.stderr, result.stderr[-4000:]
    assert "3 periodic and 0 transition functions" in result.stderr, result.stderr[-4000:]
    assert "_function is" not in result.stderr, result.stderr[-4000:]
    assert "map_fuzz: the map loaded" in result.stderr, result.stderr[-4000:]

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


def opensauce_map(header=True, widgets_outside=False, syntax_nodes=None, netgame_flags=None):
    """a Custom Edition map with OpenSauce's header (no flags: the checks
    take it as they take any Custom Edition map) and its project_yellow
    (yelo) and project_yellow_globals (gelo) tags, the scenario's first
    reference naming the yelo tag as OpenSauce's tools make it; its yelo's
    scripted widgets one element far outside the tags if widgets_outside;
    its scripts' syntax a data array of syntax_nodes nodes if given; its
    scenario's netgame flags that many, in a tag of room for 250, if given"""
    base = TAG_CACHE["ce"]
    # (the syntax, if any, after the gelo tag's root, in no tag's root)
    extra = [(b"yelo", 0x138), (b"gelo", 0x120 + (0x38 + syntax_nodes * 0x14 if syntax_nodes else 0))]
    if netgame_flags is not None:
        extra.append((b"zzzz", 250 * 0x94))
    data, offsets = ce_map_of_tags(extra)
    yelo, gelo = offsets["tags"][:2]
    scenario = struct.unpack_from("<I", data, offsets["instances"] + 0x14)[0] - base + 0x800
    # (the tags' handles: the third and fourth, ce_map_of_tags's numbering)
    yelo_index, gelo_index = ((0xE174 + 2) << 16) | 2, ((0xE174 + 3) << 16) | 3
    name = struct.unpack_from("<I", data, offsets["instances"] + 0x10)[0]
    struct.pack_into("<4sIiI", data, scenario, b"oley", name, 0, yelo_index)
    struct.pack_into("<hH", data, yelo, 2, 0)
    struct.pack_into("<4sIiI", data, yelo + 0x04, b"oleg", name, 0, gelo_index)
    for at in (0x14, 0x24):
        struct.pack_into("<4sIiI", data, yelo + at, b"\xff\xff\xff\xff", 0, 0, 0xFFFFFFFF)
    struct.pack_into("<f", data, yelo + 0x84, 1.0)
    struct.pack_into("<f", data, yelo + 0x88, 1.0)
    if widgets_outside:
        struct.pack_into("<iI", data, yelo + 0x68, 1, base + 0x01000000)
    struct.pack_into("<hH", data, gelo, 2, 0)
    struct.pack_into("<4sIiI", data, gelo + 0x28, b"\xff\xff\xff\xff", 0, 0, 0xFFFFFFFF)
    if syntax_nodes:
        syntax = gelo + 0x120
        struct.pack_into("<hh", data, syntax + 0x20, syntax_nodes, 0x14)
        struct.pack_into("<iIII", data, scenario + 0x474, 0x38 + syntax_nodes * 0x14, 0, 0, base + syntax - 0x800)
    if netgame_flags is not None:
        struct.pack_into("<iII", data, scenario + 0x378, netgame_flags, base + offsets["tags"][2] - 0x800, 0)
    if header:
        struct.pack_into("<4shH", data, 0x70, b"oley", 2, 0)
    return bytes(data)


def test_opensauce_netgame_flags_past_200(built, tmp_path):
    """OpenSauce's layout keeps Halo PC's maxima where it has none of its own:
    an OpenSauce map's netgame flags past the tools' 200 are kept, as a Halo
    PC map's are, up to a short's most"""
    binaries, _ = built
    for count, cut in ((250, False), (0x7FFF + 1, True)):
        path = tmp_path / f"opensauce_netgame_{count}.map"
        path.write_bytes(opensauce_map(netgame_flags=count))
        result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
        assert result.returncode == 0, result.stderr[-4000:]
        assert "OpenSauce" in result.stderr, result.stderr[-4000:]
        assert ("netgame_flags has 32768 elements, more than the game's 32767: cut to 32767" in result.stderr) == cut, \
            (count, result.stderr[-4000:])
        assert "more than the game's 200" not in result.stderr, (count, result.stderr[-4000:])


def test_opensauce_maps_checked_by_their_own_layout(built, tmp_path):
    """a map with OpenSauce's header has its tags checked against
    OpenSauce's layout (tag_schema_opensauce.c): its own groups are walked,
    not passed over, and a block of one outside the tags refuses the map; a
    map without the header is checked as Halo PC's, as before, its OpenSauce
    tags of groups that layout has none of"""
    binaries, _ = built
    path = tmp_path / "opensauce.map"
    path.write_bytes(opensauce_map())
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "OpenSauce's tag schema made" in result.stderr, result.stderr[-4000:]
    assert "(yelo)" not in result.stderr and "(gelo)" not in result.stderr, result.stderr[-4000:]
    assert "the map loaded" in result.stderr, result.stderr[-4000:]

    path.write_bytes(opensauce_map(widgets_outside=True))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "refused by OpenSauce's tag schema" in result.stderr, result.stderr[-4000:]

    # (no header: Halo PC's layout, where the same tags are of groups it
    # does not know, never walked, as starwarsb_3's are)
    path.write_bytes(opensauce_map(header=False, widgets_outside=True))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "Halo PC's tag schema made" in result.stderr, result.stderr[-4000:]
    assert "the map loaded" in result.stderr, result.stderr[-4000:]


def test_opensauce_scripts_syntax_kept(built, tmp_path):
    """OpenSauce's memory upgrades: its scripts' syntax for 28501 nodes is
    kept in its layout, and cut to the Xbox's 19001 in Halo PC's"""
    binaries, _ = built
    path = tmp_path / "opensauce_scripts.map"
    path.write_bytes(opensauce_map(syntax_nodes=28501))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "more than the game's 380076" not in result.stderr, result.stderr[-4000:]
    path.write_bytes(opensauce_map(header=False, syntax_nodes=28501))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "has 570076 bytes, more than the game's 380076" in result.stderr, result.stderr[-4000:]


def test_pc_linear_bitmap_with_packed_rows_stays_linear(built, tmp_path):
    """a Halo PC linear bitmap whose rows are packed, a row's texels after the
    last's, not whole 64-byte steps (SPV3's menu logo, 1193 wide; HUD icons):
    Halo PC's tag schema keeps it linear, as the texture cache reads such rows
    (any_pitch). Cleared, it was read as swizzled, and drew sheared"""
    binaries, _ = built
    base = TAG_CACHE["ce"]
    data, offsets = ce_map_of_tags([(b"bitm", 0x6C + 0x30 + 3 * 2 * 4)])
    group = offsets["tags"][0]
    bitmap = group + 0x6C
    # (one bitmap, 3x2 a8r8g8b8: a 12-byte pitch; linear; its pixels after it)
    struct.pack_into("<iI", data, group + 0x60, 1, base + bitmap - 0x800)
    struct.pack_into("<4shhhhhHhhhhii", data, bitmap, b"mtib", 3, 2, 1, 0, 11, 1 << 4, 0, 0, 0, 0, 0x30, 24)
    path = tmp_path / "pc_linear.map"
    path.write_bytes(bytes(data))
    result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
    assert result.returncode == 0, result.stderr[-4000:]
    assert "the map loaded" in result.stderr, result.stderr[-4000:]
    assert "is linear, but" not in result.stderr, result.stderr[-4000:]


def test_pc_actor_variant_grenade_type_past_the_games(built, tmp_path):
    """an actor variant whose grenade type is OpenSauce's third or fourth (as
    SPV3's maps have): in Halo PC's layout it is made the first, as an
    equipment's past the game's two is, since the AI indexes the globals'
    grenades and a unit's grenade counts by it; one within the game's two, and
    the stock 'warthog gunner''s older-layout 0x7072, are left alone"""
    binaries, _ = built
    for grenade_type, corrected in ((3, True), (2, True), (1, False), (0x7072, False)):
        data, offsets = ce_map_of_tags([(b"actv", 0x238)])
        struct.pack_into("<h", data, offsets["tags"][0] + 0x180, grenade_type)
        path = tmp_path / "actor_variant.map"
        path.write_bytes(bytes(data))
        result = run(binaries["ce"], [path], environment={"MAP_FUZZ_VERBOSE": "1"})
        assert result.returncode == 0, result.stderr[-4000:]
        assert (f"grenade_combat.grenade_type is {grenade_type}, OpenSauce's, past the game's 2: 0"
                in result.stderr) == corrected, \
            result.stderr[-4000:]


if __name__ == "__main__":
    main()

