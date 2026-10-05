"""Runs the C tests of the dedicated server's command lines and control API
(server/tests/control_test.c: server/src/command_line.c and
server/platform/control_protocol.c, with Monocypher) with the build
machine's C compiler, under AddressSanitizer and UndefinedBehaviorSanitizer
where it has them. The units have no SDL, socket or game dependency, so
they compile anywhere."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCES = [
    ROOT / "server" / "tests" / "control_test.c",
    ROOT / "server" / "src" / "command_line.c",
    ROOT / "server" / "platform" / "control_protocol.c",
    ROOT / "port" / "third_party" / "monocypher" / "monocypher.c",
]
SANITIZERS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]


def compiler():
    """Finds a C compiler on the build machine.

    Returns the path of clang, cc or gcc (the first found), or None.
    """
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def build(cc, binary, extra):
    return subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g", *extra,
         f"-I{ROOT / 'port' / 'third_party' / 'monocypher'}",
         *(str(source) for source in SOURCES), "-o", str(binary)],
        capture_output=True, text=True,
    )


def test_server_control(tmp_path):
    """Builds the units with their tests, warnings as errors (sanitized if the
    compiler can), and runs them with a few thousand random inputs."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "control_test"
    result = build(cc, binary, SANITIZERS)
    if result.returncode != 0:
        # (a compiler without the sanitizers' runtime: the tests unsanitized)
        result = build(cc, binary, [])
    assert result.returncode == 0, result.stdout + result.stderr
    run = subprocess.run([str(binary), "5000"], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
