"""Tests the dedicated server's GameSpy-style status queries and master
heartbeats (server/platform/gamespy_listing.c) through
server/tests/gamespy_harness.c, on the loopback address only: the two query
forms Halo Custom Edition 1.0.10's own server answers ("\\basic\\" and QR2's
FE FD 00), QR2's echo, and the heartbeats and challenge exchange with a
stand-in master (tools/gamespy_fake_master.py). No test reaches a real
master.
"""
import shutil
import socket
import subprocess
import sys
import threading
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools.gamespy_fake_master import (  # noqa: E402
    CAPTURED_CHALLENGE, CAPTURED_RESPONSE, FakeMaster, challenge_response, parse_pairs, parse_qr2_sections)

HARNESS_SOURCES = [ROOT / "server" / "tests" / "gamespy_harness.c", ROOT / "server" / "platform" / "gamespy_listing.c"]
def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


@pytest.fixture(scope="module")
def harness(tmp_path_factory):
    if sys.platform == "win32":
        pytest.skip("the listing is the Linux server's (POSIX sockets)")
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    out = tmp_path_factory.mktemp("gamespy") / "gamespy_harness"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-O1", "-g", "-o", str(out),
                    *map(str, HARNESS_SOURCES), "-lpthread"], check=True)
    return out


def free_udp_port():
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()
    return port


