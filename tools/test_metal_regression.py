"""Regression setup invariants shared by captured OpenGL/Metal scenes."""
import json
import os
import re
import tempfile
from pathlib import Path
from unittest import TestCase, mock

from tools import metal_regression as runner
from tools.metal_image_compare import Image, write_bmp

ROOT = Path(__file__).resolve().parents[1]


class MetalRegressionSetupTests(TestCase):
    def test_field_of_view_setting_matches_opengl(self):
        pattern = r'\{ "display\.field_of_view",[^\n]+'
        gl = (ROOT / "port/linux/src/port_config.c").read_text()
        metal = (ROOT / "port/macos/metal/renderer/port_config.c").read_text()
        gl_match, metal_match = re.search(pattern, gl), re.search(pattern, metal)
        assert gl_match is not None
        assert metal_match is not None
        self.assertEqual(metal_match.group(), gl_match.group())
        self.assertIn('"70.0", "HALO_FIELD_OF_VIEW"', metal_match.group())

    def test_seed_override_is_opt_in_and_before_host_start(self):
        source = (ROOT / "port/linux/game/network_test.c").read_text()
        begin = source.index('const char *capture = getenv("HALO_RENDER_REGRESSION");')
        end = source.index("network_game_client_request_immediate_start();", begin)
        hook = source[begin:end]
        self.assertIn('if (capture && !strcmp(capture, "1"))', hook)
        self.assertIn("network_game_set_random_seed(0x12345678);", hook)
        self.assertEqual(source.count("network_game_set_random_seed(0x12345678)"), 1)

    def test_menu_freeze_is_opt_in_and_at_simulation_time(self):
        source = (ROOT / "port/linux/game/network_test.c").read_text()
        begin = source.index("if (network_test.mode == _network_test_off)")
        end = source.index("if (network_test.mode == _network_test_download)", begin)
        hook = source[begin:end]
        self.assertIn('getenv("HALO_RENDER_REGRESSION")', hook)
        self.assertIn('main_menu_loaded && capture && !strcmp(capture, "1")', hook)
        self.assertIn("game_time_get() >= TICKS_PER_SECOND", hook)
        self.assertIn("game_time_set_paused(TRUE)", hook)

    def test_missing_data_skips_cleanly_without_building_or_launching(self):
        with tempfile.TemporaryDirectory() as folder, mock.patch.object(runner.platform, "system", return_value="Darwin"), \
                mock.patch.dict(os.environ, {"HALO_ALLOW_LAUNCH": "1"}), mock.patch.object(runner, "build_pair") as build:
            self.assertEqual(runner.main(["--data-root", folder]), 77)
            build.assert_not_called()

    def test_launch_requires_permission(self):
        with mock.patch.object(runner.platform, "system", return_value="Darwin"), \
                mock.patch.dict(os.environ, {"HALO_ALLOW_LAUNCH": "0"}):
            reason = runner.prerequisites(ROOT)
            assert reason is not None
            self.assertIn("HALO_ALLOW_LAUNCH", reason)

    def test_matrix_covers_menu_hud_all_aa_and_lighting(self):
        selected = runner.cases()
        self.assertTrue(any(case.menu for case in selected))
        self.assertEqual({case.aa for case in selected if not case.menu}, set(runner.MODES))
        self.assertTrue(any(case.lighting for case in selected))

    def test_report_and_difference_are_written_on_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            gl, metal = folder / "gl.bmp", folder / "metal.bmp"
            write_bmp(gl, Image(2, 2, bytes([0, 0, 0, 255] * 4)))
            write_bmp(metal, Image(2, 2, bytes([255] * 16)))
            (folder / "gameplay-off").mkdir()
            with mock.patch.object(runner, "capture", side_effect=[(gl, "same"), (metal, "same")]) as capture:
                passed = runner.run_cases([runner.Case("gameplay-off")], {"OpenGL": gl, "Metal": metal}, folder, folder, 14)
            self.assertFalse(passed)
            self.assertEqual(capture.call_count, 2)
            self.assertTrue((folder / "gameplay-off/difference.bmp").is_file())
            self.assertFalse(json.loads((folder / "results.json").read_text())[0]["passed"])

    def test_v1_v2_labels_use_the_requested_apps_and_are_recorded(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            reference, candidate = folder / "v1.bmp", folder / "v2.bmp"
            for image in (reference, candidate):
                write_bmp(image, Image(1, 1, bytes([1, 2, 3, 255])))
            (folder / "gameplay-off").mkdir()
            apps = {"MetalV1": Path("v1"), "MetalV2": Path("v2")}
            with mock.patch.object(runner, "capture", side_effect=[(reference, "same"), (candidate, "same")]) as capture:
                self.assertTrue(runner.run_cases([runner.Case("gameplay-off")], apps, folder, folder, 14,
                                                reference_name="MetalV1", candidate_name="MetalV2"))
            self.assertEqual([call.args[1:3] for call in capture.call_args_list],
                             [("MetalV1", apps["MetalV1"]), ("MetalV2", apps["MetalV2"])])
            report = json.loads((folder / "results.json").read_text())[0]
            self.assertEqual(report["reference_renderer"], "MetalV1")
            self.assertEqual(report["candidate_renderer"], "MetalV2")

    def test_custom_labels_require_prebuilt_apps(self):
        with mock.patch.object(runner, "prerequisites", return_value=None), \
                mock.patch.object(runner, "build_pair") as build, \
                self.assertRaises(SystemExit) as error:
            runner.main(["--reference-name", "MetalV1"])
        self.assertEqual(error.exception.code, 2)
        build.assert_not_called()

    def test_capture_uses_isolation_and_rejects_mismatched_camera(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            log = "network test: renderer regression seed 12345678\nnetwork test: tick 150 player 0 local 0 camera (1 2 3)\n"
            def game(argv, **kwargs):
                output = Path(argv[1])
                (output / "run.log").write_text(log)
                for frame in range(0, 600, 120):
                    write_bmp(output / "shots" / f"frame{frame:05d}.bmp", Image(1, 1, bytes([1, 2, 3, 4])))
                self.assertEqual(kwargs["env"]["HALO_SOURCE_DATA"], str(folder))
                self.assertIn("HALO_RENDER_REGRESSION=1", argv)
                self.assertIn("HALO_ANISOTROPIC_FILTERING=1", argv)
                self.assertIn("HALO_RESOLUTION_SCALING=native", argv)
                self.assertIn("MTL_DEBUG_LAYER=1", argv)
                self.assertIn("HALO_TEST_INPUT=", argv)
            with mock.patch.object(runner.subprocess, "run", side_effect=game):
                image, camera = runner.capture(runner.Case("gameplay-off"), "Metal", Path("app"), folder, folder, 14)
            self.assertEqual(camera, "1 2 3")
            self.assertEqual(image.name, "frame00480.bmp")
            self.assertEqual(len(list(image.parent.glob("*.bmp"))), 3)
            with mock.patch.object(runner, "capture", side_effect=[(image, "a"), (image, "b")]), \
                    self.assertRaisesRegex(ValueError, "mismatched camera"):
                runner.run_cases([runner.Case("bad")], {"OpenGL": image, "Metal": image}, folder, folder, 14)

    def test_real_gl_metal_matrix(self):
        data = Path(os.environ.get("HALO_SOURCE_DATA", str(runner.DEFAULT_DATA)))
        reason = runner.prerequisites(data)
        if reason:
            self.skipTest(reason)
        # Opt-in via HALO_ALLOW_LAUNCH; default test invocations never launch.
        # Retain artifacts even on failure, as the CLI does.
        output = Path(tempfile.mkdtemp(prefix="metal-regression-unittest-"))
        argv = ["--output", str(output), "--data-root", str(data)]
        gl, metal = os.environ.get("HALO_REGRESSION_GL_APP"), os.environ.get("HALO_REGRESSION_METAL_APP")
        if gl and metal:
            argv.extend(["--gl-app", gl, "--metal-app", metal])
        self.assertEqual(runner.main(argv), 0, f"Inspect captures and logs in {output}")
