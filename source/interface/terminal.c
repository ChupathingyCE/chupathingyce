/*
TERMINAL.C

symbols in this file:
000D2B20 0040:
	_terminal_initialize (0000)
000D2B60 0030:
	_terminal_dispose (0000)
000D2B90 0090:
	_code_000d2b90 (0000)
000D2C20 0030:
	_terminal_clear (0000)
000D2C50 0010:
	_terminal_gets_active (0000)
000D2C60 0080:
	_terminal_gets_begin (0000)
000D2CE0 0020:
	_terminal_gets_end (0000)
000D2D00 00c0:
	_code_000d2d00 (0000)
000D2DC0 0050:
	_code_000d2dc0 (0000)
000D2E10 02b0:
	_terminal_draw (0000)
000D30C0 00a0:
	_code_000d30c0 (0000)
000D3160 0030:
	_terminal_update (0000)
000D3190 0130:
	_terminal_printf (0000)
002712D0 0006:
	_terminal_tab_stops (0000)
002712D8 0010:
	??_C@_0BA@LJDMLJDP@terminal?5output?$AA@ (0000)
002712E8 0006:
	??_C@_05HDBKIDE@state?$AA@ (0000)
002712F0 0024:
	??_C@_0CE@JGPOFDIE@c?3?2halo?2SOURCE?2interface?2termina@ (0000)
00271314 0015:
	??_C@_0BF@NFLIKMOD@new_line_index?$CB?$DNNONE?$AA@ (0000)
00271330 0050:
	??_C@_0FA@BEBNLNOA@terminal_printf?5call?5generated?5?$CF@ (0000)
00301D99 0001:
	_terminal_render_enable (0000)
00454224 001c:
	_bss_00454224 (0000)
*/

/* ---------- headers */

#include "cseries.h"
#include "mcc_hud_draw.h" /* port: MCC-only terminal sizing. */
#include "terminal.h"

#include "cache_files.h"
#include "game.h"
#include "data.h"
#include "interface.h"
#include "console.h"
#include "telnet_console.h"
#include "rasterizer.h"
#include "render.h"
#ifdef HALO_PORT_FRAMES_BETWEEN_TICKS
#include "main/main.h"
#endif
#include "draw_string.h"
#include "byte_swapping.h"
#include "tag_groups.h"
#include "text_group.h"
#include "font_group.h"
#include "cutscene/cinematics.h" /* port: draw_quad, the drop-down console's panel */
#include "cseries/cseries_windows.h" /* port: system_milliseconds */
#include "hs/hs.h" /* port: the drop-down console's wording */
#include "interface/player_ui.h" /* port: the drop-down console's prompt */
#include "saved games/player_profile.h"
#include <stdarg.h>

/* ---------- constants */

enum
{
	OUTPUT_LINGER_TIME = 120,
	OUTPUT_FADE_TIME = 30,
	OUTPUT_TOTAL_TIME = 150,
	OUTPUT_MAXIMUM_LINES = 32,
	INSERTION_POINT_FLASH_PERIOD = 500,
};

/* port: the drop-down console (terminal_dropdown_draw): every build that
has the high-res text, so not the consoles, and never the original Xbox,
which keeps retail's console */
#ifndef HALO_CONSOLE_NO_HIRES
#define TERMINAL_DROPDOWN
#endif

/* ---------- macros */

#define output_line_get(index) ((struct output_line_datum *)datum_get(terminal_globals.output_lines, (index)))

/* ---------- structures */

struct terminal_globals
{
	boolean initialized;
	struct data_array *output_lines;
	long newest_output_line_index;
	long oldest_output_line_index;
	struct terminal_gets_state *input_state;
	boolean insertion_point_visible;
	long last_insertion_point_toggle;
};

struct output_line_datum
{
	short identifier;
	long newer_line_index;
	long older_line_index;
	boolean tabstop;
	char buffer[256];
	real_argb_color color;
	long timer;
};

/* ---------- prototypes */

static short terminal_font_line_height(short line_height);
static void terminal_draw_line(long font_tag_index, rectangle2d const *bounds, char const *text);
#ifdef TERMINAL_DROPDOWN
static void terminal_scroll(long rows);
int halo_console_wheel_steps(void);
static void terminal_scrollback_add(struct output_line_datum const *line);
static boolean terminal_dropdown_draw(long font_tag_index, short line_height);
#endif
#ifndef HALO_XBOX_CONSOLE
static boolean terminal_edit_key(struct edit_text *edit, struct key_stroke const *key);
#ifdef TERMINAL_DROPDOWN
static boolean terminal_shell_key(struct edit_text *edit, struct key_stroke const *key);
#endif
#endif
static void terminal_remove_line(long line_index);
static boolean terminal_update_input(void);
static void terminal_update_output(void);
static long terminal_new_line(void);

/* ---------- globals */

/* 0 = hidden, 1 = always drawn, 2 = drawn only while the console is open (the
default: the console's output stays off the game until it is opened) */
short terminal_render_enable = 2;

/* name from the 2003 PC demo PDB and the HCEX PDB (terminal file static const short[3]); January's 6 bytes
 * are identical to the demo's and it has no public for it (static) */
static const short terminal_tab_stops[] = {160, 320, 470};

static struct terminal_globals terminal_globals = {0};

/* ---------- public code */

void terminal_initialize(
	void)
{
	terminal_globals.output_lines = data_new("terminal output", OUTPUT_MAXIMUM_LINES, sizeof(struct output_line_datum));
	terminal_globals.initialized = TRUE;
	data_make_valid(terminal_globals.output_lines);
	terminal_globals.input_state = NULL;
	terminal_globals.newest_output_line_index = NONE;
	terminal_globals.oldest_output_line_index = NONE;

	return;
}

void terminal_dispose(
	void)
{
	if (terminal_globals.output_lines)
	{
		if (terminal_globals.output_lines->valid)
		{
			data_make_invalid(terminal_globals.output_lines);
		}
		data_dispose(terminal_globals.output_lines);
	}
	terminal_globals.initialized = FALSE;

	return;
}

void terminal_clear(
	void)
{
	if (terminal_globals.initialized)
	{
		terminal_globals.newest_output_line_index = NONE;
		terminal_globals.oldest_output_line_index = NONE;
		data_delete_all(terminal_globals.output_lines);
	}

	return;
}

boolean terminal_gets_active(
	void)
{
	return terminal_globals.input_state != NULL;
}

boolean terminal_gets_begin(
	struct terminal_gets_state *state)
{
	boolean result = FALSE;
	match_assert("c:\\halo\\SOURCE\\interface\\terminal.c", 195, state);

	if (!terminal_globals.input_state)
	{
		terminal_globals.input_state = state;
		terminal_globals.input_state->edit.buffer = terminal_globals.input_state->result;

		terminal_globals.input_state->edit.maximum_length = TERMINAL_GETS_MAXIMUM_LINE_LENGTH;
		edit_text_new(&terminal_globals.input_state->edit);
		state->key_count = 0;
		result = TRUE;
	}

	return result;
}

void terminal_gets_end(
	struct terminal_gets_state *state)
{
	if (state==terminal_globals.input_state)
	{
		terminal_globals.input_state = NULL;
	}

	return;
}

