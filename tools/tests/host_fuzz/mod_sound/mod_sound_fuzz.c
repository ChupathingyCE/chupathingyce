/*
MOD_SOUND_FUZZ.C

The fuzzer's entry: each input is the one tag file of the one mod that the
stubs (mod_sound_stubs.c, appended to mods.c) give the mods: a map of one
sound tag loads, the file is read over it, and the map unloads.
*/

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* the game's C library and math wrappers (cseries.h, halo_math.h, the
port's stdio.h), as the C library's */
/* (by hand, not the C library's: the mods' memory is a window the
sanitizer's interceptors know nothing of; its own instrumentation still
checks every byte the game's code touches) */
void *csmemcpy(void *destination, const void *source, unsigned int size)
{
	unsigned char *to = (unsigned char *)destination;
	const unsigned char *from = (const unsigned char *)source;

	while (size--)
		*to++ = *from++;
	return destination;
}
void *csmemset(void *buffer, int c, unsigned int size)
{
	unsigned char *to = (unsigned char *)buffer;

	while (size--)
		*to++ = (unsigned char)c;
	return buffer;
}
int csmemcmp(const void *a, const void *b, unsigned int size)
{
	const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;

	while (size--)
	{
		if (*x != *y)
			return *x - *y;
		x++;
		y++;
	}
	return 0;
}
char *csstrcpy(char *destination, const char *source) { return strcpy(destination, source); }
char *csstrcat(char *destination, const char *source) { return strcat(destination, source); }
unsigned int csstrlen(const char *text) { return (unsigned int)strlen(text); }
double halo_sin(double x) { return sin(x); }
double halo_cos(double x) { return cos(x); }
double halo_exp(double x) { return exp(x); }
double halo_log(double x) { return log(x); }
double halo_pow(double x, double y) { return pow(x, y); }
int halo_linux_fprintf(FILE *stream, const char *format, ...)
{
	va_list arguments;
	int written;

	va_start(arguments, format);
	written = vfprintf(stream, format, arguments);
	va_end(arguments);
	return written;
}

void mod_sound_fuzz_input(const uint8_t *data, size_t size);
void mods_tags_loaded(const char *map_name);
void mods_tags_unloaded(void);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	mod_sound_fuzz_input(data, size);
	mods_tags_loaded("a10");
	mods_tags_unloaded();
	return 0;
}
