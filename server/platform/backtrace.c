/*
BACKTRACE.C

glibc's backtrace and backtrace_symbols_fd (<execinfo.h>), for a C library
without them (musl: the static servers, tools/server_build.py), so the crash
reports (memory_watch.c, stack_walk_windows.c) still list the calls: the
return addresses along the chain of frame pointers, which every unit keeps
(-fno-omit-frame-pointer). Built with the host's ABI. Where the C library
has them, this file is empty.
*/

#if !__has_include(<execinfo.h>)

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* (a frame is two words: the caller's frame pointer, then the return
address, on x86, x86-64 and AArch64 alike) */
int backtrace(void **frames, int count)
{
	void **frame = __builtin_frame_address(0);
	int found = 0;

	while (frame && found < count)
	{
		void **caller = (void **)frame[0];
		void *return_address = frame[1];

		if (!return_address)
			break;
		frames[found++] = return_address;
		/* (a chain that goes down the stack, or jumps more than a few MB, is
		broken: stop there) */
		if ((uintptr_t)caller <= (uintptr_t)frame || (uintptr_t)caller - (uintptr_t)frame > 0x800000)
			break;
		frame = caller;
	}
	return found;
}

void backtrace_symbols_fd(void *const *frames, int count, int descriptor)
{
	int index;

	for (index = 0; index < count; index++)
	{
		char line[32];
		int length = snprintf(line, sizeof(line), "[%p]\n", frames[index]);

		if (length > 0 && write(descriptor, line, (size_t)length) < 0)
			return;
	}
}

#endif