void terminal_draw(
	void)
{
	char buffer[288];
	rectangle2d terminal_gets_bounds;
	rectangle2d terminal_render_bounds;
	rectangle2d line_bounds;
	real_argb_color color;

	long font_tag_index = interface_get_tag_index(_interface_font_terminal);

	if (terminal_globals.initialized)
	{
		struct font_header *font = font_definition_get(font_tag_index);
		short line_height = font->ascending_height + font->descending_height + font->leading_height;
		short font_line_height = line_height;
		/* port: preserve native line spacing outside MCC maps. */
		line_height = mcc_terminal_line_height(font_tag_index, line_height);
		/* port: and the high-res console's, a little smaller (an MCC map's
		scaled terminal font keeps its own: terminal_draw_line) */
		if (line_height == font_line_height)
			line_height = terminal_font_line_height(line_height);
#ifdef TERMINAL_DROPDOWN
		/* port: the drop-down console, while it is open or sliding */
		if (terminal_dropdown_draw(font_tag_index, line_height))
			return;
#endif
		if (terminal_globals.input_state)
		{
			short buff_len;

			buffer[0] = '\0';
			terminal_globals.input_state->prompt[TERMINAL_GETS_MAXIMUM_PROMPT_LENGTH] = '\0';
			strcat(buffer, terminal_globals.input_state->prompt);
			terminal_globals.input_state->result[TERMINAL_PRINTF_MAXIMUM_LINE_LENGTH] = '\0';
			buff_len = csstrlen(buffer);

			strcpy(&buffer[buff_len], terminal_globals.input_state->result);

			terminal_gets_bounds.x0 = render.camera.window_bounds.x0;
			terminal_gets_bounds.x1 = render.camera.window_bounds.x1;
			terminal_gets_bounds.y0 = render.camera.window_bounds.y1 - line_height;
			terminal_gets_bounds.y1 = render.camera.window_bounds.y1;

			offset_rectangle2d(&terminal_gets_bounds, -render.camera.viewport_bounds.x0, -render.camera.viewport_bounds.y0);
			draw_string_set_draw_mode(font_tag_index, NONE, 0, 0, &terminal_globals.input_state->color);

			if (terminal_globals.insertion_point_visible)
			{
				short cursor_index = (buff_len + terminal_globals.input_state->edit.insertion_point_index);
				if (buffer[cursor_index]=='\0')
				{
					buffer[cursor_index+1] = '\0';
				}
				buffer[cursor_index] = '_';
			}
			/* port: MCC terminal font scaling does not modify the font tag;
			the high-res console's font and size (terminal_draw_line) */
			terminal_draw_line(font_tag_index, &terminal_gets_bounds, buffer);
		}

		if (terminal_render_enable == 1 || (terminal_render_enable >= 2 && terminal_globals.input_state))
		{
			struct output_line_datum *line;
			long line_index;
			short height = render.camera.window_bounds.y1 - line_height;

			for (line_index = terminal_globals.newest_output_line_index;
				line_index!=NONE && height - line_height > 0;
				line_index = line->older_line_index)
			{
				real fade_progress;

				line = output_line_get(line_index);
				fade_progress = 4.f - line->timer * 0.033333335f;

				color = line->color;
				color.alpha*= PIN(fade_progress, 0.f, 1.f);

				terminal_gets_bounds.x0 = render.camera.window_bounds.x0;
				terminal_gets_bounds.x1 = render.camera.window_bounds.x1;
				terminal_gets_bounds.y1 = height;
				height -= line_height;
				terminal_gets_bounds.y0 = height;

				offset_rectangle2d(
					&terminal_gets_bounds,
					-render.camera.viewport_bounds.x0,
					-render.camera.viewport_bounds.y0
				);

				if (line->tabstop)
				{
					draw_string_set_tab_stops(
						terminal_tab_stops,
						NUMBEROF(terminal_tab_stops)
					);
				}

				draw_string_set_draw_mode(font_tag_index, NONE, 0, 0, &color);
				/* port: MCC-only scaling, also applied to console output, and
				the high-res console's font and size (terminal_draw_line) */
				terminal_draw_line(font_tag_index, &terminal_gets_bounds, line->buffer);
				draw_string_set_tab_stops(terminal_tab_stops, 0);
			}
		}
	}

	return;
}

boolean terminal_update(
	void)
{
	boolean result = FALSE;
	if (terminal_globals.initialized)
	{
#if defined(TERMINAL_DROPDOWN) && !defined(HALO_CONSOLE)
		/* port: the wheel scrolls the drop-down console's scrollback (the
		desktop's mouse) */
		{
			long steps = halo_console_wheel_steps();

			if (terminal_globals.input_state && steps)
				terminal_scroll(steps * 3);
		}
#endif
		result = terminal_update_input();
		if (!console_is_active())
		{
			terminal_update_output();
		}
	}

	return result;
}

/* port/linux/src/port_config.c's */
const char *config_string(const char *name);

boolean terminal_command_running = FALSE;

/* port: whether the console shows a kind of what is logged on screen, as
config.toml's game.console_log says: "all", everything; "important" (and
anything else), the serious and the important; "none", the serious only
(the asserts that stop the game). debug.txt has every line whatever it is */
boolean terminal_shows(
	short kind)
{
	static short level = NONE;

	if (level == NONE)
	{
		const char *setting = config_string("game.console_log");

		level = _terminal_message_important;
		if (setting && !csstrcmp(setting, "all"))
			level = _terminal_message_chatter;
		else if (setting && !csstrcmp(setting, "none"))
			level = _terminal_message_serious;
	}
	return kind <= level;
}

void terminal_printf(
	real_argb_color const *color,
	char const *format,
	...)
{
	real_argb_color default_terminal_printf_color;
	va_list arglist;

	va_start(arglist, format);

	default_terminal_printf_color.alpha = 1.f;
	default_terminal_printf_color.red = 0.7f;
	default_terminal_printf_color.green = 0.7f;
	default_terminal_printf_color.blue = 0.7f;

	if (terminal_globals.initialized)
	{
		long line_index = terminal_new_line();
		match_assert("c:\\halo\\SOURCE\\interface\\terminal.c", 397, format);

		if (line_index!=NONE)
		{
			int count;
			struct output_line_datum *line = output_line_get(line_index);
			line->timer = 0;
			line->color = !color ? default_terminal_printf_color : *color;
			/* port: a line longer than the on-screen buffer is cut to fit (it
			halted here, from inside error(), so the real halt was never
			written down) */
			count = _vsnprintf(line->buffer, NUMBEROF(line->buffer)-2, format, arglist);
			if (count < 0 || count >= (int)(NUMBEROF(line->buffer)-2))
				line->buffer[NUMBEROF(line->buffer)-2] = 0;
			line->tabstop = strstr(line->buffer, "|t") != 0;
#ifdef TERMINAL_DROPDOWN
			terminal_scrollback_add(line);
#endif
			telnet_console_print(line->buffer);
		}
	}

	va_end(arglist);

	return;
}

#ifdef HALO_CONSOLE_STATUS_LINE
/* port: the status line, while it is on screen (terminal_status_printf):
a console's shader cache says how its compile is going (the Switch's) */
static long terminal_status_line_index = NONE;

void terminal_status_printf(
	real_argb_color const *color,
	char const *format,
	...)
{
	struct output_line_datum *line = NULL;
	char *arglist;
	int count;

	if (!terminal_globals.initialized)
		return;
	/* the line it was, unless it faded or the oldest line's place was
	taken for a new one */
	if (terminal_status_line_index != NONE)
		line = datum_try_and_get(terminal_globals.output_lines, terminal_status_line_index);
	if (!line)
	{
		terminal_status_line_index = terminal_new_line();
		line = output_line_get(terminal_status_line_index);
	}
	line->timer = 0;
	if (color)
	{
		line->color = *color;
	}
	else
	{
		line->color.alpha = 1.f;
		line->color.red = 0.7f;
		line->color.green = 0.7f;
		line->color.blue = 0.7f;
	}
	va_start(arglist, format);
	count = _vsnprintf(line->buffer, NUMBEROF(line->buffer)-2, format, arglist);
	va_end(arglist);
	if (count < 0 || count >= (int)(NUMBEROF(line->buffer)-2))
		line->buffer[NUMBEROF(line->buffer)-2] = 0;
	line->tabstop = strstr(line->buffer, "|t") != 0;

	return;
}
#endif

/* ---------- private code */

#ifndef HALO_CONSOLE_NO_HIRES
/* port/linux/src/text_hires.c's */
int text_hires_enabled(void);

/* port: the console with display.high_res_text on: its lines in the
console's own font (fonts.json's "console" role, Hack Bold) and this much
smaller, so that more of a line fits; off, the map's font at its size, as
retail. The consoles draw the map's font (HALO_CONSOLE_NO_HIRES). */
#define TERMINAL_HIRES_SCALE 0.85f

static real terminal_font_scale(
	void)
{
	return text_hires_enabled() ? TERMINAL_HIRES_SCALE : 1.f;
}
#else
#define terminal_font_scale() 1.f
#endif

/* port: the console's line height, as its font is drawn (terminal_draw_line) */
static short terminal_font_line_height(
	short line_height)
{
	real scale = terminal_font_scale();

	return scale == 1.f ? line_height : (short)(line_height * scale + 0.5f);
}

/* port: a line of the console, in its bounds: an MCC map's terminal font
scaled down (mcc_terminal_draw), else with the high-res text on in the
console's own font, terminal_font_scale times as large (laid out over the
bounds made as much wider, so that a line still reaches across the screen) */
static void terminal_draw_line(
	long font_tag_index,
	rectangle2d const *bounds,
	char const *text)
{
	real scale = terminal_font_scale();

	rasterizer_text_set_console_font(scale != 1.f);
	if (!mcc_terminal_draw(font_tag_index, bounds, text))
	{
		if (scale != 1.f)
		{
			rectangle2d layout = *bounds;

			layout.x1 = (short)PIN(bounds->x0 + (bounds->x1 - bounds->x0) / scale, SHORT_MIN, SHORT_MAX);
			layout.y1 = (short)PIN(bounds->y0 + (bounds->y1 - bounds->y0) / scale + 1.f, SHORT_MIN, SHORT_MAX);
			rasterizer_text_set_scale(scale, (real)bounds->x0, (real)bounds->y0);
			rasterizer_draw_string(&layout, NULL, NULL, 0, text);
			rasterizer_text_set_scale(1.f, 0.f, 0.f);
		}
		else
		{
			rasterizer_draw_string(bounds, NULL, NULL, 0, text);
		}
	}
	rasterizer_text_set_console_font(FALSE);

	return;
}

