/*
H2_PLAYER_MODELS.C

Halo 2's multiplayer player models in a game (h2_player_models.h says what
for). The bipeds are found by the names h2_biped.c gives them
("h2\characters\spartan_mp\spartan_mp"); a model's regions and permutations
are its model tag's (the default variant's permutation first: h2_biped.c);
a look is kept for each local player and put on its unit at the top of a
frame (its region permutations, which the netcode sends on as it sends any
object's: network_objects.c).
*/

/* ---------- headers */

#include "cseries.h"
#include "tag_files/tag_groups.h"
#include "memory/data.h"
#include "game/game.h"
#include "game/players.h"
#include "models/model_definitions.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"

#include "h2_player_models.h"
#include "h2_emblem.h"
#include "player_looks.h"
#include "scene_packs.h"

int bots_player_is_bot(long player_index);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);

/* ---------- constants */

enum
{
	MAXIMUM_LOCAL_LOOKS = 4,
	/* (players.h's MAXIMUM_PLAYERS_PER_GAME: 16) */
	MAXIMUM_PLAYERS = 16,
};

/* ---------- globals */

static struct
{
	char const *name;
	char const *biped;
} const h2_player_model_table[NUMBER_OF_H2_PLAYER_MODELS] =
{
	{ "Spartan", "h2\\characters\\spartan_mp\\spartan_mp" },
	{ "Elite", "h2\\characters\\elite_mp\\elite_mp" },
};

static struct
{
	boolean set[MAXIMUM_LOCAL_LOOKS];
	struct h2_player_look looks[MAXIMUM_LOCAL_LOOKS];
	/* (the host's) each remote player's model as its machine named it,
	checked (NONE: none named) */
	boolean remote_ready;
	short remote[MAXIMUM_PLAYERS];
} h2_player_models;

/* ---------- private code */

/* FNV-1a over the model's name and its biped's path: the same on every
machine and build */
static unsigned long identity_of(short model)
{
	unsigned long hash = 2166136261UL;
	char const *texts[2];
	int t;

	texts[0] = h2_player_model_table[model].name;
	texts[1] = h2_player_model_table[model].biped;
	for (t = 0; t < 2; t++)
	{
		char const *c;

		for (c = texts[t]; *c; c++)
		{
			hash ^= (unsigned char)*c;
			hash = (hash * 16777619UL) & 0xFFFFFFFFUL;
		}
		hash ^= 0xFF;
		hash = (hash * 16777619UL) & 0xFFFFFFFFUL;
	}
	return hash;
}

/* the model tag of a model's biped (NONE: none) */
static long model_tag(short model)
{
	long biped = h2_player_models_biped(model);
	struct object_definition const *definition;

	if (biped == NONE)
		return NONE;
	definition = (struct object_definition const *)tag_get('bipd', biped);
	return definition ? definition->object.model.index : NONE;
}

/* the species the player chose (the settings panel's "Halo 2 species",
HALO_H2_PLAYER_SPECIES: "spartan", the default, or "elite") */
static short chosen_model(void)
{
	struct h2_emblem emblem;

	/* (the profile's Halo 2 appearance: h2_emblem.h; HALO_H2_PLAYER_SPECIES for
	a profile that has none) */
	h2_emblem_local(0, &emblem);
	return emblem.model ? H2_PLAYER_MODEL_ELITE : H2_PLAYER_MODEL_SPARTAN;
}

/* the species a player plays without a look of its own: a local player
the chosen one; HALO_H2_SPECIES ("spartan", "elite": everyone else;
"mixed", the default: bots alternate by their place) */
static short default_model(long player_index, boolean local)
{
	char const *setting = getenv("HALO_H2_SPECIES");

	if (local)
		return chosen_model();
	if (setting && !strcmp(setting, "elite"))
		return H2_PLAYER_MODEL_ELITE;
	if (setting && !strcmp(setting, "spartan"))
		return H2_PLAYER_MODEL_SPARTAN;
	return (DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) & 1) ? H2_PLAYER_MODEL_ELITE : H2_PLAYER_MODEL_SPARTAN;
}

