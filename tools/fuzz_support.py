"""What the fuzz and sanitizer tests share: finding a clang with libFuzzer,
skipping (or, where HALO_SANITIZERS_REQUIRED=1, failing) without one, and
running a fuzzer for a bounded time on its seeds and saved regressions.

CI sets HALO_SANITIZERS_REQUIRED=1 on the Linux job, which installs clang's
runtimes (libclang-rt-dev): there, a test that would build unsanitized or
skip its fuzzer for want of a runtime fails instead, so a broken runtime is
never mistaken for a pass. Elsewhere (Apple's clang has no libFuzzer) they
fall back or skip as before.
"""

import os
import sys
import shutil
import subprocess
import tempfile
from pathlib import Path

REQUIRED = "HALO_SANITIZERS_REQUIRED"


def required():
    """whether a missing sanitizer runtime or libFuzzer fails the test"""
    return os.environ.get(REQUIRED) == "1"


def fallback(reason):
    """a test about to build without its sanitizers: an AssertionError where
    they are required"""
    assert not required(), f"{reason} ({REQUIRED}=1)"


def skip(reason):
    """pytest's skip, or a failure where the sanitizers are required"""
    import pytest

    if required():
        pytest.fail(f"{reason} ({REQUIRED}=1)")
    pytest.skip(reason)


def libfuzzer_compiler(*preferred):
    """a clang that links -fsanitize=fuzzer,address,undefined (Apple's does
    not ship libFuzzer; Homebrew's LLVM and Linux's clang with its runtimes
    do), or None"""
    candidates = [*preferred, os.environ.get("HALO_FUZZ_CC"), os.environ.get("DELTA_FUZZ_CC"), "clang",
                  "/opt/homebrew/opt/llvm/bin/clang", "/usr/local/opt/llvm/bin/clang"]
    with tempfile.TemporaryDirectory() as directory:
        probe = Path(directory) / "probe.c"
        probe.write_text("int LLVMFuzzerTestOneInput(const unsigned char *d, unsigned long s)"
                         " { (void)d; (void)s; return 0; }\n")
        for candidate in candidates:
            path = candidate and shutil.which(candidate)
            if not path:
                continue
            result = subprocess.run([path, "-fsanitize=fuzzer,address,undefined", str(probe), "-o",
                                     str(Path(directory) / "probe")], capture_output=True, text=True)
            if result.returncode == 0:
                return path
    return None


FUZZ_ENVIRONMENT = {
    # (leaks only on Linux: on macOS libFuzzer's own threads are reported at exit)
    "ASAN_OPTIONS": f"detect_leaks={0 if sys.platform == 'darwin' else 1}:abort_on_error=0:allocator_may_return_null=1",
    "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1",
}


def run_fuzzer(binary, workdir, seconds, seeds=(), regressions=None, max_len=4096, extra=(), timeout_per_input=5,
               corpus=None, jobs=1):
    """binary's regressions (each input once), then seconds of fuzzing from
    its seeds (a corpus folder kept in corpus, else one in workdir): the
    completed process of the first that failed, else of the fuzzing. A hang
    (an input over timeout_per_input seconds) fails as a crash does; each
    process has a hard limit of its own besides."""
    workdir = Path(workdir)
    environment = dict(os.environ, **FUZZ_ENVIRONMENT)
    if regressions and Path(regressions).is_dir():
        inputs = sorted(str(path) for path in Path(regressions).iterdir() if path.is_file())
        if inputs:
            result = subprocess.run([str(binary), f"-timeout={timeout_per_input}", f"-max_len={max_len}", *extra,
                                     *inputs], capture_output=True, text=True, cwd=workdir, env=environment,
                                    timeout=60 + timeout_per_input * len(inputs))
            if result.returncode != 0:
                return result
    corpus = Path(corpus) if corpus else workdir / "corpus"
    corpus.mkdir(parents=True, exist_ok=True)
    folders = [str(corpus)] + [str(seed) for seed in seeds if Path(seed).is_dir()]
    command = [str(binary), f"-max_total_time={int(seconds)}", f"-timeout={timeout_per_input}",
               f"-max_len={max_len}", "-rss_limit_mb=2048", "-print_final_stats=1", *extra]
    if jobs > 1:
        command += [f"-fork={jobs}", "-ignore_crashes=0"]
    result = subprocess.run([*command, *folders], capture_output=True, text=True, cwd=workdir, env=environment,
                            timeout=int(seconds) + 300)
    # (with -fork, libFuzzer ends with a failed job's status shifted, which exit() cuts to 0:
    # the input it saved is what says a job failed)
    if result.returncode == 0 and any(workdir.glob(f"{kind}-*") for kind in ("crash", "timeout", "oom", "leak")):
        result.returncode = 1
    return result
