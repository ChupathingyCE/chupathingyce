/*
FORGE_MENU.H

The Forge menu (forge_menu.c): Forge's screens in the Delta Guide's look,
drawn and driven by code over the menus or the game, as the Guide is.

- FORGE in the Multiplayer menu (ui_widget.c, with game.forge on) opens its
  lobby: the multiplayer maps, and for the one chosen, on FORGE, NEW LAYOUT
  and its saved layouts to edit, on PLAY its saved layouts to play as a
  game of a game type; Y deletes one (pressed twice).
- In a Forge game the monitor's X (the keyboard's controls.forge_menu)
  opens the Forge menu: OBJECTS (the map's objects by category, the one
  chosen placed in the monitor's hand), PROPERTIES (of what is held),
  LAYOUTS (save, save as new, load and delete the map's layouts) and
  OPTIONS (snap, speed, rotation snap, undo, redo, new round, play as the
  Spartan, the controls).

Off with game.forge off; none of it on the consoles (HALO_CONSOLE_NO_FORGE)
or without the Guide (HALO_GUIDE), where the calls below do nothing.

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef __FORGE_MENU_H
#define __FORGE_MENU_H

#include "cseries.h"

struct halo_ui_pointer;

/* a prompt of the monitor's HUD: a controller's button (ui_overlay.h's
UI_BUTTON_*, NONE: the word alone), the Forge key that does the same on the
keyboard (halo_keyboard.h's halo_forge_key, NONE: the button's word), and
the word */
struct forge_hud_prompt
{
	short button;
	short key;
	char const *word;
};

/* the Multiplayer menu's FORGE: the lobby opened over the menus */
void forge_menu_open_lobby(
	void);

/* (forge.c) the Forge menu in a Forge game: opened (on PROPERTIES while
something is held), whether it is, and closed */
void forge_menu_game_open_now(
	void);
boolean forge_menu_game_open(
	void);
void forge_menu_game_close(
	void);

/* whether a screen of the Forge menu is up (it takes the first player's
input, as the Guide does) */
boolean forge_menu_active(
	void);

/* (ui_widget.c) each frame, as the Guide's: what closes it; while it is
up, its input instead of the menus'; the menus' pointer; and its drawing,
after the menus' and under the Guide's */
void forge_menu_update(
	void);
void forge_menu_process(
	void);
void forge_menu_pointer(
	struct halo_ui_pointer const *pointer);
void forge_menu_render(
	void);

/* (forge.c's forge_draw) the monitor's prompts along the bottom of the
view, in the prompts' buttons (input.button_style, input.hud_buttons) or
the keyboard's keys */
void forge_menu_draw_prompts(
	struct forge_hud_prompt const *prompts,
	short count);

/* (forge.c's forge_draw) the delete held (game.forge_hold_to_delete): how far
along, 0 to 1, as a fill over the crosshair */
void forge_menu_draw_hold(
	float fraction);

#endif
