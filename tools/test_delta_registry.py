# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
"""Delta's registry (docs/delta/registry.json and registry.md) agrees with
the game's code: the platform numbers, keys, tags and names of delta.h, the
capability bits, the wire IDs, the console floor, the network versions our
clients join, the Delta major and port, and every other section of it.
Other projects test against registry.json, so it must never drift from what
the game sends. The files are copies of ChupathingyCE/delta-spec's: with
DELTA_SPEC set to a checkout of it, they must be the same bytes. The other
lists of platform keys in this repository may name only registered keys."""
import json
import os
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


def test_projects_match_delta_h():
    """the projects a layout 2 tag names (DELTA_PROJECTS), and the tag's
    layouts (DELTA_ADVERTISED_TAG_VERSION and _PROJECT)"""
    projects = REGISTRY["projects"]
    rows = projects["list"]
    assert [row["number"] for row in rows] == list(range(len(rows)))
    named = re.findall(r'X\(_delta_project_(\w+), (\d+), "(\w+)", "([^"]+)"\)', DELTA_H)
    assert [(int(number), key, name) for _, number, key, name in named] == \
        [(row["number"], row["key"], row["name"]) for row in rows if row["number"]]
    assert all(symbol == key for symbol, _, key, _ in named)
    assert rows[0]["key"] == "none" and not rows[0]["name"]
    assert int(re.search(r"#define DELTA_PROJECT_OURS (\d+)", DELTA_H).group(1)) == 1 == \
        [row["number"] for row in rows if row["key"] == "chupathingy"][0]
    layouts = {row["name"]: row["layout"] for row in projects["tag_layouts"]}
    assert layouts == {"wire": int(re.search(r"#define DELTA_ADVERTISED_TAG_VERSION (\d+)", DELTA_H).group(1)),
                       "project": int(re.search(r"#define DELTA_ADVERTISED_TAG_VERSION_PROJECT (\d+)", DELTA_H).group(1))}
    for row in rows:
        assert row["status"]
        assert f"| {row['number']} | `{row['key']}` |" in REGISTRY_MD, row
    for row in projects["tag_layouts"]:
        assert f"| {row['layout']} | `{row['name']}` |" in REGISTRY_MD, row
    reference = (ROOT / "tools" / "delta-ref" / "delta_ref.h").read_text()
    for name, number in re.findall(r"DREF_PROJECT_(\w+) = (\d+)", reference):
        if name != "NONE":
            assert [row["number"] for row in rows if row["key"] == name.lower()] == [int(number)], name


def test_platform_floors_match_the_code():
    floors = REGISTRY["platform_floors"]
    assert floors["section"] == "platform_floors"
    assert floors["rows_maximum"] == int(re.search(r"#define DELTA_PLATFORM_FLOORS_MAXIMUM (\d+)", DELTA_H).group(1))
    assert floors["consoles_without_a_row"] == REGISTRY["wires"]["console_floor"]
    keys = {platform["key"] for platform in REGISTRY["platforms"] if platform["key"] not in ("", "unknown")}
    section = REGISTRY_MD[REGISTRY_MD.index("## Platform floors"):REGISTRY_MD.index("## Network versions")]
    assert f"at most {floors['rows_maximum']} rows" in section and f"`{floors['consoles_without_a_row']}`" in section
    for row in floors["rows"]:
        assert row["platform"] in keys, row
        assert 11 <= row["network"] <= 65535 and row["since_table"] >= 1 and row["why"]
        assert f"| `{row['platform']}` | {row['network']} | {row['since_table']} |" in section, row
    assert ("None yet." in section) == (not floors["rows"])


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


