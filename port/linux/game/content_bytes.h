/*
CONTENT_BYTES.H

The game's one set of byte readers, header-only: whole numbers read a byte at a
time in the order a file or wire gives them (little- or big-endian, the same
on every processor, aligned or not), and values read in the processor's own
order out of memory the game already holds that way (tag data, which the
big-endian consoles swap as it loads). Each reader takes a pointer to at least
its size in bytes: checking that it is in range is the caller's. C89 too
(delta_wire.c, map_report.c).
*/

#ifndef CONTENT_BYTES_H
#define CONTENT_BYTES_H

#include <stdint.h>
#include <string.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define CONTENT_BYTES_INLINE static inline
#elif defined(__GNUC__) || defined(__clang__)
#define CONTENT_BYTES_INLINE static __inline__
#elif defined(_MSC_VER)
#define CONTENT_BYTES_INLINE static __inline
#else
#define CONTENT_BYTES_INLINE static
#endif

/* ---------- in a file's or a wire's order */

CONTENT_BYTES_INLINE uint16_t content_u16le(const void *at)
{
	const unsigned char *bytes = (const unsigned char *)at;

	return (uint16_t)((unsigned int)bytes[0] | (unsigned int)bytes[1] << 8);
}

CONTENT_BYTES_INLINE uint32_t content_u32le(const void *at)
{
	const unsigned char *bytes = (const unsigned char *)at;

	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

CONTENT_BYTES_INLINE uint16_t content_u16be(const void *at)
{
	const unsigned char *bytes = (const unsigned char *)at;

	return (uint16_t)((unsigned int)bytes[0] << 8 | (unsigned int)bytes[1]);
}

CONTENT_BYTES_INLINE uint32_t content_u32be(const void *at)
{
	const unsigned char *bytes = (const unsigned char *)at;

	return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | (uint32_t)bytes[3];
}

/* ---------- in the processor's own order (memory the game holds) */

CONTENT_BYTES_INLINE uint32_t content_u32(const void *at)
{
	uint32_t value;

	memcpy(&value, at, sizeof(value));
	return value;
}

CONTENT_BYTES_INLINE int32_t content_s32(const void *at)
{
	int32_t value;

	memcpy(&value, at, sizeof(value));
	return value;
}

CONTENT_BYTES_INLINE int16_t content_s16(const void *at)
{
	int16_t value;

	memcpy(&value, at, sizeof(value));
	return value;
}

CONTENT_BYTES_INLINE float content_f32(const void *at)
{
	float value;

	memcpy(&value, at, sizeof(value));
	return value;
}

#endif
