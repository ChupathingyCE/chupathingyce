#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks a port's Delta packets against docs/delta/peer.md and
docs/delta/ports.md. Python 3 alone, nothing to install.

    conformance.py check FILE...
        datagrams the port sent to a host's Delta port (5160): a file of hex,
        one datagram a line (spaces and "0x" allowed, # starts a comment), or
        a .bin file of one datagram
    conformance.py check --host FILE...
        the same, for datagrams a port's host sent its clients (WELCOME,
        LEGACY, ROSTER, BYE)
    conformance.py advert HEX
        a hosted game's 26 reserved advertisement bytes, as hex: OpenCE's
        fields alone, or with a port host's tag (layout 2, docs/delta/ports.md "If you
        host games from your port")
    conformance.py announce FORM
        the form a host posts to /v1/announce (or just its Delta fields)
    conformance.py listen [--port 5160] [--seconds 60]
        a stand-in Delta host on 127.0.0.1 alone: answers each HELLO with
        WELCOME (the platform capability agreed), sends ROSTER every 5 s,
        checks everything the port sends, and says at the end whether the
        port passed. It plays no game: point the port's Delta socket at it
        by hand. For the real thing, run loopback_host.sh.
    conformance.py listen --project N [--project-version V] [--announce 44]
        the same stand-in, as a port host of project N (docs/delta/registry.md,
        "Projects"): it prints the advertisement bytes such a host writes,
        to feed a client's advertisement reader, and its WELCOME names no
        Chupathingy build.
    conformance.py layout N [--table legacy.json --wire W]
        the layout facts of network version N (layouts.json, versioning.md
        "Layout facts by network version"), and with a signed table's
        document its "layouts" for a wire applied past 44
    conformance.py record N FILE [--options BYTES]
        a game settings record (a .bin file) a port read or made for a host
        of network version N: its size, its counts and its further options
        (--options: another project's row, Content Evolved's 88 at 44)
    conformance.py convert FROM TO IN OUT [--expect FILE]
        the record of version FROM's layout in IN made TO's, written to OUT,
        as delta_layout.c makes it; with --expect, a port's own conversion
        compared with it byte for byte (the first difference named by field)

Exit status 0 when everything passed, 1 when something failed. Warnings
(things a port may do but should know about) don't fail."""
import argparse
import json
import re
import socket
import struct
import sys
import time
from pathlib import Path

MAJOR = 1
HEADER = 12
MAXIMUM_DATAGRAM = 1200
BUILD_MOST = 31
TYPES = {1: "HELLO", 2: "WELCOME", 3: "LEGACY", 4: "ROSTER", 5: "BYE", 6: "PROFILE", 7: "TABLE", 8: "TABLE_HAVE"}
# what a client sends its host in the minimum kit (others are features a
# port would have read docs/delta/peer.md for)
CLIENT_TYPES = {1, 5, 6, 8}
# capability bits: a HELLO never carries these (retired, or the list's alone)
NEVER_SAID = {5: "md_maps (retired)", 9: "console_slots (retired)", 12: "ping (the list's alone)",
              18: "host_cheats (the list's alone)"}
KNOWN_CAPABILITIES = 26
# the platform registry (docs/delta/registry.md, "Platforms"; registry.json)
# the platform keys, by number, from the registry (docs/delta/registry.json):
# the one list, so a new number never needs an edit here
def _platform_keys():
    import json, pathlib
    path = pathlib.Path(__file__).resolve().parent.parent.parent / "docs" / "delta" / "registry.json"
    rows = json.loads(path.read_text(encoding="utf-8"))["platforms"]
    keys = {}
    for row in rows:
        keys[int(row["number"])] = row["key"]
    return [keys.get(number, f"platform {number}") for number in range(max(keys) + 1)]


PLATFORMS = _platform_keys()
# the OpenCE network versions our builds know (delta.h: 11 is the newest
# breaking one; 45 the built-in maximum today)
LEGACY_OLDEST = 11
LEGACY_NEWEST_KNOWN = 45
DELTA_FIELDS = ("delta", "platform_key", "arch", "build", "capabilities")
# the tag at the advertisement's tail (docs/delta/peer.md, "The tag"): layout 1 is a
# build of Chupathingy's code (a wire), layout 2 a port host (a project)
TAG_OFFSET = 17
TAG_LAYOUT_WIRE = 1
TAG_LAYOUT_PROJECT = 2
MODDED_VERSION = 0xFF00
# host-to-client types a port's host sends in the minimum kit
HOST_TYPES = {2, 3, 4, 5}


