/*
VOICE_GAME.H

Voice chat in the game (voice_game.c; docs/delta.md, Voice): the host's
rules and its routing's view of the game, and this copy's player talking
and hearing on their channel.
*/

#ifndef __VOICE_GAME_H
#define __VOICE_GAME_H

#include <stddef.h>

/* the host's rules: a player's host reads config.toml's network.voice_*, a
dedicated server sets its own (voice_game_host_set) */
struct voice_host_rules
{
	boolean on;
	boolean team;
	boolean proximity;
	boolean everyone;
	boolean party;
	boolean lobby;
	/* near, in world units (5 to 100), and the quality (8 to 64 kbps) */
	long range;
	long kbps;
};

/* why a channel can't be talked on now (voice_game_channel_state) */
enum voice_channel_state
{
	_voice_channel_open = 0,
	/* voice is off on this copy (audio.voice_chat) */
	_voice_channel_voice_off,
	/* the game has no voice: its host has none, or this copy isn't in one */
	_voice_channel_no_host,
	/* the host turned the channel off */
	_voice_channel_host_off,
	/* party: not in a party */
	_voice_channel_no_party,
	/* team: a game without teams */
	_voice_channel_no_teams,
	/* the host muted this player */
	_voice_channel_muted,

	NUMBER_OF_VOICE_CHANNEL_STATES
};

/* each frame (main.c) */
void voice_game_update(void);

/* the dedicated server's rules (a player's host reads config.toml's) */
void voice_game_host_set(struct voice_host_rules const *rules);
void voice_game_host_get(struct voice_host_rules *rules);

/* the channel this copy's player talks on (enum delta_voice_channel), and
switching it (written to audio.voice_channel) */
int voice_game_channel(void);
void voice_game_set_channel(int channel);
/* the next channel that can be talked on (the one now if none can) */
int voice_game_next_channel(void);
/* whether a channel can be talked on now (enum voice_channel_state), and a
plain line saying why not */
int voice_game_channel_state(int channel);
char const *voice_game_channel_reason(int state);
/* a channel's name as the menus show it ("TEAM") */
char const *voice_game_channel_name(int channel);

/* the party's tag (DELTA_WIRE_VOICE_PARTY_TAG_SIZE bytes, NULL: no party):
set by the party code; its members in a game hear each other on the party
channel */
void voice_game_set_party(unsigned char const *tag);

/* the party's own path between its members' games (the party code's; NULL:
none). Until there is one, the party channel reaches the members in this
game alone, through its host. With one, a party frame also goes out over it,
and frames its members say in other games come back in through
voice_game_party_heard (member: the party's own number for them, 0 to 15;
never this copy's own; name: theirs, for this copy's mutes, chat's
chat_local_mute(NONE, name): a muted member is never played). These two
are for the party path to come: nothing calls them yet */
struct voice_party_path
{
	void *context;
	int (*send)(void *context, unsigned short sequence, unsigned char const *packet, int length);
};
void voice_game_set_party_path(struct voice_party_path const *path);
void voice_game_party_heard(int member, wchar_t const *name, unsigned short sequence, unsigned char const *packet,
	int length);

/* whether this copy's player talks now, and another machine's (a frame of
it this player heard just now: never one a host only relays, nor a muted
player's) */
boolean voice_game_talking(void);
boolean voice_game_machine_talking(short machine_index);

#endif
