/*
H2_PLAYER_MODELS.H

Halo 2's multiplayer player models in a game (h2_player_models.c): the
Spartan and the Elite that a Halo 2 level's builder made (h2_biped.c), which
of them each player plays, and each one's look - its armour permutations by
name and its colours - for the Halo 3-style CUSTOMIZATION screen to offer
(the screen itself comes later: this is its data and its calls).

Every machine of a game must agree on a player's model: a model is named
across the network by its identity (a hash of its name and its biped's tag
path, the same on every machine), and an identity a machine is given is
checked against this table and the map loaded before it is used - one it
does not know, or whose biped the map lacks, is the map's own multiplayer
unit (the Spartan on a Halo 2 level), never a tag index taken on trust.
*/

#ifndef __H2_PLAYER_MODELS_H
#define __H2_PLAYER_MODELS_H

#include "cseries.h"

/* ---------- constants */

enum
{
	H2_PLAYER_MODEL_SPARTAN = 0,
	H2_PLAYER_MODEL_ELITE,
	NUMBER_OF_H2_PLAYER_MODELS,

	H2_PLAYER_MODEL_MAXIMUM_REGIONS = 8,
	H2_PLAYER_MODEL_MAXIMUM_PERMUTATIONS = 16,
	H2_PLAYER_MODEL_NAME_SIZE = 32,
};

/* ---------- structures */

/* a player's look: the model, a permutation by name for each region named
(an empty name: the model's default), the colours (primary: the player's
own colour today; secondary: kept for when model shaders tint a second
colour - stand_ins.md) */
struct h2_player_look
{
	short model;
	char regions[H2_PLAYER_MODEL_MAXIMUM_REGIONS][H2_PLAYER_MODEL_NAME_SIZE];
	char permutations[H2_PLAYER_MODEL_MAXIMUM_REGIONS][H2_PLAYER_MODEL_NAME_SIZE];
	short region_count;
	boolean has_secondary;
	float secondary[3];
};

/* ---------- prototypes/H2_PLAYER_MODELS.C */

/* the table: a model's name as shown ("Spartan"), its identity (the same on
every machine), the index an identity names (NONE: not a model of the
table) */
char const *h2_player_models_name(
	short model);
unsigned long h2_player_models_identity(
	short model);
short h2_player_models_from_identity(
	unsigned long identity);

/* the loaded map's biped of a model (its tag index; NONE: the map has none) */
long h2_player_models_biped(
	short model);

/* a model's customization, as the loaded map's model tag has it: its
regions by name, and a region's permutations by name (the first: its
default); how many */
short h2_player_models_regions(
	short model,
	char (*names)[H2_PLAYER_MODEL_NAME_SIZE],
	short maximum);
short h2_player_models_permutations(
	short model,
	char const *region,
	char (*names)[H2_PLAYER_MODEL_NAME_SIZE],
	short maximum);

/* a local player's look (local player 0..3): read and set (taken at its
next spawn, its permutations kept on its unit) */
boolean h2_player_models_get_look(
	short local_player_index,
	struct h2_player_look *look);
boolean h2_player_models_set_look(
	short local_player_index,
	struct h2_player_look const *look);

/* (the host, as the netcode hears it from a player's machine) the model a
remote player asked for, by identity: kept if this machine knows it and
its map has its biped (TRUE), else none - the map's unit */
boolean h2_player_models_remote_identity(
	long player_absolute_index,
	unsigned long identity);

/* (players.c through player_models_unit) the unit a player spawns as, on
the machine that decides (a local game's, a network game's host - whose
units the clients are sent as they are): a local player's look's model,
else its chosen species (h2_player_models_local_species); a
remote player's as its identity named it (h2_player_models_remote_identity);
a bot's by its place (HALO_H2_SPECIES: "spartan", "elite", "mixed" - bots
alternate - the default); else unit_definition_index */
long h2_player_models_unit(
	long player_index,
	long unit_definition_index);

/* a local player's species (H2_PLAYER_MODEL_SPARTAN or _ELITE): the unit
it plays now, else the one it spawns as next (its look, else the settings
panel's "Halo 2 species", HALO_H2_PLAYER_SPECIES); NONE on a level that is
not Halo 2's. For the HUD (Elite violet, Spartan blue) and the first-person
arms (the weapons' branch) */
short h2_player_models_local_species(
	short local_player_index);

/* (the settings panel's "Halo 2 species" row: shown) a Halo 2 level is
loaded */
int h2_player_models_species_offered(
	void);

/* a local player's HUD colour by its species (0..1 RGB): FALSE (colour
untouched) on a level that is not Halo 2's */
boolean h2_player_models_hud_colour(
	short local_player_index,
	float colour[3]);

/* (the top of a frame, through player_models_update) each local player's
unit given its look's permutations */
void h2_player_models_update(
	void);

/* (the console's "models h2 ...", player_models_command) "list": the
models, their identities, regions and permutations; "look <local player>
<spartan|elite> [region=permutation...]": a local player's look; "species
<spartan|elite>": the chosen species, as the settings panel sets it. TRUE if
understood */
boolean h2_player_models_command(
	char const *arguments);

#endif
