/*
SERVER_TERMINAL.C

The console's line editing (server_terminal.h) on a POSIX terminal: what
the operator types stays on a prompt line ("> ") at the bottom, whatever
the server prints meanwhile.

Line editing starts only when standard input and standard output are both a
terminal and the program is in its foreground; otherwise nothing here runs
and nothing the program writes changes. Once on:
- The terminal takes keys one at a time without echoing them (ICANON and
  ECHO off; ISIG stays on, so Ctrl+C is SIGINT and Ctrl+Z SIGTSTP as
  before). Its settings are put back at exit, when the input ends, on
  Ctrl+Z (and taken again on fg), and on a crashing signal.
- Standard output, and standard error when it is the terminal too, become
  a pipe; a thread of this file's reads it and writes to the terminal:
  each time, the prompt line cleared, the server's whole lines written, the
  prompt and what was typed so far drawn again. A line still without its
  end after a moment is written as it is, ended. Everything written to the
  terminal is under one mutex, so the prompt and the server's lines never
  mix.
- The keys: printable bytes (UTF-8 too) are added at the line's end, at most
  line_size - 1 bytes (a bell past that); Backspace takes the last
  character out, Ctrl+U the whole line; Enter enters it; Ctrl+D on an empty
  line ends the input; Up and Down go through the lines entered before.
  Other control keys and escape sequences (arrows left and right, function
  keys, Alt+key) are ignored whole.
A line wider than the terminal shows its end ("<" before it).

Built with the host's ABI, as server_control.c.
*/

#include "server_terminal.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

enum
{
	/* a line, at most (server_terminal_start's line_size is less) */
	LINE_SIZE = 1024,
	/* the lines entered kept for Up and Down */
	HISTORY_LINES = 32,
	/* the server's output not yet ended by a newline, at most, and how long
	it waits for its end */
	PARTIAL_SIZE = 4096,
	PARTIAL_MILLISECONDS = 100,
	/* what the end drains from the pipe, at most */
	DRAIN_MAXIMUM = 1024 * 1024,
};

static const char PROMPT[] = "> ";

static const int crash_signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGQUIT };
#define CRASH_SIGNAL_COUNT ((int)(sizeof(crash_signals) / sizeof(crash_signals[0])))

static pthread_mutex_t terminal_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct
{
	/* line editing on (the output through the pipe), and stopped since */
	volatile sig_atomic_t active;
	volatile sig_atomic_t stopped;
	/* the terminal settings in effect are ours; a redraw is wanted (after
	fg) */
	volatile sig_atomic_t raw_applied;
	volatile sig_atomic_t redraw_wanted;
	struct termios original;
	struct termios raw;
	/* the terminal (standard output as it was), and the pipe's reading end */
	int terminal;
	int pipe_read;
	int stderr_redirected;
	/* the line being typed */
	char line[LINE_SIZE];
	size_t length;
	size_t limit;
	int prompt_shown;
	/* in an escape sequence: 1 after ESC, 2 in its CSI or SS3 part; the
	sequence's parameters so far */
	int escape;
	int escape_parameters;
	/* after a CR: the LF of a CR LF is the same Enter */
	int after_return;
	/* the bytes of a character past the line's limit, still to be dropped */
	int dropping;
	/* the lines entered, oldest first in the ring; which one Up and Down
	show (-1: the line being typed, kept in saved) */
	char history[HISTORY_LINES][LINE_SIZE];
	int history_count;
	int history_next;
	int browsing;
	char saved[LINE_SIZE];
	/* the server's output not yet ended, and since when */
	char partial[PARTIAL_SIZE];
	size_t partial_length;
	long long partial_since;
	/* the crashing signals' handlers before ours */
	struct sigaction previous[CRASH_SIGNAL_COUNT];
} terminal = { .terminal = -1, .pipe_read = -1 };

/* ---------- writing to the terminal */

static long long milliseconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/* (async-signal-safe) */
static void write_all(const char *bytes, size_t count)
{
	while (count)
	{
		ssize_t written = write(terminal.terminal, bytes, count);

		if (written < 0 && errno == EINTR)
			continue;
		if (written <= 0)
			return;
		bytes += written;
		count -= (size_t)written;
	}
}

