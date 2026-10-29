/*
NETWORK_SERVER_MESSAGE_HANDLER.H

header included in hcex build.
*/

#ifndef __NETWORK_SERVER_MESSAGE_HANDLER_H
#define __NETWORK_SERVER_MESSAGE_HANDLER_H
#pragma once

/* ---------- headers */

#include "cseries.h"

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

struct network_machine;
struct network_game_server;
struct network_game_server_client_machine;
struct network_message;
struct network_player;
struct transport_address;

/* ---------- prototypes/NETWORK_SERVER_MESSAGE_HANDLER.C */

boolean network_game_server_send_message_to_machine(
	struct network_game_server *server,
	struct network_machine *machine,
	struct network_message *message);
boolean network_game_server_send_message_to_all_machines(
	struct network_game_server *server,
	struct network_message *message);
boolean network_game_server_send_player_joined_info_ingame(
	struct network_game_server *server,
	struct network_player *player);
boolean network_game_server_send_game_data_pregame(
	struct network_game_server *server);
/* the native builds send the game settings record in pieces, and send pregame
changes at most four times a second */
boolean network_game_server_send_game_settings_to_all_machines(
	struct network_game_server *server,
	void const *game,
	long game_size);
boolean network_game_server_flush_game_data_pregame(
	struct network_game_server *server);
/* port: the record to machines it was held back from (network version 35's
longer record, sent by each machine's version once its HELLO or the
handshake's window says which), each frame; whether any machine of the game
still waits for one; whether a machine's players report the damage a static
target deals (network version 34) */
void network_game_server_settings_frame(
	struct network_game_server *server);
boolean network_game_server_settings_owed(
	struct network_game_server *server);
/* (and whether one machine's record waits so: a machine joining the game in
progress is started once it does not) */
boolean network_game_server_settings_waiting(
	struct network_game_server *server,
	struct network_game_server_client_machine *machine);
boolean network_game_server_machine_reports_static_damage(
	long machine_index);
boolean network_game_server_handle_client_message(
	struct network_game_server *server,
	struct network_game_server_client_machine *machine,
	word *message,
	short message_buffer_size);
boolean network_game_server_handle_datagram(
	struct network_game_server *server,
	word *message,
	short datagram_size,
	struct transport_address *source_address);

/* ---------- globals */

/* ---------- public code */

#endif // __NETWORK_SERVER_MESSAGE_HANDLER_H
