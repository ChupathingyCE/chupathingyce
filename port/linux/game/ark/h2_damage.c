/*
H2_DAMAGE.C

port: Halo 2's damage model on a Halo 2 level (h2_damage.h says what is
where). Halo 2's numbers come from the player's own MCC maps: a model's
damage info (hlmt), the globals' damage table and global materials (matg),
a damage effect's damage groups (jpt!). The rules are the upstream
decompilation's (damage.cpp: function_d5bc0 the table, function_d9110 the
shield, function_d8cb0 the body, function_d5de0 the recharge), reimplemented
on this engine's damage.c. A vehicle with a living rider does not die
(function_d8cb0's riders rule: function_db110): its body held at none, it
dies once its last rider dies in it (function_d6800) and is whole again to
a quarter once its last rider gets out alive (function_db670).

HALO_H2_DAMAGE_LOG: each damage a Halo 2 level's biped takes, logged (its
damage effect, shield and body before and after).
*/

#include "cseries.h"
#include "game/game_globals.h"
#include "objects/damage.h"
#include "objects/damage_effect_definitions.h"
#include "objects/object_definitions.h"
#include "objects/objects.h"
#include "physics/collision_model_definitions.h"
#include "scenario/scenario.h"
#include "tag_files/tag_groups.h"
#include "units/unit_definitions.h"
#include "units/units.h"

#include "h2_cache.h"
#include "h2_damage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HALO_LINUX
#include "render_epoch.h" /* HALO_ENV_ONCE */
#endif

#define H2_ALIGN __attribute__((force_align_arg_pointer))

void platform_log(char const *format, ...);
extern unsigned long halo_map_generation;

/* ---------- constants */

enum
{
	/* the hlmt: its damage info block (0xF8 an element) */
	H2D_HLMT_DAMAGE_INFO = 0x60,
	H2D_DAMAGE_INFO_SIZE = 0xF8,
	/* the bipd/vehi: its model */
	H2D_OBJECT_MODEL = 0x34,
	/* the globals: the damage table (a block of one: its groups' block),
	the global materials (0xB4 each: name, general armor 0x10, specific
	armor 0x14) */
	H2D_GLOBALS_DAMAGE_TABLE = 0xD0,
	H2D_GLOBALS_MATERIALS = 0x150,
	H2D_GLOBAL_MATERIAL_SIZE = 0xB4,
	/* a damage effect's damage groups (general, specific) */
	H2D_DAMAGE_GENERAL = 0x50,
	H2D_DAMAGE_SPECIFIC = 0x54,
	/* (the game) vehicles held alive by their riders at once, and the
	riders of one counted */
	H2D_MAXIMUM_HELD = 32,
	H2D_MAXIMUM_RIDERS = 16,
};

/* each species' biped and the material types this engine's tags give its
body and shield (the Elite's are the stock Elite's - its blood and its
shield's hits, h2_biped.c and h2_effects.c; the Spartan's the stock
cyborg's); a damage effect made from Halo 2's has Halo 2's multipliers
for these, the species' own armor's */
static struct
{
	char const *name;
	char const *biped;
	short material_types[NUMBER_OF_H2_DAMAGE_LAYERS];
} const h2d_species[NUMBER_OF_H2_DAMAGE_SPECIES] =
{
	{ "spartan", "objects\\characters\\masterchief\\masterchief_mp", { _material_cyborg, _material_cyborg_energy_shield } },
	{ "elite", "objects\\characters\\elite\\elite_mp", { _material_elite, _material_elite_energy_shield } },
};

/* the armor classes of Halo 2's characters that are none of the species
(the Hunter, Brute, Grunt, Jackal, Marine, Drone, Sentinel, Enforcer, Flood,
the campaign's Elite and Prophets...): a class is one pair of armors of the
damage table (a global material's general and specific armor, damage.cpp
function_d5bc0), each named here by a global material that has it. The
level builder gives each collision material of such a character its class
(h2_biped.c, damage_resistance_material.pad = class + 1) and each damage
effect made from Halo 2's the damage table's multiplier for every class
(struct h2_class_modifiers, damage.c): this engine's material types are too
few for them (the surfaces, species and vehicles hold most). The classes
are the pairs the characters' hlmts name, 18 of the 24 */
static char const *const h2d_class_materials[] =
{
	"energy_shield_thin_cov",                   /* 0: the Elite's, Prophets', Master Chief's shields */
	"soft_organic_flesh_grunt",                 /* 1: Grunt, Jackal, Marine, Drone flesh */
	"soft_tough_armor_hum_kevlar",              /* 2: the Marine's armor: no armor, x1 */
	"hard_metal_thick_for",                     /* 3: the Enforcer's metal */
	"tough_organic_flesh_brute",                /* 4: Brute flesh and armor */
	"energy_holo_cov",                          /* 5: holograms */
	"hard_metal_thin_cov_elite",                /* 6: Elite, Marine, Prophet armor */
	"tough_organic_flesh_brute_tartarus",       /* 7 */
	"hard_metal_thin_cov_grunt",                /* 8: the Grunt's pack */
	"tough_floodflesh",                         /* 9: Flood combat, carrier and infection forms */
	"hard_floodflesh_juggernaut",               /* 10 */
	"energy_shield_thick_cov_jackal",           /* 11: the Jackal's shield */
	"hard_metal_thin_for_sentinel_enforcer",    /* 12 */
	"energy_shield_invincible",                 /* 13 */
	"brittle_elec_for_sentinel",                /* 14 */
	"hard_metal_solid_cov_hunter",              /* 15: the Hunter's armor */
	"soft_organic_flesh_hunter",                /* 16: the Hunter's belly and back */
	"hard_metal_thin_for_sentinel",             /* 17: the Sentinels' metal */
};