def _projects():
    import json, pathlib
    path = pathlib.Path(__file__).resolve().parent.parent.parent / "docs" / "delta" / "registry.json"
    rows = json.loads(path.read_text(encoding="utf-8"))["projects"]["list"]
    return {int(row["number"]): row["name"] for row in rows if row["name"]}


PROJECTS = _projects()


def tag_check(tag):
    check = 0xA5
    for byte in tag[:8]:
        check ^= byte
    return check


def project_tag(project, version):
    tag = bytes([0x44, 0x4C, MAJOR, TAG_LAYOUT_PROJECT, project, version & 0xFF, version >> 8, 0])
    return tag + bytes([tag_check(tag)])


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
        report.warn(where, "platform 0 (unknown): ask for your port's number, docs/delta/registry.md")
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
                    "speak in full (docs/delta/peer.md)")
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


def check_host_datagram(report, where, data):
    """one datagram from a port's host to a client: its header, then its
    message (WELCOME, LEGACY, ROSTER or BYE)"""
    if len(data) < HEADER or data[:2] != b"DP":
        report.fail(where, "not a Delta Peer datagram (the magic, or under the 12-byte header)")
        return
    if len(data) > MAXIMUM_DATAGRAM:
        report.fail(where, f"{len(data)} bytes: over the {MAXIMUM_DATAGRAM} a datagram may be")
    major, kind, length, reserved, session = struct.unpack_from("<BBHHI", data, 2)
    payload = data[HEADER:]
    if length != len(payload):
        report.fail(where, f"payload length {length}, but {len(payload)} bytes follow the header")
        return
    if reserved:
        report.fail(where, f"header bytes 6 and 7 are 0x{reserved:04x}, not 0")
    if not session:
        report.fail(where, "session 0: a host answers with the client's own session, never 0")
    if kind == 3:
        if payload:
            report.warn(where, f"LEGACY with {len(payload)} payload bytes (it has none)")
        report.ok(where, f"LEGACY of major {major}")
        return
    if major != MAJOR:
        report.fail(where, f"major {major}, not {MAJOR} (only LEGACY goes out under another major)")
    if kind not in HOST_TYPES:
        name = TYPES.get(kind)
        if name:
            report.fail(where, f"{name} is a client's to send, not a host's")
        else:
            report.warn(where, f"message type {kind}: not in the minimum kit (fine if you built that feature)")
        return
    if kind == 5:
        report.ok(where, "BYE")
        return
    failed = report.failed
    if kind == 2:
        if len(payload) < 24:
            report.fail(where, f"WELCOME payload of {len(payload)} bytes, under its 24 fixed ones")
            return
        offered, agreed, legacy, machine, size, serial = struct.unpack_from("<IIHBBI", payload)
        if size > BUILD_MOST or len(payload) < 24 + size:
            report.fail(where, f"build length {size}: at most {BUILD_MOST}, and the bytes must follow")
            return
        build = payload[24:24 + size]
        if agreed & ~offered:
            report.fail(where, f"agreed 0x{agreed:08x} has bits the host didn't offer (0x{offered:08x})")
        for bit, name in NEVER_SAID.items():
            if offered & (1 << bit):
                report.fail(where, f"capability bit {bit}, {name}, in a WELCOME")
        if offered & ~1:
            report.warn(where, f"capabilities past platform (0x{offered:08x}): agree only to what you speak in full")
        if any(byte < 0x20 or byte > 0x7E for byte in build):
            report.fail(where, f"build text {build!r} is not printable ASCII")
        if build.lower().startswith(b"chupa-") or b"chupathingy" in build.lower():
            report.fail(where, f"build text {build!r} names Chupathingy: say your own project and version")
        if legacy < LEGACY_OLDEST:
            report.fail(where, f"network version {legacy}: our builds play {LEGACY_OLDEST} and later")
        if serial != 0xFFFFFFFF:
            report.warn(where, f"legacy table serial {serial}: 0xFFFFFFFF unless your host checks signed tables")
        key_text = check_key(report, where, payload[16:24])
        (report.ok if report.failed == failed else report.seen)(where, f"WELCOME: build "
            f"{build.decode('ascii', 'replace')!r}, network version {legacy}, host machine {machine}, offered "
            f"0x{offered:08x}, agreed 0x{agreed:08x}; {key_text}")
        return
    if len(payload) < 12:
        report.fail(where, f"ROSTER payload of {len(payload)} bytes, under its 12 fixed ones")
        return
    room, players, count = struct.unpack_from("<IB3xB", payload)
    if count > 32 or len(payload) < 12 + 36 * count:
        report.fail(where, f"ROSTER of {count} entries in {len(payload)} bytes: at most 32, 36 bytes each")
        return
    for index in range(count):
        machine, flags = payload[12 + 36 * index], payload[13 + 36 * index]
        if flags & ~0x07:
            report.fail(where, f"entry {index} (machine {machine}): flags 0x{flags:02x}, only 0x01 to 0x04 exist")
    (report.ok if report.failed == failed else report.seen)(where, f"ROSTER: {count} machine(s), room "
        f"capabilities 0x{room:08x}, room's player limit {players or 'none said'}")


