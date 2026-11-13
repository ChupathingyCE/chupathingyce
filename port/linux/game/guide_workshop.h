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
/* Steam's sign-in window, over the Guide in the middle of its panel (any
section): up while a link waits for the scan (its QR, "Scan with the Steam
app", Waiting, Expired, refreshing, Linked) or for UNLINK's answer. While
it is up it takes the input: A its button with the focus (CANCEL, BACK,
DONE; the question's UNLINK or CANCEL, the focus on CANCEL as it opens),
B back, the d-pad's left and right the question's two buttons, a click
beside the question's window CANCEL. Drawn last, over the prompts; its
buttons are target kind GUIDE_WORKSHOP_TARGET_MODAL (index 0 the button,
1 the question's CANCEL) */
boolean guide_workshop_link_modal(void);
void guide_workshop_link_modal_press(void);
void guide_workshop_link_modal_back(void);
void guide_workshop_link_modal_move(short step);
void guide_workshop_link_modal_click(short index);
void guide_workshop_link_modal_click_outside(short x, short y);
void guide_workshop_link_modal_render(float left, float top, float right, float bottom,
	void (*target)(float x0, float y0, float x1, float y1, short kind, short index));
/* a line for SETTINGS' help line under STEAM ACCOUNT (NULL: none) */
char const *guide_workshop_link_note(void);
/* Steam's list a page on or back (LT and RT, Page Up and Page Down, the
heading's PREV and NEXT, the list's last row): TRUE when it moved, the new
page's first item chosen. Whether the list has pages, and one that way */
boolean guide_workshop_page_step(short step);
boolean guide_workshop_pages(void);
boolean guide_workshop_page_offered(short step);
/* each frame (open or not): the folders watched while the section shows or
an item waits on Steam; a newly installed item's maps join the map lists */
void guide_workshop_update(boolean showing);
/* its rows (the items), the d-pad, A, B (TRUE: taken, an item's page
closed), a click on a row or an option, and A's word */
short guide_workshop_rows(void);
void guide_workshop_move(short step);
/* the d-pad's left and right: Show's, Sort's and the page row's value
stepped (on another row, nothing) */
void guide_workshop_side(short step);
/* a click on a row's arrow (target kind GUIDE_WORKSHOP_TARGET_PAGE, its
index the row's times two, plus 1 for the right one) */
void guide_workshop_arrow_click(short index);
void guide_workshop_activate(void);
boolean guide_workshop_back(void);
void guide_workshop_row_click(short row, boolean touch);
/* the wheel over an item page's description scrolls it (TRUE: taken), a
click on it gives it the focus (TRUE: taken); x and y as the targets are */
boolean guide_workshop_wheel(short x, short y, short steps);
boolean guide_workshop_click(short x, short y);
void guide_workshop_option_click(short option);
char const *guide_workshop_prompt(void);
/* B's word is BACK on an item's page */
boolean guide_workshop_in_page(void);
/* drawn in the panel's body (the menus' 640x480); target tells the Guide
where a row, an option, a page button (index 0 back, 1 on) or the sign-in
window's button is (kind GUIDE_WORKSHOP_TARGET_*), for the pointer */
enum
{
	GUIDE_WORKSHOP_TARGET_ROW,
	GUIDE_WORKSHOP_TARGET_OPTION,
	GUIDE_WORKSHOP_TARGET_PAGE,
	GUIDE_WORKSHOP_TARGET_MODAL,
};
void guide_workshop_render(float left, float top, float right, float bottom,
	void (*target)(float x0, float y0, float x1, float y1, short kind, short index));

#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the Guide's
calls do nothing, their arguments unevaluated */
#define guide_workshop_shown() ((boolean)0)
#define guide_workshop_link_modal() ((boolean)0)
#define guide_workshop_link_modal_press() ((void)0)
#define guide_workshop_link_modal_back() ((void)0)
#define guide_workshop_link_modal_move(a0) ((void)sizeof(a0), (void)0)
#define guide_workshop_link_modal_click(a0) ((void)sizeof(a0), (void)0)
#define guide_workshop_link_modal_click_outside(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (void)0)
#define guide_workshop_link_modal_render(a0, a1, a2, a3, a4) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (void)sizeof(a4), (void)0)
#define guide_workshop_link_note() ((char const *)0)
#define guide_workshop_page_step(a0) ((void)sizeof(a0), (boolean)0)
#define guide_workshop_pages() ((boolean)0)
#define guide_workshop_page_offered(a0) ((void)sizeof(a0), (boolean)0)
#define guide_workshop_open() ((void)0)
#define guide_workshop_update(a0) ((void)sizeof(a0), (void)0)
#define guide_workshop_rows() ((short)0)
#define guide_workshop_move(a0) ((void)sizeof(a0), (void)0)
#define guide_workshop_side(a0) ((void)sizeof(a0), (void)0)
#define guide_workshop_arrow_click(a0) ((void)sizeof(a0), (void)0)
#define guide_workshop_wheel(a0, a1, a2) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (boolean)0)
#define guide_workshop_click(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (boolean)0)
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
