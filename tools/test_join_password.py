"""Tests of a host's check of a game with a password on every kind of join:
the C tests (port/linux/tests/join_password_test.c, of port/linux/game/
network_rules.c's rule), built with the build machine's C compiler, and what
the sources must hold that the C tests cannot reach: the check in the one
place every join is taken (network_game_server_accept_client_machine_into_game,
which only the join request's handler calls), before the machine is added;
its refusal the protocol's own bad password code, which every client already
knows; the host's own machine left out; the password the one the menus set
for an internet game; and a dedicated server, which has no password, never
refusing for one. And the invites of a password game (port/linux/src/
p2p_crypto.c, as p2p.c and p2p_lobby.c use them, in
port/linux/tests/p2p_join_password_test.c): the host's link never holds the
token it listens with, only the password opens it, and older versions read
the link as no invite.

A game with a password run for real, host and client, is
tools/test_join_password_pair.py (it needs a built game and maps)."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SERVER_MANAGER_C = ROOT / "source/networking/network_server_manager.c"
SERVER_HANDLER_C = ROOT / "source/networking/network_server_message_handler.c"
CLIENT_MANAGER_C = ROOT / "source/networking/network_client_manager.c"
PROTOCOL_H = ROOT / "source/networking/network_game_protocol.h"
P2P_C = ROOT / "port/linux/src/p2p.c"
P2P_LOBBY_C = ROOT / "port/linux/src/p2p_lobby.c"
MENU_FUNCTIONS_C = ROOT / "port/linux/game/menu_functions.c"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def _function(source, signature):
    # (the definition: after any prototype)
    start = source.rindex(signature)
    return source[start:source.index("\n}\n", start)]


def test_rules(tmp_path):
    """the rule's C tests, warnings as errors"""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "join_password_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port/linux/tests/join_password_test.c"), str(ROOT / "port/linux/game/network_rules.c"),
         "-o", str(binary)],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "join_password_test: ok" in result.stdout


def test_host_checks_before_adding_the_machine():
    accept = _function(SERVER_MANAGER_C.read_text(encoding="utf-8"),
                       "boolean network_game_server_accept_client_machine_into_game(")
    check = accept.index("network_rules_join_password_refused((uint32_t)address.address.long_words[0],")
    assert check < accept.index("network_game_add_machine(&server->game, &new_machine)")
    piece = accept[check - 400:check + 500]
    # (the host's own machine never refused)
    assert "!network_game_server_client_machine_is_local(server, machine)" in piece
    assert "address.address.long_words[0] != IPV4_LOOPBACK_ADDRESS" in piece
    assert "p2p_hosting_has_password()" in piece
    assert "network_game_server_refusal_code = _rejection_code_bad_password;" in piece
    # (no address written whole)
    assert "transport_address_to_string(&address)" in piece
    # (in a build with internet hosting, where the password is)
    before = accept[:check]
    assert before.rindex("#ifdef HALO_GAME_BROWSER") > before.rfind("#endif")


def test_every_join_goes_through_the_check():
    """one place takes a machine into the game, and only the join request's
    handler calls it: system link, internet play, Delta, MCC and Custom
    Edition cross-play joins all send that request"""
    pattern = re.compile(r"\bnetwork_game_server_accept_client_machine_into_game\(")
    callers = []
    for folder in ("source", "port", "server"):
        for path in (ROOT / folder).rglob("*.c"):
            if "third_party" in path.parts or "tests" in path.parts:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for match in pattern.finditer(text):
                line = text[text.rfind("\n", 0, match.start()) + 1:text.find("\n", match.start())]
                if line.startswith("boolean network_game_server_accept_client_machine_into_game("):
                    continue
                # (its name in a log line's text)
                if line[:match.start() - text.rfind("\n", 0, match.start()) - 1].count('"') % 2:
                    continue
                callers.append((path.relative_to(ROOT).as_posix(), line.strip()))
    assert callers == [("source/networking/network_server_message_handler.c",
                        "if (network_game_server_accept_client_machine_into_game(server, server_client_machine))")]
    handler = _function(SERVER_HANDLER_C.read_text(encoding="utf-8"),
                        "static boolean network_game_server_handle_message_client_join_game_request(")
    # (a refusal says the check's reason)
    assert "rejection.reason = network_game_server_last_refusal_code();" in handler


def test_refusal_is_the_protocols_bad_password():
    protocol = PROTOCOL_H.read_text(encoding="utf-8")
    codes = protocol[protocol.index("_rejection_code_version_too_old = 0,"):]
    # (code 3, as retail's)
    assert codes.index("_rejection_code_bad_password,") > codes.index("_rejection_code_bad_join_token,")
    client = CLIENT_MANAGER_C.read_text(encoding="utf-8")
    assert "rejection_code == _rejection_code_bad_password ? JOIN_REASON_PASSWORD :" in client


def test_password_is_the_hosted_games():
    p2p = P2P_C.read_text(encoding="utf-8")
    has = _function(p2p, "int p2p_hosting_has_password(void)")
    assert "result = p2p.has_join_key;" in has
    assert "pthread_mutex_lock(&p2p_lock);" in has
    assert "p2p.has_join_key = join_key != NULL;" in _function(p2p, "void p2p_new_invite_for_password(")
    lobby = _function(P2P_LOBBY_C.read_text(encoding="utf-8"), "void p2p_set_hosting_password(const char *password)")
    assert "p2p_new_invite_for_password(has_password ? join_key : NULL);" in lobby
    # (the menus: a public internet game's password, else none)
    menus = MENU_FUNCTIONS_C.read_text(encoding="utf-8")
    assert "p2p_set_hosting_password(multiplayer.mode == _multiplayer_mode_host_internet && " \
           "!server_settings_private() ?" in menus
    assert "p2p_set_hosting_password(host_listing_shown() && !multiplayer.game_private ? " \
           "multiplayer.game_password : NULL);" in menus


def test_dedicated_server_sets_no_password():
    """a dedicated server has no join password, so it takes every join as
    before"""
    for path in (ROOT / "server").rglob("*.c"):
        assert "p2p_set_hosting_password" not in path.read_text(encoding="utf-8", errors="replace"), path


def test_p2p_join_password(tmp_path):
    """the invites' C tests, warnings as errors. The unit needs no SDL or
    game: the platform headers are left out, and the two functions it calls
    from them are declared ahead."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    prelude = tmp_path / "prelude.h"
    prelude.write_text("void posix_random_bytes(void *buffer, unsigned long size);\n"
                       "void platform_log(const char *format, ...);\n")
    binary = tmp_path / "p2p_join_password_test"
    monocypher = ROOT / "port" / "third_party" / "monocypher"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         "-D__HALO_LINUX_PLATFORM_H", "-D__HALO_LINUX_POSIX_H", "-include", str(prelude),
         "-I", str(ROOT / "port" / "linux" / "src"), "-I", str(monocypher),
         str(ROOT / "port" / "linux" / "tests" / "p2p_join_password_test.c"),
         str(ROOT / "port" / "linux" / "src" / "p2p_crypto.c"),
         str(ROOT / "port" / "linux" / "game" / "content_sha256.c"),
         str(monocypher / "monocypher.c"), str(monocypher / "monocypher-ed25519.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout
