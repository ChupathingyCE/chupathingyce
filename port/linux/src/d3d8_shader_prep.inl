/*
D3D8_SHADER_PREP.INL

A map's shaders made while its loading screen is up (display.prepare_shaders),
by the OpenGL renderer and bgfx's alike, so that a draw in play never stops the
game to compile one. Included at the end of d3d8_gl.c (HALO_SHADER_PREP), whose
device and renderers' state it shares.

A draw's shaders follow from its key (xgpu_shader_keys.h): the game's vertex
program and variant, and the pixel shader key. Each map has a list of the keys
its draws use: the stock maps' come with the game (xgpu_shader_keys_stock), and
every map played keeps its own in the cache folder (shader_keys/<map>.keys,
a line for each key it had not had, as its draws first use it), so a custom
map's shaders are made at its loading screen from its second load on.

As a map starts loading (d3d8_gl_map_loading, game_precache_new_map), its keys
are read, and each frame of the loading screen (D3DDevice_Present) makes as
many of their shaders and programs as PREP_LOADING_BUDGET allows:

- OpenGL: each shader's compile and each program's link asked for, and not
  waited on (xgpu_shader_cache_submit), then taken as the driver finishes them:
  with GL_KHR_parallel_shader_compile (or ARB's: Mesa's, NVIDIA's, AMD's), its
  threads compile them all at once, and the loading screen asks whether each is
  done (COMPLETION_STATUS) without waiting; without it (macOS), a compile and a
  link are done in turn, a frame's budget at a time. What is made goes into the
  shader cache (xgpu_shader_cache.c) as a draw's would.
- bgfx: each shader handed to the compiler threads (bg_shader_get), and each
  pair made a program once both are made (bg_program_get); what they make is
  kept in bgfx's cache folder as before.

bgfx's backends make a pipeline (the pair with a draw's blend, depth and
target) only at a draw: the map's first frame is drawn first, unseen, through
every shader its tags hold (port/linux/game/render_enumerate.c,
halo_gpu_warm_draws), so that those are made before it is shown.

The loading screen waits for them (d3d8_gl_map_waiting, its progress
d3d8_gl_map_progress) for PREP_WAIT_SECONDS at most from the start of the load.
What is left then is made during play at PREP_PLAY_BUDGET a frame, by a
renderer that can without waiting (bgfx's threads, a parallel GL driver); on
another, the draws make what they need, as before. Nothing waits for a shader
in a frame of play that would not have waited for it before.

debug.frame_hitches logs every frame of a map's first minute that came more
than its milliseconds after the one before it, and how many there were: the
measure of what is left to compile in play.
*/

#include "xgpu_shader_keys.h"
#include "posix.h"

#include <ctype.h>

/* a frame's time for the shaders while the loading screen is up, and during
play (on a renderer that does not wait for them: bgfx, a parallel GL driver) */
#define PREP_LOADING_BUDGET 0.025
#define PREP_PLAY_BUDGET 0.002
/* the most the loading screen waits for them, from the start of the load,
and the most a load without one does */
#define PREP_WAIT_SECONDS 5.0
#define PREP_LOAD_SECONDS 0.5
/* the most programs waiting on the driver or bgfx's threads at once, and
the longest bgfx's threads are waited for one (it failed) */
#define PREP_PENDING_MAXIMUM 4096
#define PREP_THREAD_SECONDS 30.0
/* (debug.frame_hitches: the first minute of a map) */
#define HITCH_WINDOW_SECONDS 60.0

struct prep_pending
{
	/* OpenGL: the two shaders and the program linked of them */
	GLuint vertex_shader, fragment_shader, program;
#ifdef HALO_BGFX
	/* bgfx: the key's, until both are made */
	struct vertex_shader_object *object;
	double started;
	BOOL immediate;
	int flags;
	struct nv2a_pixel_shader_key key;
#endif
};