/* the material type of this engine's a class's collision material takes for
its hit effects (the Hunter's own; NONE: the stock model's) */
short h2_damage_class_effect_type(short armor_class)
{
	return armor_class == 15 ? _material_hunter_armor : armor_class == 16 ? _material_hunter_skin : NONE;
}

/* Halo 2's vehicles' armor (their hlmt's damage info's material, the damage
table's armor by it) on this engine's: each vehicle's collision materials
take one material type (h2_vehicles.c), its own, whose material modifiers a
damage effect made from Halo 2's has the damage table's multiplier for. The
material types are ones no Halo 2 level's object has otherwise (the
Scorpion's and the Wraith's armor is Halo 2's solid metal, which bullets,
plasma and melee do not hurt and grenades a quarter; the others' thick
metal); two vehicles share a type only when their armor is the same (the
Banshee and Spectre the Ghost's: h2_damage_modifiers_write checks) */
static struct
{
	char const *name;           /* (h2_vehicles.c's netgame name, its beginning) */
	char const *model;          /* its hlmt */
	short material_type;
} const h2d_vehicles[] =
{
	{ "warthog", "objects\\vehicles\\warthog\\warthog", _material_thick_metal },
	{ "ghost", "objects\\vehicles\\ghost\\ghost", _material_hollow_metal },
	{ "h_turret_ap", "objects\\vehicles\\h_turret_ap\\h_turret_ap", _material_thin_metal },
	{ "h_turret_mp", "objects\\weapons\\fixed\\h_turret_mp\\h_turret_mp", _material_thin_metal },
	{ "scorpion", "objects\\vehicles\\scorpion\\scorpion", _material_sentinel },
	{ "wraith", "objects\\vehicles\\wraith\\wraith", _material_monitor },
	{ "banshee", "objects\\vehicles\\banshee\\banshee", _material_hollow_metal },
	{ "spectre", "objects\\vehicles\\spectre\\spectre", _material_hollow_metal },
	{ "c_turret_mp", "objects\\weapons\\fixed\\plasma_cannon\\plasma_cannon", _material_hunter_shield },
	{ "c_turret_ap", "objects\\vehicles\\c_turret_ap\\c_turret_ap", _material_engineer_force_field },
	{ "creep", "objects\\vehicles\\creep\\creep", _material_hollow_metal },
	{ "cov_guntower_turret", "scenarios\\objects\\covenant\\military\\cov_guntower\\cov_guntower_turret\\cov_guntower_turret",
		_material_hunter_shield },
};

/* ---------- private code (the builder) */

