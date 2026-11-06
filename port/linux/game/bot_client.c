/*
BOT_CLIENT.C

A player's build's stand-ins for the bots, which are the dedicated server's
alone (HALO_SERVER; bots.c and the rest of bot_*.c are empty here): no bots,
no settings for them, no console command, and nothing a player can start or
change. Every stand-in does nothing.

This file and bots.c both define halo_bots_server_only_guard: a build that
links the server's bots and these stand-ins together fails to link.
*/

#ifndef HALO_SERVER

#include "cseries.h"
#include "bots.h"
#include "bot_debug.h"

int halo_bots_server_only_guard = 0;

long bots_configured(void) { return 0; }
void bots_set_count(long count) { (void)count; }
long bots_fill(void) { return 0; }
void bots_set_fill(long fill) { (void)fill; }
int bots_play_empty(void) { return 0; }
void bots_set_skill(long skill) { (void)skill; }
long bots_get_skill(void) { return 5; }
void bots_set_skill_spread(long spread) { (void)spread; }
long bots_get_skill_spread(void) { return 0; }
void bots_set_balance(long on) { (void)on; }
long bots_get_balance(void) { return 0; }
char const *bot_name(long slot) { (void)slot; return ""; }
void bot_reset(long player_absolute_index) { (void)player_absolute_index; }
void bot_think(long player_absolute_index, struct player_action *action)
{
	(void)player_absolute_index;
	(void)action;
}
void bots_map_loaded(void) {}
long bots_navigation_bsp(void) { return -1; }
boolean bot_bake_enabled(void) { return FALSE; }
void bot_bake_update(boolean main_menu_loaded, real seconds)
{
	(void)main_menu_loaded;
	(void)seconds;
}
void bots_map_disposed(void) {}

/* (the console's "bots", "bot_skill" and the rest: no command here) */
boolean bots_console_command(char const *text)
{
	(void)text;
	return FALSE;
}

/* (no bot is any player here) */
int bot_player_is_bot(long player_index)
{
	(void)player_index;
	return 0;
}
long bot_rvb_character_of(wchar_t const *name) { (void)name; return NONE; }
long bot_rvb_team(long character) { (void)character; return NONE; }
long bot_rvb_color(long character) { (void)character; return NONE; }
unsigned long long bot_time_microseconds(void) { return 0; }

/* the bots' screens and debug view: none */
boolean bots_screen_active(void) { return FALSE; }
boolean bots_screen_open(void) { return FALSE; }
void bots_screen_update(char const *screen_name) { (void)screen_name; }
void bots_screen_process(void) {}
void bots_screen_render(void) {}
void bot_debug_window_register(struct bot_debug_window_ops const *ops) { (void)ops; }
void bot_debug_update(void) {}
short bot_debug_camera_mode(short local_player_index, long *unit_index)
{
	(void)local_player_index;
	(void)unit_index;
	return 0;
}
boolean bot_debug_flying(short local_player_index)
{
	(void)local_player_index;
	return FALSE;
}
void bot_debug_draw(short local_player_index) { (void)local_player_index; }
void bot_debug_camera_update(void *data, struct camera_control const *controls, struct observer_command *result)
{
	(void)data;
	(void)controls;
	(void)result;
}

#endif /* HALO_SERVER */
