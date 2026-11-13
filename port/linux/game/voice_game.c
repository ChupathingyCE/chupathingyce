/*
VOICE_GAME.C

Voice chat in the game (voice_game.h; docs/delta.md, Voice), over Delta
Peer: the host's rules and its view of the game for routing, and this
copy's player talking and hearing.

The host (a player's, or a dedicated server) decides who hears whom. Each
frame it tells Delta Peer its rules (config.toml's network.voice_*, a
dedicated server's own) and where each machine's first player is: in a game
or the lobby, their team, alive or not, and where they stand. Delta Peer
relays each frame to the machines its rules allow, never decoding it.

A player talks on one channel at a time (audio.voice_channel):
- PARTY: their party, wherever its members' players are (the party code
  gives its tag: voice_game_set_party);
- TEAM: their team in the game (everyone in co-op);
- PROXIMITY: anyone near them in the game, heard from where they stand
  through the game's 3D sound;
- EVERYONE: everyone in the game, only when the host allows it.
In the lobby and after a game, everyone hears everyone if the host allows
it, whatever the channel. A channel the host turned off can't be talked
on, and the menus show it dimmed with the reason (voice_game_channel_state).
The player hears every channel the host allows, each at its own volume
(audio.voice_volume_*; 0 turns one off, and the host then sends none of it).

Keys: controls.push_to_talk (V) talks while held (audio.voice_chat =
"push_to_talk"), controls.voice_channel (B) switches the channel; a
controller holds the D-pad right to talk and presses the D-pad left to
switch. A player muted in the chat log or the Guide's Players (chat.c's
local mute) isn't heard; a player the host muted (a moderator's, chat's mute)
isn't relayed.

Voice is off unless the player turns it on (audio.voice_chat): retail had
none. Nothing is ever recorded.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "voice_game.h"

#ifdef HALO_PORT_VOICE

#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "input/input.h"
#include "interface/ui_widget.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_client_manager.h"
#include "objects/objects.h"
#include "halo_keyboard.h"
#include "network_coop.h"
#include "chat.h"
#include "../src/delta_peer.h"
#include "../src/delta_chat_game.h"
#ifdef HALO_PORT_VOICE_AUDIO
#include "../src/voice.h"
#endif
#ifdef HALO_GUIDE
#include "guide.h"
#endif

#include <string.h>

int config_boolean(char const *name);
long config_integer(char const *name);
char const *config_string(char const *name);
int config_write(char const *name, char const *value);
unsigned long config_changes(void);
void platform_log(char const *format, ...);

enum
{
	MACHINES = DELTA_PEER_MAXIMUM_MACHINES,
	/* a party member heard over the party's path is a speaker past the
	game's machines */
	PARTY_SPEAKERS = 1000,
	PARTY_MEMBERS = 16,
	/* a speaker's place kept this long after their last frame (ms) */
	PLACE_TIME = 2000
};

static struct
{
	/* the dedicated server's rules (else config.toml's) */
	boolean host_set;
	struct voice_host_rules host;
	/* the party's tag (none: all 0), and its own path (none: NULL) */
	struct voice_party_path const *party_path;
	boolean has_party;
	unsigned char party[DELTA_WIRE_VOICE_PARTY_TAG_SIZE];
	/* this copy's: the channel, the controls, and who was heard */
	boolean ready;
	unsigned long settings_read;
	int channel;
	unsigned long actions_held;
	boolean dpad_left_held;
	int role;
	signed char heard_player[MACHINES];
	unsigned long heard_time[MACHINES];
} voice_game;

/* ---------- the host's */

static long clamp_long(long value, long minimum, long maximum)
{
	return value < minimum ? minimum : value > maximum ? maximum : value;
}

void voice_game_host_set(struct voice_host_rules const *rules)
{
	voice_game.host_set = TRUE;
	voice_game.host = *rules;
	voice_game.host.range = clamp_long(rules->range, 5, 100);
	voice_game.host.kbps = clamp_long(rules->kbps, DELTA_WIRE_VOICE_MINIMUM_KBPS, DELTA_WIRE_VOICE_MAXIMUM_KBPS);
}

