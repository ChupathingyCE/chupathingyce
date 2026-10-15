"""Regression setup invariants shared by captured OpenGL/Metal scenes."""
import re
from pathlib import Path
from unittest import TestCase

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
