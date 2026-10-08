"""The map loader's fuzz targets (tools/tests/map_fuzz.c): an Xbox map's tags
through the tag validator, a Custom Edition or HaloMD map through its checks
and then its conversions, and a Custom Edition sound's Ogg Vorbis stream
through its decoder. Each is built from the 64-bit build's own units, with
the macOS build's flags (from build.ninja: `python3 configure.py` first) plus
libFuzzer, AddressSanitizer and UndefinedBehaviorSanitizer (or linux64's:
MAP_FUZZ_BUILD=linux64).

As a test (pytest), each target runs on maps made here (small, whole, of no
game's data) and on the inputs in tools/tests/map_fuzz_regressions, then for
a bounded number of runs from them. Where clang has no libFuzzer, the tests
are skipped.

For longer runs, on the maps in assets/ (read in place, inflated into a
scratch folder, never copied into the repository):

    python3 tools/test_map_fuzz.py --target xbox --seconds 1800 [--base MAP] [--corpus DIR]
    python3 tools/test_map_fuzz.py --target ce --seconds 1800 --base maps_ce/foo.map --resources maps_ce
    python3 tools/test_map_fuzz.py --target vorbis --seconds 600 --corpus DIR

With --base, each input is a list of changes to that map (map_fuzz.c), which
reaches far deeper into a real map than whole-file mutations. A crash leaves
its input in the working folder (crash-*); a regression input goes in
tools/tests/map_fuzz_regressions only if it holds no game's data.
"""

import argparse
import os
import shutil
import struct
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

from tools.test_network_messages import compile_command, object_command  # noqa: E402

# the build whose units and flags are used (ninja macos, or linux64)
BUILD = os.environ.get("MAP_FUZZ_BUILD", "macos")
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
]
TARGETS = {
    "xbox": ("MAP_FUZZ_XBOX", VALIDATOR),
    "ce": ("MAP_FUZZ_CE", CUSTOM_EDITION),
    "vorbis": ("MAP_FUZZ_VORBIS", ["port/linux/game/ce_vorbis.c"]),
}


def libfuzzer_compiler(directory):
    """a clang that links -fsanitize=fuzzer (Apple's does not ship it;
    Homebrew's LLVM does)"""
    candidates = [os.environ.get("MAP_FUZZ_CC"), "/opt/homebrew/opt/llvm/bin/clang", "/usr/local/opt/llvm/bin/clang",
                  "clang"]
    probe = Path(directory) / "probe.c"
    probe.write_text("int LLVMFuzzerTestOneInput(const unsigned char *d, unsigned long s)"
                     " { (void)d; (void)s; return 0; }\n")
    for candidate in candidates:
        path = candidate and shutil.which(candidate)
        if not path:
            continue
        result = subprocess.run([path, "-fsanitize=fuzzer", str(probe), "-o", str(Path(directory) / "probe")],
                                capture_output=True)
        if result.returncode == 0:
            return path
    return None


def build(target, directory, cc):
    """the target's fuzzer, built in directory: its path"""
    define, sources = TARGETS[target]
    directory = Path(directory)
    objects = []
    words, _ = object_command(BUILD, "port/linux/game/tag_validate.c")

    def compile_unit(source, output, extra_source=None):
        command = compile_command(BUILD, source, output, {"flags": [f"-D{define}", *SANITIZE],
                                                          **({"source": extra_source} if extra_source else {})})
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


def ce_map(name=b"fuzz"):
    """a Custom Edition map of a scenario and one empty structure BSP, its
    scripts' syntax an empty data array"""
    base = TAG_CACHE["ce"]
    tags = bytearray(0x28)
    instances = len(tags)
    tags += bytes(0x40)
    names = len(tags)
    tags += name + b"\0"
    bsp_name = len(tags)
    tags += b"fuzz_bsp\0" + bytes((-(len(tags) + 9)) % 4)
    scenario = len(tags)
    tags += bytes(0x5B0)
    bsps = len(tags)
    tags += bytes(0x20)
    syntax = len(tags)
    tags += bytes(0x38)
    tags += bytes((-len(tags)) % 16)
    tag_data_size = len(tags)
    bsp_offset = 0x800 + tag_data_size
    bsp = bytearray(0x18 + 0x288 + 0x40)
    struct.pack_into("<I", bsp, 0, base + 0x100000 + 0x18)
    struct.pack_into("<4s", bsp, 0x14, b"psbs")
    model_offset = bsp_offset + len(bsp)
    # the tag header: instances, the scenario, checksum, count, model parts,
    # model data offset, model parts, vertex data size, model data size
    struct.pack_into("<IIIIIIIII4s", tags, 0, base + instances, 0xE1740000, 0, 2, 0, model_offset, 0, 0, 0, b"sgat")
    struct.pack_into("<4s4s4sIIIII", tags, instances, b"rncs", b"\xff\xff\xff\xff", b"\xff\xff\xff\xff", 0xE1740000,
                     base + names, base + scenario, 0, 0)
    struct.pack_into("<4s4s4sIIIII", tags, instances + 0x20, b"psbs", b"\xff\xff\xff\xff", b"\xff\xff\xff\xff",
                     0xE1750001, base + bsp_name, 0, 0, 0)
    # the scenario's scripts' syntax (a data array of no nodes of 0x14
    # bytes) and its structure BSP
    struct.pack_into("<iIII", tags, scenario + 0x474, 0x38, 0, 0, base + syntax)
    struct.pack_into("<hh", tags, syntax + 0x20, 0, 0x14)
    struct.pack_into("<iII", tags, scenario + 0x5A4, 1, base + bsps, 0)
    struct.pack_into("<iiII4sIiI", tags, bsps, bsp_offset, len(bsp), base + 0x100000, 0, b"psbs", base + bsp_name,
                     8, 0xE1750001)
    data = header(609, model_offset, 0x800, tag_data_size) + tags + bsp
    return bytes(data)


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
        pytest.skip("no clang with libFuzzer")
    if not (ROOT / "build.ninja").is_file():
        pytest.skip("needs python3 configure.py")
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
