#ifndef MCC_GAME_SPEED_H
#define MCC_GAME_SPEED_H
#include "cseries.h"

void mcc_game_speed_reset(void);
boolean mcc_game_speed_global(word designator);
void mcc_game_speed_set(real value);
real mcc_game_speed_client(real original);
unsigned long mcc_game_speed_clock(unsigned long now);
void mcc_game_speed_frame(void);
void mcc_game_speed_receive(void const *entries, short count);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_game_speed_client(a0) ((a0))
#define mcc_game_speed_clock(a0) ((a0))
#define mcc_game_speed_frame() ((void)0)
#define mcc_game_speed_global(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_game_speed_set(a0) ((void)sizeof(a0), (void)0)
#endif

#endif