static struct
{
	BOOL initialized, enabled;
	/* the map being prepared (its name as its file's, lower case), its keys, the next to start */
	char map[64];
	struct xgpu_shader_key_list keys;
	unsigned long next;
	/* where the keys are made before play: "at the loading screen", or
	"during the load" (a map in the cache already, loaded without one) */
	const char *where;
	/* started at the loading screen, which waits; the time it started */
	BOOL loading;
	double started;
	/* keys made, failed or found made already; of the keys */
	unsigned long done, total;
	/* made, and how long it took (logged once the map's keys are all done) */
	unsigned long made, failed;
	BOOL reported;
	/* started and not finished */
	struct prep_pending *pending;
	unsigned long pending_count;
	/* the vertex programs the game made (D3DDevice_CreateVertexShader), to find a key's */
	struct vertex_shader_object **objects;
	unsigned long object_count, object_capacity;
	/* the map now played, and its own list's writer */
	char noting_map[64];
	struct xgpu_shader_key_writer *writer;
	unsigned long flushed_frame;
} prep;

/* (debug.frame_hitches) */
static struct
{
	/* a frame this much after the one before is a hitch (-1: not read yet, 0: none) */
	double seconds;
	char map[64];
	BOOL counting;
	double last, map_start;
	unsigned long count;
	double worst, total;
} hitches = { -1.0 };

/* (debug.frame_times: every frame's time to a file, for the renderer
measurements of tools/picture_check.py's kind: one line a frame, the clock's
seconds, the milliseconds since the frame before, and the map; a "loaded"
line when a map has finished loading) */
static struct
{
	FILE *file;
	BOOL checked;
	unsigned long unflushed, lines;
} frame_times;
/* the most lines a run writes (about 10 MB; an hour at 60 Hz): past them,
one line says so and the file is closed */
#define FRAME_TIMES_MAXIMUM_LINES 250000

static double prep_seconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

static FILE *frame_times_file(void)
{
	if (!frame_times.checked)
	{
		const char *path = config_string("debug.frame_times");

		frame_times.checked = TRUE;
		if (path && *path)
		{
			frame_times.file = fopen(path, "w");
			if (!frame_times.file)
				platform_log("frame times: %s could not be made", path);
		}
	}
	return frame_times.file;
}

static void frame_times_write(const char *what, double interval, const char *map)
{
	FILE *file = frame_times_file();
	struct timespec now;

	if (!file)
		return;
	if (++frame_times.lines > FRAME_TIMES_MAXIMUM_LINES)
	{
		fprintf(file, "stopped\tafter %d lines\n", FRAME_TIMES_MAXIMUM_LINES);
		fclose(file);
		frame_times.file = NULL;
		platform_log("frame times: stopped after %d lines", FRAME_TIMES_MAXIMUM_LINES);
		return;
	}
	/* (the wall clock, so that a run's launch time compares with it) */
	clock_gettime(CLOCK_REALTIME, &now);
	fprintf(file, "%s\t%ld.%03ld\t%.3f\t%s\n", what, (long)now.tv_sec, (long)(now.tv_nsec / 1000000), interval * 1000.0,
		map);
	if (++frame_times.unflushed >= 60)
	{
		fflush(file);
		frame_times.unflushed = 0;
	}
}

static BOOL prep_renderer_up(void)
{
#ifdef HALO_BGFX
	if (bg_active())
		return TRUE;
#endif
#ifdef HALO_CONSOLE_NATIVE_RENDERER
	if (cr_active())
		return FALSE;
#endif
	return device.gl_ready;
}

static BOOL prep_enabled(void)
{
	if (!prep.initialized)
	{
		prep.initialized = TRUE;
		prep.enabled = config_boolean("display.prepare_shaders") != 0;
	}
	return prep.enabled && prep_renderer_up();
}

/* a map's name as its list has it: its file's, without a folder or an
extension, in lower case */
static void prep_map_name(char *name, size_t size, const char *path)
{
	const char *start = path ? path : "";
	const char *at;
	size_t length = 0;

	for (at = start; *at; at++)
	{
		if (*at == '\\' || *at == '/')
			start = at + 1;
	}
	for (at = start; *at && *at != '.' && length + 1 < size; at++)
		name[length++] = (char)tolower((unsigned char)*at);
	name[length] = 0;
}

