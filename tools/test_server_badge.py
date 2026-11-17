"""Server badges' certificates and proofs (port/linux/tests/server_badge_test.c,
docs/server-badges.md): the shared test certificate, which command's
tools/test_server_badge.py makes and puma's tests read, checked with the
test badge key; and the same build with this repository's own keys. No SDL,
socket or game dependency."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCES = [ROOT / "port" / "linux" / "tests" / "server_badge_test.c", ROOT / "port" / "linux" / "src" / "server_badge.c",
           ROOT / "port" / "third_party" / "monocypher" / "monocypher.c",
           ROOT / "port" / "third_party" / "monocypher" / "monocypher-ed25519.c"]
# (the public half of SHA-256 of "chupathingy test badge key": command's and puma's test key)
TEST_KEY = "a32edf54a17391184c72e23278f2908175f62951817ffdd7db8fc61bd0d074fd"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def c_bytes(hex_text: str) -> str:
    return ",".join(f"0x{byte:02x}" for byte in bytes.fromhex(hex_text))


def build_and_run(tmp_path, defines):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "server_badge_test"
    built = subprocess.run(
        [cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O1", "-g", *defines,
         f"-I{ROOT / 'port' / 'linux' / 'src'}", f"-I{ROOT / 'port' / 'third_party' / 'monocypher'}",
         *(str(source) for source in SOURCES), "-o", str(binary)],
        capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "server_badge_test: ok" in run.stdout


def test_with_the_test_key(tmp_path):
    build_and_run(tmp_path, ["-DSERVER_BADGE_TEST_SIGNED", f"-DHALO_BADGE_TEST_KEY={c_bytes(TEST_KEY)}",
                             f"-DHALO_BADGE_TEST_RECOVERY_KEY={c_bytes('00' * 32)}"])


def test_with_the_repository_keys(tmp_path):
    """server_badge_key.h as it is: all zero until the badge key set is
    pasted in, and then skipped (the test key build covers the checks)"""
    header = (ROOT / "port" / "linux" / "src" / "server_badge_key.h").read_text()
    if "0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00," not in header:
        pytest.skip("real badge keys: covered by the test key build")
    build_and_run(tmp_path, [])
