/*
H3_WEAPONS.C

Halo 3's weapons made into this engine's, as h2_weapons.c makes Halo 2's
(h2_level.c compiles this file after it, as a part of itself: it fills
h2_weapons.c's struct h2w_weapon and uses its models, graphs and tag
writing; built alone it is empty). Switch: HALO_H3_WEAPONS=1 (off by
default; PC for now, the Halo 3 maps read whole as h3_animation.c's).

What a Halo 3 weapon is made of, each from the player's own MCC Halo 3
maps (the map HALO_H3_WEAPONS_MAP names, else the first of H3_SPARTAN_MAPS
that holds the weapon; the pages of shared.map beside it):
- its world model and its first-person model: Halo 3 render models (mode)
  read on their own bones by h3_spartan.c's reader (h3_model_read) - the
  first-person model the Spartan's arms (mp_masterchief\fp\fp) and the
  weapon's own first-person model as one, as Halo 2's is made - their
  materials this engine's model shaders with the bitmaps h3_spartan.c
  makes (base, multipurpose, detail, bump);
- its first-person animations: the Spartan's graph for the weapon (the
  weap's first person block's jmad) opened as an h2_animation.h source
  (h3_animation_source_open: the animations' data gathered from the pages in
  one pass), each "first_person:<state>" animation decoded with h2_weapons.c's
  codec reader - Halo 3's codecs are Halo 2's - into this engine's frames
  (h2w_animation_make), in the slots first_person_weapons.c plays;
- the weapon itself: the kit stand-in the Halo 2 weapon of the same
  collection is made on, with Halo 3's values where they map: magazine,
  rate of fire, burst (shots a fire, recovery), error, zoom, aim assist,
  melee; its projectile the stand-in's with Halo 3's speed, range and damage
  (h3_weapons_damage_write: the impact damage's bounds and flags from the
  jpt!, the stand-in's material response kept).

Layouts (read against the owner's zanzibar.map and guardian.map:
~/halo-test-out/h3-anim/AUDIT.md "M-B2 probe", probe_barrels.txt,
probe_proj_jpt_mb2.txt - the field names Halo 2's where every weapon's values
agree with Halo 2's meaning, else "?" and left alone):
- weap: 0x34 hlmt (its render model at the hlmt's 0); 0x240 melee pyramid
  (width, height, depth); 0x24C melee damage jpt!, 0x25C melee response;
  0x31E zoom levels, 0x320/0x324 magnification; 0x328 autoaim angle, 0x32C
  range, 0x334 magnetism angle, 0x338 range; 0x3DC weapon class sid, 0x3E0
  weapon name sid (the animation label); 0x3FC first person block (0x20: mode,
  jmad at 0x10; [0] the Spartan's); 0x424 magazines (0x80: 6 initial, 8
  maximum, 0xA loaded, 0xC reserve, 0x10 reload time, 0x14 reloaded); 0x430
  triggers (0x30: 6 behavior, 8 primary barrel); 0x43C barrels (0x130: 0
  flags, 4/8 rounds a second, 0xC/0x10 its rise and fall, 0x1C/0x1E shots a
  fire, 0x20 recovery, 0x24 soft recovery, 0x28 magazine, 0x2A rounds a shot,
  0x2E rounds between tracers, 0x38/0x3C error rise and fall, dual wielded
  0x54/0x58 error times, 0x64 minimum, 0x68/0x6C angles, 0x70 damage scale;
  0x74 distribution function, 0x76 projectiles a shot, 0x78 distribution
  angle, 0x7C minimum error, 0x80/0x84 error angle at 0 and 1, 0x98 proj,
  0xE0 illumination recovery, 0xE4 heat a round, 0xE8 age a round, 0xF4/0xF8
  recoil angles, 0xFC/0x100 times, 0x104 curve, 0x128 firing effects) - Halo
  2's barrel from its error on, 0xC further;
- proj: 0xF8 flags, 0xFC timer starts, 0x100 collision radius, 0x104 arming
  time, 0x108 danger radius, 0x10C/0x110 timer, 0x114 minimum velocity, 0x118
  maximum range (the rocket's 175, Halo 2's), 0x1F0 impact damage, 0x224 air
  gravity scale, 0x228/0x22C air damage range (the rocket's 0..150, Halo 2's),
  0x230 water gravity, 0x234/0x238 water damage range, 0x23C/0x240 initial and
  final velocity, 0x244/0x248 guided angular velocity, 0x24C/0x250
  acceleration range - Halo 2's projectile 0xC0 further;
- jpt!: Halo 2's to 0x48 (0x14 flags - bit 1 headshots -, 0x1C minimum, 0x20
  and 0x24 bounds, 0x40 instantaneous acceleration), 0x50 category sid.
*/

#ifdef H2_LEVEL_INTERNAL

#include "h3_cache.h"
#include "h3_spartan.h"
#include "h3_animation.h"

/* ---------- constants */

enum
{
	H3W_WEAP_MODEL = 0x34,
	/* (the weapon's flags: bit 16 "can be dual wielded" - the SMG, plasma
	rifle, plasma pistol, spiker, mauler and magnum have it, the needler,
	carbine and rifles not: Halo 3's dual set) */
	H3W_WEAP_FLAGS = 0x18C,
	H3W_WEAP_DUAL_WIELD_BIT = 16,
	/* (its heat: seven reals as Halo 2's at 0x150 - recovery threshold 0.1,
	overheated threshold 1, detonation threshold, detonation fraction, loss
	a second (plasma rifle 0.8525, plasma pistol 0.6, laser 1, flamethrower
	0.5), illumination 1, loss a second overheated; 0 on the rest) */
	H3W_WEAP_HEAT = 0x1C4,
	H3W_WEAP_MELEE_PYRAMID = 0x240,
	H3W_WEAP_MELEE_DAMAGE = 0x24C,
	H3W_WEAP_MELEE_RESPONSE = 0x25C,
	/* (the seventh melee pair: a melee weapon's lunge - the sword's
	dash_melee and the hammer's crush_melee with globals\trigger_lunge; the
	sniper's smash_melee_response sits there too, read on sword kinds only) */
	H3W_WEAP_LUNGE_DAMAGE = 0x2AC,
	H3W_WEAP_ZOOM_LEVELS = 0x31E,
	H3W_WEAP_ZOOM = 0x320,
	H3W_WEAP_AUTOAIM = 0x328,
	H3W_WEAP_CLASS = 0x3DC,
	H3W_WEAP_LABEL = 0x3E0,
	H3W_WEAP_FIRST_PERSON = 0x3FC,
	H3W_FIRST_PERSON_SIZE = 0x20,
	H3W_WEAP_MAGAZINES = 0x424,
	H3W_MAGAZINE_SIZE = 0x80,
	H3W_WEAP_TRIGGERS = 0x430,
	/* (a trigger: 0 flags, 4 input, 6 behavior - 0 spew, 1 latch, 2 the
	plasma pistol's charge, 4 latch zoom, 6 the laser's charge -, 8 primary
	barrel, 0xA secondary barrel, 0x10/0x14 autofire time and throw, 0x18
	secondary action, 0x1C charging time (plasma pistol 0.8 s, laser 2.5 s),
	0x20 charged time, 0x24 overcharged action (laser 2: discharge - it fires
	when charged; plasma pistol 0: held), 0x28 charged illumination 1, 0x2C
	spew time: Halo 2's trigger's order) */
	H3W_TRIGGER_SIZE = 0x30,
	H3W_TRIGGER_SECONDARY_BARREL = 0xA,
	H3W_TRIGGER_CHARGING_TIME = 0x1C,
	H3W_TRIGGER_CHARGED_TIME = 0x20,
	H3W_TRIGGER_OVERCHARGED_ACTION = 0x24,
	H3W_TRIGGER_CHARGED_ILLUMINATION = 0x28,
	H3W_TRIGGER_SPEW_TIME = 0x2C,
	H3W_WEAP_BARRELS = 0x43C,
	/* (0x134: its firing effects block - count, address, pad - ends it at
	0x128 + 12; a second barrel read at 0x130 was four bytes early) */
	H3W_BARREL_SIZE = 0x134,
	H3W_BARREL_SHOTS = 0x1C,
	H3W_BARREL_PROJECTILE = 0x98,
	H3W_BARREL_HEAT = 0xE4,
	H3W_BARREL_AGE = 0xE8,
	H3W_FIRING_EFFECTS = 0x128,
	H3W_FIRING_EFFECT_SIZE = 0x44,

	H3W_PROJ_FLAGS = 0xF8,
	H3W_PROJ_TIMER_STARTS = 0xFC,
	H3W_PROJ_COLLISION_RADIUS = 0x100,
	H3W_PROJ_ARMING_TIME = 0x104,
	H3W_PROJ_DANGER_RADIUS = 0x108,
	H3W_PROJ_TIMER = 0x10C,
	H3W_PROJ_MINIMUM_VELOCITY = 0x114,
	H3W_PROJ_MAXIMUM_RANGE = 0x118,
	/* (its damage effects: 0x148 detonation effect, 0x158 detonation damage,
	0x168 attached detonation damage, 0x178 super detonation effect, 0x188
	super detonation damage, 0x1AC attached super detonation damage, 0x1F0
	impact damage - the rocket's explosion at 0x158, the plasma grenade's
	stuck one at 0x168, the needler's supercombine at 0x188/0x1AC with proj
	flag bit 3 as Halo 2's) */
	H3W_PROJ_DETONATION_DAMAGE = 0x158,
	H3W_PROJ_ATTACHED_DAMAGE = 0x168,
	H3W_PROJ_SUPER_DAMAGE = 0x188,
	H3W_PROJ_ATTACHED_SUPER_DAMAGE = 0x1AC,
	H3W_PROJ_IMPACT_DAMAGE = 0x1F0,
	H3W_PROJ_AIR_GRAVITY = 0x224,
	H3W_PROJ_AIR_DAMAGE_RANGE = 0x228,
	H3W_PROJ_WATER_GRAVITY = 0x230,
	H3W_PROJ_WATER_DAMAGE_RANGE = 0x234,
	H3W_PROJ_VELOCITY = 0x23C,
	H3W_PROJ_GUIDED = 0x244,
	H3W_PROJ_ACCELERATION_RANGE = 0x24C,

	/* (a damage effect: 0/4 its radius - falloff, cutoff -, 0x18 its core
	radius, as Halo 2's and this engine's) */
	H3W_JPT_RADIUS = 0x0,
	H3W_JPT_CORE_RADIUS = 0x18,
	H3W_JPT_FLAGS = 0x14,
	H3W_JPT_DAMAGE = 0x1C,
	H3W_JPT_ACCELERATION = 0x40,
	H3W_JPT_CATEGORY = 0x50,

	/* Halo 3's render model (h3_spartan.c's MODE_* and its nodes: 0x60 each,
	name, parent at 4, default translation 0xC, rotation 0x18, scale 0x28, the
	inverse of its default 0x2C - forward, left, up, position; marker groups
	0x3C, 0x10 each, markers 0x24 each: node at 2, translation 4, rotation 0x10) */
	H3W_MODE_NODES = 0x30,
	H3W_MODE_NODE_SIZE = 0x60,
	H3W_MODE_MARKER_GROUPS = 0x3C,
	H3W_MODE_MARKER_GROUP_SIZE = 0x10,
	H3W_MODE_MARKER_SIZE = 0x24,

	H3W_MAXIMUM_BITMAPS = 256,
};

#define H3W_ARMS_MODES { "objects\\characters\\masterchief\\mp_masterchief\\fp\\fp", "objects\\characters\\masterchief\\fp\\fp" }

/* ---------- structures */

/* the Halo 3 weapons made: the collection (h2w_kinds') each stands in
for - what a Halo 2 level hands out and what the multiplayer start gives;
the kit stand-in, label and sword flag of that row - and Halo 3's weapon.
(A Halo 3 level's own placements are the h3/direct-level lane's, through
h3_weapons_for.) Halo 3's twenty-two multiplayer weapons: the eight of no
Halo 2 twin have rows of their own in h2w_kinds (h2_weapons.c's end) */
static struct
{
	char const *collection;
	char const *h3;
} const h3w_kinds[] =
{
	{ "assault_rifle", "objects\\weapons\\rifle\\assault_rifle\\assault_rifle" },
	{ "battle_rifle", "objects\\weapons\\rifle\\battle_rifle\\battle_rifle" },
	{ "magnum", "objects\\weapons\\pistol\\magnum\\magnum" },
	{ "smg", "objects\\weapons\\rifle\\smg\\smg" },
	{ "plasma_rifle", "objects\\weapons\\rifle\\plasma_rifle\\plasma_rifle" },
	{ "brute_plasma_rifle", "objects\\weapons\\rifle\\plasma_rifle_red\\plasma_rifle_red" },
	{ "plasma_pistol", "objects\\weapons\\pistol\\plasma_pistol\\plasma_pistol" },
	{ "needler", "objects\\weapons\\pistol\\needler\\needler" },
	{ "carbine", "objects\\weapons\\rifle\\covenant_carbine\\covenant_carbine" },
	{ "shotgun", "objects\\weapons\\rifle\\shotgun\\shotgun" },
	{ "sniper_rifle", "objects\\weapons\\rifle\\sniper_rifle\\sniper_rifle" },
	{ "beam_rifle", "objects\\weapons\\rifle\\beam_rifle\\beam_rifle" },
	{ "spike_rifle", "objects\\weapons\\rifle\\spike_rifle\\spike_rifle" },
	{ "excavator", "objects\\weapons\\pistol\\excavator\\excavator" },
	{ "brute_shot", "objects\\weapons\\support_low\\brute_shot\\brute_shot" },
	{ "rocket_launcher", "objects\\weapons\\support_high\\rocket_launcher\\rocket_launcher" },
	{ "flak_cannon", "objects\\weapons\\support_high\\flak_cannon\\flak_cannon" },
	{ "spartan_laser", "objects\\weapons\\support_high\\spartan_laser\\spartan_laser" },
	{ "sentinel_beam", "objects\\weapons\\support_low\\sentinel_gun\\sentinel_gun" },
	{ "energy_sword", "objects\\weapons\\melee\\energy_blade\\energy_blade" },
	{ "gravity_hammer", "objects\\weapons\\melee\\gravity_hammer\\gravity_hammer" },
	{ "flamethrower", "objects\\weapons\\turret\\flamethrower\\flamethrower" },
	{ "smg_silenced", "objects\\weapons\\rifle\\smg_silenced\\smg_silenced" },
};
#define H3W_KIND_COUNT ((long)(sizeof(h3w_kinds) / sizeof(h3w_kinds[0])))