static void prep_list_path(char *path, size_t size, const char *map)
{
	snprintf(path, size, "%s/shader_keys/%s.keys", platform_folder(_port_folder_cache), map);
}

/* ---------- the game's vertex programs */

static void prep_vertex_program_made(struct vertex_shader_object *object)
{
	if (!object->instructions)
		return;
	if (prep.object_count == prep.object_capacity)
	{
		unsigned long capacity = prep.object_capacity ? prep.object_capacity * 2 : 256;
		struct vertex_shader_object **grown = realloc(prep.objects, capacity * sizeof(*grown));

		if (!grown)
			return;
		prep.objects = grown;
		prep.object_capacity = capacity;
	}
	prep.objects[prep.object_count++] = object;
}

static struct vertex_shader_object *prep_vertex_program(unsigned long long code_hash)
{
	static unsigned long last;
	unsigned long index;

	if (last < prep.object_count && prep.objects[last]->code_hash == code_hash)
		return prep.objects[last];
	for (index = 0; index < prep.object_count; index++)
	{
		if (prep.objects[index]->code_hash == code_hash)
		{
			last = index;
			return prep.objects[index];
		}
	}
	return NULL;
}

/* ---------- a map's own list */

/* a draw's key, the first time the map draws with its program: into the
map's list */
static void prep_note(unsigned long *noted, struct vertex_shader_object *object, BOOL immediate, BOOL lit,
	BOOL plasma, BOOL depth_bias, unsigned long packed_mask, const struct nv2a_pixel_shader_key *pixel)
{
	struct xgpu_program_key key;

	*noted = shader_prep_serial;
	if (!prep.writer || !object)
		return;
	memset(&key, 0, sizeof(key));
	key.vertex_code = object->code_hash;
	key.flags = (immediate ? _xgpu_key_immediate_bit : 0) | (lit ? _xgpu_key_lit_bit : 0) |
		(plasma ? _xgpu_key_plasma_bit : 0) | (depth_bias ? _xgpu_key_depth_bias_bit : 0);
	key.packed_mask = immediate ? 0 : packed_mask;
	key.pixel = *pixel;
	xgpu_shader_key_writer_add(prep.writer, &key);
}

/* the map now played: its own list opened (scenario_load) */
static void prep_noting_start(const char *map)
{
	char path[1100];

	if (!strcmp(prep.noting_map, map) && prep.writer)
		return;
	xgpu_shader_key_writer_close(prep.writer);
	prep.writer = NULL;
	snprintf(prep.noting_map, sizeof(prep.noting_map), "%s", map);
	/* (every program noted again for this map) */
	shader_prep_serial++;
	if (!*map || !*platform_folder(_port_folder_cache))
		return;
	snprintf(path, sizeof(path), "%s/shader_keys", platform_folder(_port_folder_cache));
	posix_make_directory(path);
	prep_list_path(path, sizeof(path), map);
	prep.writer = xgpu_shader_key_writer_open(path);
}

/* ---------- making a key's shaders */

/* the multisampling of the screen's targets now (the keys' alpha_test_samples
is the recording machine's) */
static unsigned char prep_screen_samples(void)
{
	int samples;

#ifdef HALO_BGFX
	if (bg_active())
		samples = bg_screen_samples();
	else
#endif
	{
#if defined(HALO_GL_ES)
		samples = 0;
#else
		samples = XGPU_GL_SAMPLE_MASK && anti_aliasing() == _anti_aliasing_msaa ? anti_aliasing_samples : 0;
#endif
	}
	return (unsigned char)(samples > 1 ? samples : 0);
}

static void prep_pending_add(const struct prep_pending *pending)
{
	if (prep.pending_count % 256 == 0)
	{
		struct prep_pending *grown = realloc(prep.pending, (prep.pending_count + 256) * sizeof(*grown));

		if (!grown)
			return;
		prep.pending = grown;
	}
	prep.pending[prep.pending_count++] = *pending;
}

