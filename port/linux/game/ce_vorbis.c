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
(11,025 Hz between 44.1 and 22.05 kHz), flat to about 10 kHz and more than
80 dB down from about 12.5 kHz. Every other tap but the middle one is zero,
so only the odd ones are kept: ce_vorbis_half_band[i] is the tap 2i+1 away
from the middle, on either side; the middle one is a half. (float: the
original Xbox does this at a map's load too, on its x87) */
#define CE_VORBIS_HALF_BAND_TAPS 24
#define CE_VORBIS_HALF_BAND_BETA 7.0

static float ce_vorbis_half_band[CE_VORBIS_HALF_BAND_TAPS];
/* the shorter filters for a sample near an end, where only its first k
taps a side reach real samples: the halfway point of the polynomial through
those 2k samples (Lagrange's), whose ce_vorbis_short_taps[k][i] are the
weights of the two samples 2i+1 away (halving: half of them, and the middle
sample a half). The shortest is the straight line between two samples, and
each is exact for a polynomial of its degree: a low tone keeps its shape */
static float ce_vorbis_short_taps[CE_VORBIS_HALF_BAND_TAPS][CE_VORBIS_HALF_BAND_TAPS];
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
	double taps[CE_VORBIS_HALF_BAND_TAPS];
	double total = 0.0;
	int i, k;

	if (ce_vorbis_half_band_ready)
		return;
	for (i = 0; i < CE_VORBIS_HALF_BAND_TAPS; i++)
	{
		int n = 2 * i + 1;
		double ratio = (double)n / (double)(half + 1);
		double window = ce_vorbis_bessel_i0(CE_VORBIS_HALF_BAND_BETA * sqrt(1.0 - ratio * ratio)) /
			ce_vorbis_bessel_i0(CE_VORBIS_HALF_BAND_BETA);

		taps[i] = sin(pi * n / 2.0) / (pi * n) * window;
		total += 2.0 * taps[i];
	}
	/* (a gain of exactly one at 0 Hz: the middle tap's half and the
	others' half) */
	for (i = 0; i < CE_VORBIS_HALF_BAND_TAPS; i++)
		ce_vorbis_half_band[i] = (float)(taps[i] * 0.5 / total);
	for (k = 1; k < CE_VORBIS_HALF_BAND_TAPS; k++)
	{
		for (i = 0; i < k; i++)
		{
			/* (the samples at +-(j + 1/2) from the halfway point) */
			double weight = 1.0, node = i + 0.5;
			int j;

			for (j = 0; j < k; j++)
			{
				double other = j + 0.5;

				if (j != i)
					weight *= (0.0 - other) / (node - other);
				weight *= (0.0 + other) / (node + other);
			}
			ce_vorbis_short_taps[k][i] = (float)weight;
		}
	}
	ce_vorbis_half_band_ready = 1;
}

/* a piece's samples of one channel, with its neighbors' beside it: frame -1
is the last of `before`, frame `frames` the first of `after` (the caller
reads only frames that are there) */
struct ce_vorbis_piece
{
	const short *in;
	int frames;
	const short *before;
	int before_frames;
	const short *after;
	int after_frames;
	int channels;
};

static float ce_vorbis_piece_sample(const struct ce_vorbis_piece *piece, int channel, int frame)
{
	if (frame < 0)
		return piece->before[(piece->before_frames + frame) * piece->channels + channel];
	if (frame >= piece->frames)
		return piece->after[(frame - piece->frames) * piece->channels + channel];
	return piece->in[frame * piece->channels + channel];
}

static short ce_vorbis_clamp(float value)
{
	value += value < 0.0f ? -0.5f : 0.5f;
	if (value > 32767.0f)
		return 32767;
	if (value < -32768.0f)
		return -32768;
	return (short)value;
}

static int ce_vorbis_minimum(int a, int b)
{
	return a < b ? a : b;
}

