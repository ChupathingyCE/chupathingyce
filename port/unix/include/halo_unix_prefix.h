/*
HALO_UNIX_PREFIX.H

Force-included ahead of halo_linux_prefix.h in the game's units and the
platform units of the native 64-bit builds on the Unix hosts other than Linux
and macOS (tools/unix64_build.py; FreeBSD so far). It states, before any
header is read, what those hosts' C libraries do differently from the one
the port's sources were written against. Nothing here changes a game source or
a file of port/linux.
*/

#ifndef __HALO_UNIX_PREFIX_H
#define __HALO_UNIX_PREFIX_H

#ifdef __FreeBSD__

/* ---------- wide characters

FreeBSD types wchar_t and wint_t as plain ints (sys/_types.h: ___wchar_t),
whatever -fshort-wchar says. The game's are 16 bits wide (its Xbox structures
hold wchar_t arrays): typed here, ahead of every header, so that stddef.h's
and wchar.h's own (their _DECLARED guards) are skipped. */

typedef __WCHAR_TYPE__ wchar_t;
#define _WCHAR_T_DECLARED
typedef unsigned short wint_t;
#define _WINT_T_DECLARED

/* ---------- the MSVC names libtiff uses

FreeBSD's libc calls its own _open, _close, _read and _write through their
exported names, and the executable's definitions (port/linux/src/msvc_crt.c)
preempt them: fopen reached the game's _open, which loaded the configuration
with fopen, until the stack ended. It also has _fstat, _lseek and _stat. Spelled
apart, in every unit and header alike. */

#define _open halo_msvc_open
#define _close halo_msvc_close
#define _read halo_msvc_read
#define _write halo_msvc_write
#define _lseek halo_msvc_lseek
#define _fstat halo_msvc_fstat
#define _stat halo_msvc_stat

/* ---------- the x87 environment

msvc_crt.c reads glibc's __control_word and __status_word (and __mxcsr, which
FreeBSD names the same); FreeBSD's amd64 fenv_t nests them in __x87. */

#ifdef __x86_64__
#define __control_word __x87.__control
#define __status_word __x87.__status
#endif

/* ---------- the host's u_long

The XDK's Winsock declares a 32-bit u_long (halo_linux_prefix.h renames the
host's before it reads <sys/types.h>), and FreeBSD's <unistd.h> uses u_long
(fflagstostr, strtofflags): read it here, with the host's name, so that the
prefix's own read finds it done. */

#define u_long halo_host_u_long
#include <sys/types.h>
#include <unistd.h>
#undef u_long

/* ---------- ctype

glibc's headers are told (__NO_CTYPE) to leave the ctype functions as
functions, which the game declares itself (hs.c's isspace). FreeBSD's make them
macros; its libc has the functions. */

#include <ctype.h>
#undef isalnum
#undef isalpha
#undef isblank
#undef iscntrl
#undef isdigit
#undef isgraph
#undef islower
#undef isprint
#undef ispunct
#undef isspace
#undef isupper
#undef isxdigit
#undef tolower
#undef toupper

#endif /* __FreeBSD__ */

#endif
