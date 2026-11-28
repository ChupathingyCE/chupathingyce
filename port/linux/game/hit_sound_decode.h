/*
HIT_SOUND_DECODE.H

The hit tone's samples (hit_sound.c), whole in this header so that its test
(port/linux/tests/hit_sound_test.c) builds it alone: Custom Edition's ting
found in its sounds.map (the resource sound\sfx\impulse\ting\ting: the
sound, its pitch ranges and its permutations, laid out as ce_resources.c
reads them, the samples at the file offset the first permutation gives), a
WAV file's 16-bit PCM (its start: a long file is read only as far as the
tone needs), Xbox ADPCM decoded, and any of them made one channel at the
mixer's rate. Everything read is anyone's file: every size and offset is
checked.
*/

#ifndef HIT_SOUND_DECODE_H
#define HIT_SOUND_DECODE_H

#include "content_audio.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

enum
{
	/* the longest tone played, the largest samples read from sounds.map,
	and the most of a player's file read (its start: the tone is its first
	seconds, whatever its length) */
	HIT_SOUND_MAXIMUM_SECONDS = 2,
	HIT_SOUND_MAXIMUM_BYTES = 1024 * 1024,
	HIT_SOUND_FILE_READ_BYTES = 4 * 1024 * 1024,
	/* the highest rate taken, and so the most frames a tone's samples are
	decoded to before they are brought to the mixer's */
	HIT_SOUND_MAXIMUM_RATE = 96000,
	HIT_SOUND_MAXIMUM_SOURCE_FRAMES = HIT_SOUND_MAXIMUM_RATE * HIT_SOUND_MAXIMUM_SECONDS,
	/* (dsound_sdl.c's OUTPUT_RATE: a voice there is not resampled) */
	HIT_SOUND_OUTPUT_RATE = 48000,

	/* (sound_manager.c's sound_compression) */
	HIT_SOUND_COMPRESSION_NONE = 0,
	HIT_SOUND_COMPRESSION_XBOX_ADPCM = 1,
	HIT_SOUND_COMPRESSION_OGG = 3,

	/* (ce_resources.c's: the sound, a pitch range, a permutation) */
	HIT_SOUND_SOUND_SIZE = 0xa4,
	HIT_SOUND_SAMPLE_RATE_OFFSET = 0x06,
	HIT_SOUND_ENCODING_OFFSET = 0x6c,
	HIT_SOUND_PITCH_RANGES_OFFSET = 0x98,
	HIT_SOUND_PITCH_RANGE_SIZE = 0x48,
	HIT_SOUND_PERMUTATIONS_OFFSET = 0x3c,
	HIT_SOUND_PERMUTATION_SIZE = 0x7c,
	HIT_SOUND_PERMUTATION_COMPRESSION_OFFSET = 0x28,
	HIT_SOUND_PERMUTATION_SAMPLES_OFFSET = 0x40,

	/* (dsound_sdl.c's Xbox ADPCM block: 36 bytes a channel, 64 samples) */
	HIT_SOUND_ADPCM_BLOCK_BYTES = 36,
	HIT_SOUND_ADPCM_BLOCK_SAMPLES = 64,
};

/* where a sound's samples are, and how they are kept */
struct hit_sound_samples
{
	int compression;
	int channels;
	int rate;
	unsigned long offset;
	unsigned long size;
};

static inline unsigned long hit_sound_u32(const unsigned char *at)
{
	return (unsigned long)at[0] | ((unsigned long)at[1] << 8) | ((unsigned long)at[2] << 16) |
		((unsigned long)at[3] << 24);
}

static inline int hit_sound_u16(const unsigned char *at)
{
	return at[0] | (at[1] << 8);
}

/* whether a resource's path is the ting's (the globals' multiplayer sound
0x2B in Custom Edition's maps): sound\sfx\impulse\ting\ting, any case */
static inline int hit_sound_ting_path(const char *path)
{
	static const char ting[] = "sound\\sfx\\impulse\\ting\\ting";
	size_t index;

	if (!path)
		return 0;
	for (index = 0; ting[index]; index++)
	{
		if (tolower((unsigned char)path[index]) != ting[index])
			return 0;
	}
	return !path[index];
}

/* a Custom Edition sound resource's first permutation's samples (offset:
in sounds.map): 0 if the resource is not a sound this reads */
static inline int hit_sound_ce_samples(const unsigned char *resource, unsigned long size,
	struct hit_sound_samples *samples)
{
	unsigned long ranges, permutations, first;
	const unsigned char *permutation;

