"""Fuzz targets for what another machine can send a host: each parser on its
own, built with libFuzzer, AddressSanitizer and UndefinedBehaviorSanitizer
(tools/fuzz_support.py), run on every input saved in its regressions
(tools/tests/host_fuzz/<name>/regressions: each crash once found and
fixed), then from its seeds (seeds/ beside it), then for a few seconds of fuzzing
(HOST_FUZZ_SECONDS, 3 by default). Without a clang that has libFuzzer the
tests are skipped (they fail where HALO_SANITIZERS_REQUIRED=1, as in CI).

For longer runs (a crash's input is left in the working folder, crash-* or
timeout-*; a fixed one goes in the regressions folder, if it holds nobody's
data):

    python3 tools/test_host_fuzz.py --target control --seconds 3600 [--jobs 4] [--corpus DIR]
    python3 tools/test_host_fuzz.py --list
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from tools import fuzz_support  # noqa: E402

try:
    import pytest
except ImportError:
    pytest = None

TARGETS_DIR = ROOT / "tools" / "tests" / "host_fuzz"
SECONDS = int(os.environ.get("HOST_FUZZ_SECONDS", "3"))


def load_targets():
    """each target's folder, tools/tests/host_fuzz/<name>: target.json (the
    harness, the units it takes (paths from the repository's root),
    includes, other compiler flags and arguments to libFuzzer, the longest
    input), seeds/ and regressions/"""
    targets = {}
    for path in sorted(TARGETS_DIR.glob("*/target.json")):
        target = json.loads(path.read_text(encoding="utf-8"))
        target["folder"] = path.parent
        targets[path.parent.name] = target
    return targets


TARGETS = load_targets()

SANITIZE = ["-g", "-O1", "-fsanitize=fuzzer,address,undefined", "-fno-sanitize-recover=all",
            "-fno-omit-frame-pointer"]


def build_game(name, directory, cc):
    """a target of the game's own sources (target.json's game_sources, built
    with the build's flags as tools/test_network_messages.py builds them,
    through ninja: `python3 configure.py` first), and its harness: the
    fuzzer's path. game_sizes names the source that also gets the size of
    each network message's structure (network_messages_test_sizes)."""
    from tools import test_network_messages as messages

    target = TARGETS[name]
    directory = Path(directory)
    build_name = "macos" if sys.platform == "darwin" else "linux64"
    # (the encoder reads unaligned on purpose: the wire has no alignment)
    flags = ["-g", "-O1", "-fsanitize=fuzzer-no-link,address,undefined", "-fno-sanitize=alignment",
             "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    objects = []
    for source in target["game_sources"]:
        output = directory / (Path(source).stem + ".o")
        extra = {"flags": flags}
        if source == target.get("game_sizes"):
            words, _ = messages.object_command(build_name, source)
            rewritten = next(word for word in words if word.endswith(source))
            sizes = directory / "sizes.c"
            sizes.write_text(f'#include "{(ROOT / rewritten).as_posix()}"\n'
                             "const int network_messages_test_sizes[] =\n{\n"
                             + "".join(f"\tsizeof({entry}),\n" for entry in messages.message_names()) + "};\n")
            extra["source"] = str(sizes)
        command = messages.compile_command(build_name, source, output, extra)
        command[0] = cc
        result = subprocess.run(command, capture_output=True, text=True, cwd=ROOT)
        if result.returncode != 0:
            raise RuntimeError(f"{name}: the build failed\n{result.stdout}{result.stderr}")
        objects.append(str(output))
    words, _ = messages.object_command(build_name, target["game_sources"][0])
    binary = directory / f"{name}_fuzz"
    command = [cc, *(word for word in words if word.startswith("--target=")), *SANITIZE, "-o", str(binary),
               str(ROOT / target["harness"]), *objects]
    result = subprocess.run(command, capture_output=True, text=True, cwd=ROOT)
    if result.returncode != 0:
        raise RuntimeError(f"{name}: the build failed\n{result.stdout}{result.stderr}")
    return binary


def build(name, directory, cc):
    """the target's fuzzer, built in directory: its path"""
    target = TARGETS[name]
    if "game_sources" in target:
        return build_game(name, directory, cc)
    binary = Path(directory) / f"{name}_fuzz"
    command = [cc, *SANITIZE, *target.get("flags", []), "-iquote", str(ROOT / "port" / "linux" / "include"),
               *(f"-I{ROOT / folder}" for folder in target.get("includes", [])),
               str(ROOT / target["harness"]), *(str(ROOT / source) for source in target["sources"]),
               *target.get("libraries", []), "-o", str(binary)]
    result = subprocess.run(command, capture_output=True, text=True, cwd=ROOT)
    if result.returncode != 0:
        raise RuntimeError(f"{name}: the build failed\n{result.stdout}{result.stderr}")
    return binary


def fuzz(name, seconds, workdir, cc, corpus=None, jobs=1):
    binary = build(name, workdir, cc)
    return fuzz_support.run_fuzzer(binary, workdir, seconds, seeds=[TARGETS[name]["folder"] / "seeds"],
                                   regressions=TARGETS[name]["folder"] / "regressions",
                                   max_len=TARGETS[name]["max_len"], extra=TARGETS[name].get("extra", []),
                                   corpus=corpus, jobs=jobs)


if pytest:
    @pytest.mark.parametrize("name", sorted(TARGETS))
    def test_host_fuzz(name, tmp_path):
        cc = fuzz_support.libfuzzer_compiler()
        if not cc:
            fuzz_support.skip("no clang with libFuzzer")
        if "game_sources" in TARGETS[name] and not (ROOT / "build.ninja").exists():
            fuzz_support.skip("the game's sources need build.ninja (python3 configure.py)")
        result = fuzz(name, SECONDS, tmp_path, cc)
        assert result.returncode == 0, result.stdout[-4000:] + result.stderr[-8000:]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--target", action="append", help="a target (every one if none)")
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--corpus", help="a folder to keep the corpus in (one folder per target under it)")
    parser.add_argument("--keep-failures", metavar="DIR", help="copy a failing target's inputs (crash-*, timeout-*) here")
    parser.add_argument("--list", action="store_true")
    options = parser.parse_args()
    if options.list:
        print("\n".join(sorted(TARGETS)))
        return 0
    cc = fuzz_support.libfuzzer_compiler()
    if not cc:
        print("no clang with libFuzzer (HALO_FUZZ_CC)", file=sys.stderr)
        return 2
    failed = 0
    for name in options.target or sorted(TARGETS):
        workdir = Path(tempfile.mkdtemp(prefix=f"host_fuzz_{name}_"))
        corpus = Path(options.corpus) / name if options.corpus else None
        result = fuzz(name, options.seconds, workdir, cc, corpus, options.jobs)
        # (the stats' last lines: what it ran)
        tail = [line for line in result.stderr.splitlines() if line.startswith(("stat::", "Done", "#", "=="))]
        print(f"{name}: exit {result.returncode}, {workdir}")
        print("\n".join(tail[-6:]))
        if result.returncode != 0:
            failed += 1
            print(result.stderr[-6000:])
            if options.keep_failures:
                for found in workdir.glob("*-*"):
                    if found.is_file() and found.name.split("-")[0] in ("crash", "timeout", "leak", "oom"):
                        shutil.copy(found, Path(options.keep_failures) / f"{name}-{found.name}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
