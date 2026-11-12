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
/* SETTINGS' STEAM ACCOUNT chosen: its QR while a link waits, or UNLINK's
question, in a panel at x, y (nothing otherwise) */
void guide_workshop_link_panel(float x, float y, float width, float bottom);
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

#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the Guide's
calls do nothing, their arguments unevaluated */
#define guide_workshop_shown() ((boolean)0)
#define guide_workshop_link_panel(a0, a1, a2, a3) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (void)0)
#define guide_workshop_open() ((void)0)
#define guide_workshop_update(a0) ((void)sizeof(a0), (void)0)
#define guide_workshop_rows() ((short)0)
#define guide_workshop_move(a0) ((void)sizeof(a0), (void)0)
#define guide_workshop_activate() ((void)0)
#define guide_workshop_back() ((boolean)0)
#define guide_workshop_row_click(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (void)0)
#define guide_workshop_option_click(a0) ((void)sizeof(a0), (void)0)
#define guide_workshop_prompt() ((char const *)"")
#define guide_workshop_in_page() ((boolean)0)
#define guide_workshop_render(a0, a1, a2, a3, a4) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (void)sizeof(a4), (void)0)
#endif

#endif

#endif
