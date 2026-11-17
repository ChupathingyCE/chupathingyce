#ifndef MCC_CAMERA_H
#define MCC_CAMERA_H

boolean mcc_camera_weapon_following(long unit_index);

#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_camera_weapon_following(a0) ((void)sizeof(a0), (boolean)0)
#endif

#endif
