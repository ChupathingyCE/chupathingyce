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
up when it closes, display.console_height of the screen tall: a dimmed
background with an edge along its bottom, the scrollback (every line printed
since the game started, up to TERMINAL_SCROLLBACK_LINES, long ones wrapped
at a space), and the prompt line under a rule. PgUp and PgDn, and the mouse
wheel, scroll it; "^ ^ ^" under the scrollback says there is more below.
Errors are drawn red and warnings amber (terminal_scrollback_color). "classic" draws retail's console:
the prompt at the bottom and the last lines above it, fading. The game keeps
running underneath, as retail's console let it (a network game is never
paused). The original Xbox and the consoles keep retail's console. */

enum
{
	TERMINAL_SCROLLBACK_LINES = 512,
	/* the slide, open or shut, in milliseconds */
	TERMINAL_DROPDOWN_SLIDE_TIME = 160,
};

/* port/linux/src/port_config.c's */
double config_real(const char *name);
unsigned long config_changes(void);

static struct
{
	char text[TERMINAL_PRINTF_MAXIMUM_LINE_LENGTH + 1];
	real_argb_color color;
	boolean tabstop;
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
	/* the prompt as it was last drawn, for the slide up after it closed */
	char prompt[288];
	real_argb_color prompt_color;
} terminal_dropdown = { 0.f, 0, "", { 0 } };

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

/* a line's color in the scrollback: an error's red and a warning's amber,
where the game printed them in the plain white or grey */
static void terminal_scrollback_color(
	char const *text,
	real_argb_color const *color,
	boolean command_running,
	real_argb_color *result)
{
	static real_argb_color const error_color = { 1.f, 1.f, 0.35f, 0.3f };
	static real_argb_color const warning_color = { 1.f, 1.f, 0.75f, 0.25f };
	boolean plain = color->red == color->green && color->green == color->blue;

	*result = *color;
	/* (console_warning's lines, pure red, are warnings) */
	if (color->red == 1.f && color->green == 0.f && color->blue == 0.f)
	{
		*result = warning_color;
		return;
	}
	if (!plain)
		return;
	while (*text == ' ' || *text == '#')
		text++;
	/* (what a typed command logs in white is its error: errors.c's lines;
	its answers are the console's grey, or a script's print's green) */
	if ((command_running && color->red == 1.f) ||
		!strncmp(text, "EXCEPTION", 9) || !strncmp(text, "ERROR", 5) || !strncmp(text, "error", 5))
	{
		*result = error_color;
	}
	else if (!strncmp(text, "WARNING", 7) || !strncmp(text, "warning", 7))
	{
		*result = warning_color;
	}
}

static void terminal_scrollback_add(
	struct output_line_datum const *line)
{
	long slot = terminal_scrollback_next;
	char *end;

