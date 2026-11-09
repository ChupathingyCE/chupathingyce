/*
UNIX_HOST.C

The functions of halo_unix_host.h, with the host's ABI as the other native
units (tools/unix64_build.py: host_sources).
*/

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __FreeBSD__
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#endif

#include "../include/halo_unix_host.h"

#undef readlink

int halo_unix_command_line_argument(int index, char *buffer, unsigned long size)
{
#ifdef __FreeBSD__
	/* (KERN_PROC_ARGS: the arguments NUL-separated, as /proc/self/cmdline has
	them; a command line that does not fit is refused) */
	int name[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ARGS, (int)getpid() };
	char command_line[4096];
	size_t length = sizeof(command_line) - 1;
	size_t offset = 0;

	if (!buffer || !size || index < 0 || sysctl(name, 4, command_line, &length, NULL, 0) != 0 || length == 0)
		return 0;
	command_line[length] = '\0';
	while (index-- > 0)
	{
		offset += strlen(command_line + offset) + 1;
		if (offset >= length)
			return 0;
	}
	snprintf(buffer, size, "%s", command_line + offset);
	return 1;
#else
	(void)index;
	(void)buffer;
	(void)size;
	return 0;
#endif
}

int halo_unix_resident_kb(void)
{
#ifdef __FreeBSD__
	int name[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)getpid() };
	struct kinfo_proc process;
	size_t length = sizeof(process);

	if (sysctl(name, 4, &process, &length, NULL, 0) != 0 || length != sizeof(process))
		return -1;
	return (int)((long)process.ki_rssize * sysconf(_SC_PAGESIZE) / 1024);
#else
	return -1;
#endif
}

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
