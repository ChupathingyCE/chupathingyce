"""The experimental CI selector must not select/overwrite V1 or OpenGL."""
import contextlib
import io
import sys
import unittest
from unittest.mock import patch

import tools.ci_build as ci


class StopConfigure(Exception):
    pass


class MetalV2CITests(unittest.TestCase):
    def test_configure_selector_is_exclusive(self):
        for platform, flag in (("macos", None), ("macos-metal", "--macos-metal"),
                               ("macos-metal-v2", "--macos-metal-v2")):
            with self.subTest(platform=platform):
                with patch.object(sys, "argv", ["ci_build.py", platform, "debug"]), \
                     patch.object(ci, "run", side_effect=StopConfigure) as run, \
                     patch.object(ci, "release_build", return_value=False), \
                     patch.object(ci, "version", return_value="test"), \
                     contextlib.redirect_stdout(io.StringIO()), \
                     self.assertRaises(StopConfigure):
                    ci.main()
                command = run.call_args.args[0]
                flags = [value for value in command if value in ("--macos-metal", "--macos-metal-v2")]
                self.assertEqual(flags, [] if flag is None else [flag])
                self.assertIn("--portable", command)

    def test_artifact_and_target_do_not_overwrite_v1(self):
        self.assertEqual(ci.NINJA_TARGETS["macos-metal-v2"], "macos_v2")
        self.assertEqual(ci.OUTPUTS["macos-metal-v2"], ["build/macos_v2/ChupathingyCE.app"])
        self.assertNotEqual(ci.OUTPUTS["macos-metal-v2"], ci.OUTPUTS["macos-metal"])


if __name__ == "__main__":
    unittest.main()
