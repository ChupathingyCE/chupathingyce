"""Benchmark aggregation contracts; GPU fixture is explicit, not a unit-test dependency."""
import unittest
from pathlib import Path
from unittest.mock import patch

import tools.metal_draw_benchmark as benchmark


class DrawBenchmarkTests(unittest.TestCase):
    def records(self):
        return [{"variant": variant, "workload": "same-state", "draws": 2048,
                 "digest": "same-image", "ns_per_draw": values, "shader_lookups": 2,
                 "pipeline_lookups": 1, "depth_lookups": 1, "attribute_binds": 1}
                for variant in benchmark.VARIANTS for values in ([100, 101, 1000], [110, 111, 1200])]

    def test_uses_medians_of_independent_runs(self):
        summary = benchmark.summarize(self.records())
        self.assertEqual(summary["v2/same-state"]["median_ns"], 106)
        self.assertEqual(summary["v2/same-state"]["lookups"]["pipeline_lookups"], 1)

    def test_different_images_rejected(self):
        records = self.records()
        records[-1]["digest"] = "different"
        with self.assertRaises(ValueError):
            benchmark.summarize(records)

    def test_build_is_actual_backend_and_isolated_variant(self):
        for v2 in (False, True):
            with self.subTest(v2=v2), patch.object(benchmark.subprocess, "run") as run:
                command = benchmark.build_probe(Path("probe"), v2)
            self.assertIn("tools/metal_draw_probe.m", command)
            self.assertEqual("-DHALO_MACOS_METAL_V2=1" in command, v2)
            self.assertNotIn("-DNDEBUG", command)  # fixture correctness checks must execute
            run.assert_called_once()


if __name__ == "__main__":
    unittest.main()
