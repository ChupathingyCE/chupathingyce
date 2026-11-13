#ifndef MCC_TEXTURE_CACHE_H
#define MCC_TEXTURE_CACHE_H
struct bitmap_data;
void *mcc_texture_cache_get(struct bitmap_data const *bitmap, boolean load);
void mcc_texture_cache_dispose(void);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_texture_cache_get(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (void *)0)
#endif

#endif