static void prep_pending_remove(unsigned long index)
{
	prep.pending[index] = prep.pending[--prep.pending_count];
}

static struct program_entry *program_find(GLuint vertex_shader, GLuint fragment_shader)
{
	unsigned long hash = (vertex_shader * 2654435761UL) ^ fragment_shader;
	struct program_entry *entry;

	for (entry = program_buckets[hash % PROGRAM_BUCKETS]; entry; entry = entry->next)
	{
		if (entry->vertex_shader == vertex_shader && entry->fragment_shader == fragment_shader)
			return entry;
	}
	return NULL;
}

/* a GL program of the pending's, finished: kept (as at start-up's warming) */
static void prep_gl_take(struct prep_pending *pending, BOOL linked)
{
	xgpu_shader_cache_finish(pending->vertex_shader, TRUE);
	xgpu_shader_cache_finish(pending->fragment_shader, TRUE);
	if (!linked || program_find(pending->vertex_shader, pending->fragment_shader))
	{
		/* (it failed, to be linked again at its draw, which logs why; or a
		draw needed it first and linked its own) */
		glDeleteProgram(pending->program);
		if (!linked)
			prep.failed++;
		return;
	}
	program_adopt(pending->vertex_shader, pending->fragment_shader, pending->program);
	xgpu_shader_cache_linked(pending->vertex_shader, pending->fragment_shader);
	prep.made++;
}

/* a key's shaders and program started; FALSE if it has none to make */
static BOOL prep_start(const struct xgpu_program_key *recorded)
{
	struct vertex_shader_object *object = prep_vertex_program(recorded->vertex_code);
	struct nv2a_pixel_shader_key key = recorded->pixel;
	BOOL immediate = (recorded->flags & _xgpu_key_immediate_bit) != 0;
	BOOL lit = (recorded->flags & _xgpu_key_lit_bit) != 0;
	BOOL plasma = (recorded->flags & _xgpu_key_plasma_bit) != 0;
	struct prep_pending pending;

	if (!object || !object->instructions)
		return FALSE;
	/* (as this machine draws it: its multisampling, its lighting) */
	if (key.alpha_test_samples)
		key.alpha_test_samples = prep_screen_samples();
#ifndef HALO_GL_ES
	key.count_samples = 0;
#endif
	memset(&pending, 0, sizeof(pending));
#ifdef HALO_BGFX
	if (bg_active())
	{
		int flags = recorded->flags & _xgpu_key_depth_bias_bit ? _bg_shader_depth_bias : 0;
		struct bg_shader *vertex, *fragment;

		/* (bgfx lights each vertex, without the Light effects) */
		key.per_pixel_lighting = 0;
		key.plasma_model = 0;
		vertex = bg_vertex_shader(object, immediate);
		fragment = bg_fragment_shader(&key, flags);
		if (vertex && fragment)
		{
			/* (both in the cache, or made before) */
			bg_program_get(vertex, fragment);
			prep.made++;
			return TRUE;
		}
		pending.object = object;
		pending.started = prep_seconds();
		pending.immediate = immediate;
		pending.flags = flags;
		pending.key = key;
		prep_pending_add(&pending);
		return TRUE;
	}
#endif
	/* (as prepare_draw draws it here: the Light effects' form only of a
	program they light, per-pixel lighting only where it is on) */
	if (plasma && (!object->plasma_model || object->plasma_lighting_failed))
	{
		plasma = FALSE;
		key.plasma_model = 0;
	}
	if (lit && (!object->lighting.lights || object->lighting_failed || !per_pixel_lighting()))
	{
		lit = FALSE;
		key.per_pixel_lighting = 0;
		key.plasma_model = 0;
	}
	pending.vertex_shader = vertex_shader_make(object, immediate, lit, plasma, recorded->packed_mask, TRUE);
	pending.fragment_shader = fragment_shader_make(&key, TRUE);
	if (!pending.vertex_shader || !pending.fragment_shader || program_find(pending.vertex_shader, pending.fragment_shader))
		return FALSE;
	pending.program = glCreateProgram();
	glAttachShader(pending.program, pending.vertex_shader);
	glAttachShader(pending.program, pending.fragment_shader);
	glLinkProgram(pending.program);
	if (xgpu_parallel_compile)
		prep_pending_add(&pending);
	else
		prep_gl_take(&pending, xgpu_program_finish(pending.program, TRUE) == 1);
	return TRUE;
}

