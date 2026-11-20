/*
H2_BUILD_HEAP.H

The Halo 2 level builder's heap (the Vita tier): while a level is made
(h2_level.c's h2_level_build), what the builder's own files allocate with
malloc, calloc and realloc comes from a heap of its own - TLSF
(port/third_party/tlsf) over memory blocks of user RAM, outside the C heap -
not from the C heap.

Why: the Vita's C heap is newlib's fixed 48 MB, which the system's libraries,
the renderer and the menus share (28-34 MB of it in use with the menus up),
and a level's build held 20-130 MB of it at its peak (the gathered models,
animations, weapons and AI, the level's tag data): no level could be made on
the device. The Vita has 60-100 MB of user RAM free outside the C heap and
the memory window then.

The builder's files (those that include this header with H2_BUILD_HEAP_WRAP
defined, after their system headers: their memory is all their own) have
malloc, calloc, realloc and free made these functions'. An allocation made
while no build runs, or that the heap has no room for (on the Vita the free
user RAM less 8 MB when the build begins, on the harness 64 MB;
HALO_H2_BUILD_HEAP_MB=n either way, 0: none), is the C heap's as before; free and realloc go
where the pointer is from. The heap's blocks are given back when a build
ends with nothing of it in use (what a build keeps past its end keeps the
heap). Only these files may free what they allocated during a build: the
harness's own free (port/linux/src/xbox_kernel.c) finds and logs any other.

The other builds (the desktop's PC tier, Android, the servers) are as
before: the C library's allocator.
*/

#ifndef __H2_BUILD_HEAP_H
#define __H2_BUILD_HEAP_H

#include <stddef.h>

#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
#define H2_BUILD_HEAP 1
#else
#define H2_BUILD_HEAP 0
#endif

/* (tick_thread.c, at its start) the calling thread plays - the tick -: its
allocations are the C heap's while a build another thread began is on (the
HUD's make at load on the game thread), never the heap's - what it makes in
the wrapped files is play memory that other files free */
void h2_build_heap_thread_plays(void);

/* a build begins (its allocations the heap's) and ends (the heap's blocks
given back if nothing of it is in use; its figures logged) */
void h2_build_heap_begin(void);
void h2_build_heap_end(void);

/* (h3vita) whether the builds from here may take blocks of video memory
(CDRAM, read uncached by the CPU) once user RAM is at its floor - a Halo 3
level's make around itself: its peak is past what the Vita's user RAM has
beside the game -; HALO_H2_BUILD_HEAP_CDRAM=0/1 overrides at a build's begin.
Harness: HALO_CDRAM_MB=n adds that room */
void h2_build_heap_video_memory(int allowed);

void *h2_build_heap_malloc(size_t size);
void *h2_build_heap_calloc(size_t count, size_t size);
void *h2_build_heap_realloc(void *pointer, size_t size);
void h2_build_heap_free(void *pointer);

/* (harness, debug: HALO_H2_BUILD_HEAP_TRACE=1) the live allocations' bytes
by their callers, logged */
void h2_build_heap_trace_log(char const *when);

/* whether a build is on (between h2_build_heap_begin and end): a part made
at load from a map (h2_hud_make_loaded, h2_menu_make_loaded) opens the heap
for its own make only when none is */
int h2_build_heap_active(void);

/* memory the build made that lasts past it (the direct level's kept
pixels, tables, copies): a small allocation of the heap's moved to the C
heap, so it holds no block of the heap's after the make; the pointer to
keep (pointer itself if not the heap's, large - a block of its own, given
back alone - or the C heap had no room) */
void *h2_build_heap_keep(void *pointer);

/* (piece H) what the build's heap has left of its room now (0: none, or no
build on): what work made ahead may take without the make running short */
unsigned long h2_build_heap_room_left(void);

