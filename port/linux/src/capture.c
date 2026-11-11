/*
CAPTURE.C

Screenshots (capture.h). The ask comes from the input (xinput_sdl.c), the
frame from the renderer once the GPU has it (d3d8_gl.c, d3d8_bgfx.inl), and
the PNG is made and written here, on a thread of its own so that the game's
frame never waits for the encoder or the disk. The thread starts with the
first screenshot and then waits for the next; a game that never takes one
never starts it.
*/

#include "platform.h"
#include "port_config.h"
#include "port_folders.h"
#include "capture.h"

#ifdef HALO_CAPTURE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STBIW_ASSERT(x) ((void)0)
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "../../third_party/stb/stb_image_write.h"

/* the frames waiting for the writer: a few at once, more are dropped */
#define CAPTURE_QUEUE 4

struct capture_job
{
	unsigned char *pixels;
	/* (or the renderer's, lent until copied: capture_screenshot_lend) */
	const unsigned char *lent;
	volatile int *released;
	long width, height, pitch;
	int bgra, bottom_up;
	char name[160];
};

static struct
{
	pthread_mutex_t lock;
	pthread_cond_t wake;
	int started;
	struct capture_job queue[CAPTURE_QUEUE];
	int queued;

	/* the ask (any thread): what the next frame is for */
	volatile int screenshot_wanted;
	/* the map's name, and when the ask came, for the file's name */
	char map[64];
	char wanted_name[160];

	/* the toast for the game to show */
	char notice_title[64];
	char notice_line[160];
	int notice;
} capture = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER };

/* ---------- the file's name */

/* a map's name as a file's: its last path part, without @ce and the like,
lowercase letters, digits, - and _ */
static void file_safe(char *out, unsigned long size, const char *name)
{
	const char *start = name, *part;
	unsigned long length = 0;

	for (part = name; *part; part++)
	{
		if (*part == '\\' || *part == '/')
			start = part + 1;
	}
	for (part = start; *part && *part != '@' && length + 1 < size; part++)
	{
		char c = *part;

		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
			out[length++] = c;
		else if (c == ' ' || c == '.')
			out[length++] = '_';
	}
	out[length] = '\0';
	if (!length)
		snprintf(out, size, "halo");
}

/* <map>_<YYYY-MM-DD_HH-MM-SS>, at the moment of the ask */
void capture_file_name(char *name, unsigned long size)
{
	char map[64], when[32];
	time_t now = time(NULL);
	struct tm *local;

	pthread_mutex_lock(&capture.lock);
	file_safe(map, sizeof(map), capture.map[0] ? capture.map : "menu");
	pthread_mutex_unlock(&capture.lock);
	/* (the input's thread alone asks) */
	local = localtime(&now);
	if (!local || !strftime(when, sizeof(when), "%Y-%m-%d_%H-%M-%S", local))
		snprintf(when, sizeof(when), "%lld", (long long)now);
	snprintf(name, size, "%s_%s", map, when);
}

void capture_notice(const char *title, const char *line)
{
	pthread_mutex_lock(&capture.lock);
	snprintf(capture.notice_title, sizeof(capture.notice_title), "%s", title);
	snprintf(capture.notice_line, sizeof(capture.notice_line), "%s", line);
	capture.notice = 1;
	pthread_mutex_unlock(&capture.lock);
}

/* ---------- the writer */

static void png_write_to_file(void *context, void *data, int size)
{
	FILE *file = (FILE *)context;

	if (size > 0)
		fwrite(data, 1, (size_t)size, file);
}

static int file_exists(const char *path)
{
	FILE *file = fopen(path, "rb");

	if (!file)
		return 0;
	fclose(file);
	return 1;
}

/* the PNG, three bytes a pixel (the game uses the frame's alpha as scratch,
so it is no transparency); written beside its final name and renamed into
place, so that a half-written file is never seen */
static void write_screenshot(struct capture_job *job)
{
	const char *folder = platform_folder(_port_folder_screenshots);
	unsigned char *rgb;

	if (job->lent)
	{
		/* (the renderer's own memory: copied at once and given back) */
		job->pixels = malloc((size_t)job->pitch * (size_t)job->height);
		if (job->pixels)
			memcpy(job->pixels, job->lent, (size_t)job->pitch * (size_t)job->height);
		__atomic_store_n(job->released, 1, __ATOMIC_RELEASE);
		job->lent = NULL;
		if (!job->pixels)
		{
			capture_notice("Screenshot not saved", "Out of memory");
			return;
		}
	}
	rgb = malloc((size_t)job->width * (size_t)job->height * 3);
	char path[1024], temporary[1040], file_name[200];
	FILE *file;
	long x, y;
	int written, attempt;

	if (!rgb)
	{
		free(job->pixels);
		capture_notice("Screenshot not saved", "Out of memory");
		return;
	}
	for (y = 0; y < job->height; y++)
	{
		const unsigned char *source = job->pixels + (job->bottom_up ? job->height - 1 - y : y) * job->pitch;
		unsigned char *destination = rgb + y * job->width * 3;

		for (x = 0; x < job->width; x++, source += 4, destination += 3)
		{
			destination[0] = source[job->bgra ? 2 : 0];
			destination[1] = source[1];
			destination[2] = source[job->bgra ? 0 : 2];
		}
	}
	free(job->pixels);
	job->pixels = NULL;
	/* (two in the same second: _2, _3...) */
	snprintf(file_name, sizeof(file_name), "%s.png", job->name);
	snprintf(path, sizeof(path), "%s/%s", folder, file_name);
	for (attempt = 2; file_exists(path) && attempt < 100; attempt++)
	{
		snprintf(file_name, sizeof(file_name), "%s_%d.png", job->name, attempt);
		snprintf(path, sizeof(path), "%s/%s", folder, file_name);
	}
	snprintf(temporary, sizeof(temporary), "%s.part", path);
	file = fopen(temporary, "wb");
	written = 0;
	if (file)
	{
		stbi_write_png_compression_level = 6;
		written = stbi_write_png_to_func(png_write_to_file, file, (int)job->width, (int)job->height, 3, rgb,
			(int)(job->width * 3));
		if (fclose(file) != 0)
			written = 0;
		if (written && rename(temporary, path) != 0)
			written = 0;
		if (!written)
			remove(temporary);
	}
	free(rgb);
	if (written)
	{
		platform_log("screenshot: saved %s (%ldx%ld)", file_name, job->width, job->height);
		capture_notice("Screenshot saved", file_name);
	}
	else
	{
		platform_log("screenshot: could not write %s", file_name);
		capture_notice("Screenshot not saved", "Could not write the file");
	}
}

