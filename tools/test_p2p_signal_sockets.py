"""Internet play's connections to the signalling brokers
(port/linux/src/p2p_signal.c) over hours of brokers that misbehave: one
that stays up, one that refuses, one that never completes the connection,
one that refuses the CONNECT and one that drops it at once, with the
doorbell changing and the network coming back from a sleep now and then.
port/linux/tests/p2p_signal_sockets_test.c runs it on a simulated clock and
sockets and counts them: at most one open per broker, none left open that
no broker holds, and no broker connected more often than its retries allow
(#1009). Built with the build machine's C compiler, warnings as errors."""
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


def test_signalling_closes_every_connection(tmp_path):
    """Builds the test with p2p_signal.c and runs it."""
    cc = compiler()
    if not cc or sys.platform == "win32":
        pytest.skip("no C compiler, or not POSIX")
    output = tmp_path / "p2p_signal_sockets_test"
    build = subprocess.run(
        [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1", "-D__HALO_LINUX_PLATFORM_H", "-D__HALO_LINUX_POSIX_H",
         "-include", "port/linux/tests/p2p_fuzz_prelude.h", "-Iport/third_party/monocypher", "-Iport/linux/src",
         "port/linux/tests/p2p_signal_sockets_test.c", "port/linux/src/p2p_crypto.c", "port/linux/src/p2p_probe.c",
         "port/third_party/monocypher/monocypher.c", "port/third_party/monocypher/monocypher-ed25519.c",
         "-o", str(output)],
        capture_output=True, text=True, cwd=ROOT,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(output)], capture_output=True, text=True, timeout=120)
    assert run.returncode == 0, run.stdout + run.stderr
    assert run.stdout.rstrip().endswith("ok"), run.stdout
