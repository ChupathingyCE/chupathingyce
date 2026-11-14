#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks a port's Delta packets against docs/delta.md and
docs/delta-for-ports.md. Python 3 alone, nothing to install.

    conformance.py check FILE...
        datagrams the port sent to a host's Delta port (5160): a file of hex,
        one datagram a line (spaces and "0x" allowed, # starts a comment), or
        a .bin file of one datagram
    conformance.py advert HEX
        a hosted game's 26 reserved advertisement bytes, as hex
    conformance.py announce FORM
        the form a host posts to /v1/announce (or just its Delta fields)
    conformance.py listen [--port 5160] [--seconds 60]
        a stand-in Delta host on 127.0.0.1 alone: answers each HELLO with
        WELCOME (the platform capability agreed), sends ROSTER every 5 s,
        checks everything the port sends, and says at the end whether the
        port passed. It plays no game: point the port's Delta socket at it
        by hand. For the real thing, run loopback_host.sh.

Exit status 0 when everything passed, 1 when something failed. Warnings
(things a port may do but should know about) don't fail."""
import argparse
import re
import socket
import struct
import sys
import time

MAJOR = 1
HEADER = 12
MAXIMUM_DATAGRAM = 1200
BUILD_MOST = 31
TYPES = {1: "HELLO", 2: "WELCOME", 3: "LEGACY", 4: "ROSTER", 5: "BYE", 6: "PROFILE", 7: "TABLE", 8: "TABLE_HAVE"}
# what a client sends its host in the minimum kit (others are features a
# port would have read docs/delta.md for)
CLIENT_TYPES = {1, 5, 6, 8}
# capability bits: a HELLO never carries these (retired, or the list's alone)
NEVER_SAID = {5: "md_maps (retired)", 9: "console_slots (retired)", 12: "ping (the list's alone)",
              18: "host_cheats (the list's alone)"}
KNOWN_CAPABILITIES = 26
# the platform registry (docs/delta.md, "Platform numbers")
PLATFORMS = ["unknown", "pc_windows", "pc_macos", "pc_linux", "android", "steam_deck", "xbox", "xbox360", "wiiu",
             "switch", "psp", "vita", "xbox_uwp", "pc_freebsd", "ps3", "ps4", "ps5", "n64", "pc_legacy",
             "steamlink", "ios", "tvos"]
# the OpenCE network versions our builds know (delta.h: 11 is the newest
# breaking one; 45 the built-in maximum today)
LEGACY_OLDEST = 11
LEGACY_NEWEST_KNOWN = 45
DELTA_FIELDS = ("delta", "platform_key", "arch", "build", "capabilities")


class Report:
    def __init__(self):
        self.failed = 0
        self.warned = 0

    def fail(self, where, text):
        self.failed += 1
        print(f"FAIL  {where}: {text}")

    def warn(self, where, text):
        self.warned += 1
        print(f"warn  {where}: {text}")

    def ok(self, where, text):
        print(f"ok    {where}: {text}")

    def seen(self, where, text):
        print(f"      {where}: {text}")


def platform_name(number):
    return PLATFORMS[number] if number < len(PLATFORMS) else f"platform {number} (not in the registry)"


def check_key(report, where, key):
    platform, version, flags, host, join, opt_in, memory, reserved = key
    if platform == 0:
        report.warn(where, "platform 0 (unknown): ask for your port's number, docs/delta.md \"Platform numbers\"")
    elif platform >= len(PLATFORMS):
        report.fail(where, f"platform {platform} is not in the registry: ask for a number, never pick one")
    if version != 1:
        report.fail(where, f"platform key version {version}, not 1")
    if flags & ~0x03:
        report.fail(where, f"platform key flags 0x{flags:02x}: only 0x01 (opted in) and 0x02 (dedicated) exist")
    if join == 0:
        report.fail(where, "join_players 0: a machine that joins says how many players it joins with")
    if opt_in and opt_in < join:
        report.warn(where, f"join_players_opt_in {opt_in} under join_players {join}: read as {join}")
    if reserved:
        report.fail(where, f"platform key byte 7 is 0x{reserved:02x}, not 0")
    return (f"{platform_name(platform)}, hosts {host}, joins {join} ({opt_in} opted in), "
            f"memory class {memory}{', opted in' if flags & 1 else ''}")


def check_hello(report, where, payload):
    failed = report.failed
    if len(payload) < 20:
        report.fail(where, f"HELLO payload of {len(payload)} bytes, under its 20 fixed ones")
        return None
    capabilities, legacy, machine, length, serial = struct.unpack_from("<IHBBI", payload)
    if length > BUILD_MOST or len(payload) < 20 + length:
        report.fail(where, f"build length {length}: at most {BUILD_MOST}, and the bytes must follow")
        return None
    build = payload[20:20 + length]
    rest = len(payload) - 20 - length
    if any(byte < 0x20 or byte > 0x7E for byte in build):
        report.fail(where, f"build text {build!r} is not printable ASCII")
    if not length:
        report.warn(where, "no build text: hosts log it, and players' reports name it")
    for bit, name in NEVER_SAID.items():
        if capabilities & (1 << bit):
            report.fail(where, f"capability bit {bit}, {name}, in a HELLO")
    if capabilities >> KNOWN_CAPABILITIES:
        report.warn(where, f"capability bits past {KNOWN_CAPABILITIES - 1} (0x{capabilities:08x}): not in the "
                    "registry yet; hosts ignore them")
    if not capabilities & 1:
        report.warn(where, "no platform capability (bit 0): hosts then can't keep the game to your limits")
    extra = capabilities & ~1 & ~sum(1 << bit for bit in NEVER_SAID)
    if extra:
        report.warn(where, f"capabilities past platform (0x{extra:08x}): each one is a feature you must then "
                    "speak in full (docs/delta.md)")
    if legacy < LEGACY_OLDEST:
        report.fail(where, f"network version {legacy}: our builds play {LEGACY_OLDEST} and later")
    elif legacy > LEGACY_NEWEST_KNOWN:
        report.warn(where, f"network version {legacy}, newer than any our builds know ({LEGACY_NEWEST_KNOWN})")
    if machine == 0xFF:
        report.fail(where, "machine index 255: say HELLO only once the host gave this machine its index")
    if serial not in (0, 0xFFFFFFFF):
        report.warn(where, f"legacy table serial {serial}: only if you check and keep signed tables; "
                    "0xFFFFFFFF says you take none")
    if rest not in (0, 4):
        report.warn(where, f"{rest} bytes after the build: HELLO has 4 (the game modes) there")
    modes = struct.unpack_from("<I", payload, 20 + length)[0] if rest >= 4 else 0
    if modes and not capabilities & (1 << 25):
        report.fail(where, f"game modes 0x{modes:x} without the game_modes capability (bit 25)")
    key_text = check_key(report, where, payload[12:20])
    (report.ok if report.failed == failed else report.seen)(where, f"HELLO: build {build.decode('ascii', 'replace')!r}, machine {machine}, network version "
              f"{legacy}, capabilities 0x{capabilities:08x}, table serial 0x{serial:08x}; {key_text}")
    return {"capabilities": capabilities, "legacy": legacy, "machine": machine}


def check_datagram(report, where, data, session=None):
    """one datagram from a port's client: its header, then its message"""
    if len(data) < HEADER:
        report.fail(where, f"{len(data)} bytes: shorter than the 12-byte header")
        return None
    if len(data) > MAXIMUM_DATAGRAM:
        report.fail(where, f"{len(data)} bytes: over the {MAXIMUM_DATAGRAM} a datagram may be")
    if data[:2] != b"DP":
        report.fail(where, f"magic {data[:2].hex()}, not 4450 (\"DP\")")
        return None
    major, kind, length, reserved, sent_session = struct.unpack_from("<BBHHI", data, 2)
    if major != MAJOR:
        report.fail(where, f"major {major}, not {MAJOR}")
    if length != len(data) - HEADER:
        report.fail(where, f"payload length {length}, but {len(data) - HEADER} bytes follow the header")
        return None
    if reserved:
        report.fail(where, f"header bytes 6 and 7 are 0x{reserved:04x}, not 0")
    if not sent_session:
        report.fail(where, "session 0: a session is random and never 0")
    elif session is not None and sent_session != session:
        report.warn(where, "another session than the last HELLO's: a new handshake?")
    name = TYPES.get(kind)
    if kind not in CLIENT_TYPES:
        if name:
            report.fail(where, f"{name} is the host's to send, not a client's")
        else:
            report.warn(where, f"message type {kind}: not in the minimum kit (fine if you built that feature)")
        return {"session": sent_session, "type": kind}
    payload = data[HEADER:]
    if kind == 1:
        hello = check_hello(report, where, payload)
        return dict(hello or {}, session=sent_session, type=kind)
    if kind == 5:
        if payload:
            report.warn(where, f"BYE with {len(payload)} payload bytes (it has none)")
        report.ok(where, "BYE")
    else:
        report.ok(where, f"{name}, {len(payload)} payload bytes")
    return {"session": sent_session, "type": kind}


def read_datagrams(path):
    if path.endswith(".bin"):
        with open(path, "rb") as file:
            return [file.read()]
    datagrams = []
    with open(path, encoding="utf-8") as file:
        for line in file:
            line = line.split("#", 1)[0]
            text = re.sub(r"0x|[\s,]", "", line)
            if text:
                datagrams.append(bytes.fromhex(text))
    return datagrams


def command_check(args):
    report = Report()
    for path in args.files:
        session = None
        for number, data in enumerate(read_datagrams(path), 1):
            result = check_datagram(report, f"{path}:{number}", data, session)
            if result and result.get("type") == 1:
                session = result["session"]
    return report


def command_advert(args):
    report = Report()
    data = bytes.fromhex(re.sub(r"0x|[\s,]", "", args.hex))
    where = "advertisement"
    if len(data) != 26:
        report.fail(where, f"{len(data)} bytes, not the 26 (0x1A) reserved bytes")
        return report
    version, flags, kind = struct.unpack_from("<HBB", data)
    if version < LEGACY_OLDEST:
        report.fail(where, f"network version {version}: our clients join {LEGACY_OLDEST} and later")
    if flags & ~0x07:
        report.warn(where, f"flags 0x{flags:02x}: bits past 0x04 mean nothing to our builds yet")
    if not flags & 0x01:
        report.fail(where, "flag 0x01 (the distributed netcode) not set: every host since OpenCE's 4 plays it")
    if flags & 0x04 and version < 39:
        report.fail(where, "flag 0x04 below version 39 reads as Delta's old flag: only a dedicated server of 39 "
                    "or later sets it")
    if kind and version < 37:
        report.fail(where, f"game kind {kind} below version 37 (the byte was zero then)")
    if any(data[17:26]):
        report.fail(where, "bytes 17 to 25 (Delta's tag) are not zero: a port has no wire ID of its own, so it "
                    "writes no tag")
    if any(data[4:17]):
        report.warn(where, "bytes 4 to 16 are not zero (nothing is defined there for a port)")
    report.ok(where, f"network version {version}, flags 0x{flags:02x}, kind {kind}")
    return report


def command_announce(args):
    report = Report()
    fields = {}
    for part in args.form.split("&"):
        if "=" in part:
            name, value = part.split("=", 1)
            if name in fields:
                report.fail("announce", f"{name} twice")
            fields[name] = value
    for name in DELTA_FIELDS:
        if name not in fields:
            report.fail("announce", f"no {name} field")
    if fields.get("delta") not in (None, "0", "1"):
        report.fail("announce", f"delta={fields['delta']}: 0 (no Delta Peer) or 1 (the major you answer HELLO with)")
    if "platform_key" in fields:
        key = fields["platform_key"]
        if not re.fullmatch(r"[0-9a-f]{16}", key):
            report.fail("announce", "platform_key is not 16 lowercase hex digits")
        else:
            report.ok("announce", "platform key: " + check_key(report, "announce platform_key", bytes.fromhex(key)))
    if "capabilities" in fields and not re.fullmatch(r"[0-9a-f]{8}", fields["capabilities"]):
        report.fail("announce", "capabilities is not 8 lowercase hex digits")
    if fields.get("arch") not in (None, "x64", "x86", "arm64", "arm", ""):
        report.warn("announce", f"arch={fields['arch']}: the site knows x64, x86, arm64 and arm; another shows "
                    "as unknown")
    if not report.failed:
        report.ok("announce", "Delta fields present and well formed")
    return report


def welcome(session, capabilities, legacy):
    build = b"conformance host"
    key = bytes([3, 1, 0x02, 16, 128, 128, 12, 0])
    host_caps = 0x03FBEDDF
    payload = struct.pack("<IIHBBI", host_caps, host_caps & capabilities & 1, legacy, 0xFF, len(build), 0xFFFFFFFF)
    payload += key + build + struct.pack("<I", 0)
    return struct.pack("<2sBBHHI", b"DP", MAJOR, 2, len(payload), 0, session) + payload


def roster(session, machine, capabilities, key):
    entry = struct.pack("<BBHI", machine, 0x01 | 0x02, 0, capabilities & 1) + key + bytes(20)
    payload = struct.pack("<IB3xB3x", capabilities & 1, key[4] or 16, 1) + entry
    return struct.pack("<2sBBHHI", b"DP", MAJOR, 4, len(payload), 0, session) + payload


def command_listen(args):
    report = Report()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", args.port))
    sock.settimeout(0.25)
    print(f"listening on 127.0.0.1:{args.port} for {args.seconds} s (this machine only)")
    sessions = {}
    end = time.monotonic() + args.seconds
    welcomed = byes = 0
    while time.monotonic() < end:
        now = time.monotonic()
        for (source, session), state in list(sessions.items()):
            if now - state["roster"] >= 5.0:
                sock.sendto(roster(session, state["machine"], state["capabilities"], state["key"]), source)
                state["roster"] = now
        try:
            data, source = sock.recvfrom(2048)
        except socket.timeout:
            continue
        where = f"from port {source[1]}"
        result = check_datagram(report, where, data)
        if not result:
            continue
        if data[2] != MAJOR:
            sock.sendto(struct.pack("<2sBBHHI", b"DP", MAJOR, 3, 0, 0, result["session"]), source)
            continue
        if result["type"] == 1 and "machine" in result:
            key = data[HEADER + 12:HEADER + 20]
            known = (source, result["session"]) in sessions
            sessions[(source, result["session"])] = {"machine": result["machine"], "key": key, "roster": 0.0,
                                                     "capabilities": result["capabilities"]}
            sock.sendto(welcome(result["session"], result["capabilities"], result["legacy"]), source)
            if not known:
                welcomed += 1
        elif result["type"] == 5:
            sessions.pop((source, result["session"]), None)
            byes += 1
    sock.close()
    print(f"{welcomed} handshake(s) welcomed, {byes} BYE(s)")
    if not welcomed:
        report.fail("listen", "no valid HELLO came")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("check")
    check.add_argument("files", nargs="+")
    advert = sub.add_parser("advert")
    advert.add_argument("hex")
    announce = sub.add_parser("announce")
    announce.add_argument("form")
    listen = sub.add_parser("listen")
    listen.add_argument("--port", type=int, default=5160)
    listen.add_argument("--seconds", type=float, default=60.0)
    args = parser.parse_args()
    report = {"check": command_check, "advert": command_advert, "announce": command_announce,
              "listen": command_listen}[args.command](args)
    print(f"{'FAILED' if report.failed else 'passed'}: {report.failed} failure(s), {report.warned} warning(s)")
    return 1 if report.failed else 0


if __name__ == "__main__":
    sys.exit(main())