void voice_game_host_get(struct voice_host_rules *rules)
{
	if (voice_game.host_set)
	{
		*rules = voice_game.host;
		return;
	}
	rules->on = config_boolean("network.voice") ? TRUE : FALSE;
	rules->team = config_boolean("network.voice_team") ? TRUE : FALSE;
	rules->proximity = config_boolean("network.voice_proximity") ? TRUE : FALSE;
	rules->everyone = config_boolean("network.voice_everyone") ? TRUE : FALSE;
	rules->party = config_boolean("network.voice_party") ? TRUE : FALSE;
	rules->lobby = config_boolean("network.voice_lobby") ? TRUE : FALSE;
	rules->range = clamp_long(config_integer("network.voice_range"), 5, 100);
	rules->kbps = clamp_long(config_integer("network.voice_kbps"), DELTA_WIRE_VOICE_MINIMUM_KBPS,
		DELTA_WIRE_VOICE_MAXIMUM_KBPS);
}

/* the host's rules as VOICE_CONFIG carries them: FALSE with voice off */
static boolean host_wire_rules(struct delta_wire_voice_config *config)
{
	struct voice_host_rules rules;

	voice_game_host_get(&rules);
	csmemset(config, 0, sizeof(*config));
	if (!rules.on)
		return FALSE;
	config->channels = (unsigned char)((rules.team ? DELTA_VOICE_TEAM : 0) |
		(rules.proximity ? DELTA_VOICE_PROXIMITY : 0) | (rules.everyone ? DELTA_VOICE_EVERYONE : 0) |
		(rules.party ? DELTA_VOICE_PARTY : 0) | (rules.lobby ? DELTA_VOICE_LOBBY : 0));
	config->kbps = (unsigned char)rules.kbps;
	config->proximity = (unsigned short)(rules.range * 10);
	return TRUE;
}

/* whether a game is played now (not the lobby or its postgame) */
static boolean voice_in_game(void)
{
	/* (the pregame lobby has the menus' map running, not a game engine) */
	return game_in_progress() && game_engine_running() && !game_engine_showing_postgame();
}

/* a player of the game's list's unit's place: FALSE if it has none (dead,
or no game) */
static boolean player_place(long list_index, real_point3d *place)
{
	struct data_iterator iterator;
	struct player_datum *player;

	if (list_index < 0 || !game_in_progress())
		return FALSE;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (!player->quit_out_of_game && player->network_player_data.player_list_index == list_index)
		{
			if (player->unit_index == NONE)
				return FALSE;
			object_get_origin(player->unit_index, place);
			return TRUE;
		}
	}
	return FALSE;
}

