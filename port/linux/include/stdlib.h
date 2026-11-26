/*
STDLIB.H

Host <stdlib.h> plus MSVC-named functions and constants.
*/

#ifndef __HALO_LINUX_STDLIB_H
#define __HALO_LINUX_STDLIB_H

#include_next <stdlib.h>

#define _MAX_PATH 260
#define _MAX_DRIVE 3
#define _MAX_DIR 256
#define _MAX_FNAME 256
#define _MAX_EXT 256

char *_itoa(int value, char *string, int radix);
char *_ltoa(long value, char *string, int radix);
char *_ultoa(unsigned long value, char *string, int radix);

#define itoa _itoa
#define ltoa _ltoa
#define ultoa _ultoa

#endif