/* the pending keys the driver or bgfx's threads have finished (wait: all of
them, waiting for the driver, a new map's start; bgfx's are let go) */
static void prep_collect(double deadline, BOOL wait)
{
	unsigned long index = 0;
	double now = prep_seconds();

	while (index < prep.pending_count)
	{
		struct prep_pending *pending = &prep.pending[index];

#ifdef HALO_BGFX
		if (pending->object)
		{
			struct bg_shader *vertex = wait ? NULL : bg_vertex_shader(pending->object, pending->immediate);
			struct bg_shader *fragment = wait ? NULL : bg_fragment_shader(&pending->key, pending->flags);

			if (vertex && fragment)
			{
				bg_program_get(vertex, fragment);
				prep.made++;
			}
			else if (!wait && now - pending->started < PREP_THREAD_SECONDS)
			{
				/* (not made yet) */
				index++;
				continue;
			}
			else if (!wait)
			{
				/* (one that fails is not made: its draws are left out, as before) */
				prep.failed++;
			}
			prep_pending_remove(index);
			continue;
		}
#endif
		{
			int linked = xgpu_program_finish(pending->program, wait);

			if (linked < 0)
			{
				index++;
				if (prep_seconds() > deadline)
					break;
				continue;
			}
			prep_gl_take(pending, linked == 1);
			prep_pending_remove(index);
		}
	}
	(void)now;
}

/* whether this renderer makes shaders without waiting for them (bgfx's
threads, a parallel GL driver): it can go on with them in play */
static BOOL prep_in_parallel(void)
{
#ifdef HALO_BGFX
	if (bg_active())
		return TRUE;
#endif
	return xgpu_parallel_compile;
}

/* each frame (D3DDevice_Present): the map's keys, as far as the frame's
budget goes */
static void prep_frame(void)
{
	double start, deadline;

	if (!prep.total || prep.reported || !prep_renderer_up())
		return;
	start = prep_seconds();
	if (prep.loading && start - prep.started > PREP_WAIT_SECONDS)
		prep.loading = FALSE;
	if (!prep.loading && !prep_in_parallel())
	{
		/* (in play, a driver that compiles as it is asked would stop the
		frame: the draws make what is left, as before) */
		platform_log("shaders: %s: %lu of %lu keys made %s in %.1f s; the rest at their draws",
			prep.map, prep.done, prep.total, prep.where, start - prep.started);
		prep.reported = TRUE;
		xgpu_shader_key_list_free(&prep.keys);
		return;
	}
	deadline = start + (prep.loading ? PREP_LOADING_BUDGET : PREP_PLAY_BUDGET);
	prep_collect(deadline, FALSE);
	while (prep.next < prep.keys.count && prep.pending_count < PREP_PENDING_MAXIMUM && prep_seconds() < deadline)
		prep_start(&prep.keys.keys[prep.next++]);
	/* (a key is done once it is started and not waited for: made, found
	made, or with nothing to make here) */
	prep.done = prep.next > prep.pending_count ? prep.next - prep.pending_count : 0;
	if (prep.next >= prep.keys.count && !prep.pending_count)
	{
		platform_log("shaders: %s: %lu keys, %lu programs made%s, %lu failed, in %.1f s %s", prep.map, prep.total,
			prep.made, prep_in_parallel() ? " in parallel" : "", prep.failed, prep_seconds() - prep.started,
			prep.loading ? prep.where : "(some in play)");
		prep.reported = TRUE;
		prep.loading = FALSE;
		prep.done = prep.total;
		xgpu_shader_key_list_free(&prep.keys);
	}
}

/* ---------- the game's side (game_precache_new_map, scenario_load) */