H2_ALIGN static float h2d_real(struct h2_cache *cache, unsigned long address)
{
	unsigned long bits = h2_cache_read32(cache, address);
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

H2_ALIGN static unsigned long h2d_globals(struct h2_cache *cache)
{
	long matg = h2_cache_tag_find(cache, 'matg', "globals\\globals");
	struct h2_cache_tag t;

	return matg != NONE && h2_cache_tag_get(cache, matg, &t) ? t.address : 0;
}

/* a global material's (by name) armor: its general and specific armor's
string ids; FALSE: no such material */
H2_ALIGN static boolean h2d_armor(struct h2_cache *cache, unsigned long globals, unsigned long material, unsigned long armor[2])
{
	long count, i;
	unsigned long elements;

	if (!globals || !h2_cache_block(cache, globals + H2D_GLOBALS_MATERIALS, H2D_GLOBAL_MATERIAL_SIZE, &count, &elements))
		return FALSE;
	for (i = 0; i < count; i++)
	{
		unsigned long at = elements + H2D_GLOBAL_MATERIAL_SIZE * i;

		if (h2_cache_read32(cache, at) == material)
		{
			armor[0] = h2_cache_read32(cache, at + 0x10);
			armor[1] = h2_cache_read32(cache, at + 0x14);
			return TRUE;
		}
	}
	return FALSE;
}

/* the damage table's multiplier for damage groups on armors (damage.cpp's
function_d5bc0: each group's modifier for each armor, multiplied) */
H2_ALIGN static float h2d_table(struct h2_cache *cache, unsigned long globals, unsigned long const groups[2], unsigned long const armor[2])
{
	long table_count, group_count, i, j, k;
	unsigned long table, group_elements;
	float result = 1.0f;

	if (!h2_cache_block(cache, globals + H2D_GLOBALS_DAMAGE_TABLE, 0x8, &table_count, &table) || table_count < 1 ||
		!h2_cache_block(cache, table, 0xC, &group_count, &group_elements))
	{
		return 1.0f;
	}
	for (i = 0; i < 2; i++)
	{
		for (j = 0; j < group_count; j++)
		{
			unsigned long group = group_elements + 0xC * j;
			long modifier_count;
			unsigned long modifiers;

			if (h2_cache_read32(cache, group) != groups[i])
				continue;
			if (h2_cache_block(cache, group + 4, 0x8, &modifier_count, &modifiers))
			{
				for (k = 0; k < 2; k++)
				{
					long m;

					for (m = 0; m < modifier_count; m++)
					{
						if (h2_cache_read32(cache, modifiers + 0x8 * m) == armor[k])
						{
							result *= h2d_real(cache, modifiers + 0x8 * m + 4);
							break;
						}
					}
				}
			}
			break;
		}
	}
	return result;
}

/* ---------- public code (the builder) */

char const *h2_damage_species_biped(short species)
{
	return species >= 0 && species < NUMBER_OF_H2_DAMAGE_SPECIES ? h2d_species[species].biped : "";
}

short h2_damage_species_named(char const *name)
{
	short s;

	for (s = 0; name && s < NUMBER_OF_H2_DAMAGE_SPECIES; s++)
	{
		if (!strcmp(name, h2d_species[s].name))
			return s;
	}
	return NONE;
}

short h2_damage_player_material_type(short species, short layer)
{
	if (species < 0 || species >= NUMBER_OF_H2_DAMAGE_SPECIES || layer < 0 || layer >= NUMBER_OF_H2_DAMAGE_LAYERS)
		return NONE;
	return h2d_species[species].material_types[layer];
}

H2_ALIGN boolean h2_damage_info_read(struct h2_cache *cache, long object_tag, struct h2_damage_info *info)
{
	struct h2_cache_tag t, model;
	long model_tag, count;
	unsigned long elements;

	memset(info, 0, sizeof(*info));
	if (object_tag == NONE || !h2_cache_tag_get(cache, object_tag, &t) ||
		(model_tag = h2_cache_reference(cache, t.address + H2D_OBJECT_MODEL)) == NONE || !h2_cache_tag_get(cache, model_tag, &model) ||
		!h2_cache_block(cache, model.address + H2D_HLMT_DAMAGE_INFO, H2D_DAMAGE_INFO_SIZE, &count, &elements) || count < 1)
	{
		return FALSE;
	}
	info->body_material = h2_cache_read32(cache, elements + 0x04);
	info->body_vitality = h2d_real(cache, elements + 0x28);
	info->body_minimum_stun_damage = h2d_real(cache, elements + 0x2C);
	info->body_stun_time = h2d_real(cache, elements + 0x30);
	info->body_recharge_time = h2d_real(cache, elements + 0x34);
	info->body_recharge_fraction = h2d_real(cache, elements + 0x38);
	info->shield_vitality = h2d_real(cache, elements + 0x8C);
	info->shield_material = h2_cache_read32(cache, elements + 0x90);
	info->shield_minimum_stun_damage = h2d_real(cache, elements + 0x94);
	info->shield_stun_time = h2d_real(cache, elements + 0x98);
	info->shield_recharge_time = h2d_real(cache, elements + 0x9C);
	info->found = info->body_vitality > 0.0f;
	return info->found;
}

/* the flags of the damage section of a Halo 2 object's model's material
(by its string id): the hlmt's materials at 0x58 (0x14 each: name, the
section at 6), its damage info's sections at 0xBC (0x38 each: flags at 4 -
0x80 headshotable, damage.cpp function_d8cb0's head test; 0x100 ignores
shields, function_d9110 skipped) */
H2_ALIGN boolean h2_damage_material_section_flags(
	struct h2_cache *cache,
	long object_tag,
	unsigned long material_name,
	short *section_out,
	unsigned long *flags)
{
	struct h2_cache_tag t, model;
	long model_tag, material_count, info_count, section_count, i;
	unsigned long materials, info, sections;

	*section_out = NONE;
	*flags = 0;
	if (!h2_cache_tag_get(cache, object_tag, &t) ||
		(model_tag = h2_cache_reference(cache, t.address + H2D_OBJECT_MODEL)) == NONE || !h2_cache_tag_get(cache, model_tag, &model) ||
		!h2_cache_block(cache, model.address + 0x58, 0x14, &material_count, &materials) ||
		!h2_cache_block(cache, model.address + H2D_HLMT_DAMAGE_INFO, H2D_DAMAGE_INFO_SIZE, &info_count, &info) || info_count < 1 ||
		!h2_cache_block(cache, info + 0xBC, 0x38, &section_count, &sections))
	{
		return FALSE;
	}
	for (i = 0; i < material_count; i++)
	{
		if (h2_cache_read32(cache, materials + 0x14 * i) == material_name)
		{
			short section = (short)(h2_cache_read32(cache, materials + 0x14 * i + 4) >> 16);

			if (section < 0 || section >= section_count)
				return FALSE;
			*section_out = section;
			*flags = h2_cache_read32(cache, sections + 0x38 * section + 4);
			return TRUE;
		}
	}
	return FALSE;
}

/* whether a Halo 2 object's model's damage section of a material (by its
string id) is flagged "ignores shields" (0x100). The Jackal's body and head,
Regret's: a hit there goes to the body, whatever shield the object has; that
shield is for the sections without it (the Jackal's arm shield) */
H2_ALIGN boolean h2_damage_material_ignores_shields(
	struct h2_cache *cache,
	long object_tag,
	unsigned long material_name)
{
	short section;
	unsigned long flags;

	return h2_damage_material_section_flags(cache, object_tag, material_name, &section, &flags) && (flags & 0x100) != 0;
}

/* a Halo 2 model's (hlmt) damage info */
H2_ALIGN static boolean h2d_model_info_read(struct h2_cache *cache, long model_tag, struct h2_damage_info *info)
{
	struct h2_cache_tag model;
	long count;
	unsigned long elements;

	memset(info, 0, sizeof(*info));
	if (model_tag == NONE || !h2_cache_tag_get(cache, model_tag, &model) ||
		!h2_cache_block(cache, model.address + H2D_HLMT_DAMAGE_INFO, H2D_DAMAGE_INFO_SIZE, &count, &elements) || count < 1)
	{
		return FALSE;
	}
	info->body_material = h2_cache_read32(cache, elements + 0x04);
	info->body_vitality = h2d_real(cache, elements + 0x28);
	info->shield_vitality = h2d_real(cache, elements + 0x8C);
	info->shield_material = h2_cache_read32(cache, elements + 0x90);
	info->found = info->body_vitality > 0.0f;
	return info->found;
}

H2_ALIGN float h2_damage_multiplier(struct h2_cache *cache, long damage_tag, unsigned long global_material)
{
	unsigned long globals = h2d_globals(cache), groups[2], armor[2];
	struct h2_cache_tag d;

	if (!globals || damage_tag == NONE || !h2_cache_tag_get(cache, damage_tag, &d) || !h2d_armor(cache, globals, global_material, armor))
		return 1.0f;
	groups[0] = h2_cache_read32(cache, d.address + H2D_DAMAGE_GENERAL);
	groups[1] = h2_cache_read32(cache, d.address + H2D_DAMAGE_SPECIFIC);
	return h2d_table(cache, globals, groups, armor);
}

H2_ALIGN static unsigned long h2d_material_named(struct h2_cache *cache, char const *name);

/* a global material's armors (string ids) */
H2_ALIGN static boolean h2d_class_armor(struct h2_cache *cache, unsigned long material, unsigned long armor[2])
{
	return h2d_armor(cache, h2d_globals(cache), material, armor);
}

/* a global material's named by its string id: its class */
H2_ALIGN short h2_damage_armor_class(struct h2_cache *cache, unsigned long global_material)
{
	unsigned long armor[2], representative[2];
	long k;

	if (!global_material || !h2d_class_armor(cache, global_material, armor))
		return NONE;
	for (k = 0; k < (long)NUMBEROF(h2d_class_materials); k++)
	{
		unsigned long id = h2d_material_named(cache, h2d_class_materials[k]);

		if (id && h2d_class_armor(cache, id, representative) && representative[0] == armor[0] && representative[1] == armor[1])
			return (short)k;
	}
	return NONE;
}

short h2_damage_vehicle_material_type(char const *name)
{
	long v;

	for (v = 0; name && v < (long)NUMBEROF(h2d_vehicles); v++)
	{
		if (!strncmp(name, h2d_vehicles[v].name, strlen(h2d_vehicles[v].name)))
			return h2d_vehicles[v].material_type;
	}
	return NONE;
}

/* a global material's string id by its name; 0: none */
H2_ALIGN static unsigned long h2d_material_named(struct h2_cache *cache, char const *name)
{
	unsigned long globals = h2d_globals(cache), elements;
	long count, i;
	char text[64];

	if (!globals || !h2_cache_block(cache, globals + H2D_GLOBALS_MATERIALS, H2D_GLOBAL_MATERIAL_SIZE, &count, &elements))
		return 0;
	for (i = 0; i < count; i++)
	{
		unsigned long id = h2_cache_read32(cache, elements + H2D_GLOBAL_MATERIAL_SIZE * i);

		if (h2_cache_string(cache, id, text, sizeof(text)) && !strcmp(text, name))
			return id;
	}
	return 0;
}

H2_ALIGN boolean h2_damage_modifiers_write(struct h2_cache *cache, long damage_tag, void *modifiers,
	void (*write_real)(void *at, float value))
{
	short s, layer;
	long v;
	boolean written = FALSE;
	struct h2_cache_tag d;

	if (damage_tag == NONE || !h2d_globals(cache) || getenv("HALO_H2_DAMAGE_AS_IS"))
		return FALSE;
	/* (its riders' scales: a vehicle's riders' share of it, h2_damage.h) */
	if (h2_cache_tag_get(cache, damage_tag, &d))
	{
		write_real((char *)modifiers + sizeof(float) * H2_DAMAGE_RIDER_SLOT, H2_DAMAGE_RIDER_MAGIC);
		write_real((char *)modifiers + sizeof(float) * (H2_DAMAGE_RIDER_SLOT + 1), h2d_real(cache, d.address + 0x44));
		write_real((char *)modifiers + sizeof(float) * (H2_DAMAGE_RIDER_SLOT + 2), h2d_real(cache, d.address + 0x48));
		write_real((char *)modifiers + sizeof(float) * (H2_DAMAGE_RIDER_SLOT + 3), h2d_real(cache, d.address + 0x4C));
	}
	/* (held props' metal: Zanzibar's crane pin, its awnings' supports) */
	write_real((char *)modifiers + sizeof(float) * H2_DAMAGE_THICK_METAL_SLOT,
		h2_damage_multiplier(cache, damage_tag, h2d_material_named(cache, "hard_metal_thick")));
	write_real((char *)modifiers + sizeof(float) * H2_DAMAGE_THIN_METAL_SLOT,
		h2_damage_multiplier(cache, damage_tag, h2d_material_named(cache, "hard_metal_thin")));
	/* (the vehicles' armor; a type two vehicles share has the first's) */
	for (v = 0; v < (long)NUMBEROF(h2d_vehicles); v++)
	{
		struct h2_damage_info info;
		float multiplier;
		long w;

		if (!h2d_model_info_read(cache, h2_cache_tag_find(cache, 'hlmt', h2d_vehicles[v].model), &info))
			continue;
		multiplier = h2_damage_multiplier(cache, damage_tag, info.body_material);
		for (w = 0; w < v && h2d_vehicles[w].material_type != h2d_vehicles[v].material_type; w++)
			;
		if (w < v)
		{
			struct h2_damage_info first;

			if (h2d_model_info_read(cache, h2_cache_tag_find(cache, 'hlmt', h2d_vehicles[w].model), &first) &&
				h2_damage_multiplier(cache, damage_tag, first.body_material) != multiplier)
			{
				platform_log("h2 damage: %s on the %s: x%.2f, but its material type the %s's (x%.2f)", h2_cache_tag_name(cache, damage_tag),
					h2d_vehicles[v].name, multiplier, h2d_vehicles[w].name, h2_damage_multiplier(cache, damage_tag, first.body_material));
			}
			if (first.found)
				continue;
		}
		write_real((char *)modifiers + sizeof(float) * h2d_vehicles[v].material_type, multiplier);
		if (getenv("HALO_H2_DEBUG"))
			platform_log("h2 damage: %s on the %s: x%.2f", h2_cache_tag_name(cache, damage_tag), h2d_vehicles[v].name, multiplier);
	}
	/* (the characters' armor classes: the damage effect's own bytes, struct
	h2_class_modifiers, past the modifiers' place in the definition) */
	{
		unsigned short *classes = (unsigned short *)((char *)modifiers - offsetof(struct damage_effect_definition, damage.material_modifiers) +
			H2_CLASS_MODIFIERS_OFFSET + offsetof(struct h2_class_modifiers, multiplier));
		unsigned long magic = H2_CLASS_MODIFIERS_MAGIC;
		long k;

		memcpy((char *)classes - offsetof(struct h2_class_modifiers, multiplier), &magic, sizeof(magic));
		for (k = 0; k < (long)NUMBEROF(h2d_class_materials); k++)
		{
			unsigned long material = h2d_material_named(cache, h2d_class_materials[k]);
			float multiplier = material ? h2_damage_multiplier(cache, damage_tag, material) : 1.0f;
			long fixed = (long)(multiplier * 1024.0f + 0.5f);

			classes[k] = (unsigned short)(fixed < 0 ? 0 : fixed > 65535 ? 65535 : fixed);
			if (HALO_ENV_ONCE("HALO_H2_DEBUG"))
				platform_log("h2 damage: %s on class %ld (%s): x%.2f", h2_cache_tag_name(cache, damage_tag), k, h2d_class_materials[k], multiplier);
		}
		written = TRUE;
	}
	for (s = 0; s < NUMBER_OF_H2_DAMAGE_SPECIES; s++)
	{
		struct h2_damage_info info;

		if (!h2_damage_info_read(cache, h2_cache_tag_find(cache, 'bipd', h2d_species[s].biped), &info))
			continue;
		for (layer = 0; layer < NUMBER_OF_H2_DAMAGE_LAYERS; layer++)
		{
			unsigned long material = layer == _h2_damage_body ? info.body_material : info.shield_material;
			float multiplier = h2_damage_multiplier(cache, damage_tag, material);

			write_real((char *)modifiers + sizeof(float) * h2d_species[s].material_types[layer], multiplier);
			written = TRUE;
			if (getenv("HALO_H2_DEBUG"))
				platform_log("h2 damage: %s on the %s's %s: x%.2f", h2_cache_tag_name(cache, damage_tag), h2d_species[s].name,
					layer == _h2_damage_body ? "body" : "shield", multiplier);
		}
	}
	return written;
}

H2_ALIGN boolean h2_damage_effect_write(struct h2_cache *cache, long damage_tag, void *definition,
	void (*write_real)(void *at, float value))
{
	struct h2_cache_tag d;
	float lower, upper_minimum, upper;

	if (damage_tag == NONE || !h2_cache_tag_get(cache, damage_tag, &d) || getenv("HALO_H2_DAMAGE_AS_IS"))
		return FALSE;
	/* (Halo 2's lower bound is the damage at no scale - the far end of a
	falloff, an explosion's edge -, its upper bound the random range at full
	scale: this engine's minimum and lower..upper bounds, damage.c's
	(1 - scale) * minimum + scale * random(lower, upper)) */
	lower = h2d_real(cache, d.address + 0x1C);
	upper_minimum = h2d_real(cache, d.address + 0x20);
	upper = h2d_real(cache, d.address + 0x24);
	if (upper_minimum > upper || upper_minimum <= 0.0f)
		upper_minimum = upper;
	write_real((char *)definition + offsetof(struct damage_effect_definition, damage.damage_minimum), lower);
	write_real((char *)definition + offsetof(struct damage_effect_definition, damage.damage_lower_bound), upper_minimum);
	write_real((char *)definition + offsetof(struct damage_effect_definition, damage.damage_upper_bound), upper);
	h2_damage_modifiers_write(cache, damage_tag, (char *)definition + offsetof(struct damage_effect_definition, damage.material_modifiers),
		write_real);
	platform_log("h2 damage: %s: damage %.0f..%.0f", h2_cache_tag_name(cache, damage_tag), lower, upper);
	return TRUE;
}

/* ---------- private code (the game) */

static struct
{
	long scenario;
	unsigned long generation;
	boolean halo2;
	boolean log;
	boolean transferring;       /* (a vehicle's damage passed to its riders: no direct scale again) */
	struct
	{
		long object;
		short stun_ticks;
	} bodies[MAXIMUM_OBJECTS_PER_MAP];
	/* vehicles whose body ran out with riders in them, alive until their
	last rider goes (damage.cpp function_d8cb0's riders rule): each one's
	living riders as last seen */
	struct
	{
		long vehicle;
		short rider_count;
		long riders[H2D_MAXIMUM_RIDERS];
	} held[H2D_MAXIMUM_HELD];
} h2d = { NONE };

static void h2d_level(void)
{
	char const *name;
	long i;

	if (h2d.scenario == global_scenario_index && h2d.generation == halo_map_generation)
		return;
	h2d.scenario = global_scenario_index;
	h2d.generation = halo_map_generation;
	name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : "";
	h2d.halo2 = name && !strncmp(name, "h2\\", 3) && !getenv("HALO_H2_DAMAGE_AS_IS");
	h2d.log = getenv("HALO_H2_DAMAGE_LOG") != NULL;
	for (i = 0; i < MAXIMUM_OBJECTS_PER_MAP; i++)
	{
		h2d.bodies[i].object = NONE;
		h2d.bodies[i].stun_ticks = 0;
	}
	for (i = 0; i < H2D_MAXIMUM_HELD; i++)
		h2d.held[i].vehicle = NONE;
}

/* a vehicle's living riders - units in its seats, and in its children's
(a turret's gunner) - up to `maximum` of them in riders; how many
(damage.cpp function_db110: any unit seated in it or what it carries) */
static short h2d_riders(long vehicle_index, long *riders, short count, short maximum)
{
	struct object_datum *vehicle = object_try_and_get(vehicle_index);
	long child_index = vehicle ? vehicle->object.first_child_object_index : NONE;

	while (child_index != NONE)
	{
		struct object_datum *child = object_get(child_index);

		if (TEST_FLAG(_object_mask_unit, child->object.type) && ((struct unit_datum *)child)->unit.parent_seat_index != NONE &&
			!TEST_FLAG(child->object.damage_flags, _object_dead_bit) && child->object.body_vitality > 0.0f)
		{
			if (count < maximum)
				riders[count] = child_index;
			count++;
		}
		if (child->object.type == _object_type_vehicle)
			count = h2d_riders(child_index, riders, count, maximum);
		child_index = child->object.next_object_index;
	}
	return count;
}

static long h2d_held_slot(long vehicle_index, boolean add)
{
	long i, free_slot = NONE;

	for (i = 0; i < H2D_MAXIMUM_HELD; i++)
	{
		if (h2d.held[i].vehicle == vehicle_index)
			return i;
		if (free_slot == NONE && (h2d.held[i].vehicle == NONE || !object_try_and_get(h2d.held[i].vehicle)))
			free_slot = i;
	}
	if (add && free_slot != NONE)
	{
		h2d.held[free_slot].vehicle = vehicle_index;
		h2d.held[free_slot].rider_count = 0;
	}
	return add ? free_slot : NONE;
}

/* a held vehicle each tick: its riders as they are; once the last is gone,
dead it dies, out alive it is whole again to a quarter (function_d6800's
last rider's death, function_db670's last rider's exit) */
static void h2d_held_update(long vehicle_index)
{
	long slot = h2d_held_slot(vehicle_index, FALSE), riders[H2D_MAXIMUM_RIDERS];
	struct object_datum *vehicle = object_try_and_get(vehicle_index);
	short count, k;
	boolean died = FALSE;

	if (slot == NONE)
		return;
	if (!vehicle || TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) || vehicle->object.body_vitality > 0.0f)
	{
		h2d.held[slot].vehicle = NONE;
		return;
	}
	count = h2d_riders(vehicle_index, riders, 0, H2D_MAXIMUM_RIDERS);
	if (count > 0)
	{
		h2d.held[slot].rider_count = MIN(count, H2D_MAXIMUM_RIDERS);
		csmemcpy(h2d.held[slot].riders, riders, sizeof(long) * h2d.held[slot].rider_count);
		return;
	}
	/* (the last gone: dead, or out alive) */
	for (k = 0; k < h2d.held[slot].rider_count; k++)
	{
		struct object_datum *rider = object_try_and_get(h2d.held[slot].riders[k]);

		if (!rider || TEST_FLAG(rider->object.damage_flags, _object_dead_bit) || rider->object.body_vitality <= 0.0f)
			died = TRUE;
	}
	h2d.held[slot].vehicle = NONE;
	if (died)
	{
		extern void h2_vehicle_damage_body(long vehicle_index, real damage, real_point3d const *point, boolean area, long owner_player_index);

		if (h2d.log)
			platform_log("h2 damage: %08lx's last rider died: it dies", (unsigned long)vehicle_index);
		vehicle->object.body_vitality = -0.001f;
		h2_vehicle_damage_body(vehicle_index, 0.0f, &vehicle->object.bounding_sphere_center, TRUE, NONE);
		object_deplete_body(vehicle_index);
	}
	else
	{
		if (vehicle->object.body_vitality < 0.25f)
			vehicle->object.body_vitality = 0.25f;
		if (h2d.log)
			platform_log("h2 damage: %08lx's last rider got out: its body back to %.2f", (unsigned long)vehicle_index,
				vehicle->object.body_vitality);
	}
}

