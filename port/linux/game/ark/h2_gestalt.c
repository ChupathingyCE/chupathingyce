/*
H2_GESTALT.C

Halo 2 (MCC)'s sound gestalt read for the game (h2_gestalt.h).
*/

/* ---------- headers */

#include "h2_gestalt.h"

#include <stdlib.h>
#include <string.h>

#undef malloc
#undef free
#undef calloc
#undef realloc

/* ---------- constants */

enum
{
	GESTALT_BLOCKS = 15,
	GESTALT_CODEC_BLOCK = 0,
	GESTALT_CODECS = 16,
	GESTALT_PLAYBACK_BLOCK = 1,
	GESTALT_SCALE_BLOCK = 2,
	GESTALT_RANGE_BLOCK = 5,
	GESTALT_PERMUTATION_BLOCK = 6,
	GESTALT_CHUNK_BLOCK = 12,

	PLAYBACK_BYTES = 56,
	SCALE_BYTES = 20,
	RANGE_BYTES = 12,
	PERMUTATION_BYTES = 16,
	LANGUAGE_RECORD_BYTES = 8,
	CHUNK_BYTES = 16,
	SOUND_BYTES = 28,
	CHUNK_SIZE_MASK = 0x1FFFFFFF,
	GESTALT_GROUP = 'ugh!'
};

/* ---------- structures */

struct h2_gestalt
{
	struct h2_cache const *cache;
	struct
	{
		unsigned long count;
		unsigned long address;
	} block[GESTALT_BLOCKS];
	/* the chunk lists of the languages (block 12's records) */
	struct
	{
		unsigned long count;
		unsigned long address;
	} languages[H2_SOUND_LANGUAGES];
	long language_count;
	/* the map's own platform codecs (block 0, 3 bytes each: sample rate,
	encoding, compression), which a snd! tag's byte 3 indexes - not the
	same table in every map (shared.map's has 8, zanzibar.map's 4,
	mainmenu.map's 5) */
	long codec_count;
	unsigned char codecs[GESTALT_CODECS][3];
};

/* ---------- private code */

