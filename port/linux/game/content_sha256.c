/*
CONTENT_SHA256.C

The game's one SHA-256 (content_sha256.h): FIPS 180-4, adapted from this
repository's CC0 p2p_crypto.c, with the processor's own SHA-256 instructions
where it has them. Map identity and MCC content digests are on the wire, so
every copy of it the loaders, internet play, Delta and the updater had is a
wrapper of this one now (p2p_crypto.c, identity.c, playlist_manifest.c,
update_manifest.c; content_sha256.h's names).
*/

#include "content_sha256.h"
#include "mcc_sha256.h"
#if !defined(HALO_CONSOLE) && !defined(HALO_XBOX_CONSOLE)
#include <stdlib.h>
#endif
#include <string.h>

static const unsigned int content_sha256_constants[64] =
{
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROTATE_RIGHT(value, count) (((value) >> (count)) | ((value) << (32 - (count))))

static void content_sha256_block(uint32_t *state, const unsigned char *block)
{
	unsigned int words[64];
	unsigned int a, b, c, d, e, f, g, h;
	int index;

	for (index = 0; index < 16; index++)
	{
		words[index] = (unsigned int)block[index * 4] << 24 | (unsigned int)block[index * 4 + 1] << 16 |
			(unsigned int)block[index * 4 + 2] << 8 | (unsigned int)block[index * 4 + 3];
	}
	for (index = 16; index < 64; index++)
	{
		unsigned int s0 = ROTATE_RIGHT(words[index - 15], 7) ^ ROTATE_RIGHT(words[index - 15], 18) ^
			(words[index - 15] >> 3);
		unsigned int s1 = ROTATE_RIGHT(words[index - 2], 17) ^ ROTATE_RIGHT(words[index - 2], 19) ^
			(words[index - 2] >> 10);

		words[index] = words[index - 16] + s0 + words[index - 7] + s1;
	}
	a = state[0]; b = state[1]; c = state[2]; d = state[3];
	e = state[4]; f = state[5]; g = state[6]; h = state[7];
	for (index = 0; index < 64; index++)
	{
		unsigned int s1 = ROTATE_RIGHT(e, 6) ^ ROTATE_RIGHT(e, 11) ^ ROTATE_RIGHT(e, 25);
		unsigned int choice = (e & f) ^ (~e & g);
		unsigned int first = h + s1 + choice + content_sha256_constants[index] + words[index];
		unsigned int s0 = ROTATE_RIGHT(a, 2) ^ ROTATE_RIGHT(a, 13) ^ ROTATE_RIGHT(a, 22);
		unsigned int majority = (a & b) ^ (a & c) ^ (b & c);
		unsigned int second = s0 + majority;

		h = g; g = f; f = e; e = d + first;
		d = c; c = b; b = a; a = first + second;
	}
	state[0] += a; state[1] += b; state[2] += c; state[3] += d;
	state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

/* blocks (count of 64 bytes) of data into the state */
typedef void (*content_sha256_blocks_function)(uint32_t *state, const unsigned char *data, size_t count);

static void content_sha256_blocks_portable(uint32_t *state, const unsigned char *data, size_t count)
{
	while (count--)
	{
		content_sha256_block(state, data);
		data += 64;
	}
}

/* The processor's own SHA-256 instructions, where it has them: x86's SHA
extensions (Intel since Goldmont and Ice Lake, AMD since Zen) and ARMv8's
(every Apple silicon Mac, nearly every 64-bit ARM phone, board and server).
Each is compiled for its instructions alone and used only when the
processor says it has them (content_sha256_blocks_choose); the same digest as
the portable code, which every other processor and the consoles use. */
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__clang__) || defined(__GNUC__)) && \
	!defined(HALO_CONSOLE) && !defined(HALO_XBOX_CONSOLE)
#define CONTENT_SHA256_X86 1
#include <cpuid.h>
#include <immintrin.h>