	memset(samples, 0, sizeof(*samples));
	if (!resource || size < HIT_SOUND_SOUND_SIZE)
		return 0;
	ranges = hit_sound_u32(resource + HIT_SOUND_PITCH_RANGES_OFFSET);
	if (!ranges || ranges > 64 || HIT_SOUND_SOUND_SIZE + ranges * HIT_SOUND_PITCH_RANGE_SIZE > size)
		return 0;
	permutations = hit_sound_u32(resource + HIT_SOUND_SOUND_SIZE + HIT_SOUND_PERMUTATIONS_OFFSET);
	/* (the permutations follow the pitch ranges, the first range's first) */
	first = HIT_SOUND_SOUND_SIZE + ranges * HIT_SOUND_PITCH_RANGE_SIZE;
	if (!permutations || permutations > 256 || first + HIT_SOUND_PERMUTATION_SIZE > size)
		return 0;
	permutation = resource + first;
	samples->compression = hit_sound_u16(permutation + HIT_SOUND_PERMUTATION_COMPRESSION_OFFSET);
	samples->channels = hit_sound_u16(resource + HIT_SOUND_ENCODING_OFFSET) == 1 ? 2 : 1;
	samples->rate = hit_sound_u16(resource + HIT_SOUND_SAMPLE_RATE_OFFSET) == 1 ? 44100 : 22050;
	samples->size = hit_sound_u32(permutation + HIT_SOUND_PERMUTATION_SAMPLES_OFFSET);
	samples->offset = hit_sound_u32(permutation + HIT_SOUND_PERMUTATION_SAMPLES_OFFSET + 8);
	if (!samples->size || samples->size > HIT_SOUND_MAXIMUM_BYTES)
		return 0;
	return samples->compression == HIT_SOUND_COMPRESSION_NONE ||
		samples->compression == HIT_SOUND_COMPRESSION_XBOX_ADPCM || samples->compression == HIT_SOUND_COMPRESSION_OGG;
}

/* a WAV file's 16-bit PCM (one or two channels): 0 if it is not one */
static inline int hit_sound_wav(const unsigned char *data, unsigned long size, struct hit_sound_samples *samples)
{
	unsigned long at = 12;
	int have_format = 0;

	memset(samples, 0, sizeof(*samples));
	if (!data || size < 12 || memcmp(data, "RIFF", 4) || memcmp(data + 8, "WAVE", 4))
		return 0;
	while (at + 8 <= size)
	{
		unsigned long length = hit_sound_u32(data + at + 4);
		const unsigned char *chunk = data + at + 8;

		/* (the data chunk may run past what was read of a long file: what
		there is of it is taken; any other chunk must be whole) */
		if (length > size - at - 8 && memcmp(data + at, "data", 4))
			return 0;
		if (!memcmp(data + at, "fmt ", 4))
		{
			if (length < 16 || hit_sound_u16(chunk) != 1 || hit_sound_u16(chunk + 14) != 16)
				return 0;
			samples->channels = hit_sound_u16(chunk + 2);
			samples->rate = (int)hit_sound_u32(chunk + 4);
			if (samples->channels < 1 || samples->channels > 2 || samples->rate < 8000 ||
				samples->rate > HIT_SOUND_MAXIMUM_RATE)
				return 0;
			have_format = 1;
		}
		else if (!memcmp(data + at, "data", 4))
		{
			if (length > size - at - 8)
				length = size - at - 8;
			if (!have_format || length < 2u * (unsigned long)samples->channels)
				return 0;
			samples->compression = HIT_SOUND_COMPRESSION_NONE;
			samples->offset = at + 8;
			samples->size = length;
			return 1;
		}
		/* (chunks are padded to an even length) */
		at += 8 + length + (length & 1);
	}
	return 0;
}