static int columns(void)
{
	struct winsize size;

	if (!ioctl(terminal.terminal, TIOCGWINSZ, &size) && size.ws_col >= 10)
		return size.ws_col;
	return 80;
}

/* the prompt line drawn again, the cursor at its end; under the mutex */
static void redraw(void)
{
	char text[16 + sizeof(PROMPT) + LINE_SIZE];
	size_t used = 0;
	size_t start = 0;
	size_t characters = 0;
	size_t room = (size_t)columns() - (sizeof(PROMPT) - 1) - 1;
	size_t index;

	for (index = 0; index < terminal.length; index++)
		characters += ((unsigned char)terminal.line[index] & 0xc0) != 0x80;
	/* (wider than the terminal: its end, after a "<") */
	if (characters > room)
	{
		size_t skip = characters - (room - 1);

		while (start < terminal.length && skip)
		{
			start++;
			while (start < terminal.length && ((unsigned char)terminal.line[start] & 0xc0) == 0x80)
				start++;
			skip--;
		}
	}
	memcpy(text, "\r\033[K", 4);
	used = 4;
	memcpy(text + used, PROMPT, sizeof(PROMPT) - 1);
	used += sizeof(PROMPT) - 1;
	if (start)
		text[used++] = '<';
	memcpy(text + used, terminal.line + start, terminal.length - start);
	used += terminal.length - start;
	write_all(text, used);
	terminal.prompt_shown = 1;
}

static void clear_prompt(void)
{
	if (terminal.prompt_shown)
		write_all("\r\033[K", 4);
	terminal.prompt_shown = 0;
}

/* the server's output read from the pipe, above the prompt; under the
mutex */
static void output(const char *bytes, size_t count)
{
	const char *end = NULL;
	size_t index;

	for (index = count; index > 0; index--)
	{
		if (bytes[index - 1] == '\n')
		{
			end = bytes + index;
			break;
		}
	}
	if (end)
	{
		clear_prompt();
		write_all(terminal.partial, terminal.partial_length);
		write_all(bytes, (size_t)(end - bytes));
		terminal.partial_length = 0;
		bytes = end;
		count -= index;
	}
	while (count)
	{
		size_t take = sizeof(terminal.partial) - terminal.partial_length;

		if (!take)
		{
			clear_prompt();
			write_all(terminal.partial, terminal.partial_length);
			write_all("\n", 1);
			terminal.partial_length = 0;
			continue;
		}
		if (take > count)
			take = count;
		if (!terminal.partial_length)
			terminal.partial_since = milliseconds();
		memcpy(terminal.partial + terminal.partial_length, bytes, take);
		terminal.partial_length += take;
		bytes += take;
		count -= take;
	}
	if (!terminal.prompt_shown)
		redraw();
}

/* a line not ended for a while, written as it is; under the mutex */
static void flush_partial(int now)
{
	if (!terminal.partial_length ||
		(!now && milliseconds() - terminal.partial_since < PARTIAL_MILLISECONDS))
	{
		return;
	}
	clear_prompt();
	write_all(terminal.partial, terminal.partial_length);
	write_all("\n", 1);
	terminal.partial_length = 0;
}

/* what is in the pipe, to the terminal; under the mutex. Whether its
writing ends are all closed */
static int drain(size_t maximum)
{
	char buffer[4096];
	size_t total = 0;

	while (total < maximum)
	{
		ssize_t count = read(terminal.pipe_read, buffer, sizeof(buffer));

		if (count < 0 && errno == EINTR)
			continue;
		if (count == 0)
			return 1;
		if (count < 0)
			break;
		output(buffer, (size_t)count);
		total += (size_t)count;
	}
	return 0;
}

static void *output_thread(void *argument)
{
	(void)argument;
	for (;;)
	{
		struct pollfd descriptor = { .fd = terminal.pipe_read, .events = POLLIN };
		int closed;

		poll(&descriptor, 1, PARTIAL_MILLISECONDS / 2);
		pthread_mutex_lock(&terminal_mutex);
		if (terminal.stopped)
		{
			pthread_mutex_unlock(&terminal_mutex);
			break;
		}
		if (terminal.redraw_wanted)
		{
			terminal.redraw_wanted = 0;
			redraw();
		}
		closed = drain(64 * 1024);
		flush_partial(0);
		if (!terminal.prompt_shown)
			redraw();
		pthread_mutex_unlock(&terminal_mutex);
		if (closed)
			break;
	}
	return NULL;
}