def command_check(args):
    report = Report()
    for path in args.files:
        session = None
        for number, data in enumerate(read_datagrams(path), 1):
            if args.host:
                check_host_datagram(report, f"{path}:{number}", data)
                continue
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
    if version >= MODDED_VERSION:
        report.ok(where, f"version word 0x{version:04x}: a Content Evolved modded game, whose 26 bytes are its "
                  "own (nothing else checked)")
        return report
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
    tag = data[TAG_OFFSET:]
    said = "no Delta tag: our clients join by your number and say no HELLO"
    if any(tag):
        said = "the tag is not valid"
        if tag[:2] != b"DL":
            report.fail(where, "bytes 17 to 25 are neither zero nor a tag ('D', 'L' first)")
        elif tag[8] != tag_check(tag):
            report.fail(where, f"the tag's check is 0x{tag[8]:02x}, not 0x{tag_check(tag):02x} (0xA5 xor the "
                        "eight bytes before)")
        elif tag[3] == TAG_LAYOUT_WIRE:
            report.fail(where, "a layout 1 tag names a wire, a revision of Chupathingy's own game code: a port "
                        "host writes layout 2, with its project")
        elif tag[3] != TAG_LAYOUT_PROJECT:
            report.fail(where, f"tag layout {tag[3]}: a port host writes layout {TAG_LAYOUT_PROJECT}")
        else:
            project, project_version, spare = tag[4], tag[5] | tag[6] << 8, tag[7]
            if tag[2] != MAJOR:
                report.fail(where, f"the tag's major is {tag[2]}, not {MAJOR}: say the major you answer HELLO with")
            if project <= 1:
                report.fail(where, f"project {project}: {'none' if not project else 'Chupathingy, whose builds write layout 1'}; "
                            "ask for your project's number (docs/delta/registry.md, Projects)")
            elif project not in PROJECTS:
                report.fail(where, f"project {project} is not in the registry: ask for a number, never pick one")
            if spare:
                report.fail(where, f"tag byte 7 (byte 24) is 0x{spare:02x}: reserved, write 0")
            said = (f"a port host's tag: project {project} ({PROJECTS.get(project, 'not in the registry')}), its "
                    f"version {project_version}; our clients say HELLO and join by your number")
    if any(data[4:TAG_OFFSET]):
        report.warn(where, "bytes 4 to 16 are not zero (nothing is defined there for a port)")
    (report.ok if not report.failed else report.seen)(where, f"network version {version}, flags 0x{flags:02x}, "
                                                        f"kind {kind}; {said}")
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


def welcome(session, capabilities, legacy, build=b"conformance host", host_caps=0x03FBEDDF):
    key = bytes([3, 1, 0x02, 16, 128, 128, 12, 0])
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
    build = b"conformance host"
    offered = 0x03FBEDDF
    if args.project is not None:
        if args.project <= 1 or args.project > 255 or not 0 <= args.project_version <= 0xFFFF:
            report.fail("listen", "--project is 2 to 255 (docs/delta/registry.md, Projects), --project-version 0 to 65535")
            sock.close()
            return report
        name = PROJECTS.get(args.project, f"project {args.project}")
        reserved = struct.pack("<HBB", args.announce, 0x01, 0) + bytes(13) + project_tag(args.project,
                                                                                        args.project_version)
        build = f"{name} test host {args.project_version}".encode("ascii", "replace")[:BUILD_MOST]
        offered = 1
        print(f"as a port host of {name} (project {args.project}), announcing {args.announce}; its advertisement's "
              f"reserved bytes:\n{reserved.hex()}")
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
            sock.sendto(welcome(result["session"], result["capabilities"], result["legacy"], build, offered), source)
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


# ---------- layout facts by network version (layouts.json, delta_layout.c)

