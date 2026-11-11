"""Source checks for the bgfx renderer's target formats (#1046).

bgfx's Vulkan renderer gives D24S8 no format on a device without
D24_UNORM_S8_UINT (AMD's drivers, RADV included) since the pin bump of #1002,
and the game made every depth buffer as D24S8 without checking the handle:
bgfx then read its texture table at 0xFFFF and crashed in the intro. The
patch puts back the old fallback to D32_SFLOAT_S8_UINT; the game picks its
depth format from bgfx's caps, falls back to OpenGL when none is drawable,
and checks each target's and frame buffer's handle before using it.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src" / "d3d8_bgfx.inl"
PATCH = ROOT / "tools" / "bgfx-patches" / "0004-vulkan-d24s8-fallback.patch"


def read(path):
	return path.read_text(encoding="utf-8")


def function_body(source, signature):
	start = source.rindex(signature)
	body = source.index("\n{", start)
	return source[body:source.index("\n}\n", body)]


def test_patch_falls_back_to_d32_s8():
	patch = read(PATCH)
	assert "+++ b/src/renderer_vk.cpp" in patch
	added = "\n".join(line for line in patch.splitlines() if line.startswith("+"))
	assert "VK_FORMAT_D32_SFLOAT_S8_UINT" in added
	assert "fp32.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT" in added


def test_depth_format_from_caps():
	source = read(SOURCE)
	# (no target is made with a fixed depth format any more)
	assert "depth ? BGFX_TEXTURE_FORMAT_D24S8" not in source
	target = function_body(source, "static struct bg_target *bg_target_get(")
	assert "depth ? bg.depth_format : BGFX_TEXTURE_FORMAT_BGRA8" in target
	samples = function_body(source, "static int bg_screen_samples(")
	assert "caps->formats[bg.depth_format] & bit" in samples
	# (D24S8 first: the game's stencil)
	table = source[source.index("bg_depth_formats[] = {"):]
	table = table[:table.index("};")]
	assert table.index("BGFX_TEXTURE_FORMAT_D24S8") < table.index("BGFX_TEXTURE_FORMAT_D32F")
	choose = function_body(source, "static BOOL bg_formats_choose(")
	assert "bg_format_drawable(caps, BGFX_TEXTURE_FORMAT_BGRA8" in choose
	assert "bg.depth_format = bg_depth_formats[index].format;" in choose
	drawable = function_body(source, "static BOOL bg_format_drawable(")
	assert "BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER" in drawable
	assert 'getenv("HALO_BGFX_UNSUPPORTED")' in drawable


def test_no_drawable_format_means_opengl():
	start = function_body(read(SOURCE), "static BOOL bg_start(")
	check = start.index("if (!bg_formats_choose())")
	assert "bgfx_shutdown();" in start[check:check + 80]
	# (after bgfx has started: the caps are bgfx's)
	assert start.index("bgfx_init(&init)") < check


def test_stencil_only_with_a_stencil():
	source = read(SOURCE)
	assert "has_depth && bg.depth_stencil && rs[D3DRS_STENCILENABLE]" in source
	bind = function_body(source, "static BOOL bg_bind_targets(")
	assert "bg.depth_stencil ? BGFX_CLEAR_STENCIL : 0" in bind


def test_handles_checked_before_use():
	source = read(SOURCE)
	target = function_body(source, "static struct bg_target *bg_target_get(")
	assert "return BGFX_HANDLE_IS_VALID(entry->texture) ? entry : NULL;" in target
	assert target.rindex("if (!BGFX_HANDLE_IS_VALID(entry->texture))") > target.index("bgfx_create_texture_2d(")
	framebuffer = function_body(source, "static bgfx_frame_buffer_handle_t bg_framebuffer_get(")
	assert "color && BGFX_HANDLE_IS_VALID(color->texture)" in framebuffer
	assert "depth && BGFX_HANDLE_IS_VALID(depth->texture)" in framebuffer
	assert framebuffer.index("if (!count)") < framebuffer.index("bgfx_create_frame_buffer_from_handles(")
	bind = function_body(source, "static BOOL bg_bind_targets(")
	assert bind.index("if (!BGFX_HANDLE_IS_VALID(bg.framebuffer))") < bind.index("bg_view_set(bg.framebuffer")
	blit = function_body(source, "static void bg_blit_2d(")
	assert "!BGFX_HANDLE_IS_VALID(destination) || !BGFX_HANDLE_IS_VALID(source)" in blit
	read_back = function_body(source, "static uint32_t bg_read_texture(")
	assert "!BGFX_HANDLE_IS_VALID(texture)" in read_back