/* a Halo 3 damage effect (jpt!) read: its radii (falloff, cutoff, core),
its damage (minimum, lower and upper bound), its instantaneous
acceleration, flags, side effect (3: emp - the plasma pistol's overcharge),
its category's name; valid FALSE: none */
struct h3w_jpt
{
	boolean valid;
	long tag;
	float radius[3];
	float damage[3];
	float acceleration;
	unsigned long flags;
	short side_effect;
	char category[32];
	char name[64];
};

/* what a Halo 3 weapon keeps beside h2w_weapon's fields */
struct h3w_weapon
{
	long h3_kind;
	char label[32];             /* its animation label (the weap's weapon name sid; a melee weapon's its class's) */
	char class_name[32];        /* its weapon class sid (rifle, pistol, missile, support, sword, hammer, turret) */
	char h3_name[64];
	unsigned long flags;        /* the weap's flags (H3W_WEAP_FLAGS) */
	/* its barrels' projectiles' damage effects (the jpt!s): [0] the primary
	barrel's, [1] the secondary's (the charged bolt's, the laser beam's) -
	the impact's, the detonation's (the rocket's, grenade's, beam's
	explosion), the attached detonation's, the super detonation's and the
	attached super detonation's (the needler's supercombine) */
	struct h3w_jpt impact[2], detonation[2], attached[2], super_[2], attached_super[2];
	float maximum_range;
	/* the barrels' shots a fire, heat and age a round ([1] the second's:
	the laser's beam fires five rounds of 0.04 battery, the charged bolt one
	of 0.15), and whether a barrel's firing effects fire no shot (the laser's
	first: its aiming tracer, shot count 0..0) */
	short shots[2];
	float heat[2], age[2];
	boolean fires_nothing[2];
	/* its trigger's charge (the plasma pistol's, the laser's): charging and
	charged time, the overcharged action, the charged illumination, the spew
	time; charges FALSE: none */
	boolean charges;
	float charging_time, charged_time, charged_illumination, spew_time;
	short overcharged_action;
	/* a melee weapon's strike explosion (the gravity hammer's
	gravity_hammer_explosion, the damage part of its impact effect);
	area.valid FALSE: none */
	struct h3w_jpt area;
};

/* a bitmap made for the build from a Halo 3 one, by name (the same map on the
first-person and the world model made once) */
static struct
{
	long count;
	struct { char name[H3_SPARTAN_NAME_SIZE]; long bitmap; } made[H3W_MAXIMUM_BITMAPS];
} h3w_bitmaps;

/* (h3_sandbox.c: a Halo 3 level made from its own map) the level being
made: on, its map, and the tag paths its scenario's palettes name - what it
can place; the weapons and vehicles made are these (and the players' start)
from that map, no Halo 2 level's rules */
static struct
{
	boolean on;
	char directory[256];
	char name[64];
	char const *const *paths;
	long path_count;
	struct h3_cache *cache;     /* the level's own map, open (borrowed: not closed here); NULL opens it */
} h3w_level;

/* ---------- private code: helpers */

H2_ALIGN static boolean h3_weapons_on(void)
{
	char const *setting = getenv("HALO_H3_WEAPONS");

	return h3w_level.on || (setting && atoi(setting) > 0);
}

/* (a Halo 3 level) whether its palettes name the tag path (a weap's or a
vehi's full path) */
H2_ALIGN static boolean h3w_level_names(char const *path)
{
	long i;

	for (i = 0; path && i < h3w_level.path_count; i++)
	{
		if (h3w_level.paths[i] && !strcmp(h3w_level.paths[i], path))
			return TRUE;
	}
	return FALSE;
}

/* (a Halo 3 level) whether the weapon of a collection is made for it: one
its palettes name, or the players' starting weapons (Halo 3's default start,
the assault rifle and the magnum: h3_weapons_start), or every one
(HALO_H2_ALL_WEAPONS) */
H2_ALIGN static boolean h3w_level_wants(char const *h3_path, char const *collection)
{
	return h3w_level_names(h3_path) || !strcmp(collection, "assault_rifle") || !strcmp(collection, "magnum") ||
		getenv("HALO_H2_ALL_WEAPONS") != NULL;
}

/* (h3_sandbox.c) the Halo 3 level being made named, and let go */
H2_ALIGN static void h3_weapons_level_begin(char const *directory, char const *name, char const *const *paths, long path_count,
	struct h3_cache *cache)
{
	memset(&h3w_level, 0, sizeof(h3w_level));
	h3w_level.on = TRUE;
	h3w_level.cache = cache;
	snprintf(h3w_level.directory, sizeof(h3w_level.directory), "%s", directory ? directory : "");
	snprintf(h3w_level.name, sizeof(h3w_level.name), "%s", name ? name : "");
	h3w_level.paths = paths;
	h3w_level.path_count = path_count;
}

H2_ALIGN static void h3_weapons_level_end(void)
{
	memset(&h3w_level, 0, sizeof(h3w_level));
}

/* (h2_weapons.c) whether a Halo 3 level is being made */
H2_ALIGN static boolean h3_weapons_level_on(void)
{
	return h3w_level.on;
}

/* this engine's marker and node names are spaced, Halo 3's underscored */
H2_ALIGN static void h3w_name(struct h3_cache *cache, unsigned long sid, char *out, long size)
{
	char const *text = h3_cache_string(cache, sid);
	char *p;

	snprintf(out, (size_t)size, "%s", text && text[0] ? text : "node");
	for (p = out; *p; p++)
	{
		if (*p == '_')
			*p = ' ';
	}
}

/* the Halo 3 map read: HALO_H3_WEAPONS_MAP's, else HALO_H3_ANIMATION_MAP's,
else the first of H3_SPARTAN_MAPS that holds the first weapon */
H2_ALIGN static struct h3_cache *h3w_cache_open(char *why, long why_size)
{
	static char const *const maps[] = H3_SPARTAN_MAPS;
	char directory[256];
	char const *named = getenv("HALO_H3_WEAPONS_MAP");
	unsigned long i;

	if (!named)
		named = getenv("HALO_H3_ANIMATION_MAP");
	/* (a Halo 3 level: its own map, whatever it holds - the level's open one) */
	if (h3w_level.on)
		return h3w_level.cache ? h3w_level.cache : h3_cache_open(h3w_level.directory, h3w_level.name, why, why_size);
	if (!h3_spartan_maps_directory(directory, sizeof(directory)))
	{
		snprintf(why, (size_t)why_size, "no Halo 3 maps in the content folder");
		return NULL;
	}
	for (i = 0; i < (named ? 1 : sizeof(maps) / sizeof(maps[0])); i++)
	{
		struct h3_cache *cache = h3_cache_open(directory, named ? named : maps[i], why, why_size);

		if (cache && h3_cache_find_tag(cache, 'weap', h3w_kinds[0].h3) != NONE)
			return cache;
		if (cache)
			h3_cache_close(cache);
	}
	snprintf(why, (size_t)why_size, "no Halo 3 map found holds the weapons");
	return NULL;
}

/* ---------- private code: models */

/* a material of a Halo 3 model (h3_spartan.c's) as one of the build's
shaders: this engine's model shader with its maps (made the build's bitmaps,
one a name), its detail and bump scales, its highlight; NONE if there is no
room */
H2_ALIGN static long h3w_bitmap(struct build *build, struct h3_spartan *sp, short index)
{
	struct h3_spartan_bitmap *b;
	unsigned char *pixels;
	char path[256];
	long i, made;

	if (index < 0 || index >= sp->bitmap_count)
		return NONE;
	b = &sp->bitmaps[index];
	for (i = 0; i < h3w_bitmaps.count; i++)
	{
		if (!strcmp(h3w_bitmaps.made[i].name, b->name))
			return h3w_bitmaps.made[i].bitmap;
	}
	if (!b->pixels || !b->size)
		return NONE;
	/* (h3vita) the model's pixels handed over to the build's bitmap, not
	copied - a Guardian sandbox at the Vita's 256 held 50 MB of them twice at
	the make's peak - when the name is remembered (a second ask for the same
	map finds the bitmap by its name; the model keeps none). The kept arms
	model's maps go the same way: their names are remembered first */
	if (h3w_bitmaps.count < H3W_MAXIMUM_BITMAPS)
	{
		pixels = b->pixels;
		b->pixels = NULL;
	}
	else
	{
		if (!(pixels = malloc(b->size)))
			return NONE;
		memcpy(pixels, b->pixels, b->size);
	}
	snprintf(path, sizeof(path), "h3\\bitmaps\\%s", b->name);
	made = bitmap_made(build, path, b->width, b->height, b->format, b->mipmaps, 0, pixels, b->size);
	if (made != NONE && h3w_bitmaps.count < H3W_MAXIMUM_BITMAPS)
	{
		snprintf(h3w_bitmaps.made[h3w_bitmaps.count].name, H3_SPARTAN_NAME_SIZE, "%s", b->name);
		h3w_bitmaps.made[h3w_bitmaps.count++].bitmap = made;
	}
	return made;
}

H2_ALIGN static short h3w_shader(struct build *build, struct h3_spartan *sp, short material)
{
	struct h3_spartan_material const *m = &sp->materials[material];
	struct shader *sh;
	long base, k;
	char path[256];

	snprintf(path, sizeof(path), "h3\\shaders\\%s", m->name);
	/* (one a name: the first-person and the world model share most) */
	for (k = 0; k < build->shader_count; k++)
	{
		if (build->shaders[k].h2_tag == NONE && !strcmp(build->shaders[k].path, path))
			return (short)k;
	}
	if (build->shader_count >= BUILD_SHADER_ROOM(build))
		return NONE;
	base = h3w_bitmap(build, sp, m->base_bitmap);
	if (base == NONE)
		return NONE;
	sh = &build->shaders[build->shader_count];
	memset(sh, 0, sizeof(*sh));
	sh->h2_tag = NONE;
	sh->group = 'senv';
	sh->rendered = TRUE;
	sh->bitmap = base;
	sh->alpha_bitmap = sh->ripple_bitmap = sh->reflection_bitmap = NONE;
	if (m->illum_only)
	{
		/* (a material of its self-illumination alone - an assault rifle's
		display: its map, tinted, added; as h2w_digit_shader's) */
		sh->group = 'schi';
		sh->blend = 3;
		snprintf(sh->path, sizeof(sh->path), "%s", path);
		return (short)build->shader_count++;
	}
	sh->detail_bitmap[0] = sh->detail_bitmap[1] = NONE;
	sh->bump_bitmap = sh->cube_bitmap = sh->illum_bitmap = sh->multipurpose_bitmap = NONE;
	sh->illum_three_tag = sh->specular_multipurpose = sh->scope_overlay_bitmap = sh->texture_camera_white = sh->sky_bitmap2 = NONE;
	sh->plasma_illum_bitmap = NONE;
	for (k = 0; k < 4; k++)
		sh->plasma_bitmap[k] = NONE, sh->waves[k].bitmap = NONE;
	sh->glass_specular_bitmap = NONE;
	sh->detail_alpha = -1.0f;
	sh->multipurpose_bitmap = h3w_bitmap(build, sp, m->multipurpose_bitmap);
	sh->detail_bitmap[0] = h3w_bitmap(build, sp, m->detail_bitmap);
	/* (HALO_H3_WEAPON_NO_BUMP=1: no bump map on a Halo 3 weapon's or arms'
	material - the red/green world hunt, ~/halo-test-out/h3-sandbox/NOTES.md) */
	sh->bump_bitmap = getenv("HALO_H3_WEAPON_NO_BUMP") ? NONE : h3w_bitmap(build, sp, m->bump_bitmap);
	sh->detail_scale[0] = m->detail_scale[0];
	sh->detail_scale[1] = m->detail_scale[1];
	sh->bump_scale = m->bump_scale[0];
	memcpy(sh->illum_color, m->illum_color, sizeof(sh->illum_color));
	/* (Halo 2's material record: what the soso writer and the per-pixel pass
	read - its detail and bump scales, its highlight's colours and curve.
	OFF by default for now - a STAND-IN look, the plain model shader -:
	with it, once a Halo 3 weapon is made again after the spawn's (a death,
	a pickup, "@weapon") the world draws through the weapon's maps - red/green
	bump bands, or black - until the level is left: a texture stage the
	per-pixel model pass leaves set that the environment pass inherits
	(~/halo-test-out/h3-sandbox/NOTES.md: HALO_H2_MATERIALS_OLD=1 clears it;
	the renderer's fix is the h3/lighting lane's). HALO_H3_WEAPON_MATERIALS=1
	brings the record back) */
	sh->material.known = getenv("HALO_H3_WEAPON_MATERIALS") && atoi(getenv("HALO_H3_WEAPON_MATERIALS")) > 0;
	sh->material.bump_tag = sh->material.base_tag = sh->material.detail_tag = sh->material.secondary_tag = NONE;
	sh->material.cube_tag = sh->material.change_colour_tag = NONE;
	sh->material.bump_scale[0] = m->bump_scale[0];
	sh->material.bump_scale[1] = m->bump_scale[1];
	sh->material.detail_scale[0] = m->detail_scale[0];
	sh->material.detail_scale[1] = m->detail_scale[1];
	sh->material.base_scale[0] = sh->material.base_scale[1] = 1.0f;
	sh->material.specular_color = m->specular_color;
	sh->material.specular_glancing_color = m->specular_glancing_color;
	sh->material.shader_specular_type = (signed char)m->specular_type;
	sh->material.shader_lightmap_type = -1;
	sh->material.lightmap_specular_brightness = -1.0f;
	snprintf(sh->path, sizeof(sh->path), "%s", path);
	return (short)build->shader_count++;
}

