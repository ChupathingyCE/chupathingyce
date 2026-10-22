"""Checks tools/compare_stock_maps.py on a Custom Edition map made to pass for
a Halo PC one: the same file with its cache version 7 must match, and the same
with a byte of a weapon's data changed must not (skipped without a Custom
Edition stock map in assets/maps/ce or maps_ce)."""
import struct
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import compare_stock_maps as compare  # noqa: E402

CANDIDATES = [ROOT / "assets/maps/ce/deathisland.map", ROOT / "assets/maps_ce/deathisland.map",
              Path.home() / "halo-ce-decomp-port/assets/maps/ce/deathisland.map"]


@pytest.fixture(scope="module")
def stock():
    for path in CANDIDATES:
        if path.exists():
            return path
    pytest.skip("no Custom Edition deathisland.map")


def as_halo_pc(path, target, patch=None):
    data = bytearray(path.read_bytes())
    struct.pack_into("<I", data, 4, 7)
    if patch:
        patch(data)
    target.write_bytes(data)
    return target


def test_same_data_matches(stock, tmp_path):
    pc = as_halo_pc(stock, tmp_path / "deathisland.map")
    assert compare.compare(pc, stock) == (True, [])


def test_changed_weapon_does_not(stock, tmp_path):
    data, _, tag_offset, tag_size = compare.load(stock)
    tags, _ = compare.read_tags(data, tag_offset, tag_size)
    weapon = next(tag for tag in tags if tag["class"] == "weap" and not tag["indexed"])
    spot = tag_offset + weapon["address"] - compare.TAG_BASE + 0x10

    def patch(buffer):
        buffer[spot] ^= 0xFF

    pc = as_halo_pc(stock, tmp_path / "deathisland.map", patch)
    same, problems = compare.compare(pc, stock)
    assert not same and any("weap" in problem for problem in problems)


def test_not_a_pair_of_editions(stock):
    same, problems = compare.compare(stock, stock)
    assert not same and "609" in problems[0]