#ifdef TERMINAL_DROPDOWN
/* ---------- port: the drop-down console

display.console_style = "dropdown" (the default): the console is a panel of
its own that slides down from the top of the screen when it opens and back
up when it closes, display.console_height of the screen tall: deep navy,
near-opaque, with a lighter line along its top and an Xbox-blue edge along
its bottom; a header strip ("CONSOLE" and the keys it takes); the scrollback
(every line printed since the game started, up to
TERMINAL_SCROLLBACK_LINES, long ones wrapped at a space); and the prompt, a
blue "> " and a block caret, under a rule. PgUp and PgDn, and the mouse
wheel, scroll it; "^ ^ ^" under the scrollback says there is more below.
Tab's candidates are a list under the panel, Tab and Shift+Tab choosing one.

Its lines are worded and colored for it as they are kept
(terminal_scrollback_add): the commands typed in blue after "> ", the
script compiler's errors in plain words (terminal_wording, the one table of
them) in red after "Error:", warnings in amber after "Warning:", answers in
light grey-blue and the game's own notes in a dimmer grey. Retail's lines
are never changed: "classic" draws the console as retail does, the prompt
at the bottom and the last lines above it, fading, in retail's words. The
game keeps running underneath, as retail's console let it (a network game
is never paused). The original Xbox and the consoles keep retail's console. */

enum
{
	TERMINAL_SCROLLBACK_LINES = 512,
	/* the slide, open or shut, in milliseconds */
	TERMINAL_DROPDOWN_SLIDE_TIME = 160,
	/* Tab's candidates kept, and the rows of them shown */
	TERMINAL_COMPLETION_MAXIMUM = 256,
	TERMINAL_COMPLETION_ROWS = 8,
	TERMINAL_COMPLETION_NAME_LENGTH = 64,
};

/* the panel's colors (ARGB; draw_quad's texture halves the alpha, so the
panel's is laid on TERMINAL_PANEL_PASSES times: about 92% over the game) */
#define TERMINAL_PANEL_COLOR 0xF0050A1A
#define TERMINAL_PANEL_PASSES 4
#define TERMINAL_HEADER_COLOR 0xC00E1A38
#define TERMINAL_TOP_LINE_COLOR 0xFF3A4E78
#define TERMINAL_EDGE_COLOR 0xFF2896FF
#define TERMINAL_RULE_COLOR 0xFF1E2C4A
#define TERMINAL_PICK_COLOR 0xFF1B4E8C

/* the text's colors, each at least 4.5:1 against the panel */
static real_argb_color const terminal_color_output = { 1.f, 0.784f, 0.839f, 0.898f };   /* #C8D6E5 */
static real_argb_color const terminal_color_command = { 1.f, 0.157f, 0.588f, 1.f };     /* #2896FF */
static real_argb_color const terminal_color_error = { 1.f, 1.f, 0.42f, 0.42f };         /* #FF6B6B */
static real_argb_color const terminal_color_warning = { 1.f, 1.f, 0.702f, 0.278f };     /* #FFB347 */
static real_argb_color const terminal_color_ok = { 1.f, 0.486f, 1.f, 0.604f };          /* #7CFF9A */
static real_argb_color const terminal_color_note = { 1.f, 0.541f, 0.608f, 0.69f };      /* #8A9BB0 */

/* port/linux/src/port_config.c's */
double config_real(const char *name);
unsigned long config_changes(void);
#if !defined(HALO_CONSOLE)
/* port/linux/src/xinput_sdl.c's: whether the keyboard or mouse was used
last (else a controller) */
int halo_keyboard_prompts(void);
/* port/linux/src/d3d8_gl.c's: the display's pixels for each of the 480 lines */
float halo_screen_pixel_scale(void);
#endif

static struct
{
	char text[TERMINAL_PRINTF_MAXIMUM_LINE_LENGTH + 1];
	real_argb_color color;
	boolean tabstop;
	/* an echoed command's prompt: where its user, host, path and "$ " end
	(terminal_prompt); 0 for a line of one color */
	short marks[4];
} terminal_scrollback[TERMINAL_SCROLLBACK_LINES];
/* the lines kept, and where the next goes */
static long terminal_scrollback_count;
static long terminal_scrollback_next;
/* the rows scrolled back from the newest (terminal_scroll) */
static long terminal_scrollback_offset;

static struct
{
	/* how far it is down, 0 shut to 1 open, and when that was */
	real openness;
	unsigned long time;
	/* the input as it was last drawn, for the slide up after it closed */
	char input[TERMINAL_GETS_MAXIMUM_LINE_LENGTH + 1];
	short insertion_point;
} terminal_dropdown = { 0.f, 0, "", 0 };

/* Tab's candidates (terminal_completion_show), and the one chosen (NONE
before Tab chooses one) */
static struct
{
	char names[TERMINAL_COMPLETION_MAXIMUM][TERMINAL_COMPLETION_NAME_LENGTH];
	short count;
	short pick;
	/* the column of the input the word completed starts at */
	short column;
} terminal_completion = { { { 0 } }, 0, NONE, 0 };

/* "dropdown" unless display.console_style says "classic" */
static boolean terminal_dropdown_enabled(
	void)
{
	static unsigned long read_at = (unsigned long)-1;
	static boolean enabled = TRUE;

	if (read_at != config_changes())
	{
		char const *style = config_string("display.console_style");

		read_at = config_changes();
		enabled = !(style && !csstrcmp(style, "classic"));
	}
	return enabled;
}

/* display.console_height, a share of the screen (0.45) */
static real terminal_dropdown_height(
	void)
{
	static unsigned long read_at = (unsigned long)-1;
	static real height = 0.45f;

	if (read_at != config_changes())
	{
		real value = (real)config_real("display.console_height");

		read_at = config_changes();
		height = value > 0.f ? PIN(value, 0.2f, 0.9f) : 0.45f;
	}
	return height;
}

/* ---------- its wording

The script compiler's errors (hs_compile.c, as error() prints them while a
typed command runs: "<message>: <the source>"), in plain words for the
drop-down console alone. Each retail message, from its start, and its plain
form: "%s" is the word the error is about (the source, its parentheses
trimmed). The argument count's message, which names the function, is
terminal_reword's own. */
static struct
{
	char const *retail;
	char const *plain;
} const terminal_wording[] =
{
	{ "this is not a valid function or script name.", "command not found: %s" },
	{ "this is not a global variable reference, function call, or script call.", "command not found: %s" },
	{ "this is not a valid global variable.", "no such variable: %s" },
	{ "this is not a valid variable name.", "no such variable: %s" },
	{ "this is not a valid script name.", "no such script: %s" },
	{ "this is not a static script.", "not a static script: %s" },
	{ "this static script cannot be awakened.", "that script can't be woken: %s" },
	{ "this is not a valid object name.", "no such object: %s" },
	{ "this is not a valid ai encounter or squad.", "no such AI encounter or squad: %s" },
	{ "this is not a valid MCC AI encounter or squad.", "no such AI encounter or squad: %s" },
	{ "this is not a valid integer.", "expected a whole number: %s" },
	{ "this is not a valid real number.", "expected a number: %s" },
	{ "this is not a valid type.", "no such type: %s" },
	{ "i expected \"true\" or \"false\".", "expected true or false: %s" },
	{ "shorts must be in the range [-32767, 32768].", "out of range (-32767 to 32768): %s" },
	{ "this left parenthesis is unmatched.", "missing a closing parenthesis" },
	{ "this quoted constant is unterminated.", "missing a closing quote" },
	{ "unterminated comment.", "missing the end of a comment" },
	{ "this expression is empty.", "nothing to run" },
	{ "i didn't expect this argument.", "too many values: %s" },
	{ "i expected a variable to set and a value.", "usage: set <variable> <value>" },
	{ "i expected an assignment value.", "missing the value to set" },
	{ "it is illegal to set the value of variables in this context.", "variables can't be set here" },
	{ "it is illegal to block in this context.", "this can't wait here" },
	{ "the value of this expression (in a <void> slot) can never be used.", "this value is never used: %s" },
	{ "the sleep call requires a time and, optionally, a script name.", "usage: sleep <ticks> [<script>]" },
	{ "the sleep_until call requires a condition and, optionally, a period.", "usage: sleep_until <condition> [<ticks>]" },
};

/* the word an error is about: its source with spaces, parentheses and
quotes trimmed, and only its first word */
static void terminal_reword_subject(
	char const *source,
	char *subject,
	long size)
{
	long length = 0;

	while (*source == ' ' || *source == '(' || *source == '"')
		source++;
	while (source[length] && source[length] != ' ' && source[length] != ')' && source[length] != '"' &&
		length < size - 1)
	{
		length++;
	}
	csstrncpy(subject, source, length);
	subject[length] = 0;
}

