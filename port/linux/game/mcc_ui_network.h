#ifndef MCC_UI_NETWORK_H
#define MCC_UI_NETWORK_H

struct network_game_server;
boolean mcc_ui_network_defer_map(struct network_game_server *server, char const *name);
boolean mcc_ui_network_map_pending(struct network_game_server *server);
void mcc_ui_network_update_map(struct network_game_server *server);
boolean mcc_ui_restart_network_game(void);
boolean mcc_ui_network_restart_settings(struct network_game_server *server);
boolean mcc_ui_network_restart_pregame(void);
void mcc_ui_network_server_dispose(struct network_game_server *server);

#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_ui_network_defer_map(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (boolean)0)
#define mcc_ui_network_map_pending(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_ui_network_update_map(a0) ((void)sizeof(a0), (void)0)
#define mcc_ui_restart_network_game() ((boolean)0)
#define mcc_ui_network_restart_settings(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_ui_network_restart_pregame() ((boolean)0)
#define mcc_ui_network_server_dispose(a0) ((void)sizeof(a0), (void)0)
#endif

#endif
