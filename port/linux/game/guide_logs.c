/*
GUIDE_LOGS.C

The Delta Guide's rows for a bug report (guide_logs.h). SEND MY LOGS opens
a window over the Guide, as UNLINK STEAM's question does (guide_workshop.c):

	asked     "Send your debug.txt and latest crash report ..." SEND or
	          CANCEL, CANCEL picked: nothing is sent without SEND
	sending   a bar that runs while the site is reached; CLOSE leaves it
	          sending, and its code comes as a toast
	sent      the site's code, to paste in #bug-reports: COPY or DONE
	failed    why, in the site's or our words: TRY AGAIN or CLOSE
	nothing   no debug.txt yet: BACK

The send itself is log_send.c's, the setup screen's too. OPEN LOGS FOLDER
opens the logs folder in the system's file manager (not on Android, nor
under gamescope, the Steam Deck's Game Mode, which has none).
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "guide.h"

#ifdef HALO_GUIDE

#include "../src/ui_overlay.h"
#include "../src/guide_layout.h"
#include "../src/log_send.h"
#include "../src/port_folders.h"
#include "guide_logs.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

const char *platform_folder(enum port_folder folder);
void platform_open_url(char const *url);
void platform_clipboard_set(const char *text);
void platform_log(char const *format, ...);
#if !defined(__ANDROID__)
int updater_gamescope(void);
#endif

enum
{
	_logs_none,
	_logs_asked,
	_logs_sending,
	_logs_sent,
	_logs_failed,
	_logs_nothing,
};

static struct
{
	boolean open;
	/* the button picked: 0 the left (SEND, COPY, TRY AGAIN), 1 the right */
	short option;
	boolean copied;
	/* a send this window started, its answer not shown in it yet (closed
	while sending: told in a toast once it comes) */
	boolean waiting;
	/* why no send began (no debug.txt yet, or the upload couldn't start) */
	char nothing[160];
	boolean nothing_failed;
	float x0, y0, x1, y1;
} logs_view;

/* ---------- the rows */

static boolean folder_offered(void)
{
#if defined(__ANDROID__)
	return FALSE;
#else
	return !updater_gamescope();
#endif
}

short guide_logs_rows(void)
{
	return (short)(folder_offered() ? NUMBER_OF_GUIDE_LOGS_ROWS : 1);
}

char const *guide_logs_row_label(short row)
{
	return row == _guide_logs_row_folder ? "OPEN LOGS FOLDER" : "SEND MY LOGS";
}

char const *guide_logs_row_activate(short row)
{
	if (row == _guide_logs_row_folder)
	{
		char url[3 * 1024 + 16];

		log_send_folder_url(platform_folder(_port_folder_logs), url, (int)sizeof(url));
		platform_open_url(url);
		platform_log("Guide: opened the logs folder");
		return "Opened the logs folder";
	}
	logs_view.open = TRUE;
	logs_view.copied = FALSE;
	logs_view.nothing[0] = 0;
	/* (CANCEL picked first: SEND takes a move) */
	logs_view.option = 1;
	/* (a send still on its way, or its answer unread: shown, not asked) */
	if (log_send_poll(NULL, 0, NULL, 0) == LOG_SEND_SENT)
		logs_view.option = 0;
	return NULL;
}

/* ---------- the window */

static short logs_phase(void)
{
	if (!logs_view.open)
		return _logs_none;
	if (logs_view.nothing[0])
		return _logs_nothing;
	switch (log_send_poll(NULL, 0, NULL, 0))
	{
	case LOG_SEND_SENDING: return _logs_sending;
	case LOG_SEND_SENT: return _logs_sent;
	case LOG_SEND_FAILED: return _logs_failed;
	default: return _logs_asked;
	}
}

/* the phase's two buttons' words (right NULL: one button) */
static void phase_buttons(short phase, char const **left, char const **right)
{
	*left = *right = NULL;
	switch (phase)
	{
	case _logs_asked: *left = "SEND"; *right = "CANCEL"; break;
	case _logs_sending: *left = "CLOSE"; break;
	case _logs_sent: *left = "COPY"; *right = "DONE"; break;
	case _logs_failed: *left = "TRY AGAIN"; *right = "CLOSE"; break;
	case _logs_nothing: *left = "BACK"; break;
	}
}

boolean guide_logs_modal(void)
{
	return logs_view.open;
}

static void logs_close(void)
{
	short phase = logs_phase();

	/* (an answer seen in the window is done with; one still on its way is
	told in a toast) */
	logs_view.waiting = phase == _logs_sending;
	if (phase == _logs_sent || phase == _logs_failed)
		log_send_forget();
	logs_view.open = FALSE;
	logs_view.nothing[0] = 0;
}