/* a type's name as the drop-down console says it */
static char const *terminal_type_word(
	char const *type,
	long length)
{
	static struct
	{
		char const *type;
		char const *word;
	} const words[] =
	{
		{ "real", "number" }, { "short", "whole number" }, { "long", "whole number" },
		{ "boolean", "true/false" }, { "string", "text" },
	};
	short index;

	for (index = 0; index < (short)NUMBEROF(words); index++)
	{
		if ((long)strlen(words[index].type) == length && !strncmp(words[index].type, type, length))
			return words[index].word;
	}
	return NULL;
}

/* a function's usage with its types in everyday words: "<real>" as
"<number>" */
static void terminal_usage_words(
	char const *usage,
	char *words,
	long size)
{
	long length = 0;

	while (*usage && length < size - 1)
	{
		char const *close = *usage == '<' ? strchr(usage, '>') : NULL;
		char const *word = close ? terminal_type_word(usage + 1, (long)(close - usage - 1)) : NULL;

		if (word)
		{
			length += snprintf(words + length, size - length, "<%s>", word);
			length = MIN(length, size - 1);
			usage = close + 1;
			continue;
		}
		words[length++] = *usage++;
	}
	words[length] = 0;
}

/* a typed command's error as a shell says it, into plain: "game_speed:
needs 1 value (usage: game_speed <number>)", "chupathingy: command not
found: sv_ (did you mean sv_say?)", the others from terminal_wording after
"chupathingy: "; a line that is none of the compiler's is only tidied */
static void terminal_reword(
	char const *text,
	char *plain,
	long size)
{
	char subject[64];
	char hint[128];
	short index;

	hint[0] = 0;
	/* (the argument count's: the "<name>" call requires exactly <n> arguments.) */
	if (!strncmp(text, "the \"", 5) && strstr(text, "\" call requires exactly "))
	{
		char name[64];
		char usage[256];
		char words[256];
		char const *quote = strchr(text + 5, '"');
		long count = atol(strstr(text, "requires exactly ") + 17);
		long length = MIN((long)(quote - (text + 5)), (long)sizeof(name) - 1);

		csstrncpy(name, text + 5, length);
		name[length] = 0;
		if (!count)
			snprintf(plain, size, "%s: takes no values", name);
		else if (hs_function_usage(name, usage, sizeof(usage)))
		{
			terminal_usage_words(usage, words, sizeof(words));
			snprintf(plain, size, "%s: needs %ld value%s (usage: %s)", name, count, count == 1 ? "" : "s", words);
		}
		else
			snprintf(plain, size, "%s: needs %ld value%s", name, count, count == 1 ? "" : "s");
		return;
	}
	for (index = 0; index < (short)NUMBEROF(terminal_wording); index++)
	{
		long length = (long)strlen(terminal_wording[index].retail);
		char const *source;
		char message[192];

		if (strncmp(text, terminal_wording[index].retail, length))
			continue;
		source = text + length;
		while (*source == ':' || *source == ' ')
			source++;
		terminal_reword_subject(source, subject, sizeof(subject));
		if (strstr(terminal_wording[index].plain, "%s"))
			snprintf(message, sizeof(message), terminal_wording[index].plain, subject);
		else
			snprintf(message, sizeof(message), "%s", terminal_wording[index].plain);
		/* (a name not found: the closest one, or Tab for those it starts) */
		if (!strncmp(terminal_wording[index].plain, "command not found", 17) && subject[0])
		{
			char closest[64];
			char const *matches[2];

			if (hs_closest_name(subject, closest, sizeof(closest)))
				snprintf(hint, sizeof(hint), " (did you mean %s?)", closest);
			else if (hs_tokens_enumerate(subject, NONE, matches, NUMBEROF(matches)) > 1)
				snprintf(hint, sizeof(hint), " (Tab lists those starting %s)", subject);
		}
		snprintf(plain, size, "chupathingy: %s%s", message, hint);
		return;
	}
	/* (another: ".: " made ": ", a final period dropped) */
	{
		char const *from = text;
		long length = 0;

		while (*from && length < size - 1)
		{
			if (from[0] == '.' && from[1] == ':')
			{
				from++;
				continue;
			}
			plain[length++] = *from++;
		}
		plain[length] = 0;
		while (length && (plain[length - 1] == '.' || plain[length - 1] == ' '))
			plain[--length] = 0;
	}
}

/* a line into the scrollback as it is: its text and color */
static void terminal_scrollback_put(
	char const *text,
	real_argb_color const *color,
	boolean tabstop)
{
	long slot = terminal_scrollback_next;

	csstrncpy(terminal_scrollback[slot].text, text, NUMBEROF(terminal_scrollback[slot].text) - 1);
	terminal_scrollback[slot].text[NUMBEROF(terminal_scrollback[slot].text) - 1] = 0;
	terminal_scrollback[slot].color = *color;
	terminal_scrollback[slot].tabstop = tabstop;
	csmemset(terminal_scrollback[slot].marks, 0, sizeof(terminal_scrollback[slot].marks));
	terminal_scrollback_next = (slot + 1) % TERMINAL_SCROLLBACK_LINES;
	if (terminal_scrollback_count < TERMINAL_SCROLLBACK_LINES)
		terminal_scrollback_count++;
	/* (a line printed while scrolled back keeps the view where it is) */
	if (terminal_scrollback_offset)
		terminal_scrollback_offset++;
}

/* a line printed, worded and colored for the drop-down console: what a
typed command logs in white is its error (errors.c's lines), console_warning's
pure red a warning, the console's grey an answer (or, outside a command, a
note of the game's), a script's green print an answer too */
static void terminal_scrollback_add(
	struct output_line_datum const *line)
{
	char text[TERMINAL_PRINTF_MAXIMUM_LINE_LENGTH + 1];
	char plain[TERMINAL_PRINTF_MAXIMUM_LINE_LENGTH + 1];
	char worded[TERMINAL_PRINTF_MAXIMUM_LINE_LENGTH + 1];
	real_argb_color const *color = &line->color;
	char const *start;
	long length;

	csstrncpy(text, line->buffer, NUMBEROF(text) - 1);
	text[NUMBEROF(text) - 1] = 0;
	/* (an error()'s line ends in its own line break) */
	length = (long)strlen(text);
	while (length && (text[length - 1] == '\r' || text[length - 1] == '\n'))
		text[--length] = 0;
	start = text;

	if (line->color.red == 1.f && line->color.green == 0.f && line->color.blue == 0.f)
	{
		terminal_reword(start, plain, sizeof(plain));
		snprintf(worded, sizeof(worded), "warning: %s", plain);
		terminal_scrollback_put(worded, &terminal_color_warning, FALSE);
		return;
	}
	if (line->color.red == line->color.green && line->color.green == line->color.blue)
	{
		while (*start == ' ' || *start == '#')
			start++;
		if (terminal_command_running && line->color.red == 1.f)
		{
			terminal_reword(start, plain, sizeof(plain));
			terminal_scrollback_put(plain, &terminal_color_error, FALSE);
			return;
		}
		if (!strncmp(start, "EXCEPTION", 9) || !strncmp(start, "ERROR", 5) || !strncmp(start, "error", 5))
			color = &terminal_color_error;
		else if (!strncmp(start, "WARNING", 7) || !strncmp(start, "warning", 7))
			color = &terminal_color_warning;
		else
			color = terminal_command_running ? &terminal_color_output : &terminal_color_note;
		start = text;
	}
	else if (line->color.red == 0.f && line->color.green == 1.f && line->color.blue == 0.f)
	{
		/* (a script's print: an answer) */
		color = &terminal_color_output;
	}
	terminal_scrollback_put(start, color, line->tabstop);
}

/* the shell's prompt, "openrift@chupathingy:bloodgulch $ " (the
profile's name, then the map, or "menu" in the menus), the host left out
on a narrow screen: where its user, host, path and "$ " end, in marks */
static boolean terminal_prompt_wide = TRUE;

static void terminal_prompt(
	char *prompt,
	long size,
	short *marks)
{
	char user[32] = "player";
	char map[64] = "menu";
	char const *loaded = cache_file_loaded_map_name();
	long length;

	if (player_ui_get_active_player_profile_index(0) != NONE)
	{
		struct player_profile profile;
		short index;
		short out = 0;

		player_ui_get_active_player_profile(0, &profile);
		for (index = 0; index < MAXIMUM_PLAYER_PROFILE_NAME_LENGTH && profile.player_name[index] && out < 31; index++)
		{
			wchar_t character = profile.player_name[index];

			if (character >= 'A' && character <= 'Z')
				user[out++] = (char)(character - 'A' + 'a');
			else if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
				character == '_' || character == '-' || character == '.')
			{
				user[out++] = (char)character;
			}
			else if (character == ' ' && out)
				user[out++] = '_';
		}
		if (out)
			user[out] = 0;
	}
	if (loaded && *loaded)
	{
		char const *base = strrchr(loaded, '\\');

		base = base ? base + 1 : loaded;
		if (csstrcmp(base, "ui"))
		{
			csstrncpy(map, base, sizeof(map) - 1);
			map[sizeof(map) - 1] = 0;
		}
	}
	length = snprintf(prompt, size, "%s", user);
	marks[0] = (short)MIN(length, size - 1);
	length = snprintf(prompt + marks[0], size - marks[0], "%s:", terminal_prompt_wide ? "@chupathingy" : "");
	marks[1] = (short)MIN(marks[0] + length, size - 1);
	length = snprintf(prompt + marks[1], size - marks[1], "%s", map);
	marks[2] = (short)MIN(marks[1] + length, size - 1);
	length = snprintf(prompt + marks[2], size - marks[2], " $ ");
	marks[3] = (short)MIN(marks[2] + length, size - 1);
}

