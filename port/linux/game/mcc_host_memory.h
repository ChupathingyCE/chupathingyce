/* port: an MCC unit's own buffers (file copies, decoded audio and pixels,
validation maps) in the host's memory, not the game's heap (cseries.h's
malloc: the Xbox address space in a 64-bit build, xbox_heap.c), which a
large MCC map would fill. Anything a tag names by its 32-bit address is in
the MCC tags (mcc_runtime_allocate) or the game's heap (system_malloc). After
cseries.h */
#ifndef MCC_HOST_MEMORY_H
#define MCC_HOST_MEMORY_H
#include <stdlib.h>
#undef malloc
#undef free
#undef realloc
#undef calloc
#endif
