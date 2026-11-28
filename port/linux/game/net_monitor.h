/*
NET_MONITOR.H

The server monitor (debug.network_test "monitor:<listed name>",
tools/server_monitor.py; port/linux/README.md, "Server monitor"): a headless
run joins one game from the game list by its listed name, as a quiet guest
that plays nothing, and writes down how the host behaves for a while: its
tick rate and jitter as its updates arrive, the corrections this machine
makes and how far, the messages and datagrams both ways, the server's chat
lines, the players and the bots, the maps, and the warnings. Every ten
seconds a line of JSON goes to debug.monitor_output; the tool reads them and
writes a report.

The hooks below cost one test each when the monitor is off.
*/

#ifndef __NET_MONITOR_H
#define __NET_MONITOR_H

/* what a correction moved (net_monitor_correction) */
enum
{
	/* this machine's own player, the host pulling its prediction back */
	_net_monitor_own_player,
	/* another player (a person or a bot), smoothed toward the host's word */
	_net_monitor_remote_player,
	/* the vehicle this machine's player drives */
	_net_monitor_own_vehicle,
	/* any other vehicle */
	_net_monitor_remote_vehicle,
	/* any other object in the world: a weapon lying about, a body, a
	grenade */
	_net_monitor_object,
	/* an AI's unit (co-op) */
	_net_monitor_actor,
	/* (the host's: a client's prediction taken) */
	_net_monitor_host,
	NUMBER_OF_NET_MONITOR_CORRECTIONS
};

/* (warnings: net_monitor_warning) */
enum
{
	/* a message kind this build does not know */
	_net_monitor_unknown_kind,
	/* a message stamped too far from the host's clock */
	_net_monitor_out_of_window,
	/* a message older than one of its kind already had (reordered) */
	_net_monitor_stale,
	/* a message too short for its count of entries */
	_net_monitor_malformed,
	/* (dropped without a word) a message while no game runs here (loading,
	the lobby) */
	_net_monitor_not_in_game,
	/* ... one before the host's objects are in, of a kind not taken till then */
	_net_monitor_unsynchronized,
	/* ... one of a kind only a client sends, or only over a stream */
	_net_monitor_wrong_sender,
	/* ... an MCC kind outside an MCC game */
	_net_monitor_mcc,
	/* ... one of another checkpoint epoch, or before the first boundary (a
	layout with an epoch: 44's) */
	_net_monitor_epoch,
	NUMBER_OF_NET_MONITOR_WARNINGS
};

/* (whether a correction is a snap: further than the glide it could have
been drawn as, or, with none, further than a world unit) */
#define net_monitor_snap(distance, blend_distance) \
	((blend_distance) > 0.0f ? !((distance) <= (blend_distance)) : !((distance) <= 1.0f))

/* (a desktop build with the game list: the network tests' and the list's) */
#if !defined(HALO_CONSOLE_NO_NETWORK_TEST) && defined(HALO_GAME_BROWSER)
#define NET_MONITOR_BUILT

/* whether this run is the monitor */
int net_monitor_active(void);
/* a distributed message from the host: its kind and size, whether it came in
a batch (a batch's own size holds its messages'), and whether its tick
(game_time) is a sample of the host's tick rate (a datagram's outermost) */
void net_monitor_message_in(int type, int size, long game_time, int in_batch, int tick_sample);
/* every message of the game's connection the client's handler reads: its
message type, a packet's type (else 0), its size and whether it came over
the stream (else a datagram) */
void net_monitor_game_message(int message_type, int packet_type, int size, int stream);
/* the host's notice (a distributed notice: OpenCE's servers' server
messages), its text */
void net_monitor_notice(char const *text, int length);
/* the header's further bytes a layout's messages carry (44's epoch), taken
off before a message is handled */
void net_monitor_layout_bytes(int size);
void net_monitor_message_out(int type, int size);
/* a correction: what it moved, how far (world units) and whether it was a
snap (net_monitor_snap) */
void net_monitor_correction(int kind, float distance, int snap);
/* the host's word on a player's unit (each entry of a unit states message) */
void net_monitor_unit_state(long player_index);
/* a chat line heard: 0 a player's, 1 the server's (Delta's), 2 a system
line (a notice shown in the chat); its length and, but for a player's, its
text (a player's is never kept) */
void net_monitor_chat(int channel, int length, char const *text);
void net_monitor_warning(int warning, int type, int size);
/* the player's name for this run (debug.monitor_name; an automated network
test's, debug.network_test_name), over the profile's */
void net_monitor_player_name(wchar_t *name, int size);
/* each frame (network_test.c) */
void net_monitor_update(int main_menu_loaded, float seconds);

#else

#define net_monitor_active() 0
#define net_monitor_message_in(type, size, game_time, in_batch, tick_sample) ((void)0)
#define net_monitor_game_message(message_type, packet_type, size, stream) ((void)0)
#define net_monitor_notice(text, length) ((void)0)
#define net_monitor_layout_bytes(size) ((void)0)
#define net_monitor_message_out(type, size) ((void)0)
#define net_monitor_correction(kind, distance, snap) ((void)0)
#define net_monitor_unit_state(player_index) ((void)0)
#define net_monitor_chat(channel, length, text) ((void)0)
#define net_monitor_warning(warning, type, size) ((void)0)
#define net_monitor_player_name(name, size) ((void)0)
#define net_monitor_update(main_menu_loaded, seconds) ((void)0)

#endif

#endif