void terminal_echo_command(
	char const *command)
{
	char text[TERMINAL_PRINTF_MAXIMUM_LINE_LENGTH + 1];
	short marks[4];
	long slot = terminal_scrollback_next;

	terminal_prompt(text, sizeof(text), marks);
	csstrncpy(text + marks[3], command, sizeof(text) - marks[3] - 1);
	text[sizeof(text) - 1] = 0;
	terminal_scrollback_put(text, &terminal_color_output, FALSE);
	csmemcpy(terminal_scrollback[slot].marks, marks, sizeof(marks));
}

boolean terminal_shell_style(
	void)
{
	return terminal_dropdown_enabled();
}

static void terminal_shell_line(
	real_argb_color const *color,
	char const *format,
	va_list arguments)
{
	char text[TERMINAL_PRINTF_MAXIMUM_LINE_LENGTH + 1];

	vsnprintf(text, sizeof(text), format, arguments);
	terminal_scrollback_put(text, color, FALSE);
	telnet_console_print(text);
}

void terminal_shell_output(
	char const *format,
	...)
{
	va_list arguments;

	va_start(arguments, format);
	terminal_shell_line(&terminal_color_output, format, arguments);
	va_end(arguments);
}

void terminal_shell_note(
	char const *format,
	...)
{
	va_list arguments;

	va_start(arguments, format);
	terminal_shell_line(&terminal_color_note, format, arguments);
	va_end(arguments);
}

void terminal_shell_error(
	char const *format,
	...)
{
	va_list arguments;

	va_start(arguments, format);
	terminal_shell_line(&terminal_color_error, format, arguments);
	va_end(arguments);
}

void terminal_scrollback_clear(
	void)
{
	terminal_scrollback_count = 0;
	terminal_scrollback_offset = 0;
}

/* (the columns of the panel as last drawn) */
static long terminal_dropdown_columns = 100;

short terminal_columns(
	void)
{
	return (short)PIN(terminal_dropdown_columns, 20, 400);
}

/* ---------- Ctrl+R: the history searched back

While it is on, the keys typed are what is looked for, the input is the
newest command that has it (Ctrl+R again: the one before that), Enter runs
it, and Escape or Tab, or an arrow key, ends the search and leaves it to
change. */
static struct
{
	boolean active;
	char query[64];
	/* the command found: its age (console_history_entry) */
	short age;
	boolean failed;
} terminal_search = { FALSE, "", NONE, FALSE };

/* the newest command from age back that has the query: its age, or NONE */
static short terminal_search_find(
	short from)
{
	char const *entry;
	short age;

	for (age = from; (entry = console_history_entry(age)) != NULL; age++)
	{
		if (strstr(entry, terminal_search.query))
			return age;
	}
	return NONE;
}

static void terminal_search_show(
	struct edit_text *edit,
	short from)
{
	short age = terminal_search.query[0] ? terminal_search_find(from) : NONE;

	terminal_search.failed = terminal_search.query[0] && age == NONE;
	if (age == NONE)
		return;
	terminal_search.age = age;
	csstrncpy(edit->buffer, console_history_entry(age), edit->maximum_length);
	edit->buffer[edit->maximum_length] = 0;
	edit->insertion_point_index = (short)strlen(edit->buffer);
	edit->selection_start_index = NONE;
}

/* ---------- Tab's candidates */

boolean terminal_completion_wanted(
	void)
{
	return terminal_dropdown_enabled();
}

void terminal_completion_show(
	char const **names,
	short count,
	short column)
{
	short index;

	terminal_completion.count = (short)MIN(count, TERMINAL_COMPLETION_MAXIMUM);
	for (index = 0; index < terminal_completion.count; index++)
	{
		csstrncpy(terminal_completion.names[index], names[index], TERMINAL_COMPLETION_NAME_LENGTH - 1);
		terminal_completion.names[index][TERMINAL_COMPLETION_NAME_LENGTH - 1] = 0;
	}
	terminal_completion.pick = NONE;
	terminal_completion.column = column;
}

char const *terminal_completion_cycle(
	short direction)
{
	if (terminal_completion.count < 2)
		return NULL;
	if (terminal_completion.pick == NONE)
		terminal_completion.pick = direction < 0 ? (short)(terminal_completion.count - 1) : 0;
	else
		terminal_completion.pick = (short)((terminal_completion.pick + direction + terminal_completion.count) %
			terminal_completion.count);
	return terminal_completion.names[terminal_completion.pick];
}

void terminal_completion_clear(
	void)
{
	terminal_completion.count = 0;
	terminal_completion.pick = NONE;
}

/* the drop-down console's shell keys, as bash and zsh take them: Ctrl+R
searches the history back (and, while it does, takes the keys typed),
Ctrl+A and Ctrl+E go to the start and the end, Ctrl+U clears to the start,
Ctrl+W deletes the word before, Ctrl+L clears the screen. TRUE when it took
the key to itself (console.c does not see it); others go on as before */
static boolean terminal_shell_key(
	struct edit_text *edit,
	struct key_stroke const *key)
{
	boolean control = TEST_FLAG(key->modifier_flags, 1);
	short at, start;

	if (!terminal_dropdown_enabled())
	{
		terminal_search.active = FALSE;
		return FALSE;
	}
	if (key->key_code == _key_shift || key->key_code == _key_control || key->key_code == _key_alt)
		return FALSE;
	if (terminal_search.active)
	{
		long length = (long)strlen(terminal_search.query);

		if (control && key->key_code == _key_r)
		{
			/* (the next older one) */
			terminal_search_show(edit, (short)(terminal_search.age == NONE ? 0 : terminal_search.age + 1));
			return TRUE;
		}
		if (key->key_code == _key_backspace)
		{
			if (length)
				terminal_search.query[length - 1] = 0;
			terminal_search_show(edit, 0);
			return TRUE;
		}
		if (key->key_code == _key_escape || key->key_code == _key_tab || (control && key->key_code == _key_g))
		{
			terminal_search.active = FALSE;
			return TRUE;
		}
		if (key->key_code == _key_return || key->key_code == _keypad_enter)
		{
			/* (run, as console.c does with the input: the match) */
			terminal_search.active = FALSE;
			return FALSE;
		}
		if (!control && key->ascii_code >= ' ' && key->ascii_code < 127 && length < (long)sizeof(terminal_search.query) - 1)
		{
			terminal_search.query[length] = key->ascii_code;
			terminal_search.query[length + 1] = 0;
			terminal_search_show(edit, terminal_search.age == NONE ? 0 : terminal_search.age);
			return TRUE;
		}
		/* (anything else ends it, and does what it does) */
		terminal_search.active = FALSE;
	}
	if (!control)
		return FALSE;
	switch (key->key_code)
	{
	case _key_r:
		terminal_search.active = TRUE;
		terminal_search.query[0] = 0;
		terminal_search.age = NONE;
		terminal_search.failed = FALSE;
		return TRUE;
	case _key_a:
		edit->selection_start_index = NONE;
		edit->insertion_point_index = 0;
		return TRUE;
	case _key_e:
		edit->selection_start_index = NONE;
		edit->insertion_point_index = (short)strlen(edit->buffer);
		return TRUE;
	case _key_u:
		at = (short)MIN(edit->insertion_point_index, (short)strlen(edit->buffer));
		memmove(edit->buffer, edit->buffer + at, strlen(edit->buffer + at) + 1);
		edit->insertion_point_index = 0;
		edit->selection_start_index = NONE;
		return TRUE;
	case _key_w:
		at = (short)MIN(edit->insertion_point_index, (short)strlen(edit->buffer));
		start = at;
		while (start > 0 && edit->buffer[start - 1] == ' ')
			start--;
		while (start > 0 && edit->buffer[start - 1] != ' ')
			start--;
		memmove(edit->buffer + start, edit->buffer + at, strlen(edit->buffer + at) + 1);
		edit->insertion_point_index = start;
		edit->selection_start_index = NONE;
		return TRUE;
	case _key_l:
		terminal_scrollback_clear();
		return TRUE;
	default:
		return FALSE;
	}
}

/* ---------- its drawing */

