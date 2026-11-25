/*
COOP_PROGRESS.H

Network co-op's campaign progress (coop_progress.c): a level won together
recorded in the profiles of every machine's players, as a solo or split
screen win records it, so that the campaign's next level opens.
*/

#ifndef __COOP_PROGRESS_H
#define __COOP_PROGRESS_H

#ifdef HALO_XBOX_CONSOLE
/* (the original Xbox's build, warthog-xbox's port/xbox/build.py, lists the
platform's units it compiles and has no coop_progress.c: there network
co-op's progress is recorded as before, main.c) */
static __inline__ void coop_progress_client_level_started(char const *map_name, short difficulty)
{
	(void)map_name;
	(void)difficulty;
}
static __inline__ void coop_progress_client_round_ended(void) {}
static __inline__ void coop_progress_client_lobby_map(char const *map_name) { (void)map_name; }
#else

/* the host: the co-op level (of the campaign's ten, or NONE) won, on its
difficulty */
void coop_progress_host_won(
	short level,
	short difficulty);

/* a client: its co-op level (a map name) began (network_game_create_game_objects) */
void coop_progress_client_level_started(
	char const *map_name,
	short difficulty);

/* a client: its round ended and the host sent it back to the lobby
(network_game_client_switch_to_pregame) */
void coop_progress_client_round_ended(
	void);

/* a client in the lobby: the host's settings, with their map
(network_game_client_game_settings_updated) */
void coop_progress_client_lobby_map(
	char const *map_name);
#endif

#endif /* __COOP_PROGRESS_H */
