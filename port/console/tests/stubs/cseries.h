/* host-side stand-in for the game's cseries.h (tag_byte_swap_test.c) */
#include <string.h>
#define NONE (-1)
#define csmemset memset

/* as the game's cseries.h: malloc and free are the game's own allocator
(match_malloc and match_free), calloc stays the C library's. The game's free
refuses a block it did not hand out (debug_memory.c, "Signature is
incorrect"), so this one does too */
#include <stdio.h>
#include <stdlib.h>
#define STUB_MATCH_SIGNATURE 0x6D617463UL
#define STUB_MATCH_HEADER 16
static inline void *stub_match_malloc(size_t size)
{
	unsigned long *block = (malloc)(size + STUB_MATCH_HEADER);

	if (!block)
		return NULL;
	block[0] = STUB_MATCH_SIGNATURE;
	return (unsigned char *)block + STUB_MATCH_HEADER;
}
static inline void stub_match_free(void *pointer)
{
	unsigned long *block;

	if (!pointer)
		return;
	block = (unsigned long *)((unsigned char *)pointer - STUB_MATCH_HEADER);
	if (block[0] != STUB_MATCH_SIGNATURE)
	{
		fprintf(stderr, "free: a block the game's malloc did not hand out\n");
		abort();
	}
	block[0] = 0;
	(free)(block);
}
#define malloc(size) stub_match_malloc(size)
#define free(pointer) stub_match_free(pointer)
