"""Checks how a Custom Edition or HaloMD map's effect flags are read
(port/linux/game/ce_effect_flags.h, ce_repairs.c). Gearbox's layout keeps
"must be deterministic" in bit 2 and its own "required for gameplay" in bit
1; the Xbox's keeps "must be deterministic" in bit 1
(source/effects/effects.c). The header is compiled alone with the build
machine's C compiler."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "port" / "linux" / "game" / "ce_effect_flags.h"

HARNESS = r"""
#include <stdio.h>
#include "ce_effect_flags.h"
int main(void)
{
	unsigned long flags;
	for (flags = 0; flags < 16; flags++)
	{
		unsigned long once = ce_effect_flags_from_gearbox(flags);
		printf("%lu %lu %lu\n", flags, once, ce_effect_flags_from_gearbox(once));
	}
	return 0;
}
"""

# Gearbox's bits (Halo PC's tools; Invader's effect.json lists the same)
GEARBOX_DELETED, GEARBOX_REQUIRED, GEARBOX_DETERMINISTIC, GEARBOX_BLOOD = 1, 2, 4, 8
# the Xbox's
XBOX_DELETED, XBOX_DETERMINISTIC = 1, 2


@pytest.fixture(scope="module")
def table(tmp_path_factory):
    cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    directory = tmp_path_factory.mktemp("ce_effect_flags")
    source = directory / "harness.c"
    source.write_text(HARNESS)
    binary = directory / "harness"
    subprocess.run([cc, "-std=c99", "-Wall", "-Werror", "-I", str(HEADER.parent), str(source), "-o", str(binary)],
                   check=True)
    output = subprocess.run([str(binary)], capture_output=True, text=True, check=True).stdout
    return {int(a): (int(b), int(c)) for a, b, c in (line.split() for line in output.splitlines())}


def test_deterministic_moves_to_the_xbox_bit(table):
    for flags, (once, _) in table.items():
        assert bool(once & XBOX_DETERMINISTIC) == bool(flags & GEARBOX_DETERMINISTIC), flags


def test_deleted_when_inactive_kept(table):
    for flags, (once, _) in table.items():
        assert bool(once & XBOX_DELETED) == bool(flags & GEARBOX_DELETED), flags


def test_required_for_gameplay_alone_is_not_deterministic(table):
    # (Halo PC's engine never reads bit 1)
    assert table[GEARBOX_REQUIRED][0] & XBOX_DETERMINISTIC == 0
    assert table[GEARBOX_REQUIRED | GEARBOX_DELETED][0] == XBOX_DELETED


def test_coldsnap_burning_effect(table):
    # coldsnap_20th_xl_rc5's burning_flame effect: flags 4, with a damage part
    assert table[4][0] & XBOX_DETERMINISTIC


def test_reading_twice_reads_as_once(table):
    for flags, (once, twice) in table.items():
        assert once == twice, flags


def test_xbox_layout_matches_effects_c():
    text = (ROOT / "source" / "effects" / "effects.c").read_text()
    enum = re.search(r"enum effect_definition_flags\s*\{(.*?)\}", text, re.S).group(1)
    names = re.findall(r"(_effect_definition_\w+_bit)", enum)
    assert names[:2] == ["_effect_definition_deleted_when_inactive_bit",
                         "_effect_definition_must_be_deterministic_bit"]


def test_repairs_read_every_effect():
    text = (ROOT / "port" / "linux" / "game" / "ce_repairs.c").read_text()
    assert re.search(r"group_tag == 'effe'\)\s*ce_effect_flags_repair\(", text)