/* As ce_vorbis_resample, a piece of a longer sound: `before` the last
`before_frames` of the piece played before it, `after` the first
`after_frames` of the one played after it, at its rate and channels (NULL,
0 where there is none; a looping sound's own end and start, to loop
seamlessly; silence for a sound played from or into silence). The filter
reaches 47 frames either side when halving, 24 when doubling. Where it
would reach past what there is, a shorter one that reaches only real
samples is used (ce_vorbis_short_taps): no sample is made up, so pieces
brought to their rate alone still join without a step */
int ce_vorbis_resample_piece(short **samples, int frames, int channels, int rate, int wanted,
	const short *before, int before_frames, const short *after, int after_frames)
{
	struct ce_vorbis_piece piece;
	short *in = *samples;
	int frame, channel, tap;

	if (frames <= 0 || channels < 1 || channels > CE_VORBIS_MAXIMUM_CHANNELS)
		return -1;
	if (rate == wanted)
		return frames;
	ce_vorbis_half_band_make();
	piece.frames = frames;
	piece.channels = channels;
	piece.before = before;
	piece.before_frames = before ? before_frames : 0;
	piece.after = after;
	piece.after_frames = after ? after_frames : 0;
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
		piece.in = copy;
		for (frame = 0; frame < count; frame++)
		{
			int middle = 2 * frame;
			int reach = ce_vorbis_minimum(middle + piece.before_frames, frames - 1 - middle + piece.after_frames);
			int taps = ce_vorbis_minimum(CE_VORBIS_HALF_BAND_TAPS, (reach + 1) / 2);

			for (channel = 0; channel < channels; channel++)
			{
				float sum = 0.5f * copy[middle * channels + channel];

				if (taps == CE_VORBIS_HALF_BAND_TAPS && middle >= 2 * CE_VORBIS_HALF_BAND_TAPS &&
					middle + 2 * CE_VORBIS_HALF_BAND_TAPS < frames)
				{
					/* (away from the ends, read directly) */
					const short *at = copy + middle * channels + channel;

					for (tap = 0; tap < CE_VORBIS_HALF_BAND_TAPS; tap++)
					{
						int offset = (2 * tap + 1) * channels;

						sum += ce_vorbis_half_band[tap] * (float)(at[-offset] + at[offset]);
					}
				}
				else
				{
					float const *weights = taps == CE_VORBIS_HALF_BAND_TAPS ? NULL : ce_vorbis_short_taps[taps];

					for (tap = 0; tap < taps; tap++)
					{
						int offset = 2 * tap + 1;

						sum += (weights ? 0.5f * weights[tap] : ce_vorbis_half_band[tap]) *
							(ce_vorbis_piece_sample(&piece, channel, middle - offset) +
							ce_vorbis_piece_sample(&piece, channel, middle + offset));
					}
					if (!taps)
						sum *= 2.0f;
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
		piece.in = in;
		for (frame = 0; frame < frames; frame++)
		{
			/* (between this sample and the next: the odd taps, doubled for
			the zeros put between the samples) */
			int reach = ce_vorbis_minimum(frame + piece.before_frames,
				frames - 2 - frame + piece.after_frames);
			int taps = ce_vorbis_minimum(CE_VORBIS_HALF_BAND_TAPS, reach + 1);

			for (channel = 0; channel < channels; channel++)
			{
				float sum = 0.0f;

				if (taps == CE_VORBIS_HALF_BAND_TAPS && frame >= CE_VORBIS_HALF_BAND_TAPS &&
					frame + CE_VORBIS_HALF_BAND_TAPS < frames)
				{
					const short *at = in + frame * channels + channel;

					for (tap = 0; tap < CE_VORBIS_HALF_BAND_TAPS; tap++)
						sum += 2.0f * ce_vorbis_half_band[tap] * (float)(at[-tap * channels] + at[(tap + 1) * channels]);
				}
				else if (taps <= 0)
				{
					/* (the last sample, with nothing after it) */
					sum = in[frame * channels + channel];
				}
				else
				{
					float const *weights = taps == CE_VORBIS_HALF_BAND_TAPS ? NULL : ce_vorbis_short_taps[taps];

					for (tap = 0; tap < taps; tap++)
					{
						sum += (weights ? weights[tap] : 2.0f * ce_vorbis_half_band[tap]) *
							(ce_vorbis_piece_sample(&piece, channel, frame - tap) +
							ce_vorbis_piece_sample(&piece, channel, frame + 1 + tap));
					}
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

/* samples ce_vorbis_decode gave at twice the rate wanted, or half it (a
44.1 kHz mono sound, which the game plays at 22.05 kHz as the Xbox's
channels do, or 44.1 kHz Ogg Vorbis in a 22 kHz sound, which some tools
built: battlecraft_hd's), brought to that rate so they play at their pitch,
through the half-band low pass: halved (each second sample of the filtered
sound kept), or doubled (a filtered sample between each two). A sound
alone: ce_vorbis_resample_piece has its neighbors. The new count of samples
per channel, or -1 (the samples are then as they were) */
int ce_vorbis_resample(short **samples, int frames, int channels, int rate, int wanted)
{
	return ce_vorbis_resample_piece(samples, frames, channels, rate, wanted, NULL, 0, NULL, 0);
}

/* the samples ce_vorbis_decode gave (freed as stb_vorbis allocated them: the
game's units free through its own allocator, which takes no NULL: debug_free) */
void ce_vorbis_free(short *samples)
{
	if (samples)
		free(samples);
}

#endif