/* a Halo 3 weapon's counter's digits (its materials 'tens' and 'ones', their
image the numbers bitmap's by the weapon's rounds): twenty added shaders of
the model's ten digit bitmaps, the tens' 0..9 then the ones', named as
h2w_digit_shader names Halo 2's - h2_weapon_counter.c finds them by name and
draws the one the rounds pick; the first's index of place (0 tens, 1 ones),
NONE if they cannot be made */
H2_ALIGN static short h3w_digit_shader(struct build *build, struct h3_spartan *sp, int place)
{
	long d;
	int p;

	if (build->h2w_digit_tried)
		return build->h2w_digit_first == NONE ? NONE : (short)(build->h2w_digit_first + 10 * place);
	for (d = 0; d < 10; d++)
	{
		if (sp->digit_bitmaps[d] == NONE)
			return NONE;
	}
	build->h2w_digit_tried = TRUE;
	build->h2w_digit_first = NONE;
	if (build->shader_count + 20 > BUILD_SHADER_ROOM(build))
		return NONE;
	for (p = 0; p < 2; p++)
	{
		for (d = 0; d < 10; d++)
		{
			long b = h3w_bitmap(build, sp, sp->digit_bitmaps[d]);
			struct shader *sh = &build->shaders[build->shader_count];

			if (b == NONE)
			{
				build->h2w_digit_first = NONE;
				return NONE;
			}
			memset(sh, 0, sizeof(*sh));
			sh->h2_tag = NONE;
			sh->bitmap = b;
			sh->alpha_bitmap = sh->ripple_bitmap = sh->reflection_bitmap = NONE;
			sh->group = 'schi';
			sh->blend = 3;
			sh->rendered = TRUE;
			snprintf(sh->path, sizeof(sh->path), "h2\\shaders\\digits %s %ld", p ? "ones" : "tens", d);
			if (build->h2w_digit_first == NONE)
				build->h2w_digit_first = (short)build->shader_count;
			build->shader_count++;
		}
	}
	return (short)(build->h2w_digit_first + 10 * place);
}

/* a Halo 3 render model added to m (as h2w_model_add adds a Halo 2 one): its
nodes by name - one there already is shared -, its markers, its parts of
the look's permutations (h3_model_read: the first-person arms' and the
weapon's own), their materials the build's shaders; FALSE if nothing of it
was drawn. skip: a material name part not drawn (NULL none); offset: where
the model's origin is in m (NULL: at m's - a vehicle's child object at its
marker, h3_vehicles.c: its positions and its nodes' inverses moved, as
h2w_model_add_renamed moves a Halo 2 piece's) */
/* the first-person arms' model kept read between the weapons (every
first-person model is the arms and the weapon's own: the arms' read, its
pages inflated, took a second each of the twenty); let go after the gather */
static struct
{
	struct h3_cache *cache;
	long mode_tag;
	struct h3_spartan *sp;
} h3w_model_kept;

H2_ALIGN static void h3w_model_kept_free(void)
{
	if (h3w_model_kept.sp)
		h3_spartan_free(h3w_model_kept.sp);
	memset(&h3w_model_kept, 0, sizeof(h3w_model_kept));
}

/* a model read done with: kept if it is the arms (not kept already), else
let go unless it is the kept one */
H2_ALIGN static void h3w_model_done(struct h3_cache *cache, long mode_tag, struct h3_spartan *sp)
{
	char const *name = h3_cache_tag_name(cache, mode_tag);
	size_t length = name ? strlen(name) : 0;

	if (sp == h3w_model_kept.sp)
		return;
	if (name && length > 6 && !strcmp(name + length - 6, "\\fp\\fp"))
	{
		h3w_model_kept_free();
		h3w_model_kept.cache = cache;
		h3w_model_kept.mode_tag = mode_tag;
		h3w_model_kept.sp = sp;
		return;
	}
	h3_spartan_free(sp);
}

/* (h3_characters.c) look: the permutations wanted of its regions
("region=permutation,...", h3_model_read's; NULL: each region's base);
node_limit: the most nodes m may hold after it (0: no limit) - a model of
more has its leaves let go, the fingers and toes first, their vertices
their nearest kept ancestor's */
H2_ALIGN static boolean h3w_model_add_look(struct build *build, struct h2w_model *m, struct h3_cache *cache, long mode_tag, char const *skip,
	float const *offset, char const *look, long node_limit);

H2_ALIGN static boolean h3w_model_add(struct build *build, struct h2w_model *m, struct h3_cache *cache, long mode_tag, char const *skip,
	float const *offset)
{
	return h3w_model_add_look(build, m, cache, mode_tag, skip, offset, NULL, 0);
}

H2_ALIGN static boolean h3w_model_add_look(struct build *build, struct h2w_model *m, struct h3_cache *cache, long mode_tag, char const *skip,
	float const *offset, char const *look, long node_limit)
{
	struct h3_cache_tag t;
	struct h3_spartan *sp;
	char why[H3_SPARTAN_WHY_SIZE];
	long node_count = 0, group_count = 0, i, p, vertices = 0, indices = 0, parts_before = m->made.part_count;
	unsigned long nodes = 0, groups = 0;
	short node_map[H3_SPARTAN_MAXIMUM_NODES];
	boolean dropped[H3_SPARTAN_MAXIMUM_NODES];
	short *shaders;
	struct h2w_section s;

	if (mode_tag == NONE || !h3_cache_tag_get(cache, mode_tag, &t) || t.group_tag != 'mode' ||
		!h3_cache_block(cache, t.address + H3W_MODE_NODES, H3W_MODE_NODE_SIZE, &node_count, &nodes) || node_count < 1 ||
		node_count > H3_SPARTAN_MAXIMUM_NODES)
	{
		return FALSE;
	}
	why[0] = 0;
	if (!look && h3w_model_kept.sp && h3w_model_kept.cache == cache && h3w_model_kept.mode_tag == mode_tag)
		sp = h3w_model_kept.sp;
	else
		sp = h3_model_read(cache, mode_tag, look, NULL, why, sizeof(why));
	if (!sp)
	{
		platform_log("h3 weapons: %s: not read: %s", h3_cache_tag_name(cache, mode_tag), why);
		return FALSE;
	}
	/* (a model of more nodes than the limit: leaves let go until it fits -
	the fingers and toes first, then the deepest) */
	{
		short parents[H3_SPARTAN_MAXIMUM_NODES];
		long kept = m->node_count + node_count;

		for (i = 0; i < node_count; i++)
		{
			unsigned char const *c = h3_cache_at(cache, nodes + H3W_MODE_NODE_SIZE * (unsigned long)i, H3W_MODE_NODE_SIZE);

			parents[i] = c ? read16(c + 4) : NONE;
			dropped[i] = FALSE;
		}
		while (node_limit > 0 && kept > node_limit)
		{
			long best = NONE, best_score = -1;

			for (i = 1; i < node_count; i++)
			{
				unsigned char const *c;
				char name[32];
				long k, depth = 0, score;

				if (dropped[i])
					continue;
				for (k = i + 1; k < node_count && (dropped[k] || parents[k] != i); k++)
					;
				if (k < node_count)
					continue;
				if (!(c = h3_cache_at(cache, nodes + H3W_MODE_NODE_SIZE * (unsigned long)i, H3W_MODE_NODE_SIZE)))
					continue;
				h3w_name(cache, read32(c), name, sizeof(name));
				for (k = i; k > 0 && parents[k] >= 0 && parents[k] < k && depth < 64; k = parents[k])
					depth++;
				score = depth + (strstr(name, "finger") || strstr(name, "thumb") || strstr(name, "index") || strstr(name, "middle") ||
					strstr(name, "ring") || strstr(name, "pinky") || strstr(name, "toe") ? 1000 : 0);
				if (score >= best_score)
					best_score = score, best = i;
			}
			if (best == NONE)
				break;
			dropped[best] = TRUE;
			kept--;
		}
	}
	/* nodes */
	for (i = 0; i < node_count; i++)
	{
		unsigned char const *c = h3_cache_at(cache, nodes + H3W_MODE_NODE_SIZE * (unsigned long)i, H3W_MODE_NODE_SIZE);
		char name[32];
		short n, parent;

		node_map[i] = NONE;
		if (!c)
			continue;
		h3w_name(cache, read32(c), name, sizeof(name));
		parent = read16(c + 4);
		if (dropped[i])
		{
			node_map[i] = parent >= 0 && parent < i ? node_map[parent] : 0;
			continue;
		}
		n = h2w_model_node(m, name);
		if (n == NONE && m->node_count < H2W_MAXIMUM_NODES)
		{
			struct h2w_node *o = &m->nodes[m->node_count];

			memset(o, 0, sizeof(*o));
			snprintf(o->name, sizeof(o->name), "%s", name);
			o->parent = parent >= 0 && parent < i ? node_map[parent] : NONE;
			memcpy(o->translation, c + 0xC, 12);
			memcpy(o->rotation, c + 0x18, 16);
			memcpy(&o->scale, c + 0x28, 4);
			memcpy(o->inverse, c + 0x2C, 48);
			if (offset)
			{
				int a;

				for (a = 0; a < 3; a++)
					o->inverse[9 + a] -= o->inverse[a] * offset[0] + o->inverse[3 + a] * offset[1] + o->inverse[6 + a] * offset[2];
				if (o->parent == NONE)
				{
					for (a = 0; a < 3; a++)
						o->translation[a] += offset[a];
				}
			}
			n = (short)m->node_count++;
		}
		node_map[i] = n;
	}
	/* markers */
	if (h3_cache_block(cache, t.address + H3W_MODE_MARKER_GROUPS, H3W_MODE_MARKER_GROUP_SIZE, &group_count, &groups))
	{
		for (i = 0; i < group_count; i++)
		{
			long marker_count = 0, k;
			unsigned long markers = 0;
			char name[32];

			h3w_name(cache, h3_cache_read32(cache, groups + H3W_MODE_MARKER_GROUP_SIZE * (unsigned long)i), name, sizeof(name));
			if (!h3_cache_block(cache, groups + H3W_MODE_MARKER_GROUP_SIZE * (unsigned long)i + 4, H3W_MODE_MARKER_SIZE, &marker_count, &markers))
				continue;
			for (k = 0; k < marker_count && m->marker_count < H2W_MAXIMUM_MARKERS; k++)
			{
				unsigned char const *c = h3_cache_at(cache, markers + H3W_MODE_MARKER_SIZE * (unsigned long)k, H3W_MODE_MARKER_SIZE);
				struct h2w_marker *o = &m->markers[m->marker_count];
				int node = c ? c[2] : -1;

				if (!c || node < 0 || node >= node_count || node_map[node] == NONE)
					continue;
				snprintf(o->name, sizeof(o->name), "%s", name);
				o->node = node_map[node];
				memcpy(o->translation, c + 4, 12);
				memcpy(o->rotation, c + 0x10, 16);
				m->marker_count++;
				/* (this engine's names for the hand a biped's left hand takes,
				as h2w_model_add gives them) */
				if (!strcmp(name, "left hand") && m->marker_count + 1 < H2W_MAXIMUM_MARKERS)
				{
					m->markers[m->marker_count] = *o;
					snprintf(m->markers[m->marker_count++].name, 32, "left hand cyborg");
					m->markers[m->marker_count] = *o;
					snprintf(m->markers[m->marker_count++].name, 32, "left hand marine");
				}
			}
		}
	}
	/* its parts as one section: every part's vertices and triangles, a
	section part a model part, its material the build's shader */
	memset(&s, 0, sizeof(s));
	shaders = malloc(((unsigned long)sp->material_count + 1) * sizeof(short));
	for (p = 0; p < sp->part_count; p++)
	{
		vertices += sp->parts[p].vertex_count;
		indices += sp->parts[p].triangle_count * 3;
	}
	s.positions = malloc(((unsigned long)vertices + 1) * 3 * sizeof(float));
	s.coordinates = malloc(((unsigned long)vertices + 1) * 2 * sizeof(float));
	s.packed = malloc(((unsigned long)vertices + 1) * 3 * sizeof(unsigned long));
	s.nodes = malloc(((unsigned long)vertices + 1) * 4);
	s.weights = malloc(((unsigned long)vertices + 1) * 4);
	s.indices = malloc(((unsigned long)indices + 1) * sizeof(unsigned short));
	s.parts = calloc((unsigned long)sp->part_count + 1, sizeof(*s.parts));
	if (!shaders || !s.positions || !s.coordinates || !s.packed || !s.nodes || !s.weights || !s.indices || !s.parts || vertices > 0xFFFF)
	{
		free(shaders);
		h2w_section_free(&s);
		h3w_model_done(cache, mode_tag, sp);
		return FALSE;
	}
	for (i = 0; i < sp->material_count; i++)
	{
		shaders[i] = skip && skip[0] && strstr(skip, sp->materials[i].name) ? NONE : h3w_shader(build, sp, (short)i);
		/* (a counter's digits: the weapon's rounds'; none made: not drawn - a
		still digit would lie) */
		if (sp->materials[i].digit)
			shaders[i] = h3w_digit_shader(build, sp, sp->materials[i].digit - 1);
	}
	for (p = 0; p < sp->part_count; p++)
	{
		struct h3_spartan_part const *part = &sp->parts[p];
		long v, k;

		s.parts[s.part_count].material = part->material;
		s.parts[s.part_count].first = s.index_count;
		s.parts[s.part_count].count = part->triangle_count * 3;
		s.part_count++;
		for (v = 0; v < part->vertex_count; v++)
		{
			struct h3_spartan_vertex const *in = &part->vertices[v];
			long o = s.vertex_count + v;
			int w0 = (int)(in->weight * 255.0f + 0.5f);

			if (w0 < 0)
				w0 = 0;
			if (w0 > 255)
				w0 = 255;
			if (in->nodes[0] == in->nodes[1])
				w0 = 255;
			else if (w0 < 1)
				w0 = 1;
			memcpy(s.positions + 3 * o, in->position, sizeof(in->position));
			if (offset)
			{
				s.positions[3 * o] += offset[0];
				s.positions[3 * o + 1] += offset[1];
				s.positions[3 * o + 2] += offset[2];
			}
			memcpy(s.coordinates + 2 * o, in->uv, sizeof(in->uv));
			s.packed[3 * o] = pack_vector(in->normal);
			s.packed[3 * o + 1] = pack_vector(in->binormal);
			s.packed[3 * o + 2] = pack_vector(in->tangent);
			s.nodes[4 * o] = in->nodes[0];
			s.nodes[4 * o + 1] = in->nodes[1];
			s.nodes[4 * o + 2] = s.nodes[4 * o + 3] = 0;
			s.weights[4 * o] = (unsigned char)w0;
			s.weights[4 * o + 1] = (unsigned char)(255 - w0);
			s.weights[4 * o + 2] = s.weights[4 * o + 3] = 0;
		}
		for (k = 0; k < part->triangle_count * 3; k++)
			s.indices[s.index_count + k] = (unsigned short)(part->triangles[k] + s.vertex_count);
		s.index_count += part->triangle_count * 3;
		s.vertex_count += part->vertex_count;
	}
	h2w_model_add_section(m, &s, shaders, sp->material_count, node_map, node_count);
	if (getenv("HALO_H2_DEBUG"))
		platform_log("h3 weapons: %s: %ld nodes, %ld markers, %ld parts, %ld triangles, %d materials, %d bitmaps -> %ld parts made",
			h3_cache_tag_name(cache, mode_tag), node_count, m->marker_count, (long)sp->part_count, sp->triangles, (int)sp->material_count,
			(int)sp->bitmap_count, m->made.part_count - parts_before);
	free(shaders);
	h2w_section_free(&s);
	h3w_model_done(cache, mode_tag, sp);
	return m->made.part_count > parts_before;
}

