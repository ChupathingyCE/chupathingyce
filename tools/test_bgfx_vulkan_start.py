"""Source checks for the bgfx renderer's Vulkan start on AMD GPUs and RADV
(game issue #1046), beside tools/test_bgfx_formats.py's depth formats.

Three things the Khronos validation layer found on Mesa's lavapipe, each
undefined on AMD's drivers where NVIDIA's shrug them off, and the fallback
for what is still unknown:

- a draw's textures were set before the targets were bound, and binding a
  fresh target (its clear) or a view's blits touched the view, a submit
  that dropped them: the draw went out with its samplers unbound
  (bg_apply_textures sets them last, after the view);
- the shader containers marked an input location the shader does not read
  with 0xffff, which bgfx's Vulkan renderer reads as instance data and
  gives a fallback input at a location another attribute has
  (0xfffe now, and tools/bgfx-patches/0005 leaves it out);
- bgfx told a swap chain it may switch between two present modes without
  asking the surface whether they are compatible, which Mesa's Wayland WSI
  says they are not (tools/bgfx-patches/0006);
- a Vulkan start leaves a note in the save root that the game's first
  frames after its first map loads remove (never the setup screen's); a note found at the next start means the last one closed before
  them, and that start draws with OpenGL and asks whether to stay there; the
  setting changes only on a yes. The setup screen's own restart (a setting
  that needs one, such as the renderer) takes the note away, and the next
  start tries Vulkan again (#1665). A start the player closes after the
  game drew that many frames of its own, before a map did (a load that
  never finished, a minimized window), takes it away too (#1970). Nothing
  else does: a black window left with Escape, Quit or its close button
  before then, a crash or a hang still goes to OpenGL next (#1046).
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src" / "d3d8_bgfx.inl"
PATCHES = ROOT / "tools" / "bgfx-patches"


def read(path):
	return path.read_text(encoding="utf-8")


def function_body(source, signature):
	start = source.rindex(signature)
	body = source.index("\n{", start)
	return source[body:source.index("\n}\n", body)]


def test_textures_set_after_the_view():
	source = read(SOURCE)
	bind = function_body(source, "static void bg_bind_textures(")
	# (recorded, not set: the submit sets them)
	assert "bgfx_set_texture(" not in bind
	assert "bg_stage_textures[stage].handle = bg.blank;" in bind
	apply = function_body(source, "static void bg_apply_textures(")
	assert "bgfx_set_texture((uint8_t)stage, bg.samplers[stage], bg_stage_textures[stage].handle" in apply
	draw = function_body(source, "static void bg_draw(")
	assert draw.index("bg_bind_textures(&key, texture_scale);") < draw.index("bg_bind_targets(&has_depth)")
	view = draw.index("bg_view_set(bg.framebuffer, rect, FALSE);")
	textures = draw.index("bg_apply_textures();")
	assert view < textures < draw.index("bgfx_submit(bg.view, handle")
	assert textures < draw.index("bgfx_submit_occlusion_query(bg.view, handle")


def test_unread_input_locations_are_not_instance_data():
	container = function_body(read(SOURCE), "static char *bg_vulkan_container(")
	assert "used[index] ? bg_attribute_ids[index] : 0xfffe" in container
	assert ": 0xffff" not in container
	patch = read(PATCHES / "0005-vulkan-attribute-gaps.patch")
	assert "+++ b/src/renderer_vk.cpp" in patch
	added = [line[1:] for line in patch.splitlines() if line.startswith("+") and not line.startswith("+++")]
	assert any("else if (UINT16_MAX == id)" in line for line in added)


def test_present_modes_checked_for_compatibility():
	patch = read(PATCHES / "0006-vulkan-present-modes-compatible.patch")
	assert "+++ b/src/renderer_vk.cpp" in patch
	added = "\n".join(line[1:] for line in patch.splitlines() if line.startswith("+") and not line.startswith("+++"))
	assert "VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_COMPATIBILITY_EXT" in added
	assert "vkGetPhysicalDeviceSurfaceCapabilities2KHR" in added
	assert "!presentModesCompatible(physicalDevice, m_surface, m_sci.presentMode" in added
	assert "s_renderVK->m_swapchainMaintenance1Supported = false;" in added


def test_patches_apply_in_order():
	names = sorted(path.name for path in PATCHES.glob("*.patch"))
	assert names.index("0005-vulkan-attribute-gaps.patch") < names.index("0006-vulkan-present-modes-compatible.patch")


def test_a_start_that_did_not_get_going_goes_to_opengl():
	source = read(SOURCE)
	start = function_body(source, "static BOOL bg_start(")
	check = start.index("if (bg_last_start_failed())")
	# (before bgfx starts, after the driver check: a blocklisted driver never
	# leaves a note)
	assert start.index("bg_vulkan_check(&init.vendorId") < check < start.index("bgfx_init(&init)")
	after = start[check:check + 600]
	assert "platform_offer_opengl();" in after
	assert "return FALSE;" in after
	# (the start itself never writes the setting: only the player's yes does)
	assert "config_write(" not in start
	assert start.index("bg_start_note();") > check
	assert "bg_vulkan && bg_start_note_wanted()" in start
	failed = function_body(source, "static BOOL bg_last_start_failed(")
	assert "remove(path);" in failed
	out = function_body(source, "static void bg_frame_out(")
	assert "++frames == BG_START_FRAMES" in out and "remove(path);" in out
	# (only the game's frames after its first map count: a start that closes
	# loading that map, past the setup screen, still leaves the note)
	assert "!bg_map_seen" in out
	assert "bg_map_seen = TRUE;" in function_body(source, "static void bg_warm(")
	present = function_body(source, "static void bg_present(")
	assert present.index("bg_warm();") < present.index("bgfx_frame(BGFX_FRAME_NONE);") < present.index("bg_frame_out();")
	launcher = function_body(source, "BOOL xgpu_bgfx_launcher_frame(")
	assert "bg_frame_out();" not in launcher
	assert 'getenv("HALO_BGFX_START_NOTE")' in function_body(source, "static BOOL bg_start_note_wanted(")


def test_setup_screen_restart_is_not_a_failed_start():
	"""#1665: choosing Vulkan on the setup screen restarts the game for it;
	a start that drew the setup screen on Vulkan and was closed by that
	restart is not one that didn't get going"""
	source = read(SOURCE)
	clear = function_body(source, "void xgpu_bgfx_start_note_clear(")
	assert "if (!bg_vulkan || !bg_start_note_wanted())" in clear
	assert "bg_start_note_path(path, sizeof(path));" in clear
	assert "remove(path)" in clear
	assert "void xgpu_bgfx_start_note_clear(void);" in read(ROOT / "port" / "linux" / "src" / "xgpu.h")
	launcher = read(ROOT / "port" / "linux" / "src" / "launcher.c")
	purpose = function_body(launcher, "static void closing_on_purpose(")
	assert "if (xgpu_bgfx)" in purpose and "xgpu_bgfx_start_note_clear();" in purpose
	# (before either way of starting again: in this same process, which never
	# returns, or a new one)
	restart = function_body(launcher, "static int restart_with(")
	cleared = restart.index("closing_on_purpose();")
	assert cleared < restart.index("update_launch_with(path, argument)")
	assert cleared < restart.index("update_start_in_place_with(path, argument)")
	to_setup = function_body(launcher, "int launcher_restart_to_setup(")
	assert to_setup.index("closing_on_purpose();") < to_setup.index('update_launch_with(path, "--setup")')
	# (only those restarts: the cleared note's only callers)
	assert launcher.count("closing_on_purpose();") == 2
	assert "xgpu_bgfx_start_note_clear" not in launcher.replace(purpose, "")


