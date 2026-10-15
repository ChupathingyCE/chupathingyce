"""Asset-free tests of validation isolation, failure recording and owned-process timeout."""
import contextlib
import io
import json
import runpy
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

run = runpy.run_path(str(Path(__file__).with_name("run_bot_validation.py")))["run"]


class ValidationRunnerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="bot-validation-test-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.data = self.root / "original-data"
        maps = self.data / "maps"
        maps.mkdir(parents=True)
        for name in ("ratrace.map", "ui.map"):
            (maps / name).write_bytes(b"test filename only; not game data")
        (self.data / "debug.txt").write_text("preserve me")
        self.binary = self.root / "fake halo"
        self.output = self.root / "evidence"

    def launch_fixture(self, body, seconds: float = 2.0):
        self.binary.write_text("#!/bin/sh\n" + body + "\n")
        self.binary.chmod(0o755)
        args = SimpleNamespace(binary=self.binary, source_data=self.data,
                               output_root=self.output, map="ratrace", bots=7, seconds=seconds)
        with contextlib.redirect_stdout(io.StringIO()):
            return run(args)

    def test_isolated_hidden_offline_environment_and_fresh_logs(self):
        body = 'printf "%s\\n" "$HALO_HIDDEN_WINDOW/$HALO_NULL_RENDERER/$HALO_NET_ONLINE/$HALO_BOT_HUMAN_CALLOUTS/$HALO_BOTS"'
        first = self.launch_fixture(body)
        second = self.launch_fixture(body)
        self.assertNotEqual(first, second)
        self.assertEqual((first / "game.log").read_text().strip(), "1/1/false/false/7")
        self.assertTrue((first / "data/maps").is_symlink())
        self.assertEqual((self.data / "debug.txt").read_text(), "preserve me")
        metadata = json.loads((first / "run.json").read_text())
        self.assertFalse(metadata["stopped_by_watchdog"])
        self.assertEqual(metadata["returncode"], 0)
        self.assertEqual(len(metadata["binary_sha256"]), 64)

    def test_watchdog_stops_its_process_group(self):
        output = self.launch_fixture("sleep 30", seconds=0.1)
        metadata = json.loads((output / "run.json").read_text())
        self.assertTrue(metadata["stopped_by_watchdog"])
        self.assertLess(metadata["returncode"], 0)
        self.assertLess(metadata["elapsed_wall_seconds"], 6)

    def test_nonzero_exit_is_not_validation_success(self):
        with self.assertRaisesRegex(SystemExit, "game failed with 3"):
            self.launch_fixture("exit 3")
        output = next(self.output.iterdir())
        self.assertEqual(json.loads((output / "run.json").read_text())["returncode"], 3)

    def test_missing_map_does_not_create_run_directory(self):
        (self.data / "maps/ratrace.map").unlink()
        with self.assertRaisesRegex(ValueError, "missing Xbox map"):
            self.launch_fixture("exit 0")
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