static void logs_send(void)
{
	char error[160];
	int started = log_send_start(platform_folder(_port_folder_logs), FALSE, error, (int)sizeof(error));

	logs_view.copied = FALSE;
	/* (none begun: why, as failed when it couldn't start) */
	if (started != LOG_SEND_SENDING)
	{
		snprintf(logs_view.nothing, sizeof(logs_view.nothing), "%s", error);
		logs_view.nothing_failed = started != LOG_SEND_NOTHING;
		logs_view.option = 0;
		return;
	}
	platform_log("Guide: sending the logs");
	logs_view.option = 0;
}

void guide_logs_modal_press(void)
{
	short phase = logs_phase();

	switch (phase)
	{
	case _logs_asked:
		if (logs_view.option == 0)
			logs_send();
		else
			logs_close();
		break;
	case _logs_sending:
	case _logs_nothing:
		logs_close();
		break;
	case _logs_sent:
		if (logs_view.option == 0)
		{
			char code[32];

			log_send_poll(code, (int)sizeof(code), NULL, 0);
			platform_clipboard_set(code);
			logs_view.copied = TRUE;
			/* (DONE next) */
			logs_view.option = 1;
		}
		else
			logs_close();
		break;
	case _logs_failed:
		if (logs_view.option == 0)
		{
			log_send_forget();
			logs_send();
		}
		else
			logs_close();
		break;
	}
}

void guide_logs_modal_back(void)
{
	logs_close();
}

void guide_logs_modal_move(short step)
{
	char const *left, *right;

	phase_buttons(logs_phase(), &left, &right);
	if (right)
		logs_view.option = (short)(step < 0 ? 0 : 1);
}

void guide_logs_modal_click(short index)
{
	logs_view.option = index;
	guide_logs_modal_press();
}

void guide_logs_modal_click_outside(short x, short y)
{
	/* (beside the question: CANCEL; beside a send under way or its code,
	nothing, so that a stray click loses neither) */
	if (logs_phase() == _logs_asked && (x < logs_view.x0 || x >= logs_view.x1 || y < logs_view.y0 || y >= logs_view.y1))
		logs_close();
}

char const *guide_logs_modal_prompt(void)
{
	char const *left, *right;

	phase_buttons(logs_phase(), &left, &right);
	return logs_view.option && right ? right : left;
}

static float measure_font(void *context, float size, char const *text)
{
	return ui_overlay_text_width((int)(intptr_t)context, size, text);
}

static void text_line(int kind, float x, float y, float width, int align, char const *text)
{
	char shown[GUIDE_LAYOUT_LINE_SIZE];

	guide_layout_fit(text, width, ui_style_text_size(kind), measure_font, (void *)(intptr_t)ui_style_text_font(kind), 0,
		shown, sizeof(shown));
	ui_style_text(kind, x, y, align, shown);
}

static void button_draw(float x, float y, float width, float height, boolean on, char const *word,
	void (*target)(float x0, float y0, float x1, float y1, short index), short index)
{
	ui_style_row(x, y, width, height, on);
	ui_overlay_text(UI_FONT_MENU, 10.0f, x + width * 0.5f, y + (height - 10) * 0.5f, UI_ALIGN_CENTER, 0xFFFFFFFF, word);
	if (target)
		target(x, y, x + width, y + height, index);
}

