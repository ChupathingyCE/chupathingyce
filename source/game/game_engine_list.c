/*
GAME_ENGINE_LIST.C

symbols in this file:
002DE510 0020:
	_game_engines (0000)
*/

/* ---------- headers */

#include "cseries/cseries.h"

#include "game/game_engine.h"
#include "game/game_engine_race.h"
#include "game/game_engine_slayer.h"
#include "game/game_engine_list.h"

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

/* ---------- prototypes */

/* ---------- globals */

extern struct game_engine stub_engine;
extern struct game_engine king_engine;
extern struct game_engine oddball_engine;
extern struct game_engine ctf_engine;
#ifdef HALO_GAME_BROWSER
/* port: game_engine_bomb.c */
extern struct game_engine bomb_engine;
#endif

struct game_engine *game_engines[] =
{
	NULL,
	&ctf_engine,
	&slayer_engine,
	&oddball_engine,
	&king_engine,
	&race_engine,
#ifdef HALO_GAME_BROWSER
	&bomb_engine,
#else
	&stub_engine,
#endif
	NULL,
};

/* ---------- public code */

/* ---------- private code */
