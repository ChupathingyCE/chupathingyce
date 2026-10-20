/*
PROFILE_NAMES.C

Players by their Delta profile on the scoreboard and in the lobby
(display.profile_names; docs/delta.md, Delta Profile): a player whose copy
shares its profile in a game on Delta (network.share_profile: Delta Peer's
opt-in profile claim) shows by the profile's display name, as the site's
public card has it (browser.c's browser_player_card, asked of the site in
the background). Everyone else shows by their game name, as before: an
OpenCE player, a player who does not share, a split screen player (the
profile is a copy's, not a player's), a profile hidden or locked on the
site. Only what this copy draws changes: the game's own names, and what
OpenCE players see, are untouched.
*/

#if defined(HALO_GAME_BROWSER) && !defined(HALO_CONSOLE_NO_DELTA_PROFILE)

#include "cseries.h"
#include "../src/browser.h"
#include "../src/delta_profile.h"
#include "halo_profile_screen.h"

#include <stdio.h>

int config_boolean(char const *name);
/* delta_peer_game.c's */
int delta_peer_game_on_delta(void);
int delta_peer_player_profile(int player_index, unsigned char *player_id);

enum
{
	PLAYER_ID_SIZE = 16,
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

boolean profile_names_player(
	long player_index,
	wchar_t *name,
	long size)
{
	static char const digits[] = "0123456789abcdef";
	unsigned char player_id[PLAYER_ID_SIZE];
	char hex[2 * PLAYER_ID_SIZE + 1];
	struct delta_profile_card card;
	long index;

	if (!config_boolean("display.profile_names") || !profile_screen_available() || !delta_peer_game_on_delta() ||
		!delta_peer_player_profile((int)player_index, player_id))
	{
		return FALSE;
	}
	for (index = 0; index < PLAYER_ID_SIZE; index++)
	{
		hex[2 * index] = digits[player_id[index] >> 4];
		hex[2 * index + 1] = digits[player_id[index] & 15];
	}
	hex[2 * PLAYER_ID_SIZE] = 0;
	if (!browser_player_card(hex, &card))
		return FALSE;
	wide_from_utf8(card.display[0] ? card.display : card.handle, name, size);
	return name[0] != 0;
}

#endif
