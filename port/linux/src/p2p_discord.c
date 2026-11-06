/*
P2P_DISCORD.C

The game on Discord, through the Discord desktop client's local RPC socket
(a Unix socket, or a named pipe on Windows): its activity
(discord_activity.c builds it) says what the player is doing, from what the
game tells (p2p_discord_set_game: port/linux/game/discord_game.c) and the
public game this machine hosts (p2p_discord_set_join): its invite is the
activity's join secret, so Discord shows Join and Ask to Join on the
profile and a Join card for an invite sent in a chat. A Join (Discord's
ACTIVITY_JOIN, to players of the same application only) joins the invite
as a halo://join link handed to the game does; one that comes before the
game is up (Discord started the game for it) is held until it is. A player
in a public game it joined from a listing (or a Join) shares the game's
invite the same way, under the host's party (p2p.c's join_public). Once
connected, the game registers how Discord starts it for a Join
(discord.register_launch: discord_register.h), so friends whose game is
closed see Join too. An Ask to Join
(ACTIVITY_JOIN_REQUEST) is accepted only with discord.accept_join_requests;
otherwise it is left to Discord.

It goes through the Discord application in discord.application_id
(config.toml; the project's by default), whose name is what Discord shows
as being played. discord.presence chooses how much shows ("full",
"minimal" or "off"), discord.show_server_name and discord.show_join what
of a network game (no join secret with show_join false, presence other
than "full", or a password game); debug.discord_log logs each activity sent.

The protocol: frames of a little-endian opcode and length, then JSON. The
handshake (opcode 0) names the application; commands and events are opcode
1; 2 closes; 3 and 4 are ping and pong. Nothing here waits for Discord (the
p2p thread holds its lock, which the game's threads take): what it does not
take at once waits in a buffer, and a client that lets that fill up is
disconnected. Discord takes about five activities in twenty seconds: a
change waits until SEND_INTERVAL has passed since the last.
*/

#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "p2p_internal.h"
#include "discord_activity.h"
#include "discord_register.h"

#include <stdio.h>
#include <string.h>

enum
{
	RETRY_INTERVAL = 20000,
	SEND_INTERVAL = 4000,
	BUFFER_SIZE = 16384,
	MAXIMUM_CODE_SIZE = 2 * (P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE) + 1,

	_opcode_handshake = 0,
	_opcode_frame,
	_opcode_close,
	_opcode_ping,
	_opcode_pong,
};

static struct
{
	int checked;
	int enabled;
	char application[32];
	int handle;
	int ready;
	unsigned long attempt_time;
	int attempted;
	unsigned long nonce;
	unsigned char input[BUFFER_SIZE];
	int input_size;
	unsigned char output[BUFFER_SIZE];
	int output_size;

	/* what to show; changed marks it for sending, after sent_time */
	struct discord_activity_game game;
	struct discord_activity_options options;
	char join_code[MAXIMUM_CODE_SIZE];
	/* the host's party (hex) for a game joined, whose invite this machine
	shares as a player in it; "" for its own game's */
	char join_party[2 * P2P_IDENTIFIER_SIZE + 1];
	int join_player_count;
	int join_maximum_player_count;
	int changed;
	unsigned long sent_time;
	int sent;

	/* the Discord user signed in to the client (its READY), as told: an id
	of digits, a name of the letters, digits and marks Discord's allow (a
	host logs them, for a player it drops for cheating) */
	char user_id[P2P_DISCORD_ID_SIZE];
	char user_name[P2P_DISCORD_NAME_SIZE];

	/* the launch command registered (once a run, after the first READY:
	register_launch) */
	int register_wanted;
	int registered;
	/* the game told what it is doing (p2p_discord_set_game: its menus are
	up, or a map loaded); a Join that comes before (Discord started the
	game for it) is held until then */
	int game_told;
	char held_join[160];
} discord = { .handle = -1 };

/* the text kept of a Discord user's id or name: of the characters allowed
(the rest left out), no longer than the size (and ended) */
void p2p_discord_sanitize(char *destination, int size, const char *source, int name)
{
	int length = 0;

	for (; source && *source && length < size - 1; source++)
	{
		char character = *source;

		if ((character >= '0' && character <= '9') ||
			(name && ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
				character == '_' || character == '.' || character == '-')))
		{
			destination[length++] = character;
		}
	}
	destination[length] = 0;
}