/* 16-bit PCM (little-endian, interleaved) or Xbox ADPCM made one channel,
at most maximum frames into out: the frames made */
static inline long hit_sound_mono(const unsigned char *data, unsigned long size, int compression, int channels,
	short *out, long maximum)
{
	long frames = 0;

	if (channels < 1 || channels > 2 || maximum <= 0)
		return 0;
	if (compression == HIT_SOUND_COMPRESSION_NONE)
	{
		unsigned long frame_bytes = 2u * (unsigned long)channels;

		for (; frames < maximum && ((unsigned long)frames + 1) * frame_bytes <= size; frames++)
		{
			const unsigned char *at = data + (unsigned long)frames * frame_bytes;
			long sum = (short)hit_sound_u16(at);

			if (channels == 2)
				sum = (sum + (short)hit_sound_u16(at + 2)) / 2;
			out[frames] = (short)sum;
		}
		return frames;
	}
	if (compression == HIT_SOUND_COMPRESSION_XBOX_ADPCM)
	{
		unsigned long block_bytes = HIT_SOUND_ADPCM_BLOCK_BYTES * (unsigned long)channels;
		unsigned long block;

		for (block = 0; (block + 1) * block_bytes <= size; block++)
		{
			const unsigned char *at = data + block * block_bytes;
			short decoded[2][HIT_SOUND_ADPCM_BLOCK_SAMPLES];
			int channel, sample;

			for (channel = 0; channel < channels; channel++)
			{
				const unsigned char *header = at + channel * 4;
				int predictor = (short)hit_sound_u16(header);
				int index = header[2] > 88 ? 88 : header[2];
				int group, byte;

				decoded[channel][0] = (short)predictor;
				for (group = 0; group < 8; group++)
				{
					const unsigned char *nibbles = at + 4 * channels + (group * channels + channel) * 4;

					for (byte = 0; byte < 4; byte++)
					{
						/* (nibble n codes sample n + 1) */
						sample = group * 8 + byte * 2 + 1;
						decoded[channel][sample] = (short)content_ima_expand(nibbles[byte] & 0xf, &predictor, &index);
						if (sample + 1 < HIT_SOUND_ADPCM_BLOCK_SAMPLES)
							decoded[channel][sample + 1] =
								(short)content_ima_expand(nibbles[byte] >> 4, &predictor, &index);
					}
				}
			}
			for (sample = 0; sample < HIT_SOUND_ADPCM_BLOCK_SAMPLES && frames < maximum; sample++)
				out[frames++] = (short)(channels == 2 ? (decoded[0][sample] + decoded[1][sample]) / 2 : decoded[0][sample]);
		}
		return frames;
	}
	return 0;
}

/* frames of one channel at rate brought to the mixer's (linear: a short
tone), at most maximum: the frames made */
static inline long hit_sound_resample(const short *in, long frames, int rate, short *out, long maximum)
{
	long made = 0;

	if (!in || frames <= 0 || rate <= 0)
		return 0;
	for (; made < maximum; made++)
	{
		/* (where this output frame falls in the input, in 1/65536ths) */
		unsigned long long position = (unsigned long long)made * (unsigned long long)rate * 65536u / HIT_SOUND_OUTPUT_RATE;
		long index = (long)(position >> 16);
		long fraction = (long)(position & 0xffff);
		long next;

		if (index >= frames)
			break;
		next = index + 1 < frames ? in[index + 1] : in[index];
		out[made] = (short)(in[index] + ((next - in[index]) * fraction >> 16));
	}
	return made;
}

#ifdef HIT_SOUND_VORBIS
/* (content_vorbis.c's: the first `count` frames of a stream, without
decoding the rest) */
int ce_vorbis_edges(const unsigned char *data, int size, int count, int whole, int *channels, int *sample_rate,
	short *head, int *head_frames, short *tail, int *tail_frames);

/* an Ogg Vorbis stream's first HIT_SOUND_MAXIMUM_SECONDS made one channel,
at most maximum frames (HIT_SOUND_MAXIMUM_SOURCE_FRAMES holds any rate
taken), its rate in *rate: the frames made, 0 if it is not one this takes.
Only its start is decoded, so a long stream (or the start of one, as much
of a file as was read) is as quick as a short one */
static inline long hit_sound_ogg_mono(const unsigned char *data, unsigned long size, short *out, long maximum,
	int *rate)
{
	short *head, *tail;
	int channels = 0, frames = 0, tail_frames = 0, index;

	*rate = 0;
	if (!data || !size || size > 0x7fffffff || maximum <= 0)
		return 0;
	/* (the tail: ce_vorbis_edges keeps a stream's last frames as it goes,
	until the head is full) */
	head = malloc((size_t)HIT_SOUND_MAXIMUM_SOURCE_FRAMES * 2 * sizeof(short));
	tail = malloc((size_t)HIT_SOUND_MAXIMUM_SOURCE_FRAMES * 2 * sizeof(short));
	if (!head || !tail)
	{
		free(head);
		free(tail);
		return 0;
	}
	if (ce_vorbis_edges(data, (int)size, HIT_SOUND_MAXIMUM_SOURCE_FRAMES, 0, &channels, rate, head, &frames, tail,
		&tail_frames) <= 0 || channels < 1 || channels > 2 || *rate < 8000 || *rate > HIT_SOUND_MAXIMUM_RATE)
	{
		free(head);
		free(tail);
		*rate = 0;
		return 0;
	}
	if (frames > *rate * HIT_SOUND_MAXIMUM_SECONDS)
		frames = *rate * HIT_SOUND_MAXIMUM_SECONDS;
	if (frames > maximum)
		frames = (int)maximum;
	for (index = 0; index < frames; index++)
		out[index] = (short)(channels == 2 ? (head[index * 2] + head[index * 2 + 1]) / 2 : head[index]);
	free(head);
	free(tail);
	return frames;
}
#endif

#endif
