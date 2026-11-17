"""Structure culling on a big-endian machine (the Xbox 360 and Wii U): the
culling built from a terrain whose vertex bytes are the Xbox's, here and
under QEMU, must find as many triangles disagreeing with their vertex normals
on both, or not cull at all there.

#925: the culling read the bsp's vertices in the console's byte order, so on
the 360 one level triangle in nine disagreed (4639 of 40947 on Blood Gulch)
and the facing tests cut holes in the ground and the cliffs. #926 made the
big-endian builds fall back to the original walk until the reads are fixed.

The big-endian half needs a cross compiler and its QEMU (tools/
test_delta_big_endian.py's: CI installs gcc-s390x-linux-gnu and qemu-user,
and sets DELTA_BE_REQUIRED=1 so their absence fails); it skips without them."""
import re
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from harness import HARNESS, build, run  # noqa: E402
from test_structure_culling import generated  # noqa: E402

import test_delta_big_endian as big_endian  # noqa: E402

# the guard #926 put in structure_culling_traverse
GUARD = re.compile(r"#ifdef HALO_BIG_ENDIAN\n(?:[^#\n]*\n)*?#endif\n")


def counts(output):
    match = re.search(r"disagree (\d+) of (\d+), walk (culls|falls back)", output)
    assert match, output
    return int(match.group(1)), int(match.group(2)), match.group(3) == "culls"


def build_big_endian(tmp_path, parts):
    tools = big_endian.toolchain()
    if not tools:
        assert big_endian.os.environ.get("DELTA_BE_REQUIRED") != "1", "no big-endian cross compiler and QEMU"
        pytest.skip("no big-endian cross compiler and QEMU")
    cc, qemu = tools
    for name, text in parts:
        (tmp_path / name).write_text(text)
    binary = tmp_path / "structure_culling_byte_order"
    # (the console's define: the culling's own big-endian path, if it has one)
    command = [cc, "-std=gnu99", "-O1", "-static", "-DHALO_BIG_ENDIAN", "-w", "-I", str(HARNESS / "include"),
               "-I", str(tmp_path), str(HARNESS / "tests" / "structure_culling_byte_order.c"), "-o", str(binary), "-lm"]
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr[-4000:]
    probe = tmp_path / "probe.c"
    probe.write_text("int main(void) { unsigned int one = 1; return *(unsigned char *)&one; }\n")
    built = subprocess.run([cc, "-static", str(probe), "-o", str(tmp_path / "probe")], capture_output=True, text=True)
    assert built.returncode == 0, built.stderr
    assert subprocess.run([qemu, str(tmp_path / "probe")]).returncode == 0, "the target is not big-endian"
    ran = subprocess.run([qemu, str(binary), "terrain"], capture_output=True, text=True, timeout=300)
    assert ran.returncode == 0, ran.stdout[-4000:] + ran.stderr[-4000:]
    return counts(ran.stdout)


def little_endian():
    status, output = run(build("structure_culling_byte_order", generated()), "terrain")
    assert status == 0, output
    return counts(output)


def test_little_endian_agrees():
    disagree, triangles, culls = little_endian()
    assert triangles == 288 and disagree == 0 and culls


def test_big_endian_matches(tmp_path):
    disagree, triangles, culls = build_big_endian(tmp_path, generated())
    expected, expected_triangles, _ = little_endian()
    assert triangles == expected_triangles
    # (a big-endian build that culls must read the Xbox's bytes as a PC does)
    if culls:
        assert disagree == expected, f"big-endian: {disagree} of {triangles} triangles disagree, little-endian {expected}"


def test_negative_control_unguarded_reader(tmp_path):
    """Without #926's fallback, today's reader on a big-endian machine is
    caught. Once the reads are fixed and the fallback goes, this has nothing
    to show and skips; test_big_endian_matches covers it."""
    parts = dict(generated())
    if not GUARD.search(parts["under_test.inc"]):
        pytest.skip("structure_culling_traverse has no big-endian fallback: the reads are the console's own")
    parts["under_test.inc"] = GUARD.sub("", parts["under_test.inc"], count=1)
    disagree, triangles, culls = build_big_endian(tmp_path, tuple(parts.items()))
    expected, _, _ = little_endian()
    assert culls and disagree != expected, f"the byte order escaped: {disagree} of {triangles} disagree"