/* ---------- private code: the first-person graph */

/* an animation's data (the source's) decoded into samples (h2w_codec_read:
Halo 3's codec blocks are Halo 2's) */
H2_ALIGN static boolean h3w_animation_decode(struct h2_graph_source const *source, unsigned long entry, long node_count, short frames,
	struct h2w_samples *s)
{
	struct h2_animation_data d;
	unsigned long size;
	unsigned char const *flags;

	memset(s, 0, sizeof(*s));
	if (!source->animation_data(source->context, entry, &d) || !d.bytes)
		return FALSE;
	size = d.static_size + d.animated_size + d.static_flags + d.animated_flags + d.movement;
	s->frames = frames;
	if (frames < 1 || frames > 2048 || node_count > H2W_MAXIMUM_GRAPH_NODES || (d.static_flags % 3) || (d.animated_flags % 3) ||
		d.static_size > size || d.static_size + d.animated_size + d.static_flags + d.animated_flags > size)
	{
		return FALSE;
	}
	flags = d.bytes + d.static_size + d.animated_size;
	if (d.static_size && d.static_flags && !h2w_codec_read(d.bytes, size, 0, flags, (long)d.static_flags / 3, FALSE, s))
	{
		h2w_samples_free(s);
		return FALSE;
	}
	if (d.animated_flags && !h2w_codec_read(d.bytes, size, d.static_size, flags + d.static_flags, (long)d.animated_flags / 3, TRUE, s))
	{
		h2w_samples_free(s);
		return FALSE;
	}
	return TRUE;
}

/* the weapon's first-person graph (its jmad, through the source) made this
engine's, as h2w_graph_make makes a Halo 2 one: the graph's nodes those the
model has and their parents (the camera's control left out), each
"first_person:[dual:]<state>[:var<n>]" animation in its slot
(h2w_first_person_states), the variants chained, pitch_and_turn's blend
screen kept. No sounds yet (Halo 3's are not read: M-C5) */
H2_ALIGN static struct h2w_graph *h3w_graph_make(struct h2_graph_source const *source, long jmad, struct h2w_model const *model)
{
	struct h2_graph_layout const *layout = source->layout;
	struct h2w_graph *graph;
	unsigned long group_tag, address, nodes, animations, screens = 0;
	long node_count, animation_count, screen_count = 0, i, k;

	if (!(source->tag_get)(source->context, jmad, &group_tag, &address) || group_tag != 'jmad' ||
		!source->block(source->context, address + layout->nodes, layout->node_size, &node_count, &nodes) ||
		!source->block(source->context, address + layout->animations, layout->animation_size, &animation_count, &animations) ||
		node_count < 1 || node_count > H2W_MAXIMUM_GRAPH_NODES || !(graph = calloc(1, sizeof(*graph))))
	{
		return NULL;
	}
	source->block(source->context, address + layout->blend_screens, layout->blend_screen_size, &screen_count, &screens);
	/* the graph's nodes: the jmad's the model has, and their parents */
	{
		struct h2w_graph_node all[H2W_MAXIMUM_GRAPH_NODES];
		boolean keep[H2W_MAXIMUM_GRAPH_NODES];
		short map[H2W_MAXIMUM_GRAPH_NODES];

		for (i = 0; i < node_count; i++)
		{
			unsigned char const *c = source->at(source->context, nodes + layout->node_size * (unsigned long)i, layout->node_size);
			char text[64];

			memset(&all[i], 0, sizeof(all[i]));
			all[i].parent = NONE;
			keep[i] = FALSE;
			if (!c)
				continue;
			source->string(source->context, read32(c), text, sizeof(text));
			{
				char *p;

				for (p = text; *p; p++)
				{
					if (*p == '_')
						*p = ' ';
				}
			}
			snprintf(all[i].name, sizeof(all[i].name), "%s", text);
			all[i].parent = read16(c + 8);
			keep[i] = h2w_model_node(model, all[i].name) != NONE;
		}
		for (i = node_count - 1; i >= 0; i--)
		{
			if (keep[i] && all[i].parent >= 0 && all[i].parent < node_count)
				keep[all[i].parent] = TRUE;
		}
		for (i = 0; i < node_count; i++)
		{
			map[i] = NONE;
			if (!keep[i])
				continue;
			map[i] = (short)graph->node_count;
			graph->nodes[graph->node_count] = all[i];
			graph->nodes[graph->node_count].source = (short)i;
			graph->nodes[graph->node_count].parent = all[i].parent >= 0 && all[i].parent < node_count ? map[all[i].parent] : NONE;
			graph->node_count++;
		}
		for (i = 0; i < graph->node_count; i++)
		{
			graph->nodes[i].child = graph->nodes[i].sibling = NONE;
			for (k = 0; k < graph->node_count && graph->nodes[i].child == NONE; k++)
			{
				if (graph->nodes[k].parent == i)
					graph->nodes[i].child = (short)k;
			}
			for (k = i + 1; k < graph->node_count; k++)
			{
				if (graph->nodes[k].parent == graph->nodes[i].parent)
				{
					graph->nodes[i].sibling = (short)k;
					break;
				}
			}
			/* (the model's node of the same index must be this one: this
			engine's node matrices are one array for both) */
			if (i >= model->node_count || strcmp(model->nodes[i].name, graph->nodes[i].name))
				platform_log("h3 weapons: graph node %ld %s is model node %s: the first-person model's order differs", i, graph->nodes[i].name,
					i < model->node_count ? model->nodes[i].name : "-");
		}
		graph->source_count = node_count;
	}
	for (k = 0; k < H2W_FIRST_PERSON_SLOTS; k++)
		graph->slots[0][k] = graph->slots[1][k] = NONE;
	for (i = 0; i < animation_count && graph->animation_count < H2W_MAXIMUM_ANIMATIONS; i++)
	{
		unsigned long entry = animations + layout->animation_size * (unsigned long)i;
		unsigned char const *e = source->at(source->context, entry, layout->animation_size);
		char name[64], state[64];
		char const *colon;
		short slot = NONE, type, frames;
		struct h2w_samples s;
		struct h2w_animation *a;
		short set = 0;

		if (!e)
			continue;
		source->string(source->context, read32(e), name, sizeof(name));
		type = e[layout->a_type];
		frames = read16(e + layout->a_frames);
		if (getenv("HALO_H2_DEBUG") && atoi(getenv("HALO_H2_DEBUG")) >= 3)
			fprintf(stderr, "h3 weapons: all %s %ld '%s' type %d frames %d\n", source->tag_name(source->context, jmad), i, name, (int)type, (int)frames);
		if (strncmp(name, "first_person:", 13) || type < 0 || type > 2)
			continue;
		if (!strncmp(name + 13, "dual:", 5))
			set = 1;
		snprintf(state, sizeof(state), "%s", name + 13 + 5 * set);
		if ((colon = strchr(state, ':')) != NULL)
			state[colon - state] = 0;
		for (k = 0; k < (long)(sizeof(h2w_first_person_states) / sizeof(h2w_first_person_states[0])); k++)
		{
			if (!strcmp(state, h2w_first_person_states[k].state))
			{
				slot = h2w_first_person_states[k].slot;
				break;
			}
		}
		if (slot == NONE)
			continue;
		/* (this engine's kind: an overlay's slots are overlays; a replacement
		- Halo 3's ammunition - is not made yet) */
		if (type == 2)
			continue;
		if ((slot == 3 || slot == 4 || slot == 15 || slot == 17 || slot == H2W_PITCH_AND_TURN_SLOT) != (type == 1))
			continue;
		if (slot == H2W_PITCH_AND_TURN_SLOT)
		{
			int screen = (signed char)e[layout->a_screen];
			unsigned char const *b;

			if (screen < 0 || screen >= screen_count ||
				!(b = source->at(source->context, screens + layout->blend_screen_size * (unsigned long)screen, layout->blend_screen_size)) ||
				read16(b + 12) != 1 || read16(b + 14) != 1 || read16(b + 24) != 1 || read16(b + 26) != 1 || frames != 9)
			{
				continue;
			}
			graph->pitch_and_turn_screen[set][0] = readf(b + 4);
			graph->pitch_and_turn_screen[set][1] = readf(b + 8);
			graph->pitch_and_turn_screen[set][2] = readf(b + 16);
			graph->pitch_and_turn_screen[set][3] = readf(b + 20);
			/* (b is good no longer after the next read) */
			e = source->at(source->context, entry, layout->animation_size);
			if (!e)
				continue;
		}
		if (!h3w_animation_decode(source, entry, node_count, frames, &s))
		{
			if (getenv("HALO_H2_DEBUG"))
				fprintf(stderr, "h3 weapons: animation %s not decoded\n", name);
			continue;
		}
		/* (the samples of the graph's nodes, in its order) */
		{
			struct h2w_samples kept;
			long n;

			memset(&kept, 0, sizeof(kept));
			kept.frames = s.frames;
			for (n = 0; n < graph->node_count; n++)
			{
				short from = graph->nodes[n].source;

				kept.rotation[n] = s.rotation[from], s.rotation[from] = NULL;
				kept.translation[n] = s.translation[from], s.translation[from] = NULL;
				kept.scale[n] = s.scale[from], s.scale[from] = NULL;
				kept.rotation_animated[n] = s.rotation_animated[from];
				kept.translation_animated[n] = s.translation_animated[from];
				kept.scale_animated[n] = s.scale_animated[from];
			}
			h2w_samples_free(&s);
			s = kept;
		}
		a = &graph->animations[graph->animation_count];
		if (!h2w_animation_make(graph, model, &s, type == 1 ? _animation_overlay : _animation_base, name, a))
		{
			h2w_samples_free(&s);
			continue;
		}
		h2w_samples_free(&s);
		a->slot = slot;
		a->set = set;
		a->sound = a->graph_sound = NONE;
		a->second_key_frame = NONE;
		/* its key frames (frame events: type 0 the primary, 1 the secondary -
		a melee's strike, a reload's magazine) */
		{
			long event_count = 0, ev;
			unsigned long events = 0;

			e = source->at(source->context, entry, layout->animation_size);
			if (e && source->block(source->context, entry + layout->a_events, 4, &event_count, &events))
			{
				for (ev = event_count - 1; ev >= 0; ev--)
				{
					unsigned char const *q = source->at(source->context, events + 4 * (unsigned long)ev, 4);

					if (!q)
						continue;
					if (read16(q) == 0)
						a->key_frame = read16(q + 2);
					else if (read16(q) == 1)
						a->second_key_frame = read16(q + 2);
				}
			}
		}
		/* (a variant: the slot's list) */
		if (graph->slots[set][slot] == NONE)
			graph->slots[set][slot] = (short)graph->animation_count;
		else
		{
			short last = graph->slots[set][slot];

			while (graph->animations[last].next != NONE)
				last = graph->animations[last].next;
			graph->animations[last].next = (short)graph->animation_count;
		}
		graph->animation_count++;
	}
	/* the variants' normalized weights and parents, the slot aliases */
	for (i = 0; i < 2; i++)
	{
		for (k = 0; k < H2W_FIRST_PERSON_SLOTS; k++)
		{
			short a = graph->slots[i][k];
			long count = 0, done = 0;

			for (; a != NONE; a = graph->animations[a].next)
				count++;
			for (a = graph->slots[i][k]; a != NONE; a = graph->animations[a].next)
			{
				done++;
				graph->animations[a].normalized_weight = (float)done / (float)count;
				graph->animations[a].parent = graph->slots[i][k];
			}
		}
		for (k = 0; k < (long)(sizeof(h2w_slot_aliases) / sizeof(h2w_slot_aliases[0])); k++)
		{
			if (graph->slots[i][h2w_slot_aliases[k][0]] == NONE)
				graph->slots[i][h2w_slot_aliases[k][0]] = graph->slots[i][h2w_slot_aliases[k][1]];
		}
	}
	graph->dual = graph->slots[1][0] != NONE;
	if (graph->dual)
	{
		for (k = 0; k < H2W_FIRST_PERSON_SLOTS; k++)
		{
			if (graph->slots[1][k] == NONE)
			{
				graph->slots[1][k] = graph->slots[0][k];
				if (k == H2W_PITCH_AND_TURN_SLOT)
					memcpy(graph->pitch_and_turn_screen[1], graph->pitch_and_turn_screen[0], sizeof(graph->pitch_and_turn_screen[1]));
			}
		}
	}
	if (graph->slots[0][0] == NONE)
	{
		h2w_graph_free(graph);
		return NULL;
	}
	if (getenv("HALO_H2_DEBUG"))
	{
		fprintf(stderr, "h3 weapons: graph %s: %ld nodes, %ld animations:", source->tag_name(source->context, jmad), graph->node_count,
			graph->animation_count);
		for (i = 0; i < graph->animation_count; i++)
			fprintf(stderr, " %s(%d,%d frames)", graph->animations[i].name, graph->animations[i].slot, graph->animations[i].frame_count);
		fprintf(stderr, "\n");
	}
	return graph;
}

/* ---------- private code: the weapon */

