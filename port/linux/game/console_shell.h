/*
CONSOLE_SHELL.H

The drop-down console's typing, as a shell's (source/main/console.c): a line
typed is made the script expression the console runs, without changing
what the scripts' compiler takes. Words are the values after a command,
'single' or "double" quoted text is one value, a parenthesized expression
is passed through as it is (a call inside a call), a ")" outside one is
an error, and a line that starts with "(" is the console's own form, passed
through whole. A command whose last value is text takes the rest of the
line as it was typed as that text (apostrophes and parentheses too, a
double quote dropped: the scripts' text cannot hold one), so
sv_say I'm back :( says "I'm back :(", and sv_say "hello there" stays as
it is. The compiler then reads a line as
retail's console did: a bare command wrapped in parentheses, a global and a
value set. No game dependency: port/linux/tests/console_shell_test.c builds
it on its own.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CONSOLE_SHELL_H
#define CONSOLE_SHELL_H

#include <stddef.h>

/* how many values a command takes when its last one is text, else -1 (the
caller's: the scripts' function table) */
typedef int (*console_shell_text_tail_proc)(char const *command, void *context);

enum
{
	_console_shell_ok = 0,
	/* a quote opened and not closed */
	_console_shell_unterminated_quote,
	/* a parenthesis opened and not closed */
	_console_shell_unmatched_parenthesis,
	/* the expression would not fit */
	_console_shell_too_long,
	/* a ";": the scripts' compiler runs no line that has one */
	_console_shell_semicolon,
};

/* line as the script expression to run, into expression (size bytes):
_console_shell_ok, else what was wrong (expression then empty) */
int console_shell_translate(char const *line, char *expression, size_t size,
	console_shell_text_tail_proc text_tail, void *context);

#endif
