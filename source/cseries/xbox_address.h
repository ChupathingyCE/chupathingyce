/*
XBOX_ADDRESS.H

Pointers inside Xbox data, for the 64-bit builds (HALO_64BIT).

The game's data formats embed 32-bit pointers: cache files hold tag data
whose blocks, references and data point at each other by address, game
state is saved and restored as raw memory, and Direct3D resources carry
physical addresses. A 64-bit build keeps all of that memory inside one
4 GB region reserved at start-up, at a fixed host address (anywhere it
fits on Android), so that Xbox address X is host
address XBOX_ADDRESS_SPACE_BASE + X (port/linux/src/xbox_memory.c).
Structures laid out like the Xbox's declare their pointer fields XPTR(type)
(a 32-bit Xbox address) and convert with XBOX_POINTER and XBOX_ADDRESS.

HALO_WINDOW_TRANSLATED is the same layer in a 32-bit build that cannot have the
Xbox's addresses (the Xbox 360): a smaller window, anywhere in its memory.

Everywhere else (the Xbox, the matching MSVC build and the 32-bit ports) a
pointer is an Xbox address: XPTR(type) is `type *` and the conversions do
nothing, so the code compiles as before.
*/

#ifndef __XBOX_ADDRESS_H
#define __XBOX_ADDRESS_H

/* the builds whose Xbox addresses are not their pointers: the 64-bit builds
(HALO_64BIT) and the 32-bit ones whose window is not at 0x80000000
(HALO_WINDOW_REBASE, below). Code that converts between the two, rather than
casting, tests this. */
#if defined(HALO_64BIT) || defined(HALO_WINDOW_REBASE)
#define HALO_ADDRESS_TRANSLATED 1
#endif

#if defined(HALO_64BIT) && defined(HALO_WINDOW_TRANSLATED)

/* A 32-bit build whose host cannot have the Xbox's addresses (the Xbox 360:
0x80000000 up is its executable's): the Xbox addresses the game uses,
XBOX_WINDOW_LOW to XBOX_WINDOW_HIGH (its heap below 0x80000000, the
contiguous memory above), are one block of host memory placed wherever the
host had room. Xbox address X is host address xbox_address_space_base + X,
modulo 2^32 (the port reserves it: platform_xbox_window_reserve). */

#define XBOX_WINDOW_LOW 0x78000000U
#define XBOX_WINDOW_HIGH 0x88000000U
#define XBOX_WINDOW_SIZE (XBOX_WINDOW_HIGH - XBOX_WINDOW_LOW)

/* the host address of Xbox address 0, modulo 2^32 */
extern unsigned int xbox_address_space_base;

#define XPTR(type) unsigned int
#define XBOX_NULL 0

static __inline__ __attribute__((always_inline)) void *xbox_pointer(unsigned int address)
{
	return address ? (void *)(__UINTPTR_TYPE__)(xbox_address_space_base + address) : (void *)0;
}

void xbox_address_out_of_range(void const *pointer) __attribute__((noreturn));

static __inline__ __attribute__((always_inline)) unsigned int xbox_address(void const *pointer)
{
	unsigned int address = (unsigned int)(__UINTPTR_TYPE__)pointer - xbox_address_space_base;

	if (!pointer)
		return 0;
	if (address - XBOX_WINDOW_LOW >= XBOX_WINDOW_SIZE)
		xbox_address_out_of_range(pointer);
	return address;
}

#define POINTER_BITS(pointer) ((__UINTPTR_TYPE__)(pointer))

#elif defined(HALO_64BIT)

/* (a build may place it elsewhere: AddressSanitizer's shadow memory lies
where these are, so the sanitized builds put it above it, at 48 TB:
tools/test_map_fuzz.py) */
#ifndef XBOX_ADDRESS_SPACE_BASE
#if defined(HALO_ANDROID)
/* wherever the system has 4 GB free, 4 GB-aligned, found at start-up
(port/linux/src/xbox_memory.c): an Android app's library is loaded into a
process the system has laid out already, and the Java runtime's heaps lie
where a fixed address would (port/android/README.md) */
extern unsigned long long xbox_address_space_base;
#define XBOX_ADDRESS_SPACE_BASE xbox_address_space_base
#elif defined(__linux__) && defined(__aarch64__)
/* 256 GB: below the top of the smallest address space a 64-bit ARM Linux
kernel gives a program (512 GB, with 39-bit addresses: Raspberry Pi OS's
kernels, Android's) */
#define XBOX_ADDRESS_SPACE_BASE 0x4000000000ULL /* 256 GB */
#else
#define XBOX_ADDRESS_SPACE_BASE 0x10000000000ULL /* 1 TB */
#endif
#endif
#define XBOX_ADDRESS_SPACE_SIZE 0x100000000ULL /* 4 GB */