/* the object's collision model's regeneration (NULL: none) */
static struct damage_resistance const *h2d_regenerates(long object_index, struct object_datum **out)
{
	struct object_datum *object = object_try_and_get(object_index);
	struct object_definition *definition;
	struct collision_model *collision_model;

	if (!object || !TEST_FLAG(_object_mask_unit, object->object.type))
		return NULL;
	definition = object_definition_get(object->definition_index);
	if (definition->object.collision_model.index == NONE ||
		!(collision_model = collision_model_definition_get(definition->object.collision_model.index)) ||
		collision_model->resistance.port_body_recharge_velocity <= 0.0f)
	{
		return NULL;
	}
	*out = object;
	return &collision_model->resistance;
}

/* ---------- public code (the game) */

/* (perf/ce-floor-2, h2_level_fast.h h2_damage_rules_fast) the answer and what
it was found for, read inline by the hot callers */
long h2_damage_rules_seen_scenario = NONE;
unsigned long h2_damage_rules_seen_generation = ~0UL;
boolean h2_damage_rules_answer;

boolean h2_damage_rules(void)
{
	h2d_level();
	h2_damage_rules_answer = h2d.halo2;
	h2_damage_rules_seen_generation = h2d.generation;
	h2_damage_rules_seen_scenario = h2d.scenario;
	return h2d.halo2;
}