static void *writer_main(void *unused)
{
	(void)unused;
	pthread_mutex_lock(&capture.lock);
	for (;;)
	{
		struct capture_job job;

		while (!capture.queued)
			pthread_cond_wait(&capture.wake, &capture.lock);
		job = capture.queue[0];
		memmove(capture.queue, capture.queue + 1, sizeof(capture.queue[0]) * (size_t)(capture.queued - 1));
		capture.queued--;
		pthread_mutex_unlock(&capture.lock);
		write_screenshot(&job);
		pthread_mutex_lock(&capture.lock);
	}
	return NULL;
}

/* ---------- public code */

void capture_request_screenshot(void)
{
	char name[160];

	capture_file_name(name, sizeof(name));
	pthread_mutex_lock(&capture.lock);
	snprintf(capture.wanted_name, sizeof(capture.wanted_name), "%s", name);
	capture.screenshot_wanted = 1;
	pthread_mutex_unlock(&capture.lock);
}

int capture_screenshot_wanted(void)
{
	return capture.screenshot_wanted;
}

int capture_screenshot_take(void)
{
	int wanted;

	/* (the renderer asks every frame: no lock until there is an ask) */
	if (!capture.screenshot_wanted)
		return 0;
	pthread_mutex_lock(&capture.lock);
	wanted = capture.screenshot_wanted;
	capture.screenshot_wanted = 0;
	pthread_mutex_unlock(&capture.lock);
	return wanted;
}

static int queue_job(unsigned char *pixels, const unsigned char *lent, volatile int *released, long width,
	long height, long pitch, int bgra, int bottom_up)
{
	int queued = 0;

	/* (the folder is made here, on the game's thread, as every other of
	platform_folder's callers) */
	platform_folder(_port_folder_screenshots);
	pthread_mutex_lock(&capture.lock);
	if (!capture.started)
	{
		pthread_t thread;

		if (pthread_create(&thread, NULL, writer_main, NULL) == 0)
		{
			pthread_detach(thread);
			capture.started = 1;
		}
	}
	if (capture.started && capture.queued < CAPTURE_QUEUE && width > 0 && height > 0)
	{
		struct capture_job *job = &capture.queue[capture.queued++];

		job->pixels = pixels;
		job->lent = lent;
		job->released = released;
		job->width = width;
		job->height = height;
		job->pitch = pitch;
		job->bgra = bgra;
		job->bottom_up = bottom_up;
		snprintf(job->name, sizeof(job->name), "%s", capture.wanted_name);
		pthread_cond_signal(&capture.wake);
		queued = 1;
	}
	pthread_mutex_unlock(&capture.lock);
	if (!queued)
		platform_log("screenshot: dropped (%d waiting)", CAPTURE_QUEUE);
	return queued;
}

void capture_screenshot_submit(unsigned char *pixels, long width, long height, long pitch, int bgra, int bottom_up)
{
	if (pixels && !queue_job(pixels, NULL, NULL, width, height, pitch, bgra, bottom_up))
		free(pixels);
}

int capture_screenshot_lend(const unsigned char *pixels, long width, long height, long pitch, int bgra,
	int bottom_up, volatile int *released)
{
	*released = 0;
	if (pixels && queue_job(NULL, pixels, released, width, height, pitch, bgra, bottom_up))
		return 1;
	*released = 1;
	return 0;
}

void capture_set_map(const char *name)
{
	/* (a try: the game never waits on the writer) */
	if (pthread_mutex_trylock(&capture.lock) != 0)
		return;
	snprintf(capture.map, sizeof(capture.map), "%s", name ? name : "");
	pthread_mutex_unlock(&capture.lock);
}

int capture_take_notice(char *title, unsigned long title_size, char *line, unsigned long line_size)
{
	int taken = 0;

	if (!capture.notice || pthread_mutex_trylock(&capture.lock) != 0)
		return 0;
	if (capture.notice)
	{
		snprintf(title, title_size, "%s", capture.notice_title);
		snprintf(line, line_size, "%s", capture.notice_line);
		capture.notice = 0;
		taken = 1;
	}
	pthread_mutex_unlock(&capture.lock);
	return taken;
}

#endif
