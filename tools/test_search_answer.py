"""Tests of a host's answer to a game search that came from a port other than
the game's client port: the C tests (port/linux/tests/search_answer_test.c,
of port/linux/game/network_rules.c's rule), built with the build machine's C
compiler, and what the sources must hold that the C tests cannot reach: the
rule asked in the search's handler before anything is answered, with
network.direct_ip_host (off by default); the broadcast answer left as it
was; the game's own searches still broadcasts, so System Link finds games as
before; and Online Games' pings, which are not game searches, untouched."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SERVER_HANDLER_C = ROOT / "source/networking/network_server_message_handler.c"
CLIENT_MANAGER_C = ROOT / "source/networking/network_client_manager.c"
PORT_CONFIG_C = ROOT / "port/linux/src/port_config.c"
P2P_PROBE_C = ROOT / "port/linux/src/p2p_probe.c"
FUZZ_C = ROOT / "tools/tests/host_fuzz/server_messages/server_messages_fuzz.c"


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
    binary = tmp_path / "search_answer_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port/linux/tests/search_answer_test.c"), str(ROOT / "port/linux/game/network_rules.c"),
         "-o", str(binary)],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "search_answer_test: ok" in result.stdout


def _search_handler():
    return _function(SERVER_HANDLER_C.read_text(encoding="utf-8"),
                     "static boolean network_game_server_handle_message_client_broadcast_game_search(")


def test_handler_asks_the_rule_before_answering():
    handler = _search_handler()
    direct = handler.index("boolean direct = source_address->port && source_address->port != NETWORK_GAME_CLIENT_PORT;")
    rule = handler.index("network_rules_search_answered((uint32_t)source_address->address.long_words[0],")
    assert direct < rule
    piece = handler[direct:rule + 300]
    assert 'config_boolean("network.direct_ip_host")' in piece
    assert "static struct network_rules_search_budget search_budget;" in piece
    # (an address that is not IPv4 never answered at its address)
    assert "source_address->address_length != IPV4_ADDRESS_LENGTH" in piece
    # (unanswered: nothing built, nothing sent)
    assert re.search(r"&search_budget\)\)\)\s*\{\s*return TRUE;\s*\}", piece)
    assert rule < handler.index("struct message_server_game_advertise advertisement")
    assert rule < handler.index("create_network_game_message(")


def test_broadcast_answer_unchanged():
    """a search from the client port is answered by the broadcast, no more
    than once each interval, as before"""
    handler = _search_handler()
    assert "if (game && !direct && last_advertised_time && now - last_advertised_time < " \
           "GAME_ADVERTISEMENT_INTERVAL)" in handler
    assert "address.address.long_words[0] = NONE;" in handler
    assert "address.port = NETWORK_GAME_CLIENT_PORT;" in handler


def test_setting_off_by_default():
    config = PORT_CONFIG_C.read_text(encoding="utf-8")
    row = config[config.index('{ "network.direct_ip_host",'):]
    row = row[:row.index("},") + 2]
    assert '_config_boolean, "false", "HALO_NET_DIRECT_IP_HOST"' in row
    assert "_platform_server" in row
    for doc in ("docs/settings.md", "server/docs/settings.md"):
        text = (ROOT / doc).read_text(encoding="utf-8")
        assert "network.direct_ip_host" in text, doc


def test_game_searches_still_broadcast():
    """the game's own searches go to the broadcast address, which reaches a
    host from its local network or internet play's tunnel: System Link finds
    games as before"""
    client = CLIENT_MANAGER_C.read_text(encoding="utf-8")
    send = client.index("_message_client_broadcast_game_search,\n")
    before = client[send - 1500:send]
    assert "broadcast_address.address.long_words[0] = BROADCAST_IPV4_ADDRESS;" in before
    assert "broadcast_game_search.port = NETWORK_GAME_CLIENT_PORT;" in before


def _dangling_else(text):
    """the #if blocks that start a new if and are followed, after their
    #endif, by an else: that else, written for the if before the block,
    binds to the block's if instead whenever the block is compiled in"""
    found = []
    for block in re.finditer(r"^[ \t]*#if(?:def|ndef)?\b[^\n]*\n(.*?)^[ \t]*#endif[^\n]*\n", text, re.S | re.M):
        body = re.sub(r"/\*.*?\*/", " ", block.group(1), flags=re.S).strip()
        if "#if" in body or "#else" in body or "#elif" in body:
            continue
        after = re.sub(r"/\*.*?\*/", " ", text[block.end():block.end() + 400], flags=re.S).lstrip()
        if re.match(r"if\s*\(", body) and re.match(r"else\b", after):
            found.append(text.count("\n", 0, block.start()) + 1)
    return found


def test_search_send_keeps_its_else():
    """the broadcast search's failure is logged when its write fails, and
    only then: the Direct IP search to an address comes after the whole
    if/else, never between them"""
    client = CLIENT_MANAGER_C.read_text(encoding="utf-8")
    assert re.search(
        r'network_event\("sent out a broadcast game search packet"\);\s*'
        r"client->last_broadcast_search_time = now;\s*\}\s*else\s*\{\s*"
        r'network_event\("network_game_client_write\(\) failed while sending a message_client_broadcast_game_search',
        client)
    assert _dangling_else(client) == []


def test_dangling_else_check_finds_one():
    """the check above finds the mistake it is for"""
    bad = ("\tif (success == TRUE)\n\t{\n\t\tsent();\n\t}\n#ifdef HALO_GAME_BROWSER\n"
           "\t/* to the address */\n\tif (success == TRUE && target)\n\t{\n\t\tsend();\n\t}\n#endif\n"
           "\telse\n\t{\n\t\tfailed();\n\t}\n")
    assert _dangling_else(bad) == [5]
    good = ("\tif (a)\n\t{\n\t}\n#ifdef X\n\telse if (b)\n\t{\n\t}\n#endif\n\telse\n\t{\n\t}\n")
    assert _dangling_else(good) == []


def test_networking_has_no_dangling_else():
    for path in sorted((ROOT / "source/networking").glob("*.c")):
        assert _dangling_else(path.read_text(encoding="utf-8", errors="replace")) == [], path


def test_online_games_pings_are_not_game_searches():
    """Online Games' pings and the game list's are the probe's own messages
    (p2p_probe.c), which a dedicated server answers whatever this setting"""
    probe = P2P_PROBE_C.read_text(encoding="utf-8")
    assert "game_search" not in probe
    for folder in ("port/linux/src", "server"):
        for path in (ROOT / folder).rglob("*.c"):
            text = path.read_text(encoding="utf-8", errors="replace")
            assert "_message_client_broadcast_game_search" not in text, path


def test_fuzz_target_reaches_it():
    fuzz = FUZZ_C.read_text(encoding="utf-8")
    assert "control & 32 ? 0xCB007101 : 0x0A000001" in fuzz
    assert "fuzz_direct_ip_host = (control & 64) != 0;" in fuzz
    assert 'return fuzz_direct_ip_host && !strcmp(name, "network.direct_ip_host");' in fuzz
