"""Checks that a console's GL layer has every OpenGL entry point the shared
renderer needs.

The renderer loads its functions by name (port/linux/src/gl.h's
GL_FUNCTIONS list, gl_functions.c); a console port answers those lookups from
its own table (the Xbox 360's port/xbox360/src/x360_gl.c, the Wii U's
port/wiiu/src/wiiu_gl.c: `ENTRY(glName)` lines). A function the renderer
starts using that a console's table lacks makes gl_functions_load fail there,
and the console shows a black screen (glDeleteRenderbuffers, 2026-11-09).

    python3 tools/console_gl_check.py <console GL source> [...]

Each source is checked against the desktop list (consoles build without
HALO_GL_ES). A required function missing from a table fails; one the loader
treats as optional (gl_functions.c's GL_OPTIONAL_FUNCTION) is only listed.
The console repositories run this from their game submodule in CI."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GL_H = ROOT / "port" / "linux" / "src" / "gl.h"
GL_FUNCTIONS_C = ROOT / "port" / "linux" / "src" / "gl_functions.c"


def macro_body(text, name):
    """The X(...) names of a `#define name(X) \\` list, up to its last line."""
    match = re.search(r"#define " + re.escape(name) + r"\(X\)\s*\\\n((?:[^\n]*\\\n)*[^\n]*)", text)
    if not match:
        raise ValueError(f"no {name} list")
    return re.findall(r"X\((\w+)\)", match.group(1))


def desktop_functions(gl_h_text):
    """GL_FUNCTIONS of the build without HALO_GL_ES: the list after its #else."""
    start = gl_h_text.index("#ifdef HALO_GL_ES")
    branch = gl_h_text.index("\n#else\n", gl_h_text.index("ANDROID_GL_FUNCTIONS_END", start))
    end = gl_h_text.index("\n#endif", branch)
    names = macro_body(gl_h_text[branch:end], "GL_FUNCTIONS")
    if not names:
        raise ValueError("the desktop GL_FUNCTIONS list is empty")
    return names


def optional_functions(gl_functions_text):
    match = re.search(r"#define GL_OPTIONAL_FUNCTION\(name\)((?:[^\n]*\\\n)*[^\n]*)", gl_functions_text)
    if not match:
        raise ValueError("no GL_OPTIONAL_FUNCTION in gl_functions.c")
    return set(re.findall(r'"(gl\w+)"', match.group(1)))


def table_entries(console_text):
    # (the ENTRY macro's own #define line names no function)
    return set(re.findall(r"(?<!#define )\bENTRY\((gl\w+)\)", console_text))


def check(console_text, required, optional):
    """(missing required, missing optional) for one console table."""
    entries = table_entries(console_text)
    if not entries:
        raise ValueError("no ENTRY(gl...) table found")
    missing = [name for name in required if name not in entries]
    return [name for name in missing if name not in optional], [name for name in missing if name in optional]


def main(arguments):
    if not arguments:
        print(__doc__.strip())
        return 2
    required = desktop_functions(GL_H.read_text())
    optional = optional_functions(GL_FUNCTIONS_C.read_text())
    failed = False
    for path in arguments:
        missing, skipped = check(Path(path).read_text(), required, optional)
        name = Path(path).name
        if skipped:
            print(f"{name}: without the optional {', '.join(skipped)} (the renderer manages)")
        if missing:
            failed = True
            print(f"{name}: MISSING {len(missing)} the renderer needs: {', '.join(missing)}")
            print(f"{name}: add each to the ENTRY table, or the game stops at gl_functions_load with a black screen")
        else:
            print(f"{name}: all {len(required)} GL functions the renderer needs are in the table")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