def test_internet_listing_matches_p2p_lobby():
    lobby = (ROOT / "port" / "linux" / "src" / "p2p_lobby.c").read_text()
    flags = {int(value) for value in re.findall(r"\b_listing_\w+ = (\d+),", lobby)}
    listing = REGISTRY["internet_listing"]
    # (a flag "for" another project, such as Content Evolved's 64, isn't in our code)
    in_use = {flag["value"] for flag in listing["flags"] if flag["status"] == "in use" and "for" not in flag}
    assert in_use == flags
    for flag in listing["flags"]:
        assert flag["value"] in (1, 2, 4, 8, 16, 32, 64, 128) and flag["status"], flag
        assert (flag["name"] is None) == (flag["status"] != "in use"), flag
        assert f"| {flag['value']} |" in REGISTRY_MD, flag
    stamp = listing["stamp"]
    assert stamp["size"] == int(re.search(r"\bSTAMP_SIZE = (\d+),", lobby).group(1))
    assert [entry["byte"] for entry in stamp["bytes"]] == list(range(stamp["size"]))
    assert stamp["bytes"][0]["name"] == "kind"
    assert all(entry["name"] is None for entry in stamp["bytes"][1:] if entry.get("for") is None)
    for entry in stamp["bytes"]:
        assert (entry["name"] is None) == (entry["status"] != "in use"), entry


def test_advertisement_matches_the_code():
    advertisement = REGISTRY["advertisement"]
    assert advertisement["reserved_size"] == int(re.search(r"#define DELTA_ADVERTISED_RESERVED_SIZE (0x\w+)", DELTA_H).group(1), 16)
    stock = advertisement["stock"]
    assert stock["flags_byte"] == int(re.search(r"#define HALO_PORT_ADVERTISED_FLAGS_OFFSET (\d+)", LIMITS_H).group(1))
    ours = values(LIMITS_H, r"#define HALO_PORT_ADVERTISED_(DISTRIBUTED|IN_PROGRESS|DEDICATED)_FLAG (0x\w+)")
    # (0x08 and byte 4: the MCC network protocol's, from older GulchCE builds;
    # reserved, and no code of ours sets or reads them)
    flags = {flag["name"]: flag for flag in stock["flags"]}
    assert flags["mcc"]["value"] == 0x08 and flags["mcc"]["status"].startswith("reserved")
    assert flags["mcc"]["value"] not in ours.values()
    ours["mcc"] = flags["mcc"]["value"]
    assert {flag["name"]: flag["value"] for flag in stock["flags"]} == ours
    where = {entry["name"]: entry["bytes"] for entry in stock["bytes"] if entry["name"]}
    assert where["version"][0] == int(re.search(r"#define HALO_PORT_ADVERTISED_VERSION_OFFSET (\d+)", LIMITS_H).group(1))
    assert where["kind"] == [int(re.search(r"#define HALO_PORT_ADVERTISED_KIND_OFFSET (\d+)", LIMITS_H).group(1))]
    mcc_version = next(entry for entry in stock["bytes"] if entry["name"] == "mcc_version")
    assert mcc_version["bytes"] == [4] and mcc_version["status"].startswith("reserved")
    assert where["delta_tag"] == [advertisement["reserved_size"] - int(re.search(r"#define DELTA_ADVERTISED_TAG_SIZE (\d+)", DELTA_H).group(1)),
        advertisement["reserved_size"] - 1]
    assert advertisement["modded"]["versions"] == [0xFF00, 0xFFFF]
    assert REGISTRY["network_versions"]["modded_range"]["from"] == 0xFF00
    for flag in stock["flags"]:
        assert f"| {stock['flags_byte']} | 0x{flag['value']:02X} | `{flag['name']}` |" in REGISTRY_MD, flag
    assert "| 0xFF00 to 0xFFFF |" in REGISTRY_MD


# The rest of the registry, section by section, against the code: every
# section of registry.json is checked below or above, and every row of it is
# in registry.md, so the copy can't drift from the game or from the page.
WIRE_H = (ROOT / "port" / "linux" / "src" / "delta_wire.h").read_text()


def values(text, pattern):
    return {name.lower(): int(value, 0) for name, value in re.findall(pattern, text)}


def test_every_section_is_checked():
    checked = {"registry", "description", "platforms", "capabilities", "wires", "platform_floors",
        "projects", "network_versions", "ports", "xbox_title_id", "delta_major", "platform_policy", "advertisement",
        "platform_key_flags", "internet_listing", "messages", "map_families", "host_rules_flags", "game_modes", "permissions",
        "device_certificate_flags", "device_capabilities", "device_account_marks", "report_reasons", "moderation_actions",
        "labels"}
    assert set(REGISTRY) == checked


