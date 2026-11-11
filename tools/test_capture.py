"""Source checks for screenshots (port/linux/src/capture.c).

A screenshot is what the window shows: the readback comes after the overlay
(DELTA, toasts, Online Games and the other code-drawn screens) is drawn over
the picture, on both renderers (#1025). The screenshot's own toast is left
out of the next shot, and the keys and buttons that take one are not the
menus' input (#1024).
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "port" / "linux" / "src"


def read(path):
	return path.read_text(encoding="utf-8")


def function_body(source, signature):
	# (the definition: the last time the signature comes, after any forward
	# declaration)
	start = source.rindex(signature)
	body = source.index("\n{", start)
	return source[body:source.index("\n}\n", body)]


def test_gl_screenshot_after_overlay():
	body = function_body(read(SRC / "d3d8_gl.c"), "void WINAPI D3DDevice_Present(")
	overlay = body.index("ui_overlay_present(x, y, width, height, window_width, window_height)")
	capture = body.index("gl_capture_frame(x, y, width, height)")
	swap = body.index("platform_video_swap()")
	assert overlay < capture < swap


def test_gl_screenshot_reads_the_window():
	body = function_body(read(SRC / "d3d8_gl.c"), "static void gl_capture_frame(")
	assert "platform_window_framebuffer()" in body
	assert "framebuffer_get(" not in body
	# (the window's rows come bottom first)
	assert "TRUE, TRUE, &gl_capture.released" in body


def test_bgfx_screenshot_after_overlay():
	source = read(SRC / "d3d8_bgfx.inl")
	body = function_body(source, "static void bg_present(")
	overlay = body.index("ui_overlay_present(")
	start = body.index("bg_shot_start(letterbox)")
	frame = body.index("bgfx_frame(BGFX_FRAME_NONE)")
	submit = body.index("capture_screenshot_submit(")
	assert overlay < start < frame < submit
	# (the window's screenshot, not a copy of the back buffer)
	start_body = function_body(source, "static void bg_shot_start(")
	assert "bgfx_request_screen_shot((bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE, bg_shot_path)" in start_body
	callback = function_body(source, "static void bg_callback_screen_shot(")
	assert "bg_shot_taken(" in callback


def test_no_picture_keeps_the_ask():
	# (a minimized window: the ask is taken only by a frame with a picture)
	gl = function_body(read(SRC / "d3d8_gl.c"), "static void gl_capture_frame(")
	assert "if (width <= 0 || height <= 0 || !capture_screenshot_take())" in gl
	bgfx = function_body(read(SRC / "d3d8_bgfx.inl"), "static void bg_present(")
	assert "window && letterbox[2] > 0 && letterbox[3] > 0 && !bg.shot_pending && capture_screenshot_take()" in bgfx


def test_screenshot_toast_left_out():
	guide = read(ROOT / "port" / "linux" / "game" / "guide.c")
	body = function_body(guide, "static void render_toast(")
	assert "guide.toast_capture && capture_screenshot_wanted()" in body


def test_capture_keys_are_not_keyboard_use():
	source = read(SRC / "xinput_sdl.c")
	body = function_body(source, "static void keyboard_gamepad(")
	assert "!capture_scancode(scancode)" in body
	helper = function_body(source, "static BOOL capture_scancode(")
	assert "SDL_SCANCODE_PRINTSCREEN" in helper and "capture_key(0)" in helper and "capture_key(1)" in helper


def test_capture_buttons_kept_from_the_menus():
	source = read(SRC / "xinput_sdl.c")
	body = function_body(source, "static void capture_asks(")
	assert "XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB" in body
	# (before DELTA's hold of Back reads the pad)
	poll = source[source.index("capture_asks(&input, own, own_count, &state->Gamepad)"):]
	assert "guide_asks(&input, own, own_count, &state->Gamepad)" in poll


if __name__ == "__main__":
	import sys

	failed = 0
	for name, test in list(globals().items()):
		if name.startswith("test_") and callable(test):
			try:
				test()
			except AssertionError:
				failed += 1
				print("FAILED", name)
	sys.exit(1 if failed else 0)
