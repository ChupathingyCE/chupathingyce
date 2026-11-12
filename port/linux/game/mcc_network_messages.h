/* port: chester's MCC distributed messages at OpenCE network version 43's
numbers (its build-344, from chester's opence-mcc-support work: the
clean-room record of its wire). Past NUMBER_OF_DISTRIBUTED_MESSAGES: never
in this build's own message table, sent and taken only in an MCC game of a
43-layout session (network_distributed_mcc_session); any other drops them,
as every 43 build does outside MCC. 103 to 106 (package sharing, admission
holds, load progress) are dropped here for now. */
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
#endif
