/*
TERMINAL.H

header included in hcex build.
*/

#ifndef __TERMINAL_H
#define __TERMINAL_H
#pragma once

/* ---------- headers */

#include "dialogs/edit_text.h"
#include "input/input.h"
#include "math/real_math.h"

/* ---------- constants */

enum
{
	TERMINAL_PRINTF_MAXIMUM_LINE_LENGTH = 255,
	TERMINAL_GETS_MAXIMUM_KEYS = 32,
	TERMINAL_GETS_MAXIMUM_LINE_LENGTH = 255,
	TERMINAL_GETS_MAXIMUM_PROMPT_LENGTH = 31,
	TERMINAL_TAB_STOP_COUNT = 3,
};

/* ---------- macros */

/* ---------- structures */

struct terminal_gets_state
{
	short key_count;
	struct key_stroke keys[TERMINAL_GETS_MAXIMUM_KEYS];
	real_argb_color color;
	char prompt[TERMINAL_GETS_MAXIMUM_PROMPT_LENGTH+1];
	char result[TERMINAL_PRINTF_MAXIMUM_LINE_LENGTH+1];
	struct edit_text edit;
};

/* ---------- prototypes/TERMINAL.C */

void terminal_initialize(
	void);
void terminal_dispose(
	void);
void terminal_clear(void);
boolean terminal_gets_active(void);
boolean terminal_gets_begin(struct terminal_gets_state *state);
void terminal_gets_end(struct terminal_gets_state *state);
void terminal_draw(
	void);
boolean terminal_update(void);
void terminal_printf(real_argb_color const *color, char const *format, ...);
#ifdef HALO_CONSOLE_STATUS_LINE
/* port: one line kept on screen while it is called again, its text
replaced: a state that lasts, such as the shader cache's compile
(port/switch/guest/dk_shaders.c). When the calls stop, it fades as any
line does. */
void terminal_status_printf(real_argb_color const *color, char const *format, ...);
#endif
/* port: the drop-down console (terminal.c; nothing on a build without it,
and with display.console_style "classic"): a command typed, echoed in its
scrollback; Tab's candidates shown in a list under it (wanted: whether it
draws them, else console.c prints them as retail), the next (or, -1, the
previous) chosen, and the list put away */
void terminal_echo_command(char const *command);
boolean terminal_completion_wanted(void);
void terminal_completion_show(char const **names, short count, short column);
char const *terminal_completion_cycle(short direction);
void terminal_completion_clear(void);
/* port: the drop-down console's shell (terminal.c): whether it is the
style in use, a line of its own in its colors (an answer, a note, an
error), the scrollback cleared (clear, Ctrl+L), and the columns a line
has on the screen */
boolean terminal_shell_style(void);
void terminal_shell_output(char const *format, ...);
void terminal_shell_note(char const *format, ...);
void terminal_shell_error(char const *format, ...);
void terminal_scrollback_clear(void);
short terminal_columns(void);
/* port: console.c's: the command typed age commands ago (0 the newest) */
char const *console_history_entry(short age);

/* port: kinds of what is logged, and whether the console shows a kind on
screen (config.toml's game.console_log) */
enum
{
	_terminal_message_serious = 0,
	_terminal_message_important,
	_terminal_message_chatter,
};
boolean terminal_shows(short kind);
/* port: whether a command someone typed is running (hs_compile_and_evaluate):
what it logs, its errors too, is its answer, always shown */
extern boolean terminal_command_running;

/* ---------- globals */

extern short terminal_render_enable;

/* ---------- public code */

#endif // __TERMINAL_H