static void prep_begin(const char *map, BOOL loading)
{
	const char *stock;
	unsigned long stock_size = 0, before;
	char path[1100];
	FILE *file;

	/* (a map's keys left from the one before are dropped, what it has
	pending taken) */
	prep_collect(0.0, TRUE);
	xgpu_shader_key_list_free(&prep.keys);
	snprintf(prep.map, sizeof(prep.map), "%s", map);
	prep.next = prep.done = prep.total = prep.made = prep.failed = 0;
	prep.reported = FALSE;
	prep.loading = loading;
	prep.where = "at the loading screen";
	prep.started = prep_seconds();
	if (!*map)
		return;
	if ((stock = xgpu_shader_keys_stock(&stock_size)) != NULL)
		xgpu_shader_keys_parse(stock, stock_size, map, &prep.keys);
	before = prep.keys.count;
	prep_list_path(path, sizeof(path), map);
	if ((file = fopen(path, "rb")) != NULL)
	{
		char *text = NULL;
		long size;

		if (fseek(file, 0, SEEK_END) == 0 && (size = ftell(file)) > 0 && size < (8L << 20) &&
			fseek(file, 0, SEEK_SET) == 0 && (text = malloc((size_t)size)) != NULL &&
			fread(text, 1, (size_t)size, file) == (size_t)size)
		{
			unsigned long whole = (unsigned long)size;

			/* (not a last line cut short by a crash) */
			while (whole && text[whole - 1] != '\n')
				whole--;
			xgpu_shader_keys_parse(text, whole, NULL, &prep.keys);
		}
		free(text);
		fclose(file);
	}
	prep.total = prep.keys.count;
	if (!prep.total)
	{
		/* (nothing to wait for) */
		prep.reported = TRUE;
		prep.loading = FALSE;
	}
	else
	{
		platform_log("shaders: %s: making %lu keys (%lu from the game's list, %lu from this machine's)%s", map,
			prep.total, before, prep.total - before, loading ? " at the loading screen" : "");
	}
}

void d3d8_gl_map_loading(const char *name)
{
	char map[64];

	if (!prep_enabled())
		return;
	prep_map_name(map, sizeof(map), name);
	prep_begin(map, TRUE);
}

float d3d8_gl_map_progress(void)
{
	if (!prep.loading || !prep.total)
		return -1.0f;
	return (float)prep.done / (float)prep.total;
}

int d3d8_gl_map_waiting(void)
{
	if (!prep.loading || !prep.total || !prep_enabled())
		return FALSE;
	if (prep.reported || prep_seconds() - prep.started > PREP_WAIT_SECONDS)
	{
		prep.loading = FALSE;
		return FALSE;
	}
	return TRUE;
}

void d3d8_gl_map_loaded(const char *name)
{
	char map[64];

	prep_map_name(map, sizeof(map), name);
	frame_times_write("loaded", 0.0, map);
	if (prep_renderer_up())
		prep_noting_start(map);
	/* (a map loaded without its loading screen, one in the cache already:
	still part of the load here, before its first frame. A renderer that
	makes them without waiting goes on with them in play; another makes
	them now, for as long as the loading screen would have waited) */
	if (prep_enabled() && strcmp(prep.map, map))
	{
		prep_begin(map, FALSE);
		if (prep.total && !prep_in_parallel())
		{
			/* (no frame is shown meanwhile: PREP_LOAD_SECONDS at the most,
			about what loading a map from the cache takes; the rest at
			their draws) */
			double start = prep_seconds();

			prep.loading = TRUE;
			prep.where = "during the load";
			while (prep.loading && !prep.reported && prep_seconds() - start < PREP_LOAD_SECONDS)
				prep_frame();
			prep.loading = FALSE;
		}
	}
	snprintf(hitches.map, sizeof(hitches.map), "%s", map);
	hitches.counting = FALSE;
	hitches.map_start = -1.0;
}

/* whether a map's first frame is drawn first through every shader, unseen
(port/linux/game/render_enumerate.c): on bgfx, whose Metal and Vulkan
pipelines are made at a draw, so that they are made before it is shown */
int halo_gpu_warm_draws(void)
{
#ifdef HALO_BGFX
	return prep_enabled() && bg_active();
#else
	return FALSE;
#endif
}

