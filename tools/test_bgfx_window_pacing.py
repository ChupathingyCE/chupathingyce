"""Source checks for bgfx's window pacing with V-Sync off (d3d8_bgfx.inl,
bg_window_due and bg_window_presented).

bgfx held every backend's window to the display's rate with V-Sync off,
because Metal's presents wait for the display. Vulkan's immediate mode and
Direct3D 11 do not wait, and there the rule left out each frame that came a
little before its turn: with a frame limit at the refresh rate
(display.max_fps 165 at 165 Hz) about three frames a second were never
shown, and the one before stayed up twice as long. The model below is the
old rule; on Vulkan and Direct3D 11 every frame goes to the window now, as
OpenGL's do, and only Metal, or a swap chain seen to wait for the display,
is paced.
"""
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src" / "d3d8_bgfx.inl"


def read():
	return SOURCE.read_text(encoding="utf-8")


def function_body(source, signature):
	start = source.rindex(signature)
	body = source.index("\n{", start)
	return source[body:source.index("\n}\n", body)]


def paced_frames_left_out(fps, hz, jitter_ms, seconds=20):
	"""The old rule (bg_window_due, paced) over frames from a limiter at fps
	on a display at hz: how many frames were never shown."""
	random.seed(1)
	interval, next_due, now, left_out = 1.0 / hz, 0.0, 0.0, 0
	while now < seconds:
		now += 1.0 / fps + random.uniform(-jitter_ms, jitter_ms) / 1000.0
		if now < next_due:
			left_out += 1
			continue
		next_due = now + interval if now - next_due > interval else next_due + interval
	return left_out


def test_pacing_left_out_frames_at_the_refresh_rate():
	# (why only a backend whose presents wait is paced)
	assert paced_frames_left_out(165, 165.0, 0.3) > 20


def test_only_metal_is_paced_from_the_start():
	source = read()
	start = source.index("static BOOL bg_window_paced")
	declaration = source[start:source.index(";", start)]
	assert "#ifdef __APPLE__" in declaration and "= TRUE" in declaration


def test_unpaced_frames_all_go_to_the_window():
	due = function_body(read(), "static BOOL bg_window_due(")
	assert "if (!bg_window_paced)\n\t\treturn TRUE;" in due


def test_a_waiting_swap_chain_is_paced():
	presented = function_body(read(), "static void bg_window_presented(")
	assert "bg_window_waits >= BG_WINDOW_WAITS_TO_PACE" in presented
	assert "bg_window_paced = TRUE;" in presented
