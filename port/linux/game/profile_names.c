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
#include "text/unicode.h"
#include "interface/player_ui.h"
#include "networking/network_game_manager.h"
#include "../src/browser.h"
#include "../src/delta_profile.h"
#include "../src/delta_peer.h"
#include "../src/identity.h"
#include "halo_profile_screen.h"

#include <stdio.h>
#include <string.h>

int config_boolean(char const *name);
/* delta_peer_game.c's */
int delta_peer_game_on_delta(void);
int delta_peer_game_player_identity(int machine_index, int local_player, struct delta_peer_identity *identity);
int delta_peer_game_linked_name(int choice, const unsigned char *account, char *name, int size);
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
	int clan_size,
	boolean *looks_like)
{
	static char const digits[] = "0123456789abcdef";
	char hex[2 * PLAYER_ID_SIZE + 1];
	struct delta_profile_card card;
	long index;

	clan_tag[0] = 0;
	*looks_like = FALSE;
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
		/* (a handle the site marked as looking like another's: not shown) */
		*looks_like = card.looks_like[0] != 0;
		return TRUE;
	}
	/* (no card yet, or none: the proof's capitals, checked to be the
	handle's, else the handle, which the certificate names) */
	snprintf(name, (size_t)name_size, "%s", identity->tag[0] ? identity->tag : identity->handle);
	return TRUE;
}

static boolean profile_names_player_unmarked(struct delta_peer_identity const *identity, char const *text,
	char const *clan, wchar_t *name, long size);

/* a player's name masked: "Player 4" (their place in the game's list) */
static boolean masked_name(
	long player_index,
	wchar_t *name,
	long size)
{
	wchar_t text[24];

	usnprintf(text, NUMBEROF(text), L"Player %ld", player_index + 1);
	text[NUMBEROF(text) - 1] = 0;
	fit(text, name, size);
	return TRUE;
}

/* the game's wide name as UTF-8 */
static void utf8_from_name(
	wchar_t const *name,
	long length,
	char *out,
	int size)
{
	int used = 0;
	long index;

	for (index = 0; index < length && name[index] && used < size - 4; index++)
	{
		unsigned int c = (unsigned short)name[index];

		if (c < 0x80)
			out[used++] = (char)c;
		else if (c < 0x800)
		{
			out[used++] = (char)(0xC0 | c >> 6);
			out[used++] = (char)(0x80 | (c & 0x3F));
		}
		else
		{
			out[used++] = (char)(0xE0 | c >> 12);
			out[used++] = (char)(0x80 | (c >> 6 & 0x3F));
			out[used++] = (char)(0x80 | (c & 0x3F));
		}
	}
	out[used] = 0;
}

/* whether an unverified player's game name looks like the handle of a
verified player of this game (compared as look-alikes, as written and as it
shows, with every @ dropped; against the whole handle and against the cut
a linked profile's game name is: identity_impersonates) */
static boolean impersonates(
	struct network_game const *game,
	struct network_player const *player)
{
	char text[64];
	long index;

	utf8_from_name(player->name, NUMBEROF(player->name), text, (int)sizeof(text));
	for (index = 0; index < (long)NUMBEROF(game->players); index++)
	{
		struct network_player const *verified = &game->players[index];
		struct delta_peer_identity identity;

		if (verified == player || !network_player_is_valid(verified) ||
			!delta_peer_game_player_identity(verified->machine_index, verified->controller_index, &identity) ||
			!identity.handle[0])
		{
			continue;
		}
		if (identity_impersonates(text, identity.handle, (int)NUMBEROF(player->name) - 1))
			return TRUE;
	}
	return FALSE;
}

