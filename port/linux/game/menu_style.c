/*
MENU_STYLE.C

The Chupathingy menus (display.menus = "chupathingy"): the Xbox's menus in
their own look, with more shown where the Xbox's screens have room. Their
main menu and settings are menu files (port/assets/menus/chupathingy); this
adds to the Xbox's screens themselves:

  - The gametype picker's cards (the Xbox's three) show each gametype's main
    rules under its description, in the card's own text.
*/

#include "cseries.h"
#include "game/game_engine.h"
#include "saved games/playlist_profile.h"

#include <stdio.h>
#include <string.h>

boolean pc_menus_chupathingy(void);

static char const *const weapon_set_names[] =
{
	"Normal", "Pistols", "Rifles", "Plasma", "Snipers", "No sniping", "Rockets", "Shotguns", "Short range",
	"Human", "No grenades", "Covenant", "Classic", "Heavy",
};

/* (a line of ASCII onto the text, after a line break, within its size) */
static void line_add(
	wchar_t *text,
	long size,
	char const *line)
{
	long used = 0;

	while (used < size && text[used])
		used++;
	if (used + 2 >= size)
		return;
	text[used++] = L'\r';
	text[used++] = L'\n';
	for (; *line && used + 1 < size; line++)
		text[used++] = (wchar_t)*line;
	text[used] = 0;
}

/* the lines a card's text box shows (its art's height in small_ui) */
#define CARD_LINES 7

/* the gametype picker's card of the saved gametype (playlist profile): its
rules added under the Xbox's description, as many as the card has room for,
the card's text of size characters */
void menu_style_gametype_rules(
	long profile_index,
	wchar_t *text,
	long size)
{
	struct game_variant variant;
	char lines[4][48];
	long used, room, index;

	if (!pc_menus_chupathingy() || profile_index == NONE || !text)
		return;
	memset(&variant, 0, sizeof(variant));
	if (!playlist_profile_get(profile_index, &variant))
		return;
	if (variant.universal_variant.score_to_win > 0)
		snprintf(lines[0], sizeof(lines[0]), "Score to win: %ld", (long)variant.universal_variant.score_to_win);
	else
		snprintf(lines[0], sizeof(lines[0]), "Score to win: None");
	if (variant.universal_variant.lives > 0)
		snprintf(lines[1], sizeof(lines[1]), "Lives: %ld", (long)variant.universal_variant.lives);
	else
		snprintf(lines[1], sizeof(lines[1]), "Lives: Unlimited");
	if (variant.universal_variant.respawn_time)
		snprintf(lines[2], sizeof(lines[2]), "Respawn: %lds", (long)variant.universal_variant.respawn_time / 30);
	else
		snprintf(lines[2], sizeof(lines[2]), "Respawn: Instant");
	snprintf(lines[3], sizeof(lines[3]), "Weapons: %s", variant.universal_variant.weapon_set >= 0 &&
		variant.universal_variant.weapon_set < (long)NUMBEROF(weapon_set_names) ?
		weapon_set_names[variant.universal_variant.weapon_set] : "Custom");
	/* (the description's lines, then a blank one where there is room) */
	room = CARD_LINES - 1;
	for (used = 0; used < size && text[used]; used++)
	{
		if (text[used] == L'\n')
			room--;
	}
	if (room > (long)NUMBEROF(lines))
	{
		line_add(text, size, "");
		room--;
	}
	for (index = 0; index < room && index < (long)NUMBEROF(lines); index++)
		line_add(text, size, lines[index]);
}
