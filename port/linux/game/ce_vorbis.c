/*
CE_VORBIS.C

Halo PC's Ogg Vorbis sounds decoded to 16-bit PCM (ce_resources.c), with
stb_vorbis (port/third_party/stb).
*/

#ifdef HALO_CUSTOM_EDITION

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include <stdlib.h>
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
/* (port/third_party/stb, a game include directory: port/linux/port.json) */
#include "stb_vorbis.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

/* the most samples per channel a sound's permutation is decoded to (as
ce_resources.c keeps no more), and the most channels (the game's sounds are
mono or stereo) */
#define CE_VORBIS_MAXIMUM_FRAMES 0x1000000
#define CE_VORBIS_MAXIMUM_CHANNELS 2

/* an Ogg Vorbis stream's samples, interleaved 16-bit (malloc'd: free them),
their channels and rate; the count of samples per channel, or -1. As
stb_vorbis_decode_memory, but a stream of more channels than a sound has, or
longer than one is kept, is refused as it is found to be, not decoded whole
first (a map's sounds are anyone's) */
int ce_vorbis_decode(const unsigned char *data, int size, int *channels, int *sample_rate, short **samples)
{
	int error = 0;
	stb_vorbis *vorbis = stb_vorbis_open_memory(data, size, &error, NULL);
	short *buffer;
	int frames = 0;
	int capacity, used = 0;

	*samples = NULL;
	if (!vorbis)
		return -1;
	*channels = vorbis->channels;
	*sample_rate = (int)vorbis->sample_rate;
	if (vorbis->channels < 1 || vorbis->channels > CE_VORBIS_MAXIMUM_CHANNELS)
	{
		stb_vorbis_close(vorbis);
		return -1;
	}
	capacity = vorbis->channels * 4096;
	buffer = malloc((size_t)capacity * sizeof(*buffer));
	while (buffer)
	{
		int count = stb_vorbis_get_frame_short_interleaved(vorbis, vorbis->channels, buffer + used, capacity - used);

		if (!count)
		{
			*samples = buffer;
			stb_vorbis_close(vorbis);
			return frames;
		}
		frames += count;
		used += count * vorbis->channels;
		if (frames > CE_VORBIS_MAXIMUM_FRAMES)
			break;
		if (used + vorbis->channels * 4096 > capacity)
		{
			short *larger = realloc(buffer, (size_t)capacity * 2 * sizeof(*buffer));

			if (!larger)
				break;
			buffer = larger;
			capacity *= 2;
		}
	}
	free(buffer);
	stb_vorbis_close(vorbis);
	return -1;
}

/* the samples ce_vorbis_decode gave (freed as stb_vorbis allocated them: the
game's units free through its own allocator) */
void ce_vorbis_free(short *samples)
{
	free(samples);
}

#endif
