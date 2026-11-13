/* port: chester's MCC distributed messages at OpenCE network version 43's
numbers (its build-344, from chester's opence-mcc-support work: the
clean-room record of its wire). Past NUMBER_OF_DISTRIBUTED_MESSAGES: never
in this build's own message table, sent and taken only in an MCC game of a
43-layout session (network_distributed_mcc_session); any other drops them,
as every 43 build does outside MCC. 105 (admission holds) a host of
ours answers (network_distributed_mcc_admission_message); 106 (load
progress) is dropped here; 103 and 104 (package sharing) this build never
sends: a host of ours declines a client's 103 (mcc_share_rules.h) and a
client of ours drops any 104. */
#ifndef MCC_NETWORK_MESSAGES_H
#define MCC_NETWORK_MESSAGES_H
#include "cseries.h"
enum
{
	/* (the MCC kinds' range at 43: 101 and 102 never used) */
	_distributed_message_mcc_first = 97,
	_distributed_message_mcc_last = 106
};
enum
{
	/* MCC-only extra grenade slots; ordinary inventory packets stay exact. */
	_distributed_message_mcc_grenades = 97,
	/* MCC-only campaign gravity and breadcrumb state. */
	_distributed_message_mcc_campaign = 98,
	/* Expanded MCC scenario-name presence, in bounded chunks. */
	_distributed_message_mcc_object_names = 99,
	/* MCC host-authorized speed, ordered on the reliable stream, even paused. */
	_distributed_message_mcc_game_speed = 100,
};
/* (network_distributed.c) whether this machine plays an MCC map in a network
game of a layout that carries MCC's messages (43's, with its 13,200-byte
settings), so that they go out and are taken */
boolean network_distributed_mcc_session(void);
/* ... and whether the network game's layout would carry them, before any map
is loaded: what an MCC map needs to be played in it */
boolean network_distributed_mcc_layout(void);
/* (the host: network_server_message_handler.c, a joined machine's stream,
lobby or game; the client: network_client_message_handler.c, machine_index
NONE) OpenCE's MCC package sharing messages, 103 and 104, which this build
never sends (mcc_share_rules.h): a host declines a 103 (104, "off") when its
game's layout carries MCC's messages, a client notes a 104 and drops it.
TRUE when the message was one of them (taken here, whatever was done) */
boolean network_distributed_mcc_share_message(long machine_index, void const *message, word size);
/* OpenCE's admission hold, 105 (the host: network_server_message_handler.c,
a joined machine's stream): a client checking its files against the MCC
identity in the host's settings asks to be kept meanwhile; the host answers
"ack" (or "refused") and keeps it, as a machine downloading the map is kept
(network_server_manager.c). Its 204 bytes: op, generation, then the identity
(the 128-byte map name field, two 32-byte digests, the map checksum). TRUE
when the message was one (taken here, whatever was done) */
boolean network_distributed_mcc_admission_message(long machine_index, void const *message, word size);
/* whether the machine is in an admission hold, or its short grace after
"ready": not removed for having no player (a game's start doesn't wait on
it; the machine joins the game in progress) */
boolean network_distributed_mcc_admission_holding(long machine_index);
/* (a client, network_test.c's probe alone) a 103 manifest request for the
level to the host, as OpenCE's client sends one: FALSE when the game's layout
has no MCC messages */
boolean network_distributed_mcc_share_request(char const *level);
#endif
