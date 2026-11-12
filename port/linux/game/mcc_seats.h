#ifndef MCC_SEATS_H
#define MCC_SEATS_H

struct unit_datum;
struct player_control_unit_camera_info;
struct unit_datum *mcc_seat_parent(long object_index);
long mcc_seat_controller(long object_index);
boolean mcc_seat_nearby(long player_index, long target_index, short *seat_index);
boolean mcc_seat_action(long player_index, long target_index, short seat_index);
boolean mcc_seat_update(long unit_index);
void mcc_seat_camera(struct player_control_unit_camera_info *info);
boolean mcc_seat_blocks_avoidance(long actor_index);

#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_seat_action(a0, a1, a2) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (boolean)0)
#define mcc_seat_blocks_avoidance(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_seat_camera(a0) ((void)sizeof(a0), (void)0)
#define mcc_seat_nearby(a0, a1, a2) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (boolean)0)
#define mcc_seat_parent(a0) ((void)sizeof(a0), (struct unit_datum *)0)
#define mcc_seat_update(a0) ((void)sizeof(a0), (boolean)0)
#endif

#endif
