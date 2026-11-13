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
  setting changes only on a yes.
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
