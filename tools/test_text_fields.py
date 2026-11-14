"""Runs the C tests of the high-res text's distance fields
(port/linux/tests/text_fields_test.c): the generator in
port/third_party/msdfgen/msdf_gen.h, as port/linux/src/text_hires.c makes a
glyph's field, and the verified mark's outline (halo_delta_mark.h). The
units have no game dependency, so they compile anywhere; the generator is
built with warnings as errors, as text_hires.c includes it."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def test_text_fields(tmp_path):
    """Builds the generator with its tests, warnings as errors, and runs them."""
    cc = next((path for path in map(shutil.which, ("clang", "cc", "gcc")) if path), None)
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "text_fields_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-O1",
         str(ROOT / "port" / "linux" / "tests" / "text_fields_test.c"), "-lm",
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_shader_decodes_the_fields():
    """Both pixel shader emitters decode texture 0 as the fields when the
    key says so (nv2a_pixel_shader_key's text_fields), with the range the
    fields are made with (xgpu.h's XGPU_TEXT_FIELD_RANGE), and every
    renderer's bind sets the bit from the atlas."""
    for name in ("nv2a_psh.c", "nv2a_psh_vk.c"):
        source = (ROOT / "port" / "linux" / "src" / name).read_text(encoding="utf-8")
        assert "key->text_fields" in source, name
        assert 'XGPU_TEXT_FIELD_RANGE_TEXT " * 0.5 * (1.0 / texels.x + 1.0 / texels.y)' in source, name
        assert "max(min(t0.r, t0.g), min(max(t0.r, t0.g), t0.b))" in source, name
    xgpu = (ROOT / "port" / "linux" / "src" / "xgpu.h").read_text(encoding="utf-8")
    assert "#define XGPU_TEXT_FIELD_RANGE 6\n" in xgpu
    assert '#define XGPU_TEXT_FIELD_RANGE_TEXT "6.0"' in xgpu
    for name in ("d3d8_gl.c", "d3d8_bgfx.inl", "d3d8_vk.inl"):
        source = (ROOT / "port" / "linux" / "src" / name).read_text(encoding="utf-8")
        assert "key->text_fields = " in source, name
    keys = (ROOT / "port" / "linux" / "src" / "xgpu_shader_keys.c").read_text(encoding="utf-8")
    assert "case _slot_text_fields: return &key->text_fields;" in keys
    assert "_slot_last = _slot_text_fields" in keys


def test_console_keeps_rasterized_glyphs():
    """A console's own renderer has no decode, so its build leaves the fields
    out (halo_port_features.h), and the original Xbox has no high-res text
    at all."""
    features = (ROOT / "port" / "linux" / "include" / "halo_port_features.h").read_text(encoding="utf-8")
    assert "#define HALO_CONSOLE_NO_TEXT_FIELDS" in features
    text = (ROOT / "port" / "linux" / "src" / "text_hires.c").read_text(encoding="utf-8")
    assert "#if !defined(HALO_CONSOLE_NO_TEXT_FIELDS) && !defined(HALO_CONSOLE_HOST_ART)" in text


def test_notices_carry_the_license():
    """msdfgen's MIT license is in the downloads' notices."""
    import json

    manifest = json.loads((ROOT / "port" / "notices" / "components.json").read_text(encoding="utf-8"))
    entry = next(entry for entry in manifest["components"] if entry["name"] == "msdfgen")
    assert entry["license"] == "MIT"
    assert entry["files"] == ["port/third_party/msdfgen/LICENSE"]
    assert (ROOT / "port" / "third_party" / "msdfgen" / "LICENSE").read_text(encoding="utf-8").startswith("MIT License")
