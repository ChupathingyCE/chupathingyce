#ifndef MCC_UI_TEAMS_H
#define MCC_UI_TEAMS_H
struct network_game_server;
struct network_game_server_client_machine;
boolean mcc_ui_team_balance_allows(short machine, short controller, short team);
boolean mcc_ui_team_request(struct network_game_server *server,
    struct network_game_server_client_machine *machine, word *message, short size);
#endif
