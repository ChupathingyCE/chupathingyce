/*
CONDUCT_RULES.H

A dedicated server's betrayal and AFK limits (server/src/server_conduct.c,
server/docs/moderation.md), the parts that need no game to decide: when a
player's betrayals in a game earn a warning or a kick, whether an automatic
kick is a player's second in a day (then a short ban), whether a player's
input is input (anything they pressed, moved or turned) and when a player
with none has been idle long enough to be warned or kicked. Both limits are
off unless set. Nothing here has a dependency but the C library; port/linux/
tests/conduct_rules_test.c tests it (tools/test_conduct_rules.py).
*/

#ifndef __CONDUCT_RULES_H
#define __CONDUCT_RULES_H

#include <stddef.h>

/* ---------- constants */

enum
{
	/* nothing to do */
	_conduct_none,
	/* a warning: one more and the player is kicked */
	_conduct_warn,
	/* the limit reached: kicked */
	_conduct_kick,
};

enum
{
	/* the most a limit may be set to: betrayals in a game, idle minutes */
	CONDUCT_MAXIMUM_BETRAYALS = 100,
	CONDUCT_MAXIMUM_AFK_MINUTES = 120,
	/* a second automatic kick within this long is a ban of this long */
	CONDUCT_REPEAT_SECONDS = 24 * 60 * 60,
	CONDUCT_BAN_SECONDS = 30 * 60,
	/* how long before an AFK kick the player is warned */
	CONDUCT_AFK_WARNING_MILLISECONDS = 30 * 1000,
	/* the automatic kicks remembered (the oldest forgotten past it) */
	CONDUCT_HISTORY_SIZE = 256,
	/* a player's key in it ("h:<hardware id>"), with its end */
	CONDUCT_KEY_SIZE = 80,
};

/* ---------- structures */

/* a player's input as the host takes it (the game's player_action, its
values alone) */
struct conduct_input
{
	unsigned long control_flags;
	float yaw;
	float pitch;
	float throttle_i;
	float throttle_j;
	float primary_trigger;
	short weapon_index;
	short grenade_index;
	short zoom_level;
};

/* the automatic kicks of the last day, by the player's key */
struct conduct_kick_history
{
	struct
	{
		char key[CONDUCT_KEY_SIZE];
		/* when (unix seconds); 0 for an empty entry */
		long long time;
	} entries[CONDUCT_HISTORY_SIZE];
};

/* ---------- prototypes */

/* what a player's betrayals in this game call for, the limit (0: off) and
their count before and after the latest: a warning when the count reaches
one short of the limit, a kick when it reaches the limit (a limit of 1:
the first is a kick); nothing again for counts already acted on */
int conduct_betrayal_action(int limit, int previous, int now);

/* an automatic kick of the player with that key (empty: none known, never
a repeat) at now (unix seconds): TRUE (1) if the same key was kicked
automatically within CONDUCT_REPEAT_SECONDS before (then the kick is a
ban), else 0. Either way, it is remembered */
int conduct_note_automatic_kick(struct conduct_kick_history *history, char const *key, long long now);

/* whether a player's input is input (1), against the one before it: a
button, the trigger, the weapon, the grenade or the zoom changed, they
turned, or they move (the stick held out) */
int conduct_input_active(struct conduct_input const *previous, struct conduct_input const *now);

/* what a player idle so long (milliseconds without input) calls for, with
the limit in minutes (0: off): a warning CONDUCT_AFK_WARNING_MILLISECONDS
before the limit (if the limit is longer than that), a kick at it */
int conduct_afk_action(int minutes, unsigned long idle_milliseconds);

#endif
