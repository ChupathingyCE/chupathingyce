#ifndef MCC_OBJECTS_H
#define MCC_OBJECTS_H

struct scenario_object_datum;
struct powered_mass_point_datum;
union real_rgb_color;
/* Called only for the boat controller's three-point scratch array. */
void mcc_boat_controls_initialize(struct powered_mass_point_datum *state);
boolean mcc_vehicle_placement_rules(void);
boolean mcc_item_small_scale(real scale);
boolean mcc_vehicle_placement_allowed(struct scenario_object_datum const *placement);
boolean mcc_object_color_extrapolate(union real_rgb_color *result, unsigned long flags,
    union real_rgb_color const *lower, union real_rgb_color const *upper, real fraction);
/* (port/linux/game/mcc_first_person.inl, in first_person_weapons.c) */
void mcc_first_person_weapon_deleted(long weapon_index);

#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_boat_controls_initialize(a0) ((void)sizeof(a0), (void)0)
#define mcc_first_person_weapon_deleted(a0) ((void)sizeof(a0), (void)0)
#define mcc_item_small_scale(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_object_color_extrapolate(a0, a1, a2, a3, a4) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (void)sizeof(a4), (boolean)0)
#define mcc_vehicle_placement_allowed(a0) ((void)sizeof(a0), (boolean)1)
#define mcc_vehicle_placement_rules() ((boolean)0)
#endif

#endif
