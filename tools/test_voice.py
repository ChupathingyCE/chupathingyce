"""Voice chat's C tests that need no game or device
(port/linux/tests/voice_test.c): the jitter buffer, and Opus as voice.c sets
it up (each frame within the host's size cap, FEC), built with the build
machine's C compiler against the port's own Opus (port/third_party/opus),
under AddressSanitizer and UndefinedBehaviorSanitizer where it has them.
The wire format and the host's routing are delta_test.c's
(tools/test_delta_peer.py)."""
import shutil
import subprocess
from pathlib import Path

import pytest

from tools import fuzz_support  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OPUS = ROOT / "port" / "third_party" / "opus"
SOURCES = [ROOT / "port" / "linux" / "tests" / "voice_test.c", ROOT / "port" / "linux" / "src" / "voice_jitter.c",
           ROOT / "port" / "linux" / "src" / "delta_wire.c"]
SANITIZERS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def opus_sources():
    return [OPUS / line.strip() for line in (OPUS / "sources.txt").read_text().splitlines() if line.strip()]


def build(cc, output, extra):
    opus_flags = ["-DOPUS_BUILD", "-DVAR_ARRAYS", "-DHAVE_LRINTF", "-DHAVE_LRINT",
                  *(f"-I{OPUS / name}" for name in ("include", "celt", "silk", "silk/float"))]
    objects = []
    for source in opus_sources():
        obj = output.parent / (source.relative_to(OPUS).as_posix().replace("/", "_") + ".o")
        result = subprocess.run([cc, "-std=gnu11", "-O2", "-w", *extra, *opus_flags, "-c", str(source), "-o", str(obj)],
                                capture_output=True, text=True)
        if result.returncode != 0:
            return result
        objects.append(str(obj))
    return subprocess.run(
        [cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2", "-g", *extra, f"-I{OPUS / 'include'}",
         "-iquote", str(ROOT / "port" / "linux" / "include"), *(str(source) for source in SOURCES), *objects,
         "-lm", "-o", str(output)],
        capture_output=True, text=True)


def test_jitter_and_codec(tmp_path):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "voice_test"
    result = build(cc, binary, SANITIZERS)
    if result.returncode != 0:
        fuzz_support.fallback("no sanitizer runtime")
        result = build(cc, binary, [])
    assert result.returncode == 0, result.stdout + result.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=600)
    print(run.stdout)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "voice_test: ok" in run.stdout
