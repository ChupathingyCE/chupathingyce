/*
ANDROID64_VULKAN.C

What the Vulkan backend (port/android/host/host_vk*.c) needs of the host in
the native 64-bit app, where the guest build's host library (host_main.c)
gave it: the log, and whether the game draws with Vulkan. The backend itself
is thelinkin3000's, from the guest build (port/android/VULKAN.md).
*/

#include <android/log.h>
#include <stdarg.h>
#include <stdio.h>

#include "host.h"

int host_renderer_vulkan;

void host_logf(int priority, const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	__android_log_vprint(priority, HOST_LOG_TAG, format, arguments);
	va_end(arguments);
}
