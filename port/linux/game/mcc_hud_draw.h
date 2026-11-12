#ifndef MCC_HUD_DRAW_H
#define MCC_HUD_DRAW_H

#include "cseries.h"
#include "math/real_math.h"

struct bitmap_data;
struct hud_placement_definition;
struct hud_absolute_placement_definition;
struct icon_hud_element_definition;

/* MCC conversion owns this value in native placement padding. */
#define MCC_HUD_NATIVE_FRAME 0x4D53

/* Neutral for every non-MCC cache. No native or CE tag is rewritten. */
real mcc_hud_canvas_scale(void);
real mcc_hud_bitmap_scale(long bitmap_group_index);
boolean mcc_hud_icon_draw(long bitmap_group_index, struct bitmap_data const *bitmap,
    real_rectangle2d const *clip, rectangle2d *cursor, pixel32 color,
    struct icon_hud_element_definition const *icon, boolean menu);
void mcc_hud_glyphs_dispose(void);
void mcc_hud_weapon_frame_begin(long hud_index);
void mcc_hud_weapon_frame_end(void);
struct hud_absolute_placement_definition const *mcc_hud_child_placement(
    struct hud_absolute_placement_definition const *parent, void const *element,
    struct hud_absolute_placement_definition *storage);

boolean mcc_hud_anchor_point(short anchor, struct hud_placement_definition const *placement,
    struct bitmap_data const *bitmap, real scale, rectangle2d const *window,
    rectangle2d const *viewport, point2d *point);
boolean mcc_hud_anchor_bounds(short anchor, real width, real height, real_rectangle2d *bounds);
boolean mcc_hud_anchor_number(short anchor, real width, real origin, short *cursor);
void mcc_hud_title_bounds(rectangle2d *bounds, short justification, unsigned long flags);
short mcc_terminal_line_height(long font, short height);
boolean mcc_terminal_draw(long font, rectangle2d const *bounds, char const *text);

#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_hud_anchor_bounds(a0, a1, a2, a3) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (boolean)0)
#define mcc_hud_anchor_number(a0, a1, a2, a3) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (boolean)0)
#define mcc_hud_anchor_point(a0, a1, a2, a3, a4, a5, a6) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (void)sizeof(a4), (void)sizeof(a5), (void)sizeof(a6), (boolean)0)
#define mcc_hud_bitmap_scale(a0) ((void)sizeof(a0), (real)1)
#define mcc_hud_canvas_scale() ((real)1)
#define mcc_hud_child_placement(a0, a1, a2) ((void)sizeof(a1), (void)sizeof(a2), (a0))
#define mcc_hud_icon_draw(a0, a1, a2, a3, a4, a5, a6) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (void)sizeof(a4), (void)sizeof(a5), (void)sizeof(a6), (boolean)0)
#define mcc_hud_title_bounds(a0, a1, a2) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)0)
#define mcc_hud_weapon_frame_begin(a0) ((void)sizeof(a0), (void)0)
#define mcc_hud_weapon_frame_end() ((void)0)
#define mcc_terminal_draw(a0, a1, a2) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (boolean)0)
#define mcc_terminal_line_height(a0, a1) ((void)sizeof(a0), (a1))
#endif

#endif
