/*
CE_VORBIS.C

Halo PC's Ogg Vorbis sounds decoded to 16-bit PCM (ce_resources.c), with
stb_vorbis (port/third_party/stb).
*/

#ifdef HALO_CUSTOM_EDITION

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include <math.h>
#include <stdlib.h>
#include <string.h>
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
	if (buffer)
		free(buffer);
	stb_vorbis_close(vorbis);
	return -1;
}

/* the half-band low pass a sound is brought to half or twice its rate
through: a Kaiser-windowed sinc cut off at a quarter of the higher rate
(11,025 Hz between 44.1 and 22.05 kHz), flat to about 9.5 kHz and more than
70 dB down from about 12.5 kHz. Every other tap but the middle one is zero,
so only the odd ones are kept: ce_vorbis_half_band[i] is the tap 2i+1 away
from the middle, on either side; the middle one is a half */
#define CE_VORBIS_HALF_BAND_TAPS 24
#define CE_VORBIS_HALF_BAND_BETA 7.0

static double ce_vorbis_half_band[CE_VORBIS_HALF_BAND_TAPS];
static int ce_vorbis_half_band_ready;

/* the modified Bessel function I0, by its series */
static double ce_vorbis_bessel_i0(double x)
{
	double sum = 1.0, term = 1.0;
	int k;

	for (k = 1; k < 50; k++)
	{
		term *= (x / (2.0 * k)) * (x / (2.0 * k));
		sum += term;
		if (term < sum * 1e-12)
			break;
	}
	return sum;
}

static void ce_vorbis_half_band_make(void)
{
	double const pi = 3.14159265358979323846;
	int const half = 2 * CE_VORBIS_HALF_BAND_TAPS - 1;
	double total = 0.0;
	int i;

	if (ce_vorbis_half_band_ready)
		return;
	for (i = 0; i < CE_VORBIS_HALF_BAND_TAPS; i++)
	{
		int n = 2 * i + 1;
		double ratio = (double)n / (double)(half + 1);
		double window = ce_vorbis_bessel_i0(CE_VORBIS_HALF_BAND_BETA * sqrt(1.0 - ratio * ratio)) /
			ce_vorbis_bessel_i0(CE_VORBIS_HALF_BAND_BETA);

		ce_vorbis_half_band[i] = sin(pi * n / 2.0) / (pi * n) * window;
		total += 2.0 * ce_vorbis_half_band[i];
	}
	/* (a gain of exactly one at 0 Hz: the middle tap's half and the
	others' half) */
	for (i = 0; i < CE_VORBIS_HALF_BAND_TAPS; i++)
		ce_vorbis_half_band[i] *= 0.5 / total;
	ce_vorbis_half_band_ready = 1;
}

/* a sample of one channel, the ends mirrored (so a sound keeps its level up
to its first and last samples) */
static int ce_vorbis_sample(const short *in, int frames, int channels, int channel, int frame)
{
	while (frame < 0 || frame >= frames)
	{
		if (frame < 0)
			frame = -frame;
		if (frame >= frames)
			frame = 2 * (frames - 1) - frame;
		if (frames == 1)
			frame = 0;
	}
	return in[frame * channels + channel];
}

static short ce_vorbis_clamp(double value)
{
	value += value < 0.0 ? -0.5 : 0.5;
	if (value > 32767.0)
		return 32767;
	if (value < -32768.0)
		return -32768;
	return (short)value;
}

/* samples ce_vorbis_decode gave at twice the rate wanted, or half it (a
44.1 kHz mono sound, which the game plays at 22.05 kHz as the Xbox's
channels do, or 44.1 kHz Ogg Vorbis in a 22 kHz sound, which some tools
built: battlecraft_hd's), brought to that rate so they play at their pitch,
through the half-band low pass: halved (each second sample of the filtered
sound kept), or doubled (a filtered sample between each two). The new count
of samples per channel, or -1 (the samples are then as they were) */
int ce_vorbis_resample(short **samples, int frames, int channels, int rate, int wanted)
{
	short *in = *samples;
	int frame, channel, tap;

	if (frames <= 0 || channels < 1 || channels > CE_VORBIS_MAXIMUM_CHANNELS)
		return -1;
	if (rate == wanted)
		return frames;
	ce_vorbis_half_band_make();
	if (rate == wanted * 2)
	{
		int count = frames / 2;
		short *copy;

		if (count <= 0)
			return -1;
		/* (written in place, from a copy) */
		copy = malloc((size_t)frames * (size_t)channels * sizeof(*copy));
		if (!copy)
			return -1;
		memcpy(copy, in, (size_t)frames * (size_t)channels * sizeof(*copy));
		for (frame = 0; frame < count; frame++)
		{
			for (channel = 0; channel < channels; channel++)
			{
				int middle = 2 * frame;
				double sum = 0.5 * copy[middle * channels + channel];

				for (tap = 0; tap < CE_VORBIS_HALF_BAND_TAPS; tap++)
				{
					int offset = 2 * tap + 1;

					sum += ce_vorbis_half_band[tap] *
						(ce_vorbis_sample(copy, frames, channels, channel, middle - offset) +
						ce_vorbis_sample(copy, frames, channels, channel, middle + offset));
				}
				in[frame * channels + channel] = ce_vorbis_clamp(sum);
			}
		}
		free(copy);
		return count;
	}
	if (rate * 2 == wanted && frames <= CE_VORBIS_MAXIMUM_FRAMES / 2)
	{
		short *out = malloc((size_t)frames * 2 * (size_t)channels * sizeof(*out));

		if (!out)
			return -1;
		for (frame = 0; frame < frames; frame++)
		{
			for (channel = 0; channel < channels; channel++)
			{
				double sum = 0.0;

				/* (the samples between: the odd taps, doubled for the
				zeros put between the samples) */
				for (tap = 0; tap < CE_VORBIS_HALF_BAND_TAPS; tap++)
				{
					sum += 2.0 * ce_vorbis_half_band[tap] *
						(ce_vorbis_sample(in, frames, channels, channel, frame - tap) +
						ce_vorbis_sample(in, frames, channels, channel, frame + 1 + tap));
				}
				out[(2 * frame) * channels + channel] = in[frame * channels + channel];
				out[(2 * frame + 1) * channels + channel] = ce_vorbis_clamp(sum);
			}
		}
		free(in);
		*samples = out;
		return frames * 2;
	}
	return -1;
}

/* the samples ce_vorbis_decode gave (freed as stb_vorbis allocated them: the
game's units free through its own allocator, which takes no NULL: debug_free) */
void ce_vorbis_free(short *samples)
{
	if (samples)
		free(samples);
}

#endif