/* a rider's seat's Halo 2 damage scales (h2_vehicles.c wrote them:
unit_seat's unused3); FALSE: none (Halo 2's seat has no entry: a gunner) */
static boolean h2d_seat_scales(long rider_index, long *vehicle_index, real scales[4])
{
	struct unit_datum *rider = unit_try_and_get(rider_index);
	struct object_datum *vehicle;
	struct unit_definition *definition;
	struct unit_seat const *seat;
	short seat_index;
	int k;

	if (!rider || rider->object.parent_object_index == NONE || (seat_index = rider->unit.parent_seat_index) < 0 ||
		!(vehicle = object_try_and_get(rider->object.parent_object_index)) || vehicle->object.type != _object_type_vehicle)
	{
		return FALSE;
	}
	definition = unit_definition_get(vehicle->definition_index);
	if (seat_index >= definition->unit.seats.count)
		return FALSE;
	seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, seat_index, struct unit_seat);
	if ((unsigned long)seat->unused3[0] != H2_DAMAGE_SEAT_MAGIC && (unsigned long)seat->unused3[0] != H2_DAMAGE_SEAT_MAGIC_CHILD)
		return FALSE;
	for (k = 0; k < 4; k++)
		memcpy(&scales[k], &seat->unused3[1 + k], sizeof(real));
	*vehicle_index = rider->object.parent_object_index;
	return TRUE;
}

