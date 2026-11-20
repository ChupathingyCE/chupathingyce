"""Source checks for the bgfx renderer's Vulkan pipeline log (#1367).

bgfx's Vulkan renderer makes a pipeline the first time a run draws with a
draw state, in the frame, and the driver's compile can take tens of
milliseconds. Nothing logged that: debug.gpu_stats's slow frames counted
Metal's pipelines only. bgfx's cache callbacks bracket each one (its data
asked for, then handed back), so they time it. A map's total is logged when
the next map loads, and a slow frame's are in its debug.gpu_stats line.

The cache folder's files are renamed into place with SDL_RenamePath:
Windows' rename does not replace a file that is there, so a pipeline whose
data changed (after a driver update) kept the old file, which the driver
turns down, and was made from scratch in every run.
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


def test_pipeline_counted_and_timed_by_the_callbacks():
	source = read()
	size = function_body(source, "static uint32_t bg_callback_cache_read_size(")
	# (counted before the folder check: a run without one makes them too)
	assert size.index("bg_pipelines.started = stats_seconds();") < size.index("if (!*bg_jobs.folder)")
	assert "bg.account.pipelines++;" in size
	read_body = function_body(source, "static bool bg_callback_cache_read(")
	assert "bg_pipelines.cached++;" in read_body
	write = function_body(source, "static void bg_callback_cache_write(")
	assert "bg_pipelines.written++;" in write
	assert "bg.account.pipeline_seconds += seconds;" in write
	# (the time ends after the write, which is in the frame too)
	assert write.index("bg_pipeline_write(") < write.index("stats_seconds() - bg_pipelines.started")


def test_map_total_logged_at_the_next_map():
	source = read()
	warm = function_body(source, "static void bg_warm(")
	assert warm.index("bg_pipelines_log(bg_jobs.map);") < warm.index("snprintf(bg_jobs.map, sizeof(bg_jobs.map), \"%s\", map);")
	log = function_body(source, "static void bg_pipelines_log(")
	assert "bg_vulkan" in log
	assert "Vulkan pipelines" in log


def test_slow_frame_line_has_vulkan_pipelines_and_frame_time():
	source = read()
	account = function_body(source, "static void bg_frame_account(")
	assert "bg.account.pipelines_cached" in account
	assert "bg.account.pipelines_written" in account
	assert "bg.account.frame_seconds" in account
	present = function_body(source, "static void bg_present(")
	assert "bg.account.frame_seconds += frame_end - frame_start;" in present
	assert "bg_pipelines_frame_end();" in present


def test_cache_files_replaced_on_windows():
	source = read()
	pipeline = function_body(source, "static BOOL bg_pipeline_write(")
	assert "SDL_RenamePath(temporary, path)" in pipeline
	assert "rename(temporary" not in pipeline.replace("SDL_RenamePath(temporary", "")
	shader = function_body(source, "static void bg_cache_write(")
	assert "SDL_RenamePath(temporary, path)" in shader
	assert "rename(temporary" not in shader.replace("SDL_RenamePath(temporary", "")
