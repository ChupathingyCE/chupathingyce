"""Validation and progress-helper tests; never launch the game."""
import contextlib
import io
import json
import os
import runpy
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

soak = runpy.run_path(str(Path(__file__).with_name("metal_lifetime_soak.py")))
validate_log = soak["validate_log"]
run_with_progress = soak["run_with_progress"]


def fixture(maps):
    lines = ["Metal API Validation Enabled", "bots: bot 1 joined (marine)"]
    for index, name in enumerate(maps, 1):
        lines.extend([f"network test: game {index}, slayer", "network test: starting the game",
                      "renderer regression seed 12345678", "network test: player 0 is named Player",
                      "network test: player 1 is named Bot 1",
                      f"shader list {name}: warmed", "network test: tick 150 player 0",
                      "network test: tick 300 player 0", "frame 600: 500 draws",
                      "Metal: 7 renames in the last 600 frames"])
    return "\n".join(lines)


class LifetimeTests(unittest.TestCase):
    def test_confirms_each_loaded_map_and_return_trip(self):
        maps = ["bloodgulch", "hangemhigh", "bloodgulch"]
        result = validate_log(fixture(maps), maps, 1)
        self.assertEqual([row["map"] for row in result["rounds"]], maps)
        self.assertEqual(result["rounds"][2]["max_tick"], 300)
        self.assertEqual(result["rounds"][1]["rename_events_reported"], 7)

    def test_rejects_missing_validation_transition_or_gameplay(self):
        maps = ["bloodgulch", "hangemhigh", "bloodgulch"]
        text = fixture(maps)
        for broken in (text.replace("Metal API Validation Enabled", ""),
                       text.replace("network test: game 3, slayer", ""),
                       text.replace("shader list hangemhigh:", "other map:"),
                       text.replace("tick 300", "tick 10").replace("tick 150", "tick 20"),
                       text.replace("500 draws", "0 draws"),
                       text.replace("player 1 is named", "not a player"),
                       text.replace("renderer regression seed 12345678", ""),
                       text + "\ncommand buffer 9 failed"):
            with self.subTest(log=broken[:50]), self.assertRaises(ValueError):
                validate_log(broken, maps, 1)

    def test_rss_summary_is_process_memory(self):
        result = soak["memory_summary"]([
            {"children": [{"pid": 10, "rss_kib": value}]} for value in (1024, 2048, 3072)])
        self.assertEqual(result["peak_mib"], 3)
        self.assertEqual(result["first_third_median_mib"], 1)
        self.assertEqual(result["last_third_median_mib"], 3)
        with self.assertRaises(ValueError):
            soak["memory_summary"]([])

    def test_progress_is_flushed_and_retained(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.redirect_stdout(io.StringIO()) as output:
            folder = Path(temporary)
            with mock.patch.dict(run_with_progress.__globals__, {"child_rss": lambda pid: []}):
                samples = run_with_progress([sys.executable, "-c", "import time; time.sleep(0.1)"],
                                            folder, 1, os.environ.copy(), interval=0.02)
            self.assertTrue(samples)
            self.assertIn("running", output.getvalue())
            self.assertIn("DONE", output.getvalue())
            self.assertEqual(json.loads((folder / "progress.json").read_text()), samples)

    def test_progress_propagates_nonzero_exit(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.redirect_stdout(io.StringIO()), \
                self.assertRaises(subprocess.CalledProcessError) as error:
            run_with_progress([sys.executable, "-c", "raise SystemExit(7)"], Path(temporary), 1,
                              os.environ.copy())
        self.assertEqual(error.exception.returncode, 7)

    def test_progress_timeout_terminates_the_child(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.redirect_stdout(io.StringIO()), \
                self.assertRaises(subprocess.TimeoutExpired), \
                mock.patch.dict(run_with_progress.__globals__, {"child_rss": lambda pid: []}):
            run_with_progress([sys.executable, "-c", "import time; time.sleep(30)"], Path(temporary),
                              -109.9, os.environ.copy(), interval=0.02)
