"""Delta's numbers (port/linux/include/delta.h) against the network version
the game announces and joins (port/linux/include/halo_port_limits.h), and the
capability registry's bits. Plain text checks: the command repository's
tools read halo_port_limits.h's numbers as text too."""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DELTA = ROOT / "port" / "linux" / "include" / "delta.h"
LIMITS = ROOT / "port" / "linux" / "include" / "halo_port_limits.h"

ROW = re.compile(r'X\((\d+),\s*"(build-\d+)",\s*(additive|breaking)\)')
CAPABILITY = re.compile(r"_delta_capability_(\w+)\s*=\s*(\d+)")


def define(text, name):
    match = re.search(rf"^#define {name} (\d+)", text, re.M)
    assert match, f"{name} is not a number in halo_port_limits.h"
    return int(match.group(1))


def rows():
    text = DELTA.read_text(encoding="utf-8")
    table = text[text.index("#define DELTA_LEGACY_VERSIONS"):]
    found = [(int(number), build, kind) for number, build, kind in ROW.findall(table)]
    assert found, "delta.h's DELTA_LEGACY_VERSIONS has no rows"
    return found


def test_table_is_in_order():
    numbers = [number for number, _, _ in rows()]
    assert numbers == sorted(set(numbers)), f"versions out of order or repeated: {numbers}"
    builds = [int(build.split("-")[1]) for _, build, _ in rows()]
    assert builds == sorted(builds), f"first builds out of order: {builds}"


def test_limits_follow_the_table():
    limits = LIMITS.read_text(encoding="utf-8")
    table = rows()
    newest = table[-1][0]
    last_breaking = max(number for number, _, kind in table if kind == "breaking")
    assert define(limits, "HALO_PORT_NETWORK_VERSION") == newest, \
        "hosts must announce the table's newest version"
    assert define(limits, "HALO_PORT_NETWORK_VERSION_MAXIMUM") == newest, \
        "clients must join hosts up to the table's newest version"
    assert define(limits, "HALO_PORT_NETWORK_VERSION_MINIMUM") == last_breaking, \
        "clients must join hosts back to the table's newest breaking version"


def test_capability_bits():
    found = [(name, int(bit)) for name, bit in CAPABILITY.findall(DELTA.read_text(encoding="utf-8"))]
    bits = [bit for _, bit in found]
    assert bits == list(range(len(bits))), f"capability bits must be 0, 1, 2... without gaps: {found}"
    assert len(bits) <= 32, "the handshake carries 32 capability bits"


def test_header_compiles():
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "delta_check.c"
        source.write_text(
            '#include "delta.h"\n'
            "#define ROW(number, build, kind) number,\n"
            "static const int versions[] = { DELTA_LEGACY_VERSIONS(ROW) };\n"
            "_Static_assert(NUMBER_OF_DELTA_CAPABILITIES <= 32, \"capabilities\");\n"
            "_Static_assert(DELTA_ADVERTISED_FLAG == 0x04, \"flag\");\n"
            "int main(void) { return versions[0] > 0 ? 0 : 1; }\n", encoding="utf-8")
        subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", "-I", str(DELTA.parent), "-c", str(source),
                        "-o", str(Path(directory) / "delta_check.o")], check=True)
