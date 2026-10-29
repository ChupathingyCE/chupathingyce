/*
INTTYPES.H (the Xbox consoles')

The little of C99's inttypes.h that Mbed TLS's headers name (the printf
formats of 64-bit and pointer-sized integers), for the Xbox SDKs' C
runtimes, which have none: on the include path of port/console/https's
units alone (tools/console_build.py, console_https_flags with
windows_crt). The formats are never used: the consoles' Mbed TLS prints
nothing.
*/

#ifndef __CONSOLE_HTTPS_INTTYPES_H
#define __CONSOLE_HTTPS_INTTYPES_H

#include <stdint.h>

#ifndef PRId64
#define PRId64 "I64d"
#endif
#ifndef PRIu64
#define PRIu64 "I64u"
#endif
#ifndef PRIuPTR
#define PRIuPTR "u"
#endif

#endif