/* allocations since the build began that got no memory at all (the heap's
nor the C heap's; not one of 4 MB or more, whose callers try less) and the
first one's size: what a maker dropped for want of memory - the build fails
on any (h2_level.c), never a level with pieces left out */
unsigned long h2_build_heap_failed(unsigned long *first_size);
/* (the makers) the part this thread makes now ("miranda's render model"),
until the next: the first allocation that got no memory names it
(h2_build_heap_failed_unit: "" if none was named), and the failed make with
it (h2_level.c build_memory_whole). NULL or "": none */
void h2_build_heap_unit(char const *what);
char const *h2_build_heap_failed_unit(void);

/* (an MCC cache's inflated piece the window and the C heap had no room
for, h2_cache.c) a user RAM block of its own; NULL if the system has too
little free. h2_user_block_free gives one back - FALSE if data is not one */
void *h2_user_block_alloc(unsigned long size);
/* (the same, the block named for the user RAM census: Jolt's large
allocations the C heap had no room for, physics_jolt.cpp) */
void *h2_user_block_alloc_named(char const *name, unsigned long size);
/* (h3vita: a Halo 3 level's kept pixels, h3_level.c) a block of the Vita's
physically contiguous main memory - 26 MB free beside user RAM through a
run, the movie decoder's alone - and only that; NULL where there is none
(the harness, or taken). h2_user_block_free gives it back */
void *h2_phycont_block_alloc_named(char const *name, unsigned long size);
void *h2_any_block_alloc_named(char const *name, unsigned long size, int *where);
/* (h3vita) a block of video memory alone (the Vita's CDRAM, read uncached by
the CPU; NULL elsewhere): what a Halo 3 level keeps when neither the
contiguous main memory nor user RAM has the room */
void *h2_video_block_alloc_named(char const *name, unsigned long size);
int h2_user_block_free(void *data);

/* whether pointer is in the heap's blocks */
int h2_build_heap_owns(void const *pointer);

/* (the build's log line) bytes given out now and at most, and the blocks' */
void h2_build_heap_figures(unsigned long *in_use, unsigned long *peak, unsigned long *blocks);
/* large allocations refused since the build began: the heap had no room
and the C heap is not to take them - the level too big for the builder's
memory (a build that fails says so, h2_level.c build_fail) */
unsigned long h2_build_heap_refused_large(void);

/* (piece H) what lets memory go when the build's heap has no room for an
allocation (the chunks the make keeps, h2_cache.c; the BSPs' sections made
ahead, h2_level.c): each added called with no lock of the heap's held, and
the allocation tried once more */
void h2_build_heap_pressure_add(void (*release)(void));
void h2_build_heap_pressure_remove(void (*release)(void));

/* (debug) the calling thread's make in the derive arena: begun, and its
largest allocation and the arena's bytes in use at most while it ran */
void h2_derive_heap_make_begin(void);
void h2_derive_heap_make_figures(unsigned long *largest, unsigned long *peak);

/* (piece J) the derive arena: a second heap of the same kind for the
derived bitmaps' worker threads (h2_texture_source.c) - reserved at a
level's load (reserve_mb, in blocks of block_mb, at most room_mb), the
allocations of the threads that called h2_derive_heap_thread its, never the
C heap's (none it has no room for is the C heap's: logged as a sizing bug);
freed wherever they are freed; given back at the level's end */
void h2_derive_heap_begin(long room_mb, long reserve_mb, long block_mb);
void h2_derive_heap_thread(void);
void h2_derive_heap_end(void);
void h2_derive_heap_figures(unsigned long *in_use, unsigned long *peak, unsigned long *blocks, unsigned long *failures);

#if defined(H2_BUILD_HEAP_WRAP) && H2_BUILD_HEAP
#undef malloc
#undef calloc
#undef realloc
#undef free
#define malloc(size) h2_build_heap_malloc(size)
#define calloc(count, size) h2_build_heap_calloc(count, size)
#define realloc(pointer, size) h2_build_heap_realloc(pointer, size)
#define free(pointer) h2_build_heap_free(pointer)
#endif

#endif
