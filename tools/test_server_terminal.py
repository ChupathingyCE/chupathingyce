"""Runs the dedicated server's console in a terminal (server/platform/
server_terminal.c, in server/tests/control_harness.c) and checks its line
editing: typing while log lines arrive keeps the typed text whole on the
prompt line, under every log line; Enter, Backspace, Ctrl+U, Up and the
ignored escape sequences; the line's limit; Ctrl+D giving the terminal back
as it was. And that with a pipe instead of a terminal nothing of it runs:
no prompt, no escape sequence, the output as before."""
import os
import re
import select
import subprocess
import sys
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools.test_server_control import HARNESS_SOURCES, build_sanitized, compiler  # noqa: E402

if os.name != "posix":
    pytest.skip("the terminal's line editing is POSIX's", allow_module_level=True)

import fcntl  # noqa: E402
import pty  # noqa: E402
import struct  # noqa: E402
import termios  # noqa: E402

WIDTH = 200
# the lines the harness prints on its own, between what is typed
LOG_LINE = re.compile(r"halo: log line \d+|harness: started|notice: .*|audit: .*|ran .*")


@pytest.fixture(scope="module")
def harness_binary(tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path_factory.mktemp("terminal") / "control_harness"
    build_sanitized(cc, binary, HARNESS_SOURCES, "gnu11")
    return binary


def environment(data):
    data.mkdir(parents=True, exist_ok=True)
    keep = {key: value for key, value in os.environ.items() if not key.startswith("HALO_")}
    return dict(keep, HARNESS_DATA=str(data), HALO_SAVE_ROOT=str(data), HARNESS_STDERR_LINES="4",
                HALO_DEDICATED_CONTROL="off", ASAN_OPTIONS="detect_leaks=0", TERM="xterm")


class Screen:
    """What a terminal shows of the bytes written to it: carriage return,
    line feed, Backspace, ESC [ K and printable text (no wrapping: the
    terminal is wider than every line here)."""

    def __init__(self):
        self.rows = [""]
        self.row = 0
        self.column = 0
        self.pending = b""

    def feed(self, data):
        text = (self.pending + data).decode("utf-8", "replace")
        self.pending = b""
        index = 0
        while index < len(text):
            character = text[index]
            if character == "\x1b":
                match = re.match(r"\x1b\[[0-9;?]*[@-~]", text[index:])
                if not match:
                    self.pending = text[index:].encode()
                    return
                if match.group(0) == "\x1b[K":
                    self.rows[self.row] = self.rows[self.row][:self.column]
                index += len(match.group(0))
                continue
            if character == "\r":
                self.column = 0
            elif character == "\n":
                self.row += 1
                if self.row == len(self.rows):
                    self.rows.append("")
            elif character == "\b":
                self.column = max(0, self.column - 1)
            elif character >= " ":
                line = self.rows[self.row].ljust(self.column)
                self.rows[self.row] = line[:self.column] + character + line[self.column + 1:]
                self.column += 1
            index += 1


class Terminal:
    """The harness on a pseudo-terminal of its own, as a server started in
    one: its screen, and keys typed into it."""

    def __init__(self, binary, data):
        pid, master = pty.fork()
        if pid == 0:
            try:
                os.execve(str(binary), [str(binary)], environment(data))
            finally:
                os._exit(127)
        self.pid = pid
        self.master = master
        fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 50, WIDTH, 0, 0))
        self.raw = b""
        self.screen = Screen()

    def read(self, seconds):
        deadline = time.time() + seconds
        while True:
            left = deadline - time.time()
            if left <= 0:
                return
            ready, _, _ = select.select([self.master], [], [], left)
            if not ready:
                return
            try:
                data = os.read(self.master, 65536)
            except OSError:
                return
            if not data:
                return
            self.raw += data
            self.screen.feed(data)

    def wait_for(self, predicate, what, timeout=30):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if predicate():
                return
            self.read(0.05)
        raise AssertionError(f"no {what} on the screen:\n" + "\n".join(self.screen.rows[-40:]))

    def type(self, keys, pause=0.0):
        for character in keys if pause else [keys]:
            os.write(self.master, character.encode() if isinstance(character, str) else character)
            self.read(pause)

    @property
    def prompt(self):
        return self.screen.rows[-1]

    def close(self):
        try:
            os.kill(self.pid, 15)
            os.waitpid(self.pid, 0)
        finally:
            os.close(self.master)


