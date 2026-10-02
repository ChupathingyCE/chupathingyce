/*
CE_VORBIS.C

Halo PC's Ogg Vorbis sounds decoded to 16-bit PCM (ce_resources.c), with
stb_vorbis (port/third_party/stb).
*/

#ifdef HALO_64BIT

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
/* (port/third_party/stb, a game include directory: port/linux/port.json) */
#include "stb_vorbis.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

/* an Ogg Vorbis stream's samples, interleaved 16-bit (malloc'd: free them),
their channels and rate; the count of samples per channel, or -1 */
int ce_vorbis_decode(const unsigned char *data, int size, int *channels, int *sample_rate, short **samples)
{
	return stb_vorbis_decode_memory(data, size, channels, sample_rate, samples);
}

/* the samples ce_vorbis_decode gave (freed as stb_vorbis allocated them: the
game's units free through its own allocator) */
void ce_vorbis_free(short *samples)
{
	free(samples);
}

#endif