	csstrncpy(terminal_scrollback[slot].text, line->buffer, NUMBEROF(terminal_scrollback[slot].text) - 1);
	terminal_scrollback[slot].text[NUMBEROF(terminal_scrollback[slot].text) - 1] = 0;
	/* (an error()'s line ends in its own line break) */
	end = terminal_scrollback[slot].text + strlen(terminal_scrollback[slot].text);
	while (end > terminal_scrollback[slot].text && (end[-1] == '\r' || end[-1] == '\n'))
		*--end = 0;
	terminal_scrollback_color(line->buffer, &line->color, terminal_command_running, &terminal_scrollback[slot].color);
	terminal_scrollback[slot].tabstop = line->tabstop;
	terminal_scrollback_next = (slot + 1) % TERMINAL_SCROLLBACK_LINES;
	if (terminal_scrollback_count < TERMINAL_SCROLLBACK_LINES)
		terminal_scrollback_count++;
	/* (a line printed while scrolled back keeps the view where it is) */
	if (terminal_scrollback_offset)
		terminal_scrollback_offset++;
}

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

	csstrncpy(row, text, MIN(length, (short)(NUMBEROF(row) - 1)));
	row[MIN(length, (short)(NUMBEROF(row) - 1))] = 0;
	if (tabstop)
		draw_string_set_tab_stops(terminal_tab_stops, NUMBEROF(terminal_tab_stops));
	draw_string_set_draw_mode(font_tag_index, NONE, 0, 0, color);
	terminal_draw_line(font_tag_index, bounds, row);
	draw_string_set_tab_stops(terminal_tab_stops, 0);
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
	rectangle2d panel, bounds;
	real eased, full_height, shift;
	short padding = (short)MAX(line_height / 2, 2);
	short prompt_top, rows_bottom;
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
		return FALSE;
	}
	eased = terminal_dropdown.openness * terminal_dropdown.openness * (3.f - 2.f * terminal_dropdown.openness);

	offset_rectangle2d(&window, -render.camera.viewport_bounds.x0, -render.camera.viewport_bounds.y0);
	full_height = (window.y1 - window.y0) * terminal_dropdown_height();
	/* (its contents slide with it: laid out as when it is all the way down,
	moved up by what is still hidden) */
	shift = full_height * (eased - 1.f);

	panel = window;
	panel.y1 = (short)(window.y0 + full_height * eased);
	draw_quad(&panel, 0xE0060A12);
	/* (its bottom edge) */
	bounds = panel;
	bounds.y0 = (short)(panel.y1 - 2);
	draw_quad(&bounds, 0xFF2E8BD8);

	/* the prompt line, under a faint rule */
	prompt_top = (short)(window.y0 + full_height + shift - padding - line_height);
	bounds = window;
	bounds.y0 = (short)(prompt_top - padding / 2 - 1);
	bounds.y1 = (short)(prompt_top - padding / 2);
	draw_quad(&bounds, 0x602E8BD8);
	if (open)
	{
		short length;

		csstrcpy(terminal_dropdown.prompt, terminal_globals.input_state->prompt);
		length = (short)strlen(terminal_dropdown.prompt);
		csstrncpy(&terminal_dropdown.prompt[length], terminal_globals.input_state->result,
			NUMBEROF(terminal_dropdown.prompt) - length - 2);
		terminal_dropdown.prompt[NUMBEROF(terminal_dropdown.prompt) - 2] = 0;
		terminal_dropdown.prompt_color = terminal_globals.input_state->color;
		if (terminal_globals.insertion_point_visible)
		{
			short cursor = (short)(length + terminal_globals.input_state->edit.insertion_point_index);

			if (cursor < (short)NUMBEROF(terminal_dropdown.prompt) - 2)
			{
				if (!terminal_dropdown.prompt[cursor])
					terminal_dropdown.prompt[cursor + 1] = 0;
				terminal_dropdown.prompt[cursor] = '_';
			}
		}
	}
	bounds.x0 = (short)(window.x0 + padding);
	bounds.x1 = window.x1;
	bounds.y0 = prompt_top;
	bounds.y1 = (short)(prompt_top + line_height);
	if (bounds.y0 >= window.y0)
	{
		terminal_dropdown_text(font_tag_index, &bounds, terminal_dropdown.prompt,
			(short)strlen(terminal_dropdown.prompt), &terminal_dropdown.prompt_color, FALSE);
	}

	/* the scrollback, newest at the bottom, wrapped to the panel's width */
	{
		rectangle2d measured, cursor;
		real width;
		long rows_to_skip = terminal_scrollback_offset;
		long line;
		short y;

		/* (a character's width, from ten of the widest, as
		terminal_draw_line draws them: terminal_font_scale times as wide) */
		bounds = window;
		draw_string_set_draw_mode(font_tag_index, NONE, 0, 0, global_real_argb_white);
		draw_string_compute_bounds(&bounds, "mmmmmmmmmm", &measured, &cursor);
		width = (measured.x1 - measured.x0) / 10.f;
		if (width <= 0.f)
			width = 8.f;
		width *= terminal_font_scale();
		columns = width > 0.f ? (long)((window.x1 - window.x0 - 2 * padding) / width) : 80;
		rows_bottom = (short)(prompt_top - padding);
		if (terminal_scrollback_offset)
		{
			static real_argb_color const more_color = { 1.f, 0.18f, 0.55f, 0.85f };

			bounds.y0 = (short)(rows_bottom - line_height);
			bounds.y1 = rows_bottom;
			if (bounds.y0 >= window.y0)
				terminal_dropdown_text(font_tag_index, &bounds, "^   ^   ^   ^", 14, &more_color, FALSE);
			rows_bottom -= line_height;
		}
		y = rows_bottom;
		for (line = 0; line < terminal_scrollback_count && y - line_height >= window.y0 + padding / 2; line++)
		{
			long slot = (terminal_scrollback_next - 1 - line + 2 * TERMINAL_SCROLLBACK_LINES) % TERMINAL_SCROLLBACK_LINES;
			short starts[TERMINAL_PRINTF_MAXIMUM_LINE_LENGTH / 8 + 2];
			short count = terminal_wrap(terminal_scrollback[slot].text, terminal_scrollback[slot].tabstop, columns,
				starts, (short)NUMBEROF(starts));
			short row;

			for (row = (short)(count - 1); row >= 0 && y - line_height >= window.y0 + padding / 2; row--)
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
				terminal_dropdown_text(font_tag_index, &bounds, terminal_scrollback[slot].text + starts[row],
					(short)(end - starts[row]), &terminal_scrollback[slot].color, terminal_scrollback[slot].tabstop);
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
			if (terminal_globals.input_state->key_count < TERMINAL_GETS_MAXIMUM_KEYS)
			{
				terminal_globals.input_state->keys[terminal_globals.input_state->key_count] = key;
				terminal_globals.input_state->key_count++;
			}

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