/* a Halo 3 projectile's fields (its proj at tag) */
H2_ALIGN static void h3w_projectile_read(struct h3_cache *cache, long tag, struct h2w_projectile *p)
{
	struct h3_cache_tag t;
	unsigned long a;

	memset(p, 0, sizeof(*p));
	p->tag = NONE;
	p->detonation_damage = p->attached_detonation_damage = p->super_detonation_damage = p->attached_super_detonation_damage =
		p->impact_damage = NONE;
	if (tag == NONE || !h3_cache_tag_get(cache, tag, &t) || t.group_tag != 'proj')
		return;
	a = t.address;
	/* (its tag Halo 3's index: h2w_projectile_write writes a projectile with
	one - its speeds, ranges, flags - and reads no Halo 2 map by it; its
	damage tags stay NONE - h3_weapons_damage_write writes the impact's) */
	p->tag = tag;
	p->flags = h3_cache_read32(cache, a + H3W_PROJ_FLAGS);
	p->timer_starts = h3_cache_read16(cache, a + H3W_PROJ_TIMER_STARTS);
	p->collision_radius = h3_cache_readf(cache, a + H3W_PROJ_COLLISION_RADIUS);
	p->arming_time = h3_cache_readf(cache, a + H3W_PROJ_ARMING_TIME);
	p->danger_radius = h3_cache_readf(cache, a + H3W_PROJ_DANGER_RADIUS);
	p->timer[0] = h3_cache_readf(cache, a + H3W_PROJ_TIMER);
	p->timer[1] = h3_cache_readf(cache, a + H3W_PROJ_TIMER + 4);
	p->minimum_velocity = h3_cache_readf(cache, a + H3W_PROJ_MINIMUM_VELOCITY);
	p->maximum_range = h3_cache_readf(cache, a + H3W_PROJ_MAXIMUM_RANGE);
	p->air_gravity_scale = h3_cache_readf(cache, a + H3W_PROJ_AIR_GRAVITY);
	p->air_damage_range[0] = h3_cache_readf(cache, a + H3W_PROJ_AIR_DAMAGE_RANGE);
	p->air_damage_range[1] = h3_cache_readf(cache, a + H3W_PROJ_AIR_DAMAGE_RANGE + 4);
	p->water_gravity_scale = h3_cache_readf(cache, a + H3W_PROJ_WATER_GRAVITY);
	p->water_damage_range[0] = h3_cache_readf(cache, a + H3W_PROJ_WATER_DAMAGE_RANGE);
	p->water_damage_range[1] = h3_cache_readf(cache, a + H3W_PROJ_WATER_DAMAGE_RANGE + 4);
	p->velocity[0] = h3_cache_readf(cache, a + H3W_PROJ_VELOCITY);
	p->velocity[1] = h3_cache_readf(cache, a + H3W_PROJ_VELOCITY + 4);
	p->guided_angular_velocity[0] = h3_cache_readf(cache, a + H3W_PROJ_GUIDED);
	p->guided_angular_velocity[1] = h3_cache_readf(cache, a + H3W_PROJ_GUIDED + 4);
	p->acceleration_range[0] = h3_cache_readf(cache, a + H3W_PROJ_ACCELERATION_RANGE);
	p->acceleration_range[1] = h3_cache_readf(cache, a + H3W_PROJ_ACCELERATION_RANGE + 4);
	platform_log("h3 weapons: %s: flags %lx, timer %d %.2f..%.2f, range %.0f, velocity %.1f..%.1f over %.2f..%.2f, damage range %.1f..%.1f, "
		"gravity %.2f, collision radius %.3f", tag_short_name(h3_cache_tag_name(cache, tag)), p->flags, (int)p->timer_starts, p->timer[0],
		p->timer[1], p->maximum_range, p->velocity[0], p->velocity[1], p->acceleration_range[0], p->acceleration_range[1],
		p->air_damage_range[0], p->air_damage_range[1], p->air_gravity_scale, p->collision_radius);
}

/* a Halo 3 damage effect's bounds (minimum, lower, upper), acceleration and
flags; FALSE if there is none */
H2_ALIGN static boolean h3w_damage_read(struct h3_cache *cache, long tag, float *out, unsigned long *flags, char *category, long category_size)
{
	struct h3_cache_tag t;

	out[0] = out[1] = out[2] = out[3] = 0.0f;
	*flags = 0;
	if (category && category_size > 0)
		category[0] = 0;
	if (tag == NONE || !h3_cache_tag_get(cache, tag, &t) || t.group_tag != 'jpt!')
		return FALSE;
	out[0] = h3_cache_readf(cache, t.address + H3W_JPT_DAMAGE);
	out[1] = h3_cache_readf(cache, t.address + H3W_JPT_DAMAGE + 4);
	out[2] = h3_cache_readf(cache, t.address + H3W_JPT_DAMAGE + 8);
	out[3] = h3_cache_readf(cache, t.address + H3W_JPT_ACCELERATION);
	*flags = h3_cache_read32(cache, t.address + H3W_JPT_FLAGS);
	if (category && category_size > 0)
		snprintf(category, (size_t)category_size, "%s", h3_cache_string(cache, h3_cache_read32(cache, t.address + H3W_JPT_CATEGORY)));
	return TRUE;
}

/* a Halo 3 damage effect read whole (h3w_jpt): FALSE (valid FALSE) if
there is none */
H2_ALIGN static boolean h3w_jpt_read(struct h3_cache *cache, long tag, struct h3w_jpt *j)
{
	struct h3_cache_tag t;
	float d[4];

	memset(j, 0, sizeof(*j));
	j->tag = NONE;
	if (tag == NONE || !h3_cache_tag_get(cache, tag, &t) || t.group_tag != 'jpt!')
		return FALSE;
	j->tag = tag;
	j->radius[0] = h3_cache_readf(cache, t.address + H3W_JPT_RADIUS);
	j->radius[1] = h3_cache_readf(cache, t.address + H3W_JPT_RADIUS + 4);
	j->radius[2] = h3_cache_readf(cache, t.address + H3W_JPT_CORE_RADIUS);
	h3w_damage_read(cache, tag, d, &j->flags, j->category, sizeof(j->category));
	j->damage[0] = d[0];
	j->damage[1] = d[1];
	j->damage[2] = d[2];
	j->acceleration = d[3];
	j->side_effect = h3_cache_read16(cache, t.address + 0x10);
	snprintf(j->name, sizeof(j->name), "%s", tag_short_name(h3_cache_tag_name(cache, tag)));
	j->valid = TRUE;
	return TRUE;
}

/* a barrel's projectile (at b, the barrel) and that projectile's damage
effects into p and the h3 weapon's [which] (the proj's 0x158 detonation,
0x168 attached detonation, 0x188 super detonation, 0x1AC attached super
detonation, 0x1F0 impact: Halo 2's five, 0x58 further); whether a barrel's
firing effects fire no shot */
H2_ALIGN static void h3w_barrel_projectile_read(struct h3_cache *cache, unsigned long b, struct h2w_weapon *w, int which)
{
	struct h3w_weapon *h3 = w->h3;
	struct h2w_projectile *p = &w->projectiles[which];
	long projectile = h3_cache_reference(cache, b + H3W_BARREL_PROJECTILE), count;
	unsigned long effects;
	struct h3_cache_tag pt;

	h3->shots[which] = h3_cache_read16(cache, b + H3W_BARREL_SHOTS);
	h3->heat[which] = h3_cache_readf(cache, b + H3W_BARREL_HEAT);
	h3->age[which] = h3_cache_readf(cache, b + H3W_BARREL_AGE);
	h3->fires_nothing[which] = h3_cache_block(cache, b + H3W_FIRING_EFFECTS, H3W_FIRING_EFFECT_SIZE, &count, &effects) && count > 0 &&
		h3_cache_read16(cache, effects + 2) == 0;
	h3w_projectile_read(cache, projectile, p);
	h3w_jpt_read(cache, NONE, &h3->impact[which]);
	h3w_jpt_read(cache, NONE, &h3->detonation[which]);
	h3w_jpt_read(cache, NONE, &h3->attached[which]);
	h3w_jpt_read(cache, NONE, &h3->super_[which]);
	h3w_jpt_read(cache, NONE, &h3->attached_super[which]);
	if (p->tag != NONE && h3_cache_tag_get(cache, projectile, &pt))
	{
		h3w_jpt_read(cache, h3_cache_reference(cache, pt.address + H3W_PROJ_IMPACT_DAMAGE), &h3->impact[which]);
		h3w_jpt_read(cache, h3_cache_reference(cache, pt.address + H3W_PROJ_DETONATION_DAMAGE), &h3->detonation[which]);
		h3w_jpt_read(cache, h3_cache_reference(cache, pt.address + H3W_PROJ_ATTACHED_DAMAGE), &h3->attached[which]);
		h3w_jpt_read(cache, h3_cache_reference(cache, pt.address + H3W_PROJ_SUPER_DAMAGE), &h3->super_[which]);
		h3w_jpt_read(cache, h3_cache_reference(cache, pt.address + H3W_PROJ_ATTACHED_SUPER_DAMAGE), &h3->attached_super[which]);
	}
}

/* a melee weapon's strike explosion: the damage effect with an area among
the weapon's own folder's (objects\weapons\melee\gravity_hammer\
damage_effects\gravity_hammer_explosion: the damage part of its impact
effect, fx\gravity_hammer_impact, which its strike's material effect
plays where it lands - the strongest where there are several); FALSE
none */
H2_ALIGN static boolean h3w_area_damage_find(struct h3_cache *cache, char const *weapon_name, struct h3w_weapon *h3)
{
	char folder[128];
	char const *slash = strrchr(weapon_name, '\\');
	long i, best = NONE;
	float best_damage = 0.0f;

	h3w_jpt_read(cache, NONE, &h3->area);
	if (!slash)
		return FALSE;
	snprintf(folder, sizeof(folder), "%.*s\\", (int)(slash - weapon_name), weapon_name);
	for (i = 0; i < h3_cache_tag_count(cache); i++)
	{
		struct h3_cache_tag t;
		char const *name;
		float radius, damage;

		if (!h3_cache_tag_get(cache, i, &t) || t.group_tag != 'jpt!' || !(name = h3_cache_tag_name(cache, i)) || strncmp(name, folder, strlen(folder)))
			continue;
		radius = h3_cache_readf(cache, t.address + H3W_JPT_RADIUS + 4);
		damage = h3_cache_readf(cache, t.address + H3W_JPT_DAMAGE + 8);
		if (radius > 0.0f && damage > best_damage)
		{
			best = i;
			best_damage = damage;
		}
	}
	return best != NONE && h3w_jpt_read(cache, best, &h3->area);
}

