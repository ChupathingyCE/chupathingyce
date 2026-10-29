/*
HALO_UNIX_HOST.H

Force-included into the native 64-bit builds' platform units and posix_*.c
units on the Unix hosts other than Linux and macOS (tools/unix64_build.py;
FreeBSD so far), which have the host C library's own names: what port/linux/src
asks of Linux (/proc/self/exe, a mapping hint that is kept, the registers of a
signal's context) and these hosts answer otherwise. Not for the game's units,
whose C library is restricted to ISO C. Nothing here changes a file of
port/linux; port/unix/src/unix_host.c has the functions.
*/

#ifndef __HALO_UNIX_HOST_H
#define __HALO_UNIX_HOST_H

#include <signal.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>

/* ---------- /proc/self/exe

The running executable's path. FreeBSD mounts no procfs by default: the
kernel's own answer stands in (port/unix/src/unix_host.c). Any other path is
readlink's. */

ssize_t halo_unix_readlink(const char *path, char *buffer, size_t size);
#define readlink halo_unix_readlink

/* ---------- socket addresses

FreeBSD's struct sockaddr starts with sa_len, then the family; Winsock's, which
the game's addresses are, with a 16-bit family (posix_net.c converts them). */

#ifdef __FreeBSD__
#define HALO_SOCKADDR_LEN 1
#endif

/* ---------- /proc/self/cmdline

The process's arguments (posix_net.c: posix_command_line_argument), from
the kernel's own answer. 1 and the argument, or 0. */

int halo_unix_command_line_argument(int index, char *buffer, unsigned long size);

/* ---------- /proc/self/statm

The process's resident memory in KB (event_upload.c), or -1. */

int halo_unix_resident_kb(void);

/* ---------- mmap

The Xbox address space is asked for with a hint (port/linux/src/xbox_memory.c:
no MAP_FIXED, which would replace what is mapped), and Linux keeps it when it
is free. FreeBSD places the mapping elsewhere when it pleases: a hint here is a
demand (MAP_FIXED with MAP_EXCL, which keeps what is mapped, and fails with an
error that says why). It has no MAP_NORESERVE (its mappings take no swap until
they are touched) and calls Linux's MAP_FIXED_NOREPLACE MAP_EXCL. */

#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif
#ifndef MAP_EXCL
#define MAP_EXCL 0x00004000
#endif
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE (MAP_FIXED | MAP_EXCL)
#endif

static inline void *halo_unix_mmap(void *address, size_t length, int protection, int flags, int descriptor,
	off_t offset)
{
	if (address && !(flags & MAP_FIXED))
		flags |= MAP_FIXED | MAP_EXCL;
	return mmap(address, length, protection, flags, descriptor, offset);
}
#define mmap halo_unix_mmap

/* ---------- the registers of a signal's context
(memory_watch.c and posix_crash.c: HALO_UNIX_CONTEXT_PC and the other two) */

#if defined(__FreeBSD__) && defined(__x86_64__)
#define HALO_UNIX_CONTEXT_PC(ucontext) ((ucontext)->uc_mcontext.mc_rip)
#define HALO_UNIX_CONTEXT_FRAME(ucontext) ((ucontext)->uc_mcontext.mc_rbp)
#define HALO_UNIX_CONTEXT_STACK(ucontext) ((ucontext)->uc_mcontext.mc_rsp)
#endif

#endif
