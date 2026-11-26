"""The overlay (port/linux/src/ui_overlay.c, ui_overlay_style.c) is built only with the game
list (HALO_GAME_BROWSER): a --no-game-browser build has none of its functions, so a file
that build compiles may call them only where HALO_GAME_BROWSER (or the Guide's
HALO_GUIDE and the others that need it) is defined. Forge's delete prompt called ui_overlay_keyboard
in every build, and the --no-game-browser builds did not link (#1854)."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OVERLAY = [ROOT / "port" / "linux" / "src" / name for name in ("ui_overlay.c", "ui_overlay_style.c")]
# (the files every build compiles that may call it; a file of the game list's own is
# gated whole, below)
CALLERS = sorted((ROOT / "port" / "linux" / "game").glob("*.c")) + sorted((ROOT / "source").rglob("*.c"))
# (each defined only with HALO_GAME_BROWSER: halo_console_features.h)
GUARDS = ("HALO_GAME_BROWSER", "HALO_GUIDE", "HALO_MEDAL_ICONS", "HALO_HD_BUTTONS")
DIRECTIVE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)")
DEFINITION = re.compile(r"^(?:[A-Za-z_][\w ]*?[\s*])(ui_overlay\w*)\s*\([^;]*$", re.M)


def overlay_functions():
    names = set()
    for path in OVERLAY:
        text = path.read_text(encoding="latin-1")
        assert "#ifdef HALO_GAME_BROWSER" in text[:3000], f"{path.name} is no longer the game list's alone"
        names.update(match.group(1) for match in DEFINITION.finditer(text) if not match.group(0).startswith("static"))
    return names


def guarded(condition):
    """Whether a branch is compiled only with the game list: a positive test of a guard."""
    kind, expression = condition
    if kind == "ifndef" or kind == "else-of-guard":
        return False
    return any(re.search(r"(?<!!)\b(?:defined\s*\(\s*)?" + guard + r"\b", expression) and
               not re.search(r"!\s*defined\s*\(\s*" + guard, expression) for guard in GUARDS)


def unguarded_calls(text, names):
    """(line, name) of each call outside a branch the game list's builds alone compile."""
    stack = []
    found = []
    call = re.compile(r"\b(" + "|".join(sorted(names)) + r")\s*\(")
    for number, line in enumerate(text.splitlines(), 1):
        directive = DIRECTIVE.match(line)
        if directive:
            kind, rest = directive.group(1), directive.group(2).split("/*")[0].strip()
            if kind in ("if", "ifdef", "ifndef"):
                stack.append((kind, rest))
            elif kind in ("elif", "else") and stack:
                was = stack.pop()
                stack.append(("else-of-guard", "") if guarded(was) else (kind, rest))
            elif kind == "endif" and stack:
                stack.pop()
            continue
        stripped = line.strip()
        if stripped.startswith(("/*", "*", "//")) or not stripped:
            continue
        for match in call.finditer(line):
            prototype = re.match(r"^\s*(?:extern\s+)?(?:int|float|void|unsigned int)\s+\w+\s*\(.*\);", line)
            if prototype:
                continue
            if not any(guarded(condition) for condition in stack):
                found.append((number, match.group(1)))
    return found


def test_overlay_functions_found():
    assert "ui_overlay_keyboard" in overlay_functions()


def test_calls_are_guarded():
    names = overlay_functions()
    problems = []
    for path in CALLERS:
        text = path.read_text(encoding="latin-1")
        for number, name in unguarded_calls(text, names):
            problems.append(f"{path.relative_to(ROOT)}:{number}: {name}")
    assert not problems, "overlay calls a --no-game-browser build cannot link:\n" + "\n".join(problems)


def test_negative_control():
    # (Forge's delete prompt as it was: the call in every build)
    text = ("static char const *forge_delete_word(void)\n{\n"
            "\tif (mode == _forge_delete_hold_all || (mode == _forge_delete_hold_controller && !ui_overlay_keyboard()))\n"
            "\t\treturn \"HOLD DELETE\";\n}\n")
    assert unguarded_calls(text, overlay_functions()) == [(3, "ui_overlay_keyboard")]
    assert unguarded_calls("#ifdef HALO_GAME_BROWSER\n" + text + "#endif\n", overlay_functions()) == []
    assert unguarded_calls("#ifdef HALO_GAME_BROWSER\n#else\n" + text + "#endif\n", overlay_functions()) == [(5, "ui_overlay_keyboard")]
