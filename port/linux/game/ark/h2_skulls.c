/*
H2_SKULLS.C

(port) Halo 2's skulls. Bungie's name is "ice cream flavor": a level's script
creates a weapon (objects\weapons\multiplayer\ball\head_sp), waits until a
player holds it, and calls (ice_cream_flavor_stock N).

The flavour's numbers come from the tag test's own debug globals table
(debug_ice_cream_flavor_status_*, each global's byte in the flavours' array):
0 Envy, 1 Grunt Birthday Party, 2 Assassins, 3 Thunderstorm, 4 Famine, 5 IWHBYD,
6 Blind, 7 Ghost, 8 Black Eye, 9 Catch, 10 Sputnik, 11 Iron, 12 Mythic, 13 Anger,
14 "That's Just... Wrong". Each effect's rule is its reader's in the code (the
owner OK'd reading it for rules; no code copied):
- ice_cream_flavor_stock N: a white fade of 20 ticks, globals' "ice cream" sound.
- Envy: the flashlight button is the Arbiter's camouflage.
- Assassins: every unit with an actor on a team hostile to the players camouflaged.
- Thunderstorm: every character its squad's major.
- Famine: a dropped weapon's rounds halved.
- Blind: no first person weapon, no HUD.
- Ghost: a unit with an actor pinged softly at most.
- Black Eye: a player's shield recharge held off; a melee hit gives +1 shield, 3 at most.
- Catch: an actor's grenade chance 1, no delay between tries, grenades always dropped.
- Sputnik: a blow's acceleration times 3.
- Mythic: a character's body and shield times 2.
- Anger: a hostile's weapon's rate of fire times 2.
Their hooks are at the engine's code, each behind h2_skull_on.

Not in Halo 2's rules (the MCC keeps skulls in its menu, the 2004 game turned one
on when found): this port does both: a found skull is on until the game exits;
the Campaign row (game.h2_skulls, HALO_H2_SKULLS, a mask) chooses any before the
level. A Halo 2 campaign level only (skulls_level): none in multiplayer.
*/

#include "cseries.h"
#include "memory/data.h"
#include "objects/objects.h"
#include "units/units.h"
#include "game/players.h"
#include "game/game_allegiance.h"
#include "objects/damage.h"
#include "main/main.h"
#include "hs/hs.h"
#include "tag_files/tag_groups.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "h2_skulls.h"
#include "../../src/port_config.h"

#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
boolean h2_scripts_level(void);
boolean network_coop_active(void);
boolean h2_ai_object_variant_named(long object_index, char const *name);
void h2_scripts_skull_sound(void);
void scripted_sound_new(long tag_index, long object_index, real scale);
void main_lost_map(void);

static unsigned long skulls_found;
/* a game mode's skulls (FIREFIGHT's sets, forge_modes.inc): on whatever the
level while it sets them */
static unsigned long skulls_mode;
static unsigned long skulls_chosen;
static boolean skulls_chosen_read;

static char const *const skull_names[NUMBER_OF_H2_SKULLS] =
{
	"Envy", "Grunt Birthday Party", "Assassins", "Thunderstorm", "Famine", "IWHBYD", "Blind", "Ghost",
	"Black Eye", "Catch", "Sputnik", "Iron", "Mythic", "Anger", "Whuppopotamus"
};

static unsigned long chosen_mask(void)
{
	if (!skulls_chosen_read)
	{
		char const *environment = getenv("HALO_H2_SKULLS");

		skulls_chosen = environment ? strtoul(environment, NULL, 0) : (unsigned long)config_integer("game.h2_skulls");
		skulls_chosen_read = TRUE;
	}
	return skulls_chosen;
}

/* (a skull is the campaign's: Halo 2's and the MCC's play none in
multiplayer. With them on in every Halo 2 level an insider.5 tester who had
turned them all on - the Campaign screen's EXTRAS, HALO_H2_SKULLS=32767 -
played every multiplayer map Blind: no HUD, no first person weapon) */
static boolean skulls_level(void)
{
	struct scenario *scenario = global_scenario_get();

	return h2_scripts_level() && scenario && scenario->type == _scenario_type_solo;
}

void h2_skulls_mode_set(unsigned long mask)
{
	if (mask != skulls_mode)
		platform_log("h2 skulls: a game mode's skulls %#lx", mask);
	skulls_mode = mask;
}

boolean h2_skull_on(short skull)
{
	if (skull >= 0 && skull < NUMBER_OF_H2_SKULLS && (skulls_mode & (1UL << skull)))
		return TRUE;
	return skull >= 0 && skull < NUMBER_OF_H2_SKULLS && ((skulls_found | chosen_mask()) & (1UL << skull)) != 0 &&
		skulls_level();
}

boolean h2_skull_available(short skull)
{
	return h2_skull_on(skull);
}

void h2_skull_found(short skull)
{
	if (skull < 0 || skull >= NUMBER_OF_H2_SKULLS)
		return;
	skulls_found |= 1UL << skull;
	platform_log("h2 skulls: %s found (flavor %d)", skull_names[skull], (int)skull);
	hs_compile_and_evaluate("(fade_in 1 1 1 20)");
	h2_scripts_skull_sound();
}

char const *h2_skull_name(short skull)
{
	return skull >= 0 && skull < NUMBER_OF_H2_SKULLS ? skull_names[skull] : "";
}

boolean h2_skull_chosen(short skull)
{
	return skull >= 0 && skull < NUMBER_OF_H2_SKULLS && (chosen_mask() & (1UL << skull)) != 0;
}

