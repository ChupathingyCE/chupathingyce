/*
HALT_SCREEN.C

The halt screen in the menus' look (halt_screen.h): the facts on the
Guide's glass panel over the setup screen's backdrop, and a row of ways out
under them. The left and right arrows (or the d-pad, or the left stick)
pick one; Enter, Space or A takes it. A button held as the game stopped
counts only once let go of, so a press meant for the game takes nothing.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "halo_console_features.h"

#ifdef HALO_GUIDE

#include "../src/ui_overlay.h"
#include "../src/guide_layout.h"
#include "../src/guide_input.h"
#include "../src/log_send.h"
#include "../src/launcher.h"
#include "../src/port_folders.h"
#include "halt_screen.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *platform_folder(enum port_folder folder);
void platform_open_url(char const *url);
void platform_clipboard_set(const char *text);
void platform_log(char const *format, ...);
int platform_run_is_unwatched(void);
#if !defined(__ANDROID__)
int updater_gamescope(void);
#endif

enum
{
	_halt_way_restart,
	_halt_way_send,
	_halt_way_folder,
	_halt_way_quit,
	NUMBER_OF_HALT_WAYS
};

static char const *const halt_way_words[NUMBER_OF_HALT_WAYS] =
{
	"RESTART",
	"SEND MY LOGS",
	"OPEN LOG FOLDER",
	"QUIT",
};

static struct
{
	int ways[NUMBER_OF_HALT_WAYS];
	int way_count;
	int selected;
	/* what was held last frame: all of it at first, so that a button held
	as the game stopped is let go of before it counts */
	unsigned int held;
	/* a send this screen began, and the line under the ways */
	int sending;
	char status[192];
	int status_bad;
} halt_screen;

/* ---------- private code */

static int folder_offered(void)
{
#if defined(__ANDROID__)
	return 0;
#else
	return !updater_gamescope();
#endif
}

static float measure_font(void *context, float size, char const *text)
{
	return ui_overlay_text_width((int)(intptr_t)context, size, text);
}

/* (the share of a width text is fitted to: the overlay's measure of the
menus' small face runs a little short of what it draws) */
#define HALT_SCREEN_FIT 0.9f

/* text of a kind on one line, cut to width */
static void text_fitted(int kind, float x, float y, float width, int align, char const *text)
{
	char shown[GUIDE_LAYOUT_LINE_SIZE];

	guide_layout_fit(text, width * HALT_SCREEN_FIT, ui_style_text_size(kind), measure_font, (void *)(intptr_t)ui_style_text_font(kind), 0,
		shown, sizeof(shown));
	ui_style_text(kind, x, y, align, shown);
}

static void status_set(int bad, char const *text)
{
	snprintf(halt_screen.status, sizeof(halt_screen.status), "%s", text);
	halt_screen.status_bad = bad;
}

static void way_take(int way)
{
	switch (way)
	{
	case _halt_way_restart:
#ifdef HALO_LAUNCHER
		platform_log("halt screen: RESTART");
		if (!launcher_restart_to_setup())
			status_set(1, "Couldn't restart. Quit and start the game again.");
#endif
		break;
	case _halt_way_send:
	{
		char error[256];
		int state;

		if (halt_screen.sending)
			break;
		platform_log("halt screen: SEND MY LOGS");
		state = log_send_start(platform_folder(_port_folder_logs), 0, error, (int)sizeof(error));
		if (state == LOG_SEND_SENDING)
		{
			halt_screen.sending = 1;
			status_set(0, "Sending\xE2\x80\xA6");
		}
		else
			status_set(1, error);
		break;
	}
	case _halt_way_folder:
	{
		char url[1200];

		platform_log("halt screen: OPEN LOG FOLDER");
		log_send_folder_url(platform_folder(_port_folder_logs), url, (int)sizeof(url));
		platform_open_url(url);
		break;
	}
	case _halt_way_quit:
		platform_log("halt screen: QUIT");
		exit(EXIT_SUCCESS);
		break;
	}
}

static void input_read(void)
{
	unsigned int held = platform_guide_held();
	unsigned int pressed = held & ~halt_screen.held;

	halt_screen.held = held;
	if (pressed & GUIDE_INPUT_LEFT)
		halt_screen.selected = (halt_screen.selected + halt_screen.way_count - 1) % halt_screen.way_count;
	if (pressed & GUIDE_INPUT_RIGHT)
		halt_screen.selected = (halt_screen.selected + 1) % halt_screen.way_count;
	if (pressed & GUIDE_INPUT_A)
		way_take(halt_screen.ways[halt_screen.selected]);
}