def test_black_window_left_still_falls_back():
	"""#1046 against #1665: a Vulkan window that never shows a picture is
	left blind, with Escape or B (Welcome's QUIT, back), Quit or the close
	button; none of them may take the note away, so the next start draws with
	OpenGL"""
	launcher = read(ROOT / "port" / "linux" / "src" / "launcher.c")
	back = function_body(launcher, "static void back(")
	welcome = back.index("case _screen_welcome:")
	assert "closing_on_purpose" not in back[welcome:back.index("exit(EXIT_SUCCESS);", welcome)]
	quit_at = launcher.index('platform_log("launcher: quit");')
	assert "closing_on_purpose" not in launcher[quit_at:launcher.index("exit(EXIT_SUCCESS);", quit_at)]
	assert "closing_on_purpose" not in function_body(launcher, "static void events(")
	assert "closing_on_purpose" not in function_body(launcher, "static void input(")
	assert "closing_on_purpose" not in function_body(launcher, "static void script_step(")



def test_closed_after_the_games_frames_is_not_a_failed_start():
	"""#1970: a start the player closes while its first map's load hangs, or
	with the window minimized, drew the game's frames on Vulkan: the note
	goes. Fewer frames than a start needs, the setup screen's (never
	counted), a crash or a hang (the close never read) still leave it"""
	source = read(SOURCE)
	out = function_body(source, "static void bg_frame_out(")
	# (every frame of the game's counts, the first map's or not: counted before the map's check)
	counted = out.index("bg_frames_out++;")
	assert counted < out.index("!bg_map_seen")
	assert "if (bg_vulkan && bg_frames_out <= BG_START_FRAMES)" in out
	closing = function_body(source, "void xgpu_bgfx_closing(")
	assert "if (!bg_vulkan || !bg_start_note_wanted() || bg_frames_out < BG_START_FRAMES)" in closing
	assert "bg_start_note_path(path, sizeof(path));" in closing and "remove(path)" in closing
	# (the setup screen's frames never count toward it)
	assert "bg_frame_out();" not in function_body(source, "BOOL xgpu_bgfx_launcher_frame(")
	assert source.count("bg_frame_out();") == 1
	assert "void xgpu_bgfx_closing(void);" in read(ROOT / "port" / "linux" / "src" / "xgpu.h")
	# (only the player's own close: never a crash, abort or halt path)
	platform = read(ROOT / "port" / "linux" / "src" / "sdl_platform.c")
	closed = function_body(platform, "void platform_exit_window_closed(")
	assert "if (xgpu_bgfx)\n\t\txgpu_bgfx_closing();" in closed
	assert closed.index("quit_watchdog_stand_down();") < closed.index("xgpu_bgfx_closing();")
	assert platform.count("xgpu_bgfx_closing") == 1
	for path in (ROOT / "port" / "linux").rglob("*.c"):
		if path.name != "sdl_platform.c":
			assert "xgpu_bgfx_closing" not in read(path), path
	assert "xgpu_bgfx_closing" not in read(ROOT / "source" / "main" / "main.c")


