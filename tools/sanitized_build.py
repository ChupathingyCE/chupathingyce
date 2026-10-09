"""A 64-bit build of the game with AddressSanitizer and UndefinedBehaviorSanitizer,
for playing maps under them (tools/test_map_fuzz.py fuzzes the loader's parts
alone; this runs the whole game).

It compiles every unit of an existing build (`python3 configure.py && ninja
macos` first, or linux64) with that build's own commands, as ninja runs them
(build.ninja), retargeted at build/<build>-asan and given the sanitizers, then
links them. Link-time optimization is left out, which the sanitizers do
without. The Xbox address space goes above AddressSanitizer's shadow memory
(xbox_address.h).

    python3 tools/sanitized_build.py [--build macos] [--jobs N]

The game then runs as any build does. AddressSanitizer's report goes to
stderr and stops it; UndefinedBehaviorSanitizer's are reported and the game
goes on (UBSAN_OPTIONS=halt_on_error=1 stops it). Misaligned reads and
writes, which real maps' tags have and the processors the game runs on
allow, floats converted to integers out of their range, and the decompiled
code's pointers one past an array (object-size) are not reported.
"""

import argparse
import os
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SANITIZE = ["-fsanitize=address,undefined", "-fno-sanitize=alignment,float-cast-overflow,object-size",
            "-fsanitize-recover=undefined", "-fno-omit-frame-pointer",
            "-DXBOX_ADDRESS_SPACE_BASE=0x300000000000ULL", "-O1", "-g"]


def commands(build):
    """the build's compile commands and its link command, as ninja runs them"""
    target = f"build/{build}/halo"
    lines = subprocess.run(["ninja", "-t", "commands", target], cwd=ROOT, check=True, capture_output=True,
                           text=True).stdout.splitlines()
    compiles = [shlex.split(line) for line in lines if " -c " in line and not line.startswith(("python", '"'))
                and "lp64_rewrite" not in line]
    link = shlex.split(lines[-1])
    return compiles, link


def retarget(words, build, output_dir):
    """a compile command with its output in output_dir and the sanitizers"""
    result = []
    skip = False
    prefix = f"build/{build}/obj/"
    for word in words:
        if skip:
            skip = False
            continue
        if word in ("-MF",):
            skip = True
            continue
        if word == "-MMD" or word.startswith("-flto") or word.startswith("-fprofile") or word == "-O2":
            continue
        if word.startswith(prefix):
            word = str(output_dir / "obj" / word[len(prefix):])
        result.append(word)
    return result + SANITIZE


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", default="macos")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--clean", action="store_true", help="compile every unit again")
    options = parser.parse_args()

    subprocess.run(["ninja", f"build/{options.build}/halo"], cwd=ROOT, check=True)
    output_dir = Path(f"build/{options.build}-asan")
    compiles, link = commands(options.build)
    objects = []

    def compile_one(words):
        command = retarget(words, options.build, output_dir)
        output = Path(command[command.index("-o") + 1])
        original = Path(words[words.index("-o") + 1])
        (ROOT / output).parent.mkdir(parents=True, exist_ok=True)
        # (an object newer than the build's own is current, if built with these
        # flags: --clean otherwise)
        if not options.clean and (ROOT / output).exists() and \
                (ROOT / output).stat().st_mtime >= (ROOT / original).stat().st_mtime:
            return None
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        if result.returncode:
            return f"{output}: {result.stderr[-2000:]}"
        return None

    with ThreadPoolExecutor(options.jobs) as pool:
        failures = [failure for failure in pool.map(compile_one, compiles) if failure]
    if failures:
        sys.exit("\n".join(failures))

    # the link: its response file's objects, retargeted
    words = []
    for word in link:
        if word.startswith("@"):
            # (the response file's: the target's explicit inputs)
            query = subprocess.run(["ninja", "-t", "query", f"build/{options.build}/halo"], cwd=ROOT, check=True,
                                   capture_output=True, text=True).stdout.splitlines()
            listed = []
            for line in query[2:]:
                if not line.startswith("    "):
                    break
                item = line.strip()
                if not item.startswith("|"):
                    listed.append(item)
            prefix = f"build/{options.build}/obj/"
            objects = [str(output_dir / "obj" / item[len(prefix):]) if item.startswith(prefix) else item
                       for item in listed]
            words += objects
        elif word.startswith("-flto") or word.startswith("-fprofile") or word.startswith("-Wl,-cache_path") or \
                word.startswith("-Wl,--thinlto"):
            continue
        elif word == f"build/{options.build}/halo":
            words.append(str(output_dir / "halo"))
        else:
            words.append(word)
    subprocess.run([*words, "-fsanitize=address,undefined"], cwd=ROOT, check=True)
    print(output_dir / "halo")


if __name__ == "__main__":
    main()
