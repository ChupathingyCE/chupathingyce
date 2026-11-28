"""server/CHANGELOG.md has a heading for every release since 0.7.5b (#1396):
each version with release notes in docs/release-notes, in order, newest
first, under Unreleased. A release made without moving the Unreleased
entries under its version fails here."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CHANGELOG = ROOT / "server/CHANGELOG.md"
NOTES = ROOT / "docs/release-notes"


def _key(version: str) -> tuple:
    """0.9.2b as (0, 9, 2, "b"), for ordering"""
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)([a-z]?)", version)
    assert match, version
    return (int(match.group(1)), int(match.group(2)), int(match.group(3)), match.group(4))


def _headings() -> list:
    return re.findall(r"^## (.+)$", CHANGELOG.read_text(encoding="utf-8"), re.M)


def _released() -> list:
    names = (path.stem for path in NOTES.glob("*.md"))
    return [name for name in names if re.fullmatch(r"\d+\.\d+\.\d+[a-z]?", name)]


def test_unreleased_comes_first():
    assert _headings()[0] == "Unreleased"


def test_headings_are_newest_first():
    versions = _headings()[1:]
    assert versions == sorted(versions, key=_key, reverse=True)
    assert len(versions) == len(set(versions))


def test_every_release_since_075b_has_a_heading():
    headings = set(_headings())
    missing = [version for version in _released() if _key(version) > _key("0.7.5b") and version not in headings]
    assert not missing, f"server/CHANGELOG.md has no heading for {sorted(missing, key=_key)}"
