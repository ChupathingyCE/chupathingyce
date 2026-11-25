# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
"""The layout facts by network version (docs/delta/versioning.md, "Layout
facts by network version", and docs/delta/layouts.json) held to the game's
own code: each version's layout from 11 to 45 as network_rules.c builds it,
the record's fixed fields as halo_port_limits.h and network_game_layout.h
place them, the further options' defaults and ranges, Content Evolved's row,
and the reference kit's delta_layout.c (facts and converter) against
network_rules.c's fold and copy, byte for byte. The kit's own tests and
conformance.py's layout commands run here too."""
import importlib.util
import json
import random
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
KIT = ROOT / "tools" / "delta-ref"
LAYOUTS = ROOT / "docs" / "delta" / "layouts.json"
RULES_C = ROOT / "port" / "linux" / "game" / "network_rules.c"
LIMITS_H = ROOT / "port" / "linux" / "include" / "halo_port_limits.h"
GAME_LAYOUT_H = ROOT / "port" / "linux" / "game" / "network_game_layout.h"
AT = {1: "after_up", 2: "last"}

# (a line a version: the layout's facts, its dropped kinds, its unit parts;
# LAYOUT is network_rules.h's or delta_layout.h's, by -D)
DUMP = r"""
#include <stdio.h>
#ifdef GAME
#include "network_rules.h"
#define BUILT_IN(v, l) network_rules_layout_built_in(v, l)
#define DROPS(l, k) network_rules_layout_drops(l, k)
typedef struct network_rules_layout layout_t;
#else
#include "delta_layout.h"
#define BUILT_IN(v, l) delta_layout_built_in(v, l)
#define DROPS(l, k) delta_layout_drops(l, k)
typedef struct delta_layout layout_t;
#endif
int main(void)
{
	unsigned v;
	for (v = 11; v <= 45; v++) {
		layout_t l; int k, b;
		BUILT_IN(v, &l);
		printf("%u %d %d %d %d %d %d %d %d |", v, (int)l.settings_size, (int)l.options_size,
			(int)l.shield_effects_maximum, (int)l.join_request_extra, (int)l.join_request_version_at,
			(int)l.header_extra, (int)l.epoch_kind, (int)l.rider_ejection_up);
		for (k = 1; k < 256; k++) if (DROPS(&l, (unsigned)k)) printf(" %d", k);
		printf(" |");
		for (b = 0; b < 8; b++) if (l.unit_part_size[b]) printf(" %d:%d:%d", b, l.unit_part_size[b], l.unit_part_at[b]);
		printf("\n");
	}
	return 0;
}
"""

