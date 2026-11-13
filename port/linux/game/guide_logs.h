/*
GUIDE_LOGS.H

The Delta Guide's SETTINGS rows for a bug report (guide_logs.c): SEND MY
LOGS, a window over the Guide that asks first (SEND or CANCEL, CANCEL
picked), then sends the logs (log_send.c, as the setup screen does) and
shows the site's code to paste in #bug-reports with COPY; and OPEN LOGS
FOLDER on the desktops.
*/

#ifndef __GUIDE_LOGS_H
#define __GUIDE_LOGS_H

#ifdef HALO_GUIDE

enum
{
	_guide_logs_row_send,
	_guide_logs_row_folder,

	NUMBER_OF_GUIDE_LOGS_ROWS
};

/* the rows this build shows (OPEN LOGS FOLDER only where a file manager
opens it), and a row's label */
short guide_logs_rows(void);
char const *guide_logs_row_label(short row);
/* a row's A: SEND MY LOGS' window, or the folder opened (a message for the
Guide's line when it can't be: NULL otherwise) */
char const *guide_logs_row_activate(short row);

/* the window: up, its buttons, and drawn over the Guide in the panel's
rect (target: its buttons for the pointer, index 0 the left one) */
boolean guide_logs_modal(void);
void guide_logs_modal_press(void);
void guide_logs_modal_back(void);
void guide_logs_modal_move(short step);
void guide_logs_modal_click(short index);
void guide_logs_modal_click_outside(short x, short y);
char const *guide_logs_modal_prompt(void);
void guide_logs_modal_render(float left, float top, float right, float bottom,
	void (*target)(float x0, float y0, float x1, float y1, short index));
/* each frame, the Guide open or not: a send that ends with its window
closed tells its code in a toast */
void guide_logs_update(void);

#endif

#endif