static void send_poll(void)
{
	char code[32], error[256], line[192];
	int state;

	if (!halt_screen.sending)
		return;
	state = log_send_poll(code, (int)sizeof(code), error, (int)sizeof(error));
	if (state == LOG_SEND_SENDING)
		return;
	halt_screen.sending = 0;
	if (state == LOG_SEND_SENT)
	{
		/* (on the clipboard too, to paste with a report) */
		platform_clipboard_set(code);
		snprintf(line, sizeof(line), "Sent. Your code is %s (copied).", code);
		status_set(0, line);
	}
	else
		status_set(1, error[0] ? error : "That didn't send.");
	log_send_forget();
}

/* the log box's rows: each line wrapped (at most HALT_SCREEN_LINE_ROWS
rows, its own rows indented), the oldest of the plain lines left out first
when they are more than HALT_SCREEN_LOG_ROWS, then the oldest of all */
enum
{
	HALT_SCREEN_LINE_ROWS = 3,
	HALT_SCREEN_LOG_ROWS = 14,
};
static char log_rows[HALT_SCREEN_LOG_ROWS][GUIDE_LAYOUT_LINE_SIZE];
static char log_row_important[HALT_SCREEN_LOG_ROWS];
static char log_row_continued[HALT_SCREEN_LOG_ROWS];
static int log_row_count;

static int log_rows_layout(struct halt_facts const *facts, float width)
{
	static char wrapped[HALT_FACTS_LOG_LINES][HALT_SCREEN_LINE_ROWS][GUIDE_LAYOUT_LINE_SIZE];
	int counts[HALT_FACTS_LOG_LINES], shown[HALT_FACTS_LOG_LINES];
	int index, total = 0, row;

	for (index = 0; index < facts->line_count; index++)
	{
		int kind = facts->important[index] ? UI_TEXT_WARNING : UI_TEXT_STATUS;

		counts[index] = guide_layout_wrap(facts->lines[index], width * HALT_SCREEN_FIT, ui_style_text_size(kind),
			measure_font, (void *)(intptr_t)ui_style_text_font(kind), HALT_SCREEN_LINE_ROWS, wrapped[index], NULL);
		shown[index] = 1;
		total += counts[index];
	}
	for (index = 0; index < facts->line_count && total > HALT_SCREEN_LOG_ROWS; index++)
	{
		if (!facts->important[index])
			shown[index] = 0, total -= counts[index];
	}
	for (index = 0; index < facts->line_count && total > HALT_SCREEN_LOG_ROWS; index++)
	{
		if (shown[index])
			shown[index] = 0, total -= counts[index];
	}
	log_row_count = 0;
	for (index = 0; index < facts->line_count; index++)
	{
		if (!shown[index])
			continue;
		for (row = 0; row < counts[index] && log_row_count < HALT_SCREEN_LOG_ROWS; row++)
		{
			snprintf(log_rows[log_row_count], sizeof(log_rows[0]), "%s", wrapped[index][row]);
			log_row_important[log_row_count] = facts->important[index];
			log_row_continued[log_row_count] = (char)(row > 0);
			log_row_count++;
		}
	}
	return log_row_count;
}

/* ---------- public code */

int halt_screen_shown(void)
{
	if (!ui_overlay_available() || platform_run_is_unwatched())
		return 0;
	halt_screen.way_count = 0;
#ifdef HALO_LAUNCHER
	halt_screen.ways[halt_screen.way_count++] = _halt_way_restart;
#endif
	halt_screen.ways[halt_screen.way_count++] = _halt_way_send;
	if (folder_offered())
		halt_screen.ways[halt_screen.way_count++] = _halt_way_folder;
	halt_screen.ways[halt_screen.way_count++] = _halt_way_quit;
	halt_screen.selected = 0;
	halt_screen.held = ~0u;
	/* (the first player's controller and keys are the screen's, as the
	Guide takes them) */
	platform_guide_capture(1);
	return 1;
}

