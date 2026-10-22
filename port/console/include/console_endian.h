/*
CONSOLE_ENDIAN.H

Endianness handling and byte-swapping utilities for Warthog.

The Halo CE data formats (.map cache files, tag declarations, saved games,
and network packets) were designed for little-endian x86 hardware (Original
Xbox and PC).

When running on big-endian architectures (PowerPC on Xbox 360 Xenon and
Nintendo Wii U Espresso), data loaded from map files and wire packets must
be byte-swapped into CPU-native order. Furthermore, PowerPC can trap or
suffer penalties on misaligned memory access, so unaligned multi-byte buffer
readers are provided here.
*/

#ifndef __CONSOLE_ENDIAN_H
#define __CONSOLE_ENDIAN_H

#include <stdint.h>

#if defined(__has_include)
#if __has_include(<string.h>)
#include <string.h>
#endif
#endif

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define CONSOLE_IS_BIG_ENDIAN 1
#elif defined(_M_PPCBE) || defined(_M_PPC) || defined(__PPC__) || defined(__powerpc__) || defined(HALO_BIG_ENDIAN)
#define CONSOLE_IS_BIG_ENDIAN 1
#else
#define CONSOLE_IS_BIG_ENDIAN 0
#endif

/* Compiler-level byte swap intrinsics */
#if defined(__GNUC__) || defined(__clang__)
#define CONSOLE_BSWAP16(x) __builtin_bswap16((uint16_t)(x))
#define CONSOLE_BSWAP32(x) __builtin_bswap32((uint32_t)(x))
#define CONSOLE_BSWAP64(x) __builtin_bswap64((uint64_t)(x))
#elif defined(_MSC_VER)
#include <stdlib.h>
#define CONSOLE_BSWAP16(x) _byteswap_ushort((uint16_t)(x))
#define CONSOLE_BSWAP32(x) _byteswap_ulong((uint32_t)(x))
#define CONSOLE_BSWAP64(x) _byteswap_uint64((uint64_t)(x))
#else
static inline uint16_t CONSOLE_BSWAP16(uint16_t x) {
	return (uint16_t)((x << 8) | (x >> 8));
}
static inline uint32_t CONSOLE_BSWAP32(uint32_t x) {
	return ((x << 24) & 0xFF000000) |
	       ((x << 8)  & 0x00FF0000) |
	       ((x >> 8)  & 0x0000FF00) |
	       ((x >> 24) & 0x000000FF);
}
static inline uint64_t CONSOLE_BSWAP64(uint64_t x) {
	return (((uint64_t)CONSOLE_BSWAP32((uint32_t)(x & 0xFFFFFFFF))) << 32) |
	       CONSOLE_BSWAP32((uint32_t)(x >> 32));
}
#endif

/* IEEE-754 32-bit float byte swap */
static inline float console_bswap_float(float f) {
	union {
		float f;
		uint32_t u;
	} conv;
	conv.f = f;
	conv.u = CONSOLE_BSWAP32(conv.u);
	return conv.f;
}

/* Endian-conditional conversion macros */
#if CONSOLE_IS_BIG_ENDIAN

#define CPU_TO_LE16(x) CONSOLE_BSWAP16(x)
#define LE16_TO_CPU(x) CONSOLE_BSWAP16(x)
#define CPU_TO_LE32(x) CONSOLE_BSWAP32(x)
#define LE32_TO_CPU(x) CONSOLE_BSWAP32(x)
#define CPU_TO_LE64(x) CONSOLE_BSWAP64(x)
#define LE64_TO_CPU(x) CONSOLE_BSWAP64(x)
#define CPU_TO_LE_FLOAT(x) console_bswap_float(x)
#define LE_FLOAT_TO_CPU(x) console_bswap_float(x)

#define CPU_TO_BE16(x) ((uint16_t)(x))
#define BE16_TO_CPU(x) ((uint16_t)(x))
#define CPU_TO_BE32(x) ((uint32_t)(x))
#define BE32_TO_CPU(x) ((uint32_t)(x))

#else

#define CPU_TO_LE16(x) ((uint16_t)(x))
#define LE16_TO_CPU(x) ((uint16_t)(x))
#define CPU_TO_LE32(x) ((uint32_t)(x))
#define LE32_TO_CPU(x) ((uint32_t)(x))
#define CPU_TO_LE64(x) ((uint64_t)(x))
#define LE64_TO_CPU(x) ((uint64_t)(x))
#define CPU_TO_LE_FLOAT(x) (x)
#define LE_FLOAT_TO_CPU(x) (x)

#define CPU_TO_BE16(x) CONSOLE_BSWAP16(x)
#define BE16_TO_CPU(x) CONSOLE_BSWAP16(x)
#define CPU_TO_BE32(x) CONSOLE_BSWAP32(x)
#define BE32_TO_CPU(x) CONSOLE_BSWAP32(x)

#endif

/*
Safe alignment-tolerant memory readers for unaligned byte buffers.
PowerPC architecture generates alignment faults on misaligned word reads.
*/
static inline uint16_t console_read_le16(const void *ptr) {
	uint16_t val;
	__builtin_memcpy(&val, ptr, sizeof(val));
	return LE16_TO_CPU(val);
}

static inline uint32_t console_read_le32(const void *ptr) {
	uint32_t val;
	__builtin_memcpy(&val, ptr, sizeof(val));
	return LE32_TO_CPU(val);
}

static inline float console_read_le_float(const void *ptr) {
	uint32_t raw;
	union {
		uint32_t u;
		float f;
	} conv;
	__builtin_memcpy(&raw, ptr, sizeof(raw));
	conv.u = LE32_TO_CPU(raw);
	return conv.f;
}

static inline void console_write_le16(void *ptr, uint16_t val) {
	uint16_t le = CPU_TO_LE16(val);
	__builtin_memcpy(ptr, &le, sizeof(le));
}

static inline void console_write_le32(void *ptr, uint32_t val) {
	uint32_t le = CPU_TO_LE32(val);
	__builtin_memcpy(ptr, &le, sizeof(le));
}

/* In-place byte swapping helpers for loaded tag headers */
static inline void console_swap_tag_header(void *tag_header_ptr) {
#if CONSOLE_IS_BIG_ENDIAN
	/* Swap 32-bit tag group magic, ID, and data offset */
	uint32_t *p32 = (uint32_t *)tag_header_ptr;
	p32[0] = CONSOLE_BSWAP32(p32[0]); /* tag group magic */
	p32[1] = CONSOLE_BSWAP32(p32[1]); /* parent group magic */
	p32[2] = CONSOLE_BSWAP32(p32[2]); /* grandparent group magic */
	p32[3] = CONSOLE_BSWAP32(p32[3]); /* tag datum index/id */
#else
	(void)tag_header_ptr;
#endif
}

#endif /* __CONSOLE_ENDIAN_H */
