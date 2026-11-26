"""Internet play: the game's sockets drop what claims to come from a peer's
address directly (port/linux/src/p2p.c, p2p_spoofed_source; xnet.c asks it).
port/linux/tests/p2p_spoofed_source_test.c checks which sources are dropped
on a machine with a joined peer, using the p2p tunnel fuzz target's stand-in
platform. Built with the build machine's C compiler, warnings as errors."""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def compiler():
    """Finds a C compiler: clang, cc or gcc (the first found), or None."""
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_spoofed_peer_sources_are_dropped(tmp_path):
    """Builds the test with p2p.c (through p2p_tunnel_fuzz.c) and runs it."""
    cc = compiler()
    if not cc or sys.platform == "win32":
        pytest.skip("no C compiler, or not POSIX")
    output = tmp_path / "p2p_spoofed_source_test"
    # (the third-party units as they are; the test, which includes p2p.c, with warnings as errors)
    objects = []
    for source in ("port/third_party/kcp/ikcp.c", "port/third_party/monocypher/monocypher.c",
                   "port/third_party/monocypher/monocypher-ed25519.c"):
        objects.append(tmp_path / (Path(source).stem + ".o"))
        build = subprocess.run([cc, "-std=gnu11", "-w", "-O1", "-c", source, "-o", str(objects[-1])],
                               capture_output=True, text=True, cwd=ROOT)
        assert build.returncode == 0, build.stdout + build.stderr
    build = subprocess.run(
        [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1",
         "-D__HALO_LINUX_PLATFORM_H", "-include", "port/linux/tests/p2p_fuzz_prelude.h",
         "-iquote", "port/linux/include", "-Iport/third_party/monocypher", "-Iport/third_party/kcp",
         "-Iport/linux/src",
         "port/linux/tests/p2p_spoofed_source_test.c", "port/linux/src/p2p_crypto.c", "port/linux/game/content_sha256.c", "port/linux/src/p2p_probe.c",
         "port/linux/src/p2p_copies.c", "port/linux/src/log_address.c", *map(str, objects),
         "-o", str(output)],
        capture_output=True, text=True, cwd=ROOT,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(output)], capture_output=True, text=True, timeout=60)
    assert run.returncode == 0, run.stdout + run.stderr
    assert run.stdout.rstrip().endswith("ok"), run.stdout