def test_platform_policy_matches_delta_h():
    rows = re.findall(r"X\(_delta_platform_(\w+), (\d+), (\d+), (\d+), (\d+), (\d)\)", DELTA_H)
    policy = REGISTRY["platform_policy"]["rows"]
    assert len(rows) == len(policy)
    numbers = {platform["key"]: platform["number"] for platform in REGISTRY["platforms"]}
    for (key, host, join, opt_in, memory, coop), row in zip(rows, policy):
        assert (row["key"], row["platform"]) == (key, numbers[key]), row
        assert (row["host_players"], row["join_players"], row["join_players_opt_in"], row["memory_class"],
            row["coop"]) == (int(host), int(join), int(opt_in), int(memory), coop == "1"), row


def test_platform_key_flags_match_delta_h():
    flags = values(DELTA_H, r"#define DELTA_PLATFORM_KEY_(OPTED_IN|DEDICATED) (0x\w+)")
    assert {flag["name"]: flag["value"] for flag in REGISTRY["platform_key_flags"]} == flags
    for flag in REGISTRY["platform_key_flags"]:
        assert f"| 0x{flag['value']:02X} | `{flag['name']}` |" in REGISTRY_MD, flag


def test_messages_match_delta_wire_h():
    types = values(WIRE_H, r"\b_delta_message_(\w+) = (\d+),?")
    assert {message["name"].lower(): message["type"] for message in REGISTRY["messages"]} == types
    for message in REGISTRY["messages"]:
        assert f"| {message['type']} | {message['name']} |" in REGISTRY_MD, message


def test_map_families_match_halo_map_families_h():
    families = (ROOT / "port" / "linux" / "include" / "halo_map_families.h").read_text()
    enum_body = families[families.index("_map_family_xbox"):families.index("NUMBER_OF_MAP_FAMILIES")]
    names = re.findall(r"_map_family_(\w+),", enum_body)
    assert [family["name"] for family in REGISTRY["map_families"]] == names
    assert [family["number"] for family in REGISTRY["map_families"]] == list(range(len(names)))
    for family in REGISTRY["map_families"]:
        assert f"| {family['number']} | `{family['name']}`" in REGISTRY_MD, family


def test_host_rules_flags_match_delta_wire_h():
    flags = values(WIRE_H, r"\bDELTA_WIRE_HOST_(\w+) = (0x\w+),?")
    known = flags.pop("known_flags")
    assert {flag["name"]: flag["value"] for flag in REGISTRY["host_rules_flags"]} == flags
    assert sum(flags.values()) == known
    for flag in REGISTRY["host_rules_flags"]:
        assert f"| 0x{flag['value']:02X} | `{flag['name']}` |" in REGISTRY_MD, flag


def test_game_modes_match_network_rules_h():
    rules = (ROOT / "port" / "linux" / "game" / "network_rules.h").read_text()
    modes = values(rules, r"\bNETWORK_RULES_MODES_(SWAT|HARDCORE|GUN_GAME|INFECTION) = (0x\w+)")
    assert {mode["name"]: 1 << mode["bit"] for mode in REGISTRY["game_modes"]} == modes
    for mode in REGISTRY["game_modes"]:
        assert f"| {mode['bit']} | 0x{1 << mode['bit']:02X} | {mode['mode']} |" in REGISTRY_MD, mode


def test_permissions_match_the_code():
    roles = (ROOT / "server" / "src" / "server_roles.h").read_text()
    server = values(roles, r"\bSERVER_PERMISSION_(\w+) = (0x\w+),")
    server.pop("all")
    wire = values(WIRE_H, r"\bDELTA_MODERATION_(\w+) = (0x\w+)")
    assert {permission["name"]: permission["value"] for permission in REGISTRY["permissions"]} == server
    assert {name: value for name, value in server.items() if value <= 0x100} == wire
    for permission in REGISTRY["permissions"]:
        assert f"| 0x{permission['value']:03X} | `{permission['name']}` |" in REGISTRY_MD, permission


