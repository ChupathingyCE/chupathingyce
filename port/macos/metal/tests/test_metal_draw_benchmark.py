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
        self.assertEqual(summary["native/same-state"]["median_ns"], 106)
        self.assertEqual(summary["native/same-state"]["lookups"]["pipeline_lookups"], 1)

    def test_different_images_rejected(self):
        records = self.records()
        records[-1]["digest"] = "different"
        with self.assertRaises(ValueError):
            benchmark.summarize(records)

    def pool_records(self):
        records = self.records()
        for variant, count in benchmark.POOL_GROUPS.items():
            record = dict(records[0], variant=variant, pool_group_draws=count,
                          draw_pool_drains=2048 // count)
            records.append(record)
        return records

    def test_every_variant_uses_one_binary_with_bounded_options(self):
        output = Path("probe-output")
        self.assertEqual(benchmark.probe_command(output, "native"), [str(output / "probe")])
        self.assertEqual(benchmark.probe_command(output, "native-off"),
                         [str(output / "probe"), "--native-off"])
        for variant, count in benchmark.POOL_GROUPS.items():
            self.assertEqual(benchmark.probe_command(output, variant),
                             [str(output / "probe"), f"--pool-group={count}"])
        with self.assertRaises(ValueError):
            benchmark.probe_command(output, "pool4096")

    def test_grouped_results_preserve_draws_images_and_pool_bounds(self):
        summary = benchmark.summarize(self.pool_records())
        self.assertEqual(summary["pool64/same-state"]["draw_pool_drains"], 32)
        self.assertEqual(summary["pool8/same-state"]["pool_group_draws"], 8)
        for field, value in (("pool_group_draws", 4096), ("draw_pool_drains", 1),
                             ("draws", 2047), ("draws", 4096), ("digest", "different")):
            with self.subTest(field=field, value=value):
                records = self.pool_records()
                records[-1][field] = value
                with self.assertRaises(ValueError):
                    benchmark.summarize(records)

    def test_different_draw_counts_rejected(self):
        records = self.records()
        records[-1]["draws"] = 1024
        with self.assertRaises(ValueError):
            benchmark.summarize(records)

    def test_build_is_actual_backend(self):
        with patch.object(benchmark.subprocess, "run") as run:
            command = benchmark.build_probe(Path("probe"))
        self.assertIn("tools/metal_draw_probe.m", command)
        self.assertNotIn("-DNDEBUG", command)  # fixture correctness checks must execute
        run.assert_called_once()


if __name__ == "__main__":
    unittest.main()
