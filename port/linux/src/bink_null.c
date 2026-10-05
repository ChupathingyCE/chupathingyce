/*
BINK_NULL.C

The Bink video SDK entry points bink_playback.c uses. There is no Bink
decoder in the native builds (the RAD SDK is proprietary), so BinkOpen reports that a movie cannot be
opened and the game skips it, exactly as it does for a missing movie file.

The prototypes match the declarations in bink_playback.c; the RAD SDK's
RADEXPLINK is __stdcall.
*/

#include "platform.h"

typedef void *(__stdcall *rad_memory_allocate_proc)(unsigned long size);
typedef void (__stdcall *rad_memory_free_proc)(void *memory);
typedef void *(__stdcall *bink_sound_system_open_proc)(unsigned long param);
typedef struct BINK *HBINK;

void __stdcall RADSetMemory(rad_memory_allocate_proc allocate, rad_memory_free_proc release)
{
	(void)allocate;
	(void)release;
}

void *__stdcall BinkOpenDirectSound(unsigned long param)
{
	(void)param;
	return NULL;
}

long __stdcall BinkSetSoundSystem(bink_sound_system_open_proc open, unsigned long param)
{
	(void)open;
	(void)param;
	return 0;
}

void __stdcall BinkSetIOSize(unsigned long io_size)
{
	(void)io_size;
}

/* the null decoder, for the builds with no player of their own; the
Android build hands the movies to the device (below) */
#ifndef HALO_ANDROID

HBINK __stdcall BinkOpen(const char *name, unsigned long flags)
{
	(void)flags;
	platform_log("Bink video is not supported; skipping \"%s\"", name ? name : "");
	return NULL;
}

/* never reached without an open movie */

void __stdcall BinkClose(HBINK bink) { (void)bink; }
long __stdcall BinkDoFrame(HBINK bink) { (void)bink; return 0; }
void __stdcall BinkNextFrame(HBINK bink) { (void)bink; }
long __stdcall BinkWait(HBINK bink) { (void)bink; return 0; }

long __stdcall BinkCopyToBuffer(HBINK bink, void *destination, long destination_pitch,
	unsigned long destination_height, unsigned long destination_x, unsigned long destination_y,
	unsigned long flags)
{
	(void)bink; (void)destination; (void)destination_pitch; (void)destination_height;
	(void)destination_x; (void)destination_y; (void)flags;
	return 0;
}

void __stdcall BinkGetSummary(HBINK bink, void *summary) { (void)bink; (void)summary; }
void __stdcall BinkGetRealtime(HBINK bink, void *realtime, unsigned long frame_count) { (void)bink; (void)realtime; (void)frame_count; }
#endif /* !HALO_ANDROID */

/* ---- Android: los videos los reproduce el lado Android ----

The port has no Bink decoder (the RAD SDK is proprietary), but the Android
build can hand a movie to the device's own player: the guest asks the host
for it (host_movie_*), the host plays the transcoded file over the game, and
the engine here reports its progress, so the engine's own playback loop
(bink_playback.c) ends the movie when it is over. Nothing is decoded here:
BinkCopyToBuffer fills the engine's texture black and the host's player draws
the picture on top of it. */
#ifdef HALO_ANDROID

#include "guest_host.h"

typedef struct BINK
{
	unsigned long Width;
	unsigned long Height;
	unsigned long Frames;
	unsigned long FrameNum;
	unsigned long LastFrameNum;
} *HBINK;

static struct BINK bink_android_movie;
static int bink_android_id;

HBINK __stdcall BinkOpen(const char *name, unsigned long flags)
{
	int movie;

	(void)flags;

	/* (an empty name, or one the port has no file for: the engine skips the
	movie, as it did before) */
	movie = name ? host_movie_open(name) : 0;
	if (!movie)
		return NULL;

	bink_android_id = movie;
	bink_android_movie.Width = host_movie_width(movie);
	bink_android_movie.Height = host_movie_height(movie);
	bink_android_movie.Frames = host_movie_frames(movie);
	bink_android_movie.FrameNum = 0;
	bink_android_movie.LastFrameNum = 0;
	platform_log("movie: the host plays it (id %d, %lux%lu, %lu frames)", movie,
		bink_android_movie.Width, bink_android_movie.Height, bink_android_movie.Frames);
	return &bink_android_movie;
}

void __stdcall BinkClose(HBINK bink)
{
	(void)bink;

	if (bink_android_id)
		host_movie_close(bink_android_id);
	bink_android_id = 0;
}

long __stdcall BinkDoFrame(HBINK bink)
{
	(void)bink;
	return 0;
}

void __stdcall BinkNextFrame(HBINK bink)
{
	if (bink)
		bink->FrameNum = host_movie_frame(bink_android_id);
}

long __stdcall BinkWait(HBINK bink)
{
	(void)bink;
	/* the host's player keeps the time; the engine must not wait on us */
	return 0;
}

long __stdcall BinkCopyToBuffer(HBINK bink, void *destination, long destination_pitch,
	unsigned long destination_height, unsigned long destination_x, unsigned long destination_y,
	unsigned long flags)
{
	(void)destination_x;
	(void)destination_y;
	(void)flags;

	/* black: the host's player covers the picture, this only keeps the
	engine's texture from showing the frame before */
	if (bink && destination)
	{
		unsigned long row;

		for (row = 0; row < destination_height; row++)
			memset((unsigned char *)destination + row * destination_pitch, 0, bink->Width * 4);
	}

	return 0;
}

void __stdcall BinkGetSummary(HBINK bink, void *summary)
{
	(void)bink;
	if (summary)
		memset(summary, 0, 0x5C);
}

void __stdcall BinkGetRealtime(HBINK bink, void *realtime, unsigned long frame_count)
{
	(void)bink;
	(void)frame_count;
	if (realtime)
		memset(realtime, 0, 0x18);
}

int bink_android_movie_playing(void)
{
	return bink_android_id != 0;
}

#endif /* HALO_ANDROID */
