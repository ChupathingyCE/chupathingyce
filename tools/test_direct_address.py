"""Joining a game by its address (Direct IP, the console's connect:
port/linux/game/direct_join.c). The C test (port/linux/tests/
direct_address_test.c) builds port/linux/src/direct_address.h with the
build machine's C compiler and checks the addresses a player types (IPv4,
IPv6 bare and in brackets, host names, ports) and the rest, and that an
address is never shown again as typed. Also checks that the join, the
menus and the console go through the one parser and never show or log an
address whole."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
JOIN_C = ROOT / "port/linux/game/direct_join.c"
MENU_C = ROOT / "port/linux/game/menu_functions.c"
CONSOLE_C = ROOT / "source/main/console.c"
UNWIRED = ROOT / "port/assets/menus/UNWIRED.md"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_direct_address(tmp_path):
    """Builds the C test, warnings as errors, and runs it."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "direct_address_test"
    build = subprocess.run(
        [cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port/linux/tests/direct_address_test.c"), "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_one_parser_one_join():
    """The menus and the console start the join through direct_join.c,
    which parses with direct_address.h; nothing else parses an address."""
    join = JOIN_C.read_text()
    assert '#include "../src/direct_address.h"' in join
    assert "direct_address_parse(" in join
    assert "network_game_client_join_address(" in join
    assert "direct_join_start(" in MENU_C.read_text()
    assert "direct_join_console(" in CONSOLE_C.read_text()


def test_join_logs_no_address():
    """direct_join.c logs an address only through log_address (a tag for a
    public one) and a host name only through log_address_name; it never
    formats a dotted quad of its own."""
    join = JOIN_C.read_text()
    assert not re.search(r"%u\.%u\.%u\.%u|%d\.%d\.%d\.%d|%lu\.%lu", join)
    for call in re.finditer(r"platform_log\((.*?)\);", join, re.S):
        text = call.group(1)
        assert "typed" not in text and "->text" not in text, text


def test_direct_ip_functions_wired():
    """The Custom Edition menus' DIRECT IP screen's functions are wired:
    gone from UNWIRED.md's table, and named in menu_functions.c."""
    unwired = UNWIRED.read_text()
    table = unwired.split("Wired:")[0]
    menu = MENU_C.read_text()
    for name in ("direct ip connect init", "direct ip connect update", "direct ip edit field"):
        assert f"`{name}`" not in table
        assert f'"{name}"' in menu
