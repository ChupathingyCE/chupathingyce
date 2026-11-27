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

A swap chain counts as waiting only when its presents wait at the
display's beat: a loading screen's slow frames once paced a window that
never waited for the rest of the run (#1367). The pacing decision (V-Sync,
VRR, tuning.present_mode) is every renderer's, sdl_platform.c's
platform_present_mode.
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


def paced_frames_left_out(fps, hz, jitter_ms, seconds=20, early=0.0):
	"""The rule (bg_window_due, paced) over frames from a limiter at fps on a
	display at hz: how many frames were never shown. early: how much of an
	interval before its turn a frame still goes (0 the old rule's)."""
	random.seed(1)
	interval, next_due, now, left_out = 1.0 / hz, 0.0, 0.0, 0
	while now < seconds:
		now += 1.0 / fps + random.uniform(-jitter_ms, jitter_ms) / 1000.0
		if now < next_due - interval * early:
			left_out += 1
			continue
		next_due = now + interval if now - next_due > interval else next_due + interval
	return left_out


def test_pacing_left_out_frames_at_the_refresh_rate():
	# (why only a backend whose presents wait is paced)
	assert paced_frames_left_out(165, 165.0, 0.3) > 20


def test_a_paced_window_takes_a_frame_a_little_early():
	# (the Mac's Metal at display.max_fps 60 on a 60 Hz display: 56 shown)
	assert paced_frames_left_out(60, 60.0, 0.3, early=0.25) == 0
	assert "if (now < next - interval * 0.25)" in function_body(read(), "static BOOL bg_window_due(")


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


def frames_until_paced(frame_times, wait_times, hz):
	"""The rule in bg_window_presented over presents: each frame's time since
	the one before and how long its present waited; the frame it paces at,
	or None."""
	interval, waits = 1.0 / hz, 0
	for number, (gap, wait) in enumerate(zip(frame_times, wait_times)):
		# (the first present has none before it)
		waited = number > 0 and wait > interval * 0.5 and interval * 0.9 < gap < interval * 1.1
		waits = waits + 1 if waited else 0
		if waits >= 30:
			return number
	return None


def test_a_loading_screen_does_not_pace_the_window():
	# (shader compiles: long, uneven frames, each present held a while)
	random.seed(2)
	gaps = [random.uniform(0.03, 0.2) for _ in range(300)]
	assert frames_until_paced(gaps, gaps, 144.0) is None


def test_presents_at_the_displays_beat_pace_the_window():
	gaps = [1.0 / 60.0] * 100
	assert frames_until_paced(gaps, [0.012] * 100, 60.0) == 30


def test_the_rule_in_the_source_wants_the_displays_beat():
	presented = function_body(read(), "static void bg_window_presented(")
	assert "since > interval * 0.9 && since < interval * 1.1" in presented
	assert "end - start > interval * 0.5" in presented


def test_a_paced_window_is_looked_at_again():
	due = function_body(read(), "static BOOL bg_window_due(")
	assert "config_changes() != bg_window_paced_changes" in due
	assert "BG_WINDOW_RECHECK" in due


def test_one_present_decision_for_every_renderer():
	source = read()
	assert "platform_present_mode()" in function_body(source, "static unsigned long bg_reset_flags(")
	vk = (ROOT / "port" / "linux" / "src" / "d3d8_vk.inl").read_text(encoding="utf-8")
	present = function_body(vk, "static void vk_present(")
	assert "host_vk_present_mode = platform_present_mode();" in present
	assert "platform_video_swap();" in present
	host = (ROOT / "port" / "android" / "host" / "host_vk_present.c").read_text(encoding="utf-8")
	assert "info.presentMode = chain->present_mode;" in host
	assert "VK_PRESENT_MODE_FIFO_RELAXED_KHR" in function_body(host, "static VkPresentModeKHR present_mode_choose(")
	platform = (ROOT / "port" / "linux" / "src" / "sdl_platform.c").read_text(encoding="utf-8")
	assert "platform_present_mode() == PLATFORM_PRESENT_ADAPTIVE" in function_body(platform,
		"static int platform_present_adaptive(")