LAYOUT_HEAD = 0x3320
LAYOUT_OPTIONS = 0x1C
LAYOUT_EXTENDED = 16
LAYOUT_MAXIMUM = 13136 + 128
LAYOUT_OPTIONS_MAXIMUM = 0x2C + 128
EXTENDED_DEFAULTS = bytes([0, 0, 1, 150, 200, 5, 10, 1, 0, 1, 0, 0, 0, 0, 0, 0])


def layouts_file(path=None):
    if path:
        return Path(path)
    here = Path(__file__).resolve().parent
    for candidate in (here.parent / "layouts.json", here.parent.parent / "docs" / "delta" / "layouts.json"):
        if candidate.exists():
            return candidate
    raise SystemExit("conformance.py: no layouts.json beside the spec (give --layouts)")


def load_layouts(path=None):
    return json.loads(layouts_file(path).read_text())


def table_facts(document, wire):
    """a signed table's document's layouts for a wire, as (version, fact) oldest first"""
    row = document.get("wires", {}).get(wire, {})
    return sorted(((int(version), fact) for version, fact in row.get("layouts", {}).items()), key=lambda item: item[0])


def layout_of(layouts, version, facts=()):
    """a version's layout: layouts.json's (past its presumed, the newest's), then a table's facts past the
    newest within delta_layout.c's limits; None before the oldest"""
    newest_fact = max([v for v, _ in facts if v > layouts["newest"]] or [0])
    if version < layouts["oldest"] or version > max(layouts["presumed"], newest_fact + 1):
        return None
    key = str(min(version, layouts["newest"])) if version > layouts["presumed"] else str(version)
    layout = json.loads(json.dumps(layouts["versions"][key]))
    layout["version"] = version
    layout["refused"] = 0
    if version <= layouts["newest"]:
        return layout
    for fact_version, fact in facts:
        if fact_version <= layouts["newest"] or fact_version > version:
            continue

        def take(ok, field, value):
            if ok:
                layout[field] = value
            else:
                layout["refused"] += 1
        if fact.get("options_size"):
            growth = fact["options_size"] - layout["options_size"]
            ok = growth >= 0 and fact["options_size"] <= LAYOUT_OPTIONS_MAXIMUM and \
                layout["settings_size"] + growth <= LAYOUT_MAXIMUM
            take(ok, "options_size", fact["options_size"])
            if ok:
                layout["settings_size"] += growth
        if fact.get("settings_size"):
            size = fact["settings_size"]
            take(layout["settings_size"] <= size <= LAYOUT_MAXIMUM, "settings_size", size)
        if fact.get("shield_effects_maximum"):
            value = fact["shield_effects_maximum"]
            take(layout["shield_effects_maximum"] <= value <= 15, "shield_effects_maximum", value)
        if "join_request_extra" in fact:
            ok = fact.get("join_request_fill", "zeros") == "zeros" and 0 <= fact["join_request_extra"] <= 1024
            take(ok, "join_request_extra", fact["join_request_extra"])
            at = layout["join_request_version_at"]
            if ok and at is not None and at + 2 > layout["join_request_extra"]:
                layout["join_request_version_at"] = None
        if "join_request_version_at" in fact:
            at = fact["join_request_version_at"]
            take(isinstance(at, int) and 0 <= at and at + 2 <= layout["join_request_extra"], "join_request_version_at", at)
        if fact.get("header_extra"):
            value = fact["header_extra"]
            take(value <= 8 and not (layout["epoch_kind"] and value < 4), "header_extra", value)
        if fact.get("epoch_kind"):
            value = fact["epoch_kind"]
            take(layout["header_extra"] >= 4 and 93 <= value <= 255, "epoch_kind", value)
        for part in fact.get("unit_state_parts", []):
            ok = part.get("bit", 0) in (6, 7) and 1 <= part.get("size", 0) <= 64 and part.get("at") in ("after_up", "last")
            if ok:
                layout["unit_state_parts"] = [p for p in layout["unit_state_parts"] if p["bit"] != part["bit"]] + [part]
            else:
                layout["refused"] += 1
        if fact.get("rider_ejection_up"):
            value = fact["rider_ejection_up"]
            take(value <= 999, "rider_ejection_up", value)
        layout["dropped_messages"] = sorted(set(layout["dropped_messages"]) |
                                            {kind for kind in fact.get("dropped_messages", []) if 1 <= kind <= 255})
    layout["local_data_offset"] = LAYOUT_HEAD + layout["options_size"]
    layout["trailing_size"] = layout["settings_size"] - layout["local_data_offset"] - 4
    return layout