class Server:
    def __init__(self, harness, masters="", gamever="0.0.0-test", players=3):
        self.port = free_udp_port()
        self.process = subprocess.Popen([str(harness), str(self.port), masters, gamever, str(players)],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        line = self.process.stdout.readline().strip()
        assert line == "started", line
        self.output = []
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()

    def read(self):
        for line in self.process.stdout:
            self.output.append(line.strip())

    def query(self, packet: bytes, timeout=2.0) -> bytes:
        client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        client.settimeout(timeout)
        try:
            client.sendto(packet, ("127.0.0.1", self.port))
            return client.recvfrom(4096)[0]
        finally:
            client.close()

    def send(self, text):
        self.process.stdin.write(text + "\n")
        self.process.stdin.flush()

    def stop(self):
        if self.process.poll() is None:
            self.process.stdin.close()
            self.process.wait(5)
        self.reader.join(2)


@pytest.fixture
def server(harness):
    started = Server(harness)
    yield started
    started.stop()


def test_map_names(harness):
    cases = {
        "levels\\test\\bloodgulch\\bloodgulch": "xbox_bloodgulch",
        "levels\\test\\putput\\putput": "xbox_putput",
        "timberland@ce": "timberland",
        "icefields@pc": "icefields",
        "phoenix3_15@md": "phoenix3_15",
    }
    for map_name, expected in cases.items():
        out = subprocess.run([str(harness), "map", map_name], capture_output=True, text=True, check=True)
        assert out.stdout.strip() == expected, map_name


@pytest.mark.parametrize("query", [b"\\basic\\", b"\\status\\", b"\\info\\", b"\\players\\", b"\\rules\\"])
def test_classic_query(server, query):
    reply = server.query(query)
    assert reply.startswith(b"\\")
    fields = reply.decode().split("\\")[1:]
    keys = parse_pairs(fields)
    assert keys["hostname"] == "[C] Test Server"  # (a backslash would end the value)
    assert keys["gamever"] == "0.0.0-test"
    assert keys["hostport"] == ""
    assert keys["maxplayers"] == "16"
    assert keys["password"] == "0"
    assert keys["mapname"] == "xbox_bloodgulch"
    assert keys["dedicated"] == "1"
    assert keys["gamemode"] == "openplaying"
    assert keys["numplayers"] == "3"
    assert keys["gametype"] == "Slayer"
    assert keys["teamplay"] == "1"
    assert keys["gamevariant"] == "Team Slayer"
    assert keys["fraglimit"] == "50"
    assert keys["bridge"] == "chupathingy"
    assert keys["bridge_version"] == "0.0.0-test"
    assert keys["player_0"] == "Player1" and keys["ping_0"] == "" and keys["team_0"] == "0"
    assert keys["player_2"] == "Player3" and keys["score_2"] == "6" and keys["team_2"] == "0"
    assert "player_3" not in keys
    assert fields[-4:-2] == ["final", ""]
    assert keys["queryid"].endswith(".1")


def test_qr2_query(server):
    reply = server.query(b"\xfe\xfd\x00ABCD\xff\xff\xff")
    assert reply[:5] == b"\x00ABCD"
    keys, columns, rows, teams = parse_qr2_sections(reply[5:])
    assert keys["hostname"] == "[C] Test Server"
    assert keys["gamever"] == "0.0.0-test"
    assert keys["mapname"] == "xbox_bloodgulch"
    assert keys["numplayers"] == "3"
    assert keys["bridge"] == "chupathingy"
    assert list(keys)[:3] == ["hostname", "gamever", "hostport"]
    assert columns == ["player_", "score_", "ping_", "team_"]
    assert [row["player_"] for row in rows] == ["Player1", "Player2", "Player3"]
    assert rows[1] == {"player_": "Player2", "score_": "3", "ping_": "21", "team_": "1"}
    assert teams == b"\x00\x00team_t\x00score_t\x00\x00"
    # (the form with one more byte, as a client sent it too)
    assert server.query(b"\xfe\xfd\x00ABCD\xff\xff\xff\x01") == reply


def test_qr2_listed_keys_and_echo(server):
    # (keys asked for by number: the header alone, as the stock server answers)
    assert server.query(b"\xfe\xfd\x00ABCD\x04hostname\x00\x00\x00") == b"\x00ABCD"
    # (the echo a LAN browser broadcasts)
    assert server.query(b"\xfe\xfd\x02\x00\x00\x00\x00\x00") == b"\x05\x00\x00\x00\x00\x00"


def test_junk_gets_no_reply(server):
    for junk in (b"", b"\xfe", b"\xfe\xfd\x09ABCD", b"hello", b"\xfe\xfd\x00AB", b"\x00" * 64):
        with pytest.raises(socket.timeout):
            server.query(junk, timeout=0.3)
    # (still answering)
    assert server.query(b"\\basic\\").startswith(b"\\hostname\\")


def test_many_players(harness):
    started = Server(harness, players=36)
    try:
        reply = started.query(b"\xfe\xfd\x00WXYZ\xff\xff\xff")
        assert len(reply) <= 1400
        keys, _, rows, _ = parse_qr2_sections(reply[5:])
        assert keys["numplayers"] == "36"
        assert len(rows) == 32
    finally:
        started.stop()


def test_master_exchange(harness):
    """The stand-in master's exchange: the first heartbeat (statechanged 3,
    gamename halom, every key), the challenge answered as Custom Edition's
    own server answers it, a changed state's heartbeat, and the last one."""
    master = FakeMaster().start()
    started = Server(harness, masters=f"127.0.0.1:{master.port}")
    try:
        assert master.wait_for(lambda m: m.heartbeats and m.responses)
        instance, keys, rows = master.heartbeats[0]
        assert keys["statechanged"] == "3"
        assert keys["gamename"] == "halom"
        assert keys["localport"] == str(started.port)
        assert keys["natneg"] == "0"
        assert keys["hostname"] == "[C] Test Server"
        assert keys["mapname"] == "xbox_bloodgulch"
        assert keys["bridge"] == "chupathingy"
        assert len(rows) == 3
        # (the answer matches what Custom Edition 1.0.10's server gave)
        assert master.responses[0] == (instance, CAPTURED_RESPONSE, True)
        started.send("change")
        assert master.wait_for(lambda m: any(k.get("statechanged") == "1" for _, k, _ in m.heartbeats))
        changed = [k for _, k, _ in master.heartbeats if k.get("statechanged") == "1"][-1]
        assert changed["mapname"] == "timberland"
        started.stop()
        assert master.wait_for(lambda m: any(k.get("statechanged") == "2" for _, k, _ in m.heartbeats))
        last = master.heartbeats[-1][1]
        assert last["statechanged"] == "2" and last["gamename"] == "halom" and "hostname" not in last
        assert any("a master listed the server" in line for line in started.output)
    finally:
        started.stop()
        master.close()


def test_masters_checked(harness):
    port = str(free_udp_port())
    for masters in ("host:0", "host:99999", ":27900", "a,b,c,d,e"):
        out = subprocess.run([str(harness), port, masters, "x", "0"], capture_output=True, text=True, input="")
        assert out.returncode == 1 and out.stdout.startswith("not started"), masters


def test_encipher_matches_capture():
    assert challenge_response(CAPTURED_CHALLENGE) == CAPTURED_RESPONSE
