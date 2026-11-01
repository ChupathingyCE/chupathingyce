"""Delta on a big-endian machine: the C tests of Delta Peer
(port/linux/tests/delta_test.c, its wire byte order among them) and of the
wire compatibility ranges (port/linux/tests/delta_compat_test.c), built for
a big-endian Linux and run under QEMU's user mode. The Xbox 360 and the Wii U
are big-endian PowerPC, the PCs and the original Xbox little-endian: the wire
is little-endian, and these say a big-endian build writes and reads the same
bytes as a PC's.

It needs a big-endian cross compiler and its QEMU (CI installs
gcc-s390x-linux-gnu and qemu-user, and sets DELTA_BE_REQUIRED=1 so that
their absence fails), and skips without them. DELTA_BE_CC and
DELTA_BE_QEMU name others (for example powerpc-linux-gnu-gcc and qemu-ppc)."""
import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "port" / "linux" / "include"
SRC = ROOT / "port" / "linux" / "src"
MONOCYPHER = ROOT / "port" / "third_party" / "monocypher"
SESSIONS = [SRC / "delta_peer.c", SRC / "delta_wire.c", SRC / "delta_chat.c", SRC / "delta_chat_words.c",
            SRC / "delta_chat_bridge.c", MONOCYPHER / "monocypher.c", MONOCYPHER / "monocypher-ed25519.c"]
# (a cross compiler and the QEMU that runs what it builds)
CANDIDATES = [
    ("s390x-linux-gnu-gcc", "qemu-s390x"),
    ("powerpc-linux-gnu-gcc", "qemu-ppc"),
    ("powerpc64-linux-gnu-gcc", "qemu-ppc64"),
]
ITERATIONS = os.environ.get("DELTA_BE_ITERATIONS", "20000")


def toolchain():
    named = os.environ.get("DELTA_BE_CC"), os.environ.get("DELTA_BE_QEMU")
    for cc, qemu in ([named] if all(named) else []) + CANDIDATES:
        cc_path = shutil.which(cc)
        qemu_path = shutil.which(qemu) or shutil.which(qemu + "-static")
        if cc_path and qemu_path:
            return cc_path, qemu_path
    return None


def build_and_run(tmp_path, name, sources, arguments):
    tools = toolchain()
    if not tools:
        assert os.environ.get("DELTA_BE_REQUIRED") != "1", "no big-endian cross compiler and QEMU"
        pytest.skip("no big-endian cross compiler and QEMU")
    cc, qemu = tools
    binary = tmp_path / name
    build = subprocess.run(
        [cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O1", "-static", "-iquote", str(INCLUDE), f"-I{MONOCYPHER}",
         *(str(source) for source in sources), "-o", str(binary)], capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    # (the machine is big-endian: a test built for a little-endian one by
    # mistake would prove nothing)
    probe = tmp_path / "probe.c"
    probe.write_text("int main(void) { unsigned int one = 1; return *(unsigned char *)&one; }\n")
    built = subprocess.run([cc, "-static", str(probe), "-o", str(tmp_path / "probe")], capture_output=True, text=True)
    assert built.returncode == 0, built.stderr
    assert subprocess.run([qemu, str(tmp_path / "probe")]).returncode == 0, "the target is not big-endian"
    run = subprocess.run([qemu, str(binary), *arguments], capture_output=True, text=True, timeout=900)
    assert run.returncode == 0, run.stdout[-4000:] + run.stderr[-4000:]
    return run.stdout


def test_sessions_big_endian(tmp_path):
    output = build_and_run(tmp_path, "delta_test", [ROOT / "port" / "linux" / "tests" / "delta_test.c", *SESSIONS],
                           [ITERATIONS])
    assert "delta_test: ok" in output


def test_compat_big_endian(tmp_path):
    output = build_and_run(tmp_path, "delta_compat_test",
                           [ROOT / "port" / "linux" / "tests" / "delta_compat_test.c", SRC / "delta_compat.c"], [])
    assert "delta_compat_test: ok" in output
