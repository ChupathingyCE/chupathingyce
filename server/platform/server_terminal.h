/*
SERVER_TERMINAL.H

The console's line editing (server_terminal.c), for server_control.c: when
the server's standard input and output are a terminal, what is typed is
edited on a prompt line of its own, and every line the server prints (on
standard output or standard error) goes above it, the prompt and what was
typed so far drawn again after it. Anywhere else (a pipe, a file, a service
manager, a container without a terminal) none of this starts, and the
console reads its input as it always has.

server_terminal.c is POSIX's (termios, and the program's output through a
pipe of its own to the terminal). A Windows console would give the same
three functions; until then, a Windows server reads its console as it
always has (these stand in, and server_terminal.c is not built there).
*/

#ifndef SERVER_TERMINAL_H
#define SERVER_TERMINAL_H

#include <stddef.h>

#ifdef _WIN32
static inline int server_terminal_start(size_t line_size) { (void)line_size; return 0; }
static inline int server_terminal_input(const char *bytes, size_t count, void (*line)(const char *text))
{
	(void)bytes;
	(void)count;
	(void)line;
	return 1;
}
static inline void server_terminal_stop(void) {}
#else

/* line editing started, its lines at most line_size - 1 bytes: whether it
did (stdin and stdout a terminal, the program in its foreground) */
int server_terminal_start(size_t line_size);
/* bytes read from standard input, edited; each line entered is handed to
line (on this thread, with no lock held). 0: the input is at its end
(Ctrl+D on an empty line), else 1 */
int server_terminal_input(const char *bytes, size_t count, void (*line)(const char *text));
/* line editing stopped: the terminal as it was, the program's output
straight to it again (also at exit, and on a crash) */
void server_terminal_stop(void);
#endif

#endif
