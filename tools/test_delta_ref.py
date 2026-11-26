# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
"""The Delta reference client for ports (tools/delta-ref): it builds alone as
strict C99 and passes its tests (under the sanitizers where the compiler has
them); its golden packets are still what the game's own Delta code writes;
it handshakes with the game's real Delta Peer host; and the conformance
script passes the goldens and its own stand-in host's answers read right.
The big-endian run is tools/test_delta_big_endian.py's."""
import importlib.util
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
KIT = ROOT / "tools" / "delta-ref"
STRICT = ["-std=c99", "-pedantic", "-Wall", "-Wextra", "-Werror"]
SANITIZERS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def goldens():
    text = (KIT / "golden.h").read_text()
    return {name: bytes(int(byte, 16) for byte in re.findall(r"0x([0-9a-f]{2})", body))
            for name, body in re.findall(r"static const uint8_t (\w+)\[\d+\] = \{(.*?)\};", text, re.S)}


def test_builds_alone_and_passes(tmp_path):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    # (the library alone, with conversions checked too: what a port compiles)
    result = subprocess.run([cc, *STRICT, "-Wconversion", "-c", str(KIT / "delta_ref.c"), "-o",
                             str(tmp_path / "delta_ref.o")], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    binary = tmp_path / "delta_ref_test"
    sources = [str(KIT / "delta_ref.c"), str(KIT / "delta_ref_test.c")]
    result = subprocess.run([cc, *STRICT, "-O1", "-g", *SANITIZERS, *sources, "-o", str(binary)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        result = subprocess.run([cc, *STRICT, "-O1", *sources, "-o", str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    run = subprocess.run([str(binary), "200000"], capture_output=True, text=True, timeout=600)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "delta_ref_test: ok" in run.stdout


def test_library_makes_no_calls():
    """no allocation, threads, clock or OS calls: the library includes
    stdint.h and stddef.h alone"""
    for name in ("delta_ref.c", "delta_ref.h"):
        included = re.findall(r"#include [<\"]([^>\"]+)[>\"]", (KIT / name).read_text())
        assert set(included) <= {"stdint.h", "stddef.h", "delta_ref.h"}, (name, included)


def test_goldens_are_the_games_bytes():
    if not compiler():
        pytest.skip("no C compiler")
    run = subprocess.run([sys.executable, str(ROOT / "tools" / "delta_ref_capture" / "capture.py"), "golden", "--check"],
                         capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr


def test_handshake_with_the_real_host():
    if not compiler():
        pytest.skip("no C compiler")
    run = subprocess.run([sys.executable, str(ROOT / "tools" / "delta_ref_capture" / "capture.py"), "interop"], capture_output=True,
                         text=True, timeout=600)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "interop_test: ok" in run.stdout


def conformance():
    spec = importlib.util.spec_from_file_location("conformance", KIT / "conformance.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_conformance_check(tmp_path):
    packets = goldens()
    good = tmp_path / "port.txt"
    good.write_text(packets["golden_hello_port"].hex() + "\n# leaving\n" + packets["golden_bye"].hex() + "\n")
    run = subprocess.run([sys.executable, str(KIT / "conformance.py"), "check", str(good)], capture_output=True,
                         text=True)
    assert run.returncode == 0, run.stdout
    # (a host's message, a bad length, a list-only capability: each fails)
    hello = bytearray(packets["golden_hello_port"])
    hello[13] |= 0x10  # bit 12, ping
    bad = tmp_path / "bad.txt"
    bad.write_text("\n".join([packets["golden_welcome"].hex(), packets["golden_bye"].hex() + "00", hello.hex()]))
    run = subprocess.run([sys.executable, str(KIT / "conformance.py"), "check", str(bad)], capture_output=True,
                         text=True)
    assert run.returncode == 1
    assert run.stdout.count("FAIL  ") == 3, run.stdout
    # the advertisement: OpenCE's fields, no tag of a port's own
    run = subprocess.run([sys.executable, str(KIT / "conformance.py"), "advert", "2c0001" + "00" * 23],
                         capture_output=True, text=True)
    assert run.returncode == 0, run.stdout
    run = subprocess.run([sys.executable, str(KIT / "conformance.py"), "advert", packets["golden_advert_tag"].hex()],
                         capture_output=True, text=True)
    assert run.returncode == 1


def test_conformance_port_host(tmp_path):
    """a port host's tag (layout 2) passes advert; projects 0, 1 and one not
    in the registry, a wrong check and a set reserved byte fail; check --host
    passes our host's own answers; listen --project writes the same tag"""
    module = conformance()
    example = bytes.fromhex("2c00010000000000000000000000000000444c010202030000af")
    assert example[:17] + module.project_tag(2, 3) == example

    def advert(data):
        return subprocess.run([sys.executable, str(KIT / "conformance.py"), "advert", data.hex()], capture_output=True,
                              text=True)

    run = advert(example)
    assert run.returncode == 0 and "Content Evolved" in run.stdout, run.stdout
    for project in (0, 1, 250):
        assert advert(example[:17] + module.project_tag(project, 3)).returncode == 1
    broken = bytearray(example)
    broken[25] ^= 1
    assert advert(bytes(broken)).returncode == 1
    spare = bytearray(example)
    spare[24] = 1
    spare[25] ^= 1
    assert advert(bytes(spare)).returncode == 1
    # (a Content Evolved modded game's bytes are its own)
    assert advert(bytes.fromhex("70ff2d00") + bytes(range(22))).returncode == 0
    packets = goldens()
    sent = tmp_path / "host.txt"
    sent.write_text("\n".join(packets[name].hex() for name in ("golden_welcome", "golden_roster",
                                                              "golden_legacy_major2", "golden_bye")) + "\n")
    run = subprocess.run([sys.executable, str(KIT / "conformance.py"), "check", "--host", str(sent)],
                         capture_output=True, text=True)
    assert run.returncode == 0, run.stdout
    # (a WELCOME that names our build fails)
    welcome = module.welcome(0x1234ABCD, 0xFFFFFFFF, 44, b"chupa-44b", 0)
    sent.write_text(welcome.hex() + "\n")
    run = subprocess.run([sys.executable, str(KIT / "conformance.py"), "check", "--host", str(sent)],
                         capture_output=True, text=True)
    assert run.returncode == 1 and run.stdout.count("FAIL  ") == 1, run.stdout
    # (and a client's HELLO isn't a host's to send)
    sent.write_text(packets["golden_hello_port"].hex() + "\n")
    run = subprocess.run([sys.executable, str(KIT / "conformance.py"), "check", "--host", str(sent)],
                         capture_output=True, text=True)
    assert run.returncode == 1
    run = subprocess.run([sys.executable, str(KIT / "conformance.py"), "listen", "--project", "2", "--project-version",
                          "3", "--seconds", "0", "--port", "0"], capture_output=True, text=True, timeout=60)
    assert example.hex() in run.stdout, run.stdout


def test_conformance_host_answers_read_right():
    """the stand-in host's WELCOME and ROSTER have the game's layouts"""
    module = conformance()
    welcome = module.welcome(0x1234ABCD, 0xFFFFFFFF, 44)
    assert welcome[:4] == b"DP\x01\x02" and struct.unpack_from("<H", welcome, 4)[0] == len(welcome) - 12
    capabilities, agreed, legacy, machine, length = struct.unpack_from("<IIHBB", welcome, 12)
    assert agreed == 1 and legacy == 44 and machine == 0xFF and len(welcome) == 12 + 24 + length + 4
    key = bytes([17, 1, 0, 0, 8, 16, 3, 0])
    roster = module.roster(0x1234ABCD, 2, 1, key)
    assert struct.unpack_from("<H", roster, 4)[0] == len(roster) - 12 == 12 + 36
    room, players, count = struct.unpack_from("<IB3xB", roster, 12)
    assert room == 1 and players == 8 and count == 1
    assert roster[24] == 2 and roster[25] == 3 and roster[32:40] == key


def test_loopback_host_keeps_to_this_machine():
    """the loopback host's settings: every one that reaches past this
    machine is off (the script is run by hand against a real server)"""
    text = (KIT / "loopback_host.sh").read_text()
    for setting in ('address="127.0.0.1"', 'HALO_NET_ADDRESS="$address"', 'HALO_NET_BROADCAST="$broadcast"',
                    "127.*) ;;", "HALO_NET_ONLINE=false", "HALO_LEGACY_TABLE=", 'HALO_NET_MAPS_URL="http://127.0.0.1:9"',
                    "HALO_NET_ALLOW_UPNP=false", "HALO_NET_LIST_GAMES=false", "HALO_NET_REPORT_HOSTED=false",
                    "HALO_NET_PUBLIC_LOBBY=false", "HALO_NET_HOST_PUBLIC=false", "HALO_DEDICATED_PUBLIC=false",
                    "HALO_DEDICATED_LINK=false", "HALO_DEDICATED_GAMESPY_LISTING=false", "HALO_DEDICATED_CONTROL=off",
                    "HALO_NET_MAP_SHARE=false", 'HALO_NET_BROWSER="http://127.0.0.1:9"', "env -i"):
        assert setting in text, setting
    if shutil.which("bash"):
        assert subprocess.run(["bash", "-n", str(KIT / "loopback_host.sh")]).returncode == 0