void h2_skull_choose(short skull, boolean on)
{
	if (skull < 0 || skull >= NUMBER_OF_H2_SKULLS)
		return;
	chosen_mask();
	skulls_chosen = on ? skulls_chosen | (1UL << skull) : skulls_chosen & ~(1UL << skull);
	config_write_integer("game.h2_skulls", (long)skulls_chosen);
}

short h2_skulls_chosen_count(void)
{
	short skull, count = 0;

	for (skull = 0; skull < NUMBER_OF_H2_SKULLS; skull++)
		count += h2_skull_chosen(skull) ? 1 : 0;
	return count;
}

/* (damage.c) Mythic: a character's body and shield times 2 */
real h2_skulls_vitality_scale(long object_index)
{
	struct object_datum *object;

	if (!(skulls_found | skulls_chosen) && skulls_chosen_read)
		return 1.0f;
	if (!h2_skull_on(_h2_skull_mythic))
		return 1.0f;
	object = object_get(object_index);
	if (!TEST_FLAG(_object_mask_unit, object->object.type) || ((struct unit_datum *)object)->unit.actor_index == NONE)
		return 1.0f;
	return 2.0f;
}

/* (weapons.c) Anger: a weapon in the hands of a unit hostile to the players */
boolean h2_skulls_anger_weapon(long weapon_index)
{
	struct object_datum *weapon, *owner;
	long player_index = local_player_get_player_index(0);

	if (!h2_skull_on(_h2_skull_anger) || player_index == NONE)
		return FALSE;
	weapon = object_try_and_get(weapon_index);
	owner = weapon && weapon->object.parent_object_index != NONE ? object_try_and_get(weapon->object.parent_object_index) : NULL;
	return owner && game_team_is_enemy((short)player_get(player_index)->team_index, owner->object.owner_team_index);
}

/* (units.c) Grunt Birthday Party: a Grunt killed outright: the cheer */
void h2_skulls_grunt_killed(long unit_index, unsigned long damage_flags)
{
	struct object_datum *object = object_try_and_get(unit_index);

	if (object && h2_skull_on(_h2_skull_grunt_birthday_party) && strstr(tag_get_name(object->definition_index), "grunt") &&
		(damage_flags & FLAG(_object_being_damaged_killed_instantly_bit)))
	{
		long tag_index = tag_loaded('snd!', "sound_remastered\\skulls\\grunt_birthday_party");

		platform_log("h2 skulls: Grunt Birthday Party at %08lx", (unsigned long)unit_index);
		if (tag_index != NONE)
			scripted_sound_new(tag_index, unit_index, 1.0f);
	}
}

/* (units.c) Black Eye: a player's melee hit on an enemy: +1 shield, 3 at most */
void h2_skulls_melee_hit(long attacker_unit_index)
{
	struct object_datum *object = attacker_unit_index != NONE ? object_try_and_get(attacker_unit_index) : NULL;

	if (!h2_skull_on(_h2_skull_black_eye) || !object || !TEST_FLAG(_object_mask_unit, object->object.type) ||
		((struct unit_datum *)object)->unit.player_index == NONE)
	{
		return;
	}
	object->object.shield_vitality = MIN(object->object.shield_vitality + 1.0f, 3.0f);
}

void h2_skulls_update(void)
{
	struct data_iterator players;
	struct player_datum *player;
	short player_team = NONE;
	boolean someone_dead = FALSE, someone_alive = FALSE;

	if (!skulls_mode && (!h2_scripts_level() || !(skulls_found | chosen_mask())))
		return;
	data_iterator_new(&players, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&players)) != NULL)
	{
		struct object_datum *object = player->unit_index != NONE ? object_try_and_get(player->unit_index) : NULL;

		if (player_team == NONE)
			player_team = (short)player->team_index;
		if (!object)
		{
			someone_dead = TRUE;
			continue;
		}
		someone_alive = TRUE;
		/* (Black Eye: its shield's recharge held off, each tick) */
		if (h2_skull_on(_h2_skull_black_eye) && object->object.shield_stun_ticks <= 1)
			object->object.shield_stun_ticks = 10;
		/* (Envy: the flashlight button goes to the camouflage, not the light) */
		if (h2_skull_on(_h2_skull_envy))
		{
			struct unit_datum *unit = (struct unit_datum *)object;
			boolean on = unit_get_current_flashlight_state(player->unit_index) != 0;

			unit->unit.active_camouflage = on ? 1.0f : 0.0f;
			SET_FLAG(unit->unit.flags, _unit_active_camouflaged_bit, on);
		}
	}
	/* (Iron: a co-op game restarts at the checkpoint with one player's death) */
	if (h2_skull_on(_h2_skull_iron) && network_coop_active() && someone_dead && someone_alive)
		main_lost_map();
	if (h2_skull_on(_h2_skull_assassins) || h2_skull_on(_h2_skull_thunderstorm))
	{
		struct object_iterator iterator;
		struct object_datum *object;

		object_iterator_new(&iterator, _object_mask_biped, 0);
		while ((object = object_iterator_next(&iterator)) != NULL)
		{
			struct unit_datum *unit = (struct unit_datum *)object;

			if (unit->unit.actor_index == NONE || TEST_FLAG(object->object.damage_flags, _object_dead_bit))
				continue;
			if (h2_skull_on(_h2_skull_assassins) && player_team != NONE &&
				game_team_is_enemy(player_team, object->object.owner_team_index))
			{
				unit->unit.active_camouflage = 1.0f;
				SET_FLAG(unit->unit.flags, _unit_active_camouflaged_bit, TRUE);
			}
			if (h2_skull_on(_h2_skull_thunderstorm) && game_time_get() % 30 == 0)
				h2_ai_object_variant_named(iterator.index, "major");
		}
	}
}