/* the rows a line takes, wrapped at columns (at the last space that fits,
else where it must), and the start of each, at most maximum of them */
static short terminal_wrap(
	char const *text,
	boolean tabstop,
	long columns,
	short *starts,
	short maximum)
{
	short length = (short)strlen(text);
	short start = 0;
	short count = 0;

	if (tabstop || columns < 8)
	{
		starts[0] = 0;
		return 1;
	}
	while (count < maximum)
	{
		short end;

		starts[count++] = start;
		if (length - start <= columns)
			break;
		end = (short)(start + columns);
		while (end > start + columns / 2 && text[end] != ' ')
			end--;
		if (text[end] != ' ')
			end = (short)(start + columns);
		start = end;
		while (text[start] == ' ')
			start++;
		if (!text[start])
			break;
	}
	return count;
}

/* draws a string of up to length characters */
static void terminal_dropdown_text(
	long font_tag_index,
	rectangle2d const *bounds,
	char const *text,
	short length,
	real_argb_color const *color,
	boolean tabstop)
{
	char row[TERMINAL_PRINTF_MAXIMUM_LINE_LENGTH + 1];

	length = (short)MAX(0, MIN(length, (short)(NUMBEROF(row) - 1)));
	csstrncpy(row, text, length);
	row[length] = 0;
	if (tabstop)
		draw_string_set_tab_stops(terminal_tab_stops, NUMBEROF(terminal_tab_stops));
	draw_string_set_draw_mode(font_tag_index, NONE, 0, 0, color);
	terminal_draw_line(font_tag_index, bounds, row);
	draw_string_set_tab_stops(terminal_tab_stops, 0);
}

/* a rectangle, laid on passes times (draw_quad halves its alpha) */
static void terminal_dropdown_fill(
	short x0,
	short y0,
	short x1,
	short y1,
	pixel32 color,
	short passes)
{
	rectangle2d rectangle;

	rectangle.x0 = x0;
	rectangle.y0 = y0;
	rectangle.x1 = x1;
	rectangle.y1 = y1;
	while (passes-- > 0)
		draw_quad(&rectangle, color);
}

/* a row with a prompt in it (marks: where its user, host, path and "$ "
end), each part in its color, the rest in color: monospaced, so each part
starts width a character after the last */
static void terminal_dropdown_prompt_text(
	long font_tag_index,
	rectangle2d const *row,
	char const *text,
	short length,
	short const *marks,
	real_argb_color const *color,
	real width)
{
	real_argb_color const *colors[5];
	short ends[5];
	short part, start = 0;

	colors[0] = &terminal_color_ok;
	colors[1] = &terminal_color_note;
	colors[2] = &terminal_color_command;
	colors[3] = &terminal_color_output;
	colors[4] = color;
	ends[0] = marks[0];
	ends[1] = marks[1];
	ends[2] = marks[2];
	ends[3] = marks[3];
	ends[4] = length;
	for (part = 0; part < 5 && start < length; part++)
	{
		short end = (short)MIN(ends[part], length);
		rectangle2d bounds = *row;

		if (end <= start)
			continue;
		bounds.x0 = (short)(row->x0 + start * width);
		terminal_dropdown_text(font_tag_index, &bounds, text + start, (short)(end - start), colors[part], FALSE);
		start = end;
	}
}

/* Tab's candidates, under the panel at the word's column: the one chosen
lit, and how many more there are beyond the rows shown */
static void terminal_completion_draw(
	long font_tag_index,
	short line_height,
	short left,
	short top,
	real width)
{
	short rows = (short)MIN(terminal_completion.count, TERMINAL_COMPLETION_ROWS);
	short first = 0;
	short longest = 0;
	short index;
	short padding = (short)MAX(line_height / 3, 2);
	short right;
	boolean more = terminal_completion.count > TERMINAL_COMPLETION_ROWS;

	/* (only while Tab goes through them: listed, they are in the scrollback) */
	if (terminal_completion.count < 2 || terminal_completion.pick == NONE)
		return;
	if (terminal_completion.pick >= rows)
		first = (short)(terminal_completion.pick - rows + 1);
	for (index = 0; index < terminal_completion.count; index++)
		longest = (short)MAX(longest, (short)strlen(terminal_completion.names[index]));
	longest = (short)MAX(longest, 12);
	right = (short)(left + (longest + 2) * width + 2 * padding);
	terminal_dropdown_fill(left, top, right, (short)(top + (rows + (more ? 1 : 0)) * line_height + 2 * padding),
		TERMINAL_PANEL_COLOR, TERMINAL_PANEL_PASSES);
	terminal_dropdown_fill(left, top, right, (short)(top + 1), TERMINAL_EDGE_COLOR, 1);
	for (index = 0; index < rows; index++)
	{
		short name = (short)(first + index);
		rectangle2d bounds;

		bounds.x0 = (short)(left + padding + width);
		bounds.x1 = right;
		bounds.y0 = (short)(top + padding + index * line_height);
		bounds.y1 = (short)(bounds.y0 + line_height);
		if (name == terminal_completion.pick)
			terminal_dropdown_fill(left, bounds.y0, right, bounds.y1, TERMINAL_PICK_COLOR, 3);
		terminal_dropdown_text(font_tag_index, &bounds, terminal_completion.names[name],
			(short)strlen(terminal_completion.names[name]),
			name == terminal_completion.pick ? &terminal_color_output : &terminal_color_note, FALSE);
	}
	if (more)
	{
		char text[32];
		rectangle2d bounds;

		snprintf(text, sizeof(text), "+%d more", terminal_completion.count - rows);
		bounds.x0 = (short)(left + padding + width);
		bounds.x1 = right;
		bounds.y0 = (short)(top + padding + rows * line_height);
		bounds.y1 = (short)(bounds.y0 + line_height);
		terminal_dropdown_text(font_tag_index, &bounds, text, (short)strlen(text), &terminal_color_note, FALSE);
	}
}

