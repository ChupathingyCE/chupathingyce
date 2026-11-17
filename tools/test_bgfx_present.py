"""Source checks for the bgfx renderer's present (#1047).

bg_blit cleared the window to black, then drew the picture with four of the
frame's transient vertices. A busy view could use those up first, and the
present then showed only its clear: the whole window, HUD included, went
black for as long as that view lasted. The present's quad and the channel
clear's are vertex buffers of their own now, made once, so a frame that runs
out of transient room loses some of its draws but still shows its picture,
and the draws it loses are counted (debug.gpu_stats) and logged once.
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


def test_present_needs_no_transient_vertices():
	blit = function_body(read(), "static void bg_blit(")
	assert "transient" not in blit
	assert "bgfx_set_dynamic_vertex_buffer(0, bg.present_quad, 0, 4);" in blit
	# (its corners written again only when the letterbox changes)
	assert "bgfx_update_dynamic_vertex_buffer(bg.present_quad" in blit


def test_channel_clear_needs_no_transient_vertices():
	clear = function_body(read(), "static void bg_clear_channels(")
	assert "transient" not in clear
	assert "bgfx_set_vertex_buffer(0, bg.full_quad, 0, 4);" in clear


def test_quads_made_at_start():
	start = function_body(read(), "static BOOL bg_initialize(")
	layout = start.index("bgfx_vertex_layout_end(&bg.layout);")
	assert start.index("bg.full_quad = bgfx_create_vertex_buffer(") > layout
	assert start.index("bg.present_quad = bgfx_create_dynamic_vertex_buffer(4, &bg.layout") > layout


def test_starved_draws_counted():
	source = read()
	draw = function_body(source, "static void bg_draw(")
	assert "bg.stat_starved++;" in draw
	assert "bg.stat_starved / 60" in function_body(source, "static void bg_present(")


def test_overlay_room_left_by_the_scene():
	source = read()
	assert "#define BG_OVERLAY_RESERVE_VERTICES" in source
	draw = function_body(source, "static void bg_draw(")
	assert "vertex_count + BG_OVERLAY_RESERVE_VERTICES" in draw
