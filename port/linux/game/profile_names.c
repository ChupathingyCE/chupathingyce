/*
PROFILE_NAMES.C

Players by their Delta profile on the scoreboard, in the lobby and postgame
(display.profile_names; docs/delta.md, "Verified names"): a player whose
game proved who they are over Delta Peer (the identity capability: their
device certificate, checked by the host and again by this copy) shows by
their one name: their handle (the Delta Tag, which the certificate names)
in the capitals they picked ("Milenko"), as the site's public card has it
(browser.c's browser_player_card, asked of the site by the proved account,
in the background) or, until it comes, as their proof says it. A name that
is not the handle in other capitals never shows.

Everyone else shows by their game name, as before: an OpenCE player, an
older build's unproven PROFILE claim, a player who does not share their
profile, a guest Halo profile. Only what this copy draws changes: the
game's own names, and what OpenCE players see (the 11-character Halo name),
are untouched. A player who shows their clan tag has it before their name
("[TAG] Name"); a name longer than its column is cut with "...".

It also says which account each of this machine's local players proves as
(profile_names_local_account): each Halo profile keeps its choice in its
own folder (delta_account.txt: "guest", or an account's 32 hex digits);
without one it proves as the copy's own account.
*/

#if defined(HALO_GAME_BROWSER) && !defined(HALO_CONSOLE_NO_DELTA_PROFILE)

#include "cseries.h"
#include "interface/player_ui.h"
#include "networking/network_game_manager.h"
#include "../src/browser.h"
#include "../src/delta_profile.h"
#include "../src/delta_peer.h"
#include "halo_profile_screen.h"

#include <stdio.h>
#include <string.h>

int config_boolean(char const *name);
/* delta_peer_game.c's */
int delta_peer_game_on_delta(void);
int delta_peer_game_player_identity(int machine_index, int local_player, struct delta_peer_identity *identity);
/* the network game's (network_game_globals.h, network_client_manager.h) */
struct network_game_client *global_network_game_client_get(void);
struct network_game *network_game_client_get_game(struct network_game_client *client);
short network_game_client_get_local_machine_index(void);
/* (cseries_windows.h's) */
unsigned long system_milliseconds(void);

enum
{
	PLAYER_ID_SIZE = 16,
	/* a Halo profile's choice is read again this often (milliseconds) */
	ACCOUNT_READ_GAP = 5000,
};

/* UTF-8 to the game's wide text (no characters past the BMP) */
static void wide_from_utf8(
	char const *text,
	wchar_t *name,
	long size)
{
	unsigned char const *cursor = (unsigned char const *)text;
	long used = 0;

	while (*cursor && used < size - 1)
	{
		unsigned int character;

		if (*cursor < 0x80)
			character = *cursor++;
		else if ((*cursor & 0xE0) == 0xC0 && cursor[1])
		{
			character = ((cursor[0] & 0x1Fu) << 6) | (cursor[1] & 0x3Fu);
			cursor += 2;
		}
		else if ((*cursor & 0xF0) == 0xE0 && cursor[1] && cursor[2])
		{
			character = ((cursor[0] & 0x0Fu) << 12) | ((cursor[1] & 0x3Fu) << 6) | (cursor[2] & 0x3Fu);
			cursor += 3;
		}
		else
		{
			character = '?';
			cursor++;
			while ((*cursor & 0xC0) == 0x80)
				cursor++;
		}
		name[used++] = (wchar_t)character;
	}
	name[used] = 0;
}

static long wide_length(
	wchar_t const *text)
{
	long length = 0;

	while (text[length])
		length++;
	return length;
}

/* a character's drawn width, in tenths of a lowercase letter's (the menus'
and the HUD's fonts, measured at 640x480): a column of size - 1 characters
holds a name of as many tenths of ten as its Halo names have characters, so
a name of wide letters ("WWWW") is cut sooner than one of narrow ones */
static long character_width(
	wchar_t character)
{
	if (character == 'M' || character == 'W' || character == 'm' || character == 'w' || character == '@' ||
		character == '%')
	{
		return 17;
	}
	if (character >= 'A' && character <= 'Z')
		return 13;
	if (character == 'i' || character == 'l' || character == 'j' || character == 'I' || character == '.' ||
		character == ',' || character == ':' || character == ';' || character == '\'' || character == '!' ||
		character == '|')
	{
		return 5;
	}
	if (character == ' ' || character == '[' || character == ']' || character == '(' || character == ')')
		return 6;
	if (character == '_' || character == '-')
		return 8;
	return 10;
}

