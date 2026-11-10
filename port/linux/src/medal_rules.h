/*
MEDAL_RULES.H

The medals, worked out from a game's kills as Halo announces them, and
their names. One set of rules for everything that counts medals: the game
list's statistics recorder (port/linux/game/game_stats.c, on the host and
on every client), Delta Stats' event log (event_log.c, the host's), the
local service record (service_record.c) and the medal pop-ups
(display.medals). They watch the game and change nothing in it: medals are
for the HUD, the service record and the site.

- multikills: each kill within 4 seconds of the one before (the game's own
  window, game_statistics_record_kill): Double Kill (2), Triple Kill,
  Killtacular, Killtrocity (5), as Halo: Combat Evolved has them, then
  Halo 3's Killimanjaro (6), Killtastrophe, Killpocalypse and Killionaire
  (9 and every kill after);
- sprees, kills without dying: Killing Spree (5), Killing Frenzy, Running
  Riot, Rampage (20), then Untouchable, Invincible, Inconceivable and
  Unfriggenbelievable (40);
- a weapon's sprees, its kills without dying: Sniper Spree (5) and
  Sharpshooter (10), Shotgun Spree and Open Season, Splatter Spree and
  Vehicular Manslaughter, Stick Spree (5 grenades stuck);
- a kill's: Beat Down (melee), Sniper Kill, Stuck (a plasma grenade),
  Splatter, From the Grave (the killer dead as it landed), Killjoy (the end
  of someone's spree of 5 or more), First Strike (the game's first kill)
  and Revenge (the player who last killed you).

A betrayal or a suicide earns nothing, and ends the dead player's sprees.
The keys are the site's (stats.chupathingy.org's site.js, MEDALS) and the
reports' ("medals": {key: count}, docs/delta.md "Medals").

Header only (static inline functions, the platform layer's C, nothing of the
game's): the recorder, the event log, the consoles' builds and the unit
tests (port/linux/tests/medal_rules_test.c) take the same rules without
another file to build.
*/

#ifndef MEDAL_RULES_H
#define MEDAL_RULES_H

#include <string.h>

/* ---------- constants */

enum medal_kind
{
	MEDAL_DOUBLE_KILL,
	MEDAL_TRIPLE_KILL,
	MEDAL_KILLTACULAR,
	MEDAL_KILLTROCITY,
	MEDAL_KILLIMANJARO,
	MEDAL_KILLTASTROPHE,
	MEDAL_KILLPOCALYPSE,
	MEDAL_KILLIONAIRE,
	MEDAL_KILLING_SPREE,
	MEDAL_KILLING_FRENZY,
	MEDAL_RUNNING_RIOT,
	MEDAL_RAMPAGE,
	MEDAL_UNTOUCHABLE,
	MEDAL_INVINCIBLE,
	MEDAL_INCONCEIVABLE,
	MEDAL_UNFRIGGENBELIEVABLE,
	MEDAL_BEAT_DOWN,
	MEDAL_SNIPER_KILL,
	MEDAL_GRENADE_STICK,
	MEDAL_SPLATTER,
	MEDAL_FROM_THE_GRAVE,
	MEDAL_KILLJOY,
	MEDAL_FIRST_STRIKE,
	MEDAL_REVENGE,
	MEDAL_SNIPER_SPREE,
	MEDAL_SHARPSHOOTER,
	MEDAL_SHOTGUN_SPREE,
	MEDAL_OPEN_SEASON,
	MEDAL_SPLATTER_SPREE,
	MEDAL_VEHICULAR_MANSLAUGHTER,
	MEDAL_STICK_SPREE,
	NUMBER_OF_MEDALS
};

/* what a kill was (medal_rules_kill's kinds) */
enum
{
	MEDAL_KILL_MELEE = 1 << 0,
	MEDAL_KILL_SNIPER = 1 << 1,
	MEDAL_KILL_STICK = 1 << 2,
	MEDAL_KILL_SHOTGUN = 1 << 3,
	MEDAL_KILL_SPLATTER = 1 << 4,
	/* the killer was dead as it landed (a grenade or a rocket of theirs) */
	MEDAL_KILL_FROM_GRAVE = 1 << 5,
};

