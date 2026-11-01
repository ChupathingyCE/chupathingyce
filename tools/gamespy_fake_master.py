"""A stand-in GameSpy QR2 heartbeat master, on the loopback address, for
the tests of the dedicated server's GameSpy-style status queries and
heartbeats (tools/test_gamespy_listing.py) and for trying a server against
it on one machine:

    python3 tools/gamespy_fake_master.py [port]

It prints each heartbeat's keys (never the addresses), challenges each new
server and checks the answer. It never talks to a real master.
"""
import base64
import secrets
import socket
import sys
import threading
import time

GAME_KEY = b"e4Rd9J"
# a challenge and the answer Custom Edition 1.0.10's dedicated server gave
# it, from our capture of its heartbeat to a stand-in master
CAPTURED_CHALLENGE = b"ABCDEF000000000000"
CAPTURED_RESPONSE = b"mQpPAYfhsuu+99QGwVSMLw8p"


def encipher(key: bytes, data: bytes) -> bytes:
    """GameSpy's RC4 variant, as the QR2 challenge's answer uses it: the
    usual key schedule, then an index stepped by each byte plus one."""
    state = list(range(256))
    y = 0
    for index in range(256):
        y = (key[index % len(key)] + state[index] + y) & 0xFF
        state[index], state[y] = state[y], state[index]
    out = bytearray(data)
    x = y = 0
    for index, byte in enumerate(out):
        x = (x + byte + 1) & 0xFF
        y = (state[x] + y) & 0xFF
        state[x], state[y] = state[y], state[x]
        out[index] ^= state[(state[x] + state[y]) & 0xFF]
    return bytes(out)


def challenge_response(challenge: bytes) -> bytes:
    return base64.b64encode(encipher(GAME_KEY, challenge))


def parse_pairs(fields):
    return dict(zip(fields[0::2], fields[1::2]))


def parse_qr2_sections(body: bytes):
    """keys, player rows and the team section's bytes of a QR2 reply or
    heartbeat (after its header)"""
    # (a value may be empty: the keys end at an empty key)
    fields = []
    cursor = 0
    while True:
        nul = body.index(b"\x00", cursor)
        if nul == cursor and len(fields) % 2 == 0:
            cursor += 1
            break
        fields.append(body[cursor:nul].decode())
        cursor = nul + 1
    keys = parse_pairs(fields)
    count = int.from_bytes(body[cursor:cursor + 2], "big")
    cursor += 2
    columns = []
    while body[cursor] != 0:
        nul = body.index(b"\x00", cursor)
        columns.append(body[cursor:nul].decode())
        cursor = nul + 1
    cursor += 1
    rows = []
    for _ in range(count):
        row = []
        for _ in columns:
            nul = body.index(b"\x00", cursor)
            row.append(body[cursor:nul].decode())
            cursor = nul + 1
        rows.append(dict(zip(columns, row)))
    return keys, columns, rows, body[cursor:]


class FakeMaster:
    """A stand-in QR2 heartbeat master on the loopback address: it takes
    heartbeats and keep-alives, challenges each new instance, checks its
    answer and says it is listed."""

    def __init__(self, port=0, verbose=False):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("127.0.0.1", port))
        self.socket.settimeout(0.2)
        self.port = self.socket.getsockname()[1]
        self.verbose = verbose
        self.heartbeats = []
        self.keepalives = 0
        self.challenges = {}
        self.responses = []
        self.listed = set()
        self.lock = threading.Lock()
        self.running = True
        self.thread = threading.Thread(target=self.run, daemon=True)

    def start(self):
        self.thread.start()
        return self

    def close(self):
        self.running = False
        self.thread.join(2)
        self.socket.close()

    def log(self, text):
        if self.verbose:
            print(text, flush=True)

    def run(self):
        while self.running:
            try:
                packet, source = self.socket.recvfrom(4096)
            except socket.timeout:
                continue
            except OSError:
                return
            if len(packet) < 5:
                continue
            kind, instance = packet[0], packet[1:5]
            if kind == 0x03:
                keys, _, rows, _ = self.parse_heartbeat(packet[5:])
                with self.lock:
                    self.heartbeats.append((instance, keys, rows))
                self.log("heartbeat: " + ", ".join(f"{key}={value}" for key, value in keys.items()
                                                    if not key.startswith("localip")))
                if instance not in self.challenges and keys.get("statechanged") != "2":
                    challenge = CAPTURED_CHALLENGE if not self.challenges else secrets.token_hex(9).encode()
                    self.challenges[instance] = challenge
                    self.socket.sendto(b"\xfe\xfd\x01" + instance + challenge + b"\x00", source)
            elif kind == 0x01:
                response = packet[5:].split(b"\x00")[0]
                expected = challenge_response(self.challenges.get(instance, b""))
                ok = response == expected
                with self.lock:
                    self.responses.append((instance, response, ok))
                self.log(f"challenge answered {'correctly' if ok else 'wrongly'}")
                if ok:
                    self.listed.add(instance)
                    self.socket.sendto(b"\xfe\xfd\x0a" + instance, source)
            elif kind == 0x08:
                with self.lock:
                    self.keepalives += 1
                self.log("keep-alive")

    @staticmethod
    def parse_heartbeat(body: bytes):
        # (an exit heartbeat has the keys alone)
        try:
            return parse_qr2_sections(body)
        except (ValueError, IndexError):
            fields = body.rstrip(b"\x00").split(b"\x00")
            return parse_pairs([field.decode() for field in fields]), [], [], b""

    def wait_for(self, predicate, timeout=5.0):
        end = time.time() + timeout
        while time.time() < end:
            with self.lock:
                if predicate(self):
                    return True
            time.sleep(0.05)
        return False


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 27900
    master = FakeMaster(port, verbose=True).start()
    print(f"stand-in master on 127.0.0.1:{master.port} (UDP); Ctrl-C stops it", flush=True)
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    master.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
