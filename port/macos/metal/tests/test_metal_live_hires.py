"""Live high-res settings must update bindings through production cache code."""
import contextlib
import io
import runpy
import shutil
import unittest
from pathlib import Path


class LiveHiresTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("clang"), "clang required for sanitizer integration test")
    def test_live_settings_invalidate_cached_bindings(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            probe = runpy.run_path(str(Path(__file__).with_name("probe_live_hires.py")))
            result = probe["main"]()
        self.assertEqual(result, 0, output.getvalue())