/* (the host, each frame) where each machine's first player is */
static void host_world(void)
{
	static struct delta_peer_voice_world world;
	struct network_game *game = network_game_get_game();
	long index;

	csmemset(&world, 0, sizeof(world));
	world.in_game = voice_in_game() ? 1 : 0;
	world.coop = network_coop_active() ? 1 : 0;
	world.teams = game && game->variant.universal_variant.teams ? 1 : 0;
	for (index = 0; game && index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
	{
		struct network_player *player = (struct network_player *)&game->players[index];
		struct delta_peer_voice_member *member;
		real_point3d place;
		long machine = player->machine_index;

		if (!network_player_is_valid(player) || machine < 0 || machine >= MACHINES || world.members[machine].present)
			continue;
		member = &world.members[machine];
		member->present = 1;
		member->player = (unsigned char)index;
		member->team = (unsigned char)player->team_index;
		if (world.in_game && player_place(index, &place))
		{
			member->alive = 1;
			member->position[0] = place.x;
			member->position[1] = place.y;
			member->position[2] = place.z;
		}
	}
	delta_peer_game_set_voice_world(&world);
}

/* ---------- this copy's */

#ifdef HALO_PORT_VOICE_AUDIO

static char const *const channel_settings[NUMBER_OF_DELTA_VOICE_CHANNELS] =
{
	"", "team", "proximity", "everyone", "party"
};

char const *voice_game_channel_name(int channel)
{
	switch (channel)
	{
	case _delta_voice_channel_party: return "PARTY";
	case _delta_voice_channel_team: return "TEAM";
	case _delta_voice_channel_proximity: return "PROXIMITY";
	case _delta_voice_channel_everyone: return "EVERYONE";
	default: return "";
	}
}

static int channel_from_setting(char const *text)
{
	int channel;

	for (channel = 1; channel < NUMBER_OF_DELTA_VOICE_CHANNELS; channel++)
	{
		if (text && !csstrcasecmp(text, channel_settings[channel]))
			return channel;
	}
	return _delta_voice_channel_team;
}

int voice_game_channel(void)
{
	if (!voice_game.ready || config_changes() != voice_game.settings_read)
	{
		voice_game.ready = TRUE;
		voice_game.settings_read = config_changes();
		voice_game.channel = channel_from_setting(config_string("audio.voice_channel"));
	}
	return voice_game.channel;
}

void voice_game_set_channel(int channel)
{
	if (channel <= _delta_voice_channel_none || channel >= NUMBER_OF_DELTA_VOICE_CHANNELS)
		return;
	voice_game.channel = channel;
	config_write("audio.voice_channel", channel_settings[channel]);
	voice_game.settings_read = config_changes();
}

void voice_game_set_party(unsigned char const *tag)
{
	voice_game.has_party = FALSE;
	csmemset(voice_game.party, 0, sizeof(voice_game.party));
	if (tag)
	{
		int index;

		for (index = 0; index < DELTA_WIRE_VOICE_PARTY_TAG_SIZE; index++)
			voice_game.has_party |= tag[index] != 0;
		if (voice_game.has_party)
			csmemcpy(voice_game.party, tag, sizeof(voice_game.party));
	}
}

void voice_game_set_party_path(struct voice_party_path const *path)
{
	voice_game.party_path = path;
}

void voice_game_party_heard(int member, wchar_t const *name, unsigned short sequence, unsigned char const *packet,
	int length)
{
	/* (a member this copy muted is never played: no name, no way to tell,
	so nothing played either) */
	if (member < 0 || member >= PARTY_MEMBERS || !name || !name[0] || chat_local_muted(NONE, name) || !packet ||
		length <= 0 || length > VOICE_PACKET_SIZE ||
		voice_game_channel_state(_delta_voice_channel_party) == _voice_channel_voice_off)
	{
		return;
	}
	voice_heard(system_milliseconds(), PARTY_SPEAKERS + member, -1, _delta_voice_route_global,
		_delta_voice_channel_party, sequence, packet, length);
}

int voice_game_channel_state(int channel)
{
	struct delta_wire_voice_config config;
	struct network_game *game = network_game_get_game();
	int bit;

	if (voice_mode() == _voice_mode_off)
		return _voice_channel_voice_off;
	/* (the party's own path needs no host) */
	if (channel == _delta_voice_channel_party && voice_game.has_party && voice_game.party_path)
		return _voice_channel_open;
	if (!delta_peer_game_voice_config(&config))
		return _voice_channel_no_host;
	if (config.flags & DELTA_VOICE_CONFIG_MUTED)
		return _voice_channel_muted;
	switch (channel)
	{
	case _delta_voice_channel_party:
		if (!voice_game.has_party)
			return _voice_channel_no_party;
		return config.channels & DELTA_VOICE_PARTY ? _voice_channel_open : _voice_channel_host_off;
	case _delta_voice_channel_team: bit = DELTA_VOICE_TEAM; break;
	case _delta_voice_channel_proximity: bit = DELTA_VOICE_PROXIMITY; break;
	case _delta_voice_channel_everyone: bit = DELTA_VOICE_EVERYONE; break;
	default: return _voice_channel_host_off;
	}
	/* (outside a game the lobby's rule decides, whatever the channel) */
	if (!voice_in_game())
		return config.channels & DELTA_VOICE_LOBBY ? _voice_channel_open : _voice_channel_host_off;
	if (!(config.channels & bit))
		return _voice_channel_host_off;
	if (channel == _delta_voice_channel_team && !network_coop_active() &&
		!(game && game->variant.universal_variant.teams))
	{
		return _voice_channel_no_teams;
	}
	return _voice_channel_open;
}

char const *voice_game_channel_reason(int state)
{
	switch (state)
	{
	case _voice_channel_voice_off: return "Voice chat is off in your settings.";
	case _voice_channel_no_host: return "This game has no voice chat.";
	case _voice_channel_host_off: return "The host turned this channel off.";
	case _voice_channel_no_party: return "You're not in a party.";
	case _voice_channel_no_teams: return "This game has no teams.";
	case _voice_channel_muted: return "The host muted you.";
	default: return "";
	}
}

int voice_game_next_channel(void)
{
	/* (the order the menus show them) */
	static int const order[] =
	{
		_delta_voice_channel_party, _delta_voice_channel_team, _delta_voice_channel_proximity,
		_delta_voice_channel_everyone
	};
	int now = voice_game_channel(), place = 0, step;

	for (step = 0; step < (int)NUMBEROF(order); step++)
	{
		if (order[step] == now)
			place = step;
	}
	for (step = 1; step <= (int)NUMBEROF(order); step++)
	{
		int channel = order[(place + step) % NUMBEROF(order)];

		if (voice_game_channel_state(channel) == _voice_channel_open)
			return channel;
	}
	return now;
}

boolean voice_game_talking(void)
{
	return voice_talking(system_milliseconds()) ? TRUE : FALSE;
}

/* a frame heard: played unless its player is muted here */
static void heard(void *context, struct delta_wire_voice_down const *down, unsigned char const *packet)
{
	struct network_game *game = network_game_get_game();
	short machine = down->speaker_machine;
	long player = down->speaker_player;

	(void)context;
	if (machine < 0 || machine >= MACHINES)
		return;
	if (game && player >= 0 && player < HALO_PORT_MAXIMUM_NETWORK_PLAYERS ?
		chat_local_muted(machine, game->players[player].name) : chat_local_muted(machine, NULL))
	{
		return;
	}
	voice_game.heard_player[machine] = (signed char)(player < HALO_PORT_MAXIMUM_NETWORK_PLAYERS ? player : -1);
	voice_game.heard_time[machine] = system_milliseconds();
	voice_heard(system_milliseconds(), machine, (int)player, down->route, down->channel, down->sequence, packet,
		down->length);
}

static struct delta_peer_voice_client const voice_client = { NULL, heard };

/* push to talk held, and the channel switch pressed, by the first player's
keyboard or controller; none while a menu, the Guide or a chat line has
the input */
static void controls(boolean *push, boolean *next)
{
	unsigned long held = halo_keyboard_actions(0);
	unsigned long pressed = held & ~voice_game.actions_held;
	struct gamepad_state const *pad;
	boolean left = FALSE;

	voice_game.actions_held = held;
	*push = (held & (1UL << HALO_KEYBOARD_PUSH_TO_TALK)) != 0;
	*next = (pressed & (1UL << HALO_KEYBOARD_VOICE_CHANNEL)) != 0;
	if (input_has_gamepad(0) && (pad = input_get_gamepad_state(0)) != NULL)
	{
		*push = *push || pad->buttons[_gamepad_binary_button_dpad_right] > 0;
		left = pad->buttons[_gamepad_binary_button_dpad_left] > 0;
	}
	*next = *next || (left && !voice_game.dpad_left_held);
	voice_game.dpad_left_held = left;
	if (ui_widgets_take_input()
#ifdef HALO_GUIDE
		|| guide_takes_player()
#endif
		)
	{
		*push = FALSE;
		*next = FALSE;
	}
}

/* each frame: talking, hearing, and where the voices heard are */
static void local_update(int role)
{
	struct delta_wire_voice_config config;
	unsigned long now = system_milliseconds();
	boolean push, next, configured;
	int channel, hears;
	struct voice_frame frame;
	short machine;

	/* (out of a game, only the party's own path has voice) */
	if (!role && !(voice_game.party_path && voice_game.has_party))
	{
		if (voice_game.role)
			voice_reset();
		voice_game.role = 0;
		voice_update(now, FALSE, FALSE, 24);
		return;
	}
	if (!role && voice_game.role)
		voice_reset();
	voice_game.role = role;
	delta_peer_game_set_voice_client(&voice_client);
	controls(&push, &next);
	channel = voice_game_channel();
	if (next)
	{
		int changed = voice_game_next_channel();

		if (changed != channel)
		{
			voice_game_set_channel(changed);
			channel = changed;
		}
	}
	configured = delta_peer_game_voice_config(&config) != 0;
	voice_update(now, push, voice_game_channel_state(channel) == _voice_channel_open, configured ? config.kbps : 24);
	while (voice_take_frame(&frame))
	{
		delta_peer_game_voice_say(channel, frame.sequence, frame.data, frame.length);
		/* (a party frame to the members in other games too) */
		if (channel == _delta_voice_channel_party && voice_game.party_path && voice_game.party_path->send)
			voice_game.party_path->send(voice_game.party_path->context, frame.sequence, frame.data, frame.length);
	}
	hears = voice_hears();
	if (!voice_game.has_party)
		hears &= ~DELTA_VOICE_PARTY;
	delta_peer_game_set_voice_hears(hears, voice_game.has_party ? voice_game.party : NULL);
	/* (each voice heard placed where its player is) */
	for (machine = 0; machine < MACHINES; machine++)
	{
		real_point3d place;

		if (!voice_game.heard_time[machine] || now - voice_game.heard_time[machine] > PLACE_TIME)
			continue;
		if (player_place(voice_game.heard_player[machine], &place))
		{
			float position[3];

			position[0] = place.x;
			position[1] = place.y;
			position[2] = place.z;
			voice_place(machine, position, configured ? config.proximity / 10.0f : 15.0f);
		}
		else
			voice_place(machine, NULL, 0.0f);
	}
}

#else

/* (a dedicated server: it relays, and talks on no channel) */
int voice_game_channel(void) { return _delta_voice_channel_none; }
void voice_game_set_channel(int channel) { (void)channel; }
int voice_game_next_channel(void) { return _delta_voice_channel_none; }
int voice_game_channel_state(int channel) { (void)channel; return _voice_channel_no_host; }
char const *voice_game_channel_reason(int state) { (void)state; return ""; }
char const *voice_game_channel_name(int channel) { (void)channel; return ""; }
void voice_game_set_party(unsigned char const *tag) { (void)tag; }
void voice_game_set_party_path(struct voice_party_path const *path) { (void)path; }
void voice_game_party_heard(int member, wchar_t const *name, unsigned short sequence, unsigned char const *packet,
	int length)
{
	(void)member;
	(void)name;
	(void)sequence;
	(void)packet;
	(void)length;
}
boolean voice_game_talking(void) { return FALSE; }

#endif

boolean voice_game_machine_talking(short machine_index)
{
#ifdef HALO_PORT_VOICE_AUDIO
	if (machine_index == network_game_client_get_local_machine_index())
		return voice_game_talking();
	/* (only a voice this player hears: a frame routed to it and not muted
	here. On a host, a frame it only relays to others, an enemy team's,
	never shows) */
	return voice_speaker_talking(system_milliseconds(), machine_index) ? TRUE : FALSE;
#else
	(void)machine_index;
	return FALSE;
#endif
}

void voice_game_update(void)
{
	struct delta_wire_voice_config rules;
	int role = delta_peer_game_role();

	/* (the host's rules, offered to the machines that say HELLO from now) */
	delta_peer_game_set_voice_rules(host_wire_rules(&rules) ? &rules : NULL);
	if (role == 2)
		host_world();
#ifdef HALO_PORT_VOICE_AUDIO
	local_update(role);
#endif
}

#else

void voice_game_update(void)
{
}

#endif
