/*
UNIX_HOST.C

The functions of halo_unix_host.h, with the host's ABI as the other native
units (tools/unix64_build.py: host_sources).
*/

#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __FreeBSD__
#include <sys/sysctl.h>
#endif

#include "../include/halo_unix_host.h"

#undef readlink

ssize_t halo_unix_readlink(const char *path, char *buffer, size_t size)
{
#ifdef __FreeBSD__
	if (!strcmp(path, "/proc/self/exe"))
	{
		/* (readlink's answer: the length, without a terminator; sysctl's
		counts one, and refuses a path that does not fit) */
		int name[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
		size_t length = size;

		if (size == 0 || sysctl(name, 4, buffer, &length, NULL, 0) != 0 || length == 0)
			return -1;
		return (ssize_t)strnlen(buffer, length);
	}
#endif
	return readlink(path, buffer, size);
}
