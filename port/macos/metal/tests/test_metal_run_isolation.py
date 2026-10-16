"""The scripted runner must isolate data, saves/config and startup logs."""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from metal_paths import ROOT


class ScriptedRunIsolationTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("perl"), "runner watchdog needs Perl")
    def test_save_root_and_working_directory_are_isolated(self):
        with tempfile.TemporaryDirectory(prefix="metal run isolation ") as folder:
            root = Path(folder)
            maps = root / "installed/maps"
            maps.mkdir(parents=True)
            (maps / "bloodgulch.map").touch()
            installed = root / "installed saves"
            installed.mkdir()
            sentinel = installed / "config.toml"
            sentinel.write_text("user preferences stay here\n")
            app = root / "fake-game"
            app.write_text(
                '#!/bin/sh\nset -eu\n'
                'printf "CWD=%s\\nSAVES=%s\\nDATA=%s\\n" "$PWD" "$HALO_SAVE_ROOT" "$HALO_DATA_ROOT"\n'
                'printf "startup log\\n" >> debug.txt\n'
                'printf "isolated save\\n" > "$HALO_SAVE_ROOT/probe-save"\n'
                'printf "isolated game log\\n" > "$HALO_DATA_ROOT/debug.txt"\n')
            app.chmod(0o755)
            output = root / "capture"
            env = {**os.environ, "HALO_APP": str(app), "HALO_SOURCE_DATA": str(maps.parent),
                   "HALO_SAVE_ROOT": str(installed), "HALO_ALLOW_LAUNCH": "1"}
            subprocess.run(["sh", str(ROOT / "tools/metal_scripted_run.sh"), str(output), "1"],
                           env=env, check=True, capture_output=True, text=True)
            log = (output / "run.log").read_text()
            locations = dict(line.split("=", 1) for line in log.splitlines())
            self.assertEqual(Path(locations["CWD"]).resolve(), output.resolve())
            self.assertEqual(Path(locations["SAVES"]).resolve(), (output / "data").resolve())
            self.assertEqual(Path(locations["DATA"]).resolve(), (output / "data").resolve())
            self.assertEqual((output / "data/probe-save").read_text(), "isolated save\n")
            self.assertEqual((output / "debug.txt").read_text(), "isolated game log\n")
            self.assertEqual(sentinel.read_text(), "user preferences stay here\n")
            self.assertEqual(sorted(p.name for p in installed.iterdir()), ["config.toml"])
