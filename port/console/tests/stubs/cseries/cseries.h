/* host-side stand-in for the game's cseries/cseries.h (bitmap_byte_order_test.c
builds source/bitmaps/s3tc/s3tc.c with it) */
#include <stddef.h>
#include <string.h>
typedef unsigned char byte;
typedef unsigned short word;
typedef float real;
#define UNSIGNED_SHORT_MAX 0xFFFF
#define match_assert(file, line, condition) ((void)(condition))
#include <math.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define UNSIGNED_LONG_MAX 0xFFFFFFFFu