/* the Discord user signed in, as told (empty if none); under p2p_lock */
void p2p_discord_user(char *id, int id_size, char *name, int name_size)
{
	p2p_discord_sanitize(id, id_size, discord.user_id, 0);
	p2p_discord_sanitize(name, name_size, discord.user_name, 1);
}

static void discord_close(void)
{
	if (discord.handle >= 0)
		posix_discord_close(discord.handle);
	discord.handle = -1;
	discord.ready = 0;
	discord.input_size = 0;
	discord.output_size = 0;
}

/* what can be written now */
static void discord_flush(void)
{
	while (discord.handle >= 0 && discord.output_size > 0)
	{
		int written = posix_discord_write(discord.handle, discord.output, discord.output_size);

		if (written < 0)
		{
			discord_close();
			return;
		}
		if (!written)
			return;
		memmove(discord.output, discord.output + written, (size_t)(discord.output_size - written));
		discord.output_size -= written;
	}
}

static void discord_send(int opcode, const char *json, int size)
{
	unsigned char *frame;

	if (discord.handle < 0 || size > 2048)
		return;
	if (discord.output_size + 8 + size > BUFFER_SIZE)
	{
		platform_log("Internet play: Discord is not responding; disconnected from it");
		discord_close();
		return;
	}
	frame = discord.output + discord.output_size;
	frame[0] = (unsigned char)opcode;
	frame[1] = frame[2] = frame[3] = 0;
	frame[4] = (unsigned char)size;
	frame[5] = (unsigned char)(size >> 8);
	frame[6] = frame[7] = 0;
	memcpy(frame + 8, json, (size_t)size);
	discord.output_size += 8 + size;
	discord_flush();
}

static void send_activity(void)
{
	char json[DISCORD_ACTIVITY_JSON_SIZE];
	char party[2 * P2P_IDENTIFIER_SIZE + 1];
	char secret[sizeof(DISCORD_JOIN_PREFIX) + MAXIMUM_CODE_SIZE];
	struct discord_activity_game game = discord.game;
	int shared = discord.join_code[0] != 0;
	int size;

	p2p_hex(p2p_identifier(), P2P_IDENTIFIER_SIZE, party);
	snprintf(secret, sizeof(secret), DISCORD_JOIN_PREFIX "%s", discord.join_code);
	/* (a joined game's: under its host's party, and only while in it) */
	if (discord.join_party[0])
	{
		snprintf(party, sizeof(party), "%s", discord.join_party);
		shared = shared && discord_activity_player_shares(&game);
	}
	/* (the hosted game's players, where the game told none: its menus) */
	else if (shared && game.maximum_player_count <= 0)
	{
		game.player_count = discord.join_player_count;
		game.maximum_player_count = discord.join_maximum_player_count;
	}
	size = discord_activity_json(json, sizeof(json), ++discord.nonce, (unsigned long)posix_process_id(), &game,
		&discord.options, party, shared ? secret : NULL);
	if (size < 0)
		return;
	if (config_boolean("debug.discord_log"))
	{
		/* (the join code left out: the log is shared in bug reports) */
		char shown[DISCORD_ACTIVITY_JSON_SIZE];
		char *code;

		snprintf(shown, sizeof(shown), "%s", json);
		code = discord.join_code[0] ? strstr(shown, discord.join_code) : NULL;
		if (code)
			memset(code, 'x', strlen(discord.join_code));
		platform_log("Discord: %s", shown);
	}
	discord_send(_opcode_frame, json, size);
}

static void subscribe(const char *event)
{
	char command[128];
	int size = discord_activity_subscribe_json(command, sizeof(command), ++discord.nonce, event);

	if (size > 0)
		discord_send(_opcode_frame, command, size);
}

/* a string field's value in json, without its escapes' backslashes */
static int json_string(const char *json, const char *name, char *value, int size)
{
	char pattern[64];
	const char *start;
	int length = 0;

	snprintf(pattern, sizeof(pattern), "\"%s\"", name);
	start = strstr(json, pattern);
	if (!start)
		return 0;
	start += strlen(pattern);
	while (*start == ' ' || *start == ':')
		start++;
	if (*start++ != '"')
		return 0;
	while (*start && *start != '"' && length < size - 1)
	{
		if (*start == '\\' && start[1])
			start++;
		value[length++] = *start++;
	}
	value[length] = 0;
	return 1;
}