@pytest.fixture
def terminal(harness_binary, tmp_path):
    running = Terminal(harness_binary, tmp_path / "data")
    running.wait_for(lambda: running.prompt == "> " and any("harness: started" in row for row in running.screen.rows),
                     "prompt")
    yield running
    running.close()


def check_rows(screen, typed):
    """Every row but the prompt's is a whole line the harness printed, or a
    line entered at the prompt."""
    for row in screen.rows[:-1]:
        assert LOG_LINE.fullmatch(row) or row.startswith("> "), f"a broken row: {row!r}"
        if row.startswith("> "):
            assert row[2:] in typed, f"a line not entered: {row!r}"


def test_typing_between_log_lines(terminal):
    """The typed text stays whole on the bottom line while log lines (one
    every 20 ms) come in above it; Enter leaves it on the screen and runs
    it."""
    rows = len(terminal.screen.rows)
    terminal.type("sv_status please", pause=0.03)
    terminal.read(0.2)
    assert len(terminal.screen.rows) > rows + 5, "no log lines came in while typing"
    assert terminal.prompt == "> sv_status please"
    check_rows(terminal.screen, [])
    terminal.type("\r")
    terminal.wait_for(lambda: "ran sv_status please" in terminal.screen.rows, "the command's output")
    terminal.read(0.3)
    assert "> sv_status please" in terminal.screen.rows
    assert terminal.prompt == "> "
    check_rows(terminal.screen, ["sv_status please"])
    assert b"\x1b[K" in terminal.raw


def test_editing_keys(terminal):
    terminal.type("sv_maps")
    terminal.type("\r")
    terminal.wait_for(lambda: "ran sv_maps" in terminal.screen.rows, "the command's output")
    terminal.type("abc\x7f")
    terminal.wait_for(lambda: terminal.prompt == "> ab", "Backspace")
    # (arrows left and right, a function key, Alt+x, Tab: nothing typed)
    terminal.type("\x1b[C\x1b[D\x1b[15~\x1bx\x1bOP\t")
    terminal.type("é")
    terminal.wait_for(lambda: terminal.prompt == "> abé", "the escape sequences ignored")
    terminal.type("\x7f")
    terminal.wait_for(lambda: terminal.prompt == "> ab", "a UTF-8 character taken out whole")
    terminal.type("\x15")
    terminal.wait_for(lambda: terminal.prompt == "> ", "Ctrl+U")
    terminal.type("typed\x1b[A")
    terminal.wait_for(lambda: terminal.prompt == "> sv_maps", "Up")
    terminal.type("\x1b[B")
    terminal.wait_for(lambda: terminal.prompt == "> typed", "Down")
    terminal.type("\x15")
    terminal.wait_for(lambda: terminal.prompt == "> ", "Ctrl+U")
    # (a line past the limit: the bytes past it are dropped, with a bell)
    terminal.type("echo " + "x" * 400)
    terminal.type("\r")
    terminal.wait_for(lambda: any(row.startswith("ran echo ") for row in terminal.screen.rows), "the long line's output")
    ran = next(row for row in terminal.screen.rows if row.startswith("ran echo "))
    assert len(ran) == len("ran ") + 255
    assert b"\a" in terminal.raw


def test_end_of_input_gives_the_terminal_back(terminal):
    settings = termios.tcgetattr(terminal.master)
    assert not settings[3] & termios.ECHO
    terminal.type("\x04")
    terminal.wait_for(lambda: termios.tcgetattr(terminal.master)[3] & termios.ECHO, "the terminal's echo")
    assert termios.tcgetattr(terminal.master)[3] & termios.ICANON
    terminal.read(0.2)
    start = len(terminal.raw)
    terminal.read(0.3)
    after = terminal.raw[start:]
    assert b"halo: log line" in after
    assert b"\x1b" not in after and b"> " not in after


def test_pipe_unchanged(harness_binary, tmp_path):
    """With standard input and output a pipe, no prompt and no escape
    sequence: the bytes as before."""
    process = subprocess.Popen([str(harness_binary)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE,
                               env=dict(environment(tmp_path / "data"), HALO_DEDICATED_CONSOLE="true"))
    try:
        time.sleep(0.5)
        process.stdin.write(b"sv_status\n")
        process.stdin.flush()
        time.sleep(0.5)
    finally:
        process.terminate()
        output, errors = process.communicate(timeout=10)
    assert b"ran sv_status\n" in output
    assert b"halo: log line 1\n" in errors
    for stream in (output, errors):
        assert b"\x1b" not in stream and b"> " not in stream and b"\r" not in stream