/* ---------- public code */

char const *h2_player_models_name(
	short model)
{
	return model >= 0 && model < NUMBER_OF_H2_PLAYER_MODELS ? h2_player_model_table[model].name : "";
}

unsigned long h2_player_models_identity(
	short model)
{
	return model >= 0 && model < NUMBER_OF_H2_PLAYER_MODELS ? identity_of(model) : 0;
}

short h2_player_models_from_identity(
	unsigned long identity)
{
	short model;

	/* (a remote machine's word: only an identity of the table, whose biped
	this machine's map has) */
	for (model = 0; model < NUMBER_OF_H2_PLAYER_MODELS; model++)
	{
		if (identity_of(model) == identity && h2_player_models_biped(model) != NONE)
			return model;
	}
	return NONE;
}

long h2_player_models_biped(
	short model)
{
	long biped;
	char const *owner;

	if (model < 0 || model >= NUMBER_OF_H2_PLAYER_MODELS)
		return NONE;
	biped = tag_loaded('bipd', h2_player_model_table[model].biped);
	/* (a look's pack brings this biped to a Halo CE level, only to be drawn
	(player_looks.h): it is not the level's. Taken for it, the next player to
	spawn would play it, on a graph cut down to a preview's idle) */
	owner = biped != NONE ? scene_packs_owner(biped) : NULL;
	if (owner && (!strcmp(owner, PLAYER_LOOK_PACK_H2_SPARTAN) || !strcmp(owner, PLAYER_LOOK_PACK_H2_ELITE)))
		return NONE;
	return biped;
}

short h2_player_models_regions(
	short model,
	char (*names)[H2_PLAYER_MODEL_NAME_SIZE],
	short maximum)
{
	long tag = model_tag(model);
	struct model const *definition;
	short count = 0;
	long r;

	if (tag == NONE || !(definition = model_definition_get(tag)))
		return 0;
	for (r = 0; r < definition->regions.count && count < maximum; r++)
	{
		struct model_region const *region = TAG_BLOCK_GET_ELEMENT(&definition->regions, r, struct model_region);

		snprintf(names[count++], H2_PLAYER_MODEL_NAME_SIZE, "%s", region->name);
	}
	return count;
}

short h2_player_models_permutations(
	short model,
	char const *region_name,
	char (*names)[H2_PLAYER_MODEL_NAME_SIZE],
	short maximum)
{
	long tag = model_tag(model);
	struct model const *definition;
	short count = 0;
	long r, p;

	if (tag == NONE || !(definition = model_definition_get(tag)))
		return 0;
	for (r = 0; r < definition->regions.count; r++)
	{
		struct model_region const *region = TAG_BLOCK_GET_ELEMENT(&definition->regions, r, struct model_region);

		if (strcmp(region->name, region_name))
			continue;
		for (p = 0; p < region->permutations.count && count < maximum; p++)
		{
			struct model_region_permutation const *permutation =
				TAG_BLOCK_GET_ELEMENT(&region->permutations, p, struct model_region_permutation);

			snprintf(names[count++], H2_PLAYER_MODEL_NAME_SIZE, "%s", permutation->name);
		}
	}
	return count;
}

boolean h2_player_models_get_look(
	short local_player_index,
	struct h2_player_look *look)
{
	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_LOOKS || !h2_player_models.set[local_player_index])
		return FALSE;
	*look = h2_player_models.looks[local_player_index];
	return TRUE;
}

boolean h2_player_models_set_look(
	short local_player_index,
	struct h2_player_look const *look)
{
	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_LOOKS || !look ||
		look->model < 0 || look->model >= NUMBER_OF_H2_PLAYER_MODELS ||
		look->region_count < 0 || look->region_count > H2_PLAYER_MODEL_MAXIMUM_REGIONS)
	{
		return FALSE;
	}
	h2_player_models.looks[local_player_index] = *look;
	h2_player_models.set[local_player_index] = TRUE;
	return TRUE;
}

