"""Opt-in Metal build graph contracts (no Metal device needed)."""
import io
from pathlib import Path
from types import SimpleNamespace
from unittest import TestCase
from unittest.mock import patch

from tools.lp64_build import Lp64Build, Lp64Host, Lp64Unit
from tools.macos_build import MetalLp64Build, generate_macos_metal_build
from tools.ninja_syntax import Writer


class MetalBuildTests(TestCase):
    def graph(self, selected=None, architectures=None):
        sln = SimpleNamespace(build_dir=Path("build"))
        if selected is not None:
            sln.macos_metal = selected
        stream = io.StringIO()
        generate_macos_metal_build(Writer(stream), sln, architectures or ["arm64"])
        return stream.getvalue()

    def test_default_and_explicit_off_emit_nothing(self):
        self.assertEqual(self.graph(), "")
        self.assertEqual(self.graph(False), "")

    def test_opt_in_is_standalone(self):
        graph = self.graph(True)
        self.assertIn("build macos-metal-smoke: phony build/macos/metal-smoke", graph)
        self.assertIn("port/macos/metal/metal_smoke.m", graph)
        self.assertIn("-fobjc-arc", graph)
        self.assertIn("-framework Metal", graph)
        self.assertIn("-DHALO_MACOS_METAL_EXPERIMENTAL=1", graph)
        self.assertNotIn("port/macos/visr/", graph)
        self.assertNotIn("build/macos/halo", graph)
        self.assertNotIn("build/linux", graph)

    def test_game_uses_macos_only_substitutions(self):
        build = object.__new__(MetalLp64Build)
        build.lp64_dir = Path("build/macos/lp64")
        original = [
            Lp64Unit(build.lp64_dir / "port/linux/src/d3d8_gl.c", "-DHALO_64BIT"),
            Lp64Unit(build.lp64_dir / "port/linux/src/nv2a_vsh.c", "-DHALO_64BIT"),
            Lp64Unit(Path("port/linux/src/posix_files.c"), "-std=gnu11", native=True),
        ]
        with patch.object(Lp64Build, "units", return_value=original):
            units = build.units(Lp64Host("macos", [], [], []), [])
        sources = {str(unit.source) for unit in units}
        self.assertIn("build/macos/lp64/port/macos/metal/renderer/d3d8_device.c", sources)
        self.assertNotIn("build/macos/lp64/port/linux/src/d3d8_gl.c", sources)
        self.assertNotIn("build/macos/lp64/port/linux/src/nv2a_vsh.c", sources)
        self.assertTrue(all("port/macos/visr/" not in source for source in sources))
        native = next(unit for unit in units if unit.source.name == "posix_files.c")
        self.assertEqual(native.cflags, "-std=gnu11")

    def test_portable_build_has_both_slices(self):
        graph = self.graph(True, ["arm64", "x86_64"])
        self.assertIn("--target=arm64-apple-macos13.0", graph)
        self.assertIn("--target=x86_64-apple-macos13.0", graph)
        self.assertIn("build build/macos/metal-smoke: macos_metal_lipo", graph)