/* the drop-down console, while it is open or sliding: FALSE when it is
shut (or display.console_style is "classic"), for retail's drawing */
static boolean terminal_dropdown_draw(
	long font_tag_index,
	short line_height)
{
	unsigned long now = system_milliseconds();
	real step = (real)(now - terminal_dropdown.time) / (real)TERMINAL_DROPDOWN_SLIDE_TIME;
	boolean open = terminal_globals.input_state != NULL;
	/* (across the whole screen, not the HUD's safe area retail's console
	keeps to) */
	rectangle2d window = render.camera.viewport_bounds;
	rectangle2d bounds, measured, cursor_bounds;
	real eased, full_height, shift, width, lead;
	short padding = (short)MAX(line_height / 2, 2);
	short panel_top, panel_bottom, header_bottom, prompt_top, rows_bottom, text_left;
	long columns;

	terminal_dropdown.time = now;
	if (!terminal_dropdown_enabled())
	{
		terminal_dropdown.openness = 0.f;
		return FALSE;
	}
	/* (frame-rate independent: by the time since the last frame, at most a
	slide's) */
	step = PIN(step, 0.f, 1.f);
	terminal_dropdown.openness = PIN(terminal_dropdown.openness + (open ? step : -step), 0.f, 1.f);
	if (terminal_dropdown.openness <= 0.f)
	{
		terminal_scrollback_offset = 0;
		terminal_completion_clear();
		return FALSE;
	}
	eased = terminal_dropdown.openness * terminal_dropdown.openness * (3.f - 2.f * terminal_dropdown.openness);

	offset_rectangle2d(&window, -render.camera.viewport_bounds.x0, -render.camera.viewport_bounds.y0);
	full_height = (window.y1 - window.y0) * terminal_dropdown_height();
	/* (its contents slide with it: laid out as when it is all the way down,
	moved up by what is still hidden) */
	shift = full_height * (eased - 1.f);
	panel_top = (short)(window.y0 + shift);
	panel_bottom = (short)(window.y0 + full_height * eased);

	/* (a character's width, as terminal_draw_line draws them:
	terminal_font_scale times as wide; the console's font is monospaced) */
	bounds = window;
	draw_string_set_draw_mode(font_tag_index, NONE, 0, 0, global_real_argb_white);
	/* (the advance: twenty less ten, so neither end's spacing counts) */
	draw_string_compute_bounds(&bounds, "mmmmmmmmmmmmmmmmmmmm", &measured, &cursor_bounds);
	width = (real)(measured.x1 - measured.x0);
	draw_string_compute_bounds(&bounds, "mmmmmmmmmm", &measured, &cursor_bounds);
	width = (width - (measured.x1 - measured.x0)) / 10.f;
	/* (and where the first character sits past the bounds' left: the caret
	goes where the characters are) */
	lead = (real)(measured.x0 - bounds.x0) * terminal_font_scale();
	if (width <= 0.f)
		width = 8.f;
	width *= terminal_font_scale();
	text_left = (short)(window.x0 + padding);
	columns = (long)((window.x1 - window.x0 - 2 * padding) / width);
	terminal_dropdown_columns = columns;
	/* (the prompt's host left out below 1000 pixels wide; the window's
	units are the 480 lines') */
#if !defined(HALO_CONSOLE)
	terminal_prompt_wide = (window.x1 - window.x0) * halo_screen_pixel_scale() >= 1000.f;
#else
	terminal_prompt_wide = FALSE;
#endif

	/* the panel, its top line and its bottom edge */
	terminal_dropdown_fill(window.x0, window.y0, window.x1, panel_bottom, TERMINAL_PANEL_COLOR, TERMINAL_PANEL_PASSES);
	terminal_dropdown_fill(window.x0, panel_top, window.x1, (short)(panel_top + 1), TERMINAL_TOP_LINE_COLOR, 2);
	terminal_dropdown_fill(window.x0, (short)(panel_bottom - 2), window.x1, panel_bottom, TERMINAL_EDGE_COLOR, 2);

	/* the header strip: its name, and the keys it takes */
	header_bottom = (short)(panel_top + 1 + line_height + padding);
	if (header_bottom > window.y0)
	{
		char const *hints;
		boolean hi_res = terminal_font_scale() != 1.f;
		boolean keyboard = TRUE;

#if !defined(HALO_CONSOLE)
		keyboard = halo_keyboard_prompts() != 0;
#else
		keyboard = FALSE;
#endif
		/* (a middle dot where the console's font has one: Hack Bold) */
		if (keyboard)
		{
			hints = hi_res ?
				"Tab complete \xB7 Ctrl+R search \xB7 PgUp/PgDn scroll \xB7 ~ close" :
				"Tab complete - Ctrl+R search - PgUp/PgDn scroll - ~ close";
		}
		else
		{
			hints = "B or Back close";
		}
		terminal_dropdown_fill(window.x0, (short)(panel_top + 1), window.x1, header_bottom, TERMINAL_HEADER_COLOR, 1);
		bounds.y0 = (short)(panel_top + 1 + padding / 2);
		bounds.y1 = (short)(bounds.y0 + line_height);
		bounds.x0 = text_left;
		bounds.x1 = window.x1;
		terminal_dropdown_text(font_tag_index, &bounds, "CONSOLE", 7, &terminal_color_command, FALSE);
		bounds.x0 = (short)MAX(text_left + 10 * width, window.x1 - padding - (real)strlen(hints) * width);
		terminal_dropdown_text(font_tag_index, &bounds, hints, (short)strlen(hints), &terminal_color_note, FALSE);
	}

	/* the prompt line, under a rule: a blue "> ", the input, and a block
	caret (blinking) where the insertion point is; the input scrolled to
	keep the caret on the screen */
	prompt_top = (short)(panel_bottom - 2 - padding - line_height);
	terminal_dropdown_fill(window.x0, (short)(prompt_top - padding / 2 - 1), window.x1,
		(short)(prompt_top - padding / 2), TERMINAL_RULE_COLOR, 2);
	if (open)
	{
		csstrncpy(terminal_dropdown.input, terminal_globals.input_state->result, NUMBEROF(terminal_dropdown.input) - 1);
		terminal_dropdown.input[NUMBEROF(terminal_dropdown.input) - 1] = 0;
		terminal_dropdown.insertion_point = terminal_globals.input_state->edit.insertion_point_index;
	}
	if (prompt_top >= window.y0)
	{
		char prompt[160];
		short marks[4];
		short input_left;
		long input_columns;
		long input_length = (long)strlen(terminal_dropdown.input);
		long first;

		/* (the shell's prompt, or Ctrl+R's search) */
		if (open && terminal_search.active)
		{
			snprintf(prompt, sizeof(prompt), "(%sreverse-i-search)`%s': ", terminal_search.failed ? "failed " : "",
				terminal_search.query);
			marks[0] = marks[1] = marks[2] = 0;
			marks[3] = (short)strlen(prompt);
		}
		else
		{
			terminal_prompt(prompt, sizeof(prompt), marks);
		}
		input_left = (short)(text_left + marks[3] * width);
		input_columns = MAX(columns - marks[3] - 1, 8);
		first = MAX(0, terminal_dropdown.insertion_point - input_columns + 1);
		bounds.y0 = prompt_top;
		bounds.y1 = (short)(prompt_top + line_height);
		bounds.x0 = text_left;
		bounds.x1 = window.x1;
		if (open && terminal_search.active)
		{
			terminal_dropdown_text(font_tag_index, &bounds, prompt, marks[3],
				terminal_search.failed ? &terminal_color_error : &terminal_color_note, FALSE);
		}
		else
		{
			terminal_dropdown_prompt_text(font_tag_index, &bounds, prompt, marks[3], marks, &terminal_color_output,
				width);
		}
		if (open && terminal_globals.insertion_point_visible)
		{
			short caret = (short)(input_left + lead + (terminal_dropdown.insertion_point - first) * width);

			terminal_dropdown_fill(caret, (short)(prompt_top + 1), (short)(caret + width), bounds.y1,
				TERMINAL_EDGE_COLOR, 2);
		}
		bounds.x0 = input_left;
		terminal_dropdown_text(font_tag_index, &bounds, terminal_dropdown.input + first,
			(short)MIN(input_length - first, input_columns), &terminal_color_output, FALSE);
		/* Tab's candidates, under the panel at the word's column */
		if (open && terminal_dropdown.openness >= 1.f)
		{
			terminal_completion_draw(font_tag_index, line_height,
				(short)MAX(window.x0, input_left + (terminal_completion.column - first - 1) * width),
				panel_bottom, width);
		}
	}

	/* the scrollback, newest at the bottom, wrapped to the panel's width,
	between the header and the prompt */
	{
		long rows_to_skip = terminal_scrollback_offset;
		long line;
		short y;
		short top = (short)MAX(header_bottom + padding / 2, window.y0);

		rows_bottom = (short)(prompt_top - padding);
		if (terminal_scrollback_offset)
		{
			static real_argb_color const more_color = { 1.f, 0.157f, 0.588f, 1.f };

			bounds.x0 = text_left;
			bounds.x1 = window.x1;
			bounds.y0 = (short)(rows_bottom - line_height);
			bounds.y1 = rows_bottom;
			if (bounds.y0 >= top)
				terminal_dropdown_text(font_tag_index, &bounds, "^   ^   ^   ^", 14, &more_color, FALSE);
			rows_bottom -= line_height;
		}
		y = rows_bottom;
		bounds.x0 = text_left;
		bounds.x1 = window.x1;
		for (line = 0; line < terminal_scrollback_count && y - line_height >= top; line++)
		{
			long slot = (terminal_scrollback_next - 1 - line + 2 * TERMINAL_SCROLLBACK_LINES) % TERMINAL_SCROLLBACK_LINES;
			short starts[TERMINAL_PRINTF_MAXIMUM_LINE_LENGTH / 8 + 2];
			short count = terminal_wrap(terminal_scrollback[slot].text, terminal_scrollback[slot].tabstop, columns,
				starts, (short)NUMBEROF(starts));
			short row;

			for (row = (short)(count - 1); row >= 0 && y - line_height >= top; row--)
			{
				short end = row + 1 < count ? starts[row + 1] : (short)strlen(terminal_scrollback[slot].text);

				if (rows_to_skip > 0)
				{
					rows_to_skip--;
					continue;
				}
				bounds.y1 = y;
				bounds.y0 = (short)(y - line_height);
				y = bounds.y0;
				if (!starts[row] && terminal_scrollback[slot].marks[3])
				{
					terminal_dropdown_prompt_text(font_tag_index, &bounds, terminal_scrollback[slot].text, end,
						terminal_scrollback[slot].marks, &terminal_scrollback[slot].color, width);
				}
				else
				{
					terminal_dropdown_text(font_tag_index, &bounds, terminal_scrollback[slot].text + starts[row],
						(short)(end - starts[row]), &terminal_scrollback[slot].color, terminal_scrollback[slot].tabstop);
				}
			}
		}
		/* (scrolled no further than the oldest row shown at the top) */
		if (line >= terminal_scrollback_count && rows_to_skip > 0)
			terminal_scrollback_offset -= rows_to_skip;
	}

	return TRUE;
}

/* port: scrolls the drop-down console's scrollback rows back (positive) or
forward; a page is what fits */
static void terminal_scroll(
	long rows)
{
	terminal_scrollback_offset = MAX(terminal_scrollback_offset + rows, 0);
}
#else
/* (no drop-down console: retail's, which has none of these) */
void terminal_echo_command(char const *command)
{
	(void)command;
}

boolean terminal_completion_wanted(void)
{
	return FALSE;
}

void terminal_completion_show(char const **names, short count, short column)
{
	(void)names;
	(void)count;
	(void)column;
}

char const *terminal_completion_cycle(short direction)
{
	(void)direction;
	return NULL;
}

void terminal_completion_clear(void)
{
}

boolean terminal_shell_style(void)
{
	return FALSE;
}

void terminal_shell_output(char const *format, ...)
{
	(void)format;
}

void terminal_shell_note(char const *format, ...)
{
	(void)format;
}

void terminal_shell_error(char const *format, ...)
{
	(void)format;
}

void terminal_scrollback_clear(void)
{
}

