"""Runs the C tests of a console's Link Profile (port/console/link/
console_link.c: port/console/tests/console_link_test.c) with the build
machine's C compiler: the vectors the site's test_console_link.py has, the
site's answers read, and a whole link against a site the test plays. The
unit needs no game: the platform headers p2p_crypto.c includes are left out,
and the two functions it calls from them are declared ahead."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_console_link(tmp_path):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    prelude = tmp_path / "prelude.h"
    prelude.write_text("void posix_random_bytes(void *buffer, unsigned long size);\n"
                       "void platform_log(const char *format, ...);\n")
    binary = tmp_path / "console_link_test"
    monocypher = ROOT / "port" / "third_party" / "monocypher"
    qrcodegen = ROOT / "port" / "third_party" / "qrcodegen"
    link = ROOT / "port" / "console" / "link"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         "-D__HALO_LINUX_PLATFORM_H", "-D__HALO_LINUX_POSIX_H", "-include", str(prelude),
         "-I", str(link), "-I", str(ROOT / "port" / "linux" / "src"), "-I", str(monocypher), "-I", str(qrcodegen),
         str(ROOT / "port" / "console" / "tests" / "console_link_test.c"), str(link / "console_link.c"),
         str(ROOT / "port" / "linux" / "src" / "p2p_crypto.c"), str(qrcodegen / "qrcodegen.c"),
         str(monocypher / "monocypher.c"), str(monocypher / "monocypher-ed25519.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary), str(tmp_path)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout
