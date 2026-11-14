"""The back buffer is checked before it is used (#1298).

0.9.1a crashed at start on a 32-bit Windows machine in D3DDevice_Present's
first frame: render_target_get finds nothing for a surface without Data
(d3d8_surface_initialize leaves the back buffer without it when contiguous
memory runs out), and Present read through the NULL entry. Every look-up of
the back buffer, in the GL renderer and bgfx's, is checked before use.
"""
import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src"

LOOKUP = re.compile(r"(\w+)\s*=\s*(?:[^;?]*\?\s*)?(?:render_target_get|bg_target_get|cr_target_get)\(&device\.back_buffer\)")


@pytest.mark.parametrize("name", ["d3d8_gl.c", "d3d8_bgfx.inl", "d3d8_console.inl"])
def test_back_buffer_lookups_are_checked(name):
    text = (SOURCE / name).read_text(encoding="utf-8")
    found = list(LOOKUP.finditer(text))
    assert found, name
    for match in found:
        variable = match.group(1)
        if text[:match.start()].endswith("!("):
            continue  # if (!(entry = ...)) return
        after = text[match.end():]
        # the first use of the entry: a check, or a dereference
        check = re.search(r"!\s*" + variable + r"\b|\(\s*" + variable + r"\s*\)|" + variable + r"\s*&&", after)
        use = re.search(r"\b" + variable + r"\s*->", after)
        line = text.count("\n", 0, match.start()) + 1
        assert check and (not use or check.start() < use.start()), f"{name}:{line}: {variable} used unchecked"
