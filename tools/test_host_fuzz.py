"""Fuzz targets for what another machine can send a host: each parser on its
own, built with libFuzzer, AddressSanitizer and UndefinedBehaviorSanitizer
(tools/fuzz_support.py), run on every input saved in its regressions
(tools/tests/host_fuzz/<name>/regressions: each crash once found and
fixed), then from its seeds (seeds/ beside it), then for a few seconds of fuzzing
(HOST_FUZZ_SECONDS, 3 by default). Without a clang that has libFuzzer the
tests are skipped (they fail where HALO_SANITIZERS_REQUIRED=1, as in CI).

A target may also take game units (its "game_sources"), compiled with the
game build's own commands (build.ninja: `python3 configure.py` first;
HOST_FUZZ_BUILD names the build, macos or linux64 by default): without
that build it is skipped.

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


# the build whose units a target's "game_sources" are compiled as (ninja
# macos, or linux64: `python3 configure.py` first)
GAME_BUILD = os.environ.get("HOST_FUZZ_BUILD", "macos" if sys.platform == "darwin" else "linux64")
# (the game reads unaligned on purpose: the wire has no alignment)
GAME_SANITIZE = ["-fsanitize=fuzzer-no-link,address,undefined", "-fno-sanitize=alignment", "-fno-sanitize-recover=all",
                 "-fno-omit-frame-pointer", "-O1", "-g"]


def game_ready(target):
    """whether the target's game units can be compiled as the build does
    (build.ninja has the build, and ninja builds its first unit)"""
    from tools.test_network_messages import object_command

    if not target.get("game_sources"):
        return True
    if not (ROOT / "build.ninja").is_file():
        return False
    first = target["game_sources"][0]
    try:
        object_command(GAME_BUILD, first if isinstance(first, str) else first["source"])
    except (subprocess.CalledProcessError, IndexError, OSError):
        return False
    return True


def game_objects(target, directory, cc):
    """a target's "game_sources", compiled with the game's own commands (from
    build.ninja) and the sanitizers: each a unit's path, or {"source": unit,
    "append": file}, the unit as the build has it with file after it (sizes
    of its structures, say); their objects and the link's --target"""
    from tools.test_network_messages import compile_command, object_command

    objects = []
    link_target = []
    for entry in target.get("game_sources", []):
        source = entry if isinstance(entry, str) else entry["source"]
        words, _ = object_command(GAME_BUILD, source)
        link_target = [word for word in words if word.startswith("--target=")]
        # (and the target's own: an Xbox address space where the sanitizers
        # leave room for it, say, as tools/test_map_fuzz.py places it)
        extra = {"flags": GAME_SANITIZE + target.get("game_flags", [])}
        if not isinstance(entry, str) and entry.get("append"):
            # (the unit as the build compiles it, rewritten for LP64 where it is)
            built = next(word for word in words if word.endswith(source))
            wrapped = Path(directory) / f"{Path(source).stem}_with_{Path(entry['append']).stem}.c"
            wrapped.write_text(f'#include "{(ROOT / built).as_posix()}"\n' +
                               (ROOT / entry["append"]).read_text(encoding="utf-8"))
            extra["source"] = str(wrapped)
        output = Path(directory) / (Path(source).stem + ".o")
        command = compile_command(GAME_BUILD, source, output, extra)
        # (the build's compiler, after a launcher if it has one: ccache in CI)
        if Path(command[0]).name in ("ccache", "sccache"):
            command = command[1:]
        command[0] = cc
        result = subprocess.run(command, capture_output=True, text=True, cwd=ROOT)
        if result.returncode != 0:
            raise RuntimeError(f"{source}: the build failed\n{result.stdout}{result.stderr}")
        objects.append(str(output))
    return objects, link_target


def build(name, directory, cc):
    """the target's fuzzer, built in directory: its path"""
    target = TARGETS[name]
    binary = Path(directory) / f"{name}_fuzz"
    objects, link_target = game_objects(target, directory, cc)
    command = [cc, *link_target, *SANITIZE, *target.get("flags", []), "-iquote", str(ROOT / "port" / "linux" / "include"),
               *(f"-I{ROOT / folder}" for folder in target.get("includes", [])),
               str(ROOT / target["harness"]), *(str(ROOT / source) for source in target.get("sources", [])),
               *objects, *target.get("libraries", []), "-o", str(binary)]
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
        if not game_ready(TARGETS[name]):
            fuzz_support.skip(f"the game's units: python3 configure.py first, with the {GAME_BUILD} build")
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
        if not game_ready(TARGETS[name]):
            print(f"{name}: skipped (the game's units: python3 configure.py first, with the {GAME_BUILD} build)")
            continue
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