/* ---------- the terminal's settings */

/* the terminal as it was, the output straight to it (async-signal-safe:
the crash's and Ctrl+Z's too) */
static void restore_terminal(void)
{
	if (terminal.raw_applied)
	{
		tcsetattr(STDIN_FILENO, TCSADRAIN, &terminal.original);
		terminal.raw_applied = 0;
	}
}

static void crash_signal(int signal_number, siginfo_t *information, void *context)
{
	int index;

	if (terminal.active && !terminal.stopped)
	{
		char buffer[4096];
		ssize_t count;
		int rounds;

		/* (no lock: the crash may hold it) */
		terminal.stopped = 1;
		write_all("\r\033[K", 4);
		write_all(terminal.partial, terminal.partial_length < sizeof(terminal.partial) ? terminal.partial_length :
			sizeof(terminal.partial));
		for (rounds = 0; rounds < 64 && (count = read(terminal.pipe_read, buffer, sizeof(buffer))) > 0; rounds++)
			write_all(buffer, (size_t)count);
		restore_terminal();
		dup2(terminal.terminal, STDOUT_FILENO);
		if (terminal.stderr_redirected)
			dup2(terminal.terminal, STDERR_FILENO);
	}
	/* then whatever handled the signal before (the default: the signal
	again, which ends the program as it would have) */
	for (index = 0; index < CRASH_SIGNAL_COUNT; index++)
	{
		struct sigaction *previous = &terminal.previous[index];

		if (crash_signals[index] != signal_number)
			continue;
		sigaction(signal_number, previous, NULL);
		if (previous->sa_flags & SA_SIGINFO)
		{
			if (previous->sa_sigaction)
				previous->sa_sigaction(signal_number, information, context);
		}
		else if (previous->sa_handler != SIG_DFL && previous->sa_handler != SIG_IGN)
			previous->sa_handler(signal_number);
		else
			raise(signal_number);
		return;
	}
}

/* before anything else can handle the crashing signals (the renderer's
write tracking, memory_watch.c, hands a real crash on to the handler before
its own), and only for a program started in a terminal */
__attribute__((constructor))
static void install_crash_handlers(void)
{
	struct sigaction action;
	int index;

	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
		return;
	memset(&action, 0, sizeof(action));
	action.sa_sigaction = crash_signal;
	action.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
	sigemptyset(&action.sa_mask);
	for (index = 0; index < CRASH_SIGNAL_COUNT; index++)
		sigaction(crash_signals[index], &action, &terminal.previous[index]);
}

static void continue_signal(int signal_number)
{
	int saved = errno;

	(void)signal_number;
	/* (only in the foreground: a background job that set the terminal would
	be stopped for it) */
	if (terminal.active && !terminal.stopped && !terminal.raw_applied && tcgetpgrp(STDIN_FILENO) == getpgrp() &&
		!tcsetattr(STDIN_FILENO, TCSADRAIN, &terminal.raw))
	{
		terminal.raw_applied = 1;
		terminal.redraw_wanted = 1;
	}
	errno = saved;
}

static void install_suspend(void);

static void suspend_signal(int signal_number)
{
	int saved = errno;
	sigset_t set;

	(void)signal_number;
	if (terminal.active && !terminal.stopped)
	{
		write_all("\r\033[K", 4);
		restore_terminal();
	}
	signal(SIGTSTP, SIG_DFL);
	sigemptyset(&set);
	sigaddset(&set, SIGTSTP);
	pthread_sigmask(SIG_UNBLOCK, &set, NULL);
	raise(SIGTSTP);
	/* (continued: SIGCONT's handler took the terminal again) */
	install_suspend();
	errno = saved;
}

static void install_suspend(void)
{
	struct sigaction action;

	memset(&action, 0, sizeof(action));
	action.sa_handler = suspend_signal;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &action, NULL);
}