boolean h2_player_models_remote_identity(
	long player_absolute_index,
	unsigned long identity)
{
	short model = h2_player_models_from_identity(identity);
	short i;

	if (!h2_player_models.remote_ready)
	{
		for (i = 0; i < MAXIMUM_PLAYERS; i++)
			h2_player_models.remote[i] = NONE;
		h2_player_models.remote_ready = TRUE;
	}
	if (player_absolute_index < 0 || player_absolute_index >= MAXIMUM_PLAYERS)
		return FALSE;
	/* (an identity this machine does not know: none - the map's unit) */
	h2_player_models.remote[player_absolute_index] = model;
	return model != NONE;
}

long h2_player_models_unit(
	long player_index,
	long unit_definition_index)
{
	struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;
	short model, i;
	long biped;

	if (!h2_player_models.remote_ready)
	{
		for (i = 0; i < MAXIMUM_PLAYERS; i++)
			h2_player_models.remote[i] = NONE;
		h2_player_models.remote_ready = TRUE;
	}

	/* (not a Halo 2 level's game: no model of the table in its map) */
	if (!player || h2_player_models_biped(H2_PLAYER_MODEL_SPARTAN) == NONE)
		return unit_definition_index;
	/* (the host decides - a network game's units are the server's, sent
to the clients as they are: every machine the same model; a client, or a
film, the map's unit) */
	if (game_connection() != _game_connection_local && game_connection() != _game_connection_network_server)
	{
		if (getenv("HALO_H2_DEBUG"))
			platform_log("h2 player models: player %ld: a network game (%d): the map's unit",
				(long)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index), (int)game_connection());
		return unit_definition_index;
	}
	if (player->local_player_index >= 0 && player->local_player_index < MAXIMUM_LOCAL_LOOKS &&
		h2_player_models.set[player->local_player_index])
	{
		model = h2_player_models.looks[player->local_player_index].model;
	}
	else if (h2_player_models.remote[DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) & (MAXIMUM_PLAYERS - 1)] != NONE)
	{
		/* (a remote player's, as its machine named it: checked) */
		model = h2_player_models.remote[DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) & (MAXIMUM_PLAYERS - 1)];
	}
	else
	{
		/* (a remote player's: the appearance its machine told this host,
		checked: emblem_net.c) */
		struct h2_emblem emblem;

		model = default_model(player_index, player->local_player_index != NONE);
		if (player->local_player_index == NONE && !bots_player_is_bot(player_index) &&
			h2_emblem_network_player(&player->network_player_data, &emblem))
		{
			model = emblem.model ? H2_PLAYER_MODEL_ELITE : H2_PLAYER_MODEL_SPARTAN;
		}
	}
	biped = h2_player_models_biped(model);
	if (getenv("HALO_H2_DEBUG"))
		platform_log("h2 player models: player %ld (local %d) spawns as the %s", (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index),
			(int)player->local_player_index, h2_player_models_name(model));
	return biped != NONE ? biped : unit_definition_index;
}

int h2_player_models_species_offered(
	void)
{
	return h2_player_models_biped(H2_PLAYER_MODEL_SPARTAN) != NONE;
}

short h2_player_models_local_species(
	short local_player_index)
{
	long player_index;
	struct player_datum *player;
	struct object_datum *object;
	short model;

	if (h2_player_models_biped(H2_PLAYER_MODEL_SPARTAN) == NONE)
		return NONE;
	/* (the unit it plays now, if it is one of the table's) */
	if (local_player_index >= 0 && local_player_index < MAXIMUM_LOCAL_LOOKS &&
		(player_index = local_player_get_player_index(local_player_index)) != NONE &&
		(player = player_get(player_index)) != NULL && player->unit_index != NONE &&
		(object = object_try_and_get(player->unit_index)) != NULL)
	{
		for (model = 0; model < NUMBER_OF_H2_PLAYER_MODELS; model++)
			if (object->definition_index == h2_player_models_biped(model))
				return model;
	}
	/* (else - dead, between spawns - the one it spawns as next) */
	if (local_player_index >= 0 && local_player_index < MAXIMUM_LOCAL_LOOKS && h2_player_models.set[local_player_index])
		return h2_player_models.looks[local_player_index].model;
	return chosen_model();
}

