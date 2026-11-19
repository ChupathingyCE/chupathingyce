"""The drop-down console's log (source/interface/terminal.c) and history
(source/main/console.c): the scrollback keeps 2048 lines; a note with an
error's words is red and a warning's yellow, a count of none ("0 failed")
no failure; Ctrl+F filters what is shown, Ctrl+Q hides the game's own notes,
Ctrl+Shift+C copies what is shown; and the history is written to the save
root's private/ by its host path (the game's fopen took it for an Xbox path
under the data root, so it was never kept)."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TERMINAL = (ROOT / "source" / "interface" / "terminal.c").read_text()
CONSOLE = (ROOT / "source" / "main" / "console.c").read_text()
FILES = (ROOT / "port" / "linux" / "src" / "xbox_files.c").read_text()


def body(text, start):
    part = text[text.index(start):]
    return part[:part.index("\n}\n")]


def test_scrollback_keeps_more():
    assert re.search(r"TERMINAL_SCROLLBACK_LINES = 2048,", TERMINAL)


def test_colors_by_words():
    assert '"error", "failed", "couldn\'t"' in TERMINAL
    assert '"warning", "warn:"' in TERMINAL
    add = body(TERMINAL, "static void terminal_scrollback_add(\n")
    assert "terminal_line_has(start, terminal_error_words)" in add
    assert add.index("terminal_error_words") < add.index("terminal_warning_words")
    has = body(TERMINAL, "static boolean terminal_line_has(\n")
    assert "found[-2] == '0'" in has


def test_filter_and_quiet():
    keys = body(TERMINAL, "static boolean terminal_shell_key(\n")
    for key in ("control && key->key_code == _key_f", "control && key->key_code == _key_q",
                "TEST_FLAG(key->modifier_flags, 0) && key->key_code == _key_c"):
        assert key in keys
    shown = body(TERMINAL, "static boolean terminal_scrollback_shown(\n")
    assert "terminal_filter.quiet && terminal_scrollback[slot].noisy" in shown
    draw = body(TERMINAL, "static boolean terminal_dropdown_draw(\n")
    assert "if (!terminal_scrollback_shown(slot))" in draw
    assert "Ctrl+F filter" in draw and "Ctrl+Q quiet" in draw


def test_history_by_host_path():
    for name in ("static void console_history_save(", "static void console_history_load("):
        function = body(CONSOLE, name + "\n")
        assert "platform_host_fopen(path" in function
        assert "fopen(path" not in function.replace("platform_host_fopen(path", "")
    assert "FILE *platform_host_fopen(const char *path, const char *mode)\n{\n\treturn fopen(path, mode);" in FILES


def test_filter_lets_the_console_close_and_copy():
    keys = body(TERMINAL, "static boolean terminal_shell_key(\n")
    assert keys.index("terminal_scrollback_copy();") < keys.index("if (terminal_filter.editing)")
    editing = keys[keys.index("if (terminal_filter.editing)"):]
    assert "key->key_code == _key_backquote" in editing[:400]
    draw = body(TERMINAL, "static boolean terminal_dropdown_draw(\n")
    assert "if (!open)\n\t\tterminal_filter.editing = FALSE;" in draw


def test_passwords_stay_out_of_the_history_file():
    secret = body(CONSOLE, "static boolean console_history_secret(\n")
    for command in ('"rcon"', '"sv_password"', '"sv_rcon_password"'):
        assert command in secret
    # (any case, and a tab after the name or before it)
    named = body(CONSOLE, "static boolean console_history_command_is(\n")
    assert "_strnicmp(command, name, length)" in named
    assert "command[length] == '\\t'" in named
    assert "*command == '\\t'" in secret
    assert "if (console_history_secret(command))\n\t\t\tcontinue;" in body(CONSOLE, "static void console_history_save(\n")
