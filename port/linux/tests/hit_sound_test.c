/*
HIT_SOUND_TEST.C

The hit tone's decoding (port/linux/game/hit_sound_decode.h): a ting's
resource found and read, a WAV file's samples, Xbox ADPCM and 16-bit PCM
made one channel, and the resampling to the mixer's rate; anything else
refused (#2038).
*/

#include "../game/hit_sound_decode.h"

#include <stdio.h>
#include <stdlib.h>

static int failures;

#define CHECK(condition) \
	do { if (!(condition)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)

static void put32(unsigned char *at, unsigned long value)
{
	at[0] = (unsigned char)value;
	at[1] = (unsigned char)(value >> 8);
	at[2] = (unsigned char)(value >> 16);
	at[3] = (unsigned char)(value >> 24);
}

static void put16(unsigned char *at, int value)
{
	at[0] = (unsigned char)value;
	at[1] = (unsigned char)(value >> 8);
}

int main(void)
{
	unsigned char sound[HIT_SOUND_SOUND_SIZE + HIT_SOUND_PITCH_RANGE_SIZE + HIT_SOUND_PERMUTATION_SIZE];
	unsigned char *permutation = sound + HIT_SOUND_SOUND_SIZE + HIT_SOUND_PITCH_RANGE_SIZE;
	unsigned char wav[44 + 8];
	struct hit_sound_samples samples;
	short out[256], resampled[512];
	unsigned char adpcm[HIT_SOUND_ADPCM_BLOCK_BYTES];
	long frames;

	/* a ting's path, and others' */
	CHECK(hit_sound_ting_path("sound\\sfx\\ui\\ting"));
	CHECK(hit_sound_ting_path("TING"));
	CHECK(!hit_sound_ting_path("sound\\sfx\\ui\\ting__permutations"));
	CHECK(!hit_sound_ting_path("sound\\sfx\\ui\\countdown_timer"));
	CHECK(!hit_sound_ting_path("sound\\sfx\\ui\\tin"));
	CHECK(!hit_sound_ting_path(""));

	/* a sound's resource: one pitch range, one permutation of Xbox ADPCM */
	memset(sound, 0, sizeof(sound));
	put32(sound + HIT_SOUND_PITCH_RANGES_OFFSET, 1);
	put32(sound + HIT_SOUND_SOUND_SIZE + HIT_SOUND_PERMUTATIONS_OFFSET, 1);
	put16(permutation + HIT_SOUND_PERMUTATION_COMPRESSION_OFFSET, HIT_SOUND_COMPRESSION_XBOX_ADPCM);
	put32(permutation + HIT_SOUND_PERMUTATION_SAMPLES_OFFSET, 36 * 10);
	put32(permutation + HIT_SOUND_PERMUTATION_SAMPLES_OFFSET + 8, 0x1234);
	CHECK(hit_sound_ce_samples(sound, sizeof(sound), &samples));
	CHECK(samples.compression == HIT_SOUND_COMPRESSION_XBOX_ADPCM && samples.channels == 1 && samples.rate == 22050);
	CHECK(samples.offset == 0x1234 && samples.size == 360);
	/* (stereo at 44 kHz) */
	put16(sound + HIT_SOUND_ENCODING_OFFSET, 1);
	put16(sound + HIT_SOUND_SAMPLE_RATE_OFFSET, 1);
	CHECK(hit_sound_ce_samples(sound, sizeof(sound), &samples) && samples.channels == 2 && samples.rate == 44100);
	/* (refused: cut short, no ranges, too large, an unknown compression) */
	CHECK(!hit_sound_ce_samples(sound, sizeof(sound) - 1, &samples));
	put32(sound + HIT_SOUND_PITCH_RANGES_OFFSET, 0);
	CHECK(!hit_sound_ce_samples(sound, sizeof(sound), &samples));
	put32(sound + HIT_SOUND_PITCH_RANGES_OFFSET, 1);
	put32(permutation + HIT_SOUND_PERMUTATION_SAMPLES_OFFSET, HIT_SOUND_MAXIMUM_BYTES + 1);
	CHECK(!hit_sound_ce_samples(sound, sizeof(sound), &samples));
	put32(permutation + HIT_SOUND_PERMUTATION_SAMPLES_OFFSET, 360);
	put16(permutation + HIT_SOUND_PERMUTATION_COMPRESSION_OFFSET, 2);
	CHECK(!hit_sound_ce_samples(sound, sizeof(sound), &samples));
	CHECK(!hit_sound_ce_samples(NULL, 0, &samples));

	/* a WAV: mono 16-bit at 24 kHz, four samples */
	memset(wav, 0, sizeof(wav));
	memcpy(wav, "RIFF", 4);
	put32(wav + 4, sizeof(wav) - 8);
	memcpy(wav + 8, "WAVEfmt ", 8);
	put32(wav + 16, 16);
	put16(wav + 20, 1);
	put16(wav + 22, 1);
	put32(wav + 24, 24000);
	put32(wav + 28, 48000);
	put16(wav + 32, 2);
	put16(wav + 34, 16);
	memcpy(wav + 36, "data", 4);
	put32(wav + 40, 8);
	put16(wav + 44, 1000);
	put16(wav + 46, -1000);
	put16(wav + 48, 2000);
	put16(wav + 50, -2000);
	CHECK(hit_sound_wav(wav, sizeof(wav), &samples));
	CHECK(samples.channels == 1 && samples.rate == 24000 && samples.offset == 44 && samples.size == 8);
	frames = hit_sound_mono(wav + samples.offset, samples.size, samples.compression, samples.channels, out, 256);
	CHECK(frames == 4 && out[0] == 1000 && out[1] == -1000 && out[3] == -2000);
	/* (doubled to 48 kHz: each sample, then halfway to the next) */
	frames = hit_sound_resample(out, 4, 24000, resampled, 512);
	CHECK(frames == 8 && resampled[0] == 1000 && resampled[1] == 0 && resampled[2] == -1000 && resampled[4] == 2000);
	/* (a data chunk past the file's end, 8-bit, a third channel, not a WAV) */
	put32(wav + 40, 9);
	CHECK(!hit_sound_wav(wav, sizeof(wav), &samples));
	put32(wav + 40, 8);
	put16(wav + 34, 8);
	CHECK(!hit_sound_wav(wav, sizeof(wav), &samples));
	put16(wav + 34, 16);
	put16(wav + 22, 3);
	CHECK(!hit_sound_wav(wav, sizeof(wav), &samples));
	put16(wav + 22, 1);
	memcpy(wav, "OggS", 4);
	CHECK(!hit_sound_wav(wav, sizeof(wav), &samples));
	CHECK(!hit_sound_wav(wav, 11, &samples));

	/* stereo PCM made one channel */
	{
		unsigned char stereo[8];

		put16(stereo, 1000);
		put16(stereo + 2, 3000);
		put16(stereo + 4, -400);
		put16(stereo + 6, 0);
		CHECK(hit_sound_mono(stereo, 8, HIT_SOUND_COMPRESSION_NONE, 2, out, 256) == 2 && out[0] == 2000 &&
			out[1] == -200);
		/* (no more than asked for, and none of an odd byte) */
		CHECK(hit_sound_mono(stereo, 7, HIT_SOUND_COMPRESSION_NONE, 2, out, 256) == 1);
		CHECK(hit_sound_mono(stereo, 8, HIT_SOUND_COMPRESSION_NONE, 1, out, 3) == 3);
	}

	/* an Xbox ADPCM block: 64 samples, its first the header's */
	memset(adpcm, 0, sizeof(adpcm));
	put16(adpcm, 500);
	adpcm[2] = 200; /* (a step index past 88 is held at 88) */
	CHECK(hit_sound_mono(adpcm, sizeof(adpcm), HIT_SOUND_COMPRESSION_XBOX_ADPCM, 1, out, 256) == 64);
	CHECK(out[0] == 500);
	CHECK(hit_sound_mono(adpcm, sizeof(adpcm) - 1, HIT_SOUND_COMPRESSION_XBOX_ADPCM, 1, out, 256) == 0);
	CHECK(hit_sound_mono(adpcm, sizeof(adpcm), HIT_SOUND_COMPRESSION_OGG, 1, out, 256) == 0);

	/* resampling: 22050 to 48000 lengthens by their ratio, and stops at the end */
	{
		short *ramp = malloc(22050 * sizeof(short)), *long_out = malloc(48000 * 2 * sizeof(short));
		long index;

		for (index = 0; ramp && index < 22050; index++)
			ramp[index] = (short)(index % 1000);
		frames = ramp && long_out ? hit_sound_resample(ramp, 22050, 22050, long_out, 48000 * 2) : 0;
		CHECK(frames >= 47998 && frames <= 48000);
		CHECK(hit_sound_resample(ramp, 22050, 22050, long_out, 100) == 100);
		CHECK(hit_sound_resample(NULL, 10, 22050, long_out, 100) == 0);
		free(ramp);
		free(long_out);
	}

	if (failures)
	{
		printf("%d failures\n", failures);
		return 1;
	}
	printf("hit sound: all passed\n");
	return 0;
}
