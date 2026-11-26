/*
CONTENT_GOLDEN.C

What the loaders' shared pieces make, printed so tools/test_content_golden.py
can hold it against the figures it keeps: the SHA-256 copies' digests, the
Ogg Vorbis decoders' samples and the IMA ADPCM coders' samples and codes.
Built once per piece with only that piece's units (the test's build lines),
so it runs before and after the pieces are moved and must print the same.

  GOLDEN_SHA256_P2P       p2p_sha256 and p2p_hmac_sha256 (p2p_crypto.c)
  GOLDEN_SHA256_IDENTITY  identity_sha256 (identity.c)
  GOLDEN_SHA256_PLAYLIST  playlist_sha256_* (playlist_manifest.c), in pieces
  GOLDEN_SHA256_UPDATE    update_sha256 (update_manifest.c)
  GOLDEN_SHA256_MCC       mcc_sha256_* (mcc_sha256.c), in pieces
  GOLDEN_VORBIS           ce_vorbis.c's decoders and resampler, and
                          mcc_vorbis_decode, on the Ogg files named
  GOLDEN_ADPCM            the desktop mixer's Xbox ADPCM decoder and MCC's
                          IMA coder (their code, cut out of dsound_sdl.c and
                          mcc_audio.c by the test)

Every input is made here from a fixed seed: no game's data. Given files
(tools/content_hashes.py), the SHA-256 and ADPCM builds read those instead.
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- inputs and a check value of their outputs */

static uint32_t golden_seed;

static uint32_t golden_next(void)
{
	/* (xorshift32) */
	golden_seed ^= golden_seed << 13;
	golden_seed ^= golden_seed >> 17;
	golden_seed ^= golden_seed << 5;
	return golden_seed;
}

static void golden_bytes(unsigned char *bytes, size_t size, uint32_t seed)
{
	size_t index;

	golden_seed = seed ? seed : 1;
	for (index = 0; index < size; index++)
		bytes[index] = (unsigned char)(golden_next() >> 24);
}

/* FNV-1a, 64 bits: a check of the outputs that uses none of the code tested */
static uint64_t golden_fnv(const void *data, size_t size)
{
	const unsigned char *bytes = data;
	uint64_t hash = 0xcbf29ce484222325ULL;
	size_t index;

	for (index = 0; index < size; index++)
	{
		hash ^= bytes[index];
		hash *= 0x100000001b3ULL;
	}
	return hash;
}

static void golden_hex(const unsigned char *digest)
{
	int index;

	for (index = 0; index < 32; index++)
		printf("%02x", digest[index]);
}

/* a file's bytes (malloc'd), or NULL */
static unsigned char *golden_file(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	unsigned char *data = NULL;
	long length;

	*size = 0;
	if (!file)
		return NULL;
	if (!fseek(file, 0, SEEK_END) && (length = ftell(file)) >= 0 && !fseek(file, 0, SEEK_SET) &&
		(data = malloc(length ? (size_t)length : 1)) != NULL)
	{
		if (fread(data, 1, (size_t)length, file) == (size_t)length)
			*size = (size_t)length;
		else
		{
			free(data);
			data = NULL;
		}
	}
	fclose(file);
	return data;
}

/* ---------- SHA-256 */

#if defined(GOLDEN_SHA256_P2P) || defined(GOLDEN_SHA256_IDENTITY) || defined(GOLDEN_SHA256_PLAYLIST) || \
	defined(GOLDEN_SHA256_UPDATE) || defined(GOLDEN_SHA256_MCC)
#define GOLDEN_SHA256 1

#if defined(GOLDEN_SHA256_P2P)
#include <pthread.h>
void p2p_sha256(const void *data, int size, unsigned char *digest);
void p2p_hmac_sha256(const unsigned char *key, int key_size, const void *data, int size, unsigned char *digest);
/* (what p2p_crypto.c's other parts call) */
pthread_mutex_t p2p_lock = PTHREAD_MUTEX_INITIALIZER;
void posix_random_bytes(void *buffer, unsigned long size);
void posix_random_bytes(void *buffer, unsigned long size)
{
	memset(buffer, 0x5a, size);
}
void platform_log(const char *format, ...);
void platform_log(const char *format, ...)
{
	(void)format;
}
#define GOLDEN_STREAMING 0
#elif defined(GOLDEN_SHA256_IDENTITY)
void identity_sha256(const void *data, int size, unsigned char *digest);
#define GOLDEN_STREAMING 0
#elif defined(GOLDEN_SHA256_PLAYLIST)
#include "playlist_manifest.h"
#define GOLDEN_STREAMING 1
#elif defined(GOLDEN_SHA256_UPDATE)
#include "update_manifest.h"
#define GOLDEN_STREAMING 0
#elif defined(GOLDEN_SHA256_MCC)
#include "mcc_sha256.h"
#define GOLDEN_STREAMING 1
#endif