/* a damage effect's riders' scales (direct, maximum and minimum transfer);
FALSE: not one made from Halo 2's */
static boolean h2d_effect_rider_scales(long definition_index, real scales[3])
{
	struct damage_effect_definition const *effect;
	int k;

	if (definition_index == NONE)
		return FALSE;
	effect = damage_effect_definition_get(definition_index);
	if (effect->damage.material_modifiers[H2_DAMAGE_RIDER_SLOT] != H2_DAMAGE_RIDER_MAGIC)
		return FALSE;
	for (k = 0; k < 3; k++)
		scales[k] = effect->damage.material_modifiers[H2_DAMAGE_RIDER_SLOT + 1 + k];
	return TRUE;
}

real h2_damage_metal_multiplier(long definition_index, boolean thin)
{
	struct damage_effect_definition const *effect = definition_index != NONE ? damage_effect_definition_get(definition_index) : NULL;

	if (!effect || effect->damage.material_modifiers[H2_DAMAGE_RIDER_SLOT] != H2_DAMAGE_RIDER_MAGIC)
		return 1.f;
	return effect->damage.material_modifiers[thin ? H2_DAMAGE_THIN_METAL_SLOT : H2_DAMAGE_THICK_METAL_SLOT];
}

real h2_damage_rider_direct_scale(long definition_index, long rider_index)
{
	real seat[4], effect[3] = { 1.0f, 1.0f, 1.0f };
	long vehicle_index;

	if (!h2_damage_rules() || h2d.transferring || !h2d_seat_scales(rider_index, &vehicle_index, seat))
		return 1.0f;
	h2d_effect_rider_scales(definition_index, effect);
	return seat[0] * effect[0];
}

