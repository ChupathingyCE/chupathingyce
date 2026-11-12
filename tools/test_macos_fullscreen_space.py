"""Source checks for the macOS window's full-screen Space (#1110).

macOS's own full screen (the window's toggleFullScreen:) moves even an
exclusive fullscreen window to a Space of its own, and SDL still takes it
for exclusive. Its next change of fullscreen restyled the window as if it
were not in a Space, AppKit threw, and the game aborted. The window is now
taken out of the Space first, and the move waited for, before SDL changes
its fullscreen kind or its fullscreen; a change of resolution alone makes it
fullscreen again after, and a window that will not leave is left for the
next apply instead of SDL's call that throws.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PLATFORM = ROOT / "port" / "linux" / "src" / "sdl_platform.c"
MACOS = ROOT / "port" / "macos" / "src" / "macos_video.c"


def read(path):
	return path.read_text(encoding="utf-8")


def function_body(source, signature):
	start = source.index(signature)
	body = source.index("\n{", start)
	return source[body:source.index("\n}\n", body)]


def test_settled_before_each_fullscreen_change():
	source = read(PLATFORM)
	kind = function_body(source, "static void platform_fullscreen_kind_apply(void)")
	assert kind.index("if (!platform_window_space_settle())") < kind.index("SDL_SetWindowFullscreenMode(")
	# (a Space the window would not leave: the change is tried again later)
	assert "applied = -1;" in kind
	toggle = function_body(source, "static BOOL platform_window_set_fullscreen(BOOL fullscreen)")
	assert toggle.index("if (!platform_window_space_settle())") < toggle.index("SDL_SetWindowFullscreen(")
	# (every other fullscreen change goes through those two)
	assert source.count("SDL_SetWindowFullscreenMode(") == 1
	assert source.count("SDL_SetWindowFullscreen(platform_window") == 1


def test_only_a_space_sdl_takes_for_exclusive():
	settle = function_body(read(PLATFORM), "static BOOL platform_window_space_settle(void)")
	assert "#if defined(__APPLE__) && !defined(HALO_IOS)" in settle
	assert "!macos_window_in_full_screen_space(window) || !SDL_GetWindowFullscreenMode(platform_window)" in settle
	assert "macos_window_toggle_full_screen_space(window);" in settle
	# (waited for until SDL has heard the window left, with a limit)
	assert "SDL_GetWindowFlags(platform_window) & SDL_WINDOW_FULLSCREEN" in settle
	assert "SDL_GetTicks() < deadline" in settle
	assert "return FALSE;" in settle
	assert "platform_window_left_space = TRUE;" in settle


def test_fullscreen_again_after_leaving_a_space():
	apply = function_body(read(PLATFORM), "void platform_display_apply(void)")
	assert apply.index("platform_window_left_space = FALSE;") < apply.index("platform_fullscreen_kind_apply();")
	assert "else if (platform_window_left_space && platform_fullscreen_requested)" in apply
	assert "platform_window_set_fullscreen(TRUE);" in apply
	assert "? mode : -1;" in apply


def test_macos_helpers():
	source = read(MACOS)
	space = function_body(source, "int macos_window_in_full_screen_space(void *window)")
	# (NSWindowStyleMaskFullScreen)
	assert 'sel_registerName("styleMask")) & (1ul << 14)' in space
	toggle = function_body(source, "void macos_window_toggle_full_screen_space(void *window)")
	assert 'sel_registerName("toggleFullScreen:")' in toggle