/* ---------- public code */

int server_terminal_start(size_t line_size)
{
	static int exit_registered;
	struct sigaction action;
	pthread_attr_t attributes;
	pthread_t thread;
	pid_t group;
	int fds[2];
	int created;

	if (terminal.active || terminal.stopped || !isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
		return 0;
	group = tcgetpgrp(STDIN_FILENO);
	if (group != -1 && group != getpgrp())
		return 0;
	if (tcgetattr(STDIN_FILENO, &terminal.original))
		return 0;
	terminal.raw = terminal.original;
	terminal.raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | IEXTEN);
	terminal.raw.c_cc[VMIN] = 1;
	terminal.raw.c_cc[VTIME] = 0;
	terminal.limit = line_size && line_size < sizeof(terminal.line) ? line_size : sizeof(terminal.line);
	terminal.browsing = -1;
	if (pipe(fds))
		return 0;
	terminal.terminal = dup(STDOUT_FILENO);
	if (terminal.terminal < 0)
	{
		close(fds[0]);
		close(fds[1]);
		return 0;
	}
	fcntl(terminal.terminal, F_SETFD, FD_CLOEXEC);
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
	terminal.pipe_read = fds[0];
	fflush(stdout);
	fflush(stderr);
	if (tcsetattr(STDIN_FILENO, TCSADRAIN, &terminal.raw))
	{
		close(fds[0]);
		close(fds[1]);
		close(terminal.terminal);
		terminal.terminal = terminal.pipe_read = -1;
		return 0;
	}
	terminal.raw_applied = 1;
	dup2(fds[1], STDOUT_FILENO);
	terminal.stderr_redirected = isatty(STDERR_FILENO);
	if (terminal.stderr_redirected)
		dup2(fds[1], STDERR_FILENO);
	close(fds[1]);
	/* (a pipe is fully buffered by default: lines as on a terminal) */
	setvbuf(stdout, NULL, _IOLBF, 0);
	terminal.active = 1;
	memset(&action, 0, sizeof(action));
	action.sa_handler = continue_signal;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_RESTART;
	sigaction(SIGCONT, &action, NULL);
	install_suspend();
	if (!exit_registered)
	{
		exit_registered = 1;
		atexit(server_terminal_stop);
	}
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	created = !pthread_create(&thread, &attributes, output_thread, NULL);
	pthread_attr_destroy(&attributes);
	if (!created)
	{
		server_terminal_stop();
		return 0;
	}
	pthread_mutex_lock(&terminal_mutex);
	redraw();
	pthread_mutex_unlock(&terminal_mutex);
	return 1;
}

void server_terminal_stop(void)
{
	if (!terminal.active)
		return;
	/* (before the lock: a full pipe waits for the thread, which waits for
	the lock) */
	fflush(stdout);
	fflush(stderr);
	pthread_mutex_lock(&terminal_mutex);
	if (!terminal.stopped)
	{
		drain(DRAIN_MAXIMUM);
		flush_partial(1);
		clear_prompt();
		restore_terminal();
		dup2(terminal.terminal, STDOUT_FILENO);
		if (terminal.stderr_redirected)
			dup2(terminal.terminal, STDERR_FILENO);
		signal(SIGTSTP, SIG_DFL);
		signal(SIGCONT, SIG_DFL);
		terminal.stopped = 1;
	}
	terminal.active = 0;
	pthread_mutex_unlock(&terminal_mutex);
}

/* ---------- editing */

static void history_show(int entry)
{
	const char *text = entry < 0 ? terminal.saved :
		terminal.history[(terminal.history_next - 1 - entry + HISTORY_LINES) % HISTORY_LINES];

	terminal.browsing = entry;
	terminal.length = strlen(text);
	memcpy(terminal.line, text, terminal.length);
}

static void history_move(int older)
{
	if (older && terminal.browsing + 1 < terminal.history_count)
	{
		if (terminal.browsing < 0)
		{
			memcpy(terminal.saved, terminal.line, terminal.length);
			terminal.saved[terminal.length] = 0;
		}
		history_show(terminal.browsing + 1);
	}
	else if (!older && terminal.browsing >= 0)
		history_show(terminal.browsing - 1);
}