def sanitize_extended(extended):
    """delta_layout_extended_sanitize"""
    out = bytearray(extended[:LAYOUT_EXTENDED])
    out[0] = out[0] if out[0] <= 3 else 0
    for index in (1, 7, 9):
        out[index] = 1 if out[index] else 0
    for index, (least, most, default) in {2: (1, 16, 1), 3: (100, 200, 150), 4: (100, 250, 200), 5: (1, 30, 5),
                                          6: (0, 30, 10)}.items():
        out[index] = out[index] if least <= out[index] <= most else default
    out[8] = out[8] if out[8] <= 2 else 0
    for index in range(10, LAYOUT_EXTENDED):
        out[index] = 0
    return bytes(out)


def record_fits(size, options):
    return LAYOUT_OPTIONS <= options <= LAYOUT_OPTIONS_MAXIMUM and LAYOUT_HEAD + options + 4 <= size <= LAYOUT_MAXIMUM


def convert_record(record, from_options, to_size, to_options):
    """delta_layout_convert: (the record made the other layout, the source's further options sanitized)"""
    if not record_fits(len(record), from_options) or not record_fits(to_size, to_options):
        return None, None
    options = record[LAYOUT_HEAD:LAYOUT_HEAD + from_options]
    extended = bytes(options[LAYOUT_OPTIONS + index] if LAYOUT_OPTIONS + index < from_options else EXTENDED_DEFAULTS[index]
                     for index in range(LAYOUT_EXTENDED))
    new_options = bytes(options[index] if index < from_options else
                        EXTENDED_DEFAULTS[index - LAYOUT_OPTIONS] if LAYOUT_OPTIONS <= index < LAYOUT_OPTIONS + LAYOUT_EXTENDED
                        else 0 for index in range(to_options))
    local = record[LAYOUT_HEAD + from_options:LAYOUT_HEAD + from_options + 4]
    tail = record[LAYOUT_HEAD + from_options + 4:]
    to_tail = to_size - LAYOUT_HEAD - to_options - 4
    out = record[:LAYOUT_HEAD] + new_options + local + (tail[:to_tail] + bytes(max(0, to_tail - len(tail))))
    return out, sanitize_extended(extended)


def record_field(layouts, layout, offset):
    for field in layouts["record"]["fields"]:
        if field["offset"] <= offset < field["offset"] + field["size"]:
            extra = ""
            if "element_size" in field:
                extra = f"[{(offset - field['offset']) // field['element_size']}]"
            return f"{field['name']}{extra} (+0x{offset - field['offset']:x})"
    if offset < layout["local_data_offset"]:
        return f"variant_options (+0x{offset - LAYOUT_HEAD:x})"
    if offset < layout["local_data_offset"] + 4:
        return "local_data"
    return f"trailing (+{offset - layout['local_data_offset'] - 4})"


def facts_for(args):
    if not getattr(args, "table", None):
        return ()
    document = json.loads(Path(args.table).read_text())
    return table_facts(document, args.wire)


def command_layout(args):
    report = Report()
    layouts = load_layouts(args.layouts)
    layout = layout_of(layouts, args.version, facts_for(args))
    if layout is None:
        report.fail("layout", f"network version {args.version} is before {layouts['oldest']}, which no reader joins")
        return report
    print(json.dumps(layout, indent=1))
    if layout["refused"]:
        report.warn("layout", f"{layout['refused']} table fact field(s) past the limits, not taken")
    return report


