#ifndef MCC_SPRITES_H
#define MCC_SPRITES_H
struct build_sprite_data;
int mcc_sprites_continue(struct build_sprite_data *data);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_sprites_continue(a0) ((void)sizeof(a0), (int)0)
#endif

#endif