def test_device_certificates_match_identity_h():
    identity = (ROOT / "port" / "linux" / "src" / "identity.h").read_text()
    flags = values(identity, r"\bIDENTITY_FLAG_(\w+) = (0x\w+),")
    can = values(identity, r"\bIDENTITY_CAN_(\w+) = (0x\w+),")
    assert {flag["name"]: flag["value"] for flag in REGISTRY["device_certificate_flags"]} == flags
    assert {capability["name"]: capability["value"] for capability in REGISTRY["device_capabilities"]} == can
    for row in REGISTRY["device_certificate_flags"] + REGISTRY["device_capabilities"]:
        assert f"| 0x{row['value']:02X} | `{row['name']}` |" in REGISTRY_MD, row
    # (account marks: the capabilities field's top byte, apart from the capabilities)
    marks = values(identity, r"\bIDENTITY_MARK_(\w+) = (0x\w+),")
    assert {mark["name"]: mark["value"] for mark in REGISTRY["device_account_marks"]} == marks
    bits = int(re.search(r"#define IDENTITY_CAPABILITY_BITS (0x\w+?)UL", identity).group(1), 16)
    for mark in REGISTRY["device_account_marks"]:
        assert mark["value"] & bits == 0 and mark["value"] & ~0xFFFFFFFF == 0, mark
        assert f"| 0x{mark['value']:08X} | `{mark['name']}` |" in REGISTRY_MD, mark
    assert all(capability["value"] & ~bits == 0 for capability in REGISTRY["device_capabilities"])


def test_report_reasons_and_moderation_actions_match_delta_wire_h():
    reasons = WIRE_H[WIRE_H.index("enum delta_report_reason"):WIRE_H.index("NUMBER_OF_DELTA_REPORT_REASONS")]
    assert {reason["name"]: reason["number"] for reason in REGISTRY["report_reasons"]} == \
        values(reasons, r"\b_delta_report_(\w+) = (\d+),")
    actions = values(WIRE_H, r"\b_delta_moderation_action_(\w+) = (\d+),")
    assert {action["name"]: action["number"] for action in REGISTRY["moderation_actions"]} == actions
    for row in REGISTRY["report_reasons"] + REGISTRY["moderation_actions"]:
        assert f"| {row['number']} | `{row['name']}` |" in REGISTRY_MD, row


def test_labels_are_in_the_code():
    sources = "".join(path.read_text(errors="replace") for folder in ("port/linux", "port/console", "server",
        "tools/delta-ref") for path in (ROOT / folder).rglob("*") if path.suffix in (".c", ".h", ".py"))
    for label in REGISTRY["labels"]["list"]:
        written = label["label"].replace("\n", "\\n")
        assert f'"{written}' in sources, label["label"]
        assert f"| `{written}` |" in REGISTRY_MD, label["label"]


def test_xbox_title_id_and_ports():
    assert REGISTRY["xbox_title_id"]["value"] == "0x43480001"
    p2p = (ROOT / "port" / "linux" / "src" / "p2p.c").read_text()
    share = (ROOT / "port" / "linux" / "game" / "map_share.h").read_text()
    ports = REGISTRY["ports"]
    assert ports["game"] == int(re.search(r"GAME_SERVER_PORT = (0x\w+),", p2p).group(1), 16)
    assert ports["system_link_search"] == int(re.search(r"GAME_CLIENT_PORT = (0x\w+),", p2p).group(1), 16)
    assert ports["map_share_tcp"] == int(re.search(r"MAP_SHARE_PORT = (\d+),", share).group(1))
    for name, number in (("server port", ports["game"]), ("search", ports["system_link_search"]),
            ("Delta Peer", ports["delta_peer"]), ("map sharing", ports["map_share_tcp"])):
        assert f"| {number} |" in REGISTRY_MD, name


def test_same_as_the_spec_checkout():
    spec = os.environ.get("DELTA_SPEC")
    if not spec:
        return
    for name in ("registry.json", "registry.md"):
        assert (Path(spec) / name).read_bytes() == (ROOT / "docs" / "delta" / name).read_bytes(), name
