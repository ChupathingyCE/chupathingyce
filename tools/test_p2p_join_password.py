"""Runs the C tests of a password game's invites (port/linux/src/p2p_crypto.c,
as p2p.c and p2p_lobby.c use them) with the build machine's C compiler: the
host's link never holds the token it listens with, only the password opens
it, and older versions read the link as no invite. The unit needs no SDL or
game: the platform headers are left out, and the two functions it calls
from them are declared ahead."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def compiler():
    """Finds a C compiler on the build machine.

    Returns the path of clang, cc or gcc (the first found), or None.
    """
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_p2p_join_password(tmp_path):
    """Builds the invite code with its tests, warnings as errors, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    prelude = tmp_path / "prelude.h"
    prelude.write_text("void posix_random_bytes(void *buffer, unsigned long size);\n"
                       "void platform_log(const char *format, ...);\n")
    binary = tmp_path / "p2p_join_password_test"
    monocypher = ROOT / "port" / "third_party" / "monocypher"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         "-D__HALO_LINUX_PLATFORM_H", "-D__HALO_LINUX_POSIX_H", "-include", str(prelude),
         "-I", str(ROOT / "port" / "linux" / "src"), "-I", str(monocypher),
         str(ROOT / "port" / "linux" / "tests" / "p2p_join_password_test.c"),
         str(ROOT / "port" / "linux" / "src" / "p2p_crypto.c"),
         str(monocypher / "monocypher.c"), str(monocypher / "monocypher-ed25519.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout
