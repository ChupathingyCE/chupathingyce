/*
HALO_PROMPT_SCHEMES.H

The devices the button prompts are drawn for (xinput_sdl.c's
platform_prompt_scheme, ui_overlay.c's glyphs), and which a pad is: SDL
tells the Xbox, PlayStation and Nintendo pads apart by their type, and the
Steam Deck and the Steam Controller by Valve's vendor number (SDL gives
them no type of their own). The enumeration, and its functions where
HALO_PROMPT_SCHEME_FUNCTIONS is defined before it (xinput_sdl.c); plain
C89, no SDL.
*/

#ifndef HALO_PROMPT_SCHEMES_H
#define HALO_PROMPT_SCHEMES_H

enum
{
	PROMPT_SCHEME_KEYBOARD,
	PROMPT_SCHEME_XBOX,
	PROMPT_SCHEME_PLAYSTATION,
	/* the Switch's pads (by position: SDL's south button is its B) */
	PROMPT_SCHEME_NINTENDO,
	PROMPT_SCHEME_STEAM_DECK,
	/* the Wii U's GamePad and Pro Controller (by label: the Wii U port
	presses A with A) */
	PROMPT_SCHEME_WIIU,
	PROMPT_SCHEME_STEAM_CONTROLLER,

	NUMBER_OF_PROMPT_SCHEMES
};

#ifdef HALO_PROMPT_SCHEME_FUNCTIONS
#include <string.h>

#define PROMPT_VALVE_VENDOR 0x28de
/* Steam's virtual pad (Steam Input), standing in for whatever is held */
#define PROMPT_VALVE_VIRTUAL_PAD 0x11ff
#define PROMPT_VALVE_STEAM_DECK 0x1205

/* the scheme of a pad: by_type the scheme its SDL type gives (an Xbox,
PlayStation or Nintendo pad's, else PROMPT_SCHEME_XBOX), vendor and product
its USB numbers, on_steam_deck whether the game runs on a Steam Deck (its
SteamDeck environment variable), where Steam's virtual pad is the Deck's
own controls */
static int prompt_scheme_of_pad(int by_type, unsigned int vendor, unsigned int product, int on_steam_deck)
{
	if (vendor == PROMPT_VALVE_VENDOR)
	{
		if (product == PROMPT_VALVE_STEAM_DECK)
			return PROMPT_SCHEME_STEAM_DECK;
		if (product != PROMPT_VALVE_VIRTUAL_PAD)
			return PROMPT_SCHEME_STEAM_CONTROLLER;
		/* (Steam Input's pad: the Deck's, else what SDL saw behind it) */
		if (on_steam_deck && by_type == PROMPT_SCHEME_XBOX)
			return PROMPT_SCHEME_STEAM_DECK;
	}
	return by_type;
}

/* a scheme by its name ("xbox", "steam_deck"...), for debug.prompt_device:
-1 for none (empty, "auto" or unknown) */
static int prompt_scheme_named(const char *name)
{
	static const char *const names[NUMBER_OF_PROMPT_SCHEMES] =
	{
		"keyboard", "xbox", "playstation", "nintendo", "steam_deck", "wiiu", "steam_controller",
	};
	int scheme;

	if (!name)
		return -1;
	for (scheme = 0; scheme < NUMBER_OF_PROMPT_SCHEMES; scheme++)
	{
		if (!strcmp(name, names[scheme]))
			return scheme;
	}
	return -1;
}
#endif

#endif
