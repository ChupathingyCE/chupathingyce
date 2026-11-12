/* port: chester's MCC distributed messages, by GulchCE's numbers, which are
not ours: with HALO_MCC_NETWORK only, which no build defines. Their numbers
overlap ours (network_distributed.h) and lie past NUMBER_OF_DISTRIBUTED_
MESSAGES; giving them numbers of ours comes with moving them to Delta. */
#ifndef MCC_NETWORK_MESSAGES_H
#define MCC_NETWORK_MESSAGES_H
#ifdef HALO_MCC_NETWORK
enum
{
	/* MCC-only extra grenade slots; ordinary inventory packets stay exact.
	(GulchCE's number: off our wire. MCC maps' messages go over Delta when
	they come; until then HALO_MCC_NETWORK stays undefined.) */
	_distributed_message_mcc_grenades = 83,
	/* MCC-only campaign gravity and breadcrumb state. */
	_distributed_message_mcc_campaign = 84,
	/* Expanded MCC scenario-name presence, in bounded chunks. */
	_distributed_message_mcc_object_names = 89,
	/* MCC host-authorized speed, ordered on the reliable stream, even paused. */
	_distributed_message_mcc_game_speed = 90,
};
#endif
#endif
