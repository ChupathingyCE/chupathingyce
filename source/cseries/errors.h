/*
ERRORS.H
*/

#ifndef __ERRORS_H
#define __ERRORS_H
#pragma once

/* ---------- constants */

enum
{
	_error_immediate = 0,
	_error_delayed,
	_error_silent,
	_error_log,
	NUMBER_OF_ERROR_MESSAGE_PRIORITIES,
};

enum
{
	ERROR_MESSAGE_BUFFER_MAXIMUM_SIZE = 2048,
};

/* ---------- structures */

struct error_global_data
{
	boolean delayed;
	boolean output_to_debug_file;
	boolean display_state;
	boolean recursion_lock;
	boolean overflow_suppression;
	boolean suppress_all;
	short message_buffer_size;
	char message_buffer[ERROR_MESSAGE_BUFFER_MAXIMUM_SIZE];
};

/* ---------- prototypes/ERRORS.C */

void errors_initialize(
	void);
void errors_dispose(
	void);
void errors_output_to_debug_file(
	boolean output_to_debug_file);
void errors_overflow_suppression_enable(
	boolean overflow_suppression);
char *error_get(
	void);
boolean errors_handle(
	void);
void errors_clear(
	void);
void error(
	short priority,
	const char *format,
	...);

void write_to_error_file(
	char *string,
	boolean date);

/* port: where debug.txt is now: "d:\\debug.txt", or an Xbox's T: when its D:
can't be written (errors.c) */
char const *error_log_path(
	void);

/* port: what stopped the game, for the halt screen (main.c): the fatal
assert or halt's reason, file and line (display_assert), or an immediate
error's message with no file (error). Kept in fixed buffers: nothing is
allocated. error_halt_place_clear forgets it (a release build's assert that
carries on: system_exit) */
void error_halt_place_set(
	char const *reason,
	char const *file,
	long line);
void error_halt_place_clear(
	void);
/* the reason ("" for none); file ("" for none) and line through the
pointers */
char const *error_halt_place(
	char const **file,
	long *line);

#ifdef HALO_CONSOLE_LOG_BUFFER
/* port: the console's debug.txt lines wait in memory (errors.c): written out
at once, and by the main loop once a second */
void error_log_flush(
	void);
void error_log_update(
	void);
#endif

/* ---------- globals */

extern struct error_global_data error_globals;

/* ---------- public code */



#endif // __ERRORS_H
