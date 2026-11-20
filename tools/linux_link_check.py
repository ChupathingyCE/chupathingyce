#!/usr/bin/env python3
"""Reject a Linux link in which a weak reference has no definition.

The Linux build marks every MSVC inline function `#pragma weak` (see
tools/linux_msvc_semantics.py). A reference to such a name that no object
defines stays a weak undefined symbol, which the linker silently resolves to
address 0 - and then drops from a non-PIE executable's symbol table, so the
check has to look at the input objects rather than the output.

It also rejects an executable that imports glibc's wide character
functions: the game uses a 16-bit wchar_t, so they would misread its strings.
A static executable (--static: the dedicated server with musl,
tools/server_build.py) imports nothing, so there it rejects the objects'
references to wide character functions that no object defines, which the C
library would have supplied.

The units built with the host's own ABI and its 32-bit wchar_t, Jolt
Physics and its C face (tools/linux_build.py, JOLT_DIR), may call them: a
C++ library's inline code does (libc++'s std::find over 32-bit integers
calls wmemchr, in Jolt's ConvexHullBuilder), and there they read what the
C library expects. A wide function only those units call is let through.

Usage: linux_link_check.py [--static] <response file listing the objects> [executable]
"""

import os
import re
import shlex
import shutil
import subprocess
import sys
from typing import Dict, List, Set

# Weak references the C runtime and libgcc make themselves.
RUNTIME_WEAK_REFERENCES = {
    "__gmon_start__",
    "_ITM_deregisterTMCloneTable",
    "_ITM_registerTMCloneTable",
    "__cxa_finalize",
    "_Jv_RegisterClasses",
    "__pthread_key_create",
    "pthread_cancel",
}


# llvm-nm also reads the LLVM bitcode objects of link-time optimised builds
NM = os.environ.get("LLVM_NM") or shutil.which("llvm-nm") or "nm"


def symbol_table(objects: List[str]) -> Dict[str, Set[str]]:
    """Map each object symbol name to the set of nm type letters seen."""
    output = subprocess.run(
        [NM, "--format=posix", *objects],
        check=True, capture_output=True, text=True,
    ).stdout
    symbols: Dict[str, Set[str]] = {}
    for line in output.splitlines():
        fields = line.split()
        # "name type [value size]"; object headers end with ':'
        if len(fields) >= 2 and not fields[0].endswith(":") and len(fields[1]) == 1:
            symbols.setdefault(fields[0], set()).add(fields[1])
    return symbols


WIDE_FUNCTION = re.compile(r"(wcs|wmem|[a-z]*wprintf|towlower|towupper|isw)")

# the objects built with the host's ABI (a 32-bit wchar_t): Jolt's units and
# its C face (tools/linux_build.py, jolt_sources)
HOST_ABI_OBJECT = re.compile(r"(^|/)(port/third_party/jolt/.*|port/linux/game/physics_jolt)\.o$")


def wide_callers(objects: List[str]) -> Dict[str, Set[str]]:
    """Map each wide character function an object calls (and defines
    nowhere) to the objects that call it."""
    output = subprocess.run(
        [NM, "--format=posix", *objects],
        check=True, capture_output=True, text=True,
    ).stdout
    callers: Dict[str, Set[str]] = {}
    current = ""
    for line in output.splitlines():
        if line.endswith(":") and " " not in line:
            current = line[:-1]
            continue
        fields = line.split()
        if len(fields) >= 2 and fields[1] == "U":
            if WIDE_FUNCTION.match(fields[0]):
                callers.setdefault(fields[0], set()).add(current)
    return callers


def main() -> None:
    static = len(sys.argv) > 1 and sys.argv[1] == "--static"
    if static:
        del sys.argv[1]
    with open(sys.argv[1], "r", encoding="utf-8") as response:
        objects = [path for line in response for path in shlex.split(line)]
    symbols = symbol_table(objects)
    # global definitions only: a file-local static copy (t, d, ...) cannot
    # satisfy a reference from another object
    defined_types = set("TDBRCVW")
    missing = sorted(
        name
        for name, types in symbols.items()
        if types & {"w", "v"}
        and not types & defined_types
        and name not in RUNTIME_WEAK_REFERENCES
    )
    if static:
        wide = sorted(
            name for name, types in symbols.items()
            if "U" in types and not types & defined_types and WIDE_FUNCTION.match(name)
        )
        if wide:
            print("the objects call the C library's wide character functions, which assume "
                  "a 32-bit wchar_t: " + ", ".join(wide))
            sys.exit(1)
    elif len(sys.argv) > 2:
        imports = subprocess.run(
            [NM, "--undefined-only", sys.argv[2]],
            check=True, capture_output=True, text=True,
        ).stdout.split()
        wide = sorted(
            name for name in imports
            if WIDE_FUNCTION.match(name.split("@")[0])
        )
        if wide:
            # (those only the host's ABI's units call: theirs to call)
            callers = wide_callers(objects)
            wide = [name for name in wide
                    if not callers.get(name.split("@")[0])
                    or not all(HOST_ABI_OBJECT.search(caller) for caller in callers[name.split("@")[0]])]
        if wide:
            print("the executable imports glibc wide character functions, which assume "
                  "a 32-bit wchar_t: " + ", ".join(wide))
            sys.exit(1)
    if missing:
        print(f"{len(missing)} weak reference(s) that no object defines "
              "(they would silently resolve to address 0):")
        for name in missing:
            print(f"  {name}")
        sys.exit(1)


if __name__ == "__main__":
    main()
