/*
BOT_PORT.H

What the bots (bots.c, bot_move.c, bot_aas.c, bot_nav.c) take from the rest
of the port, and what the rest of the port takes from them (bots.h).
*/

#ifndef __BOT_PORT_H
#define __BOT_PORT_H

#include "cseries.h"
#include "game/players.h"
#include "bots.h"

/* the bots keep their buffers on the C runtime's heap, not the game's debug
heap (a map's navigation is tens of megabytes) */
#undef malloc
#undef free
#undef realloc
#undef memcpy
#undef memset
#undef memmove
#undef memcmp

#define BOT_TICKS_PER_SECOND 30
#define BOT_MAXIMUM_PLAYERS HALO_PORT_MAXIMUM_NETWORK_PLAYERS
#define BOT_MAXIMUM_OBJECTS HALO_PORT_MAXIMUM_OBJECTS_PER_MAP

/* a line in the port's log (and the game's debug.txt) */
void bot_log(char const *format, ...);
/* a clock for seeds and for timing the navigation's build (microseconds) */
unsigned long long bot_time_microseconds(void);

/* testing (HALO_BOT_PROFILE): what parts of the bots cost. A section's time
is from its BOT_PROFILE_BEGIN to its BOT_PROFILE_END in the same block;
bot_profile_report logs each section's share since the last report. */
extern int bot_profiling;
void bot_profile_add(char const *section, unsigned long long microseconds);
void bot_profile_report(long ticks);
/* ... or from a mark to the next (NULL ends the last) */
void bot_profile_mark(char const *section);
#define BOT_PROFILE_MARK(name) do { if (bot_profiling) bot_profile_mark(name); } while (0)
#define BOT_PROFILE_BEGIN(id) unsigned long long bot_profile_start_##id = bot_profiling ? bot_time_microseconds() : 0
#define BOT_PROFILE_END(id, name) \
	do { if (bot_profiling) bot_profile_add(name, bot_time_microseconds() - bot_profile_start_##id); } while (0)

#endif /* __BOT_PORT_H */
