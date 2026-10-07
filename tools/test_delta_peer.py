"""Delta Peer's C tests (port/linux/tests/delta_test.c: the wire format,
a host and clients in one process, and a deterministic random-input test of
every parser and both sides' sessions), built with the build machine's C
compiler under AddressSanitizer and UndefinedBehaviorSanitizer where it has
them; the wire format built as C89 (Warthog's old compiler reads it); and,
where clang has libFuzzer, port/linux/tests/delta_fuzz.c run for a bounded
number of inputs. The units have no SDL, socket or game dependency."""
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "port" / "linux" / "include"
SESSIONS = [ROOT / "port" / "linux" / "src" / "delta_peer.c", ROOT / "port" / "linux" / "src" / "delta_wire.c"]
TEST = ROOT / "port" / "linux" / "tests" / "delta_test.c"
FUZZ = ROOT / "port" / "linux" / "tests" / "delta_fuzz.c"
SANITIZERS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
# random inputs the deterministic test feeds (DELTA_TEST_ITERATIONS to change)
ITERATIONS = int(os.environ.get("DELTA_TEST_ITERATIONS", "200000"))
FUZZ_RUNS = int(os.environ.get("DELTA_FUZZ_RUNS", "300000"))


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def compile_c(cc, sources, output, extra):
    # (-iquote: delta.h, not the game's own stdio.h and friends there)
    return subprocess.run(
        [cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O1", "-g", *extra, "-iquote", str(INCLUDE),
         *(str(source) for source in sources), "-o", str(output)],
        capture_output=True, text=True)


def test_sessions_and_random_inputs(tmp_path):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "delta_test"
    result = compile_c(cc, [TEST, *SESSIONS], binary, SANITIZERS)
    if result.returncode != 0:
        # (a compiler without the sanitizers' runtime: unsanitized)
        result = compile_c(cc, [TEST, *SESSIONS], binary, [])
    assert result.returncode == 0, result.stdout + result.stderr
    run = subprocess.run([str(binary), str(ITERATIONS)], capture_output=True, text=True, timeout=600)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "delta_test: ok" in run.stdout


def test_wire_format_is_c89(tmp_path):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    result = subprocess.run(
        [cc, "-std=c89", "-pedantic", "-Wall", "-Wextra", "-Werror", "-iquote", str(INCLUDE), "-c",
         str(ROOT / "port" / "linux" / "src" / "delta_wire.c"), "-o", str(tmp_path / "delta_wire.o")],
        capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def libfuzzer_compiler(directory):
    """a clang that links -fsanitize=fuzzer (Apple's does not ship it;
    Homebrew's LLVM and Linux's clang do)"""
    candidates = [os.environ.get("DELTA_FUZZ_CC"), "clang", "/opt/homebrew/opt/llvm/bin/clang",
                  "/usr/local/opt/llvm/bin/clang"]
    probe = Path(directory) / "probe.c"
    probe.write_text("int LLVMFuzzerTestOneInput(const unsigned char *d, unsigned long s)"
                     " { (void)d; (void)s; return 0; }\n")
    for candidate in candidates:
        path = candidate and shutil.which(candidate)
        if not path:
            continue
        result = subprocess.run([path, "-fsanitize=fuzzer", str(probe), "-o", str(Path(directory) / "probe")],
                                capture_output=True, text=True)
        if result.returncode == 0:
            return path
    return None


def test_libfuzzer(tmp_path):
    with tempfile.TemporaryDirectory() as directory:
        cc = libfuzzer_compiler(directory)
    if not cc:
        pytest.skip("no clang with libFuzzer")
    binary = tmp_path / "delta_fuzz"
    result = subprocess.run(
        [cc, "-g", "-O1", "-fsanitize=fuzzer,address,undefined", "-fno-sanitize-recover=all", "-iquote",
         str(INCLUDE), str(FUZZ), *(str(source) for source in SESSIONS), "-o", str(binary)],
        capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    run = subprocess.run([str(binary), f"-runs={FUZZ_RUNS}", "-max_len=1300", "-seed=1"],
                         capture_output=True, text=True, timeout=600, cwd=tmp_path)
    assert run.returncode == 0, run.stdout[-4000:] + run.stderr[-4000:]
