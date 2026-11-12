#ifndef MCC_DAMAGE_H
#define MCC_DAMAGE_H

struct damage_definition;
union real_vector3d;

/* Updates MCC extension effects using the biased, normalized direction.
 * Returns unit admission; preserves the legacy result outside extensions. */
boolean mcc_damage_acceleration(struct damage_definition const *definition,
    real object_scale, union real_vector3d const *direction, union real_vector3d *acceleration);

#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_damage_acceleration(a0, a1, a2, a3) ((void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (boolean)((a0)->instantaneous_acceleration > _real_epsilon))
#endif

#endif