static void frame_received(int opcode, char *json)
{
	char event[64];
	char value[160];

	switch (opcode)
	{
	case _opcode_frame:
		switch (discord_activity_event(json, value, sizeof(value)))
		{
		case DISCORD_EVENT_JOIN:
			/* (Discord started the game for it: held until the game is
			up) */
			if (!discord.game_told)
			{
				platform_log("Internet play: a Join from Discord, held until the game is ready");
				snprintf(discord.held_join, sizeof(discord.held_join), "%s", value);
				return;
			}
			platform_log("Internet play: joining a game from Discord");
			p2p_invite_received(value, 1);
			return;
		case DISCORD_EVENT_JOIN_REQUEST:
			/* (only to a game still open, on the host's word) */
			if (discord.join_code[0] && config_boolean("discord.accept_join_requests"))
			{
				char command[192];
				int size = discord_activity_accept_json(command, sizeof(command), ++discord.nonce, value);

				if (size > 0)
				{
					platform_log("Internet play: accepted an Ask to Join from Discord");
					discord_send(_opcode_frame, command, size);
				}
			}
			return;
		}
		if (!json_string(json, "evt", event, sizeof(event)))
			break;
		if (!strcmp(event, "READY"))
		{
			discord.ready = 1;
			platform_log("Internet play: connected to Discord");
			/* (who is signed in: its user, after the configuration) */
			{
				const char *user = strstr(json, "\"user\"");
				char value[128];

				discord.user_id[0] = 0;
				discord.user_name[0] = 0;
				if (user && json_string(user, "id", value, sizeof(value)))
					p2p_discord_sanitize(discord.user_id, sizeof(discord.user_id), value, 0);
				if (user && json_string(user, "username", value, sizeof(value)))
					p2p_discord_sanitize(discord.user_name, sizeof(discord.user_name), value, 1);
			}
			/* (Join and Ask to Join: the events come only once asked for) */
			subscribe("ACTIVITY_JOIN");
			if (config_boolean("discord.accept_join_requests"))
				subscribe("ACTIVITY_JOIN_REQUEST");
			discord.changed = 1;
			discord.register_wanted = !discord.registered;
		}
		else if (!strcmp(event, "ERROR"))
		{
			char message[256];

			if (json_string(json, "message", message, sizeof(message)))
				platform_log("Internet play: Discord: %s", message);
		}
		break;
	case _opcode_close:
		discord_close();
		break;
	case _opcode_ping:
		discord_send(_opcode_pong, json, (int)strlen(json));
		break;
	}
}

static void discord_read(void)
{
	for (;;)
	{
		int size = posix_discord_read(discord.handle, discord.input + discord.input_size,
			BUFFER_SIZE - 1 - discord.input_size);

		if (size < 0)
		{
			if (discord.ready)
				platform_log("Internet play: disconnected from Discord");
			discord_close();
			return;
		}
		if (size == 0)
			break;
		discord.input_size += size;
		if (discord.input_size >= BUFFER_SIZE - 1)
			break;
	}
	while (discord.input_size >= 8)
	{
		int opcode = discord.input[0] | discord.input[1] << 8;
		int length = discord.input[4] | discord.input[5] << 8 | discord.input[6] << 16;
		char json[BUFFER_SIZE];

		if (length > BUFFER_SIZE - 9)
		{
			discord_close();
			return;
		}
		if (discord.input_size < 8 + length)
			break;
		memcpy(json, discord.input + 8, (size_t)length);
		json[length] = 0;
		memmove(discord.input, discord.input + 8 + length, (size_t)(discord.input_size - 8 - length));
		discord.input_size -= 8 + length;
		frame_received(opcode, json);
		if (discord.handle < 0)
			return;
	}
}