real h2_damage_rider_transfer_scale(long definition_index, long rider_index, real_point3d const *origin)
{
	real seat[4], effect[3] = { 1.0f, 1.0f, 1.0f };
	long vehicle_index;
	struct object_datum *rider = object_try_and_get(rider_index);

	if (!rider || !h2d_seat_scales(rider_index, &vehicle_index, seat))
		return 0.0f;
	h2d_effect_rider_scales(definition_index, effect);
	/* (function_db210: the rider within the seat's fall-off radius of where
	the damage came, the maximum; farther, the minimum) */
	if (origin && distance_squared3d(&rider->object.bounding_sphere_center, origin) < seat[1] * seat[1])
		return seat[2] * effect[1];
	return seat[3] * effect[2];
}

boolean h2_damage_rider_child_seat(long rider_index)
{
	struct unit_datum *rider = unit_try_and_get(rider_index);
	struct object_datum *vehicle;
	struct unit_definition *definition;
	short seat_index;

	if (!rider || rider->object.parent_object_index == NONE || (seat_index = rider->unit.parent_seat_index) < 0 ||
		!(vehicle = object_try_and_get(rider->object.parent_object_index)) || vehicle->object.type != _object_type_vehicle)
	{
		return FALSE;
	}
	definition = unit_definition_get(vehicle->definition_index);
	return seat_index < definition->unit.seats.count &&
		(unsigned long)TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, seat_index, struct unit_seat)->unused3[0] == H2_DAMAGE_SEAT_MAGIC_CHILD;
}

