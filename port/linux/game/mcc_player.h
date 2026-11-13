#ifndef MCC_PLAYER_H
#define MCC_PLAYER_H
struct player_action;
struct scenario_starting_profile;
boolean mcc_player_choice_supported(short choice);
boolean mcc_player_action_valid(struct player_action const *action);
void mcc_player_profile_grenades(long unit, struct scenario_starting_profile const *profile, boolean reset);
void mcc_player_postspawn_grenades(long unit, boolean disabled);
void mcc_player_add_equipment(long unit_index, short profile_index, boolean reset);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_player_action_valid(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_player_choice_supported(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_player_postspawn_grenades(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (void)0)
#define mcc_player_profile_grenades(a0, a1, a2) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)0)
#endif

#endif