/* a pointer field of an Xbox-layout structure; type documents the target */
#define XPTR(type) unsigned int
/* an XPTR field's null */
#define XBOX_NULL 0

/* Xbox address to host pointer; 0 stays NULL */
static __inline__ __attribute__((always_inline)) void *xbox_pointer(unsigned int address)
{
	return address ? (void *)(XBOX_ADDRESS_SPACE_BASE + address) : (void *)0;
}

/* reports a pointer outside the Xbox address space and aborts */
void xbox_address_out_of_range(void const *pointer) __attribute__((noreturn));

/* host pointer to Xbox address; NULL stays 0 */
static __inline__ __attribute__((always_inline)) unsigned int xbox_address(void const *pointer)
{
	unsigned long long offset = (unsigned long long)(__UINTPTR_TYPE__)pointer - XBOX_ADDRESS_SPACE_BASE;

	if (!pointer)
		return 0;
	if (offset >= XBOX_ADDRESS_SPACE_SIZE)
		xbox_address_out_of_range(pointer);
	return (unsigned int)offset;
}

/* a pointer as an integer, for alignment tests, ordering and differences
(the Xbox code cast pointers to int or unsigned int for these) */
#define POINTER_BITS(pointer) ((__UINTPTR_TYPE__)(pointer))

#elif defined(HALO_WINDOW_REBASE)

/* A 32-bit build whose contiguous window is not at 0x80000000 (the Switch,
whose kernel will not map memory there; the Linux build, to test it). The
game data is written for the Xbox's window: a map's tag cache sits at
0x803a6000 and every tag in it names its blocks, its data and its name by
addresses in the window, and those are what XPTR fields hold until they are
read. The window is wherever the host put it (platform_contiguous_base,
which the guest is told before any constructor runs, or the Linux build
reads from HALO_WINDOW_BASE: port/linux/src/xbox_memory.c), on a whole
number of 256 MB.

xbox_pointer moves a value only when it is one of the window's addresses as
the data was linked, 0x80000000 to 0x88000000 (the 128 MB of a development
kit, which is all the data is linked into), and leaves anything else as it
is: an address outside the window (the image, the heap, Custom Edition's
tag cache) is the same in both, a null stays null, and an address already
moved is not moved again, so a field the port has already filled with a
pointer of its own reads the same. xbox_address is the inverse for the
first 128 MB of the window where it is. Both are inline: they are on every
tag read, which the game does thousands of times a frame. With the window
at its linked address both are the identity, as in the other 32-bit
builds. */
extern unsigned long platform_contiguous_base;
extern unsigned long platform_contiguous_size;

#define XBOX_WINDOW_LINKED_BASE 0x80000000UL
#define XBOX_WINDOW_LINKED_SIZE 0x08000000UL

#define XPTR(type) type *
#define XBOX_NULL NULL

static __inline__ __attribute__((always_inline)) void *xbox_window_pointer(unsigned long address)
{
	return address - XBOX_WINDOW_LINKED_BASE < XBOX_WINDOW_LINKED_SIZE ?
		(void *)(address - XBOX_WINDOW_LINKED_BASE + platform_contiguous_base) : (void *)address;
}

static __inline__ __attribute__((always_inline)) void *xbox_window_address(void const *pointer)
{
	unsigned long value = (unsigned long)pointer;

	return value - platform_contiguous_base < XBOX_WINDOW_LINKED_SIZE ?
		(void *)(value - platform_contiguous_base + XBOX_WINDOW_LINKED_BASE) : (void *)value;
}

#define xbox_pointer(address) xbox_window_pointer((unsigned long)(address))
#define xbox_address(pointer) xbox_window_address((void const *)(pointer))
#define POINTER_BITS(pointer) ((unsigned long)(pointer))

#else

#define XPTR(type) type *
#define XBOX_NULL NULL
#define xbox_pointer(address) ((void *)(address))
#define xbox_address(pointer) ((void *)(pointer))
#define POINTER_BITS(pointer) ((unsigned long)(pointer))

#endif

#define XBOX_POINTER(type, address) ((type *)xbox_pointer(address))
#define XBOX_ADDRESS(pointer) xbox_address(pointer)

#endif // __XBOX_ADDRESS_H
