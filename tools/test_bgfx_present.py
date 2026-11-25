"""Source checks for the bgfx renderer's present (#1047).

bg_blit cleared the window to black, then drew the picture with four of the
frame's transient vertices. A busy view could use those up first, and the
present then showed only its clear: the whole window, HUD included, went
black for as long as that view lasted. The present's quad and the channel
clear's are vertex buffers of their own now, made once, so a frame that runs
out of transient room still shows its picture.

A draw past the frame's transient vertices or indices (SPV3's b30, #1801)
is drawn from buffers of its own, made for it and let go after its submit,
rather than left out; only a draw bgfx has no buffer left for is left out.
Both are counted (debug.gpu_stats) and logged once.
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


def test_draws_past_transient_room_get_their_own_buffers():
	"""#1801: a dense map's draws past the transient buffers are drawn, from
	buffers of their own, rather than dropped"""
	draw = function_body(read(), "static void bg_draw(")
	short = draw.index("bgfx_get_avail_transient_vertex_buffer((uint32_t)(vertex_count + BG_OVERLAY_RESERVE_VERTICES)")
	made = draw.index("own_vertices = bgfx_create_vertex_buffer(memory, &bg.layout, BGFX_BUFFER_NONE);")
	assert short < made
	assert draw.index("own_indices = bgfx_create_index_buffer(memory, BGFX_BUFFER_NONE);") > short
	# (immediate vertices copied, a stream's converted, as the transient path's)
	assert "memcpy(memory->data, device.immediate_vertices, vertex_count * BG_VERTEX_BYTES);" in draw
	assert "bg_vertices_convert(memory->data, declaration, first, vertex_count);" in draw
	assert "bg_indices(type, indices, indices ? index_count : vertex_count, rebase, (WORD *)memory->data);" in draw
	# (set in the transient ones' place, and let go after the submit)
	assert "bgfx_set_vertex_buffer(0, own_vertices, 0, (uint32_t)vertex_count);" in draw
	assert "bgfx_set_index_buffer(own_indices, 0, (uint32_t)generated);" in draw
	submit = draw.index("bgfx_submit(bg.view, handle, 0, BGFX_DISCARD_ALL);")
	assert draw.index("bgfx_destroy_vertex_buffer(own_vertices);", submit) > submit
	assert draw.index("bgfx_destroy_index_buffer(own_indices);", submit) > submit
	# (left out only when no buffer could be made; one made is let go then)
	starved = draw.index("if (starved)")
	assert starved < draw.index("bg.stat_starved++;", starved) < draw.index("return;", starved)
	assert draw.index("bgfx_destroy_vertex_buffer(own_vertices);", starved) < draw.index("return;", starved)
	assert "bg.stat_overflow++;" in draw
	assert "bg.stat_overflow / 60" in function_body(read(), "static void bg_present(")


def test_transient_size_for_tests():
	start = function_body(read(), "static BOOL bg_start(")
	assert "init.limits.maxTransientVbSize = 64u << 20;" in start
	assert 'getenv("HALO_BGFX_TRANSIENT_MB")' in start