boolean h2_player_models_hud_colour(
	short local_player_index,
	float colour[3])
{
	/* (Halo 2's: the Elite's violet sampled from the owner's MCC
	screenshots, Oct 23 2026; the Spartan's blue an estimate - no
	reference yet: stand_ins.md) */
	static float const colours[NUMBER_OF_H2_PLAYER_MODELS][3] =
	{
		{ 0.45f, 0.66f, 0.96f },
		{ 0.78f, 0.53f, 0.86f },
	};
	short model = h2_player_models_local_species(local_player_index);

	if (model == NONE)
		return FALSE;
	colour[0] = colours[model][0];
	colour[1] = colours[model][1];
	colour[2] = colours[model][2];
	return TRUE;
}

void h2_player_models_update(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;

	if (!player_data || h2_player_models_biped(H2_PLAYER_MODEL_SPARTAN) == NONE)
		return;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct h2_player_look const *look;
		struct object_datum *object;
		struct object_definition const *definition;
		struct model const *model;
		short r;

		static struct h2_player_look bot_look;
		static int bot_look_read = -1;

		/* (tests: HALO_H2_BOT_LOOK="region=permutation ...": every bot's) */
		if (bot_look_read < 0)
		{
			char const *setting = getenv("HALO_H2_BOT_LOOK");
			char text[256], *word, *rest;

			bot_look_read = 0;
			memset(&bot_look, 0, sizeof(bot_look));
			if (setting)
			{
				snprintf(text, sizeof(text), "%s", setting);
				for (word = strtok_r(text, " ", &rest); word && bot_look.region_count < H2_PLAYER_MODEL_MAXIMUM_REGIONS;
					word = strtok_r(NULL, " ", &rest))
				{
					char *equals = strchr(word, '=');

					if (!equals)
						continue;
					*equals = 0;
					snprintf(bot_look.regions[bot_look.region_count], H2_PLAYER_MODEL_NAME_SIZE, "%s", word);
					snprintf(bot_look.permutations[bot_look.region_count], H2_PLAYER_MODEL_NAME_SIZE, "%s", equals + 1);
					bot_look.region_count++;
				}
				bot_look_read = bot_look.region_count > 0;
			}
		}
		if (player->unit_index == NONE || !(object = object_try_and_get(player->unit_index)))
			continue;
		if (player->local_player_index >= 0 && player->local_player_index < MAXIMUM_LOCAL_LOOKS &&
			h2_player_models.set[player->local_player_index])
		{
			look = &h2_player_models.looks[player->local_player_index];
		}
		else if (player->local_player_index == NONE && bot_look_read > 0 && game_connection() != _game_connection_network_client)
			look = &bot_look;
		else
			continue;
		definition = object_definition_get(object->definition_index);
		if (definition->object.model.index == NONE || !(model = model_definition_get(definition->object.model.index)))
			continue;
		/* its permutations by name, the region's place in the model */
		for (r = 0; r < look->region_count; r++)
		{
			long region_index, p;

			if (!look->permutations[r][0])
				continue;
			for (region_index = 0; region_index < model->regions.count && region_index < MAXIMUM_REGIONS_PER_OBJECT; region_index++)
			{
				struct model_region const *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index, struct model_region);

				if (strcmp(region->name, look->regions[r]))
					continue;
				for (p = 0; p < region->permutations.count; p++)
				{
					struct model_region_permutation const *permutation =
						TAG_BLOCK_GET_ELEMENT(&region->permutations, p, struct model_region_permutation);

					if (!strcmp(permutation->name, look->permutations[r]) && object->object.region_permutations[region_index] != p)
					{
						object->object.region_permutations[region_index] = (char)p;
						if (getenv("HALO_H2_DEBUG"))
							platform_log("h2 player models: local player %d's %s: %s", (int)player->local_player_index,
								region->name, permutation->name);
					}
				}
			}
		}
		/* (the secondary colour: change colour D, which the model shaders
		do not tint yet) */
		if (look->has_secondary)
		{
			object->object.base_change_colors[3].red = look->secondary[0];
			object->object.base_change_colors[3].green = look->secondary[1];
			object->object.base_change_colors[3].blue = look->secondary[2];
		}
	}
}

