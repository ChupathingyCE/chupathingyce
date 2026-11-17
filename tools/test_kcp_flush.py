"""Builds and runs port/linux/tests/kcp_flush_test.c: a stream's message
leaves in the loop pass that wrote it (p2p.c flushes KCP after its writes),
not on KCP's next tick, and a pass of many writes still flushes once. Prints
the write-to-send wait before and after."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
KCP = ROOT / "port" / "third_party" / "kcp"
TEST = ROOT / "port" / "linux" / "tests" / "kcp_flush_test.c"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_kcp_flush(tmp_path):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "kcp_flush_test"
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
    assert "kcp_flush_test: ok" in run.stdout
    print(run.stdout)
