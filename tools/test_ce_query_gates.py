"""Checks that cache_file_tags_are_ce() is only called where it exists.

cache_files.c defines cache_file_tags_are_ce() in a build with Halo PC maps
(HALO_CUSTOM_EDITION) alone. The Xbox 360 and the Wii U build without them,
so a call outside that gate fails their link: #1492's call in ui_widget.c
(draw_bitmap_in_rect) did. Code outside the gate asks CACHE_FILE_TAGS_ARE_CE()
(cache_files.h), which is FALSE in those builds and leaves the code out.

Every use of the function's own name in source/ and port/ must be inside an
#ifdef HALO_CUSTOM_EDITION (or an #if that requires it). This needs no
console toolchain, so it runs with the other tests on every pull request."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NAME = "cache_file_tags_are_ce"
GATE = "HALO_CUSTOM_EDITION"
# where it is defined and declared, each under the gate itself
OWN = {Path("source/cache/cache_files.c"), Path("source/cache/cache_files.h")}

DIRECTIVE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)")


def requires_gate(kind, condition):
    """whether this branch is only compiled with HALO_CUSTOM_EDITION"""
    condition = condition.split("//")[0].split("/*")[0].strip()
    if kind == "ifdef":
        return condition == GATE
    if kind in ("if", "elif"):
        if "||" in condition:
            return False
        defined = r"defined\s*\(?\s*" + GATE + r"\b"
        if re.search(r"!\s*" + defined, condition):
            return False
        return re.search(defined, condition) is not None
    return False


def ungated_uses(text):
    stack = []  # one entry a conditional: whether its current branch requires the gate
    found = []
    for number, line in enumerate(text.splitlines(), 1):
        match = DIRECTIVE.match(line)
        if match:
            kind, rest = match.group(1), match.group(2)
            if kind in ("if", "ifdef", "ifndef"):
                stack.append([requires_gate(kind, rest), kind == "ifndef" and rest.strip().split()[:1] == [GATE]])
            elif stack and kind == "elif":
                stack[-1] = [requires_gate(kind, rest), False]
            elif stack and kind == "else":
                # the #else of an #ifndef HALO_CUSTOM_EDITION requires it
                stack[-1] = [stack[-1][1], False]
            elif stack and kind == "endif":
                stack.pop()
            continue
        if re.search(r"\b" + NAME + r"\b", line) and not any(frame[0] for frame in stack):
            found.append((number, line.strip()))
    return found


def test_ce_query_gated():
    problems = []
    for top in ("source", "port"):
        for path in sorted((ROOT / top).rglob("*.[ch]")):
            relative = path.relative_to(ROOT)
            if relative in OWN or "third_party" in relative.parts:
                continue
            text = path.read_text(errors="replace")
            if NAME not in text:
                continue
            for number, line in ungated_uses(text):
                problems.append(f"{relative}:{number}: {line}")
    assert not problems, (
        f"{NAME}() outside #ifdef {GATE} (a build without Halo PC maps has no such function; "
        "use CACHE_FILE_TAGS_ARE_CE() from cache_files.h):\n" + "\n".join(problems))


def test_ce_query_check_catches_ungated():
    sample = ("#ifdef HALO_CUSTOM_EDITION\nx = cache_file_tags_are_ce();\n#else\n"
              "y = cache_file_tags_are_ce();\n#endif\n"
              "#if !defined(HALO_XBOX_CONSOLE) && !defined(HALO_CONSOLE_ANAMORPHIC_SCREEN)\n"
              "z = cache_file_tags_are_ce();\n#endif\n"
              "#if defined(HALO_CUSTOM_EDITION) && !defined(HALO_XBOX_CONSOLE)\n"
              "w = cache_file_tags_are_ce();\n#endif\n")
    assert [number for number, _ in ungated_uses(sample)] == [4, 7]
