/* MCC particle-system view selection; no persistent state. */
#ifndef MCC_PARTICLES_H
#define MCC_PARTICLES_H
struct object_marker;
short mcc_particle_system_markers(long object_index, short attachment_index,
    unsigned long flags, struct object_marker *markers, short maximum, short world_count);
boolean mcc_particle_system_visible(long object_index, unsigned long flags, short viewer);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_particle_system_markers(a0, a1, a2, a3, a4, a5) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (void)sizeof(a4), (a5))
#define mcc_particle_system_visible(a0, a1, a2) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (boolean)1)
#endif

#endif