static unsigned long le32(unsigned char const *p)
{
	return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

static short le16s(unsigned char const *p)
{
	return (short)((unsigned)p[0] | (unsigned)p[1] << 8);
}

/* element `index` of block `block`, `bytes` of it, or NULL */
static unsigned char const *element(
	struct h2_gestalt const *gestalt,
	long block,
	long index,
	unsigned long bytes)
{
	if (index < 0 || (unsigned long)index >= gestalt->block[block].count || !gestalt->block[block].address)
		return NULL;
	return (unsigned char const *)h2_cache_at(gestalt->cache, gestalt->block[block].address + (unsigned long)index * bytes, bytes);
}

/* ---------- public code */

struct h2_gestalt *h2_gestalt_open(
	struct h2_cache const *cache)
{
	long count = h2_cache_tag_count(cache);
	long i;

	for (i = 0; i < count; i++)
	{
		struct h2_cache_tag tag;

		if (h2_cache_tag_get(cache, i, &tag) && tag.group_tag == GESTALT_GROUP && tag.address &&
			tag.size >= 8 * GESTALT_BLOCKS)
		{
			unsigned char const *p = (unsigned char const *)h2_cache_at(cache, tag.address, 8 * GESTALT_BLOCKS);
			struct h2_gestalt *gestalt = p ? (struct h2_gestalt *)calloc(1, sizeof(*gestalt)) : NULL;
			long block;

			if (!gestalt)
				return NULL;
			gestalt->cache = cache;
			for (block = 0; block < GESTALT_BLOCKS; block++)
			{
				gestalt->block[block].count = le32(p + 8 * block);
				gestalt->block[block].address = le32(p + 8 * block + 4);
			}
			/* its platform codecs */
			{
				unsigned long n = gestalt->block[GESTALT_CODEC_BLOCK].count;
				unsigned char const *c;

				if (n > GESTALT_CODECS)
					n = GESTALT_CODECS;
				c = n && gestalt->block[GESTALT_CODEC_BLOCK].address ?
					(unsigned char const *)h2_cache_at(cache, gestalt->block[GESTALT_CODEC_BLOCK].address, n * 3) : NULL;
				if (c)
				{
					memcpy(gestalt->codecs, c, n * 3);
					gestalt->codec_count = (long)n;
				}
			}
			/* the languages' chunk lists */
			if (gestalt->block[GESTALT_CHUNK_BLOCK].count > 0 && gestalt->block[GESTALT_CHUNK_BLOCK].address)
			{
				unsigned long n = gestalt->block[GESTALT_CHUNK_BLOCK].count;
				unsigned char const *l;

				if (n > H2_SOUND_LANGUAGES)
					n = H2_SOUND_LANGUAGES;
				l = (unsigned char const *)h2_cache_at(cache, gestalt->block[GESTALT_CHUNK_BLOCK].address, n * 8);
				if (l)
				{
					unsigned long k;

					for (k = 0; k < n; k++)
					{
						gestalt->languages[k].count = le32(l + 8 * k);
						gestalt->languages[k].address = le32(l + 8 * k + 4);
					}
					gestalt->language_count = (long)n;
				}
			}
			return gestalt;
		}
	}
	return NULL;
}

void h2_gestalt_close(
	struct h2_gestalt *gestalt)
{
	free(gestalt);
}

int h2_gestalt_codec_channels(
	int codec)
{
	switch (codec)
	{
		case H2_SOUND_CODEC_ADPCM_MONO_44:
		case H2_SOUND_CODEC_ADPCM_MONO_22:
		case H2_SOUND_CODEC_OPUS_MONO:
			return 1;
		case H2_SOUND_CODEC_ADPCM_STEREO_44:
		case H2_SOUND_CODEC_ADPCM_STEREO_44_B:
		case H2_SOUND_CODEC_OPUS_STEREO:
		case H2_SOUND_CODEC_OPUS_STEREO_B:
		/* (the main menu music: its WAVEFORMATEX's, checked as it decodes) */
		case H2_SOUND_CODEC_WMA:
			return 2;
	}
	return 0;
}

long h2_gestalt_codec_rate(
	int codec)
{
	switch (codec)
	{
		case H2_SOUND_CODEC_ADPCM_MONO_44:
		case H2_SOUND_CODEC_ADPCM_STEREO_44:
		case H2_SOUND_CODEC_ADPCM_STEREO_44_B:
		case H2_SOUND_CODEC_WMA:
			return 44100;
		case H2_SOUND_CODEC_ADPCM_MONO_22:
			return 22050;
		case H2_SOUND_CODEC_OPUS_STEREO:
		case H2_SOUND_CODEC_OPUS_MONO:
		case H2_SOUND_CODEC_OPUS_STEREO_B:
			return 48000;
	}
	return 0;
}

boolean h2_gestalt_codec_is_opus(
	int codec)
{
	return codec == H2_SOUND_CODEC_OPUS_STEREO || codec == H2_SOUND_CODEC_OPUS_MONO || codec == H2_SOUND_CODEC_OPUS_STEREO_B;
}

/* a snd! tag's codec byte (an index into its map's platform codecs) as
this file's codecs (H2_SOUND_CODEC_...): by the entry's compression - 1
Xbox ADPCM (its encoding 0 mono, 1 stereo; sample rate 0 22 kHz, 1 44.1),
5 Opus (48 kHz), 4 WMA (MCC's mainmenu.map's music: its chunks an
AM_MEDIA_TYPE of WMA version 2 and its packets, in sounds_neutral.dat) -
and, for a compression not known here (3), shared.map's index as the
audit numbered them (the byte as it is) */
static unsigned char h2_gestalt_codec(
	struct h2_gestalt const *gestalt,
	unsigned char index)
{
	unsigned char const *c;

	if (index >= gestalt->codec_count)
		return index;
	c = gestalt->codecs[index];
	switch (c[2])
	{
		case 1:
			if (c[1] == 0)
				return c[0] == 0 ? H2_SOUND_CODEC_ADPCM_MONO_22 : H2_SOUND_CODEC_ADPCM_MONO_44;
			if (c[1] == 1 && c[0] == 1)
				return H2_SOUND_CODEC_ADPCM_STEREO_44;
			break;
		case 4:
			return H2_SOUND_CODEC_WMA;
		case 5:
			if (c[1] == 0)
				return H2_SOUND_CODEC_OPUS_MONO;
			if (c[1] == 1)
				return H2_SOUND_CODEC_OPUS_STEREO_B;
			break;
	}
	return index;
}

boolean h2_gestalt_sound(
	struct h2_gestalt const *gestalt,
	unsigned long address,
	struct h2_snd *sound)
{
	unsigned char const *p = (unsigned char const *)h2_cache_at(gestalt->cache, address, SOUND_BYTES);

	if (!p)
		return FALSE;
	sound->flags = (unsigned short)((unsigned)p[0] | (unsigned)p[1] << 8);
	sound->sound_class = p[2];
	sound->codec = h2_gestalt_codec(gestalt, p[3]);
	sound->playback_index = le16s(p + 6);
	sound->first_range = le16s(p + 8);
	sound->range_count = (signed char)p[10];
	sound->scale_index = (signed char)p[11];
	sound->maximum_play_ms = (long)le32(p + 16);
	return TRUE;
}

boolean h2_gestalt_playback(
	struct h2_gestalt const *gestalt,
	long index,
	struct h2_snd_playback *playback)
{
	unsigned char const *p = element(gestalt, GESTALT_PLAYBACK_BLOCK, index, PLAYBACK_BYTES);
	union
	{
		unsigned long bits;
		float value;
	} u;

	if (!p)
		return FALSE;
#define FLOAT_AT(offset) (u.bits = le32(p + (offset)), u.value)
	playback->minimum_distance = FLOAT_AT(0);
	playback->maximum_distance = FLOAT_AT(4);
	playback->skip_fraction = FLOAT_AT(12);
	playback->gain_db = FLOAT_AT(16);
	playback->gain_variance_db = FLOAT_AT(20);
#undef FLOAT_AT
	playback->pitch_lower_cents = le16s(p + 24);
	playback->pitch_upper_cents = le16s(p + 26);
	return TRUE;
}

boolean h2_gestalt_scale(
	struct h2_gestalt const *gestalt,
	long index,
	struct h2_snd_scale *scale)
{
	unsigned char const *p = element(gestalt, GESTALT_SCALE_BLOCK, index, SCALE_BYTES);
	union
	{
		unsigned long bits;
		float value;
	} u;

	if (!p)
		return FALSE;
#define FLOAT_AT(offset) (u.bits = le32(p + (offset)), u.value)
	scale->gain_lower_db = FLOAT_AT(0);
	scale->gain_upper_db = FLOAT_AT(4);
	scale->skip_lower = FLOAT_AT(12);
	scale->skip_upper = FLOAT_AT(16);
#undef FLOAT_AT
	scale->pitch_lower_cents = le16s(p + 8);
	scale->pitch_upper_cents = le16s(p + 10);
	return TRUE;
}

boolean h2_gestalt_range(
	struct h2_gestalt const *gestalt,
	long index,
	struct h2_snd_range *range)
{
	unsigned char const *p = element(gestalt, GESTALT_RANGE_BLOCK, index, RANGE_BYTES);

	if (!p)
		return FALSE;
	range->first_permutation = le16s(p + 8);
	range->permutation_count = le16s(p + 10);
	return TRUE;
}

boolean h2_gestalt_permutation(
	struct h2_gestalt const *gestalt,
	long index,
	long language,
	struct h2_snd_permutation *permutation)
{
	unsigned char const *p = element(gestalt, GESTALT_PERMUTATION_BLOCK, index, PERMUTATION_BYTES);
	unsigned char const *record;

	if (!p || language < 0 || language >= H2_SOUND_LANGUAGES)
		return FALSE;
	permutation->name = le32(p);
	/* (the per-language records: 9 of 8 bytes) */
	record = (unsigned char const *)h2_cache_at(gestalt->cache, le32(p + 12) + (unsigned long)language * LANGUAGE_RECORD_BYTES,
		LANGUAGE_RECORD_BYTES);
	if (!record)
		return FALSE;
	permutation->decoded_bytes = le32(record);
	permutation->first_chunk = le16s(record + 4);
	permutation->chunk_count = le16s(record + 6);
	return TRUE;
}

boolean h2_gestalt_chunk(
	struct h2_gestalt const *gestalt,
	long language,
	long index,
	struct h2_snd_chunk *chunk)
{
	unsigned char const *p;

	if (language < 0 || language >= gestalt->language_count || index < 0 ||
		(unsigned long)index >= gestalt->languages[language].count || !gestalt->languages[language].address)
		return FALSE;
	p = (unsigned char const *)h2_cache_at(gestalt->cache, gestalt->languages[language].address + (unsigned long)index * CHUNK_BYTES,
		CHUNK_BYTES);
	if (!p)
		return FALSE;
	chunk->offset = le32(p);
	chunk->flags = le32(p + 4);
	chunk->bytes = chunk->flags & CHUNK_SIZE_MASK;
	chunk->decoded_bytes = le32(p + 8);
	return TRUE;
}