/* the weapon's values Halo 3's (the weap at address): flags, magazine,
trigger (its charge), barrels (the primary's rate, burst, error, recoil,
heat and age; the secondary's projectile), projectiles and damage, heat,
melee (a sword kind's lunge), zoom, aim assist */
H2_ALIGN static void h3w_weapon_values_read(struct h3_cache *cache, unsigned long address, struct h2w_weapon *w)
{
	struct h3w_weapon *h3 = w->h3;
	long count, i;
	unsigned long block;
	short primary = 0, secondary = NONE;

	h3->flags = h3_cache_read32(cache, address + H3W_WEAP_FLAGS);
	/* (Halo 2's "can be dual wielded" is bit 22: h2_weapons_write reads it
	there, with the graph's dual set) */
	w->h2_flags = (h3->flags & (1UL << H3W_WEAP_DUAL_WIELD_BIT)) ? 1UL << 22 : 0;
	for (i = 0; i < 7; i++)
		w->heat[i] = h3_cache_readf(cache, address + H3W_WEAP_HEAT + 4 * (unsigned long)i);
	if (h3_cache_block(cache, address + H3W_WEAP_MAGAZINES, H3W_MAGAZINE_SIZE, &count, &block) && count > 0)
	{
		w->rounds_initial = h3_cache_read16(cache, block + 6);
		w->rounds_maximum = h3_cache_read16(cache, block + 8);
		w->rounds_loaded = h3_cache_read16(cache, block + 0xA);
		w->rounds_held = h3_cache_read16(cache, block + 0xC);
		w->reload_time = h3_cache_readf(cache, block + 0x10);
		w->rounds_reloaded = h3_cache_read16(cache, block + 0x14);
	}
	if (h3_cache_block(cache, address + H3W_WEAP_TRIGGERS, H3W_TRIGGER_SIZE, &count, &block) && count > 0)
	{
		w->trigger_behavior = h3_cache_read16(cache, block + 6);
		primary = h3_cache_read16(cache, block + 8);
		secondary = h3_cache_read16(cache, block + H3W_TRIGGER_SECONDARY_BARREL);
		h3->charging_time = h3_cache_readf(cache, block + H3W_TRIGGER_CHARGING_TIME);
		h3->charged_time = h3_cache_readf(cache, block + H3W_TRIGGER_CHARGED_TIME);
		h3->overcharged_action = h3_cache_read16(cache, block + H3W_TRIGGER_OVERCHARGED_ACTION);
		h3->charged_illumination = h3_cache_readf(cache, block + H3W_TRIGGER_CHARGED_ILLUMINATION);
		h3->spew_time = h3_cache_readf(cache, block + H3W_TRIGGER_SPEW_TIME);
		h3->charges = h3->charging_time > 0.0f;
	}
	w->barrel = 0;
	w->barrel_index = w->second_barrel_index = NONE;
	h3w_projectile_read(cache, NONE, &w->projectiles[0]);
	h3w_projectile_read(cache, NONE, &w->projectiles[1]);
	if (h3_cache_block(cache, address + H3W_WEAP_BARRELS, H3W_BARREL_SIZE, &count, &block) && count > 0)
	{
		unsigned long b;

		if (primary < 0 || primary >= count)
			primary = 0;
		w->barrel_index = primary;
		b = block + H3W_BARREL_SIZE * (unsigned long)primary;
		/* (its flags' bits are not Halo 2's, unread: none passed to the trigger) */
		w->barrel_flags = 0;
		w->rounds_per_second = h3_cache_readf(cache, b + 4);
		w->rounds_per_second_maximum = h3_cache_readf(cache, b + 8);
		w->rate_times[0] = h3_cache_readf(cache, b + 0xC);
		w->rate_times[1] = h3_cache_readf(cache, b + 0x10);
		w->burst_shots = h3_cache_read16(cache, b + H3W_BARREL_SHOTS);
		w->burst_recovery = h3_cache_readf(cache, b + 0x20);
		w->rounds_between_tracers = h3_cache_read16(cache, b + 0x2E);
		w->error_times[0] = h3_cache_readf(cache, b + 0x38);
		w->error_times[1] = h3_cache_readf(cache, b + 0x3C);
		w->dual_error_times[0] = h3_cache_readf(cache, b + 0x54);
		w->dual_error_times[1] = h3_cache_readf(cache, b + 0x58);
		w->dual_error_minimum = h3_cache_readf(cache, b + 0x64);
		w->dual_error_angle[0] = h3_cache_readf(cache, b + 0x68);
		w->dual_error_angle[1] = h3_cache_readf(cache, b + 0x6C);
		w->dual_damage_scale = h3_cache_readf(cache, b + 0x70);
		/* (1 on every weapon, the shotgun's and mauler's too: their pellets
		are not in this field - open, AUDIT.md) */
		w->projectiles_per_shot = h3_cache_read16(cache, b + 0x76);
		w->distribution_angle = h3_cache_readf(cache, b + 0x78);
		w->error_minimum = h3_cache_readf(cache, b + 0x7C);
		w->error_angle[0] = h3_cache_readf(cache, b + 0x80);
		w->error_angle[1] = h3_cache_readf(cache, b + 0x84);
		w->heat_per_round = h3_cache_readf(cache, b + H3W_BARREL_HEAT);
		w->age_per_round = h3_cache_readf(cache, b + H3W_BARREL_AGE);
		w->recoil_angle[0] = h3_cache_readf(cache, b + 0xF4);
		w->recoil_angle[1] = h3_cache_readf(cache, b + 0xF8);
		w->recoil_times[0] = h3_cache_readf(cache, b + 0xFC);
		w->recoil_times[1] = h3_cache_readf(cache, b + 0x100);
		w->recoil_function = h3_cache_read16(cache, b + 0x104);
		h3w_barrel_projectile_read(cache, b, w, 0);
		if (w->projectiles[0].tag != NONE)
		{
			w->projectile_range = w->projectiles[0].maximum_range;
			w->projectile_gravity = w->projectiles[0].air_gravity_scale;
			w->projectile_velocity[0] = w->projectiles[0].velocity[0];
			w->projectile_velocity[1] = w->projectiles[0].velocity[1];
			h3->maximum_range = w->projectiles[0].maximum_range;
			w->damage[0] = h3->impact[0].damage[0];
			w->damage[1] = h3->impact[0].damage[1];
			w->damage[2] = h3->impact[0].damage[2];
			w->damage_acceleration = h3->impact[0].acceleration;
			w->damage_flags = h3->impact[0].flags;
		}
		/* its second barrel: the trigger's secondary where it is another
		(the plasma pistol's charged bolt, the laser's beam) */
		if (secondary >= 0 && secondary < count && secondary != primary)
		{
			w->second_barrel_index = secondary;
			h3w_barrel_projectile_read(cache, block + H3W_BARREL_SIZE * (unsigned long)secondary, w, 1);
		}
	}
	/* its melee: the pyramid and its strike's damage; a sword kind's lunge
	(Halo 2's rule, h2_weapons.c: at the aim assist's target within the
	autoaim angle and range) and the lunge's damage */
	for (i = 0; i < 3; i++)
		w->melee_pyramid[i] = h3_cache_readf(cache, address + H3W_WEAP_MELEE_PYRAMID + 4 * (unsigned long)i);
	{
		char category[32];

		w->hit_tags[0] = w->hit_tags[1] = NONE;
		w->response_tags[0] = w->response_tags[1] = NONE;
		w->lunge_hit[0] = w->lunge_hit[1] = w->lunge_hit[2] = w->lunge_hit[3] = 0.0f;
		h3w_damage_read(cache, h3_cache_reference(cache, address + H3W_WEAP_MELEE_DAMAGE), w->melee_hit, &w->hit_flags[0], category,
			sizeof(category));
		if (h2w_kinds[w->kind].sword)
		{
			h3w_damage_read(cache, h3_cache_reference(cache, address + H3W_WEAP_LUNGE_DAMAGE), w->lunge_hit, &w->hit_flags[1], category,
				sizeof(category));
			w->lunge_angle = h3_cache_readf(cache, address + H3W_WEAP_AUTOAIM);
			w->lunge_range = h3_cache_readf(cache, address + H3W_WEAP_AUTOAIM + 4);
		}
	}
	w->zoom_levels = h3_cache_read16(cache, address + H3W_WEAP_ZOOM_LEVELS);
	w->zoom[0] = h3_cache_readf(cache, address + H3W_WEAP_ZOOM);
	w->zoom[1] = h3_cache_readf(cache, address + H3W_WEAP_ZOOM + 4);
	w->autoaim[0] = h3_cache_readf(cache, address + H3W_WEAP_AUTOAIM);
	w->autoaim[1] = h3_cache_readf(cache, address + H3W_WEAP_AUTOAIM + 4);
	w->magnetism[0] = h3_cache_readf(cache, address + H3W_WEAP_AUTOAIM + 0xC);
	w->magnetism[1] = h3_cache_readf(cache, address + H3W_WEAP_AUTOAIM + 0x10);
	/* (no deviation angle in Halo 3's aim assist: the stand-in's kept) */
	w->deviation = -1.0f;
	w->movement_penalized = 0;
	w->ready_time = 0.0f;
	w->ground_scale = 0.0f;
	snprintf(h3->class_name, sizeof(h3->class_name), "%s", h3_cache_string(cache, h3_cache_read32(cache, address + H3W_WEAP_CLASS)));
	/* (a melee weapon's label its class's: the player graphs' sword and
	hammer classes have no weapon type of their own) */
	snprintf(h3->label, sizeof(h3->label), "%s",
		h2w_kinds[w->kind].sword ? h3->class_name : h3_cache_string(cache, h3_cache_read32(cache, address + H3W_WEAP_LABEL)));
}

/* Halo 3's weapon of a kind made on the stand-in the Halo 2 weapon of its
collection is made on; NULL if it cannot be */
H2_ALIGN static struct h2w_weapon *h3w_weapon_make(struct build *build, struct h3_cache *cache, long h3_kind, long kind)
{
	static char const *const arms_names[] = H3W_ARMS_MODES;
	struct h3_cache_tag t, hlmt;
	struct h2w_weapon *w;
	long tag = h3_cache_find_tag(cache, 'weap', h3w_kinds[h3_kind].h3), count, i;
	unsigned long block;

	if (tag == NONE || !h3_cache_tag_get(cache, tag, &t) || !(w = calloc(1, sizeof(*w))))
		return NULL;
	w->h3 = calloc(1, sizeof(*w->h3));
	if (!w->h3)
	{
		free(w);
		return NULL;
	}
	w->kind = kind;
	w->h3->h3_kind = h3_kind;
	w->h2_tag = NONE;
	w->base_weapon = NONE;
	w->ammo_tag = w->ammo_slot = NONE;
	w->light_tag = w->light_slot = NONE;
	w->damage_tag = w->hit_tags[0] = w->hit_tags[1] = NONE;
	w->barrel_index = w->second_barrel_index = NONE;
	h2w_model_begin(&w->ammo_model);
	h3w_projectile_read(cache, NONE, &w->projectiles[0]);
	h3w_projectile_read(cache, NONE, &w->projectiles[1]);
	snprintf(w->h3->h3_name, sizeof(w->h3->h3_name), "%s", tag_short_name(h3w_kinds[h3_kind].h3));
	/* (its own name: the Halo 2 weapon of the collection is read again at
	load by its name - h2_weapon_fields_map_loaded - which must not find
	this one) */
	snprintf(w->name, sizeof(w->name), "h3_%s", w->h3->h3_name);
	h2w_model_begin(&w->world);
	h2w_model_begin(&w->first_person);
	h2w_model_begin(&w->first_person_elite);
	/* the world model: its hlmt's render model */
	if (h3_cache_tag_get(cache, h3_cache_reference(cache, t.address + H3W_WEAP_MODEL), &hlmt) && hlmt.group_tag == 'hlmt')
	{
		if (h3w_model_add(build, &w->world, cache, h3_cache_reference(cache, hlmt.address), NULL, NULL))
			h2w_model_finish(build, &w->world);
	}
	/* the first-person model and graph: the Spartan's arms and the weapon's
	own first-person model as one, the Spartan's graph for it */
	if (h3_cache_block(cache, t.address + H3W_WEAP_FIRST_PERSON, H3W_FIRST_PERSON_SIZE, &count, &block) && count > 0)
	{
		long fp_model = h3_cache_reference(cache, block);
		long jmad = h3_cache_reference(cache, block + 0x10);
		long arms = NONE;

		for (i = 0; i < (long)(sizeof(arms_names) / sizeof(arms_names[0])) && arms == NONE; i++)
			arms = h3_cache_find_tag(cache, 'mode', arms_names[i]);
		if (arms != NONE && fp_model != NONE && jmad != NONE &&
			h3w_model_add(build, &w->first_person, cache, arms, NULL, NULL) && h3w_model_add(build, &w->first_person, cache, fp_model, NULL, NULL))
		{
			struct h2_graph_source source;
			struct h3_animation_source *opened;
			char why[160];

			why[0] = 0;
			opened = h3_animation_source_open(cache, jmad, &source, why, sizeof(why));
			if (opened)
			{
				w->graph = h3w_graph_make(&source, jmad, &w->first_person);
				h3_animation_source_close(opened);
			}
			else
				platform_log("h3 weapons: %s: its first-person graph not opened: %s", w->name, why);
			if (w->graph)
				h2w_model_finish(build, &w->first_person);
		}
		else
			platform_log("h3 weapons: %s: no first-person arms, model or graph (%ld %ld %ld)", w->name, arms, fp_model, jmad);
	}
	h3w_weapon_values_read(cache, t.address, w);
	/* (a melee weapon's strike explosion: the gravity hammer's) */
	if (h2w_kinds[kind].sword)
		h3w_area_damage_find(cache, h3w_kinds[h3_kind].h3, w->h3);
	/* its firing effects: the stand-in's, its sound kept (Halo 3's sounds
	are not read yet) */
	w->firing_effect_count = 1;
	w->firing_sounds[0] = H2W_SOUND_KEEP;
	w->firing_sounds[1] = NONE;
	platform_log("h3 weapons: %s: world model %ld (%ld nodes, %ld markers), first person %ld (%ld nodes, %ld markers), graph %s; "
		"label %s (class %s), flags %lx%s; magazine %d/%d/%d (reserve %d, reloaded %d), %.1f rounds a second, shots a fire %d, recovery %.2f s, "
		"behavior %d, error %.3f..%.3f deg (minimum %.3f, rise %.2f fall %.2f), zoom %d x%.1f..%.1f, autoaim %.1f deg %.1f, "
		"magnetism %.1f deg %.1f, projectile %.0f..%.0f wu/s range %.0f, damage %.1f..%.1f (%s, flags %lx), heat %.2f a round "
		"(thresholds %.2f/%.2f, loss %.3f/s, overheated %.3f/s), age %.3f a round, melee %.0f (pyramid %.3f %.3f %.3f)",
		w->name, w->world.model, w->world.node_count, w->world.marker_count, w->first_person.model, w->first_person.node_count,
		w->first_person.marker_count, w->graph ? "made" : "none", w->h3->label, w->h3->class_name, w->h3->flags,
		(w->h2_flags & (1UL << 22)) ? " (dual wielded)" : "", w->rounds_loaded, w->rounds_initial, w->rounds_maximum,
		w->rounds_held, w->rounds_reloaded, w->rounds_per_second, (int)w->burst_shots, w->burst_recovery, (int)w->trigger_behavior,
		w->error_angle[0] * 57.29578f, w->error_angle[1] * 57.29578f, w->error_minimum * 57.29578f, w->error_times[0], w->error_times[1],
		(int)w->zoom_levels, w->zoom[0], w->zoom[1], w->autoaim[0] * 57.29578f, w->autoaim[1], w->magnetism[0] * 57.29578f, w->magnetism[1],
		w->projectile_velocity[0], w->projectile_velocity[1], w->projectile_range, w->damage[1], w->damage[2], w->h3->impact[0].category,
		w->damage_flags, w->heat_per_round, w->heat[0], w->heat[1], w->heat[4], w->heat[6], w->age_per_round, w->melee_hit[2],
		w->melee_pyramid[0], w->melee_pyramid[1], w->melee_pyramid[2]);
	for (i = 0; i < 2; i++)
	{
		struct h3w_jpt const *js[4] = { &w->h3->detonation[i], &w->h3->attached[i], &w->h3->super_[i], &w->h3->attached_super[i] };
		static char const *const names[4] = { "detonation", "attached detonation", "super detonation", "attached super detonation" };
		long k;

		for (k = 0; k < 4; k++)
		{
			if (js[k]->valid)
			{
				platform_log("h3 weapons: %s: %s%s %s: radius %.2f..%.2f (core %.2f), damage %.0f / %.0f..%.0f, acceleration %.2f, flags %lx, "
					"side effect %d, %s", w->name, i ? "second barrel's " : "", names[k], js[k]->name, js[k]->radius[0], js[k]->radius[1],
					js[k]->radius[2], js[k]->damage[0], js[k]->damage[1], js[k]->damage[2], js[k]->acceleration, js[k]->flags,
					(int)js[k]->side_effect, js[k]->category);
			}
		}
	}
	if (w->second_barrel_index != NONE)
	{
		platform_log("h3 weapons: %s: second barrel %ld: %s%d shots a fire, heat %.2f age %.3f a round, projectile %.0f..%.0f wu/s range %.0f, "
			"impact damage %.1f..%.1f (%s, flags %lx, side effect %d); trigger %s: charging %.2f s, charged %.2f s, overcharged action %d, "
			"illumination %.2f, spew %.2f s%s", w->name, w->second_barrel_index, w->h3->fires_nothing[1] ? "fires no shot, " : "",
			(int)w->h3->shots[1], w->h3->heat[1], w->h3->age[1], w->projectiles[1].velocity[0], w->projectiles[1].velocity[1],
			w->projectiles[1].maximum_range, w->h3->impact[1].damage[1], w->h3->impact[1].damage[2], w->h3->impact[1].category,
			w->h3->impact[1].flags, (int)w->h3->impact[1].side_effect, w->h3->charges ? "charges" : "no charge", w->h3->charging_time,
			w->h3->charged_time, (int)w->h3->overcharged_action, w->h3->charged_illumination, w->h3->spew_time,
			w->h3->fires_nothing[0] ? "; the first barrel fires no shot" : "");
	}
	if (h2w_kinds[kind].sword)
	{
		platform_log("h3 weapons: %s: a melee weapon: lunge %.0f (flags %lx) within %.1f deg, %.1f units (its autoaim's, Halo 2's rule)%s",
			w->name, w->lunge_hit[2], w->hit_flags[1], w->lunge_angle * 57.29578f, w->lunge_range, w->h3->area.valid ? "" : "; no strike explosion");
		if (w->h3->area.valid)
		{
			platform_log("h3 weapons: %s: strike explosion %s: radius %.2f..%.2f (core %.2f), damage %.0f / %.0f..%.0f, acceleration %.2f, "
				"flags %lx, %s", w->name, w->h3->area.name, w->h3->area.radius[0], w->h3->area.radius[1], w->h3->area.radius[2],
				w->h3->area.damage[0], w->h3->area.damage[1], w->h3->area.damage[2], w->h3->area.acceleration, w->h3->area.flags,
				w->h3->area.category);
		}
	}
	return w;
}