def test_fallback_says_what_was_asked_for_and_what_draws():
	platform = read(ROOT / "port" / "linux" / "src" / "sdl_platform.c")
	# (bgfx's, the first: Android's Vulkan backend has its own after it)
	start = platform.index("BOOL platform_video_reopen_for_gl(")
	reopen = platform[start:platform.index("\n}\n", start)]
	assert "renderer: asked for %s (bgfx, display.renderer" in reopen
	assert "drawing with OpenGL" in reopen and "platform_bgfx_name()" in reopen
	name = function_body(platform, "static const char *platform_bgfx_name(")
	assert '"Metal"' in name and '"Vulkan"' in name and '"Direct3D 11"' in name


def test_opengl_for_good_only_on_a_yes():
	platform = read(ROOT / "port" / "linux" / "src" / "sdl_platform.c")
	offer = function_body(platform, "static void platform_opengl_offer(")
	assert '"Switch to OpenGL"' in offer and '"Not now"' in offer
	assert "SDL_ShowMessageBox(&question, &answer)" in offer
	# (a run nobody can answer is never asked and keeps the setting)
	quiet = offer.index("platform_run_is_quiet()")
	assert quiet < offer.index("SDL_ShowMessageBox(") and quiet < offer.index("config_write(")
	# (the setting is written on a yes and nowhere else)
	assert offer.count("config_write(") == 1
	yes = offer.index("if (answer == 1)")
	assert yes < offer.index('config_write("display.renderer", "gl");') < offer.index("else", yes)
	# (asked from the event loop, once the window is up)
	assert "platform_show_pending_message();\n\tplatform_opengl_offer();" in platform
	assert "void platform_offer_opengl(void);" in read(ROOT / "port" / "linux" / "src" / "sdl_platform.h")


def test_sdl_log_in_debug_txt():
	platform = read(ROOT / "port" / "linux" / "src" / "sdl_platform.c")
	initialize = function_body(platform, "BOOL platform_sdl_initialize(")
	assert "SDL_SetLogOutputFunction(platform_sdl_log, NULL);" in initialize
	assert "SDL_SetLogPriority(SDL_LOG_CATEGORY_VIDEO, SDL_LOG_PRIORITY_INFO);" in initialize
	assert 'platform_log("video: SDL %d.%d.%d, %s driver, %s window"' in platform
