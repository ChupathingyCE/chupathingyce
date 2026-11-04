"""Builds and runs the tunnel's KCP checks (port/linux/tests/kcp_peer_test.c):
what a peer's segments can do to port/third_party/kcp/ikcp.c, under
AddressSanitizer and UndefinedBehaviorSanitizer where the build machine's
compiler has them."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
KCP = ROOT / "port" / "third_party" / "kcp"
TEST = ROOT / "port" / "linux" / "tests" / "kcp_peer_test.c"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_kcp_peer(tmp_path):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "kcp_peer_test"
    sanitize = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
    for extra in (sanitize, []):
        build = subprocess.run([cc, "-std=gnu11", "-O1", "-g", "-w", *extra, str(TEST), str(KCP / "ikcp.c"),
                                "-o", str(binary)], capture_output=True, text=True)
        if build.returncode == 0:
            break
        # (no sanitizer runtime: again without)
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "kcp_peer_test: ok" in run.stdout
