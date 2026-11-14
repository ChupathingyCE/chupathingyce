/*
CONSOLE_SHELL_TEST.C

Tests of the drop-down console's typing (port/linux/game/console_shell.h):
words, quotes, numbers, a call inside a call and the console's own form
made the expression the scripts' compiler takes. Built and run by
tools/test_console_shell.py; exits nonzero on a failure.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "../game/console_shell.h"

#include <stdio.h>
#include <string.h>

static int failures;

/* sv_say and print take one text value; game_speed a number */
static int text_tail(char const *command, void *context)
{
	(void)context;
	if (!strcmp(command, "sv_say") || !strcmp(command, "print"))
		return 1;
	return -1;
}

static void check(char const *line, int expected_result, char const *expected)
{
	char expression[256];
	int result = console_shell_translate(line, expression, sizeof(expression), text_tail, NULL);

	if (result != expected_result || strcmp(expression, expected))
	{
		printf("FAIL \"%s\": %d \"%s\", wanted %d \"%s\"\n", line, result, expression, expected_result, expected);
		failures++;
	}
}

int main(void)
{
	char small[8];

	/* bare words and numbers */
	check("game_speed 2", _console_shell_ok, "game_speed 2");
	check("  game_speed   0.5  ", _console_shell_ok, "game_speed 0.5");
	check("game_speed -1", _console_shell_ok, "game_speed -1");
	check("cheat_all_weapons", _console_shell_ok, "cheat_all_weapons");
	check("", _console_shell_ok, "");
	/* quotes: double and single are one value */
	check("sv_say \"hello there\"", _console_shell_ok, "sv_say \"hello there\"");
	check("sv_say 'hello there'", _console_shell_ok, "sv_say \"hello there\"");
	check("print 'say \"hi\"'", _console_shell_ok, "print \"say hi\"");
	check("object_create \"warthog\"", _console_shell_ok, "object_create \"warthog\"");
	/* a text value takes the rest of the line */
	check("sv_say hello there", _console_shell_ok, "sv_say \"hello there\"");
	check("sv_say hello 'big' world 2", _console_shell_ok, "sv_say \"hello big world 2\"");
	check("sv_say hi", _console_shell_ok, "sv_say hi");
	/* a call inside a call, passed through */
	check("game_speed (* 2 0.5)", _console_shell_ok, "game_speed (* 2 0.5)");
	check("print (if (= 1 1) \"a b\" \"c\")", _console_shell_ok, "print (if (= 1 1) \"a b\" \"c\")");
	/* the console's own form, whole */
	check("(game_speed 2)", _console_shell_ok, "(game_speed 2)");
	check("  (sv_say \"hello there\")  ", _console_shell_ok, "(sv_say \"hello there\")");
	check("(game_speed", _console_shell_ok, "(game_speed");
	/* what is wrong */
	check("sv_say \"hello", _console_shell_unterminated_quote, "");
	check("sv_say 'hello", _console_shell_unterminated_quote, "");
	check("game_speed (* 2 1", _console_shell_unmatched_parenthesis, "");
	if (console_shell_translate("sv_say hello there", small, sizeof(small), text_tail, NULL) != _console_shell_too_long ||
		small[0])
	{
		printf("FAIL: too long\n");
		failures++;
	}
	if (failures)
		printf("%d failed\n", failures);
	return failures != 0;
}
