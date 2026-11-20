/*
H2_GESTALT.H

Halo 2 (MCC)'s sound gestalt (the 'ugh!' tag of a map, h2_gestalt.c) and its
sound tags ('snd!'): what each sound is (class, codec, playback parameters),
its pitch ranges and permutations, and where the permutations' chunks lie in
sounds_neutral.dat (the dat is read by the sound cache: h2_sound.c).

A sound tag is 28 bytes: flags at +0, the sound class at +2, the platform
codec at +3, the playback parameters' index at +6, the first pitch range's
at +8, the pitch ranges' count at +10, the scale parameters' index at +11,
the longest play time (ms) at +16. The
gestalt, 15 blocks of (count, address) at its tag data's start, keeps the
playback parameters (block 1, 56 bytes each), the scale parameters (block 2,
20 bytes: the gain, pitch and skip fraction modifiers a sound's scale picks
between), the pitch ranges (block 5, 12
bytes: the permutations' first index and count at +8 and +10), the
permutations (block 6, 16 bytes: a name at +0, the address of the per-
language records at +12, 9 of 8 bytes: decoded bytes, first chunk, chunk
count) and, for each of the 8 languages (block 12, 8 bytes: count, address),
the chunks (16 bytes: offset in the dat, size and flags - the size in the
low 29 bits - and the decoded bytes). A tag uses its own map's gestalt.
Language 0 is the language-neutral one, which every effect has. Read only,
every index and address checked against what was read: the file is anyone's.
*/

#ifndef __H2_GESTALT_H
#define __H2_GESTALT_H

#include "cseries.h"
#include "h2_cache.h"

/* ---------- constants */

enum
{
	/* the platform codecs: shared.map's indices, as the audit found them
	(a snd! tag's byte 3 indexes its own map's table: h2_gestalt_sound
	gives these) */
	H2_SOUND_CODEC_ADPCM_MONO_44 = 0,
	H2_SOUND_CODEC_ADPCM_STEREO_44 = 1,
	H2_SOUND_CODEC_ADPCM_STEREO_44_B = 2,
	H2_SOUND_CODEC_OPUS_STEREO = 3,
	H2_SOUND_CODEC_ADPCM_MONO_22 = 4,
	H2_SOUND_CODEC_OPUS_MONO = 6,
	H2_SOUND_CODEC_OPUS_STEREO_B = 7,
	/* (WMA version 2: MCC's main menu music - 2 channels, 44.1 kHz, as its
	WAVEFORMATEX has them: h2_sound_codec.c checks it) */
	H2_SOUND_CODEC_WMA = 0x40,

	/* the most chunks and permutations a sound may have for this port */
	H2_SOUND_MAXIMUM_PERMUTATIONS = 256,
	H2_SOUND_MAXIMUM_CHUNKS = 256,
	H2_SOUND_LANGUAGES = 9
};

/* ---------- structures */

struct h2_gestalt;

/* a snd! tag */
struct h2_snd
{
	unsigned short flags;
	unsigned char sound_class;
	unsigned char codec;
	short playback_index;
	short first_range;
	short range_count;
	/* (block 2's; NONE when the tag has none) */
	short scale_index;
	long maximum_play_ms;
};

/* a scale parameters record (block 2): what a sound's scale (0 to 1: an
object function's value, an effect part's scale) adds to its gain (dB),
pitch (cents) and skip fraction - the lower bound at scale 0, the upper at 1
(Halo 2's sound_definition_gain_lower/upper, halo2-upstream-work
src/unknown_218c60.cpp) */
struct h2_snd_scale
{
	float gain_lower_db;
	float gain_upper_db;
	short pitch_lower_cents;
	short pitch_upper_cents;
	float skip_lower;
	float skip_upper;
};

/* a playback parameters record (block 1): the distances (0: the class's),
the skip fraction, the gains (dB) and the random pitch (cents) */
struct h2_snd_playback
{
	float minimum_distance;
	float maximum_distance;
	float skip_fraction;
	float gain_db;
	float gain_variance_db;
	short pitch_lower_cents;
	short pitch_upper_cents;
};

/* a pitch range (block 5): its permutations in the permutation block */
struct h2_snd_range
{
	short first_permutation;
	short permutation_count;
};

/* a permutation, for the language asked */
struct h2_snd_permutation
{
	unsigned long name;
	unsigned long decoded_bytes;
	short first_chunk;
	short chunk_count;
};

struct h2_snd_chunk
{
	unsigned long offset;
	unsigned long bytes;
	unsigned long flags;
	unsigned long decoded_bytes;
};

/* ---------- prototypes/H2_GESTALT.C */

/* the gestalt of an opened map (NULL: the map has none); the cache must
stay open while it is used */
struct h2_gestalt *h2_gestalt_open(
	struct h2_cache const *cache);
void h2_gestalt_close(
	struct h2_gestalt *gestalt);

/* the codec's channels (1 or 2) and rate (Hz), 0 when it is not one this
port knows; whether it is an Opus one */
int h2_gestalt_codec_channels(
	int codec);
long h2_gestalt_codec_rate(
	int codec);
boolean h2_gestalt_codec_is_opus(
	int codec);

/* the sound tag at `address` of the map's tag data */
boolean h2_gestalt_sound(
	struct h2_gestalt const *gestalt,
	unsigned long address,
	struct h2_snd *sound);
boolean h2_gestalt_playback(
	struct h2_gestalt const *gestalt,
	long index,
	struct h2_snd_playback *playback);
boolean h2_gestalt_scale(
	struct h2_gestalt const *gestalt,
	long index,
	struct h2_snd_scale *scale);
boolean h2_gestalt_range(
	struct h2_gestalt const *gestalt,
	long index,
	struct h2_snd_range *range);
boolean h2_gestalt_permutation(
	struct h2_gestalt const *gestalt,
	long index,
	long language,
	struct h2_snd_permutation *permutation);
boolean h2_gestalt_chunk(
	struct h2_gestalt const *gestalt,
	long language,
	long index,
	struct h2_snd_chunk *chunk);

#endif
