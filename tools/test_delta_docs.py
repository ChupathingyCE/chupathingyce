# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
"""Delta's documentation holds together: every relative link in the spec
(docs/delta/) and the pages around it reaches a file and a heading that
exist; the spec links out of its folder only to the reference client or by
full URL, so it can move to a repository of its own (docs/delta/MOVING.md);
and every place in the code that cites a section of a Delta page
("docs/delta/peer.md, Chat") names a heading that page has."""
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SPEC = ROOT / "docs" / "delta"
PAGES = sorted(SPEC.glob("*.md")) + [ROOT / "docs" / "delta.md", ROOT / "docs" / "delta-implementation.md",
                                      ROOT / "docs" / "delta-compatibility.md", ROOT / "docs" / "online.md"]


def slug(heading):
    return re.sub(r"[^\w\- ]", "", heading.strip().lower()).replace(" ", "-")


def headings(path):
    text = re.sub(r"^```.*?^```", "", path.read_text(encoding="utf-8"), flags=re.S | re.M)
    return [match for match in re.findall(r"^#{1,6} (.+)$", text, re.M)]


def links(path):
    text = re.sub(r"^```.*?^```", "", path.read_text(encoding="utf-8"), flags=re.S | re.M)
    return re.findall(r"\]\(([^)\s]+)\)", text)


def test_links_resolve():
    broken = []
    for page in PAGES:
        for link in links(page):
            if re.match(r"[a-z]+:", link):
                continue
            target, _, anchor = link.partition("#")
            path = (page.parent / target).resolve() if target else page
            if not path.exists():
                broken.append((page.name, link))
            elif anchor and path.suffix == ".md" and anchor not in {slug(h) for h in headings(path)}:
                broken.append((page.name, link))
    assert not broken, broken


def test_spec_is_self_contained():
    reference = (ROOT / "tools" / "delta-ref").resolve()
    for page in sorted(SPEC.glob("*.md")):
        for link in links(page):
            if re.match(r"[a-z]+:", link) or link.startswith("#"):
                continue
            path = (page.parent / link.partition("#")[0]).resolve()
            assert path.parent == SPEC.resolve() or reference in (path, *path.parents), (page.name, link)
    for path in (ROOT / "tools" / "delta-ref").iterdir():
        if path.suffix in (".c", ".h"):
            for name in re.findall(r'#include "([^"]+)"', path.read_text()):
                assert (path.parent / name).exists(), (path.name, name)


def test_code_cites_real_sections():
    """'docs/delta/peer.md, "Chat"' and 'docs/delta/peer.md, Chat' in the
    code and docs: the page exists and has the heading"""
    files = subprocess.run(["git", "grep", "-l", "-E", r"docs/delta(/[a-z]+\.md|-implementation\.md)"], cwd=ROOT,
                           capture_output=True, text=True).stdout.split()
    wrong = []
    for name in files:
        text = (ROOT / name).read_text(encoding="utf-8", errors="replace")
        flat = re.sub(r"\s*\n\s*(?:\*|#|//|/\*)?\s*", " ", text)
        for page, rest in re.findall(r"docs/(delta/[a-z]+\.md|delta-implementation\.md)`?,? (.{0,80})", flat):
            path = ROOT / "docs" / page
            if not path.exists():
                wrong.append((name, page))
                continue
            quoted = re.match(r'\\?"([^"\\]+)\\?"', rest)
            if quoted:
                if quoted.group(1) not in headings(path):
                    wrong.append((name, page, quoted.group(1)))
            elif rest[:1].isupper() and not any(rest.startswith(h) for h in headings(path)):
                wrong.append((name, page, rest[:40]))
    assert not wrong, wrong


def test_old_pages_point_to_the_spec():
    index = (ROOT / "docs" / "delta.md").read_text()
    assert "delta/README.md" in index and "delta/peer.md#wire-format" in index
    assert "delta/versioning.md" in (ROOT / "docs" / "delta-compatibility.md").read_text()