/* ---------- private code: the grenades */

/* Halo 3's frag and plasma grenades on the kit's two (h2w_grenades' slots,
in their order: frag, plasma - Halo 2's made first, h2_weapons_gather,
replaced here): the thrown projectile's model (objects\weapons\grenade\
<name>\<name>: its hlmt's render model) in h2w.grenades[k], its values in
h2w.grenade_data[k] - the proj's flags, timer starts, arming, danger
radius, timer, air gravity, its detonation damage (radii, core, bounds,
acceleration) and its attached detonation's (the plasma grenade stuck) -
which h2w_grenades_write writes on the kit's grenade projectile; the Halo 2
tags NONE (the write's material modifiers, camera shake and flashes read
Halo 2's cache by tag: the kit's kept). Halo 3's spike (claymore_grenade)
and firebomb grenades are not made: this engine has two grenade types */
static boolean h3w_grenades_made;

H2_ALIGN static void h3_weapons_grenades_gather(struct build *build, struct h3_cache *cache)
{
	static char const *const names[H2W_GRENADE_COUNT] =
	{
		"objects\\weapons\\grenade\\frag_grenade\\frag_grenade",
		"objects\\weapons\\grenade\\plasma_grenade\\plasma_grenade",
	};
	long k;

	for (k = 0; k < H2W_GRENADE_COUNT; k++)
	{
		long tag = h3_cache_find_tag(cache, 'proj', names[k]);
		struct h3_cache_tag t, hlmt;
		struct h2w_grenade_data *g = &h2w.grenade_data[k];
		struct h3w_jpt explosion, attached;
		unsigned long a;

		if (tag == NONE || !h3_cache_tag_get(cache, tag, &t))
		{
			platform_log("h3 weapons: grenade %s not found: Halo 2's stays", names[k]);
			continue;
		}
		a = t.address;
		/* its model */
		if (h3_cache_tag_get(cache, h3_cache_reference(cache, a + H3W_WEAP_MODEL), &hlmt) && hlmt.group_tag == 'hlmt')
		{
			struct h2w_model m;

			h2w_model_begin(&m);
			if (h3w_model_add(build, &m, cache, h3_cache_reference(cache, hlmt.address), NULL, NULL))
			{
				h2w_model_finish(build, &m);
				h2w.grenades[k] = m;
			}
		}
		/* its values */
		h3w_jpt_read(cache, h3_cache_reference(cache, a + H3W_PROJ_DETONATION_DAMAGE), &explosion);
		h3w_jpt_read(cache, h3_cache_reference(cache, a + H3W_PROJ_ATTACHED_DAMAGE), &attached);
		if (!explosion.valid)
		{
			platform_log("h3 weapons: grenade %s: no detonation damage: Halo 2's values stay", tag_short_name(names[k]));
			continue;
		}
		memset(g, 0, sizeof(*g));
		g->valid = TRUE;
		g->flags = h3_cache_read32(cache, a + H3W_PROJ_FLAGS);
		g->timer_starts = h3_cache_read16(cache, a + H3W_PROJ_TIMER_STARTS);
		g->arming = h3_cache_readf(cache, a + H3W_PROJ_ARMING_TIME);
		g->danger_radius = h3_cache_readf(cache, a + H3W_PROJ_DANGER_RADIUS);
		g->timer[0] = h3_cache_readf(cache, a + H3W_PROJ_TIMER);
		g->timer[1] = h3_cache_readf(cache, a + H3W_PROJ_TIMER + 4);
		g->gravity_scale = h3_cache_readf(cache, a + H3W_PROJ_AIR_GRAVITY);
		/* (no material responses read: responses.count 0 keeps the stand-in's - 1.2.0's h2w_responses_write) */
		g->explosion.valid = TRUE;
		g->explosion.tag = NONE;
		g->explosion.radius[0] = explosion.radius[0];
		g->explosion.radius[1] = explosion.radius[1];
		g->explosion.core_radius = explosion.radius[2];
		g->explosion.damage_lower = explosion.damage[0];
		g->explosion.damage_upper[0] = explosion.damage[1];
		g->explosion.damage_upper[1] = explosion.damage[2];
		g->explosion.acceleration = explosion.acceleration;
		g->attached.valid = attached.valid;
		g->attached.tag = NONE;
		if (attached.valid)
		{
			g->attached.radius[0] = attached.radius[0];
			g->attached.radius[1] = attached.radius[1];
			g->attached.core_radius = attached.radius[2];
			g->attached.damage_lower = attached.damage[0];
			g->attached.damage_upper[0] = attached.damage[1];
			g->attached.damage_upper[1] = attached.damage[2];
			g->attached.acceleration = attached.acceleration;
		}
		h3w_grenades_made = TRUE;
		platform_log("h3 weapons: grenade %s: model %ld; flags %lx, timer starts %d, arming %.2f, danger radius %.2f, timer %.2f..%.2f, "
			"gravity %.2f; explosion %s radius %.2f..%.2f (core %.2f) damage %.0f / %.0f..%.0f acceleration %.2f%s%s", tag_short_name(names[k]),
			h2w.grenades[k].model, g->flags, (int)g->timer_starts, g->arming, g->danger_radius, g->timer[0], g->timer[1], g->gravity_scale,
			explosion.name, explosion.radius[0], explosion.radius[1], explosion.radius[2], explosion.damage[0], explosion.damage[1],
			explosion.damage[2], explosion.acceleration, attached.valid ? "; attached " : "", attached.valid ? attached.name : "");
	}
}

/* whether the grenades are Halo 3's: h2_weapons_map_loaded then leaves
Halo 2's values unwritten over them */
H2_ALIGN static boolean h3_weapons_grenades_h3(void)
{
	return h3w_grenades_made || h3_weapons_on();
}

/* ---------- private code: h2_weapons.c's hooks */

/* whether a Halo 2 kind is not made because Halo 3's weapon takes its
collection (HALO_H3_WEAPONS) */
H2_ALIGN static boolean h3_weapons_replaces(long kind)
{
	long i;

	if (!h3_weapons_on())
		return FALSE;
	for (i = 0; i < H3W_KIND_COUNT; i++)
	{
		if (!strcmp(h3w_kinds[i].collection, h2w_kinds[kind].collection))
			return TRUE;
	}
	return FALSE;
}

/* a weapon's animation label: Halo 3's weapon name where it is Halo 3's */
H2_ALIGN static char const *h3_weapons_label(struct h2w_weapon const *w)
{
	return w->h3 && w->h3->label[0] ? w->h3->label : h2w_kinds[w->kind].label;
}

/* the Halo 3 weapons gathered into h2w.weapons (after the Halo 2 kinds:
h2_weapons_gather) */
H2_ALIGN static void h3_weapons_gather(struct build *build)
{
	struct h3_cache *cache;
	char why[H3_CACHE_WHY_SIZE];
	long i;

	h3w_bitmaps.count = 0;
	h3w_grenades_made = FALSE;
	if (!h3_weapons_on() || (!h3w_level.on && (!h2_cache_is_mcc(build->cache) || h2_level_is_main_menu(build->cache))))
		return;
	why[0] = 0;
	cache = h3w_cache_open(why, sizeof(why));
	if (!cache)
	{
		platform_log("h3 weapons: not made: %s", why);
		return;
	}
	for (i = 0; i < H3W_KIND_COUNT && h2w.count < H2W_MAXIMUM_WEAPONS; i++)
	{
		struct h2w_weapon *w;
		long kind;

		for (kind = 0; kind < (long)(sizeof(h2w_kinds) / sizeof(h2w_kinds[0])) && strcmp(h2w_kinds[kind].collection, h3w_kinds[i].collection); kind++)
			;
		if (kind >= (long)(sizeof(h2w_kinds) / sizeof(h2w_kinds[0])))
			continue;
		/* (HALO_H3_SMG_SILENCED=0: the silenced SMG left out, as before) */
		if (!strcmp(h3w_kinds[i].collection, "smg_silenced") && getenv("HALO_H3_SMG_SILENCED") && !atoi(getenv("HALO_H3_SMG_SILENCED")))
			continue;
		/* (only the weapons the level can hand out, as h2_weapons_gather; a
		Halo 3 level's: its palettes' and the start's) */
		if (h3w_level.on ? !h3w_level_wants(h3w_kinds[i].h3, h2w_kinds[kind].collection) :
			(!h2w_level_uses(build->cache, h2w_kinds[kind].h2) && strcmp(h2w_kinds[kind].collection, "battle_rifle") &&
			strcmp(h2w_kinds[kind].collection, "smg") && !getenv("HALO_H2_ALL_WEAPONS")))
		{
			platform_log("h3 weapons: %s: the level hands out no %s: not made", h3w_kinds[i].h3, h2w_kinds[kind].collection);
			continue;
		}
		if (build->cache && build->gather_trim_bytes)
			h2_cache_trim_past(build->cache, build->gather_trim_bytes);
		{
			char unit[96];

			snprintf(unit, sizeof(unit), "the Halo 3 weapon %s", tag_short_name(h3w_kinds[i].h3));
			h2_build_heap_unit(unit);
		}
		w = h3w_weapon_make(build, cache, i, kind);
		if (w && w->world.model != NONE && w->first_person.model != NONE && w->graph)
			h2w.weapons[h2w.count++] = w;
		else if (w)
		{
			/* (the flamethrower and the turrets: no first-person set in the
			maps - Halo 3 carries a turret in third person; M-B5's) */
			platform_log("h3 weapons: %s not made (%s): the stand-in stays", h3w_kinds[i].h3,
				w->first_person.model == NONE && w->world.model != NONE ?
				"no first-person model or graph: Halo 3 carries it in third person, a turret - not made until the turrets are" :
				"its models or animations could not be read");
			h2w_graph_free(w->graph);
			free(w->h3);
			free(w);
		}
		else
			platform_log("h3 weapons: %s not found in %s", h3w_kinds[i].h3, h3_cache_name(cache));
	}
	h3_weapons_grenades_gather(build, cache);
	h3w_model_kept_free();
	if (cache != h3w_level.cache)
		h3_cache_close(cache);
}

/* a Halo 3 damage effect's values on a damage effect of the build at root
(a copy of a stand-in's: its material response kept): its radii, bounds,
acceleration, side effect and flags (Halo 3's first ten Halo 2's and this
engine's, its headshots - bit 1 - the multiplayer ones too, as
h2w_melee_values_write) */
H2_ALIGN static void h3w_jpt_write(struct region *r, unsigned long root, struct h3w_jpt const *j)
{
	unsigned long flags = (j->flags & 0x3FF) | ((j->flags & 2) ? 1UL << 11 : 0);

	if (j->radius[1] > 0.0f)
	{
		writef(region_at(r, root + offsetof(struct damage_effect_definition, falloff_radius)), j->radius[0]);
		writef(region_at(r, root + offsetof(struct damage_effect_definition, cutoff_radius)), j->radius[1]);
		writef(region_at(r, root + offsetof(struct damage_effect_definition, damage.area_of_effect_core_radius)), j->radius[2]);
	}
	writef(region_at(r, root + offsetof(struct damage_effect_definition, damage.damage_minimum)), j->damage[0]);
	writef(region_at(r, root + offsetof(struct damage_effect_definition, damage.damage_lower_bound)), j->damage[1]);
	writef(region_at(r, root + offsetof(struct damage_effect_definition, damage.damage_upper_bound)), j->damage[2]);
	writef(region_at(r, root + offsetof(struct damage_effect_definition, damage.instantaneous_acceleration)), j->acceleration);
	/* (Halo 3's side effect is this engine's enum: none, harmless, lethal
	to the unsuspecting, emp - the overcharge's 3) */
	if (j->side_effect >= 0 && j->side_effect <= 3)
		write16(region_at(r, root + offsetof(struct damage_effect_definition, damage.side_effect)), (unsigned short)j->side_effect);
	write32(region_at(r, root + offsetof(struct damage_effect_definition, damage.flags)), flags);
}

/* a Halo 3 damage effect made in a slot of the weapon's tags: the base
(a tag index of the build, NONE none) copied, else a bare effect, with
Halo 3's values; FALSE if the slot is taken or the region failed */
H2_ALIGN static boolean h3w_damage_slot_make(struct region *r, unsigned long instances, long slot, long base, struct h3w_jpt const *j)
{
	unsigned long from = base >= 0 ? h2w_root(r, instances, base) : 0, root;

	if (r->b.failed || read32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * slot + 20)))
		return FALSE;
	root = from ? h2w_tag_copy(r, from, sizeof(struct damage_effect_definition), 'jpt!') : region_alloc(r, sizeof(struct damage_effect_definition));
	if (r->b.failed)
		return FALSE;
	write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * slot + 20), r->link + root);
	if (!from)
	{
		long m;

		for (m = 0; m < MAXIMUM_NUMBER_OF_MATERIAL_TYPES; m++)
			writef(region_at(r, root + offsetof(struct damage_effect_definition, damage.material_modifiers) + 4 * m), 1.0f);
	}
	h3w_jpt_write(r, root, j);
	return TRUE;
}