void h2_damage_transferring(boolean transferring)
{
	h2d.transferring = transferring;
}

void h2_damage_body_damaged(long object_index, real damage)
{
	struct object_datum *object;
	struct damage_resistance const *resistance;
	long slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);

	if (!h2_damage_rules() || slot >= MAXIMUM_OBJECTS_PER_MAP || !(resistance = h2d_regenerates(object_index, &object)))
		return;
	/* (function_d8cb0: damage past the body's minimum stuns it) */
	if (damage > resistance->port_body_minimum_stun_damage)
	{
		h2d.bodies[slot].object = object_index;
		h2d.bodies[slot].stun_ticks = (short)(resistance->port_body_stun_time * TICKS_PER_SECOND);
	}
}

boolean h2_damage_vehicle_hold(long vehicle_index)
{
	struct object_datum *vehicle = object_try_and_get(vehicle_index);
	long riders[H2D_MAXIMUM_RIDERS], slot;
	short count;

	if (!vehicle || vehicle->object.type != _object_type_vehicle || vehicle->object.body_vitality >= 0.0f ||
		TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) || !h2_damage_rules() || getenv("HALO_H2_VEHICLE_DIES_WITH_RIDERS"))
	{
		return FALSE;
	}
	if ((count = h2d_riders(vehicle_index, riders, 0, H2D_MAXIMUM_RIDERS)) <= 0 || (slot = h2d_held_slot(vehicle_index, TRUE)) == NONE)
		return FALSE;
	if (h2d.log && h2d.held[slot].rider_count == 0)
		platform_log("h2 damage: %08lx's body gone with %d riders in it: held until they go", (unsigned long)vehicle_index, (int)count);
	h2d.held[slot].rider_count = MIN(count, H2D_MAXIMUM_RIDERS);
	csmemcpy(h2d.held[slot].riders, riders, sizeof(long) * h2d.held[slot].rider_count);
	vehicle->object.body_vitality = 0.0f;
	return TRUE;
}

void h2_damage_body_update(long object_index)
{
	struct object_datum *object;
	struct damage_resistance const *resistance;
	long slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	real fraction;

	if (!h2_damage_rules() || slot >= MAXIMUM_OBJECTS_PER_MAP)
		return;
	h2d_held_update(object_index);
	if (!(resistance = h2d_regenerates(object_index, &object)))
		return;
	if (h2d.bodies[slot].object != object_index)
	{
		h2d.bodies[slot].object = object_index;
		h2d.bodies[slot].stun_ticks = 0;
	}
	if (TEST_FLAG(object->object.damage_flags, _object_dead_bit) || object->object.body_vitality <= 0.f)
		return;
	fraction = resistance->port_body_recharge_fraction > 0.f ? resistance->port_body_recharge_fraction : 1.f;
	if (object->object.body_vitality >= fraction)
		return;
	/* (function_d5de0: stunned, the stun runs down; then the body
	recharges at its rate to the recharge fraction) */
	if (h2d.bodies[slot].stun_ticks > 0)
	{
		h2d.bodies[slot].stun_ticks--;
		return;
	}
	object->object.body_vitality += resistance->port_body_recharge_velocity;
	if (object->object.body_vitality >= fraction)
	{
		object->object.body_vitality = fraction;
		if (h2d.log)
			platform_log("h2 damage: %08lx body recharged to %.3f", (unsigned long)object_index, fraction);
	}
}

void h2_damage_log(long object_index, long definition_index, real shield_before, real body_before)
{
	struct object_datum *object;

	if (!h2_damage_rules() || !h2d.log || !(object = object_try_and_get(object_index)) ||
		(object->object.type != _object_type_biped && object->object.type != _object_type_vehicle))
	{
		return;
	}
	platform_log("h2 damage: %08lx hit by %s: shield %.1f -> %.1f of %.0f, body %.1f -> %.1f of %.0f%s",
		(unsigned long)object_index, definition_index != NONE ? tag_get_name(definition_index) : "(none)",
		shield_before * object->object.maximum_shield_vitality, object->object.shield_vitality * object->object.maximum_shield_vitality,
		object->object.maximum_shield_vitality, body_before * object->object.maximum_body_vitality,
		object->object.body_vitality * object->object.maximum_body_vitality, object->object.maximum_body_vitality,
		TEST_FLAG(object->object.damage_flags, _object_dead_bit) || object->object.body_vitality <= 0.f ? " (dead)" : "");
}
