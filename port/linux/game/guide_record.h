/*
GUIDE_RECORD.H

DELTA's SERVICE RECORD section (guide_record.c), which the Guide
(guide.c) shows between RECENT and SETTINGS.
*/

#ifndef GUIDE_RECORD_H
#define GUIDE_RECORD_H

#ifdef HALO_GUIDE

/* the section came into view: the record read again, the site asked */
void guide_record_open(void);
/* its rows (one: the HUD's medals), A on the row, and A's word */
short guide_record_rows(void);
void guide_record_activate(void);
char const *guide_record_prompt(void);
/* drawn in the panel's body (the menus' 640x480), the row lit when
selected */
void guide_record_render(float left, float top, float right, float bottom, boolean selected);

#endif

#endif