# (records through both: network_rules.c's fold and copy, delta_layout.c's
# convert; a seed on the command line)
COMPARE = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network_rules.h"
#include "delta_layout.h"
static unsigned char record[20000], ours[20000], theirs[20000], copy[20000];
static unsigned state;
static unsigned char next(void) { state = state * 1103515245u + 12345u; return (unsigned char)(state >> 16); }
int main(int argc, char **argv)
{
	/* (from the longer layouts to 34's, as a client folds them) */
	static const int sizes[][2] = { { 13136, 0x2C }, { 13200, 0x2C }, { 13244, 0x58 }, { 13180, 0x3C } };
	unsigned char rules_extended[16], ref_extended[16];
	int failures = 0, round, index;
	state = argc > 1 ? (unsigned)atoi(argv[1]) : 1u;
	for (round = 0; round < 200; round++) {
		int size = sizes[round % 4][0], options = sizes[round % 4][1];
		for (index = 0; index < size; index++) record[index] = next();
		memcpy(ours, record, (size_t)size);
		network_rules_settings_fold(ours, size, options, rules_extended);
		network_rules_extended_options_sanitize(rules_extended);
		if (delta_layout_convert(record, (size_t)size, (unsigned)options, theirs, 13120, 0x1C, ref_extended) ||
			memcmp(ours, theirs, 13120) || memcmp(rules_extended, ref_extended, 16)) {
			printf("fold %d/%d differs\n", size, options);
			failures++;
		}
		/* (and a 34 record made a longer layout, as a host does, its
		further options the defaults) */
		network_rules_extended_options_default(rules_extended);
		network_rules_settings_layout_copy(record, rules_extended, options, 0, size, copy);
		if (delta_layout_convert(record, 13120, 0x1C, theirs, (size_t)size, (unsigned)options, NULL) ||
			memcmp(copy, theirs, (size_t)size)) {
			printf("copy %d/%d differs\n", size, options);
			failures++;
		}
	}
	printf("%s\n", failures ? "failed" : "ok");
	return failures != 0;
}
"""


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def layouts():
    return json.loads(LAYOUTS.read_text())


def build(tmp_path, name, source, *files, defines=()):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    (tmp_path / f"{name}.c").write_text(source)
    binary = tmp_path / name
    result = subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", *defines, "-I", str(RULES_C.parent),
                             "-I", str(KIT), str(tmp_path / f"{name}.c"), *map(str, files), "-o", str(binary)],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return binary


def dumped(binary):
    facts = {}
    for line in subprocess.run([str(binary)], capture_output=True, text=True, check=True).stdout.splitlines():
        head, drops, parts = line.split("|")
        v, size, options, shields, extra, at, header, epoch, ride = (int(x) for x in head.split())
        facts[str(v)] = {
            "settings_size": size, "options_offset": 0x3320, "options_size": options,
            "local_data_offset": 0x3320 + options, "trailing_size": size - 0x3320 - options - 4,
            "shield_effects_maximum": shields, "dropped_messages": [int(x) for x in drops.split()],
            "join_request_extra": extra, "join_request_version_at": at if at >= 0 else None,
            "header_extra": header, "epoch_kind": epoch,
            "unit_state_parts": [{"bit": int(b), "size": int(n), "at": AT[int(w)]}
                                 for b, n, w in (p.split(":") for p in parts.split())],
            "rider_ejection_up": ride}
    return facts


def json_facts():
    data = layouts()
    keys = next(iter(dumped_keys()))
    return {version: {key: entry[key] for key in keys} for version, entry in data["versions"].items()}


def dumped_keys():
    yield ("settings_size", "options_offset", "options_size", "local_data_offset", "trailing_size",
           "shield_effects_maximum", "dropped_messages", "join_request_extra", "join_request_version_at",
           "header_extra", "epoch_kind", "unit_state_parts", "rider_ejection_up")


def test_versions_are_the_games(tmp_path):
    """every version from 11 to 45 as network_rules.c builds it in"""
    binary = build(tmp_path, "dump_game", DUMP, RULES_C, defines=["-DGAME"])
    data = layouts()
    assert sorted(map(int, data["versions"])) == list(range(11, 46))
    assert json_facts() == dumped(binary)
    text = (ROOT / "port" / "linux" / "include" / "delta.h").read_text()
    newest = int(re.search(r"#define DELTA_LEGACY_NEWEST (\d+)", text).group(1))
    presumed = int(re.search(r"#define DELTA_LEGACY_PRESUMED (\d+)", text).group(1))
    limits = LIMITS_H.read_text()
    oldest = int(re.search(r"#define HALO_PORT_NETWORK_VERSION_MINIMUM (\d+)", limits).group(1))
    assert (data["oldest"], data["newest"], data["presumed"]) == (oldest, newest, newest + presumed)


def test_kit_facts_are_the_games(tmp_path):
    """the reference kit's built-in facts are network_rules.c's"""
    game = dumped(build(tmp_path, "dump_game", DUMP, RULES_C, defines=["-DGAME"]))
    kit = dumped(build(tmp_path, "dump_kit", DUMP, KIT / "delta_layout.c"))
    assert kit == game


def test_record_fields_are_the_games():
    """the fixed fields where halo_port_limits.h and network_game_layout.h put
    them, and the options and local data after the players"""
    fields = {field["name"]: field for field in layouts()["record"]["fields"]}
    limits = LIMITS_H.read_text()
    game = GAME_LAYOUT_H.read_text()

    def number(text, name):
        return int(re.search(rf"#define {name} (0x[0-9A-Fa-f]+|\d+)", text).group(1), 0)
    machine = number(game, "NETWORK_GAME_LAYOUT_MACHINE_SIZE")
    player = number(game, "NETWORK_GAME_LAYOUT_PLAYER_SIZE")
    assert fields["machines"]["offset"] == number(limits, "HALO_PORT_NETWORK_GAME_MACHINES_OFFSET") == \
        number(game, "NETWORK_GAME_LAYOUT_MACHINES_OFFSET")
    assert fields["machines"]["element_size"] == machine and fields["players"]["element_size"] == player
    assert fields["maximum_players"]["offset"] == number(game, "NETWORK_GAME_LAYOUT_MAXIMUM_PLAYERS_OFFSET")
    assert fields["machine_count"]["offset"] == number(game, "NETWORK_GAME_LAYOUT_MACHINE_COUNT_OFFSET")
    player_count = fields["machines"]["offset"] + 128 * machine
    assert fields["player_count"]["offset"] == player_count
    assert fields["players"]["offset"] == player_count + 2
    players_end = player_count + 2 + 128 * player
    # (halo_port_limits.h: the seed 2 past the players, the options 0xA, the
    # local data 0x26, the record 0x2A)
    assert re.search(r"RANDOM_SEED_OFFSET \(HALO_PORT_NETWORK_GAME_PLAYERS_END \+ 2\)", limits)
    assert re.search(r"VARIANT_OPTIONS_OFFSET \(HALO_PORT_NETWORK_GAME_PLAYERS_END \+ 0xA\)", limits)
    assert re.search(r"LOCAL_DATA_OFFSET \(HALO_PORT_NETWORK_GAME_PLAYERS_END \+ 0x26\)", limits)
    assert re.search(r"NETWORK_GAME_SIZE \(HALO_PORT_NETWORK_GAME_PLAYERS_END \+ 0x2A\)", limits)
    assert fields["random_seed"]["offset"] == players_end + 2
    assert layouts()["record"]["head_size"] == players_end + 0xA == 0x3320
    assert players_end + 0x26 == 0x3320 + 0x1C and players_end + 0x2A == 13120
    # (the fields tile the head with nothing between them)
    at = 0
    for field in layouts()["record"]["fields"]:
        assert field["offset"] == at, field["name"]
        at += field["size"]
    assert at == 0x3320
    # (the header's, as the client's asserts have them)
    handler = (ROOT / "source" / "networking" / "network_client_message_handler.c").read_text()
    for name in ("cooperative_flags", "maximum_players", "machine_count", "machines"):
        offset = re.search(rf"offsetof\(struct network_game, {name}\) == (0x[0-9A-F]+)", handler).group(1)
        assert fields[name]["offset"] == int(offset, 16), name


def test_options_and_projects_are_the_games():
    """the further options' defaults and ranges are network_rules.c's, and
    Content Evolved's row is network_rules.c's"""
    text = RULES_C.read_text()
    record = layouts()["record"]
    names = {"mode": "MODE", "hardcore": "HARDCORE", "first_zombies": "FIRST_ZOMBIES", "zombie_speed": "ZOMBIE_SPEED",
             "skull_damage": "SKULL_DAMAGE", "zombie_respawn": "ZOMBIE_RESPAWN", "countdown": "COUNTDOWN",
             "zombie_shields": "ZOMBIE_SHIELDS", "weapon_order": "WEAPON_ORDER", "melee_demotes": "MELEE_DEMOTES"}
    defaults = dict(re.findall(r"options\[NETWORK_RULES_OPTION_(\w+)\] = (\d+);", text))
    ranges = {name: (int(least), int(most), int(default)) for name, least, most, default in
              re.findall(r"ranged\(options\[NETWORK_RULES_OPTION_(\w+)\], (\d+), (\d+), (\d+)\)", text)}
    for byte in record["extended_options"]["bytes"]:
        if byte["name"] == "reserved":
            continue
        name = names[byte["name"]]
        assert byte["default"] == int(defaults.get(name, 0)), byte["name"]
        if name in ranges:
            assert tuple(byte["range"]) == ranges[name][:2] and byte["default"] == ranges[name][2], byte["name"]
    row = layouts()["projects"]["content_evolved"]
    found = re.search(r'"Content Evolved",\s*0xFF00, 0xFF00, 24,\s*(\d+), (0x[0-9A-F]+),\s*(\d+), (\d+),', text)
    assert found, "network_rules.c's Content Evolved row moved"
    version, options, first, last = int(found.group(1)), int(found.group(2), 16), int(found.group(3)), int(found.group(4))
    assert (row["version"], row["options_size"], row["dropped_messages_first"], row["dropped_messages_last"]) == \
        (version, options, first, last)
    base = layouts()["versions"][str(version)]
    assert row["settings_size"] == base["settings_size"] + options - base["options_size"]


def test_kit_converter_is_the_games(tmp_path):
    """delta_layout.c's convert against network_rules.c's fold (a client's)
    and copy (a host's), on random records of every layout"""
    binary = build(tmp_path, "compare", COMPARE, RULES_C, KIT / "delta_layout.c")
    for seed in ("1", "1124", "44"):
        result = subprocess.run([str(binary), seed], capture_output=True, text=True)
        assert result.returncode == 0, result.stdout


def test_kit_layout_tests(tmp_path):
    """the kit's own layout tests, strict C99 under the sanitizers where the
    compiler has them"""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "delta_layout_test"
    strict = ["-std=c99", "-pedantic", "-Wall", "-Wextra", "-Werror"]
    result = subprocess.run([cc, *strict, "-Wconversion", "-c", str(KIT / "delta_layout.c"), "-o",
                             str(tmp_path / "delta_layout.o")], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    sources = [str(KIT / "delta_layout.c"), str(KIT / "fuzz_layout.c"), str(KIT / "delta_layout_test.c")]
    result = subprocess.run([cc, *strict, "-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                             *sources, "-o", str(binary)], capture_output=True, text=True)
    if result.returncode:
        result = subprocess.run([cc, *strict, "-O1", *sources, "-o", str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run.returncode == 0 and "ok" in run.stdout, run.stdout


def conformance():
    spec = importlib.util.spec_from_file_location("conformance", KIT / "conformance.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_conformance_layouts(tmp_path):
    """conformance.py reads layouts.json, applies a table's facts within the
    limits, and converts as delta_layout.c does"""
    module = conformance()
    data = module.load_layouts()
    assert module.layout_of(data, 10) is None
    assert module.layout_of(data, 32)["settings_size"] == 13120
    assert module.layout_of(data, 45)["settings_size"] == 13200
    assert module.layout_of(data, 46) is None
    grown = module.layout_of(data, 46, [(44, {"options_size": 64}), (46, {"options_size": 48, "rider_ejection_up": 1000})])
    assert grown["options_size"] == 48 and grown["settings_size"] == 13204 and grown["refused"] == 1
    wrapped = module.layout_of(data, 45, [(45, {"options_size": 0xFFFFFFFF, "join_request_version_at": 0xFFFFFFFE})])
    assert wrapped["options_size"] == 0x2C and wrapped["join_request_version_at"] == 234 and wrapped["refused"] == 2
    rng = random.Random(1124)
    record = bytes(rng.randrange(256) for _ in range(13120))
    out, extended = module.convert_record(record, 0x1C, 13200, 0x2C)
    assert len(out) == 13200 and out[:0x3320 + 0x1C] == record[:0x3320 + 0x1C]
    assert out[13132:13136] == record[13116:13120] and out[13136:] == bytes(64)
    assert out[0x3320 + 0x1C:0x3320 + 0x2C] == module.EXTENDED_DEFAULTS == extended
    source, target = tmp_path / "32.bin", tmp_path / "44.bin"
    source.write_bytes(bytes(13120))
    run = subprocess.run(["python3", str(KIT / "conformance.py"), "convert", "32", "44", str(source), str(target)],
                         capture_output=True, text=True)
    assert run.returncode == 0, run.stdout
    run = subprocess.run(["python3", str(KIT / "conformance.py"), "record", "44", str(target)],
                         capture_output=True, text=True)
    assert run.returncode == 0, run.stdout
    run = subprocess.run(["python3", str(KIT / "conformance.py"), "record", "41", str(target)],
                         capture_output=True, text=True)
    assert run.returncode == 1, run.stdout


def test_spec_names_the_sections():
    """the page's sections, and every version's row in "Version by version" """
    text = (ROOT / "docs" / "delta" / "versioning.md").read_text()
    for heading in ("## Layout facts by network version", "### The game settings record", "### Version by version",
                    "### Other projects' rows", "### layouts.json", "## Playing a range", "### Size and encoding"):
        assert f"\n{heading}\n" in text, heading
    section = text.split("### Version by version")[1].split("### Other projects' rows")[0]
    named = set()
    for first, last in re.findall(r"^\| (\d+)(?: to (\d+))? \|", section, re.M):
        named.update(range(int(first), int(last or first) + 1))
    assert named == set(range(11, 46))
    assert "16,384" in text.split("### Size and encoding")[1].split("### Layouts")[0]
    table = (ROOT / "tools" / "delta_table.py").read_text()
    assert 'separators=(",", ":")' in table and "DOCUMENT_SIZE = 16384" in table
