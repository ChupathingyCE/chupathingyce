"""Asset-free parser contracts for the real-game profiler; never launch Halo."""
import unittest
from typing import Any

from tools.metal_game_profile import BACKEND, FRONTEND, aggregate, overrides, summarize


def fixture(attribution=True, bots=2):
    lines = ["Metal on Apple M2 Pro", "renderer regression seed 12345678",
             "screen: 852x480 drawn at 2560x1440", "network test: starting the game"]
    lines += [f"bots: bot {index} joined (marine)" for index in range(bots)]
    for value in range(1, 7):
        lines += [f"network test: tick {value * 150}",
                  f"Metal: frame CPU {value}.0 ms, GPU 2.0 ms (medians; 95th {value + 10}.0, 3.0)"]
        if attribution:
            for name in FRONTEND:
                lines.append(f"chupathingyce: draw cpu: {name} 600 calls, {value} ns average, 9 us worst, 1.0 ms a frame")
            for name in BACKEND:
                lines.append(f"Metal draw cpu: {name} 6000 calls, {value * 100} ns average, 9 us worst, 1.0 ms a frame")
            lines += [f"frame {value * 60}: 100 draws, 10 immediate",
                      "Metal draw cpu: 300 attribute tables bound for 600 draws",
                      "draw cache: depth 80 hits, 20 misses", "draw cache: shader key 0 hits, 0 misses"]
    return "\n".join(lines)


class GameProfileTests(unittest.TestCase):
    def test_separates_frontend_backend_and_discards_warmup(self):
        report = summarize(fixture(), True, 2)
        self.assertEqual(report["frontend"]["call"]["median_ns_per_call"], 4.5)
        self.assertEqual(report["backend"]["bind_attributes"]["median_ns_per_call"], 450)
        self.assertEqual(report["frontend"]["gpu_draw"]["calls_in_retained_windows"], 2400)
        self.assertEqual(report["draws_per_frame"], 110)
        self.assertEqual(report["attribute_binds_per_draw"], 0.5)
        self.assertEqual(report["cache"]["depth"]["hit_fraction"], 0.8)
        self.assertNotIn("shader key", report["cache"])
        self.assertEqual(report["frame"]["cpu_window_p95_ms"], 14.5)
        self.assertEqual(report["bots_joined"], 2)
        self.assertEqual(report["render_sizes"], [(2560, 1440)])

    def test_uninstrumented_runs_have_only_frame_metrics(self):
        report = summarize(fixture(False), False, 2)
        self.assertNotIn("frontend", report)
        self.assertEqual(report["frame"]["cpu_median_ms"], 4.5)
        with self.assertRaisesRegex(ValueError, "timers enabled"):
            summarize(fixture(True), False, 2)

    def test_rejects_missing_bots_gameplay_timing_and_render_failures(self):
        text = fixture()
        for malformed in (text.replace("joined", "not-created"),
                          text.replace("Metal on Apple M2 Pro", "OpenGL"),
                          text.replace("renderer regression seed 12345678", ""),
                          text.replace("network test: tick", "missing tick"),
                          text.replace("Metal draw cpu:", "missing backend"),
                          text.replace("frame CPU", "missing timing"),
                          text + "\nMetal API Validation Enabled",
                          text + "\ncommand buffer 4 failed"):
            with self.subTest(log=malformed[-50:]), self.assertRaises(ValueError):
                summarize(malformed, True, 2)

    def test_aggregates_independent_runs_and_keeps_modes_separate(self):
        first = dict(summarize(fixture(), True, 2), scene="bloodgulch", mode="attribution", variant="v2")
        second: dict[str, Any] = dict(summarize(fixture(), True, 2), scene="bloodgulch",
                                      mode="attribution", variant="v2")
        second["frontend"]["call"]["median_ns_per_call"] = 8.5
        second["frame"]["cpu_median_ms"] = 6.5
        throughput = dict(summarize(fixture(False), False, 2), scene="bloodgulch", mode="throughput", variant="v2")
        summary = aggregate([first, second, throughput])
        self.assertEqual(summary["bloodgulch/attribution/v2"]["runs"], 2)
        self.assertEqual(summary["bloodgulch/attribution/v2"]["frontend_ns_per_call"]["call"], 6.5)
        self.assertEqual(summary["bloodgulch/attribution/v2"]["frame"]["cpu_median_ms"], 5.5)
        self.assertNotIn("frontend_ns_per_call", summary["bloodgulch/throughput/v2"])

    def test_controls_combat_bot_count_score_and_timer_mode(self):
        attributed = overrides("hangemhigh", True, 31)
        throughput = overrides("hangemhigh", False, 31)
        self.assertEqual(attributed["HALO_BOTS"], "31")
        self.assertEqual(attributed["HALO_NETWORK_TEST_SCORE"], "32767")
        self.assertEqual(attributed["HALO_NETWORK_TEST_KILL"], "0")
        self.assertEqual(throughput["HALO_GPU_STATS"], "0")
        self.assertEqual({k: v for k, v in attributed.items() if k != "HALO_GPU_STATS"},
                         {k: v for k, v in throughput.items() if k != "HALO_GPU_STATS"})


if __name__ == "__main__":
    unittest.main()
