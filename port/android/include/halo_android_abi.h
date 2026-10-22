/*
HALO_ANDROID_ABI.H

The contract between the two halves of the Android port (see
port/android/README.md):

- the guest: the game, the platform layer shared with the Linux port and a
  small C runtime, compiled as ILP32 AArch64 code (32-bit pointers) and
  linked into a static image that runs in the low 4 GB of the process;
- the host: an ordinary 64-bit Android library (libmain.so) that loads the
  image, owns the process (SDL3, OpenGL ES, bionic) and serves the guest's
  requests.

The guest calls the host through import stubs that jump through a table of
64-bit function pointers the host fills in at load time. Only types whose
layout and register treatment agree between the two ABIs cross this
boundary: 32-bit integers, 64-bit integers, floats, and pointers (which
arm64_32 always passes zero-extended). Structures shared here are made of
fixed-width members only.

This header is included by both halves.
*/

#ifndef __HALO_ANDROID_ABI_H
#define __HALO_ANDROID_ABI_H

#include <stdint.h>

#ifdef HALO_SWITCH_CONSOLE
/* The Switch's guest image is linked to run here, and its host loads it here
always: it cannot move the image. 0x88000000, Android's, is outside the
only range a Switch maps memory in (0x08000000 to 0x80000000). This is below
the range Halo Custom Edition maps' tag data is linked to (0x40440000), with
room to spare; at 0x42000000 the image reached 0x43a00000, where a Switch's
loader module sits (the reference's fe4e8cb5). The host holds 48 MB from
here at start-up (port/switch/host/host_memory.c). */
#define HALO_GUEST_IMAGE_BASE 0x3c000000u
#define HALO_GUEST_IMAGE_RESERVE 0x01000000u
#else
/* the guest image is linked to run here, just above the Xbox window: ART
keeps its heaps low in the address space and fills it upwards */
#define HALO_GUEST_IMAGE_BASE 0x88000000u
/* the room the host reserves for the image there (port/android/host/host_memory.c) */
#define HALO_GUEST_IMAGE_RESERVE 0x04000000u
#endif

/* the Xbox contiguous memory window (port/linux/src/platform.h). The game
data is linked to the addresses of the window. The Android host keeps it
there; the Switch's host cannot (the console will not map memory there), so
it looks for room below 4 GB, hands the guest the address it found
(halo_guest_boot.contiguous_base) and the guest, built with
HALO_WINDOW_REBASE, moves the data with it (source/cseries/xbox_address.h).

The window is as large as the host can find room for: HALO_GUEST_WINDOW_MAXIMUM,
else half that, down to HALO_GUEST_WINDOW_SIZE, the 128 MB of a development
kit, which the game data is linked into and which is the least the game runs
in. The guest is told which (halo_guest_boot.contiguous_size). */
#define HALO_GUEST_WINDOW_BASE 0x80000000u
#define HALO_GUEST_WINDOW_SIZE 0x08000000u
#define HALO_GUEST_WINDOW_MAXIMUM 0x20000000u
/* the window starts on a whole number of these, or of its own size where
that is larger, so the game's arithmetic on physical addresses (an offset,
or an address masked with ~base) still works wherever it lands */
#define HALO_GUEST_WINDOW_ALIGNMENT 0x10000000u

/* Where the Switch port puts the window when the console refuses the one
 * above. Not an ABI value - the Android port has no reason to care - and
 * deliberately a fixed address rather than the first free gap.
 *
 * The first free gap moves between runs: it is whatever the kernel reports
 * free at the moment, which depends on the launcher's own footprint, on
 * anything ASLR'd below 4 GB, and on how much of the low pool the port has
 * already taken for its stacks and pools. Across runs on this console it
 * produced 0x20000000, 0x30000000, 0x32a00000, 0x60000000 and 0x62a00000
 * for the same binary.
 *
 * That matters more than tidiness. The port's behaviour is layout-sensitive
 * - the same build reached 20 seconds and drew a frame in one run and died
 * at 4 seconds in the next - and a window that moves is one more thing that
 * moves. Fixing it takes one variable out of a failure that cannot
 * currently be reproduced on demand, and makes a good run and a bad run
 * directly comparable.
 *
 * Still checked before use, and still falls back to the gap search if the
 * kernel will not map it: a fixed address that cannot be honoured is worse
 * than a moving one that can. */
#define HALO_SWITCH_WINDOW_FALLBACK 0x20000000u

/* Custom Edition maps' tag cache (port/linux/src/platform.h): Halo PC's maps
are linked to it. The host reserves it with the fixed ranges if it can; if
ART holds it, those maps do not load and the rest of the game plays as
usual (port/android/host/host_memory.c) */
#define HALO_GUEST_CE_TAG_CACHE_BASE 0x40440000u
#define HALO_GUEST_CE_TAG_CACHE_SIZE 0x01700000u
/* and the room right above it for the port's converted models and level
(platform.h's PLATFORM_CE_CONVERSION_ROOM_SIZE), reserved with it if it is
free too */
#define HALO_GUEST_CE_CONVERSION_ROOM_SIZE 0x004c0000u

#define HALO_GUEST_MAGIC 0x4f4c4148u /* 'HALO' */
#define HALO_GUEST_ABI_VERSION 2

/* at HALO_GUEST_IMAGE_BASE */
struct halo_guest_header
{
	uint32_t magic;
	uint32_t abi_version;
	uint32_t image_end;          /* end of .bss */
	uint32_t import_table;       /* uint64_t[import_count], filled by the host */
	uint32_t import_names;       /* import_count NUL-terminated names */
	uint32_t import_count;       /* address of a uint32_t holding the count */
	uint32_t start;              /* void __guest_start(struct halo_guest_boot *) */
	uint32_t thread_start;       /* void __guest_thread_start(uint32_t thread) */
	uint32_t thread_attach;      /* uint32_t __guest_thread_attach(void) */
	uint32_t init_array_start;   /* void (*)(void) entries, 4 bytes each */
	uint32_t init_array_end;
};

/* the host's description of the process, handed to __guest_start */
struct halo_guest_boot
{
	uint32_t argc;
	uint32_t argv;               /* char ** in guest memory */
	uint32_t environment;        /* char ** in guest memory, NULL-terminated */
	uint32_t page_size;
	uint32_t contiguous_base;    /* where the host put the Xbox window */
	uint32_t image_shift;        /* where the host put the image, less HALO_GUEST_IMAGE_BASE (always 0 here) */
	uint32_t contiguous_size;    /* how large a window it found room for */
};

#endif
