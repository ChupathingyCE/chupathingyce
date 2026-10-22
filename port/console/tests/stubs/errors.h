/* host-side stand-in for errors.h (tag_byte_swap_test.c) */
enum { _error_silent = 0 };
void error(int type, const char *format, ...);