/* an unverified player: masked when their game name looks like a verified
player's handle, else their game name; while verified players carry the
Delta mark (display.verified_mark: the scoreboard's, profile_names_verified),
with every @ taken out (a handle's sign) and no Delta, triangle, space or dot
left at its front (identity_unverified_name), so " @Name" can't pass for a
handle, nor a typed Delta for the mark. FALSE: the game name as it is */
static boolean unverified_name(
	struct network_game const *game,
	long player_index,
	wchar_t *name,
	long size)
{
	struct network_player const *player = &game->players[player_index];
	unsigned short shown[NUMBEROF(player->name) + 1];
	long length, index;

	if (impersonates(game, player))
		return masked_name(player_index, name, size);
	if (!config_boolean("display.verified_mark"))
		return FALSE;
	length = identity_unverified_name((unsigned short const *)player->name, (int)NUMBEROF(player->name), shown,
		(int)NUMBEROF(shown));
	for (index = 0; index < length && player->name[index] == (wchar_t)shown[index]; index++)
		;
	if (index == length && (index == (long)NUMBEROF(player->name) || !player->name[index]))
		return FALSE;
	/* (a name of nothing but marks and dots: by its place) */
	if (!length)
		return masked_name(player_index, name, size);
	for (index = 0; index < length && index < size - 1; index++)
		name[index] = (wchar_t)shown[index];
	name[index] = 0;
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
	boolean looks_like;

	if (!config_boolean("display.profile_names") || !profile_screen_available() || !delta_peer_game_on_delta() ||
		size < 2)
	{
		return FALSE;
	}
	game = current_game();
	if (!game || player_index < 0 || player_index >= (long)NUMBEROF(game->players))
		return FALSE;
	player = &game->players[player_index];
	if (!network_player_is_valid(player))
		return FALSE;
	if (!delta_peer_game_player_identity(player->machine_index, player->controller_index, &identity) ||
		!verified_name(&identity, text, (int)sizeof(text), clan, (int)sizeof(clan), &looks_like))
	{
		return unverified_name(game, player_index, name, size);
	}
	/* (a verified handle the site marked as another's look-alike: masked as
	an impersonation is) */
	if (looks_like)
		return masked_name(player_index, name, size);
	/* (the name alone: the scoreboard draws the Delta mark in its own
	column, profile_names_verified) */
	return profile_names_player_unmarked(&identity, text, clan, name, size);
}

boolean profile_names_verified(
	long player_index)
{
	struct network_game *game;
	struct network_player const *player;
	struct delta_peer_identity identity;
	char text[64], clan[16];
	boolean looks_like;

	if (!config_boolean("display.verified_mark") || !config_boolean("display.profile_names") ||
		!profile_screen_available() || !delta_peer_game_on_delta())
	{
		return FALSE;
	}
	game = current_game();
	if (!game || player_index < 0 || player_index >= (long)NUMBEROF(game->players))
		return FALSE;
	player = &game->players[player_index];
	/* (a verified player shown by their name: not one the site marked as
	another's look-alike, who is masked) */
	return network_player_is_valid(player) &&
		delta_peer_game_player_identity(player->machine_index, player->controller_index, &identity) &&
		verified_name(&identity, text, (int)sizeof(text), clan, (int)sizeof(clan), &looks_like) && !looks_like;
}

/* (profile_names_player's) a verified name and its clan tag into name,
fitted to its column */
static boolean profile_names_player_unmarked(
	struct delta_peer_identity const *identity,
	char const *text,
	char const *clan,
	wchar_t *name,
	long size)
{
	wchar_t whole[64];

	(void)identity;
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

/* a local player's Halo profile's choice of account (delta_account.txt,
read again now and then): 0 a guest, 2 that account (into account), 1 the
copy's own; a player with no saved Halo profile is the copy's own */
static int local_account_choice(
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
	char folder[256];
	unsigned long now = system_milliseconds();

	if (local_player < 0 || local_player >= (int)NUMBEROF(cache))
		return 0;
	/* (in a network game, a local player's index is its controller's) */
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
			if (!fgets(line, sizeof(line), file))
				line[0] = 0;
			fclose(file);
			cache[local_player].choice = identity_account_choice(line, local_player == 0,
				cache[local_player].account);
		}
	}
	if (cache[local_player].choice == 2 && account)
		csmemcpy(account, cache[local_player].account, PLAYER_ID_SIZE);
	return cache[local_player].choice;
}

int profile_names_local_account(
	int local_player,
	unsigned char *account)
{
	struct network_game *game = current_game();
	short own = network_game_client_get_local_machine_index();
	long index;
	boolean playing = FALSE;

	if (!game || own < 0)
		return 0;
	for (index = 0; index < (long)NUMBEROF(game->players) && !playing; index++)
	{
		struct network_player const *player = &game->players[index];

		playing = network_player_is_valid(player) && player->machine_index == own &&
			player->controller_index == local_player;
	}
	return playing ? local_account_choice(local_player, account) : 0;
}

boolean profile_names_local_guest(
	long local_player)
{
	unsigned char account[PLAYER_ID_SIZE];

	return local_account_choice((int)local_player, account) == 0;
}

boolean profile_names_local_game_name(
	long local_player,
	wchar_t *name,
	long size)
{
	unsigned char account[PLAYER_ID_SIZE];
	char text[32];
	long index;
	int choice;

	if (!name || size < 2)
		return FALSE;
	choice = local_account_choice((int)local_player, account);
	if (!choice || !delta_peer_game_linked_name(choice, account, text, (int)sizeof(text)) || !text[0])
		return FALSE;
	/* (a handle is a-z 0-9 _ -, in the capitals its profile picked: plain
	letters a Halo name holds as they are, cut to its length) */
	for (index = 0; index < size - 1 && text[index]; index++)
		name[index] = (wchar_t)(unsigned char)text[index];
	name[index] = 0;
	return TRUE;
}

#endif