/* data's digest, given whole */
static void golden_digest(const unsigned char *data, size_t size, unsigned char *digest)
{
#if defined(GOLDEN_SHA256_P2P)
	p2p_sha256(data, (int)size, digest);
#elif defined(GOLDEN_SHA256_IDENTITY)
	identity_sha256(data, (int)size, digest);
#elif defined(GOLDEN_SHA256_PLAYLIST)
	struct playlist_sha256 context;

	playlist_sha256_begin(&context);
	playlist_sha256_add(&context, data, size);
	playlist_sha256_end(&context, digest);
#elif defined(GOLDEN_SHA256_UPDATE)
	char hex[UPDATE_SHA256_TEXT_SIZE];
	int index;

	update_sha256(data, size, hex);
	for (index = 0; index < 32; index++)
	{
		unsigned int byte;

		sscanf(hex + index * 2, "%2x", &byte);
		digest[index] = (unsigned char)byte;
	}
#elif defined(GOLDEN_SHA256_MCC)
	struct mcc_sha256 context;

	mcc_sha256_begin(&context);
	mcc_sha256_add(&context, data, (int)size);
	mcc_sha256_end(&context, digest);
#endif
}

#if GOLDEN_STREAMING
/* data's digest, given in pieces of the sizes the seed picks (1 to piece) */
static void golden_digest_pieces(const unsigned char *data, size_t size, size_t piece, uint32_t seed,
	unsigned char *digest)
{
	size_t done = 0;
#if defined(GOLDEN_SHA256_PLAYLIST)
	struct playlist_sha256 context;

	playlist_sha256_begin(&context);
#else
	struct mcc_sha256 context;

	mcc_sha256_begin(&context);
#endif
	golden_seed = seed;
	while (done < size)
	{
		size_t count = 1 + golden_next() % piece;

		if (count > size - done)
			count = size - done;
#if defined(GOLDEN_SHA256_PLAYLIST)
		playlist_sha256_add(&context, data + done, count);
#else
		mcc_sha256_add(&context, data + done, (int)count);
#endif
		done += count;
	}
#if defined(GOLDEN_SHA256_PLAYLIST)
	playlist_sha256_end(&context, digest);
#else
	mcc_sha256_end(&context, digest);
#endif
}
#endif

#if defined(GOLDEN_SHA256)
/* each file named: "<path> <hex digest>" (tools/content_hashes.py) */
static int golden_sha256_files(int count, char **paths)
{
	int which;

	for (which = 0; which < count; which++)
	{
		unsigned char digest[32];
		size_t size;
		unsigned char *data = golden_file(paths[which], &size);

		if (!data)
			return 1;
		golden_digest(data, size, digest);
		printf("%s ", paths[which]);
		golden_hex(digest);
		printf("\n");
#if GOLDEN_STREAMING
		golden_digest_pieces(data, size, 70000, 99, digest);
		printf("%s ", paths[which]);
		golden_hex(digest);
		printf("\n");
#endif
		free(data);
	}
	return 0;
}
#endif