def command_record(args):
    report = Report()
    layouts = load_layouts(args.layouts)
    layout = layout_of(layouts, args.version, facts_for(args))
    where = f"record of {args.version}"
    if layout is None:
        report.fail(where, f"version {args.version} is before {layouts['oldest']}")
        return report
    record = Path(args.file).read_bytes()
    options = args.options or layout["options_size"]
    size = layout["settings_size"] + options - layout["options_size"]
    if len(record) != size:
        report.fail(where, f"{len(record)} bytes, not {size} (options 0x{options:x}, local data at {LAYOUT_HEAD + options})")
        return report
    report.ok(where, f"{size} bytes: options 0x{options:x} at 0x{LAYOUT_HEAD:x}, local data at {LAYOUT_HEAD + options}, "
              f"{size - LAYOUT_HEAD - options - 4} trailing")
    machines, players, maximum = struct.unpack_from("<h", record, 0x112)[0], struct.unpack_from("<h", record, 0x2314)[0], \
        record[0x10E]
    for name, value in (("machine_count", machines), ("player_count", players), ("maximum_players", maximum)):
        if not 0 <= value <= 128:
            report.fail(where, f"{name} {value}, outside 0 to 128")
    if options >= LAYOUT_OPTIONS + LAYOUT_EXTENDED:
        extended = record[LAYOUT_HEAD + LAYOUT_OPTIONS:LAYOUT_HEAD + LAYOUT_OPTIONS + LAYOUT_EXTENDED]
        clean = sanitize_extended(extended)
        if clean != extended:
            names = [byte["name"] for byte in layouts["record"]["extended_options"]["bytes"]]
            bad = [names[min(index, len(names) - 1)] for index in range(LAYOUT_EXTENDED) if clean[index] != extended[index]]
            report.warn(where, f"further options out of range ({', '.join(sorted(set(bad)))}): a reader takes "
                        "them sanitized")
        else:
            report.ok(where, f"further options: mode {extended[0]}, hardcore {extended[1]}")
    trailing = record[LAYOUT_HEAD + options + 4:]
    if trailing and any(trailing) and len(trailing) >= 64:
        report.seen(where, "the trailing bytes carry MCC digests (an MCC map's game)")
    return report


def command_convert(args):
    report = Report()
    layouts = load_layouts(args.layouts)
    facts = facts_for(args)
    source, target = layout_of(layouts, args.source, facts), layout_of(layouts, args.target, facts)
    where = f"{args.source} to {args.target}"
    if source is None or target is None:
        report.fail(where, "a version before the oldest")
        return report
    record = Path(args.input).read_bytes()
    from_options = args.options or source["options_size"]
    out, extended = convert_record(record, from_options, target["settings_size"], target["options_size"])
    if out is None:
        report.fail(where, f"{len(record)} bytes is not a record with options 0x{from_options:x}")
        return report
    Path(args.output).write_bytes(out)
    report.ok(where, f"{len(record)} bytes to {len(out)}; further options mode {extended[0]}, hardcore {extended[1]}")
    if args.expect:
        theirs = Path(args.expect).read_bytes()
        if len(theirs) != len(out):
            report.fail(where, f"the port's record is {len(theirs)} bytes, not {len(out)}")
        else:
            differ = next((index for index in range(len(out)) if out[index] != theirs[index]), None)
            if differ is None:
                report.ok(where, "the port's conversion matches byte for byte")
            else:
                report.fail(where, f"the port's conversion differs first at {differ}, "
                            f"{record_field(layouts, target, differ)}: 0x{theirs[differ]:02x}, not 0x{out[differ]:02x}")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("check")
    check.add_argument("--host", action="store_true", help="the datagrams are a host's, to its clients")
    check.add_argument("files", nargs="+")
    advert = sub.add_parser("advert")
    advert.add_argument("hex")
    announce = sub.add_parser("announce")
    announce.add_argument("form")
    listen = sub.add_parser("listen")
    listen.add_argument("--port", type=int, default=5160)
    listen.add_argument("--seconds", type=float, default=60.0)
    listen.add_argument("--project", type=int, help="act as a port host of this project (docs/delta/registry.md, Projects)")
    listen.add_argument("--project-version", type=int, default=0)
    listen.add_argument("--announce", type=int, default=44, help="the network version its advertisement says")
    for name in ("layout", "record", "convert"):
        command = sub.add_parser(name)
        if name == "convert":
            command.add_argument("source", type=int)
            command.add_argument("target", type=int)
            command.add_argument("input")
            command.add_argument("output")
            command.add_argument("--expect", help="a port's own conversion, to compare")
        else:
            command.add_argument("version", type=int)
            if name == "record":
                command.add_argument("file")
        if name != "layout":
            command.add_argument("--options", type=lambda text: int(text, 0), help="the source's options' bytes, "
                                 "for another project's row (Content Evolved's 0x58 at 44)")
        command.add_argument("--layouts", help="layouts.json (the spec's, by default)")
        command.add_argument("--table", help="a signed table's document (legacy.json), for its layouts")
        command.add_argument("--wire", default="chupa-44b", help="the table's wire whose layouts to apply")
    args = parser.parse_args()
    report = {"check": command_check, "advert": command_advert, "announce": command_announce,
              "listen": command_listen, "layout": command_layout, "record": command_record,
              "convert": command_convert}[args.command](args)
    print(f"{'FAILED' if report.failed else 'passed'}: {report.failed} failure(s), {report.warned} warning(s)")
    return 1 if report.failed else 0


if __name__ == "__main__":
    sys.exit(main())
