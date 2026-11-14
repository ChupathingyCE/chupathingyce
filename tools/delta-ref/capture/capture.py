# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
"""Golden packets and the interop test for the Delta reference client
(tools/delta-ref), built from the game's own Delta code in this repository.

    capture.py golden [--check]
        builds capture/golden_capture.c with port/linux/src/delta_wire.c,
        delta_compat.c and browser.c's form encoding, and writes
        tools/delta-ref/golden.h (--check: fails if the file differs, so a
        change to our wire shows up as a change to the goldens)
    capture.py interop
        builds capture/interop_test.c: the reference client against the
        game's real Delta Peer host and client sessions (delta_peer.c), in
        one process, and runs it

Needs a C compiler (CC, else clang, cc or gcc). Builds in a temporary folder
and leaves nothing behind."""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
KIT = HERE.parent
ROOT = KIT.parent.parent
INCLUDE = ROOT / "port" / "linux" / "include"
SRC = ROOT / "port" / "linux" / "src"
MONOCYPHER = ROOT / "port" / "third_party" / "monocypher"
GOLDEN = KIT / "golden.h"
SESSIONS = [SRC / "delta_peer.c", SRC / "delta_wire.c", SRC / "delta_chat.c", SRC / "delta_chat_words.c",
            SRC / "delta_chat_bridge.c", MONOCYPHER / "monocypher.c", MONOCYPHER / "monocypher-ed25519.c"]


def compiler():
    named = os.environ.get("CC")
    if named and shutil.which(named):
        return shutil.which(named)
    for name in ("clang", "cc", "gcc"):
        if shutil.which(name):
            return shutil.which(name)
    sys.exit("capture.py: no C compiler")


def form_add_source():
    """browser.c's form_add, taken whole from the file"""
    text = (SRC / "browser.c").read_text(encoding="utf-8")
    match = re.search(r"^static void form_add\(char \*form, int size, const char \*name, const char \*value\)\n\{\n"
                      r".*?^\}\n", text, re.S | re.M)
    if not match:
        sys.exit("capture.py: form_add not found in browser.c")
    return match.group(0)


def build(cc, sources, output, include_dirs):
    command = [cc, "-std=c99", "-O1", "-w", "-iquote", str(INCLUDE), "-iquote", str(SRC), f"-I{MONOCYPHER}",
               *(f"-I{folder}" for folder in include_dirs), *(str(source) for source in sources), "-o", str(output)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit("capture.py: build failed\n" + result.stdout + result.stderr)


def golden(check):
    cc = compiler()
    with tempfile.TemporaryDirectory(prefix="delta-ref-") as work:
        work = Path(work)
        (work / "form_add.inc").write_text("#include <stdio.h>\n#include <string.h>\n" + form_add_source())
        binary = work / "golden_capture"
        build(cc, [HERE / "golden_capture.c", SRC / "delta_wire.c", SRC / "delta_compat.c"], binary, [work])
        output = subprocess.run([str(binary)], capture_output=True, text=True, check=True).stdout
    if check:
        if not GOLDEN.exists() or GOLDEN.read_text() != output:
            sys.exit("capture.py: golden.h differs from what the game's code writes now; run capture.py golden "
                     "and look at what changed")
        print("capture.py: golden.h matches the game's code")
    else:
        GOLDEN.write_text(output)
        print(f"capture.py: wrote {GOLDEN.relative_to(ROOT)}")


def interop():
    cc = compiler()
    with tempfile.TemporaryDirectory(prefix="delta-ref-") as work:
        binary = Path(work) / "interop_test"
        build(cc, [HERE / "interop_test.c", KIT / "delta_ref.c", *SESSIONS], binary, [KIT])
        run = subprocess.run([str(binary)], capture_output=True, text=True)
    sys.stdout.write(run.stdout)
    sys.stderr.write(run.stderr)
    if run.returncode != 0 or "interop_test: ok" not in run.stdout:
        sys.exit("capture.py: interop test failed")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    golden_parser = sub.add_parser("golden")
    golden_parser.add_argument("--check", action="store_true")
    sub.add_parser("interop")
    args = parser.parse_args()
    if args.command == "golden":
        golden(args.check)
    else:
        interop()


if __name__ == "__main__":
    main()