boolean h2_player_models_command(
	char const *arguments)
{
	char word[16];

	word[0] = 0;
	if (sscanf(arguments, "%15s", word) < 1)
		return FALSE;
	if (!strcmp(word, "list"))
	{
		short model;

		for (model = 0; model < NUMBER_OF_H2_PLAYER_MODELS; model++)
		{
			char regions[H2_PLAYER_MODEL_MAXIMUM_REGIONS][H2_PLAYER_MODEL_NAME_SIZE];
			short region_count = h2_player_models_regions(model, regions, H2_PLAYER_MODEL_MAXIMUM_REGIONS), r;

			platform_log("h2 player models: %d %s: identity %08lx, biped %08lx, %d regions", (int)model,
				h2_player_models_name(model), h2_player_models_identity(model), (unsigned long)h2_player_models_biped(model),
				(int)region_count);
			for (r = 0; r < region_count; r++)
			{
				char permutations[H2_PLAYER_MODEL_MAXIMUM_PERMUTATIONS][H2_PLAYER_MODEL_NAME_SIZE];
				short count = h2_player_models_permutations(model, regions[r], permutations, H2_PLAYER_MODEL_MAXIMUM_PERMUTATIONS), p;
				char line[400];
				int length = 0;

				line[0] = 0;
				for (p = 0; p < count && length < (int)sizeof(line) - 40; p++)
					length += snprintf(line + length, sizeof(line) - (size_t)length, "%s%s", p ? ", " : "", permutations[p]);
				platform_log("h2 player models:   %s: %s", regions[r], line);
			}
		}
		return TRUE;
	}
	if (!strcmp(word, "species"))
	{
		char species[16];

		species[0] = 0;
		if (sscanf(arguments, "%*s %15s", species) == 1 && (!strcmp(species, "spartan") || !strcmp(species, "elite")))
			setenv("HALO_H2_PLAYER_SPECIES", species, 1);
		platform_log("h2 player models: the chosen species: %s (local player 0 plays the %s)",
			h2_player_models_name(chosen_model()), h2_player_models_name(h2_player_models_local_species(0)));
		return TRUE;
	}
	if (!strcmp(word, "look"))
	{
		struct h2_player_look look;
		int local = 0, consumed = 0;
		char model_name[32], *text;
		char const *at;

		memset(&look, 0, sizeof(look));
		if (sscanf(arguments, "%15s %d %31s %n", word, &local, model_name, &consumed) < 3)
			return FALSE;
		look.model = !strcmp(model_name, "elite") ? H2_PLAYER_MODEL_ELITE : H2_PLAYER_MODEL_SPARTAN;
		for (at = arguments + consumed; *at && look.region_count < H2_PLAYER_MODEL_MAXIMUM_REGIONS;)
		{
			char pair[80];
			int used = 0;

			if (sscanf(at, "%79s %n", pair, &used) < 1 || !used)
				break;
			at += used;
			text = strchr(pair, '=');
			if (!text)
				continue;
			*text++ = 0;
			snprintf(look.regions[look.region_count], H2_PLAYER_MODEL_NAME_SIZE, "%s", pair);
			snprintf(look.permutations[look.region_count], H2_PLAYER_MODEL_NAME_SIZE, "%s", text);
			look.region_count++;
		}
		platform_log("h2 player models: local player %d: %s, %d regions chosen", local, h2_player_models_name(look.model),
			(int)look.region_count);
		return h2_player_models_set_look((short)local, &look);
	}
	return FALSE;
}