/* registers how Discord starts this executable for a Join of the
application (posix_register_discord_launch), with discord.register_launch,
unless it is an automated run (as p2p_register_url_scheme). It lets go of
the p2p lock while it may wait for a program */
static void register_launch(const char *application)
{
#ifdef HALO_SERVER
	(void)application;
#else
	int result;

	if (!config_boolean("discord.register_launch") || config_real("debug.exit_after") > 0.0 ||
		config_boolean("debug.hidden_window") || config_boolean("debug.null_renderer"))
		return;
	pthread_mutex_unlock(&p2p_lock);
	result = posix_register_discord_launch(application);
	pthread_mutex_lock(&p2p_lock);
	if (result == DISCORD_REGISTER_WRITTEN)
		platform_log("Discord: registered how Discord starts the game for a friend's Join");
	else if (result == DISCORD_REGISTER_LEFT)
		platform_log("Discord: another program is registered for this application's Join; left as it is");
	else if (result == DISCORD_REGISTER_FAILED)
		platform_log("Discord: could not register how Discord starts the game for a Join");
#endif
}

void p2p_discord_update(void)
{
	if (!discord.checked)
	{
		const char *application = config_string("discord.application_id");

		discord.checked = 1;
		/* (an application's id is digits: it goes into a file's path and
		the handshake's JSON) */
		if (*application && strlen(application) < sizeof(discord.application) &&
			strspn(application, "0123456789") == strlen(application))
		{
			strcpy(discord.application, application);
			discord.enabled = 1;
		}
	}
	if (!discord.enabled)
		return;
	if (discord.handle < 0)
	{
		char handshake[128];
		int size;

		/* (unsigned, as the clock wraps) */
		if (discord.attempted && (unsigned int)(p2p_now() - discord.attempt_time) < (unsigned int)RETRY_INTERVAL)
			return;
		discord.attempted = 1;
		discord.attempt_time = p2p_now();
		discord.handle = posix_discord_connect();
		if (discord.handle < 0)
			return;
		size = snprintf(handshake, sizeof(handshake), "{\"v\":1,\"client_id\":\"%s\"}", discord.application);
		discord_send(_opcode_handshake, handshake, size);
		if (discord.handle < 0)
			return;
	}
	discord_flush();
	if (discord.handle < 0)
		return;
	discord_read();
	if (discord.handle >= 0 && discord.register_wanted)
	{
		/* (Discord's discord-<application>, so it shows a friend Join and
		starts the game for it; it lets go of the lock while it may wait) */
		discord.register_wanted = 0;
		discord.registered = 1;
		register_launch(discord.application);
	}
	if (discord.held_join[0] && discord.game_told)
	{
		char held[sizeof(discord.held_join)];

		snprintf(held, sizeof(held), "%s", discord.held_join);
		discord.held_join[0] = 0;
		platform_log("Internet play: joining a game from Discord");
		p2p_invite_received(held, 1);
	}
	if (discord.handle >= 0 && discord.ready && discord.changed &&
		(!discord.sent || (unsigned int)(p2p_now() - discord.sent_time) >= (unsigned int)SEND_INTERVAL))
	{
		discord.changed = 0;
		discord.sent = 1;
		discord.sent_time = p2p_now();
		send_activity();
	}
}

void p2p_discord_set_join(const char *code, const char *party, int player_count, int maximum_player_count)
{
	if (!code)
		code = "";
	if (!party || !code[0])
		party = "";
	if (!code[0])
		player_count = maximum_player_count = 0;
	if (strcmp(code, discord.join_code) || strcmp(party, discord.join_party) ||
		player_count != discord.join_player_count || maximum_player_count != discord.join_maximum_player_count)
	{
		snprintf(discord.join_code, sizeof(discord.join_code), "%s", code);
		snprintf(discord.join_party, sizeof(discord.join_party), "%s", party);
		discord.join_player_count = player_count;
		discord.join_maximum_player_count = maximum_player_count;
		discord.changed = 1;
	}
}

void p2p_discord_set_game(const struct discord_activity_game *game, const struct discord_activity_options *options)
{
	pthread_mutex_lock(&p2p_lock);
	discord.game_told = 1;
	if (memcmp(game, &discord.game, sizeof(*game)) || memcmp(options, &discord.options, sizeof(*options)))
	{
		discord.game = *game;
		discord.options = *options;
		discord.changed = 1;
	}
	pthread_mutex_unlock(&p2p_lock);
}