__attribute__((target("sha,sse4.1")))
static void content_sha256_blocks_x86(uint32_t *state, const unsigned char *data, size_t count)
{
	const __m128i byte_swap = _mm_set_epi64x(0x0c0d0e0f08090a0bULL, 0x0405060700010203ULL);
	__m128i state0, state1, saved0, saved1, message, message0, message1, message2, message3, swap;

	/* (the state as the instructions keep it: ABEF and CDGH) */
	swap = _mm_loadu_si128((const __m128i *)&state[0]);
	state1 = _mm_loadu_si128((const __m128i *)&state[4]);
	swap = _mm_shuffle_epi32(swap, 0xB1);
	state1 = _mm_shuffle_epi32(state1, 0x1B);
	state0 = _mm_alignr_epi8(swap, state1, 8);
	state1 = _mm_blend_epi16(state1, swap, 0xF0);

#define CONTENT_SHA256_X86_ROUNDS(k3, k2, k1, k0, words) \
	message = _mm_add_epi32((words), _mm_set_epi64x((long long)(((unsigned long long)(k3) << 32) | (k2)), \
		(long long)(((unsigned long long)(k1) << 32) | (k0)))); \
	state1 = _mm_sha256rnds2_epu32(state1, state0, message); \
	message = _mm_shuffle_epi32(message, 0x0E); \
	state0 = _mm_sha256rnds2_epu32(state0, state1, message)
	/* (the schedule's next four words, into to: from it and the twelve after
	it, next, third and last) */
#define CONTENT_SHA256_X86_SCHEDULE(to, next, third, last) \
	to = _mm_add_epi32(_mm_sha256msg1_epu32(to, next), _mm_alignr_epi8(last, third, 4)); \
	to = _mm_sha256msg2_epu32(to, last)

	while (count--)
	{
		saved0 = state0;
		saved1 = state1;
		message0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 0)), byte_swap);
		message1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 16)), byte_swap);
		message2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 32)), byte_swap);
		message3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 48)), byte_swap);

		CONTENT_SHA256_X86_ROUNDS(0xe9b5dba5, 0xb5c0fbcf, 0x71374491, 0x428a2f98, message0);
		CONTENT_SHA256_X86_ROUNDS(0xab1c5ed5, 0x923f82a4, 0x59f111f1, 0x3956c25b, message1);
		CONTENT_SHA256_X86_ROUNDS(0x550c7dc3, 0x243185be, 0x12835b01, 0xd807aa98, message2);
		CONTENT_SHA256_X86_ROUNDS(0xc19bf174, 0x9bdc06a7, 0x80deb1fe, 0x72be5d74, message3);
		CONTENT_SHA256_X86_SCHEDULE(message0, message1, message2, message3);
		CONTENT_SHA256_X86_ROUNDS(0x240ca1cc, 0x0fc19dc6, 0xefbe4786, 0xe49b69c1, message0);
		CONTENT_SHA256_X86_SCHEDULE(message1, message2, message3, message0);
		CONTENT_SHA256_X86_ROUNDS(0x76f988da, 0x5cb0a9dc, 0x4a7484aa, 0x2de92c6f, message1);
		CONTENT_SHA256_X86_SCHEDULE(message2, message3, message0, message1);
		CONTENT_SHA256_X86_ROUNDS(0xbf597fc7, 0xb00327c8, 0xa831c66d, 0x983e5152, message2);
		CONTENT_SHA256_X86_SCHEDULE(message3, message0, message1, message2);
		CONTENT_SHA256_X86_ROUNDS(0x14292967, 0x06ca6351, 0xd5a79147, 0xc6e00bf3, message3);
		CONTENT_SHA256_X86_SCHEDULE(message0, message1, message2, message3);
		CONTENT_SHA256_X86_ROUNDS(0x53380d13, 0x4d2c6dfc, 0x2e1b2138, 0x27b70a85, message0);
		CONTENT_SHA256_X86_SCHEDULE(message1, message2, message3, message0);
		CONTENT_SHA256_X86_ROUNDS(0x92722c85, 0x81c2c92e, 0x766a0abb, 0x650a7354, message1);
		CONTENT_SHA256_X86_SCHEDULE(message2, message3, message0, message1);
		CONTENT_SHA256_X86_ROUNDS(0xc76c51a3, 0xc24b8b70, 0xa81a664b, 0xa2bfe8a1, message2);
		CONTENT_SHA256_X86_SCHEDULE(message3, message0, message1, message2);
		CONTENT_SHA256_X86_ROUNDS(0x106aa070, 0xf40e3585, 0xd6990624, 0xd192e819, message3);
		CONTENT_SHA256_X86_SCHEDULE(message0, message1, message2, message3);
		CONTENT_SHA256_X86_ROUNDS(0x34b0bcb5, 0x2748774c, 0x1e376c08, 0x19a4c116, message0);
		CONTENT_SHA256_X86_SCHEDULE(message1, message2, message3, message0);
		CONTENT_SHA256_X86_ROUNDS(0x682e6ff3, 0x5b9cca4f, 0x4ed8aa4a, 0x391c0cb3, message1);
		CONTENT_SHA256_X86_SCHEDULE(message2, message3, message0, message1);
		CONTENT_SHA256_X86_ROUNDS(0x8cc70208, 0x84c87814, 0x78a5636f, 0x748f82ee, message2);
		CONTENT_SHA256_X86_SCHEDULE(message3, message0, message1, message2);
		CONTENT_SHA256_X86_ROUNDS(0xc67178f2, 0xbef9a3f7, 0xa4506ceb, 0x90befffa, message3);

		state0 = _mm_add_epi32(state0, saved0);
		state1 = _mm_add_epi32(state1, saved1);
		data += 64;
	}
