"""Check that the linked rasterizer_lights holds all the lights a view draws.

rasterizer_lights was a stand-in of the Xbox's size in halo_linker_common.c,
too small for the native builds' light limit, and a busy frame wrote over the
globals after it. This links the owner's definition with every stand-in of
halo_linker_common.c, as the game does, and checks from the linked image that
the storage the linker picked is as large as the structure and that no other
global starts inside it. ELF (i386, x86-64) and COFF (i686, x86-64). No
game assets, SDK or C runtime needed; clang
and lld only.
"""

import argparse
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NAME = "rasterizer_lights"


TOOLS = {}


def tool(cc, name):
    # (clang's own llvm-nm and llvm-objdump, else the first on PATH, as
    # llvm-nm-18 and the like on Ubuntu)
    if name not in TOOLS:
        found = subprocess.run([cc, f"-print-prog-name={name}"], capture_output=True, text=True).stdout.strip()
        if not shutil.which(found):
            found = next((path for path in (shutil.which(name), *sorted(
                str(candidate) for candidate in Path("/usr/bin").glob(name + "-*"))) if path), name)
        TOOLS[name] = found
    return TOOLS[name]


def light_limit():
    # the desktop builds' value: the first definition of the capacity header
    capacity = (ROOT / "port/linux/include/halo_port_capacity.h").read_text()
    return int(re.search(r"#define HALO_PORT_MAXIMUM_RENDERED_LIGHTS (\d+)", capacity).group(1))


def owner_source():
    header = (ROOT / "source/rasterizer/rasterizer_lights.h").read_text()
    lights = (ROOT / "source/rasterizer/rasterizer_lights.c").read_text()
    definition = re.search(r"^struct rasterizer_lights rasterizer_lights\b[^;]*;", lights, re.M)
    # (none: the header's declaration alone, and the link takes a stand-in)
    definition = definition.group(0) if definition else ""
    return f'''
typedef float real;
typedef struct {{ real x, y, z; }} real_point3d;
typedef struct {{ real i, j, k; }} real_vector3d;
typedef struct {{ real red, green, blue; }} real_rgb_color;
enum {{ MAXIMUM_LIGHTS_PER_WINDOW = {light_limit()} }};
{header}
{definition}
unsigned int {NAME}_size = sizeof(struct rasterizer_lights);
void caller(void) {{ rasterizer_lights.light_count = MAXIMUM_LIGHTS_PER_WINDOW; }}
'''


def fallback_source(bits):
    common = (ROOT / "port/linux/src/halo_linker_common.c").read_text()
    lines, names = [], set()
    for match in re.finditer(r"^HALO_COMMON\((\w+), *(\d+)\);", common, re.M):
        if match.group(1) not in names:
            names.add(match.group(1))
            lines.append(match.group(0))
    scale = 2 if bits == 64 else 1
    macro = f"#define HALO_COMMON(name, size) __attribute__((weak, aligned(16))) unsigned char name[(size) * {scale}]\n"
    return macro + "\n".join(lines) + "\n", names


def symbols_elf(cc, image):
    out = subprocess.run([tool(cc, "llvm-nm"), "-S", "--defined-only", str(image)], capture_output=True, text=True,
                         check=True).stdout
    table = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4:
            table[parts[3]] = (int(parts[0], 16), int(parts[1], 16))
        elif len(parts) == 3:
            table[parts[2]] = (int(parts[0], 16), None)
    return table


def symbols_coff(map_file, prefix):
    table = {}
    for line in map_file.read_text(errors="replace").splitlines():
        # lld-link /map publics: section:offset, name, address, object
        parts = line.split()
        if len(parts) >= 3 and re.fullmatch(r"[0-9a-fA-F]{4}:[0-9a-fA-F]{8}", parts[0]) and \
                re.fullmatch(r"[0-9a-fA-F]{8,16}", parts[2]):
            name = parts[1]
            if prefix and name.startswith(prefix):
                name = name[len(prefix):]
            table.setdefault(name, (int(parts[2], 16), None))
    return table


def size_of(work, cc, target):
    # the structure's size for this ABI, from the owner's object (ELF of the
    # same bitness lays these plain structures out as COFF does)
    obj = work / "size.o"
    subprocess.run([cc, f"--target={target}", "-std=gnu99", "-O0", "-ffreestanding", "-Wno-pragma-once-outside-header", "-c",
                    str(work / "owner.c"), "-o", str(obj)], check=True, timeout=60)
    out = subprocess.run([tool(cc, "llvm-objdump"), "-s", "-j", ".data", str(obj)], capture_output=True, text=True,
                         check=True).stdout
    # (rasterizer_lights_size, the only initialized datum: the first word)
    word = re.search(r"^ 0000 ([0-9a-f]{8})", out, re.M).group(1)
    return int.from_bytes(bytes.fromhex(word), "little")


def check(table, names, size, label):
    address = table[NAME][0]
    end = address + size
    linked = table[NAME][1]
    assert linked is None or linked >= size, f"{label}: {NAME} linked with {linked} bytes, needs {size}"
    inside = sorted(name for name, (start, _) in table.items()
                    if name in names and name != NAME and address < start < end)
    assert not inside, f"{label}: {', '.join(inside)} start inside {NAME} ({size} bytes)"
    print(f"PASS: {label}: {NAME} has its {size} bytes")


def run(cc):
    targets = (("ELF i386", "i686-linux-gnu", 32, "elf"),
               ("ELF x86-64", "x86_64-linux-gnu", 64, "elf"),
               ("COFF i686", "i686-pc-windows-msvc", 32, "coff"),
               ("COFF x86-64", "x86_64-pc-windows-msvc", 64, "coff"))
    with tempfile.TemporaryDirectory(prefix="halo-rendered-lights-") as directory:
        work = Path(directory)
        (work / "owner.c").write_text(owner_source())
        for label, target, bits, kind in targets:
            fallback, names = fallback_source(bits)
            (work / "fallback.c").write_text(fallback)
            names.add(NAME)
            size = size_of(work, cc, "i686-linux-gnu" if bits == 32 else "x86_64-linux-gnu")
            image = work / ("probe.exe" if kind == "coff" else "probe")
            command = [cc, f"--target={target}", "-std=gnu99", "-O2", "-fcommon", "-ffreestanding",
                       "-Wno-pragma-once-outside-header", "-fno-builtin", "-nostdlib", "-fuse-ld=lld"]
            if kind == "coff":
                command += ["-Wl,/entry:caller", "-Wl,/subsystem:console", f"-Wl,/map:{work / 'probe.map'}"]
            else:
                command += ["-Wl,-e,caller", "-static", "-no-pie", "-Wl,--export-dynamic"]
            command += [str(work / "owner.c"), str(work / "fallback.c"), "-o", str(image)]
            subprocess.run(command, check=True, timeout=60)
            if kind == "coff":
                table = symbols_coff(work / "probe.map", "_" if bits == 32 else "")
            else:
                table = symbols_elf(cc, image)
            check(table, names, size, label)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang")
    run(parser.parse_args().cc)