enum
{
	/* the game's ticks a second, and its multikill window */
	MEDAL_RULES_TICKS_PER_SECOND = 30,
	MEDAL_RULES_MULTIKILL_TICKS = 4 * MEDAL_RULES_TICKS_PER_SECOND,
	/* the most medals one kill can earn */
	MEDAL_RULES_MAXIMUM_PER_KILL = 16,
	/* a spree's medals: one every this many kills, up to the last */
	MEDAL_RULES_SPREE_STEP = 5,
	MEDAL_RULES_SPREE_LAST = 40,
	/* a weapon's spree's medals */
	MEDAL_RULES_WEAPON_SPREE_FIRST = 5,
	MEDAL_RULES_WEAPON_SPREE_SECOND = 10,
	/* the end of a spree this long is a Killjoy */
	MEDAL_RULES_KILLJOY_SPREE = 5,
};

/* the weapons' sprees, in medal_player's weapon_sprees */
enum
{
	_medal_weapon_sniper,
	_medal_weapon_shotgun,
	_medal_weapon_splatter,
	_medal_weapon_stick,
	NUMBER_OF_MEDAL_WEAPONS
};

/* ---------- structures */

/* a player's medal state in one game (medal_player_reset: a new player) */
struct medal_player
{
	int spree;
	int best_spree;
	int chain;
	long last_kill_tick;
	/* the slot of the player who killed this one last (-1: none) */
	int last_killer;
	int weapon_sprees[NUMBER_OF_MEDAL_WEAPONS];
};

/* a game's own (medal_game_reset: a new game) */
struct medal_game
{
	int kills;
};

/* ---------- the medals' names */

static inline char const *medal_rules_key(int medal)
{
	static char const *const keys[NUMBER_OF_MEDALS] = {
	"double_kill", "triple_kill", "killtacular", "killtrocity", "killimanjaro", "killtastrophe", "killpocalypse",
	"killionaire", "killing_spree", "killing_frenzy", "running_riot", "rampage", "untouchable", "invincible",
	"inconceivable", "unfriggenbelievable", "beat_down", "sniper_kill", "grenade_stick", "splatter",
	"from_the_grave", "killjoy", "first_strike", "revenge", "sniper_spree", "sharpshooter", "shotgun_spree",
	"open_season", "splatter_spree", "vehicular_manslaughter", "stick_spree",
	};

	return medal >= 0 && medal < NUMBER_OF_MEDALS ? keys[medal] : "";
}

static inline char const *medal_rules_name(int medal)
{
	static char const *const names[NUMBER_OF_MEDALS] = {
	"Double Kill", "Triple Kill", "Killtacular", "Killtrocity", "Killimanjaro", "Killtastrophe", "Killpocalypse",
	"Killionaire", "Killing Spree", "Killing Frenzy", "Running Riot", "Rampage", "Untouchable", "Invincible",
	"Inconceivable", "Unfriggenbelievable", "Beat Down", "Sniper Kill", "Stuck", "Splatter", "From the Grave",
	"Killjoy", "First Strike", "Revenge", "Sniper Spree", "Sharpshooter", "Shotgun Spree", "Open Season",
	"Splatter Spree", "Vehicular Manslaughter", "Stick Spree",
	};

	return medal >= 0 && medal < NUMBER_OF_MEDALS ? names[medal] : "";
}

/* a key's medal, -1 for none */
static inline int medal_rules_find(char const *key)
{
	int medal;

	for (medal = 0; key && medal < NUMBER_OF_MEDALS; medal++)
	{
		if (!strcmp(medal_rules_key(medal), key))
			return medal;
	}
	return -1;
}

/* ---------- the rules */

static inline void medal_player_reset(struct medal_player *player)
{
	memset(player, 0, sizeof(*player));
	player->last_killer = -1;
}

static inline void medal_game_reset(struct medal_game *game)
{
	memset(game, 0, sizeof(*game));
}

static inline int medal_rules_add(int *medals, int count, int medal)
{
	if (count < MEDAL_RULES_MAXIMUM_PER_KILL)
		medals[count++] = medal;
	return count;
}

/* a weapon's spree: its count, and its medals at the first and second */
static inline int medal_rules_weapon_spree(struct medal_player *killer, int weapon, int first, int second, int *medals,
	int count)
{
	int kills = ++killer->weapon_sprees[weapon];

	if (kills == MEDAL_RULES_WEAPON_SPREE_FIRST)
		count = medal_rules_add(medals, count, first);
	else if (kills == MEDAL_RULES_WEAPON_SPREE_SECOND && second >= 0)
		count = medal_rules_add(medals, count, second);
	return count;
}

