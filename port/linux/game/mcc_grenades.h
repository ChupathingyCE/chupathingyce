#ifndef MCC_GRENADES_H
#define MCC_GRENADES_H
#include <stdint.h>

boolean mcc_grenades_active(void);
short mcc_grenades_type_count(void);
short mcc_grenades_get(long unit,short type);
void mcc_grenades_set(long unit,short type,short count);
short mcc_grenades_total(long unit);
short mcc_grenades_next(long unit,short current,short delta);
short mcc_grenades_add(long unit,short type,short count);
/* Clear/reset affect only the independently owned extra-slot state. */
void mcc_grenades_clear(long unit);
void mcc_grenades_reset(void);
void mcc_grenades_initialize_unit(long unit);
boolean mcc_grenades_pickup(long unit,long equipment);
void mcc_grenades_move_to_hand(long unit);
void mcc_grenades_drop_extra(long unit,void (*drop_item)(long,long));
void mcc_grenades_hud(short local_player,long unit);

/* Stable little-endian snapshot: GMC4, version u16=1, record count u16;
 * records are full salted handle u32, slot2 u8, slot3 u8, zero u16.
 * Selection already belongs to the native unit game-state image. */
uint32_t mcc_grenades_snapshot(void *out,uint32_t capacity);
int mcc_grenades_snapshot_validate(void const *in,uint32_t bytes);
int mcc_grenades_restore(void const *in,uint32_t bytes);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_grenades_active() ((boolean)0)
#define mcc_grenades_add(a0, a1, a2) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (short)0)
#define mcc_grenades_clear(a0) ((void)sizeof(a0), (void)0)
#define mcc_grenades_drop_extra(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (void)0)
#define mcc_grenades_get(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (short)0)
#define mcc_grenades_hud(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (void)0)
#define mcc_grenades_initialize_unit(a0) ((void)sizeof(a0), (void)0)
#define mcc_grenades_move_to_hand(a0) ((void)sizeof(a0), (void)0)
#define mcc_grenades_next(a0, a1, a2) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (short)NONE)
#define mcc_grenades_pickup(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (boolean)0)
#define mcc_grenades_reset() ((void)0)
#define mcc_grenades_set(a0, a1, a2) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)0)
#define mcc_grenades_total(a0) ((void)sizeof(a0), (short)0)
#define mcc_grenades_type_count() ((short)0)
#endif

#endif
