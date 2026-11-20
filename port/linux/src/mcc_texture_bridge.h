/* MCC immutable textures have identities independent of guest physical memory. */
#ifndef MCC_TEXTURE_BRIDGE_H
#define MCC_TEXTURE_BRIDGE_H

void *mcc_texture_bridge_find(void const *bitmap);
void mcc_texture_bridge_meter(void const *bitmap);
void *mcc_texture_bridge_register(void const *bitmap, unsigned long format,
    unsigned long size, void const *pixels, unsigned long bytes);
void *mcc_texture_bridge_register_linear(void const *bitmap, unsigned long format,
    unsigned long width, unsigned long height, unsigned long pitch, void const *pixels, unsigned long bytes);
void mcc_texture_bridge_dispose(void);
/* Returns estimated GPU bytes charged, or zero when skipped/unsuccessful. */
unsigned long mcc_texture_bridge_prepare(void const *bitmap, unsigned long budget);
/* each registered texture asks again which high-res HUD texture stands for
it (hud_hires_mcc_asset), as the map's stock sheets are found */
void mcc_texture_bridge_hud_refresh(void);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h) */
#define mcc_texture_bridge_hud_refresh() ((void)0)
#endif

#endif