/* a Halo 3 weapon's projectile's damage effects (after h2w_projectile_write,
which wrote the projectile with no Halo 2 damage tags): its impact damage
on the stand-in's impact damage effect in the weapon's damage slot, and -
the weapon's detonation slots, one set a weapon: the first projectile's
where it has them, else the second's (the laser's beam: its first barrel
fires nothing) - its detonation damage on the stand-in's detonation
effect's damage part, that effect copied again naming it (h2w_effect_damage_copy);
its attached detonation damage; and with proj flag bit 3 its super
detonation (the needler's supercombine) and attached super detonation
damage, the last through the projectile's port data
(h2w_projectile_values_write). which: 0 the first barrel's projectile
(slot the weapon's damage slot, weapon_index + 3), 1 the second's (slot
H2W_SLOT_SECOND_DAMAGE) */
H2_ALIGN static void h3_weapons_damage_write(struct build *build, struct region *r, unsigned long instances, unsigned long projectile_root,
	unsigned long base_projectile_root, long slot, struct h2w_weapon const *w, int which)
{
	struct h3w_weapon const *h3 = w->h3;
	long stand_in = base_projectile_root ?
		(short)read16(region_at(r, base_projectile_root + offsetof(struct projectile_definition, projectile.impact_damage) + 12)) : NONE;
	long stand_in_effect = base_projectile_root ?
		(short)read16(region_at(r, base_projectile_root + offsetof(struct projectile_definition, projectile.effect) + 12)) : NONE;
	long stand_in_super = base_projectile_root ?
		(short)read16(region_at(r, base_projectile_root + offsetof(struct projectile_definition, projectile.super_detonation) + 12)) : NONE;
	long stand_in_attached = base_projectile_root ?
		(short)read16(region_at(r, base_projectile_root + offsetof(struct projectile_definition, projectile.attached_detonation_damage) + 12)) : NONE;
	long weapon_index = slot - (which ? H2W_SLOT_SECOND_DAMAGE : 3);
	long effect_damage = h2w_effect_damage_part(r, instances, stand_in_effect);
	long super_damage = h2w_effect_damage_part(r, instances, stand_in_super);
	long attached_super = NONE;
	struct h2w_projectile const *p = &w->projectiles[which];
	int other = which ? 0 : 1;

	(void)build;
	if (!h3 || r->b.failed || which < 0 || which > 1)
		return;
	/* the impact */
	if (h3->impact[which].valid)
	{
		if (h3w_damage_slot_make(r, instances, slot, stand_in >= 0 ? stand_in : effect_damage, &h3->impact[which]))
			region_reference(r, projectile_root + offsetof(struct projectile_definition, projectile.impact_damage), 'jpt!', tag_id(slot));
	}
	else
		region_reference(r, projectile_root + offsetof(struct projectile_definition, projectile.impact_damage), 0, 0xFFFFFFFFUL);
	/* the detonations: this projectile's where the other has none */
	if (h3->detonation[other].valid || h3->attached[other].valid || h3->super_[other].valid)
	{
		if (h3->detonation[which].valid || h3->attached[which].valid || h3->super_[which].valid)
			platform_log("h3 weapons: %s: both barrels' projectiles detonate: the %s barrel's detonation damage is not written (one set of slots)",
				w->name, which ? "second" : "first");
	}
	else
	{
		if (h3->detonation[which].valid && stand_in_effect >= 0 &&
			h3w_damage_slot_make(r, instances, weapon_index + H2W_SLOT_DETONATION_DAMAGE, effect_damage, &h3->detonation[which]))
		{
			if (!h2w_effect_damage_copy(r, instances, stand_in_effect, weapon_index + H2W_SLOT_DETONATION_EFFECT,
				tag_id(weapon_index + H2W_SLOT_DETONATION_DAMAGE)))
			{
				platform_log("h3 weapons: %s: its stand-in's detonation has no damage part: no detonation damage", w->name);
			}
			region_reference(r, projectile_root + offsetof(struct projectile_definition, projectile.effect), 'effe',
				tag_id(weapon_index + H2W_SLOT_DETONATION_EFFECT));
		}
		if (h3->attached[which].valid &&
			h3w_damage_slot_make(r, instances, weapon_index + H2W_SLOT_ATTACHED_DAMAGE, stand_in_attached >= 0 ? stand_in_attached : effect_damage,
				&h3->attached[which]))
		{
			region_reference(r, projectile_root + offsetof(struct projectile_definition, projectile.attached_detonation_damage), 'jpt!',
				tag_id(weapon_index + H2W_SLOT_ATTACHED_DAMAGE));
		}
		if ((p->flags & (1UL << 3)) && h3->super_[which].valid && stand_in_super >= 0 &&
			h3w_damage_slot_make(r, instances, weapon_index + H2W_SLOT_SUPER_DAMAGE, super_damage, &h3->super_[which]))
		{
			if (!h2w_effect_damage_copy(r, instances, stand_in_super, weapon_index + H2W_SLOT_SUPER_EFFECT, tag_id(weapon_index + H2W_SLOT_SUPER_DAMAGE)))
				platform_log("h3 weapons: %s: its stand-in's super detonation has no damage part: no super detonation area damage", w->name);
			region_reference(r, projectile_root + offsetof(struct projectile_definition, projectile.super_detonation), 'effe',
				tag_id(weapon_index + H2W_SLOT_SUPER_EFFECT));
		}
		if ((p->flags & (1UL << 3)) && h3->attached_super[which].valid &&
			h3w_damage_slot_make(r, instances, weapon_index + H2W_SLOT_ATTACHED_SUPER_DAMAGE,
				stand_in_attached >= 0 ? stand_in_attached : super_damage, &h3->attached_super[which]))
		{
			attached_super = (long)tag_id(weapon_index + H2W_SLOT_ATTACHED_SUPER_DAMAGE);
			h2w_projectile_values_write(r, projectile_root, p, TRUE, attached_super);
		}
	}
	if (getenv("HALO_H2_WEAPON_LOG") || getenv("HALO_H2_DEBUG"))
		platform_log("h3 weapons: %s: %simpact damage %s%.1f / %.1f..%.1f, acceleration %.3f, flags %lx (%s) on the stand-in's damage effect; "
			"detonation %s, attached %s, super %s%s; the projectile written: velocity %.1f..%.1f wu/s, range %.1f, damage range %.1f..%.1f",
			w->name, which ? "second barrel's " : "", h3->impact[which].valid ? "" : "none: ", h3->impact[which].damage[0],
			h3->impact[which].damage[1], h3->impact[which].damage[2], h3->impact[which].acceleration, h3->impact[which].flags,
			h3->impact[which].category, h3->detonation[which].valid ? h3->detonation[which].name : "none",
			h3->attached[which].valid ? h3->attached[which].name : "none", h3->super_[which].valid ? h3->super_[which].name : "none",
			attached_super != NONE ? " (+ attached super)" : "",
			readf(region_at(r, projectile_root + offsetof(struct projectile_definition, projectile.initial_velocity))) * 30.0f,
			readf(region_at(r, projectile_root + offsetof(struct projectile_definition, projectile.final_velocity))) * 30.0f,
			readf(region_at(r, projectile_root + offsetof(struct projectile_definition, projectile.maximum_range))),
			readf(region_at(r, projectile_root + offsetof(struct projectile_definition, projectile.air_damage_range_lower_bound))),
			readf(region_at(r, projectile_root + offsetof(struct projectile_definition, projectile.air_damage_range_upper_bound))));
}

/* a Halo 3 weapon's triggers beyond h2w_trigger_values_write's (after it):
its second trigger's charge - the plasma pistol's and the laser's, on the
kit plasma pistol's second trigger: Halo 3's charging time (0.8 s, 2.5 s),
charged time, overcharged action (the laser's 2: it fires when charged),
illumination and spew time -, that trigger's heat and battery a round (the
charged bolt's 1 heat and 0.15 battery; the laser's beam five rounds of
0.04 battery a fire - one shot of this engine's: their sum), and a barrel
whose firing effects fire no shot fires no projectile (the laser's first
barrel: Halo 3's aiming tracer, a projectile of no model; this engine's
stand-in bolt would show) */
H2_ALIGN static void h3_weapons_triggers_write(struct region *r, unsigned long root, unsigned long triggers, struct h2w_weapon const *w)
{
	long trigger_count = (long)read32(region_at(r, root + offsetof(struct weapon_definition, weapon.triggers)));
	struct h3w_weapon const *h3 = w->h3;

	if (!h3 || !triggers || r->b.failed)
		return;
	/* (the stand-in's shell-by-shell type - the kit shotgun's - only where
	Halo 3's weapon reloads one round and its graph has the shotgun's reload
	enter animation: the mauler on the kit shotgun reloads a magazine, and
	items/weapons.c reads the enter animation of a shotgun type unguarded) */
	if (read16(region_at(r, root + offsetof(struct weapon_definition, weapon.weapon_type))) == 1 /* _weapon_type_shotgun, items/weapons.h */ &&
		(w->rounds_reloaded != 1 || !w->graph || w->graph->slots[0][23] == NONE))
	{
		write16(region_at(r, root + offsetof(struct weapon_definition, weapon.weapon_type)), 0 /* _weapon_type_undefined */);
		platform_log("h3 weapons: %s: not a shell-by-shell weapon: the stand-in's shotgun type cleared", w->name);
	}
	if (h3->fires_nothing[0])
		region_reference(r, triggers + offsetof(struct weapon_trigger_definition, projectile), 0, 0xFFFFFFFFUL);
	if (trigger_count > 1 && w->second_barrel_index != NONE)
	{
		unsigned long second = triggers + sizeof(struct weapon_trigger_definition);

		if (h3->charges)
		{
			writef(region_at(r, second + offsetof(struct weapon_trigger_definition, charging_time)), h3->charging_time);
			writef(region_at(r, second + offsetof(struct weapon_trigger_definition, charged_time)), h3->charged_time);
			write16(region_at(r, second + offsetof(struct weapon_trigger_definition, overcharged_action)),
				(unsigned short)(h3->overcharged_action >= 0 && h3->overcharged_action <= 2 ? h3->overcharged_action : 0));
			writef(region_at(r, second + offsetof(struct weapon_trigger_definition, charged_illumination)), h3->charged_illumination);
			writef(region_at(r, second + offsetof(struct weapon_trigger_definition, spew_time)), h3->spew_time);
		}
		if (w->heat[1] > 0.0f)
			writef(region_at(r, second + offsetof(struct weapon_trigger_definition, heat_generated_per_round)), h3->heat[1]);
		if (w->rounds_loaded <= 0 && h3->age[1] > 0.0f)
			writef(region_at(r, second + offsetof(struct weapon_trigger_definition, age_generated_per_round)), h3->age[1]);
		/* (its shots a fire - the laser's beam's five rounds at its rate -
		a burst of this engine's, items/weapons.c, as the battle rifle's) */
		if (h3->shots[1] > 1)
		{
			write32(region_at(r, second + offsetof(struct weapon_trigger_definition, port_burst_tag)), WEAPON_TRIGGER_BURST_TAG);
			write16(region_at(r, second + offsetof(struct weapon_trigger_definition, burst_shots)), (unsigned short)h3->shots[1]);
			writef(region_at(r, second + offsetof(struct weapon_trigger_definition, burst_recovery_time)), 0.0f);
		}
		if (h3->fires_nothing[1])
			region_reference(r, second + offsetof(struct weapon_trigger_definition, projectile), 0, 0xFFFFFFFFUL);
		if (getenv("HALO_H2_WEAPON_LOG") || getenv("HALO_H2_DEBUG"))
			platform_log("h3 weapons: %s: second trigger: %scharging %.2f s, charged %.2f s, action %d, heat %.2f, age %.3f a round, %d rounds a fire",
				w->name, h3->charges ? "" : "no charge; ", h3->charging_time, h3->charged_time, (int)h3->overcharged_action, h3->heat[1],
				h3->age[1], (int)(h3->shots[1] > 1 ? h3->shots[1] : 1));
	}
}

/* a Halo 3 melee weapon's strike explosion (the gravity hammer's): its
damage effect - the melee damage's copy with the explosion's radii, damage,
acceleration and flags - in the detonation damage slot (a melee weapon has
no projectile: h2w_slots_fill leaves a set slot), named by the weapon's
port_melee_area_damage; units/units.c does it from the strike's point.
Clean-room rule (no Halo 3 code read; from the tags): a hammer strike that
lands - on an object or the ground - plays its impact material effect,
whose damage part is gravity_hammer_explosion: 160 damage over 0.75..1.5
units with 3.5 acceleration, explosion_small; a strike that meets nothing
makes none */
H2_ALIGN static void h3_weapons_melee_area_write(struct region *r, unsigned long instances, unsigned long root, long weapon_index,
	struct h2w_weapon const *w)
{
	struct h3w_weapon const *h3 = w->h3;
	long slot = weapon_index + H2W_SLOT_DETONATION_DAMAGE;

	if (!h3 || !h3->area.valid || r->b.failed || !h2w_kinds[w->kind].sword)
		return;
	if (!h3w_damage_slot_make(r, instances, slot, weapon_index + H2W_TAG_MELEE, &h3->area))
		return;
	write32(region_at(r, root + offsetof(struct weapon_definition, weapon.port_melee_area_damage)), tag_id(slot));
	platform_log("h3 weapons: %s: strike explosion written (%s: %.2f..%.2f units, %.0f damage, %.2f acceleration)", w->name, h3->area.name,
		h3->area.radius[0], h3->area.radius[1], h3->area.damage[2], h3->area.acceleration);
}

/* ---------- public code: the h3/direct-level lane's lookup */

/* the made Halo 3 weapon a weap tag name stands for - its full path
(objects\weapons\rifle\battle_rifle\battle_rifle) or its last part
(battle_rifle) -: its tag index from the weapons' first tag (h2_level.c's
h2w_first; h2_weapons_for's convention: i * H2W_TAGS_PER_WEAPON), NONE if
none is made. Only after the gather (h3_weapons_gather) of the level being
made */
long h3_weapons_for(char const *h3_name)
{
	long i;
	char const *slash;

	if (!h3_name || !h3_name[0])
		return NONE;
	slash = strrchr(h3_name, '\\');
	for (i = 0; i < h2w.count; i++)
	{
		struct h2w_weapon const *w = h2w.weapons[i];

		if (!w->h3)
			continue;
		if (!strcmp(h3_name, h3w_kinds[w->h3->h3_kind].h3) || !strcmp(slash ? slash + 1 : h3_name, w->h3->h3_name))
			return i * H2W_TAGS_PER_WEAPON;
	}
	return NONE;
}

#else

/* (built alone: nothing - h2_level.c compiles it) */
typedef int h3_weapons_compiled_in_h2_level;

#endif