static void history_add(const char *text)
{
	const char *newest = terminal.history[(terminal.history_next - 1 + HISTORY_LINES) % HISTORY_LINES];

	if (!text[0] || (terminal.history_count && !strcmp(newest, text)))
		return;
	snprintf(terminal.history[terminal.history_next], sizeof(terminal.history[0]), "%s", text);
	terminal.history_next = (terminal.history_next + 1) % HISTORY_LINES;
	if (terminal.history_count < HISTORY_LINES)
		terminal.history_count++;
}

/* a byte typed, not part of an escape sequence: whether the line was
entered */
static int key(unsigned char character)
{
	if (character == '\n' && terminal.after_return)
	{
		terminal.after_return = 0;
		return 0;
	}
	terminal.after_return = character == '\r';
	if (character == '\r' || character == '\n')
		return 1;
	if (character == 0x7f || character == '\b')
	{
		terminal.dropping = 0;
		while (terminal.length && ((unsigned char)terminal.line[terminal.length - 1] & 0xc0) == 0x80)
			terminal.length--;
		if (terminal.length)
			terminal.length--;
	}
	else if (character == 0x15)
	{
		terminal.length = 0;
		terminal.dropping = 0;
	}
	else if (character == 0x03)
		kill(getpid(), SIGINT);
	else if (character >= 0x20)
	{
		/* (a whole character or none: the bytes it needs counted from its
		first) */
		int needed = character >= 0xf0 ? 4 : character >= 0xe0 ? 3 : character >= 0xc0 ? 2 : 1;

		if ((character & 0xc0) == 0x80 && terminal.dropping)
		{
			terminal.dropping--;
			return 0;
		}
		terminal.dropping = 0;
		if ((character & 0xc0) != 0x80 && terminal.length + (size_t)needed >= terminal.limit)
		{
			terminal.dropping = needed - 1;
			write_all("\a", 1);
			return 0;
		}
		if (terminal.length + 1 < terminal.limit)
			terminal.line[terminal.length++] = (char)character;
	}
	return 0;
}

int server_terminal_input(const char *bytes, size_t count, void (*line)(const char *text))
{
	size_t index;

	pthread_mutex_lock(&terminal_mutex);
	if (!terminal.active || terminal.stopped)
	{
		pthread_mutex_unlock(&terminal_mutex);
		return 0;
	}
	for (index = 0; index < count; index++)
	{
		unsigned char character = (unsigned char)bytes[index];

		if (terminal.escape == 1)
		{
			/* (ESC [ or ESC O begins a sequence; ESC and any other key is
			Alt+key, dropped) */
			terminal.escape = character == '[' || character == 'O' ? 2 : 0;
			terminal.escape_parameters = 0;
			continue;
		}
		if (terminal.escape == 2)
		{
			if (character >= 0x40 && character <= 0x7e)
			{
				if (!terminal.escape_parameters && (character == 'A' || character == 'B'))
					history_move(character == 'A');
				terminal.escape = 0;
			}
			else if (character >= 0x20 && character <= 0x3f)
				terminal.escape_parameters++;
			else
				terminal.escape = 0;
			continue;
		}
		if (character == 0x1b)
		{
			terminal.escape = 1;
			terminal.after_return = 0;
			continue;
		}
		if (character == 0x04 && !terminal.length)
		{
			pthread_mutex_unlock(&terminal_mutex);
			return 0;
		}
		if (key(character))
		{
			char entered[LINE_SIZE];

			memcpy(entered, terminal.line, terminal.length);
			entered[terminal.length] = 0;
			history_add(entered);
			/* (the line stays on the screen, as typed; a new prompt below) */
			redraw();
			write_all("\n", 1);
			terminal.length = 0;
			terminal.dropping = 0;
			terminal.browsing = -1;
			redraw();
			pthread_mutex_unlock(&terminal_mutex);
			line(entered);
			pthread_mutex_lock(&terminal_mutex);
			if (!terminal.active || terminal.stopped)
			{
				pthread_mutex_unlock(&terminal_mutex);
				return 1;
			}
		}
	}
	redraw();
	pthread_mutex_unlock(&terminal_mutex);
	return 1;
}
