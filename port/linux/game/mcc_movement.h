#ifndef MCC_MOVEMENT_H
#define MCC_MOVEMENT_H
#include "cseries.h"
#include "math/real_math.h"
void mcc_movement_apply(long unit_index, real_vector3d *desired);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_movement_apply(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (void)0)
#endif

#endif
