/*
MALLOC.H

MSVC <malloc.h>.
*/

#ifndef __HALO_LINUX_MALLOC_H
#define __HALO_LINUX_MALLOC_H

#ifdef __ANDROID__
/* bionic's <stdlib.h> declares malloc and the rest in its own <malloc.h>,
which it includes (the native 64-bit Android build) */
#include_next <malloc.h>
#endif
#include <stdlib.h>

#define _alloca __builtin_alloca
#define alloca __builtin_alloca

size_t _msize(void *pointer);

#endif
