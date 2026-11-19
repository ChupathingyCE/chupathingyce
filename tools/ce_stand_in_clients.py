#!/usr/bin/env python3
"""Stand-in Halo Custom Edition 1.0.10 clients for a dedicated server with
ce_joins on (server/src/server_ce.c), on the loopback address only: COUNT
of them join the server's GameSpy/Custom Edition port, load, send updates
and acknowledge everything for SECONDS, built on the wire of
tools/test_ce_joins.py (the transport's handshake, its key agreement, the
cipher and the framing).

Each prints how far it got (playing, refused and why, or closed by the
server: a drop) and the last roster the server told it (its game settings:
how many players, and their teams), so a run shows that every client sees
every player.

    python3 tools/ce_stand_in_clients.py --port 2412 --count 16 --seconds 150

The server must be one of your own, on this machine (a test server with its
own data and save folders, listing nowhere): the clients talk to 127.0.0.1
alone. Exit status 0 when every client played to the end and was told a
roster of COUNT players (plus --others, players not of these clients).
"""
import argparse
import os
import struct
import sys
import threading
import time
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
try:
    import pytest  # noqa: F401
except ImportError:  # (test_ce_joins.py's fixtures need it, these clients don't)
    import types
    sys.modules["pytest"] = types.SimpleNamespace(fixture=lambda *a, **k: (lambda f: f), raises=None)
from tools.test_ce_joins import Client, join_request, seal, open_sealed, unframe, update_payload  # noqa: E402

GAME_SETTINGS = 33
UNIVERSAL_WIDTHS = (1, 8, 8, 8, 9, 32, 1, 8, 8, 8, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 8, 32, 1, 32)
ENGINE_VARIANT_SIZE = 56
GAME_PLAYERS = 16


class Bits:
    def __init__(self, data, position=0):
        self.data, self.position = data, position

    def get(self, count):
        value = 0
        for index in range(count):
            bit = self.position + index
            value |= ((self.data[bit >> 3] >> (bit & 7)) & 1) << index
        self.position += count
        return value

    def text(self, most, width):
        length = self.get(most.bit_length())
        return "".join(chr(self.get(width)) for _ in range(length))


def roster(payload):
    """the players of a game settings delta at the payload's start (its
    first message: ce_messages.c, ce_payload_put_game_settings), as (name,
    machine, team); None if the payload starts with another message"""
    bits = Bits(payload, 11)
    if len(payload) * 8 < 19 or bits.get(1) != 1 or bits.get(1) != 0 or bits.get(6) != GAME_SETTINGS:
        return None
    bits.text(64, 16)
    bits.get(32)
    bits.text(128, 8)
    bits.text(24, 16)
    bits.get(32)
    for width in UNIVERSAL_WIDTHS:
        bits.get(width)
    bits.get(ENGINE_VARIANT_SIZE * 8)
    bits.get(1 + 8 + 8 + 16)
    count = bits.get(16)
    players = []
    for _ in range(GAME_PLAYERS):
        name = bits.text(12, 16)
        bits.get(16 + 16)
        machine, _controller, team, _list = bits.get(8), bits.get(8), bits.get(8), bits.get(8)
        players.append((name, machine, team))
    return players[:count]


def stand_in(port, number, prefix, seconds, results):
    name = f"{prefix}{number:02d}"
    state = {"name": name, "stage": "connecting", "refused": None, "roster": None, "reliable": 0}
    results[number] = state
    client = Client(port)
    client.rng.seed(1000 + number)
    try:
        client.sock.settimeout(3.0)
        client.handshake()
        first = client.reliable()
        map_name = first[0][18:-1].split(b"\0")[0]
        client.send(b"\xfe\xfe\x64" + struct.pack(">H", client.in_sequence))
        client.send_messages([join_request(name), b"\x01" + map_name.ljust(256, b"\0") + b"\x15"])
        state["stage"] = "asked"
        client.sock.settimeout(0.1)
        end = time.time() + seconds
        last_update = 0.0
        loaded = False
        yaw = 0.1 * number
        while time.time() < end:
            try:
                packet = client.receive()
            except OSError:
                packet = None
            if packet and packet[:3] == b"\xfe\xfe\x00":
                sequence, = struct.unpack(">H", packet[3:5])
                if sequence == client.in_sequence:
                    client.in_sequence = (client.in_sequence + 1) & 0xFFFF
                    state["reliable"] += 1
                    try:
                        payload = open_sealed(client.key, packet[7:])
                    except AssertionError:
                        payload = b""
                    players = roster(payload) if payload else None
                    if players is not None:
                        state["roster"] = players
                    for message in unframe(payload) if payload else []:
                        if message[-1] == 6:
                            state["refused"] = message[1] if len(message) > 1 else -1
                            state["stage"] = "refused"
                        elif message[-1] == 10 and state["stage"] == "asked":
                            state["stage"] = "accepted"
                client.send(b"\xfe\xfe\x64" + struct.pack(">H", client.in_sequence))
            elif packet and packet[:3] == b"\xfe\xfe\x68":
                if state["stage"] != "refused":
                    state["stage"] = "closed by the server after " + state["stage"]
                break
            if state["stage"] == "accepted" and not loaded:
                client.send_messages([b"\x01\x1a"])
                loaded = True
                state["stage"] = "playing"
            if loaded and time.time() - last_update > 0.1:
                yaw = (yaw + 0.05) % 6.28
                client.send(seal(client.key, bytes.fromhex(update_payload(yaw, 0.0))))
                last_update = time.time()
    except Exception as exception:  # noqa: BLE001
        state["stage"] += f" ({exception!r})"
    finally:
        try:
            client.send(b"\xfe\xfe\x68")
        except OSError:
            pass
        client.sock.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--port", type=int, default=2412)
    parser.add_argument("--count", type=int, default=16)
    parser.add_argument("--seconds", type=float, default=150)
    parser.add_argument("--prefix", default="CE")
    parser.add_argument("--others", type=int, default=0, help="players in the game not of these clients")
    parser.add_argument("--gap", type=float, default=0.5, help="seconds between the clients' joins")
    arguments = parser.parse_args()
    results = {}
    threads = []
    for number in range(1, arguments.count + 1):
        thread = threading.Thread(target=stand_in, daemon=True,
                                  args=(arguments.port, number, arguments.prefix, arguments.seconds, results))
        thread.start()
        threads.append(thread)
        time.sleep(arguments.gap)
    for thread in threads:
        thread.join(arguments.seconds + 30)
    expected = arguments.count + arguments.others
    good = 0
    for number in sorted(results):
        state = results[number]
        players = state["roster"] or []
        teams = Counter(team for _, _, team in players)
        print(f"{state['name']}: {state['stage']}, refused={state['refused']}, {state['reliable']} reliable, "
              f"roster {len(players)} players, teams {dict(sorted(teams.items()))}")
        good += state["stage"] == "playing" and len(players) == expected
    print(f"playing with all {expected} players seen: {good} of {arguments.count}")
    return 0 if good == arguments.count else 1


if __name__ == "__main__":
    sys.exit(main())
