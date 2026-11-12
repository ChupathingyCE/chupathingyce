/*
GUIDE_WORKSHOP.H

The Guide's WORKSHOP section (guide_workshop.c), between RECENT and SERVICE
RECORD: Halo: MCC's Steam Workshop items for Halo CE, shown only on a
computer with MCC from Steam (workshop_browser.h).
*/

#ifndef GUIDE_WORKSHOP_H
#define GUIDE_WORKSHOP_H

#ifdef HALO_GUIDE

/* whether the section shows: MCC is on this computer */
boolean guide_workshop_shown(void);
/* the section came into view */
void guide_workshop_open(void);
/* each frame (open or not): the folders watched while the section shows or
an item waits on Steam; a newly installed item's maps join the map lists */
void guide_workshop_update(boolean showing);
/* its rows (the items), the d-pad, A, B (TRUE: taken, an item's page
closed), a click on a row or an option, and A's word */
short guide_workshop_rows(void);
void guide_workshop_move(short step);
void guide_workshop_activate(void);
boolean guide_workshop_back(void);
void guide_workshop_row_click(short row, boolean touch);
void guide_workshop_option_click(short option);
char const *guide_workshop_prompt(void);
/* B's word is BACK on an item's page */
boolean guide_workshop_in_page(void);
/* drawn in the panel's body (the menus' 640x480); target tells the Guide
where a row (kind 0) or an option (kind 1) is, for the pointer */
void guide_workshop_render(float left, float top, float right, float bottom,
	void (*target)(float x0, float y0, float x1, float y1, short kind, short index));

#endif

#endif
