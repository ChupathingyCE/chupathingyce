"""Benchmark parser tests; never launch the game."""
import runpy
import unittest
from pathlib import Path

# Load the standalone CLI without depending on tools being an installed package.
benchmark = runpy.run_path(str(Path(__file__).with_name("metal_policy_benchmark.py")))
summarize = benchmark["summarize"]


def fixture():
    lines = ["renderer regression seed 12345678", "bots: bot 1 joined (marine)"]
    for value in range(1, 7):
        for stage in benchmark["STAGES"]:
            lines.append(f"draw cpu: {stage} 10 calls, {value} ns average, 20 us worst, {value}.0 ms a frame")
        lines.append(f"frame CPU {value}.0 ms, GPU 2.0 ms (medians; 95th {value + 10}.0, 3.0)")
        lines.append(f"frame {value}: 100 draws, 10 immediate, 1 clears; 40 KB mirrored, 20 KB streamed")
    return "\n".join(lines)


class BenchmarkParserTests(unittest.TestCase):
    def test_warmup_and_frame_window_percentiles(self):
        report = summarize(fixture(), 1)
        self.assertEqual(report["stages"]["mirror_refresh"]["windows"], 4)
        self.assertEqual(report["stages"]["mirror_refresh"]["median_ns_per_call"], 4.5)
        frame = report["frame_window_medians"]
        self.assertEqual(frame["cpu_median_ms"], 4.5)
        self.assertEqual(frame["cpu_p95_ms"], 14.5)
        self.assertEqual(frame["gpu_p95_ms"], 3)
        self.assertEqual(report["geometry_window_medians"]["draws"], 100)

    def test_rejects_missing_bots_seed_timings_or_profile(self):
        text = fixture()
        for malformed in (text.replace("joined", "not created"),
                          text.replace("renderer regression seed 12345678", ""),
                          text.replace("frame CPU", "unavailable"),
                          text.replace("draw cpu: mirror_refresh", "disabled"),
                          text.replace("KB mirrored", "missing")):
            with self.subTest(log=malformed[:60]), self.assertRaises(ValueError):
                summarize(malformed, 1)

    def test_rejects_render_errors(self):
        for error in ("validation error", "fault at 0x1234", "command buffer 7 failed"):
            with self.subTest(error=error), self.assertRaisesRegex(ValueError, "rendering error"):
                summarize(fixture() + "\n" + error, 1)
