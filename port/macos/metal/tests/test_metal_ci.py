"""The CI selector picks the Metal renderer only for macos-metal."""
import contextlib
import io
import sys
import unittest
from unittest.mock import patch

import tools.ci_build as ci


class StopConfigure(Exception):
    pass


class MetalCITests(unittest.TestCase):
    def test_configure_selector(self):
        for platform, flag in (("macos", None), ("macos-metal", "--macos-metal")):
            with self.subTest(platform=platform):
                with patch.object(sys, "argv", ["ci_build.py", platform, "debug"]), \
                     patch.object(ci, "run", side_effect=StopConfigure) as run, \
                     patch.object(ci, "release_build", return_value=False), \
                     patch.object(ci, "version", return_value="test"), \
                     contextlib.redirect_stdout(io.StringIO()), \
                     self.assertRaises(StopConfigure):
                    ci.main()
                command = run.call_args.args[0]
                flags = [value for value in command if value.startswith("--macos-metal")]
                self.assertEqual(flags, [] if flag is None else [flag])
                self.assertIn("--portable", command)

    def test_no_v2_platform(self):
        self.assertNotIn("macos-metal-v2", ci.OUTPUTS)
        self.assertEqual(ci.NINJA_TARGETS["macos-metal"], "macos")


if __name__ == "__main__":
    unittest.main()