/* a death: victim (NULL: not tracked) killed by killer (NULL: no player's
kill), at tick, the kill one that counts (not a betrayal or a suicide) or
not, of kinds (MEDAL_KILL_*). The killer's slot is remembered for Revenge,
the victim's checked for it. The medals the killer earned go in medals (at
most MEDAL_RULES_MAXIMUM_PER_KILL), in the order they were earned; returns
their count */
static inline int medal_rules_kill(struct medal_game *game, struct medal_player *killer, int killer_slot,
	struct medal_player *victim, int victim_slot, int counts, long tick, unsigned int kinds, int *medals)
{
	int count = 0;
	int victim_spree = victim ? victim->spree : 0;

	if (!killer || killer == victim)
		counts = 0;
	if (victim)
	{
		victim->spree = 0;
		victim->chain = 0;
		memset(victim->weapon_sprees, 0, sizeof(victim->weapon_sprees));
		if (counts)
			victim->last_killer = killer_slot;
	}
	if (!counts)
		return 0;
	/* the game's first kill */
	if (game && game->kills++ == 0)
		count = medal_rules_add(medals, count, MEDAL_FIRST_STRIKE);
	/* a multikill: each kill within the window of the one before */
	if (killer->chain > 0 && tick - killer->last_kill_tick <= MEDAL_RULES_MULTIKILL_TICKS)
		killer->chain++;
	else
		killer->chain = 1;
	killer->last_kill_tick = tick;
	if (killer->chain >= 2)
	{
		int step = killer->chain - 2;
		int last = MEDAL_KILLIONAIRE - MEDAL_DOUBLE_KILL;

		count = medal_rules_add(medals, count, MEDAL_DOUBLE_KILL + (step > last ? last : step));
	}
	/* a spree: kills without dying */
	killer->spree++;
	if (killer->spree > killer->best_spree)
		killer->best_spree = killer->spree;
	if (killer->spree % MEDAL_RULES_SPREE_STEP == 0 && killer->spree <= MEDAL_RULES_SPREE_LAST)
		count = medal_rules_add(medals, count, MEDAL_KILLING_SPREE + killer->spree / MEDAL_RULES_SPREE_STEP - 1);
	/* the kind of kill, and its weapon's spree */
	if (kinds & MEDAL_KILL_MELEE)
		count = medal_rules_add(medals, count, MEDAL_BEAT_DOWN);
	if (kinds & MEDAL_KILL_SNIPER)
	{
		count = medal_rules_add(medals, count, MEDAL_SNIPER_KILL);
		count = medal_rules_weapon_spree(killer, _medal_weapon_sniper, MEDAL_SNIPER_SPREE, MEDAL_SHARPSHOOTER, medals,
			count);
	}
	if (kinds & MEDAL_KILL_SHOTGUN)
		count = medal_rules_weapon_spree(killer, _medal_weapon_shotgun, MEDAL_SHOTGUN_SPREE, MEDAL_OPEN_SEASON, medals,
			count);
	if (kinds & MEDAL_KILL_STICK)
	{
		count = medal_rules_add(medals, count, MEDAL_GRENADE_STICK);
		count = medal_rules_weapon_spree(killer, _medal_weapon_stick, MEDAL_STICK_SPREE, -1, medals, count);
	}
	if (kinds & MEDAL_KILL_SPLATTER)
	{
		count = medal_rules_add(medals, count, MEDAL_SPLATTER);
		count = medal_rules_weapon_spree(killer, _medal_weapon_splatter, MEDAL_SPLATTER_SPREE,
			MEDAL_VEHICULAR_MANSLAUGHTER, medals, count);
	}
	if (kinds & MEDAL_KILL_FROM_GRAVE)
		count = medal_rules_add(medals, count, MEDAL_FROM_THE_GRAVE);
	/* the end of someone's spree, and the one who killed you last */
	if (victim && victim_spree >= MEDAL_RULES_KILLJOY_SPREE)
		count = medal_rules_add(medals, count, MEDAL_KILLJOY);
	if (victim && killer->last_killer >= 0 && killer->last_killer == victim_slot)
	{
		count = medal_rules_add(medals, count, MEDAL_REVENGE);
		killer->last_killer = -1;
	}
	return count;
}

#endif
