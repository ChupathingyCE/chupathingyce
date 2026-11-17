#ifndef MCC_MENU_PREVIEW_H
#define MCC_MENU_PREVIEW_H
struct rasterizer_dynamic_screen_geometry_parameters;
union rectangle2d;
/* Only the MCC display range on the level-picture tag is intercepted. */
short mcc_menu_preview_index(long bitmap_tag, short frame);
boolean mcc_menu_preview_draw(short index, union rectangle2d const *bounds, union rectangle2d const *clip,
    unsigned long color, struct rasterizer_dynamic_screen_geometry_parameters *parameters);
void mcc_menu_preview_clear(void);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): no MCC
picture, their arguments unevaluated */
#define mcc_menu_preview_index(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (short)-1)
#define mcc_menu_preview_draw(a0, a1, a2, a3, a4) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (void)sizeof(a4), (boolean)0)
#define mcc_menu_preview_clear() ((void)0)
#endif
#endif
