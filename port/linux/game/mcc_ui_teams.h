#ifndef MCC_UI_TEAMS_H
#define MCC_UI_TEAMS_H
struct network_game_server;
struct network_game_server_client_machine;
boolean mcc_ui_team_balance_allows(short machine, short controller, short team);
void mcc_ui_teams_reset(void);
void mcc_ui_team_respawn_damage(long player_index, long unit_index, boolean active);
void mcc_ui_team_respawn_death(long *killer, long *object, long dead, boolean *friendly);
boolean mcc_ui_team_request(struct network_game_server *server,
    struct network_game_server_client_machine *machine, word *message, short size);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_ui_team_request(a0, a1, a2, a3) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (boolean)0)
#define mcc_ui_team_respawn_damage(a0, a1, a2) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)0)
#define mcc_ui_team_respawn_death(a0, a1, a2, a3) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (void)0)
#endif

#endif