void halt_screen_frame(struct halt_facts const *facts)
{
	const float left = 40, right = 600, padding = 20;
	const float inner_left = left + padding, inner_width = right - left - 2 * padding;
	const float label_width = 52, line_height = 12, button_height = 26, gap = 8;
	char lines[2][GUIDE_LAYOUT_LINE_SIZE];
	float y, top, height, each, box_height;
	int count, index, rows;

	input_read();
	send_poll();
	ui_style_clock(system_milliseconds());

	count = guide_layout_wrap(facts->reason, inner_width * HALT_SCREEN_FIT, ui_style_text_size(UI_TEXT_NAME),
		measure_font, (void *)(intptr_t)ui_style_text_font(UI_TEXT_NAME), 2, lines, NULL);
	rows = log_rows_layout(facts, inner_width - 16);
	box_height = (float)(rows ? rows : 1) * line_height + 12;
	/* (the panel as tall as what is in it, in the middle of the screen) */
	height = padding + 26 + (float)count * 14 + (facts->place[0] ? line_height : 0) + gap + 3 * 13 + gap + 13 +
		box_height + 14 + button_height + (halt_screen.status[0] ? 10 + line_height : 0) + padding;
	top = (480 - height) * 0.5f;

	/* the setup screen's backdrop, past the 640x480 layout on a wide
	window, and the Guide's panel on it */
	ui_overlay_gradient(-640, 0, 1920, 240, 0, ui_style_color(UI_COLOR_BACKDROP_TOP),
		ui_style_color(UI_COLOR_BACKDROP_MIDDLE));
	ui_overlay_gradient(-640, 240, 1920, 240, 0, ui_style_color(UI_COLOR_BACKDROP_MIDDLE),
		ui_style_color(UI_COLOR_BACKDROP_BOTTOM));
	ui_style_panel(left, top, right - left, height, 1.0f);

	y = top + padding;
	ui_style_text(UI_TEXT_TITLE, inner_left, y, UI_ALIGN_LEFT, "The game stopped");
	y += 26;

	/* what stopped it, and where */
	for (index = 0; index < count; index++)
	{
		ui_style_text(UI_TEXT_NAME, inner_left, y, UI_ALIGN_LEFT, lines[index]);
		y += 14;
	}
	if (facts->place[0])
	{
		text_fitted(UI_TEXT_STATUS, inner_left, y, inner_width, UI_ALIGN_LEFT, facts->place);
		y += line_height;
	}
	y += gap;

	/* the map, the mode and the build */
	ui_style_text(UI_TEXT_LABEL, inner_left, y, UI_ALIGN_LEFT, "Map");
	text_fitted(UI_TEXT_LINE, inner_left + label_width, y, inner_width - label_width, UI_ALIGN_LEFT,
		facts->map[0] ? facts->map : "None");
	y += 13;
	ui_style_text(UI_TEXT_LABEL, inner_left, y, UI_ALIGN_LEFT, "Mode");
	text_fitted(UI_TEXT_LINE, inner_left + label_width, y, inner_width - label_width, UI_ALIGN_LEFT, facts->mode);
	y += 13;
	ui_style_text(UI_TEXT_LABEL, inner_left, y, UI_ALIGN_LEFT, "Build");
	text_fitted(UI_TEXT_LINE, inner_left + label_width, y, inner_width - label_width, UI_ALIGN_LEFT, facts->build);
	y += 13 + gap;

	/* the log's last lines */
	ui_style_text(UI_TEXT_HEADING, inner_left, y, UI_ALIGN_LEFT, "LOG");
	y += 13;
	ui_style_glass(inner_left, y, inner_width, box_height, 6, ui_style_color(UI_COLOR_PANE),
		ui_style_color(UI_COLOR_ROW_EDGE));
	for (index = 0; index < log_row_count; index++)
	{
		ui_style_text(log_row_important[index] ? UI_TEXT_WARNING : UI_TEXT_STATUS,
			inner_left + 8 + (log_row_continued[index] ? 12 : 0), y + 6 + (float)index * line_height, UI_ALIGN_LEFT,
			log_rows[index]);
	}
	if (!rows)
		ui_style_text(UI_TEXT_STATUS, inner_left + 8, y + 6, UI_ALIGN_LEFT, "Nothing in debug.txt yet.");
	y += box_height + 14;

	/* the ways out */
	each = (inner_width - gap * (float)(halt_screen.way_count - 1)) / (float)halt_screen.way_count;
	for (index = 0; index < halt_screen.way_count; index++)
	{
		float x = inner_left + (float)index * (each + gap);

		ui_style_row(x, y, each, button_height, index == halt_screen.selected);
		ui_overlay_text(UI_FONT_MENU, 10.0f, x + each * 0.5f, y + (button_height - 10) * 0.5f, UI_ALIGN_CENTER,
			0xFFFFFFFF, halt_way_words[halt_screen.ways[index]]);
	}
	y += button_height + 10;
	if (halt_screen.status[0])
	{
		text_fitted(halt_screen.status_bad ? UI_TEXT_WARNING : UI_TEXT_LINE, (left + right) * 0.5f, y, inner_width,
			UI_ALIGN_CENTER, halt_screen.status);
	}
}

#endif