#undef CONTENT_SHA256_X86_ROUNDS
#undef CONTENT_SHA256_X86_SCHEDULE

	/* (back to ABCD and EFGH) */
	swap = _mm_shuffle_epi32(state0, 0x1B);
	state1 = _mm_shuffle_epi32(state1, 0xB1);
	state0 = _mm_blend_epi16(swap, state1, 0xF0);
	state1 = _mm_alignr_epi8(state1, swap, 8);
	_mm_storeu_si128((__m128i *)&state[0], state0);
	_mm_storeu_si128((__m128i *)&state[4], state1);
}

static int content_sha256_x86_available(void)
{
	unsigned int eax, ebx, ecx, edx;

	/* (SSSE3 and SSE4.1 for the shuffles and blends, and the SHA extensions) */
	if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx) || !(ecx & (1u << 9)) || !(ecx & (1u << 19)))
		return 0;
	if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx))
		return 0;
	return (ebx & (1u << 29)) != 0;
}
#endif

/* (64-bit ARM only: Android's 32-bit guest, arm64_32, has no auxiliary
vector to ask) */
#if defined(__aarch64__) && defined(__LP64__) && (defined(__clang__) || defined(__GNUC__)) && \
	!defined(HALO_CONSOLE) && !defined(HALO_XBOX_CONSOLE) && \
	(defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__))
#define CONTENT_SHA256_ARM 1
#include <arm_neon.h>
/* (the C library's, declared here: the game's units see the system's
headers through the port's own, which sys/auxv.h does not go with) */
#if defined(__linux__)
unsigned long getauxval(unsigned long type);
#define CONTENT_SHA256_AT_HWCAP 16
#elif defined(__FreeBSD__)
int elf_aux_info(int type, void *buffer, int size);
#define CONTENT_SHA256_AT_HWCAP 25
#endif
/* (HWCAP_SHA2, on Linux and FreeBSD alike) */
#define CONTENT_SHA256_HWCAP_SHA2 (1ul << 6)

#if defined(__clang__)
#define CONTENT_SHA256_ARM_TARGET __attribute__((target("sha2")))
#else
#define CONTENT_SHA256_ARM_TARGET __attribute__((target("+crypto")))
#endif

CONTENT_SHA256_ARM_TARGET
static void content_sha256_blocks_arm(uint32_t *state, const unsigned char *data, size_t count)
{
	uint32x4_t state0 = vld1q_u32(&state[0]);
	uint32x4_t state1 = vld1q_u32(&state[4]);

	while (count--)
	{
		uint32x4_t saved0 = state0, saved1 = state1, previous, message[4];
		int index;

		for (index = 0; index < 4; index++)
			message[index] = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(data + index * 16)));
		/* (four rounds at a time, the schedule's next four words made from
		the last sixteen as they go) */
		for (index = 0; index < 16; index++)
		{
			uint32x4_t words = message[index & 3];
			uint32x4_t added = vaddq_u32(words, vld1q_u32((const uint32_t *)&content_sha256_constants[index * 4]));

			if (index < 12)
			{
				message[index & 3] = vsha256su1q_u32(vsha256su0q_u32(words, message[(index + 1) & 3]),
					message[(index + 2) & 3], message[(index + 3) & 3]);
			}
			previous = state0;
			state0 = vsha256hq_u32(state0, state1, added);
			state1 = vsha256h2q_u32(state1, previous, added);
		}
		state0 = vaddq_u32(state0, saved0);
		state1 = vaddq_u32(state1, saved1);
		data += 64;
	}
	vst1q_u32(&state[0], state0);
	vst1q_u32(&state[4], state1);
}

