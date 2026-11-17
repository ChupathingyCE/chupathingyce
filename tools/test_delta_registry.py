# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
"""Delta's registry (docs/delta/registry.json and registry.md) agrees with
the game's code: the platform numbers, keys, tags and names of delta.h, the
capability bits, the wire IDs, the console floor, the network versions our
clients join, the Delta major and port. Other projects test against
registry.json, so it must never drift from what the game sends. The other
lists of platform keys in this repository may name only registered keys."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DELTA_H = (ROOT / "port" / "linux" / "include" / "delta.h").read_text()
LIMITS_H = (ROOT / "port" / "linux" / "include" / "halo_port_limits.h").read_text()
REGISTRY = json.loads((ROOT / "docs" / "delta" / "registry.json").read_text())
REGISTRY_MD = (ROOT / "docs" / "delta" / "registry.md").read_text()


def enum(prefix):
    return {name: int(value) for name, value in re.findall(rf"\b{prefix}(\w+) = (\d+),", DELTA_H)}


def test_platforms_match_delta_h():
    numbers = enum("_delta_platform_")
    names = re.findall(r'X\(_delta_platform_(\w+), "(\w+)", "([^"]*)", "([^"]*)", "([^"]*)"\)', DELTA_H)
    assigned = [platform for platform in REGISTRY["platforms"] if platform["key"]]
    assert len(assigned) == len(numbers) == len(names)
    for (symbol, key, tag, name, _), platform in zip(names, assigned):
        assert platform["number"] == numbers[symbol], key
        assert (platform["key"], platform["tag"], platform["name"]) == (key, tag, name)
    assert [platform["number"] for platform in REGISTRY["platforms"]] == list(range(len(REGISTRY["platforms"])))
    for platform in REGISTRY["platforms"]:
        assert platform["status"]
        assert platform["class"] in (None, "pc", "console", "handheld")
        if platform["key"]:
            assert f"| {platform['number']} | `{platform['key']}` |" in REGISTRY_MD


def test_capabilities_match_delta_h():
    bits = enum("_delta_capability_")
    assert {capability["name"]: capability["bit"] for capability in REGISTRY["capabilities"]} == bits
    never = {capability["bit"] for capability in REGISTRY["capabilities"] if capability["ports_never_send"]}
    assert never == {5, 9, 12, 18}
    for capability in REGISTRY["capabilities"]:
        assert f"| {capability['bit']} | `{capability['name']}` |" in REGISTRY_MD


def test_wires_match_delta_h():
    wires = REGISTRY["wires"]
    assert wires["current"] == re.search(r'#define DELTA_WIRE "([^"]+)"', DELTA_H).group(1)
    assert wires["console_floor"] == re.search(r'#define DELTA_WIRE_CONSOLE_FLOOR "([^"]+)"', DELTA_H).group(1)
    assert wires["first_advertised"] == re.search(r'#define DELTA_WIRE_FIRST_ADVERTISED "([^"]+)"', DELTA_H).group(1)
    listed = {wire["wire"]: wire for wire in wires["list"]}
    for wire, oldest in re.findall(r'X\("(chupa-\w+)", "(chupa-\w+)"\)', DELTA_H):
        assert listed[wire]["oldest_compatible"] == oldest, wire
    for wire, newest in re.findall(r'X\("(chupa-\w+)", (\d+)\)', DELTA_H):
        assert listed[wire]["newest_network_version"] == int(newest), wire
    assert wires["current"] in listed and wires["console_floor"] in listed


def test_numbers_match_the_code():
    minimum = int(re.search(r"HALO_PORT_NETWORK_VERSION_MINIMUM (\d+)", LIMITS_H).group(1))
    maximum = int(re.search(r"HALO_PORT_NETWORK_VERSION_MAXIMUM (\d+)", LIMITS_H).group(1))
    assert REGISTRY["network_versions"]["joined_by_our_clients"] == [minimum, maximum]
    assert REGISTRY["delta_major"] == int(re.search(r"#define DELTA_MAJOR (\d+)", DELTA_H).group(1))
    offset = int(re.search(r"#define DELTA_PEER_PORT_OFFSET (\d+)", DELTA_H).group(1))
    assert REGISTRY["ports"]["delta_peer"] == REGISTRY["ports"]["game"] + offset == 0x141E + offset
    closed = (ROOT / "port" / "linux" / "src" / "game_list_rules.h").read_text()
    assert REGISTRY["network_versions"]["closed_bit"]["value"] == 0x8000 and "0x8000" in closed


def test_other_platform_lists_use_registered_keys():
    keys = {platform["key"] for platform in REGISTRY["platforms"] if platform["key"]}
    # conformance.py reads its platform list from registry.json itself
    conformance = (ROOT / "tools" / "delta-ref" / "conformance.py").read_text()
    assert "registry.json" in conformance and "PLATFORMS = [" not in conformance
    icons = (ROOT / "tools" / "platform_icons_font.py").read_text()
    for key, numbers in re.findall(r'"(\w+)": \(([\d, ]+)\)', icons):
        if key != "playstation":
            assert key in keys, key
    webui = (ROOT / "server" / "webui" / "app.js").read_text()
    devices = webui[webui.index("const DEVICES = {"):webui.index("};", webui.index("const DEVICES = {"))]
    for key in re.findall(r"(\w+): '", devices):
        assert key in keys, key
    reference = (ROOT / "tools" / "delta-ref" / "delta_ref.h").read_text()
    numbers = {platform["key"]: platform["number"] for platform in REGISTRY["platforms"] if platform["key"]}
    for name, number in re.findall(r"DREF_PLATFORM_(\w+) = (\d+)", reference):
        assert numbers[name.lower()] == int(number), name
