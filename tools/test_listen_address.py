"""The address the game's listeners bind (port/linux/src/listen_address.h):
every interface for a game others may join, this machine alone for a split
screen game, network.address whenever it is set. The C test
(port/linux/tests/listen_address_test.c) is built with the build machine's
C compiler; the header has no other dependency. Also checks that the
desktop socket layer (port/linux/src/xnet.c) makes the choice through the
header, and that the engine's hosting flag is set before the server's
sockets are made and cleared when the server goes."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
XNET_C = ROOT / "port/linux/src/xnet.c"
GLOBALS_C = ROOT / "source/networking/network_game_globals.c"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_listen_address(tmp_path):
    """Builds the C test, warnings as errors, and runs it."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "listen_address_test"
    build = subprocess.run(
        [cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port/linux/tests/listen_address_test.c"), "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_bind_makes_the_choice():
    """halo_ws_bind asks listen_address_choose with the game's hosting flag,
    and binds loopback or network.address as it says."""
    text = XNET_C.read_text()
    bind = text[text.index("int WSAAPI halo_ws_bind("):]
    bind = bind[:bind.index("\n}\n")]
    assert "listen_address_choose(" in bind
    assert "network_game_port_hosting_local()" in bind
    assert "case _listen_address_loopback:" in bind and "loopback_address()" in bind
    assert "case _listen_address_local_address:" in bind
    assert '#include "listen_address.h"' in text


def test_hosting_flag_brackets_the_server():
    """The flag is set from the accept-remote-connections state before
    network_game_server_create makes the sockets, and cleared when the
    server is disposed of or was not made."""
    text = GLOBALS_C.read_text()
    create = text[text.index("boolean create_global_network_game_server("):]
    create = create[:create.index("\n}\n")]
    set_at = create.index("network_game_hosting_local = !bss_004566dc.accept_remote_connections;")
    made_at = create.index("network_game_server_create()")
    assert set_at < made_at
    assert re.search(r"else\s*\{\s*network_game_hosting_local = FALSE;", create)
    dispose = text[text.index("void dispose_global_network_game_server("):]
    dispose = dispose[:dispose.index("\n}\n")]
    assert "network_game_hosting_local = FALSE;" in dispose
