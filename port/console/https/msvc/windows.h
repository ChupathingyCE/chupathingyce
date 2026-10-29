/*
WINDOWS.H (the Xbox consoles')

What Mbed TLS takes from windows.h on a compiler that says _WIN32
(library/platform_util.c: SecureZeroMemory, for mbedtls_platform_zeroize),
for the Xbox SDKs, whose headers are xtl.h's instead: on the include path
of port/console/https's units alone (tools/console_build.py,
console_https_flags with windows_crt). Nothing else of Windows is used: the
consoles' configuration has no files, clock, sockets or threads.
*/

#ifndef __CONSOLE_HTTPS_WINDOWS_H
#define __CONSOLE_HTTPS_WINDOWS_H

#include <stddef.h>

/* (writes the compiler cannot drop: each through a volatile pointer) */
static void console_https_secure_zero(void *buffer, size_t size)
{
	volatile unsigned char *bytes = (volatile unsigned char *)buffer;

	while (size--)
		*bytes++ = 0;
}

#define SecureZeroMemory console_https_secure_zero

#endif
