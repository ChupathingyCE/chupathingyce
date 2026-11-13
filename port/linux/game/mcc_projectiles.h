#ifndef MCC_PROJECTILES_H
#define MCC_PROJECTILES_H

#include "math/real_math.h"

/* Returns FALSE without changing outputs when the original firing path owns
 * the shot. Preview queries must not record aim-assist state. */
boolean mcc_projectile_ray(long weapon_index, long owner, short trigger_index, boolean firing,
    real_point3d *origin, real_vector3d *forward, real *velocity, long *target_index);

#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_projectile_ray(a0, a1, a2, a3, a4, a5, a6, a7) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (void)sizeof(a4), (void)sizeof(a5), (void)sizeof(a6), (void)sizeof(a7), (boolean)0)
#endif

#endif