static long text_width(
	wchar_t const *text)
{
	long width = 0;

	while (*text)
		width += character_width(*text++);
	return width;
}

/* text into out (size, its end included), cut with "..." to fit room (in
tenths of a lowercase letter) and size - 1 characters */
static void fit_room(
	wchar_t const *text,
	wchar_t *out,
	long size,
	long room)
{
	long length = wide_length(text), index, used = 0, width = 0;
	long ellipsis = 3 * character_width('.');

	if (size <= 0)
		return;
	if ((length <= size - 1 && text_width(text) <= room) || size < 5)
	{
		for (index = 0; index < length && index < size - 1; index++)
			out[used++] = text[index];
	}
	else
	{
		for (index = 0; index < length && used < size - 4 &&
			width + character_width(text[index]) + ellipsis <= room; index++)
		{
			width += character_width(text[index]);
			out[used++] = text[index];
		}
		while (used > 0 && out[used - 1] == ' ')
			used--;
		out[used++] = '.';
		out[used++] = '.';
		out[used++] = '.';
	}
	out[used] = 0;
}

/* ... its column size - 1 lowercase letters wide */
static void fit(
	wchar_t const *text,
	wchar_t *out,
	long size)
{
	fit_room(text, out, size, (size - 1) * 10);
}

static int hex_digit(
	char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* the network game this copy is in (its client's: a host has one too) */
static struct network_game *current_game(
	void)
{
	struct network_game_client *client = global_network_game_client_get();

	return client ? network_game_client_get_game(client) : NULL;
}

/* whether a name is the handle in other capitals */
static boolean same_name(
	char const *name,
	char const *handle)
{
	long index;

	for (index = 0; name[index] && handle[index]; index++)
	{
		char c = name[index];

		if ((c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c) != handle[index])
			return FALSE;
	}
	return !name[index] && !handle[index];
}

/* the name a verified player shows by: their handle (the Delta Tag) in the
capitals they picked, as the site's card has it, else as their proof says
it, else the handle itself; the clan tag the card shows, if any */
static boolean verified_name(
	struct delta_peer_identity const *identity,
	char *name,
	int name_size,
	char *clan_tag,
	int clan_size)
{
	static char const digits[] = "0123456789abcdef";
	char hex[2 * PLAYER_ID_SIZE + 1];
	struct delta_profile_card card;
	long index;

	clan_tag[0] = 0;
	if (!identity->handle[0])
		return FALSE;
	for (index = 0; index < PLAYER_ID_SIZE; index++)
	{
		hex[2 * index] = digits[identity->account[index] >> 4];
		hex[2 * index + 1] = digits[identity->account[index] & 15];
	}
	hex[2 * PLAYER_ID_SIZE] = 0;
	/* (the site's card: the name in its owner's capitals, and the clan tag
	it shows; a card's name that is not the handle is not shown) */
	if (browser_player_card(hex, &card))
	{
		snprintf(name, (size_t)name_size, "%s", same_name(card.display, identity->handle) ? card.display :
			identity->tag[0] ? identity->tag : identity->handle);
		snprintf(clan_tag, (size_t)clan_size, "%s", card.clan_tag);
		return TRUE;
	}
	/* (no card yet, or none: the proof's capitals, checked to be the
	handle's, else the handle, which the certificate names) */
	snprintf(name, (size_t)name_size, "%s", identity->tag[0] ? identity->tag : identity->handle);
	return TRUE;
}

boolean profile_names_player(
	long player_index,
	wchar_t *name,
	long size)
{
	struct network_game *game;
	struct network_player const *player;
	struct delta_peer_identity identity;
	char text[64], clan[16];
	wchar_t whole[64];

	if (!config_boolean("display.profile_names") || !profile_screen_available() || !delta_peer_game_on_delta() ||
		size < 2)
	{
		return FALSE;
	}
	game = current_game();
	if (!game || player_index < 0 || player_index >= (long)NUMBEROF(game->players))
		return FALSE;
	player = &game->players[player_index];
	if (!network_player_is_valid(player) ||
		!delta_peer_game_player_identity(player->machine_index, player->controller_index, &identity) ||
		!verified_name(&identity, text, (int)sizeof(text), clan, (int)sizeof(clan)))
	{
		return FALSE;
	}
	if (clan[0])
	{
		wchar_t tag[24];
		long used;

		wide_from_utf8(clan, tag, NUMBEROF(tag));
		whole[0] = '[';
		for (used = 1; tag[used - 1] && used < 20; used++)
			whole[used] = tag[used - 1];
		whole[used++] = ']';
		whole[used++] = ' ';
		wide_from_utf8(text, whole + used, NUMBEROF(whole) - used);
		/* (the clan tag kept whole while the name keeps six letters' width:
		"[TAG] Mil..."; else the name alone) */
		if (wide_length(whole) > size - 1 || text_width(whole) > (size - 1) * 10)
		{
			wchar_t prefix[24];
			long index;

			for (index = 0; index < used; index++)
				prefix[index] = whole[index];
			prefix[used] = 0;
			if (size - 1 - used >= 6 && (size - 1) * 10 - text_width(prefix) >= 60)
			{
				wchar_t cut[64];

				fit_room(whole + used, cut, size - used, (size - 1) * 10 - text_width(prefix));
				for (index = 0; cut[index]; index++)
					whole[used + index] = cut[index];
				whole[used + index] = 0;
			}
			else
				wide_from_utf8(text, whole, NUMBEROF(whole));
		}
	}
	else
		wide_from_utf8(text, whole, NUMBEROF(whole));
	fit(whole, name, size);
	return name[0] != 0;
}

int profile_names_local_account(
	int local_player,
	unsigned char *account)
{
	static struct
	{
		char path[256];
		int choice;
		unsigned char account[PLAYER_ID_SIZE];
		unsigned long read_time;
		boolean read;
	} cache[4];
	struct network_game *game = current_game();
	short own = network_game_client_get_local_machine_index();
	char folder[256];
	long index;
	boolean playing = FALSE;
	unsigned long now = system_milliseconds();

	if (!game || own < 0 || local_player < 0 || local_player >= (int)NUMBEROF(cache))
		return 0;
	for (index = 0; index < (long)NUMBEROF(game->players) && !playing; index++)
	{
		struct network_player const *player = &game->players[index];

		playing = network_player_is_valid(player) && player->machine_index == own &&
			player->controller_index == local_player;
	}
	if (!playing)
		return 0;
	/* (in a network game, a local player's index is its controller's; one
	playing with no saved Halo profile proves as the copy's own account) */
	if (!player_ui_get_path_to_local_player_profile_directory((short)local_player, folder))
		return 1;
	if (!cache[local_player].read || strcmp(cache[local_player].path, folder) ||
		now - cache[local_player].read_time >= ACCOUNT_READ_GAP)
	{
		char path[300], line[80];
		FILE *file;
		size_t length = strlen(folder);

		snprintf(cache[local_player].path, sizeof(cache[local_player].path), "%s", folder);
		cache[local_player].read = TRUE;
		cache[local_player].read_time = now;
		cache[local_player].choice = 1;
		snprintf(path, sizeof(path), "%s%sdelta_account.txt", folder,
			length && folder[length - 1] != '\\' && folder[length - 1] != '/' ? "\\" : "");
		file = fopen(path, "r");
		if (file)
		{
			if (fgets(line, sizeof(line), file))
			{
				line[strcspn(line, " \t\r\n")] = 0;
				if (!strcmp(line, "guest"))
					cache[local_player].choice = 0;
				else if (strlen(line) == 2 * PLAYER_ID_SIZE)
				{
					for (index = 0; index < PLAYER_ID_SIZE; index++)
					{
						int high = hex_digit(line[2 * index]), low = hex_digit(line[2 * index + 1]);

						if (high < 0 || low < 0)
							break;
						cache[local_player].account[index] = (unsigned char)(high << 4 | low);
					}
					if (index == PLAYER_ID_SIZE)
						cache[local_player].choice = 2;
				}
			}
			fclose(file);
		}
	}
	if (cache[local_player].choice == 2 && account)
		csmemcpy(account, cache[local_player].account, PLAYER_ID_SIZE);
	return cache[local_player].choice;
}

#endif