/* ---------- each frame */

static void prep_hitches(void)
{
	double now = prep_seconds(), interval;

	if (hitches.seconds < 0.0)
		hitches.seconds = config_integer("debug.frame_hitches") > 0 ? config_integer("debug.frame_hitches") / 1000.0 : 0.0;
	interval = hitches.last > 0.0 ? now - hitches.last : 0.0;
	hitches.last = now;
	frame_times_write("frame", interval, hitches.map);
	if (hitches.seconds <= 0.0 || !*hitches.map)
		return;
	if (hitches.map_start < 0.0)
	{
		/* (from the map's first frame: the one before it is the load's) */
		hitches.map_start = now;
		hitches.counting = TRUE;
		hitches.count = 0;
		hitches.worst = hitches.total = 0.0;
		return;
	}
	if (!hitches.counting)
		return;
	if (interval > hitches.seconds)
	{
		hitches.count++;
		hitches.total += interval;
		if (interval > hitches.worst)
			hitches.worst = interval;
		platform_log("frame hitch: %.1f ms, %.1f s into %s", interval * 1000.0, now - hitches.map_start, hitches.map);
	}
	if (now - hitches.map_start >= HITCH_WINDOW_SECONDS)
	{
		platform_log("frame hitches: %s's first minute had %lu over %.0f ms (worst %.1f ms, %.0f ms in all)",
			hitches.map, hitches.count, hitches.seconds * 1000.0, hitches.worst * 1000.0, hitches.total * 1000.0);
		hitches.counting = FALSE;
	}
}

/* (debug.frame_log: a line for each frame drawn once a determinism replay's
game has started, for the benchmark, tools/bench.py: the game's tick, the
frame's microseconds since the one before, and the game thread's CPU
microseconds in it, -1 where not known. The ticks are the recording's on
every build and renderer, so two runs' frames cover the same game. Based on
work by goatbutt and the OpenH1 team) */
/* (port/linux/game/determinism_check.c's) */
long determinism_check_ticks_done(void);

static struct
{
	FILE *file;
	BOOL checked;
	double last;
	long long cpu_ns;
} frame_log = { NULL, FALSE, -1.0, -1 };

static long long frame_log_thread_cpu_ns(void)
{
#ifdef CLOCK_THREAD_CPUTIME_ID
	struct timespec now;

	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) == 0)
		return (long long)now.tv_sec * 1000000000LL + (long long)now.tv_nsec;
#endif
	return -1;
}

static void prep_frame_log(void)
{
	double now;
	long long cpu_ns, cpu;
	long tick;

	if (!frame_log.checked)
	{
		const char *path = config_string("debug.frame_log");

		frame_log.checked = TRUE;
		if (path && *path)
		{
			frame_log.file = fopen(path, "w");
			if (frame_log.file)
				fprintf(frame_log.file, "# frame log 1: tick frame_us cpu_us\n");
			else
				platform_log("debug.frame_log: %s could not be made", path);
		}
	}
	if (!frame_log.file)
		return;
	now = prep_seconds();
	cpu_ns = frame_log_thread_cpu_ns();
	tick = determinism_check_ticks_done();
	if (tick >= 0 && frame_log.last > 0.0)
	{
		cpu = cpu_ns >= 0 && frame_log.cpu_ns >= 0 ? (cpu_ns - frame_log.cpu_ns) / 1000 : -1;
		fprintf(frame_log.file, "%ld %lld %lld\n", tick, (long long)((now - frame_log.last) * 1e6), cpu);
	}
	frame_log.last = now;
	frame_log.cpu_ns = cpu_ns;
}

static void prep_present(void)
{
	prep_frame();
	if (prep.writer && device.frame - prep.flushed_frame > 300)
	{
		xgpu_shader_key_writer_flush(prep.writer);
		prep.flushed_frame = device.frame;
	}
	prep_hitches();
	prep_frame_log();
}