/* each line: "<case> <hex digest>"; the cases' data (golden_bytes of their
size and seed) the test makes again to check them with Python's hashlib */
static int golden_sha256(void)
{
	static const size_t sizes[] = { 1000, 4095, 4096, 4097, 65536 + 17, (1 << 20) + 3 };
	unsigned char digest[32];
	unsigned char *data = malloc((1 << 20) + 3);
	size_t size, index;

	if (!data)
		return 1;
	printf("text-abc ");
	golden_digest((const unsigned char *)"abc", 3, digest);
	golden_hex(digest);
	printf("\ntext-448 ");
	golden_digest((const unsigned char *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, digest);
	golden_hex(digest);
	printf("\n");
	for (size = 0; size <= 300; size++)
	{
		golden_bytes(data, size, (uint32_t)size + 1);
		golden_digest(data, size, digest);
		printf("bytes-%lu-%lu ", (unsigned long)size, (unsigned long)size + 1);
		golden_hex(digest);
		printf("\n");
	}
	for (index = 0; index < sizeof(sizes) / sizeof(sizes[0]); index++)
	{
		golden_bytes(data, sizes[index], 77);
		golden_digest(data, sizes[index], digest);
		printf("bytes-%lu-77 ", (unsigned long)sizes[index]);
		golden_hex(digest);
		printf("\n");
#if GOLDEN_STREAMING
		{
			static const size_t pieces[] = { 1, 3, 63, 64, 65, 1000, 70000 };
			size_t which;

			for (which = 0; which < sizeof(pieces) / sizeof(pieces[0]); which++)
			{
				golden_digest_pieces(data, sizes[index], pieces[which], 99, digest);
				printf("bytes-%lu-77 ", (unsigned long)sizes[index]);
				golden_hex(digest);
				printf("\n");
			}
		}
#endif
	}
#if defined(GOLDEN_SHA256_P2P)
	/* (HMAC: keys shorter than, as long as and longer than a block) */
	{
		static const int key_sizes[] = { 0, 20, 64, 65, 131 };
		unsigned char key[131];

		for (index = 0; index < sizeof(key_sizes) / sizeof(key_sizes[0]); index++)
		{
			golden_bytes(key, (size_t)key_sizes[index], 5);
			golden_bytes(data, 333, 6);
			p2p_hmac_sha256(key, key_sizes[index], data, 333, digest);
			printf("hmac-%d-5-333-6 ", key_sizes[index]);
			golden_hex(digest);
			printf("\n");
		}
	}
#endif
	free(data);
	return 0;
}
#endif

/* ---------- Ogg Vorbis */

#if defined(GOLDEN_VORBIS)
int ce_vorbis_decode(const unsigned char *data, int size, int *channels, int *sample_rate, short **samples);
int ce_vorbis_edges(const unsigned char *data, int size, int count, int whole, int *channels, int *sample_rate,
	short *head, int *head_frames, short *tail, int *tail_frames);
int ce_vorbis_resample(short **samples, int frames, int channels, int rate, int wanted);
void *ce_vorbis_stream_open(const unsigned char *data, int size, int *channels, int *sample_rate);
int ce_vorbis_stream_read(void *stream, short *buffer, int frames);
void ce_vorbis_stream_close(void *stream);
void ce_vorbis_free(short *samples);
int mcc_vorbis_decode(unsigned char const *bytes, uint32_t size, uint32_t frame_limit,
	short **samples, uint32_t *frames, uint32_t *rate, int *channels);
void mcc_vorbis_free(short *samples);
/* (what mcc_vorbis.c calls as it goes) */
void mcc_loading_pulse(void);
void mcc_loading_pulse(void)
{
}

/* one stream through every decoder: "<name> <part> <figures> <check>" */
static void golden_vorbis_one(const char *name, const unsigned char *data, int size)
{
	int channels = 0, rate = 0, frames;
	short *samples = NULL;

	frames = ce_vorbis_decode(data, size, &channels, &rate, &samples);
	printf("%s ce-decode %d %d %d %016llx\n", name, frames, channels, rate,
		frames > 0 ? (unsigned long long)golden_fnv(samples, (size_t)frames * channels * 2) : 0ULL);
	if (frames > 0)
	{
		static const int rates[] = { 11025, 22050, 44100 };
		size_t which;

		for (which = 0; which < sizeof(rates) / sizeof(rates[0]); which++)
		{
			short *copy = malloc((size_t)frames * channels * 2);
			int out;

			if (!copy)
				break;
			memcpy(copy, samples, (size_t)frames * channels * 2);
			out = ce_vorbis_resample(&copy, frames, channels, rate, rates[which]);
			printf("%s ce-resample-%d %d %016llx\n", name, rates[which], out,
				out > 0 ? (unsigned long long)golden_fnv(copy, (size_t)out * channels * 2) : 0ULL);
			ce_vorbis_free(copy);
		}
	}
	if (samples)
		ce_vorbis_free(samples);
	{
		short head[48 * 2], tail[48 * 2];
		int whole;

		for (whole = 0; whole < 2; whole++)
		{
			int head_frames = 0, tail_frames = 0, edge_channels = 0, edge_rate = 0;
			int got;

			memset(head, 0, sizeof(head));
			memset(tail, 0, sizeof(tail));
			got = ce_vorbis_edges(data, size, 48, whole, &edge_channels, &edge_rate, head, &head_frames, tail,
				&tail_frames);
			printf("%s ce-edges-%d %d %d %d %d %d %016llx %016llx\n", name, whole, got, edge_channels, edge_rate,
				head_frames, tail_frames, (unsigned long long)golden_fnv(head, sizeof(head)),
				(unsigned long long)golden_fnv(tail, sizeof(tail)));
		}
	}
	{
		void *stream = ce_vorbis_stream_open(data, size, &channels, &rate);
		uint64_t hash = 0xcbf29ce484222325ULL;
		long total = 0;

		if (stream)
		{
			short buffer[333 * 2];
			int got;

			while ((got = ce_vorbis_stream_read(stream, buffer, 333)) > 0)
			{
				hash ^= golden_fnv(buffer, (size_t)got * channels * 2);
				hash *= 0x100000001b3ULL;
				total += got;
			}
			ce_vorbis_stream_close(stream);
		}
		printf("%s ce-stream %d %ld %016llx\n", name, stream != NULL, total, (unsigned long long)hash);
	}
	{
		short *pcm = NULL;
		uint32_t mcc_frames = 0, mcc_rate = 0;
		int mcc_channels = 0;
		int ok = mcc_vorbis_decode(data, (uint32_t)size, 0x1000000u, &pcm, &mcc_frames, &mcc_rate, &mcc_channels);

		printf("%s mcc-decode %d %lu %lu %d %016llx\n", name, ok, (unsigned long)mcc_frames,
			(unsigned long)mcc_rate, mcc_channels,
			ok ? (unsigned long long)golden_fnv(pcm, (size_t)mcc_frames * mcc_channels * 2) : 0ULL);
		if (pcm)
			mcc_vorbis_free(pcm);
	}
}

static int golden_vorbis(int count, char **paths)
{
	int which;

	for (which = 0; which < count; which++)
	{
		const char *name = strrchr(paths[which], '/') ? strrchr(paths[which], '/') + 1 : paths[which];
		FILE *file = fopen(paths[which], "rb");
		unsigned char *data, *changed;
		long size;
		char label[256];
		int flip;

		if (!file || fseek(file, 0, SEEK_END) || (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET))
			return 1;
		data = malloc((size_t)size);
		changed = malloc((size_t)size);
		if (!data || !changed || fread(data, 1, (size_t)size, file) != (size_t)size)
			return 1;
		fclose(file);
		golden_vorbis_one(name, data, (int)size);
		/* (cut short, and a byte changed here and there: what each decoder
		does with a damaged stream is kept too; not for a map's own streams,
		GOLDEN_VORBIS_WHOLE: tools/content_hashes.py) */
		if (getenv("GOLDEN_VORBIS_WHOLE"))
		{
			free(data);
			free(changed);
			continue;
		}
		snprintf(label, sizeof(label), "%s/half", name);
		golden_vorbis_one(label, data, (int)(size / 2));
		for (flip = 1; flip <= 3; flip++)
		{
			memcpy(changed, data, (size_t)size);
			changed[size * flip / 4] ^= 0x10;
			snprintf(label, sizeof(label), "%s/flip%d", name, flip);
			golden_vorbis_one(label, changed, (int)size);
		}
		free(data);
		free(changed);
	}
	return 0;
}
#endif

/* ---------- IMA ADPCM */

#if defined(GOLDEN_ADPCM)
/* (from dsound_sdl.c and mcc_audio.c, put in by the test) */
@GOLDEN_ADPCM_CODE@

/* each file named, its bytes as Xbox ADPCM blocks, mono then stereo, through
the mixer's decoder and MCC's (its blocks it refuses: a step past 88, a
reserved byte set): "<path> <decoder>-<channels> <frames> <check>"
(tools/content_hashes.py) */
static int golden_adpcm_files(int count, char **paths)
{
	int which;

	for (which = 0; which < count; which++)
	{
		size_t size;
		unsigned char *data = golden_file(paths[which], &size);
		unsigned long channels;

		if (!data)
			return 1;
		for (channels = 1; channels <= 2; channels++)
		{
			unsigned long frames = 0;
			short *samples = decode_adpcm(data, (unsigned long)size, channels, &frames);
			unsigned long block, blocks = (unsigned long)(size / (36 * channels)), refused = 0;
			uint64_t hash = 0xcbf29ce484222325ULL;

			printf("%s mixer-%lu %lu %016llx\n", paths[which], channels, frames,
				samples ? (unsigned long long)golden_fnv(samples, frames * channels * 2) : 0ULL);
			free(samples);
			/* (MCC's, block by block, as mcc_pcm_open reads one) */
			for (block = 0; block < blocks; block++)
			{
				const unsigned char *at = data + block * 36 * channels;
				short out[64 * 2];
				unsigned long channel;
				int ok = 1;

				for (channel = 0; channel < channels; channel++)
				{
					struct mcc_ima state;
					unsigned frame;

					state.value = (short)(at[channel * 4] | at[channel * 4 + 1] << 8);
					state.step = at[channel * 4 + 2];
					if (state.step > 88 || at[channel * 4 + 3])
					{
						ok = 0;
						break;
					}
					for (frame = 0; frame < 64; frame++)
					{
						unsigned char packed = at[channels * 4 + (frame / 8 * channels + channel) * 4 + frame % 8 / 2];

						out[frame * channels + channel] = (short)mcc_ima_advance(&state, (packed >> (frame % 2 * 4)) & 15);
					}
				}
				if (!ok)
				{
					refused++;
					continue;
				}
				hash ^= golden_fnv(out, 64 * channels * 2);
				hash *= 0x100000001b3ULL;
			}
			printf("%s mcc-%lu %lu %lu %016llx\n", paths[which], channels, blocks, refused, (unsigned long long)hash);
		}
		free(data);
	}
	return 0;
}

static int golden_adpcm(void)
{
	static const unsigned long sizes[] = { 0, 36, 72, 36 * 100, 36 * 2 * 37 };
	unsigned char *data = malloc(36 * 2 * 1000);
	size_t which;
	unsigned long channels;

	if (!data)
		return 1;
	/* the mixer's decoder, mono and stereo, on blocks of any bytes (step
	indexes past 88 too) */
	for (channels = 1; channels <= 2; channels++)
	{
		for (which = 0; which < sizeof(sizes) / sizeof(sizes[0]); which++)
		{
			unsigned long frames = 0;
			short *samples;

			golden_bytes(data, sizes[which] ? sizes[which] : 1, (uint32_t)(channels * 100 + which));
			samples = decode_adpcm(data, sizes[which], channels, &frames);
			printf("mixer-%lu-%lu %lu %016llx\n", channels, sizes[which], frames,
				samples ? (unsigned long long)golden_fnv(samples, frames * channels * 2) : 0ULL);
			free(samples);
		}
	}
	/* MCC's coder: its decoder over every code from every step, and its
	quantizer over a made-up signal (a sweep and noise), decoded back */
	{
		struct mcc_ima state;
		uint64_t hash = 0xcbf29ce484222325ULL;
		int step, value;
		unsigned code;

		for (step = 0; step <= 88; step++)
		{
			for (value = -32768; value <= 32767; value += 4093)
			{
				for (code = 0; code < 16; code++)
				{
					int out[2];

					state.value = value;
					state.step = step;
					out[0] = mcc_ima_advance(&state, code);
					out[1] = state.step;
					hash ^= golden_fnv(out, sizeof(out));
					hash *= 0x100000001b3ULL;
				}
			}
		}
		printf("mcc-advance %016llx\n", (unsigned long long)hash);
		hash = 0xcbf29ce484222325ULL;
		state.value = 0;
		state.step = 0;
		golden_seed = 1234;
		for (value = 0; value < 200000; value++)
		{
			int sample = (int)(((value * 37) % 65536) - 32768) / 2 + (int)(golden_next() % 4096) - 2048;
			int out[2];

			out[0] = (int)mcc_ima_quantize(&state, sample);
			out[1] = state.value;
			hash ^= golden_fnv(out, sizeof(out));
			hash *= 0x100000001b3ULL;
		}
		printf("mcc-quantize %016llx %d %d\n", (unsigned long long)hash, state.value, state.step);
	}
	/* the mixer's step over every code from every step, as MCC's above */
	{
		uint64_t hash = 0xcbf29ce484222325ULL;
		int step, value, nibble;

		for (step = 0; step <= 88; step++)
		{
			for (value = -32768; value <= 32767; value += 4093)
			{
				for (nibble = 0; nibble < 16; nibble++)
				{
					int out[2];

					out[1] = step;
					out[0] = value;
					out[0] = GOLDEN_MIXER_EXPAND(nibble, &out[0], &out[1]);
					hash ^= golden_fnv(out, sizeof(out));
					hash *= 0x100000001b3ULL;
				}
			}
		}
		printf("mixer-expand %016llx\n", (unsigned long long)hash);
	}
	free(data);
	return 0;
}
#endif

int main(int argc, char **argv)
{
#if defined(GOLDEN_SHA256)
	return argc > 1 ? golden_sha256_files(argc - 1, argv + 1) : golden_sha256();
#elif defined(GOLDEN_VORBIS)
	return golden_vorbis(argc - 1, argv + 1);
#elif defined(GOLDEN_ADPCM)
	return argc > 1 ? golden_adpcm_files(argc - 1, argv + 1) : golden_adpcm();
#else
	(void)argc;
	(void)argv;
	return 1;
#endif
}