void guide_logs_modal_render(float left, float top, float right, float bottom,
	void (*target)(float x0, float y0, float x1, float y1, short index))
{
	const float padding = 16, gap = 8, title_height = 16, line = 12, button_height = 26;
	short phase = logs_phase();
	char lines[3][GUIDE_LAYOUT_LINE_SIZE], code[32], error[256];
	char const *title, *text, *left_word, *right_word;
	float width = 300, height, x, y, center, extra = 0;
	int count, index;

	if (phase == _logs_none)
		return;
	log_send_poll(code, (int)sizeof(code), error, (int)sizeof(error));
	switch (phase)
	{
	case _logs_asked:
		title = "SEND MY LOGS";
		text = "Send your debug.txt and latest crash report to the Chupathingy team? Your computer's account name and network addresses are removed.";
		break;
	case _logs_sending:
		title = "SEND MY LOGS";
		text = "Sending\xE2\x80\xA6";
		extra = 12 + gap;
		break;
	case _logs_sent:
		title = "SENT";
		text = "Paste this code in #bug-reports with your report.";
		extra = 40 + gap;
		break;
	case _logs_failed:
		title = "THAT DIDN'T SEND";
		text = error;
		break;
	default:
		title = logs_view.nothing_failed ? "THAT DIDN'T SEND" : "NOTHING TO SEND";
		text = logs_view.nothing;
		break;
	}
	phase_buttons(phase, &left_word, &right_word);
	if (width > right - left - 24)
		width = right - left - 24;
	count = guide_layout_wrap(text, width - 2 * padding, ui_style_text_size(UI_TEXT_LINE), measure_font,
		(void *)(intptr_t)ui_style_text_font(UI_TEXT_LINE), 3, lines, NULL);
	height = padding + title_height + gap + (float)count * line + gap + extra + line + gap + button_height + padding;
	x = left + (right - left - width) * 0.5f;
	y = top + (bottom - top - height) * 0.5f;
	center = x + width * 0.5f;
	logs_view.x0 = x;
	logs_view.y0 = y;
	logs_view.x1 = x + width;
	logs_view.y1 = y + height;
	/* (the Guide under it dimmed past reading, the window solid) */
	ui_overlay_rect(-200, 0, 1040, 480, 0, 0x000000E6);
	ui_overlay_rect(x, y, width, height, 10, ui_style_color(UI_COLOR_WINDOW));
	ui_style_panel(x, y, width, height, 1.0f);
	y += padding;
	text_line(UI_TEXT_TITLE, center, y, width - 2 * padding, UI_ALIGN_CENTER, title);
	y += title_height + gap;
	for (index = 0; index < count; index++)
		ui_style_text(UI_TEXT_LINE, center, y + (float)index * line, UI_ALIGN_CENTER, lines[index]);
	y += (float)count * line + gap;
	if (phase == _logs_sending)
	{
		/* (no byte count to show: a band that runs while the site is reached) */
		float bar_x = x + padding, bar_width = width - 2 * padding;
		float share = (float)(system_milliseconds() % 1400) / 1400.0f;
		float band = bar_width * 0.3f, start = bar_x - band + (bar_width + band) * share;
		float from = start < bar_x ? bar_x : start, to = start + band > bar_x + bar_width ? bar_x + bar_width : start + band;

		ui_overlay_rect(bar_x, y, bar_width, 12, 6, ui_style_color(UI_COLOR_FIELD));
		ui_overlay_outline(bar_x, y, bar_width, 12, 6, 0.75f, ui_style_color(UI_COLOR_FIELD_EDGE));
		if (to > from)
		{
			ui_overlay_gradient(from, y, to - from, 12, 6, ui_style_color(UI_COLOR_PROGRESS_TOP),
				ui_style_color(UI_COLOR_PROGRESS_BOTTOM));
		}
		y += extra;
	}
	else if (phase == _logs_sent)
	{
		ui_style_row(x + padding, y, width - 2 * padding, 40, 0);
		ui_overlay_text_glow(UI_FONT_MENU, 24.0f, center, y + 8, UI_ALIGN_CENTER, 0xFFFFFFFF,
			ui_style_color(UI_COLOR_PROMPT_GLOW), code);
		y += extra;
	}
	if (phase == _logs_sent && logs_view.copied)
	{
		ui_overlay_text(ui_style_text_font(UI_TEXT_LABEL), ui_style_text_size(UI_TEXT_LABEL), center, y,
			UI_ALIGN_CENTER, ui_style_color(UI_COLOR_GOOD), "Copied");
	}
	y += line + gap;
	if (right_word)
	{
		float each = (width - 2 * padding - gap) * 0.5f;

		each = each > 120 ? 120 : each;
		button_draw(center - gap * 0.5f - each, y, each, button_height, logs_view.option == 0, left_word, target, 0);
		button_draw(center + gap * 0.5f, y, each, button_height, logs_view.option != 0, right_word, target, 1);
	}
	else
		button_draw(center - 60, y, 120, button_height, TRUE, left_word, target, 0);
}

void guide_logs_update(void)
{
	char code[32], error[256], line[96];
	int state;

	if (!logs_view.waiting || logs_view.open)
		return;
	state = log_send_poll(code, (int)sizeof(code), error, (int)sizeof(error));
	if (state == LOG_SEND_SENDING)
		return;
	logs_view.waiting = FALSE;
	if (state == LOG_SEND_SENT)
	{
		snprintf(line, sizeof(line), "Your code is %s", code);
		guide_toast("LOGS SENT", line);
	}
	else if (state == LOG_SEND_FAILED)
	{
		guide_toast("LOGS NOT SENT", error);
		log_send_forget();
	}
}

#endif