short terminal_columns(void)
{
	return 80;
}
#endif

#ifndef HALO_XBOX_CONSOLE
#if !defined(HALO_CONSOLE)
/* port/linux/src/sdl_platform.c's: the desktop's clipboard */
int platform_clipboard_get(char *text, int size);
void platform_clipboard_set(const char *text);
#endif
/* whether the character is part of a word, for Ctrl+Left and Ctrl+Right */
static boolean terminal_word_character(
	char character)
{
	return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
		(character >= '0' && character <= '9') || character == '_' || character == '.';
}

/* port: the console's editing keys besides edit_text's (left, right,
Delete, Backspace and Shift's selection, retail's): Home and End, Ctrl+Left
and Ctrl+Right a word at a time, Ctrl+A all of it, Ctrl+V pastes, Ctrl+C
copies the selection (all of it without one), and PgUp and PgDn scroll the
drop-down console; TRUE when it took the key */
static boolean terminal_edit_key(
	struct edit_text *edit,
	struct key_stroke const *key)
{
	boolean control = TEST_FLAG(key->modifier_flags, 1);
	boolean shift = TEST_FLAG(key->modifier_flags, 0);
	short length = (short)strlen(edit->buffer);
	short start, end;

	switch (key->key_code)
	{
	case _key_home:
	case _key_end:
		if (shift && edit->selection_start_index == NONE)
			edit->selection_start_index = edit->insertion_point_index;
		else if (!shift)
			edit->selection_start_index = NONE;
		edit->insertion_point_index = key->key_code == _key_home ? 0 : length;
		return TRUE;
	case _key_left_arrow:
	case _key_right_arrow:
		if (!control)
			return FALSE;
		if (shift && edit->selection_start_index == NONE)
			edit->selection_start_index = edit->insertion_point_index;
		else if (!shift)
			edit->selection_start_index = NONE;
		start = edit->insertion_point_index;
		if (key->key_code == _key_left_arrow)
		{
			while (start > 0 && !terminal_word_character(edit->buffer[start - 1]))
				start--;
			while (start > 0 && terminal_word_character(edit->buffer[start - 1]))
				start--;
		}
		else
		{
			while (start < length && !terminal_word_character(edit->buffer[start]))
				start++;
			while (start < length && terminal_word_character(edit->buffer[start]))
				start++;
		}
		edit->insertion_point_index = start;
		return TRUE;
#ifdef TERMINAL_DROPDOWN
	case _key_page_up:
		terminal_scroll(10);
		return TRUE;
	case _key_page_down:
		terminal_scroll(-10);
		return TRUE;
#endif
	default:
		break;
	}
	if (!control)
		return FALSE;
	switch (key->key_code)
	{
	case _key_a:
		edit->selection_start_index = 0;
		edit->insertion_point_index = length;
		return TRUE;
#if !defined(HALO_CONSOLE)
	case _key_c:
	{
		char text[TERMINAL_GETS_MAXIMUM_LINE_LENGTH + 1];

		if (!edit_text_get_selection_indices(edit, &start, &end))
		{
			start = 0;
			end = length;
		}
		csstrncpy(text, &edit->buffer[start], end - start);
		text[end - start] = 0;
		if (*text)
			platform_clipboard_set(text);
		return TRUE;
	}
	case _key_v:
	{
		char text[1024];
		char const *from;

		if (!platform_clipboard_get(text, sizeof(text)))
			return TRUE;
		/* (in place of the selection; a pasted line break ends the paste) */
		if (edit_text_get_selection_indices(edit, &start, &end))
		{
			memmove(&edit->buffer[start], &edit->buffer[end], strlen(&edit->buffer[end]) + 1);
			edit->insertion_point_index = start;
			edit->selection_start_index = NONE;
		}
		for (from = text; *from && *from != '\r' && *from != '\n'; from++)
		{
			short at = edit->insertion_point_index;
			char character = *from == '\t' ? ' ' : *from;

			if ((unsigned char)character < ' ' || (short)strlen(edit->buffer) >= edit->maximum_length)
				continue;
			memmove(&edit->buffer[at + 1], &edit->buffer[at], strlen(&edit->buffer[at]) + 1);
			edit->buffer[at] = character;
			edit->insertion_point_index++;
		}
		return TRUE;
	}
#endif
	default:
		/* (no other Ctrl key types its letter) */
		return key->ascii_code != 0 && key->ascii_code != NONE;
	}
}
#endif

static void terminal_remove_line(
	long line_index)
{
	struct output_line_datum *output_line = output_line_get(line_index);

	if (output_line->older_line_index!=NONE)
	{
		output_line_get(output_line->older_line_index)->newer_line_index = output_line->newer_line_index;
	}
	else
	{
		terminal_globals.oldest_output_line_index = output_line->newer_line_index;
	}

	if (output_line->newer_line_index!=NONE)
	{
		output_line_get(output_line->newer_line_index)->older_line_index = output_line->older_line_index;
	}
	else
	{
		terminal_globals.newest_output_line_index = output_line->older_line_index;
	}

	datum_delete(terminal_globals.output_lines, line_index);

	return;
}

static boolean terminal_update_input(
	void)
{
	struct key_stroke key;

	boolean result = FALSE;

	if (terminal_globals.input_state)
	{
		long time = local_time_get();
		
		for (terminal_globals.input_state->key_count = 0;
			input_get_key(&key);
			terminal_globals.last_insertion_point_toggle = time)
		{
#ifdef TERMINAL_DROPDOWN
			/* port: the drop-down console's shell keys, before console.c
			sees the key (terminal_shell_key): one it keeps to itself is
			not passed on */
			if (terminal_shell_key(&terminal_globals.input_state->edit, &key))
			{
				terminal_globals.insertion_point_visible = TRUE;
				continue;
			}
#endif
			if (terminal_globals.input_state->key_count < TERMINAL_GETS_MAXIMUM_KEYS)
			{
				terminal_globals.input_state->keys[terminal_globals.input_state->key_count] = key;
				terminal_globals.input_state->key_count++;
			}

#ifdef TERMINAL_DROPDOWN
			/* port: any key but Tab (and the modifiers) puts Tab's
			candidates away */
			if (key.key_code != _key_tab && key.key_code != _key_shift && key.key_code != _key_control &&
				key.key_code != _key_alt)
			{
				terminal_completion_clear();
			}
#endif
#ifndef HALO_XBOX_CONSOLE
			/* port: the console's own editing keys (terminal_edit_key) */
			if (!terminal_edit_key(&terminal_globals.input_state->edit, &key))
#endif
			edit_text_handle_key(&terminal_globals.input_state->edit, &key);
			terminal_globals.insertion_point_visible = TRUE;
		}

		if (time > terminal_globals.last_insertion_point_toggle + OUTPUT_FADE_TIME)
		{
			terminal_globals.last_insertion_point_toggle = time;
			terminal_globals.insertion_point_visible = terminal_globals.insertion_point_visible==FALSE;
		}
		result = TRUE;
	}

	return result;
}


static void terminal_update_output(
	void)
{
	struct output_line_datum *line;
	long line_index = terminal_globals.newest_output_line_index;
#ifdef HALO_PORT_FRAMES_BETWEEN_TICKS
	/* This runs once a frame, several frames per tick on the native builds
	(port/linux/game/render_interpolation.c): count the line timers in 30 Hz
	ticks of real time, as they counted on the Xbox, not in frames. */
	static real leftover_ticks = 0.f;
	long ticks;

	leftover_ticks += main_get_seconds_elapsed() * TICKS_PER_SECOND;
	ticks = (long)leftover_ticks;
	leftover_ticks -= (real)ticks;
#endif

	while (line_index!=NONE)
	{
		long older_line_index;
		
		line = output_line_get(line_index);
		older_line_index = line->older_line_index;
#ifdef HALO_PORT_FRAMES_BETWEEN_TICKS
		line->timer += ticks;
#else
		line->timer++;
#endif

		if (line->timer>OUTPUT_TOTAL_TIME)
		{
			terminal_remove_line(line_index);
		}
		line_index = older_line_index;
	}

	return;
}

static long terminal_new_line(
	void)
{
	long new_line_index;
	struct output_line_datum *line;

	if (terminal_globals.output_lines->count==OUTPUT_MAXIMUM_LINES)
	{
		terminal_remove_line(terminal_globals.oldest_output_line_index);
	}

	new_line_index = datum_new(terminal_globals.output_lines);
	match_assert("c:\\halo\\SOURCE\\interface\\terminal.c", 122, new_line_index!=NONE);

	line = output_line_get(new_line_index);
	line->newer_line_index = NONE;
	line->older_line_index = terminal_globals.newest_output_line_index;
	terminal_globals.newest_output_line_index = new_line_index;

	if (line->older_line_index!=NONE)
	{
		output_line_get(line->older_line_index)->newer_line_index = new_line_index;
	}
	else
	{
		terminal_globals.oldest_output_line_index = new_line_index;
	}

	return new_line_index;
}
