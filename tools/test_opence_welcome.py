"""Tests of a host's one line to OpenCE players that this is a Chupathingy
game: the C tests of its rules (port/linux/game/opence_welcome.c,
port/linux/tests/opence_welcome_test.c: OpenCE machines only, once a join,
after they spawn, a line that fits OpenCE's chat), built with the build
machine's C compiler, and what the sources must keep that the C tests
cannot reach: off unless set, on the server and a player's host alike, a
Delta machine told apart by its session, the game's notice message to that
machine alone, and nobody's address logged."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / "port" / "linux" / "game"
RULES_C = GAME / "opence_welcome.c"
CHAT_C = GAME / "chat.c"
DISTRIBUTED_C = GAME / "network_distributed.c"
MENUS_C = GAME / "menu_functions.c"
PORT_CONFIG_C = ROOT / "port" / "linux" / "src" / "port_config.c"
CONFIG_C = ROOT / "server" / "src" / "server_config.c"
CONFIG_H = ROOT / "server" / "src" / "server_config.h"
DEDICATED_C = ROOT / "server" / "src" / "dedicated.c"
SETTINGS_MD = ROOT / "server" / "docs" / "settings.md"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def function(source, signature):
    """a function's text, from its signature to its closing brace"""
    start = source.index("\n" + signature)
    end = re.search(r"\n\}\n", source[start:])
    return source[start:start + end.end()]


def test_rules(tmp_path):
    """the rules' C tests, as C89 with warnings as errors (the consoles'
    compilers build the game's units)"""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "opence_welcome_test"
    build = subprocess.run(
        [cc, "-std=c89", "-pedantic", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port/linux/tests/opence_welcome_test.c"), str(RULES_C), "-o", str(binary)],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "opence_welcome_test: ok" in result.stdout


def test_default_line_fits_opence_chat():
    """the built-in line is one line of OpenCE's chat: 63 characters of
    printable ASCII without '|', and the server's limit is the same"""
    text = re.search(r'opence_welcome_default_text\[\] = "([^"]*)";', RULES_C.read_text(encoding="utf-8")).group(1)
    assert 0 < len(text) <= 63 and text.isascii() and text.isprintable() and "|" not in text
    assert "chupathingy.org" in text
    assert "SERVER_SETTINGS_OPENCE_WELCOME_SIZE = 64," in CONFIG_H.read_text(encoding="utf-8")
    assert f"`{text}`" in SETTINGS_MD.read_text(encoding="utf-8")


def test_off_unless_set():
    """off by default on the dedicated server and on a player's host, each
    setting with its environment variable, and documented"""
    config = CONFIG_C.read_text(encoding="utf-8")
    assert '{ "opence_welcome", SERVER_FIELD_BOOLEAN, 0, 1, "HALO_DEDICATED_OPENCE_WELCOME",' in config
    assert '"HALO_DEDICATED_OPENCE_WELCOME_TEXT"' in config
    dedicated = DEDICATED_C.read_text(encoding="utf-8")
    assert 'setting_flag("HALO_DEDICATED_OPENCE_WELCOME", &settings, SERVER_SETTING_OPENCE_WELCOME, FALSE)' in dedicated
    assert '{ "network.opence_welcome", _config_boolean, "false", "HALO_NET_OPENCE_WELCOME",' in \
        PORT_CONFIG_C.read_text(encoding="utf-8")
    settings = SETTINGS_MD.read_text(encoding="utf-8")
    assert "| `HALO_DEDICATED_OPENCE_WELCOME` | `false` |" in settings
    chat = CHAT_C.read_text(encoding="utf-8")
    getter = function(chat, "int chat_host_opence_welcome(void)")
    assert 'config_boolean("network.opence_welcome")' in getter and "return 0;" in getter


def test_host_rules_row():
    """GAME SETUP's host rules have the row, read from and written to
    config.toml"""
    menus = MENUS_C.read_text(encoding="utf-8")
    assert 'L"LEGACY WELCOME:"' in menus
    assert 'host_write("network.opence_welcome", host_setup.opence_welcome ? "true" : "false");' in menus
    assert 'host_setup.opence_welcome = config_boolean("network.opence_welcome") != 0;' in menus
    assert "row <= _host_row_opence_welcome;" in menus


def test_delta_machines_never():
    """a machine is OpenCE's only with no Delta session after its window
    (delta_peer_game_machine_session), and only while the host has it on"""
    body = function(CHAT_C.read_text(encoding="utf-8"), "static void host_opence_welcome(void)")
    assert "delta_peer_game_machine_session((int)index)" in body
    assert "!on || session < 0 ? _opence_welcome_unknown : session ? _opence_welcome_delta :" in body
    assert "opence_welcome_due(&chat.opence_welcome," in body
    assert body.count("distributed_send_notice_to_machine(") == 1
    # (the host's own machine is never a candidate)
    assert "machine != local" in body


def test_notice_to_one_machine():
    """the line is the game's notice message to that machine alone, reliably,
    not to every client"""
    body = function(DISTRIBUTED_C.read_text(encoding="utf-8"), "void distributed_send_notice_to_machine(")
    assert "distributed_send_to_machine_reliably(machine_index, &message, _distributed_message_notice, 0," in body
    assert "distributed_send(" not in body
    assert "console_warning" not in body


def test_no_address_logged():
    """the log names the machine's index alone"""
    body = function(CHAT_C.read_text(encoding="utf-8"), "static void host_opence_welcome(void)")
    for call in re.findall(r"platform_log\(.*?\);", body, re.S):
        assert "address" not in call and "ipv4" not in call, call