static int content_sha256_arm_available(void)
{
#if defined(__APPLE__)
	/* (every 64-bit Apple processor has them, from the A7 and every Apple
	silicon Mac) */
	return 1;
#elif defined(__linux__)
	return (getauxval(CONTENT_SHA256_AT_HWCAP) & CONTENT_SHA256_HWCAP_SHA2) != 0;
#else
	unsigned long capabilities = 0;

	return elf_aux_info(CONTENT_SHA256_AT_HWCAP, &capabilities, (int)sizeof(capabilities)) == 0 &&
		(capabilities & CONTENT_SHA256_HWCAP_SHA2) != 0;
#endif
}
#endif

static content_sha256_blocks_function content_sha256_blocks_chosen;

/* the processor's SHA-256 instructions, or the portable code: asked once
(two threads asking at once choose the same) */
static content_sha256_blocks_function content_sha256_blocks_choose(void)
{
	content_sha256_blocks_function chosen = content_sha256_blocks_chosen;

	if (chosen)
		return chosen;
	chosen = content_sha256_blocks_portable;
#ifdef CONTENT_SHA256_X86
	if (content_sha256_x86_available())
		chosen = content_sha256_blocks_x86;
#endif
#ifdef CONTENT_SHA256_ARM
	if (content_sha256_arm_available())
		chosen = content_sha256_blocks_arm;
#endif
#if !defined(HALO_CONSOLE) && !defined(HALO_XBOX_CONSOLE)
	/* (MCC_SHA256_PORTABLE set: the portable code, to compare and time; a
	console has no environment, and only the portable code) */
	if (getenv("MCC_SHA256_PORTABLE"))
		chosen = content_sha256_blocks_portable;
#endif
	content_sha256_blocks_chosen = chosen;
	return chosen;
}

const char *content_sha256_implementation(void)
{
	content_sha256_blocks_function chosen = content_sha256_blocks_choose();

#ifdef CONTENT_SHA256_X86
	if (chosen == content_sha256_blocks_x86)
		return "x86 SHA extensions";
#endif
#ifdef CONTENT_SHA256_ARM
	if (chosen == content_sha256_blocks_arm)
		return "ARMv8 SHA-256 instructions";
#endif
	return "portable";
}

void content_sha256_begin(struct content_sha256 *context)
{
	static const unsigned int initial[8] =
	{
		0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
	};

	memcpy(context->state, initial, sizeof(initial));
	context->length = 0;
	context->used = 0;
}

void content_sha256_add(struct content_sha256 *context, const void *data, size_t size)
{
	const unsigned char *bytes = data;

	if (!size)
		return;
	context->length += (uint64_t)size;
	/* (a block begun before, filled first) */
	if (context->used)
	{
		size_t count = (size_t)(64 - context->used) < size ? (size_t)(64 - context->used) : size;

		memcpy(context->block + context->used, bytes, count);
		context->used += (int)count;
		bytes += count;
		size -= count;
		if (context->used < 64)
			return;
		content_sha256_blocks_choose()(context->state, context->block, 1);
		context->used = 0;
	}
	/* (whole blocks straight from the data: a map's tags are megabytes) */
	if (size >= 64)
	{
		content_sha256_blocks_choose()(context->state, bytes, size / 64);
		bytes += size & ~(size_t)63;
		size &= 63;
	}
	memcpy(context->block, bytes, size);
	context->used = (int)size;
}

void content_sha256_end(struct content_sha256 *context, unsigned char digest[CONTENT_SHA256_SIZE])
{
	uint64_t bits = context->length * 8;
	unsigned char length[8];
	int index;

	for (index = 0; index < 8; index++)
		length[index] = (unsigned char)(bits >> (56 - index * 8));
	content_sha256_add(context, "\x80", 1);
	while (context->used != 56)
		content_sha256_add(context, "", 1);
	content_sha256_add(context, length, 8);
	for (index = 0; index < 8; index++)
	{
		digest[index * 4] = (unsigned char)(context->state[index] >> 24);
		digest[index * 4 + 1] = (unsigned char)(context->state[index] >> 16);
		digest[index * 4 + 2] = (unsigned char)(context->state[index] >> 8);
		digest[index * 4 + 3] = (unsigned char)context->state[index];
	}
}

void content_sha256(const void *data, size_t size, unsigned char digest[CONTENT_SHA256_SIZE])
{
	struct content_sha256 context;

	content_sha256_begin(&context);
	content_sha256_add(&context, data, size);
	content_sha256_end(&context, digest);
}

/* mcc_sha256.h's add, which took an int: zero or less adds nothing */
void mcc_sha256_add(struct content_sha256 *context, void const *data, int size)
{
	if (size <= 0)
		return;
	content_sha256_add(context, data, (size_t)size);
}
