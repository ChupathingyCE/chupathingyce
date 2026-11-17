"""Source checks for the bgfx overlay's textures (#1081).

bg_overlay_prepare made the overlay's program and five textures in turn,
and when one of them failed it returned with the ones made before it kept
and never destroyed, and with the stand-in textures (picture, plasma,
medals) possibly invalid, which the overlay then bound every frame. The
textures a prepare made are let go when it fails (bg_overlay_release), the
stand-ins count like the rest, and a texture replaced later (the picture,
the plasma, the medals' atlas) only takes the place of the old one when it
could be made (bg_overlay_replace), so nothing binds an invalid handle.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src" / "d3d8_bgfx.inl"


def read():
	return SOURCE.read_text(encoding="utf-8")


def function_body(source, signature):
	start = source.rindex(signature)
	body = source.index("\n{", start)
	return source[body:source.index("\n}\n", body)]


def test_release_lets_every_handle_go():
	release = function_body(read(), "static void bg_overlay_release(")
	for texture in ("overlay_atlas", "overlay_icons", "overlay_picture", "overlay_plasma", "overlay_medals"):
		assert f"&bg.{texture}" in release, texture
	assert "bgfx_destroy_texture(*textures[index]);" in release
	assert "*textures[index] = (bgfx_texture_handle_t)BGFX_INVALID_HANDLE;" in release
	assert "bgfx_destroy_program(bg.overlay);" in release
	assert "bg.overlay = (bgfx_program_handle_t)BGFX_INVALID_HANDLE;" in release


def test_prepare_releases_on_every_failure():
	prepare = function_body(read(), "static BOOL bg_overlay_prepare(")
	# (handles start invalid, so a release destroys only what was made)
	assert "bg.overlay_atlas = bg.overlay_icons = bg.overlay_picture = bg.overlay_plasma = bg.overlay_medals =" in prepare
	# (every return FALSE after the first texture is made releases first)
	first_texture = prepare.index("bg.overlay_atlas = bgfx_create_texture_2d(")
	after = prepare[first_texture:]
	returns = [index for index in range(len(after)) if after.startswith("return FALSE;", index)]
	assert returns, "no failure path after the textures"
	for index in returns:
		before = after[max(0, index - 200):index]
		assert "bg_overlay_release();" in before, after[index - 200:index]
	# (the stand-ins are checked too)
	assert "!BGFX_HANDLE_IS_VALID(bg.overlay_picture) || !BGFX_HANDLE_IS_VALID(bg.overlay_plasma) ||" in prepare
	assert "!BGFX_HANDLE_IS_VALID(bg.overlay_medals)" in prepare


def test_replacements_keep_the_old_texture_on_failure():
	source = read()
	replace = function_body(source, "static BOOL bg_overlay_replace(")
	assert "if (!BGFX_HANDLE_IS_VALID(replacement))\n\t\treturn FALSE;" in replace
	assert "bgfx_destroy_texture(*texture);" in replace
	draw = function_body(source, "int xgpu_bgfx_overlay_draw(")
	for texture in ("overlay_picture", "overlay_plasma", "overlay_medals"):
		assert f"bg_overlay_replace(&bg.{texture}, bgfx_create_texture_2d(" in draw, texture
		# (no replacement destroys the old texture itself before the new one is made)
		assert f"bgfx_destroy_texture(bg.{texture})" not in draw, texture
