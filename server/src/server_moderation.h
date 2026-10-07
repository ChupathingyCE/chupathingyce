/*
SERVER_MODERATION.H

Delta Control in the game (server_moderation.c): the moderator keys the
players' games prove over Delta Peer's moderation capability, the role each
key has (server_roles.h), the actions a moderator's game asks for (run as
their commands), warnings shown in a player's game, and an account of the
control panel bound to a game. The server program's alone (HALO_SERVER);
a player's own hosted game has none of it (OpenCE's kick and ban, the host's
own, stay as they are).
*/

#ifndef SERVER_MODERATION_H
#define SERVER_MODERATION_H

#include "server_roles.h"
#include "server_link.h"

/* each frame: the hooks given to Delta Peer once; the bind requests the
control panel asked for sent */
void server_moderation_update(void);

/* the moderator key a machine's game proved this session (32 bytes): TRUE
if it did */
boolean server_moderation_machine_key(long machine_index, unsigned char *key);

/* a notice in a machine's game (warning: shown as one; title "Warning",
"Kicked", "Banned"; text why): TRUE if its game can show it (it speaks
Delta Peer's moderation) */
boolean server_moderation_notice(long machine_index, boolean warning, char const *title, char const *text);

/* the roles changed (a moderator added or taken out, an account's role, the
site's list): the games of the players they are told again */
void server_moderation_roles_changed(void);

/* (server_commands.c's) a command line run for someone who is not the
console: a moderator's game (via "game") */
boolean server_commands_run_as(char const *text, char const *via, char const *name, int role,
	unsigned int permissions, char *result, long result_size);

/* (server_commands.c's) a machine's first player's number in sv_players
(and name), 0 none; the machine of a player's number, -1 none */
long server_commands_machine_player(long machine_index, char *name, long name_size);
long server_commands_player_machine(long number);

#endif
