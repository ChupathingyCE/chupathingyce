/*
H2_WEAPONS.C

Halo 2's weapons made into this engine's, as a Halo 2 level is made
(h2_level.c, which compiles this file as a part of itself: it uses the
level build's buffers, shaders and tag writing; built alone it is empty).

What a weapon is made of, each from the player's own MCC shared.map:
- its world model and its first-person model: Halo 2 render models (mode)
  with their nodes and markers, skinned (h2w_model_*); the first-person
  model is the Master Chief's arms (objects\characters\masterchief\fp\fp)
  and the weapon's own first-person model as one, so its animations move
  both (the weapon's flag _weapon_first_person_model_has_hands: the globals'
  hands are not drawn with it);
- its first-person animations: the Master Chief's graph for the weapon
  (jmad), each animation decoded (h2w_animation_decode: Halo 2's codecs, a
  static block and an animated one) into this engine's uncompressed frames
  (h2w_graph_*);
- the weapon itself: this engine's weapon its kit stand-in is (third-person
  animations, HUD, effects, sounds), with Halo 2's fields where they map:
  magazine, rate of fire, burst (the trigger's port fields: shots a burst
  and the recovery after it - items/weapons.c), error, zoom, aim assist,
  movement penalties; its projectile and that projectile's damage made from
  the stand-in's with Halo 2's speed, range and damage.

Layouts: Assembly's Halo 2 plugins (read only) checked against the owner's
shared.map; the codecs from reading its animations (decoded, drawn and
compared in local runs) with the halo2-decompiled samplers
(src/unknown_28c510.cpp, unknown_2c4d60.cpp) as a guide:
- a jmad's skeleton nodes at 0xC (0x20 each: name, sibling, child, parent),
  its animations at 0x2C (0x60 each: name, type at 0x10, node count 0x13,
  frame count 0x14, data size and address at 0x28/0x2C, the data's sizes
  at 0x30 - static flag bytes, animated flag bytes, static data size (a
  word at 0x36)); the data: the static codec block, the animated one, then
  the flags (static rotation, translation, scale; animated the same), each a
  node bit;
- a codec block: its codec, its rotation, translation and scale counts,
  then by codec: 1 static (one value each: rotations from 0x20 as four
  shorts, translations and scales at the offsets at 0xC/0x10), 3 animated
  (every frame, node by node: the offsets, then the strides at 0x14..),
  4-7 keyframed (0x30 a word a node - its key count, its first key from
  bit 12 -, then the offsets at 0xC: translation and scale words, rotation,
  translation and scale keys - a byte each for 4 and 6, a word for 5 and
  7 -, rotation, translation and scale values), 8 a blend screen (not used);
- a render model's nodes at 0x48 (0x60: name, parent, child, sibling,
  default translation, rotation, scale, the inverse of its default as
  forward, left, up, position), marker groups at 0x58 (0xC: name, markers
  0x24 each: region, permutation, node, translation, rotation), regions at
  0x1C, sections at 0x24 (vertex types 1 positions, 2 positions and a node
  - 16 bytes -, 4/6 positions and four nodes and weights - 20 bytes; the
  node map resource 0x68 its nodes), materials at 0x60.
Halo 2's quaternions turn the other way to this engine's
(matrix4x3_rotation_from_quaternion): each is conjugated.
*/

#ifdef H2_LEVEL_INTERNAL

/* (the engine's headers' tag_id is not h2_level.c's) */
#define tag_id h2w_engine_tag_id
#include "game/game_globals.h"
#include "items/projectile_definitions.h"
#include "items/weapon_definitions.h"
#include "items/h2_weapon_fields.h"
#include "cache/cache_files.h"
#include "cache/physical_memory_map.h"
#include "tag_files/tag_files.h"
#include "h2_damage.h"
#include "h2_vehicle_damage.h"
#include "models/model_animation_definitions.h"
#include "models/model_definitions.h"
#include "objects/damage_effect_definitions.h"
#include "objects/damage_resistances.h"
#include "units/unit_definitions.h"
#include "effects/effect_definitions.h"
#include "objects/object_definitions.h"
#include "sound/sound_definitions.h"
#include "items/weapon_export_function_mode.h"
#include "items/equipment_definitions.h"
#include "h2_projectiles.h"
#include "h2_vehicle_physics.h"
#include "physics/physics_definitions.h"
#ifdef HALO_LINUX
#include "h2_level_fast.h" /* (perf/ce-floor-2) the Halo 2 level tests inline */
#endif
#undef tag_id

/* ---------- constants */

enum
{
	/* (the made levels' files are made again when it changes: their
	header's builder version is h2_level.h's plus a thousand times it) */
	H2_WEAPONS_BUILDER_VERSION = 130,   /* (130 (h2/logic-accuracy): the needler's ammunition overlay frame f Halo 2's frame f - min(rounds, last) -, not the rounds spread over every frame) (129 (fix/h2-campaign-2): a campaign level's scripts' damage effects made - damage_new, damage_players: h2\\<name>) (128 (fix/h2-outskirts-9): a campaign level's ammunition boxes made - its equipment palette's) (127: merge of 126 and the Vita full-detail 125) (125b (perf/h2-vita-textures-full: the Vita's vehicles and weapons their finest level, as the PC's)) (126: merge of 124 and 125) (125 (fix/h2-weapons-audit-2): the strikes' self damage Halo 2's (two more tags a weapon, " melee response", " lunge response"), a melee's damage the weapon's own 0x190 where its strike has none; the bipeds' head materials by Halo 2's headshotable sections) (124 (feat/vita-renderer-parity): the damage flashes' fade functions Halo 2's) (123 (fix/h2-vehicle-fx): a model's markers on every permutation) (122 (fix/h2-rocket-hunter): the armor classes' multipliers in a damage effect; 121 (fix/h2-rocket-hunter): character material types in a damage effect's modifiers; 120 (h2cine): the Scarab's main gun; 119 (fix/h2-vehicle-fx): grenade flashes, marker copies; 118 (feat/h2-campaign-11): skull weapons; 116 (fix/h2-weapons-11): grenades' damage categories Halo 2's, the stuck one flagged; 115 (fix/h2-weapons-11): a damage effect's "lethal to the unsuspecting" kept, melee from behind; 114 (fix/h2-weapons-11): a graph's melee_strike_3 and 4 kept, Halo 2 picks among all four (g_4407f4); 113 (fix/h2-mapfx-11): a weapon's overheated effect Halo 2's (a 19th tag a weapon, " overheated"); 112 (h2bugs): a damage effect's table on Halo 2's thick and thin metal, slots 33, 34 of its material modifiers; 111: fix/h2-weapons-11 - the ready effect's Halo 2 sound parts (the sword's sword_ready), 192 weapon sounds; 110: fix/h2-weapons-11 - a weapon's ready effect Halo 2's (an 18th tag a weapon, " ready"), a burst's sounding firing effect second; 109: fix/h2-weapons-11 - each trigger's rounds between tracers Halo 2's barrel's; 108: fix/h2-weapons-11 - the stand-in's muzzle light taken away where Halo 2's weapon has none (carbine, Brute Shot, beam rifle); 107: fix/h2-hud-11 - a grenade's attached detonation damage flagged (bit 20) for Halo 2's stuck kills; 106: fix/h2-hud-11 - each damage effect keeps Halo 2's damage category (the sniper rifle's and beam rifle's the sniper's), the kill's kind in Halo 2's kill lines; 105: fix/h2-anim-fx - Halo 2's shake radius kept in each damage effect (players past the damage radius shaken), the grenades' camera impulse and shaking Halo 2's; 104: fix/h2-vehicle-deaths - each vehicle's damage definition kept in the level, the second string of its "<name> physics" list (a level read back from its cached file has Halo 2's damage sections), its pieces' markers, speeds, cones and spins, its effects' markers; 103: fix/h2-weapon-look - Halo 2's muzzle lights (radius, colour, marker, illumination recovery) in place of the stand-ins', a detonation damage part where the stand-in's detonation has none (the needler), damage effects' camera impulse and shaking Halo 2's; 102: fix/h2-mp-core2 - the rate of fire's rise and fall times Halo 2's (the plasma rifles' spin), its movement penalties Halo 2's (none); 101: h2ai a5b0c978 - the gravity throne boarding seats in the vehicle table, merged on h2engine/play; 100: h2/characters 59a8df3d - Sentinels' built-in guns, merged on h2engine/play; 99 (r03a r7): the meters' maps their colour, outside black; 98 (r03a r6): the meters' maps their DXT alpha; 97 (r03a r5): the meters made shader_transparent_meter, filled live; 96: play 95 + r03a 95 merged - r03a: the plasma current in bands; 95: fix/h2-vehicle-damage 4d0f35bc 6c420e0b - the Hunter's assault cannon, long bursts, merged on h2engine/play; 94: play 91 + r03a 93 merged - r03a: the plasma currents their noise in their colour, h2cc's plasma noise cards undrawn; 91: fix/h2-vehicle-damage d14065e9 - extra seat cameras, merged on h2engine/play; 90: fix/h2-vehicle-damage 8fc03ccd e47aa1d2 - only the weapons a level starts with, no dead collision copies, merged on h2engine/play; 89: fix/h2-vehicle-damage 7e5b38ea - a campaign level's weapon palette made Halo 2's, Shadow seats, merged on h2engine/play; 88: h2ai/squad-vehicles ae8ac2b9 - the Phantom's turret, merged on h2engine/play; 87: fix/h2-vehicle-damage - the Shadow and the insertion pod, nearest-door seats, merged on h2engine/play; 86: h2ai/squad-vehicles - the Phantom and Pelican rigs and seats, merged on h2engine/play; 85: fix/h2-vehicle-damage b8359040 - the Covenant and gun tower turrets, 12 vehicle kinds, merged on h2engine/play; 84: r03a merged - 83 (r03a): damage effects' screen flashes from Halo 2's player responses; 83: fix/h2-vehicle-damage 2f9fcb27 799a9d9d - h_turret_mp, one burst a pull, merged on h2engine/play; 82: h2mcc/03a-fixes - a campaign starting profile's unloaded rounds Halo 2's total minus loaded; 81: fix/h2-vehicle-damage 0b44cdb6 - the plasma cannon gunner seat c_turret_mp_d, merged on h2engine/play; 80: fix/h2-vehicle-damage - armor, damage sections, collision spheres, death blasts, merged on h2engine/play; 79: fix/h2-props-weapon-physics e0076c82 - first-person animation node flags, merged on h2engine/play; 78: T5 - a campaign level's starting profile Halo 2's weapons, merged on h2engine/play; 77: S2 merged; 74: S2 - the tanks' hull boarding seats, the boarding melee damage (S2); 76: S2 merged; 73: S2 - the Banshee bomb's own damage (S2); 75: S2 merged; 72 (S2): the vehicles' hatches and canopies; 74: T1 merged; 73 (T1): no zoom where Halo 2 has none; 72 (T1): the carbine, beam rifle and Brute plasma rifle made, a trigger's own barrel; 72: S8 - the Pelican and the gravity thrones on campaign levels, merged on h2engine/play; 71: S2 - seat rules, the Banshee bomb tag, boarding seats, merged on h2engine/play; 70: S2 - the vehicles' own rigs and graphs, merged on h2engine/play; 69: S7 - the powerups' plasma orbs and glass shells; 68: S7 - Halo 2's powerups' models; 67: S7 - the Sentinel Beam, battery ages, the random powerup; 62: pitch_and_turn, the first-person sway; 63: the oddball Halo 2's; 64: the weapons' animation labels Halo 2's types; 65: merged with projectiles + scope lens; 66: the vehicles' force model tags, h2_vehicle_physics.h) */

	H2W_MAXIMUM_NODES = 43,             /* (the renderer's skinned nodes, less one) */
	H2W_MAXIMUM_MARKERS = 128,           /* (the Warthog has 96 with its turret's and its "left hand" copies) */
	H2W_MAXIMUM_ANIMATIONS = 64,
	H2W_MAXIMUM_GRAPH_NODES = 64,
	/* (this engine's 28 and Halo 2's pitch_and_turn, 28: first_person_weapons.c) */
	H2W_FIRST_PERSON_SLOTS = 29,
	H2W_PITCH_AND_TURN_SLOT = 28,
	H2W_MAXIMUM_WEAPONS = 32,   /* (32: Halo 3's twenty-two sandbox weapons beside the level's own - h3_weapons.c) */
	/* weapon, graph, projectile, damage, two firing effects, the Elite's
	graph, melee and lunge damage; the second trigger's projectile and
	damage (the plasma pistol's charged bolt), the projectile's detonation,
	attached detonation, super detonation and attached super detonation
	damage, its detonation and super detonation effects
	(h2w_projectile_write) */
	H2W_TAGS_PER_WEAPON = 21,
	H2W_TAG_PROJECTILE = 2,             /* (its projectile; its damage the next) */
	H2W_TAG_MELEE = 7,                  /* (its melee's damage: Halo 2's first hit's) */
	H2W_TAG_LUNGE = 8,                  /* (its lunge's: a sword's) */
	H2W_SLOT_SECOND_PROJECTILE = 9,
	H2W_SLOT_SECOND_DAMAGE,
	H2W_SLOT_DETONATION_DAMAGE,
	H2W_SLOT_ATTACHED_DAMAGE,
	H2W_SLOT_SUPER_DAMAGE,
	H2W_SLOT_ATTACHED_SUPER_DAMAGE,
	H2W_SLOT_DETONATION_EFFECT,
	H2W_SLOT_SUPER_EFFECT,
	/* (its ready effect: Halo 2's weap @140 - the sword's blade_activate_fp,
	the ignition as it is readied, its sound -, made by h2_effects.c at
	"<name> ready" as the firing effects are; an empty effect where Halo 2
	has none) */
	H2W_SLOT_READY_EFFECT,
	/* (its overheated effect: Halo 2's weap @16C, the reference after the heat
	block - the plasma rifle's overheated -, made as the ready effect is, at
	"<name> overheated") */
	H2W_SLOT_OVERHEATED_EFFECT,
	/* (its melee's and its lunge's self damage: Halo 2's weap 0x1C4 and
	0x1F4, the strike's own entry's second reference - else 0x198 -, done
	to the striker when the strike meets a surface, unit_object_type.cpp
	function_cf3d0: the "strike_melee_response" its camera's jolt; made at
	"<name> melee response" and "<name> lunge response") */
	H2W_SLOT_MELEE_RESPONSE,
	H2W_SLOT_LUNGE_RESPONSE,
	H2W_MAXIMUM_SOUNDS = 192,   /* (192: with every weapon made the 128 were full before the sword's ready effect asked for its sword_ready - fix/h2-weapons-11) */
	H2W_FIRING_EFFECTS = 2,
	/* (a trigger's firing effect, items/weapons.c's: shot bounds, 32 bytes,
	three effects, three damages) */
	H2W_FIRING_EFFECT_SIZE = 0x84,
	H2W_FIRING_EFFECT_EFFECT = 0x24,
	/* (a weapon's looping sounds made: the Sentinel Beam's firing hum and
	its overheated hiss - h2w_loops_read) */
	H2W_MAXIMUM_LOOPS = 8,
	/* (h2w_effect_make's sound: the stand-in effect's own kept - a Halo 3
	weapon's, whose sounds are not read yet, h3_weapons.c) */
	H2W_SOUND_KEEP = -2,
};

/* Halo 2's render models' levels of detail taken (0 L1, the least, to 5
L6, the most; this engine's model gets the one): their most on both tiers -
Halo 2's own close-up model (its L5 and L6 sections the same ones in the
Warthog, the Ghost, the battle rifle: the finest Halo 2 plays with). The
Vita had the vehicles' L3 and the weapons' L2 - a Warthog 506 triangles of
2394, a Ghost 214 of 1302, a battle rifle 366 of 1118 - until
feat/h2-vita-full-model-detail. HALO_H2_VEHICLE_DETAIL,
HALO_H2_WEAPON_DETAIL=<0..5>: that one (a test's) */
#define H2W_VEHICLE_DETAIL_DEFAULT 5
#define H2W_WORLD_DETAIL_DEFAULT 5

static short h2w_detail(char const *name, short fallback)
{
	char const *setting = getenv(name);

	return setting && *setting >= '0' && *setting <= '5' ? (short)(*setting - '0') : fallback;
}


/* ---------- structures */

struct h2w_node
{
	char name[32];
	short parent;
	float translation[3];
	float rotation[4];          /* (Halo 2's) */
	float scale;
	float inverse[12];          /* the inverse of its default: forward, left, up, position */
};

struct h2w_marker
{
	char name[32];
	short node;
	float translation[3];
	float rotation[4];          /* (Halo 2's) */
};

/* a model made of Halo 2 render models, with nodes */
struct h2w_model
{
	long model;                 /* build->models' (NONE: none) */
	long node_count;
	struct h2w_node nodes[H2W_MAXIMUM_NODES];
	long marker_count;
	struct h2w_marker markers[H2W_MAXIMUM_MARKERS];
	struct model_made made;
	/* (a vehicle's, h2_vehicles.c) its Halo 2 regions kept, each with its
	permutations - its damage states - but blurred wheels; merge: Halo 2's
	region names made another's (pairs, NULL-ended), piece_region: the
	piece being added one region of that name drawn in every permutation */
	boolean regioned;
	char const *const *merge;
	char const *piece_region;
	/* (perf/h2-object-lods) levels: made at Halo 2's own levels of detail
	(an object's world model: a vehicle's, a weapon's, equipment's, debris'),
	its first render model's distances the object's (made.h2_reduce, once
	reduce_known) */
	boolean levels, reduce_known;
};

/* a render model section's vertices, skinned */
struct h2w_section
{
	long vertex_count;
	float *positions;           /* 3 each, the model's space */
	float *coordinates;         /* 2 each */
	unsigned long *packed;      /* normal, binormal, tangent */
	unsigned char *nodes;       /* 4 each, the render model's */
	unsigned char *weights;     /* 4 each, of 255 */
	long index_count;
	unsigned short *indices;
	long part_count;
	struct section_part *parts;
};

/* an animation in this engine's uncompressed form */
struct h2w_animation
{
	char name[32];
	short type;
	short frame_count;
	short frame_size;
	short node_count;
	unsigned long translation_flags[2], rotation_flags[2], scale_flags[2];
	unsigned char *defaults;
	unsigned long default_size;
	unsigned char *frames;
	unsigned long frame_bytes;
	short next;
	float weight;
	float normalized_weight;
	short parent;
	short key_frame;
	short second_key_frame;     /* (a melee's end: Halo 2's next melee may start there) */
	short slot;
	short set;
	short sound;                /* (h2w.sounds') its first sound event's, NONE none */
	short sound_frame;
	short graph_sound;          /* (its graph's sound references') */
};

struct h2w_graph
{
	long node_count;
	struct h2w_graph_node { char name[32]; short parent, child, sibling; short source; } nodes[H2W_MAXIMUM_GRAPH_NODES];
	long source_count;          /* (the jmad's nodes, of which nodes are those the model has, and their parents) */
	long animation_count;
	struct h2w_animation animations[H2W_MAXIMUM_ANIMATIONS];
	/* the first-person animations' slots: held alone [0], dual wielded [1]
	(Halo 2's "first_person:dual:..."; the left hand's are the right's,
	mirrored, as Halo 2 draws them) */
	short slots[2][H2W_FIRST_PERSON_SLOTS];
	/* (pitch_and_turn's blend screen, each set's: its yaw and pitch deltas,
	negative and positive; zero: none) */
	float pitch_and_turn_screen[2][4];
	boolean dual;
	/* its sound references (h2w.sounds') */
	long sound_count;
	short sounds[32];
};

/* a Halo 2 projectile's material response (proj @19C, 0x58 each, MCC's
layout - the kit's fields in its order, a runtime material index after the
material name): flags 0, response 2, material name 0xC, its global material
index 0x10, potential response 0x14, its flags 0x16, chance 0x18, between
(angles) 0x1C, and (velocities) 0x24, scale effects by 0x34, angular noise
0x38, velocity noise 0x3C, initial friction 0x48, maximum distance 0x4C,
parallel and perpendicular friction 0x50 (projectiles.cpp's
s_projectile_material_response) */
struct h2w_response
{
	unsigned short flags, potential_flags;
	short response, potential, scale_by, material;
	float chance, angles[2], velocities[2], angular_noise, velocity_noise, initial_friction, maximum_distance, frictions[2];
};

enum
{
	H2W_MAXIMUM_RESPONSES = 16,
	H2W_MATERIAL_TYPES = 33,       /* this engine's material types */
	H2W_RESPONSE_NONE = -1,        /* Halo 2 has no response for the type: its zero one (an impact) */
	H2W_RESPONSE_STAND_IN = -2,    /* no Halo 2 material stands for the type: the stand-in's kept */
};

/* a Halo 2 projectile's material responses, and the one each of this
engine's material types gets (h2w_responses_read) */
struct h2w_responses
{
	short count;
	struct h2w_response responses[H2W_MAXIMUM_RESPONSES];
	signed char of_type[H2W_MATERIAL_TYPES];
};

/* a Halo 2 projectile's fields (its proj tag's, MCC's layout) */
struct h2w_projectile
{
	long tag;                   /* NONE: none */
	unsigned long flags;
	short timer_starts;
	short super_detonation_count;
	float ai_perception_radius, collision_radius, arming_time, danger_radius;
	float timer[2];
	float minimum_velocity, maximum_range;
	long detonation_damage, attached_detonation_damage, super_detonation_damage, attached_super_detonation_damage, impact_damage;
	float air_gravity_scale, air_damage_range[2], water_gravity_scale, water_damage_range[2];
	float velocity[2], guided_angular_velocity[2], acceleration_range[2];
	struct h2w_responses responses;
};

/* a Halo 2 weapon made */
struct h2w_weapon
{
	long kind;                  /* h2w_kinds' */
	long h2_tag;
	char name[64];
	struct h2w_model world;
	struct h2w_model first_person;
	struct h2w_graph *graph;
	/* the Elite's (Halo 2's dervish set: its arms, the same gun) - its model
	NONE, graph NULL if not made */
	struct h2w_model first_person_elite;
	struct h2w_graph *graph_elite;
	/* Halo 2's fields */
	short rounds_initial, rounds_maximum, rounds_loaded;
	float rounds_per_second, rounds_per_second_maximum;
	/* (the barrel's @C and @10: the rate of fire's rise from its least to
	its most while held, and its fall, in seconds - the plasma rifle's spin) */
	float rate_times[2];
	short burst_shots;
	float burst_recovery;
	unsigned long barrel_flags;
	/* its trigger's primary barrel (the trigger's @8: the carbine's the
	second - its first, two shots a pull, a stand-by the trigger never
	fires) and that barrel's address; its secondary barrel's (@A: the
	plasma pistol's overcharge), NONE none (h2w_barrels_read) */
	long barrel_index, second_barrel_index;
	unsigned long barrel;
	short trigger_behavior;
	float error_angle[2];
	/* the barrel's error (Halo 2's barrel: @38 and @3C the error's rise and
	fall times, @70 its minimum, @74 and @78 its angles at error 0 and 1), the
	same dual wielded (@48, @4C, @58, @5C, @60) with its damage scale (@64),
	and its recoil (@B0..@C0: the view's angle change a shot at its least and
	most, its rise and fall times, its curve) */
	float error_times[2], error_minimum;
	float dual_error_times[2], dual_error_minimum, dual_error_angle[2], dual_damage_scale;
	float recoil_angle[2], recoil_times[2];
	short recoil_function;
	/* the magazine's: rounds a reload moves (@14), rounds held (@C: the
	reserve's most), the ammunition box's rounds (its equipment's, 0 for the
	most), the time of a reload (@10: Halo 2 plays the animation's length),
	and the weapon's ready time (@13C), multiplayer on-ground scale (@C4) */
	short rounds_reloaded, rounds_held, rounds_box;
	float reload_time, ready_time, ground_scale;
	short projectiles_per_shot;
	/* (the barrel's @2E: the rounds fired between two tracer rounds - the
	battle rifle's 2, the SMG's 3, the rest's 0: every round) */
	short rounds_between_tracers;
	float distribution_angle;
	short zoom_levels;
	float zoom[2];
	float autoaim[2], magnetism[2], deviation;
	short movement_penalized;
	float movement_penalty[2];
	float first_person_offset[3];
	/* its barrels' projectiles (the first barrel's; the second's - the
	plasma pistol's charged bolt, the rocket launcher's locked rocket) and
	its tracking (_h2_tracking_...) */
	struct h2w_projectile projectiles[2];
	short tracking;
	float projectile_velocity[2];
	float projectile_range;
	float projectile_gravity;
	float damage[3];            /* lower, upper (min, max) */
	float damage_acceleration;
	unsigned long damage_flags;
	long damage_tag;           /* Halo 2's bullet damage effect (NONE: none): its damage groups (h2_damage.c) */
	/* its melee (Halo 2's weap 0x1B0..0x1F4): the damage pyramid (width,
	height, depth), its first hit's damage and its lunge's (a jpt's minimum,
	lower and upper bound - 0x1C, 0x20, 0x24 - and instantaneous
	acceleration, 0x40; [3] 0: none) - the hits a player's melee does
	(Halo 2's unit_object_type.cpp function_cf3d0: a player's strikes,
	melee_strike_1..4, all its first hit's; the second and third hits only
	the AI's combo names') */
	float melee_pyramid[3];
	float melee_hit[4];
	float lunge_hit[4];
	unsigned long hit_flags[2]; /* (their jpt flags, 0x14) */
	long hit_tags[2];           /* (Halo 2's melee and lunge damage effects, NONE none: their damage groups, h2_damage.c) */
	long response_tags[2];      /* (their self damage, Halo 2's 0x1C4 and 0x1F4, else 0x198; NONE none: H2W_SLOT_MELEE_RESPONSE) */
	/* a lunge's reach (a sword's): its aim assist's - Halo 2's lunge is at
	the aim assist's target when its autoaim level is full */
	float lunge_range, lunge_angle;
	float heat[7];              /* recovery, overheated, detonation thresholds, detonation fraction, loss a second, illumination,
	                               loss a second overheated (weap @0x150..0x168) */
	float heat_per_round;
	/* its battery (Halo 2's barrel @A8: age a round) and the age's penalties
	(weap @254 heat recovery, @258 rate of fire, @25C misfire start, @260
	misfire chance) */
	float age_per_round;
	float age_penalties[4];
	/* its barrel's first-person offset (@7C: forward, left, up - where a
	player's projectile starts, from the camera: the Sentinel Beam's at its
	muzzle; the others' none) */
	float barrel_offset[3];
	/* (its Halo 2 attachments all looping sounds: the Sentinel Beam's two -
	the stand-in's lights, its functions and exports given up for them,
	h2w_loops_attach) */
	boolean sounds_only;
	/* its ammunition box (Halo 2's magazine's first magazine object's
	equipment, @54 of the magazine: rounds at 0, the equipment at 4 - the
	battle rifle's br_ammo, 72 rounds), made where the level places it
	(its ammo_<weapon> collection): an equipment of this engine's (the kit's
	rocket launcher ammunition's) with Halo 2's model, which the weapon's
	magazine takes its rounds from (NONE: none; its model build->models') */
	long ammo_tag;
	struct h2w_model ammo_model;
	float ammo_scale;
	long ammo_slot;             /* (its tag: h2w_ammo_first()'s, NONE none) */
	unsigned long h2_flags;
	/* Halo 2's firing effects' sounds (h2w.sounds', NONE none): its first
	barrel's firing effects, at most two */
	long firing_effect_count;
	short firing_sounds[H2W_FIRING_EFFECTS];
	/* its muzzle light: Halo 2's light (ligh) attachment scaled by its
	"illumination" (weap @94; the light's radius @18, diffuse colour's upper
	bound @64 times its brightness's upper bound @74), at that marker, and
	its barrel's illumination recovery time (@A0) - h2w_light_read. NONE
	none (the stand-in's muzzle light kept); its tag h2w_light_first()'s
	light_slot */
	long light_tag;
	long light_slot;
	char light_marker[32];
	float light_radius, light_color[3], light_recovery;
	/* the kit's tags it is made from */
	long base_weapon;
	/* (h3_weapons.c) Halo 3's weapon this is, its own values; NULL: Halo 2's */
	struct h3w_weapon *h3;
};

/* the weapons converted: the netgame collection's name (as h2_level.c's
netgame_stand_ins name it), Halo 2's weapon, the kit's weapon it is made
from */
static struct
{
	char const *collection;
	char const *h2;
	char const *base;
	/* (it takes the stand-in's place in the globals' multiplayer weapons:
	what players start with, what weapon sets choose) */
	boolean replaces_base;
	/* (a sword: no triggers, its melee lunges - _weapon_melee_lunges_bit) */
	boolean sword;
	/* its animation label: Halo 2's weapon type's, which the player graphs'
	classes have (h2_animation.c) - the BR's own reload and firing, not the
	stand-in's label's ("ar": the SMG's); NULL: the stand-in's */
	char const *label;
	/* (an AI's only, of no model - the Hunter's assault cannon: no world
	or first-person model, no graph; a weapon of no model is invisible and
	fires from its holder's own markers, weapons.c's
	weapon_get_effect_object_index - the Hunter's arm. Gone when dropped) */
	boolean ai_only;
} const h2w_kinds[] =
{
	{ "battle_rifle", "objects\\weapons\\rifle\\battle_rifle\\battle_rifle", "weapons\\assault rifle\\assault rifle", TRUE, FALSE, "br" },
	{ "smg", "objects\\weapons\\rifle\\smg\\smg", "weapons\\assault rifle\\assault rifle", FALSE, FALSE, "smg" },
	{ "plasma_rifle", "objects\\weapons\\rifle\\plasma_rifle\\plasma_rifle", "weapons\\plasma rifle\\plasma rifle", TRUE, FALSE, "pr" },
	{ "needler", "objects\\weapons\\pistol\\needler\\needler", "weapons\\needler\\needler", TRUE, FALSE, "ne" },
	{ "shotgun", "objects\\weapons\\rifle\\shotgun\\shotgun", "weapons\\shotgun\\shotgun", TRUE, FALSE, "sg" },
	{ "sniper_rifle", "objects\\weapons\\rifle\\sniper_rifle\\sniper_rifle", "weapons\\sniper rifle\\sniper rifle", TRUE, FALSE, "sr" },
	{ "rocket_launcher", "objects\\weapons\\support_high\\rocket_launcher\\rocket_launcher", "weapons\\rocket launcher\\rocket launcher", TRUE, FALSE, "rl" },
	{ "magnum", "objects\\weapons\\pistol\\magnum\\magnum", "weapons\\pistol\\pistol", TRUE, FALSE, "hp" },
	{ "plasma_pistol", "objects\\weapons\\pistol\\plasma_pistol\\plasma_pistol", "weapons\\plasma pistol\\plasma pistol", TRUE, FALSE, "pp" },
	{ "brute_shot", "objects\\weapons\\support_low\\brute_shot\\brute_shot", "weapons\\rocket launcher\\rocket launcher", FALSE, FALSE, "bs" },
	{ "energy_sword", "objects\\weapons\\melee\\energy_blade\\energy_blade", "weapons\\plasma pistol\\plasma pistol", FALSE, TRUE },
	/* (A14: the oddball, carried - Halo 2's model, first person arms and
	animations on the stand-in's ball, its game rules the stand-in's. Not
	the flag: Halo 2's cloth is a clwd tag this engine has no reader for -
	its pole alone, the stand-in's cloth gone - and the stand-in's flag
	model held in Halo 2's hands sat apart from them) */
	{ "ball", "objects\\weapons\\multiplayer\\ball\\ball", "weapons\\ball\\ball", TRUE },
	/* (Halo 2's skulls: what a level's script waits for a player to hold,
	h2_skulls.c; 07a's is ball\\head) */
	{ "ball\\head_sp", "objects\\weapons\\multiplayer\\ball\\head_sp", "weapons\\ball\\ball", FALSE, FALSE, "hp" },
	{ "ball\\head", "objects\\weapons\\multiplayer\\ball\\head", "weapons\\ball\\ball", FALSE, FALSE, "hp" },
	/* (S7: the Sentinel Beam, Backwash's and Desolation's - the sentinel
	aggressor's beam, as Halo 2's multiplayer has it: a battery and heat
	weapon spewing an instantaneous beam thirty times a second, its beam a
	liquid (tdtl) drawn here as contrails - h2_effects.c's
	h2fx_liquid_purposes -, its looping firing sound a looping sound of
	Halo 2's in, loop and out - h2w_loop_write; the kit's plasma rifle, a
	battery and heat weapon too, its stand-in. Its label Halo 2's type's,
	"ds", which the players' graphs' support class has) */
	{ "sentinel_beam", "objects\\characters\\sentinel_aggressor\\weapons\\beam\\sentinel_aggressor_beam",
		"weapons\\plasma rifle\\plasma rifle", FALSE, FALSE, "ds" },
	/* (the Scarab's main gun, a scripted weapon no unit holds: 03a's and 03b's
	cutscenes and encounters, weapon_hold_trigger. As the Sentinel Beam, on the
	plasma rifle) */
	{ "scarab_main_gun", "scenarios\\objects\\covenant\\military\\scarab\\weapons\\main_gun\\scarab_main_gun_handheld",
		"weapons\\plasma rifle\\plasma rifle", FALSE, FALSE, "ds" },
	/* (T1: the last three stand-ins made Halo 2's. The Covenant carbine on
	the kit's pistol - a magazine and a 2x zoom; its trigger fires its second
	barrel, h2w_barrels_read -, the players' graphs' "cb" type. The beam
	rifle on the kit's plasma rifle - a heat and battery weapon, no magazine
	(Halo 2's beam rifle: 0.65 heat and 1/18 of its battery a shot), its
	zoom Halo 2's two levels -, "csr". The Brute plasma rifle on the kit's
	plasma rifle, the plasma rifle's "pr" type, as Halo 2's graphs have no
	type of its own) */
	{ "carbine", "objects\\weapons\\rifle\\covenant_carbine\\covenant_carbine", "weapons\\pistol\\pistol", FALSE, FALSE, "cb" },
	{ "beam_rifle", "objects\\weapons\\rifle\\beam_rifle\\beam_rifle", "weapons\\plasma rifle\\plasma rifle", FALSE, FALSE, "csr" },
	{ "brute_plasma_rifle", "objects\\weapons\\rifle\\brute_plasma_rifle\\brute_plasma_rifle", "weapons\\plasma rifle\\plasma rifle",
		FALSE, FALSE, "pr" },
	/* (the Hunter's assault cannon - Halo 2's hunter_particle_cannon, of no
	model: its fuel-rod bolt, particle_cannon_component, from the Hunter's
	arm; the kit's rocket launcher it is made on, which Hunters carried) */
	{ "hunter_particle_cannon", "objects\\characters\\hunter\\hunter_particle_cannon\\hunter_particle_cannon",
		"weapons\\rocket launcher\\rocket launcher", FALSE, FALSE, "rl", TRUE },
	/* (the Sentinels' guns, built into their bodies as Halo 2 has them: no
	model of their own a Sentinel holds - the Enforcer's needler, beam and
	rockets have none; the aggressors' beams' gun model is the multiplayer
	Sentinel Beam's, never drawn on a Sentinel, whose own body has the gun
	- an AI's only each, fired from the Sentinel's "primary_trigger" (the
	Enforcer's needler and rocket triggers, h2_biped.c), its graph's "sen"
	type (h2_animation.c). Their projectiles, beams and firing Halo 2's on
	the kit's needler, plasma rifle and rocket launcher. Their collections
	as h2_level.c's stand-ins look them up: a part of their tags' names) */
	{ "sentinel_enforcer_needler", "objects\\characters\\sentinel_enforcer\\weapons\\needler\\sentinel_enforcer_needler",
		"weapons\\needler\\needler", FALSE, FALSE, "sen", TRUE },
	{ "sentinel_enforcer_beam", "objects\\characters\\sentinel_enforcer\\weapons\\beam\\sentinel_enforcer_beam",
		"weapons\\plasma rifle\\plasma rifle", FALSE, FALSE, "sen", TRUE },
	{ "sentinel_enforcer_rocket", "objects\\characters\\sentinel_enforcer\\weapons\\rocket\\sentinel_enforcer_rocket",
		"weapons\\rocket launcher\\rocket launcher", FALSE, FALSE, "sen", TRUE },
	{ "sentinel_aggressor_welder", "objects\\characters\\sentinel_aggressor\\weapons\\welder\\sentinel_aggressor_welder",
		"weapons\\plasma rifle\\plasma rifle", FALSE, FALSE, "sen", TRUE },
	{ "beam\\sentinel_aggressor_beam", "objects\\characters\\sentinel_aggressor\\weapons\\beam\\sentinel_aggressor_beam",
		"weapons\\plasma rifle\\plasma rifle", FALSE, FALSE, "sen", TRUE },
	{ "sent_agg_beam_elim", "objects\\characters\\sentinel_aggressor\\weapons\\beam_elim\\sent_agg_beam_elim",
		"weapons\\plasma rifle\\plasma rifle", FALSE, FALSE, "sen", TRUE },
	/* (h3_weapons.c: Halo 3's weapons of no Halo 2 twin, the kit stand-in
	each is made on and its Halo 3 type label; their "Halo 2" name is their
	Halo 3 one, which no Halo 2 level holds - never made from a Halo 2 map,
	made from the Halo 3 maps with HALO_H3_WEAPONS: the Spartan laser on the
	kit's plasma pistol for its charge trigger, the gravity hammer a sword
	kind - its melee lunges, its primary trigger melees) */
	{ "assault_rifle", "objects\\weapons\\rifle\\assault_rifle\\assault_rifle", "weapons\\assault rifle\\assault rifle", FALSE, FALSE, "ar" },
	{ "spike_rifle", "objects\\weapons\\rifle\\spike_rifle\\spike_rifle", "weapons\\assault rifle\\assault rifle", FALSE, FALSE, "spr" },
	{ "excavator", "objects\\weapons\\pistol\\excavator\\excavator", "weapons\\shotgun\\shotgun", FALSE, FALSE, "ex" },
	{ "flak_cannon", "objects\\weapons\\support_high\\flak_cannon\\flak_cannon", "weapons\\rocket launcher\\rocket launcher", FALSE, FALSE, "fc" },
	{ "spartan_laser", "objects\\weapons\\support_high\\spartan_laser\\spartan_laser", "weapons\\plasma pistol\\plasma pistol", FALSE, FALSE, "sl" },
	{ "gravity_hammer", "objects\\weapons\\melee\\gravity_hammer\\gravity_hammer", "weapons\\plasma pistol\\plasma pistol", FALSE, TRUE, "hammer" },
	{ "flamethrower", "objects\\weapons\\turret\\flamethrower\\flamethrower", "weapons\\plasma rifle\\plasma rifle", FALSE, FALSE, "ftt" },
	/* (the silenced SMG every Halo 3 map holds and some place: the SMG's
	type - its weap's label is "smg" -, its own model, values and graph) */
	{ "smg_silenced", "objects\\weapons\\rifle\\smg_silenced\\smg_silenced", "weapons\\assault rifle\\assault rifle", FALSE, FALSE, "smg" },
};

/* the first-person animations' names (Halo 2's state, this engine's slot) */
static struct
{
	char const *state;
	short slot;
} const h2w_first_person_states[] =
{
	{ "idle", 0 },
	{ "posing", 1 },
	{ "fire_1", 2 },
	{ "moving", 3 },
	{ "overlays", 4 },
	{ "reload_empty", 7 },
	{ "reload_full", 8 },
	{ "overheated", 9 },
	{ "ready", 10 },
	{ "put_away", 11 },
	{ "overcharged", 12 },
	{ "melee_strike_1", 13 },
	{ "melee_strike_2", 13 },
	{ "melee_strike_3", 13 },
	{ "melee_strike_4", 13 },
	{ "melee", 13 },
	{ "fire_2", 14 },
	{ "throw_grenade", 16 },
	{ "misfire_1", 18 },
	{ "misfire_2", 19 },
	{ "overheating", 21 },
	{ "ammunition", 17 },
	{ "throw_overheated", 20 },
	{ "reload_enter", 23 },
	{ "reload_exit", 24 },
	{ "reload_continue_empty", 7 },
	{ "o_h_exit", 26 },
	{ "o_h_s_enter", 27 },
	{ "overcharged_jitter", 15 },
	/* (Halo 2's sway: a 3x3 blend screen, the look's pitch by the turn's
	rate - first_person_weapons.c, its screen in the set's unused words) */
	{ "pitch_and_turn", 28 },
	/* (a sword's lunge - a melee-only weapon, which never fires nor
	overheats: its misfires' slots and its overheated throw's,
	items/weapons.h _first_person_weapon_animation_melee_dash...) */
	{ "melee_dash", 18 },
	{ "melee_lunge", 19 },
	{ "melee_dash_airborne", 20 },
	{ "melee_lunge_airborne", 27 },
};

/* slots a weapon's graph fills from another when it has none: reload-full
from reload-empty and back, exit-full from exit-empty */
static short const h2w_slot_aliases[][2] =
{
	{ 8, 7 }, { 7, 8 }, { 25, 24 }, { 22, 21 },
};

/* the grenades: Halo 2's model on the kit's grenade (its equipment and
projectile keep this engine's behaviour) */
static struct
{
	char const *h2_model;
	char const *ce;            /* the kit's equipment's, projectile's and model's path */
} const h2w_grenades[] =
{
	{ "objects\\weapons\\grenade\\frag_grenade\\frag_grenade", "weapons\\frag grenade\\frag grenade" },
	{ "objects\\weapons\\grenade\\plasma_grenade\\plasma_grenade", "weapons\\plasma grenade\\plasma grenade" },
};
#define H2W_GRENADE_COUNT ((long)(sizeof(h2w_grenades) / sizeof(h2w_grenades[0])))

/* the powerups: Halo 2's look on the kit's (its model - Halo 2's glass
cubes, their shaders -, its on-ground scale and pickup sound; the kit's
equipment keeps this engine's powerup: its type and pickup, Halo 2's time
written by h2_level.c). Halo 2's have no animation, attachments or idle
effects of their own (their hlmt: a render model and collision alone);
its multiplayer maps place no health pack */
static struct
{
	char const *h2;            /* Halo 2's equipment */
	char const *ce;            /* the kit's equipment */
} const h2w_powerups[] =
{
	{ "objects\\powerups\\active_camouflage\\active_camouflage", "powerups\\active camouflage" },
	{ "objects\\powerups\\over_shield\\over_shield", "powerups\\over shield" },
};
#define H2W_POWERUP_COUNT ((long)(sizeof(h2w_powerups) / sizeof(h2w_powerups[0])))

/* (while a powerup's model is made: its plasma_shield orb and one_alpha_env
glass shell this engine's plasma and glass shaders - h2w_shell_shaders) */
static boolean h2w_powerup_materials;

/* Halo 2's grenade projectile and its damage (read; written over the kit's
projectile and the damage its explosion effect names) */
struct h2w_grenade_jpt
{
	boolean valid;
	long tag;   /* (Halo 2's damage effect) */
	float radius[2], core_radius, damage_lower, damage_upper[2], acceleration;
};

struct h2w_grenade_data
{
	boolean valid;
	unsigned long flags;
	short timer_starts;
	float arming, danger_radius, timer[2], gravity_scale;
	/* (its material responses: the default material's a bounce) */
	struct h2w_responses responses;
	struct h2w_grenade_jpt explosion, attached;
};

static struct
{
	long count;
	struct h2w_weapon *weapons[H2W_MAXIMUM_WEAPONS];
	/* the grenades' models (build->models', NONE: not made) and the kit's
	equipment and projectile */
	struct h2w_model grenades[H2W_GRENADE_COUNT];
	long grenade_equipment[H2W_GRENADE_COUNT], grenade_projectile[H2W_GRENADE_COUNT];
	struct h2w_grenade_data grenade_data[H2W_GRENADE_COUNT];
	/* (their multiplayer on-ground scales, Halo 2's equipment's @C4; 0: not read) */
	float grenade_scale[H2W_GRENADE_COUNT];
	/* the weapons' sounds: tags of their own named sound\h2\<Halo 2's
	sound's name> (h2_sound.c gives each that sound), made from the kit's
	template sound with no samples of their own; their names (Halo 2's, less
	"sound\") */
	long sound_count;
	char sounds[H2W_MAXIMUM_SOUNDS][96];
	long kit_sound;
	/* the kit's pistol (NONE: none): the multiplayer start's second weapon,
	Halo 2's SMG where no magnum stands for it */
	long kit_pistol;
	/* the weapons' looping sounds (Halo 2's lsnd attachments scaled by its
	firing or its overheat): looping sound tags after the grenades', each
	its weapon's (h2w.weapons'), its function (this engine's weapon export:
	primary firing, overheated), its start, loop and stop sounds
	(h2w.sounds', NONE none), gain (linear) and fades */
	/* the powerups' (h2w_powerups') models (NONE: not made), the kit's
	equipment, Halo 2's on-ground scale and pickup sound (h2w.sounds', NONE) */
	struct h2w_model powerups[H2W_POWERUP_COUNT];
	long powerup_equipment[H2W_POWERUP_COUNT];
	float powerup_scale[H2W_POWERUP_COUNT];
	short powerup_sound[H2W_POWERUP_COUNT];
	long ammo_count;
	long ammo_weapons[H2W_MAXIMUM_WEAPONS];
	long kit_ammo;
	/* the kit's plasma (the cyborg's shield hit: its 3D noise volume) and
	glass (the Custom Edition camouflage's shell) the powerups' shell
	shaders are copies of (NONE: none - the shaders are written plain) */
	long kit_plasma, kit_glass;
	/* the weapons' muzzle lights made (h2w_light_read): light tags after
	the ammunition boxes', each its weapon's (h2w.weapons') */
	long light_count;
	long light_weapons[H2W_MAXIMUM_WEAPONS];
	long loop_count;
	struct h2w_loop
	{
		long weapon;
		short mode;
		short sounds[3];
		float gain, fade[2];
		char name[32];
	} loops[H2W_MAXIMUM_LOOPS];
} h2w;

/* (h3_weapons.c, which h2_level.c compiles after this file: Halo 3's
weapons in h2w.weapons, HALO_H3_WEAPONS) */
struct h3w_weapon;
static boolean h3_weapons_replaces(long kind);
static boolean h3_weapons_level_on(void);
static void h3_weapons_gather(struct build *build);
static char const *h3_weapons_label(struct h2w_weapon const *w);
static void h3_weapons_damage_write(struct build *build, struct region *r, unsigned long instances, unsigned long projectile_root,
	unsigned long base_projectile_root, long slot, struct h2w_weapon const *w, int which);
static void h3_weapons_triggers_write(struct region *r, unsigned long root, unsigned long triggers, struct h2w_weapon const *w);
static void h3_weapons_melee_area_write(struct region *r, unsigned long instances, unsigned long root, long weapon_index,
	struct h2w_weapon const *w);
static boolean h3_weapons_grenades_h3(void);
/* (h2_vehicles.c, at this file's end) */
static void h2_vehicles_gather(struct build *build);
static long h2_vehicles_tag_count(void);
static void h2_vehicles_tag_name(long index, unsigned long *group, char *text, long size);
static void h2_vehicles_kit_find(unsigned char *kit);
static long h2_vehicles_for(char const *collection);
static void h2_vehicles_write(struct build *build, struct region *r, unsigned long instances, long first, long model_first);
static void h2_vehicles_dispose(void);
static long h2v_tag_named(struct h2_cache *cache, unsigned long group, char const *name);
static long h2_vehicles_kit_strips(char *text, long size, long length);
/* (h2_hud_make.c, at this file's end) */
static void h2_hud_make(struct build *build);

/* ---------- private code: helpers */

H2_ALIGN static void h2w_name(struct h2_cache *cache, unsigned long id, char *out, long size)
{
	char *p;

	if (!h2_cache_string(cache, id, out, size))
		snprintf(out, size, "node %lu", id & 0xFFFF);
	/* (this engine's markers are named with spaces) */
	for (p = out; *p; p++)
	{
		if (*p == '_')
			*p = ' ';
	}
}

H2_ALIGN static float h2w_read_real(struct h2_cache *cache, unsigned long address)
{
	unsigned char const *p = h2_cache_at(cache, address, 4);

	return p ? readf(p) : 0.0f;
}

/* (h2_damage.c: a float written into a tag) */
H2_ALIGN static void h2w_write_real(void *at, float value)
{
	writef(at, value);
}

/* a damage effect made from Halo 2's damage_tag (NONE: none): its material
modifiers for the players' material types Halo 2's damage table's */
H2_ALIGN static void h2w_cache_damage_modifiers_write(struct h2_cache *cache, struct region *r, unsigned long damage_root, long damage_tag)
{
	/* (no Halo 2 map - a Halo 3 level's, h3_sandbox.c: the stand-in's stay) */
	if (cache && damage_tag != NONE)
	{
		h2_damage_modifiers_write(cache, damage_tag,
			region_at(r, damage_root + offsetof(struct damage_effect_definition, damage.material_modifiers)), h2w_write_real);
	}
}

H2_ALIGN static void h2w_damage_modifiers_write(struct build *build, struct region *r, unsigned long damage_root, long damage_tag)
{
	h2w_cache_damage_modifiers_write(build->cache, r, damage_root, damage_tag);
}

H2_ALIGN static short h2w_read_short(struct h2_cache *cache, unsigned long address)
{
	unsigned char const *p = h2_cache_at(cache, address, 2);

	return p ? read16(p) : 0;
}

H2_ALIGN static void h2w_quaternion_matrix(float const q[4], float m[9])
{
	float x = q[0], y = q[1], z = q[2], w = q[3];

	/* columns forward, left, up (Halo 2's turn) */
	m[0] = 1 - 2 * (y * y + z * z); m[1] = 2 * (x * y + z * w); m[2] = 2 * (x * z - y * w);
	m[3] = 2 * (x * y - z * w); m[4] = 1 - 2 * (x * x + z * z); m[5] = 2 * (y * z + x * w);
	m[6] = 2 * (x * z + y * w); m[7] = 2 * (y * z - x * w); m[8] = 1 - 2 * (x * x + y * y);
}

H2_ALIGN static void h2w_normalize4(float q[4])
{
	float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	int k;

	if (n < 1e-6f)
	{
		q[0] = q[1] = q[2] = 0.0f;
		q[3] = 1.0f;
		return;
	}
	for (k = 0; k < 4; k++)
		q[k] /= n;
}

/* ---------- private code: render models with nodes */

H2_ALIGN static void h2w_section_free(struct h2w_section *s)
{
	free(s->positions);
	free(s->coordinates);
	free(s->packed);
	free(s->nodes);
	free(s->weights);
	free(s->indices);
	free(s->parts);
	memset(s, 0, sizeof(*s));
}

/* a render model's section (its entry at section, the model at model):
positions and coordinates decompressed, every vertex's nodes and weights
(a rigid section's its node's, placed in the model as section_read's
callers place it), its strips made lists; FALSE if it cannot be read */
H2_ALIGN static boolean h2w_section_read(struct build *build, unsigned long model, unsigned long section, struct h2w_section *s)
{
	struct h2_cache *cache = build->cache;
	unsigned long geometry = section + 0x10;
	unsigned char const *g = h2_cache_at(cache, geometry, 0x40);
	unsigned long reference, size, section_size, resources, data;
	long resource_count, i, k;
	unsigned char *raw;
	unsigned char const *parts = NULL, *strips = NULL, *descriptors = NULL, *node_map = NULL;
	unsigned long parts_size = 0, strips_size = 0, descriptors_size = 0, node_map_size = 0;
	unsigned char const *buffers[8] = { 0 };
	unsigned long buffer_sizes[8] = { 0 };
	float position_bounds[6] = { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f };
	float coordinate_bounds[4] = { -1.0f, 1.0f, -1.0f, 1.0f };
	unsigned short flags;
	short rigid;
	long compression_count;
	unsigned long compression;

	memset(s, 0, sizeof(*s));
	if (!g)
		return FALSE;
	flags = (unsigned short)read16((unsigned char const *)h2_cache_at(cache, section + 0x1A, 2));
	rigid = read16((unsigned char const *)h2_cache_at(cache, section + 0x2C, 2));
	reference = read32(g + 0x28);
	size = read32(g + 0x2C);
	if (h2_cache_is_mcc(cache) && reference != 0xFFFFFFFFUL && !(reference >> 30))
		reference |= (unsigned long)h2_cache_address_source(cache, geometry) << 30;
	section_size = read32(g + 0x30);
	if (!size || reference == 0xFFFFFFFFUL || size > H2_SECTION_MAXIMUM_BYTES || section_size > size ||
		!h2_cache_block(cache, geometry + 0x38, 16, &resource_count, &resources))
	{
		return FALSE;
	}
	raw = malloc(size);
	if (!raw || !h2_cache_resource_read(cache, reference, raw, size) || memcmp(raw, "hklb", 4) != 0 ||
		8 + section_size > size)
	{
		free(raw);
		return FALSE;
	}
	data = 8 + section_size;
	for (i = 0; i < resource_count; i++)
	{
		unsigned char const *r = h2_cache_at(cache, resources + 16 * i, 16);
		unsigned char type = r[0];
		unsigned short primary = (unsigned short)read16(r + 2);
		short element = read16(r + 6);
		unsigned long rsize = read32(r + 8);
		unsigned long roffset = read32(r + 12);

		if (roffset > size - data || rsize > size - data - roffset)
			continue;
		if (type == 0 && primary == 0)
			parts = raw + data + roffset, parts_size = rsize;
		else if (type == 0 && primary == 0x20)
			strips = raw + data + roffset, strips_size = rsize;
		else if (type == 0 && primary == 0x38)
			descriptors = raw + data + roffset, descriptors_size = rsize;
		else if (type == 0 && primary == 0x68)
			node_map = raw + data + roffset, node_map_size = rsize;
		else if (type == 2 && element >= 0 && element < 8)
			buffers[element] = raw + data + roffset, buffer_sizes[element] = rsize;
	}
	if (!parts || !strips || !descriptors)
	{
		free(raw);
		return FALSE;
	}
	for (i = 0; i < (long)(descriptors_size / 32) && i < 8; i++)
	{
		unsigned char type = descriptors[32 * i];
		unsigned long stride = descriptors[32 * i + 1];
		unsigned char const *b = buffers[i];
		unsigned long bs = buffer_sizes[i];
		long n;

		if (!b)
			continue;
		if ((type == 0x01 || type == 0x02 || type == 0x03 || type == 0x04 || type == 0x05 || type == 0x06) && !s->positions &&
			stride >= 12)
		{
			n = (long)(bs / stride);
			s->vertex_count = n;
			s->positions = malloc((unsigned long)n * 12 + 12);
			s->nodes = calloc((unsigned long)n * 4 + 4, 1);
			s->weights = calloc((unsigned long)n * 4 + 4, 1);
			if (!s->positions || !s->nodes || !s->weights)
				break;
			for (k = 0; k < n; k++)
			{
				unsigned char const *v = b + stride * k;

				memcpy(s->positions + 3 * k, v, 12);
				if (stride >= 20)
				{
					memcpy(s->nodes + 4 * k, v + 12, 4);
					memcpy(s->weights + 4 * k, v + 16, 4);
				}
				else if (stride >= 16)
				{
					s->nodes[4 * k] = v[12];
					s->weights[4 * k] = 255;
				}
				else
				{
					s->nodes[4 * k] = (unsigned char)(rigid >= 0 ? rigid : 0);
					s->weights[4 * k] = 255;
				}
			}
			/* (the node map: a skinned section's node numbers its own) */
			if (stride >= 16 && node_map)
			{
				for (k = 0; k < 4 * n; k++)
				{
					if (s->weights[k] && s->nodes[k] < node_map_size)
						s->nodes[k] = node_map[s->nodes[k]];
				}
			}
		}
		else if ((type == 0x17 || type == 0x18) && !s->coordinates && (s->coordinates = malloc(bs + 8)) != NULL)
			memcpy(s->coordinates, b, bs);
		else if (type == 0x19 && !s->packed && (s->packed = malloc(bs / 36 * 12 + 12)) != NULL)
		{
			for (k = 0; k < (long)(bs / 36); k++)
			{
				int axis;

				for (axis = 0; axis < 3; axis++)
				{
					float v[3];

					memcpy(v, b + 36 * k + 12 * axis, 12);
					s->packed[3 * k + axis] = pack_vector(v);
				}
			}
		}
		else if (type == 0x1B && !s->packed && (s->packed = malloc(bs + 12)) != NULL)
			memcpy(s->packed, b, bs);
	}
	if (!s->positions || !s->coordinates || !s->packed || s->vertex_count <= 0)
	{
		free(raw);
		h2w_section_free(s);
		return FALSE;
	}
	for (i = 0; i < (long)(descriptors_size / 32) && i < 8; i++)
	{
		unsigned char type = descriptors[32 * i];

		if (((type == 0x17 || type == 0x18) && buffer_sizes[i] / 8 < (unsigned long)s->vertex_count) ||
			(type == 0x19 && buffer_sizes[i] / 36 < (unsigned long)s->vertex_count) ||
			(type == 0x1B && buffer_sizes[i] / 12 < (unsigned long)s->vertex_count))
		{
			free(raw);
			h2w_section_free(s);
			return FALSE;
		}
	}
	s->index_count = (long)(strips_size / 2);
	s->indices = malloc(strips_size + 2);
	s->part_count = (long)(parts_size / H2_PART_SIZE);
	s->parts = calloc(s->part_count + 1, sizeof(*s->parts));
	if (!s->indices || !s->parts)
	{
		free(raw);
		h2w_section_free(s);
		return FALSE;
	}
	memcpy(s->indices, strips, strips_size);
	for (i = 0; i < s->part_count; i++)
	{
		unsigned char const *p = parts + H2_PART_SIZE * i;

		s->parts[i].type = read16(p);
		s->parts[i].flags = read16(p + 2);
		s->parts[i].material = read16(p + 4);
		s->parts[i].first = (unsigned short)read16(p + 6);
		s->parts[i].count = (unsigned short)read16(p + 8);
	}
	free(raw);

	/* decompressed (the section's bounds, else the model's) */
	{
		unsigned char const *c;

		if (((h2_cache_block(cache, section + 0x1C, 0x38, &compression_count, &compression) && compression_count > 0) ||
				(h2_cache_block(cache, model + 0x14, 0x38, &compression_count, &compression) && compression_count > 0)) &&
			(c = h2_cache_at(cache, compression, 0x38)) != NULL)
		{
			for (k = 0; k < 6; k++)
				memcpy(&position_bounds[k], c + 4 * k, 4);
			for (k = 0; k < 4; k++)
				memcpy(&coordinate_bounds[k], c + 0x18 + 4 * k, 4);
		}
	}
	for (i = 0; i < s->vertex_count; i++)
	{
		if (flags & 1)
		{
			for (k = 0; k < 3; k++)
				s->positions[3 * i + k] = position_bounds[2 * k] +
					0.5f * (s->positions[3 * i + k] + 1.0f) * (position_bounds[2 * k + 1] - position_bounds[2 * k]);
		}
		if (flags & 2)
		{
			for (k = 0; k < 2; k++)
				s->coordinates[2 * i + k] = coordinate_bounds[2 * k] +
					0.5f * (s->coordinates[2 * i + k] + 1.0f) * (coordinate_bounds[2 * k + 1] - coordinate_bounds[2 * k]);
		}
	}
	/* (a rigid section's vertices are in the model's space already, as the
	skinned ones are: its node is only their node - Halo 2's Warthog hull,
	at its node's height, shows it) */
	/* strips to lists (render_section_prepare's) */
	{
		unsigned short *list = malloc((s->index_count * 3 + 3) * sizeof(unsigned short));
		long used = 0, p, n;

		if (!list)
		{
			h2w_section_free(s);
			return FALSE;
		}
		for (p = 0; p < s->part_count; p++)
		{
			long first = s->parts[p].first, strip = s->parts[p].count, start = used;

			if (first < 0 || strip < 3 || first > s->index_count || strip > s->index_count - first)
			{
				s->parts[p].count = 0;
				continue;
			}
			for (n = 0; n + 2 < strip; n += (s->parts[p].flags & 8) ? 3 : 1)
			{
				unsigned short a = s->indices[first + n], b = s->indices[first + n + 1], d = s->indices[first + n + 2];
				boolean odd = !(s->parts[p].flags & 8) && (n & 1);

				if (a == b || b == d || a == d || a >= s->vertex_count || b >= s->vertex_count || d >= s->vertex_count)
					continue;
				list[used++] = a;
				list[used++] = odd ? d : b;
				list[used++] = odd ? b : d;
			}
			s->parts[p].first = start;
			s->parts[p].count = used - start;
		}
		free(s->indices);
		s->indices = list;
		s->index_count = used;
	}

	return TRUE;
}

/* ---------- private code: the powerups' shells (S7)

Halo 2's powerups (H2EK: objects\powerups\<over_shield|active_camouflage>)
are an orb in a glass box with metal corners. The orb is a plasma_shield
(template shaders\shader_templates\transparent\plasma_shield, its pass
shaders\shader_passes\plasma\plasma_shield): additive; two lookups of a 3D
noise volume (rasterizer\plasma_shield_noise) at the vertex's position
through a scale and a moving translation each (the pass's vertex
constants 0-2 and 3-5: Halo 2's noyze0 and noyze1 - the overshield's
scales 16 and 22, translations of a period of 6, 6, 6 and 10, 4, 8 seconds
along x, y and z), their difference squared - the plasma - coloured by
three bands (color_wide, color_sharp, center_line: the pixel constants 0-2;
the overshield gold, the camouflage blue), and the whole by the vertex's
colour, the "color" facing the camera (white) to the "glancing_color" at
its edge (black: the orb fades at its rim; vertex constants 6, 7). The
shell is a one_alpha_env (default_transparent_one_sided): the environment
cube (glassy_sharp_reflections) times the specular map, tinted from
tint_color facing to glancing_tint_color at the edge (vertex constants 0,
1: the overshield gold, the camouflage blue), added, over the alpha blend
map (its alpha the shell's cover) blended.

This engine's own shaders do the same: the plasma (spla - the cyborg's
shield hit, a copy: its 3D noise volume) two noise volumes moving at a
scale and velocity each, coloured from its perpendicular to its parallel
colour; the glass (sgla - the Custom Edition camouflage's shell, a copy) a
reflection cube from its perpendicular to its parallel colour, added, and
a diffuse map alpha blended. Written from the shader's postprocess
definition (0x20: its bitmaps at 4, its pixel constants at 0xC, its vertex
constants at 0x14) and its animations (at 0x3C, 0x14 each: the period at
8, the function at 0xC; at 0x44 the component each sets, 4 bytes each: the
animation and the component - 1..3 a scale, 4..6 a translation along x, y,
z): a noise's scale its scale, its velocity a translation's 1/period along
each axis (Halo 2's translations run 0 to 1 over their period). The three
bands are one colour here, their mean (this engine's plasma has one); the
orb's offset (a 0.01 unit throb, vertex constant 8) is left out. */

H2_ALIGN static unsigned long h2w_root(struct region *r, unsigned long instances, long index);
H2_ALIGN static unsigned long h2w_tag_copy(struct region *r, unsigned long from, unsigned long size, unsigned long group);

/* a Halo 2 one_alpha_env shader's glass (sgla) fields filled into sh: its
reflection the cube (its first map), tinted facing and glancing (vertex
constants 0 and 1), its alpha blend map (its third) the diffuse map; FALSE
if it cannot be - the powerups' shells, and the levels' glass (windows,
canopies, visors' glass: h2_level.c) */
H2_ALIGN static boolean h2w_glass_fill(struct build *build, long shader_tag, struct shader *sh)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	long definition_count, bitmap_count, vertex_count, i;
	unsigned long definitions, bitmaps, vertices;
	int c;

	if (!h2_cache_tag_get(cache, shader_tag, &t) ||
		!h2_cache_block(cache, t.address + 0x20, 0x7C, &definition_count, &definitions) || definition_count < 1 ||
		!h2_cache_block(cache, definitions + 0x4, 0xC, &bitmap_count, &bitmaps) ||
		!h2_cache_block(cache, definitions + 0x14, 0x10, &vertex_count, &vertices) || vertex_count < 2 || bitmap_count < 3)
	{
		return FALSE;
	}
	sh->group = 'sgla';
	sh->glass_specular_bitmap = NONE;
	for (i = 0; i < bitmap_count; i++)
	{
		long b = h2_cache_tag_from_handle(cache, h2_cache_read32(cache, bitmaps + 0xC * i));
		long made;

		if (b == NONE)
			continue;
		if (i == 0 && bitmap_tag_is_cube(build, b) && (made = bitmap_for(build, b)) != NONE && build->bitmaps[made].type == 2)
			sh->cube_bitmap = made;
		else if (i == 2 && !bitmap_tag_is_cube(build, b) && (made = bitmap_for(build, b)) != NONE && build->bitmaps[made].type == 0)
			sh->bitmap = made;
		else if (i == 1 && !bitmap_tag_is_cube(build, b) && (made = bitmap_for(build, b)) != NONE && build->bitmaps[made].type == 0)
			sh->glass_specular_bitmap = made;
	}
	for (c = 0; c < 3; c++)
	{
		sh->shell_colors[0][c] = h2w_read_real(cache, vertices + 4 * c);
		sh->shell_colors[1][c] = h2w_read_real(cache, vertices + 0x10 + 4 * c);
	}
	sh->shell_colors[0][3] = sh->shell_colors[1][3] = 1.0f;
	/* (Halo 2's pass, default_transparent_one_sided: pixel constants
	environment colour, alpha blend colour, the opacity in the third's top
	byte; vertex constants 2 and 3 the brightness facing and glancing, in w) */
	{
		long pixel_count;
		unsigned long pixels;

		if (h2_cache_block(cache, definitions + 0xC, 4, &pixel_count, &pixels) && pixel_count >= 3 && vertex_count >= 4)
		{
			unsigned long environment = h2_cache_read32(cache, pixels);
			unsigned long alpha_blend = h2_cache_read32(cache, pixels + 4);

			for (c = 0; c < 3; c++)
			{
				sh->glass_environment[c] = ((environment >> (16 - 8 * c)) & 255) / 255.0f;
				sh->glass_alpha_blend[c] = ((alpha_blend >> (16 - 8 * c)) & 255) / 255.0f;
			}
			sh->glass_opacity = (h2_cache_read32(cache, pixels + 8) >> 24) / 255.0f;
			sh->glass_brightness[0] = h2w_read_real(cache, vertices + 0x20 + 12);
			sh->glass_brightness[1] = h2w_read_real(cache, vertices + 0x30 + 12);
			sh->glass_pass = TRUE;
		}
	}
	return TRUE;
}

/* a powerup's shell shader for a Halo 2 shader of the template (its index
in build->shaders), NONE if it is not one */
H2_ALIGN static short h2w_shell_shader(struct build *build, long shader_tag, char const *template_name)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	struct shader *sh;
	long definition_count, bitmap_count, pixel_count, vertex_count, animation_count, link_count, i;
	unsigned long definitions, bitmaps, pixels, vertices, animations, links;
	boolean plasma = strstr(template_name, "\\transparent\\plasma_shield") != NULL;
	boolean glass = strstr(template_name, "\\transparent\\one_alpha_env") != NULL && !strstr(template_name, "one_alpha_env_");
	unsigned long colors[4] = { 0, 0, 0, 0 };
	float v[10][4];

	if (!plasma && !glass)
		return NONE;
	for (i = 0; i < build->shader_count; i++)
	{
		if (build->shaders[i].h2_tag == shader_tag && (build->shaders[i].group == 'spla' || build->shaders[i].group == 'sgla'))
			return (short)i;
	}
	if (build->shader_count >= BUILD_SHADER_ROOM(build) || !h2_cache_tag_get(cache, shader_tag, &t) ||
		!h2_cache_block(cache, t.address + 0x20, 0x7C, &definition_count, &definitions) || definition_count < 1 ||
		!h2_cache_block(cache, definitions + 0x4, 0xC, &bitmap_count, &bitmaps) ||
		!h2_cache_block(cache, definitions + 0xC, 4, &pixel_count, &pixels) ||
		!h2_cache_block(cache, definitions + 0x14, 0x10, &vertex_count, &vertices))
	{
		return NONE;
	}
	memset(v, 0, sizeof(v));
	for (i = 0; i < vertex_count && i < 10; i++)
	{
		int c;

		for (c = 0; c < 4; c++)
			v[i][c] = h2w_read_real(cache, vertices + 0x10 * i + 4 * c);
	}
	for (i = 0; i < pixel_count && i < 4; i++)
		colors[i] = h2_cache_read32(cache, pixels + 4 * i);
	sh = &build->shaders[build->shader_count];
	memset(sh, 0, sizeof(*sh));
	sh->h2_tag = shader_tag;
	sh->bitmap = sh->alpha_bitmap = sh->ripple_bitmap = sh->reflection_bitmap = NONE;
	sh->detail_bitmap[0] = sh->detail_bitmap[1] = sh->bump_bitmap = NONE;
	sh->cube_bitmap = sh->illum_bitmap = sh->multipurpose_bitmap = sh->specular_multipurpose = NONE;
	sh->rendered = TRUE;
	snprintf(sh->path, sizeof(sh->path), "h2\\shaders\\%s %s", tag_short_name(h2_cache_tag_name(cache, shader_tag)), plasma ? "plasma" : "glass");
	if (plasma)
	{
		int n, c;
		long group = -1;
		float scale[2] = { 0.0f, 0.0f }, velocity[2][3];

		memset(velocity, 0, sizeof(velocity));
		if (vertex_count < 8 || pixel_count < 3)
			return NONE;
		sh->group = 'spla';
		/* (the bands' mean, facing; glancing Halo 2's glancing_color) */
		for (c = 0; c < 3; c++)
		{
			float sum = 0.0f;

			for (n = 0; n < 3; n++)
				sum += ((colors[n] >> (16 - 8 * c)) & 255) / 255.0f;
			sh->shell_colors[0][c] = sum / 3.0f * v[6][c];
			sh->shell_colors[1][c] = sum / 3.0f * v[7][c];
		}
		sh->shell_colors[0][3] = 1.0f;
		sh->shell_colors[1][3] = v[7][0] + v[7][1] + v[7][2] > 0.0f ? 1.0f : 0.0f;
		/* (the two noises: their scales the vertex constants' 0 and 3; their
		translations the animations, each group's in the order the links
		have them - a group begins at a scale x) */
		scale[0] = v[0][0];
		scale[1] = v[3][0];
		if (h2_cache_block(cache, definitions + 0x3C, 0x14, &animation_count, &animations) &&
			h2_cache_block(cache, definitions + 0x44, 4, &link_count, &links))
		{
			short last = -1;

			for (i = 0; i < link_count; i++)
			{
				short animation = h2w_read_short(cache, links + 4 * i), component = h2w_read_short(cache, links + 4 * i + 2);
				float period;

				if (animation < 0 || animation >= animation_count)
					continue;
				if (component == 1 && last != 1)
					group++;
				last = component;
				period = h2w_read_real(cache, animations + 0x14 * animation + 8);
				if (group >= 0 && group < 2 && component >= 4 && component <= 6 && period > 0.0f)
				{
					/* (a transition 0 to 1 over the period: a velocity of
					1/period; its function's type, 2, at its data's first byte) */
					unsigned char const *f = h2_cache_at(cache, h2_cache_read32(cache, animations + 0x14 * animation + 0x10), 1);

					if (f && f[0] == 2)
						velocity[group][component - 4] = 1.0f / period;
				}
			}
		}
		for (n = 0; n < 2; n++)
		{
			sh->noise_scale[n] = scale[n] > 0.0f ? scale[n] : 1.0f;
			for (c = 0; c < 3; c++)
				sh->noise_velocity[n][c] = velocity[n][c];
		}
		/* (the pass itself for the desktop's program: its three colours,
		color and glancing_color - the orbs' are constants - h2_plasma_shield.h) */
		sh->shield_pass_read = h2_plasma_shield_read(cache, shader_tag, template_name, &sh->shield_pass);
		platform_log("h2 weapons: %s: plasma, colour %.2f %.2f %.2f to %.2f %.2f %.2f, noise x%.0f moving %.3f %.3f %.3f, x%.0f moving %.3f %.3f %.3f",
			sh->path, sh->shell_colors[0][0], sh->shell_colors[0][1], sh->shell_colors[0][2], sh->shell_colors[1][0], sh->shell_colors[1][1],
			sh->shell_colors[1][2], sh->noise_scale[0], sh->noise_velocity[0][0], sh->noise_velocity[0][1], sh->noise_velocity[0][2],
			sh->noise_scale[1], sh->noise_velocity[1][0], sh->noise_velocity[1][1], sh->noise_velocity[1][2]);
	}
	else
	{
		int c;

		if (!h2w_glass_fill(build, shader_tag, sh))
			return NONE;
		platform_log("h2 weapons: %s: glass, reflection %.2f %.2f %.2f to %.2f %.2f %.2f, cube %ld, alpha blend map %ld", sh->path,
			sh->shell_colors[0][0], sh->shell_colors[0][1], sh->shell_colors[0][2], sh->shell_colors[1][0], sh->shell_colors[1][1],
			sh->shell_colors[1][2], sh->cube_bitmap, sh->bitmap);
	}
	return (short)build->shader_count++;
}

/* a shell shader's tag written (its slot), FALSE if it is not one: a copy
of the kit's plasma or glass, Halo 2's fields over it */
H2_ALIGN static boolean h2w_shell_shader_write(struct region *r, unsigned long instances, long slot, struct shader const *sh, long bitmap_first)
{
	long kit = sh->group == 'spla' ? h2w.kit_plasma : sh->group == 'sgla' ? h2w.kit_glass : NONE;
	unsigned long from, root;
	int n, c;

	if (sh->group != 'spla' && sh->group != 'sgla')
		return FALSE;
	from = kit != NONE ? h2w_root(r, instances, kit) : 0;
	root = from ? h2w_tag_copy(r, from, sh->group == 'spla' ? 0x14C : 0x1E0, sh->group) :
		region_alloc(r, sh->group == 'spla' ? 0x14C : 0x1E0);
	if (r->b.failed)
		return TRUE;
	write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * slot + 20), r->link + root);
	if (sh->group == 'spla' && from && getenv("HALO_H2_WEAPON_LOG"))
	{
		unsigned char const *k = region_at(r, from);

		platform_log("h2 weapon fields: kit plasma: flags %04x sources %d %d exp %.2f offset %.3f %.2f perp %.2f %.2f %.2f %.2f par %.2f %.2f %.2f %.2f "
			"colour source %d noise %.2f (%.2f %.2f %.2f) x%.2f, %.2f (%.2f %.2f %.2f) x%.2f", read16(k + 0x28), (short)read16(k + 0x2C),
			(short)read16(k + 0x34), readf(k + 0x30), readf(k + 0x38), readf(k + 0x3C), readf(k + 0x60), readf(k + 0x64), readf(k + 0x68),
			readf(k + 0x6C), readf(k + 0x70), readf(k + 0x74), readf(k + 0x78), readf(k + 0x7C), (short)read16(k + 0x80), readf(k + 0xC0),
			readf(k + 0xC4), readf(k + 0xC8), readf(k + 0xCC), readf(k + 0xD0), readf(k + 0x108), readf(k + 0x10C), readf(k + 0x110),
			readf(k + 0x114), readf(k + 0x118));
	}
	if (sh->group == 'spla')
	{
		/* (perpendicular alpha and colour at 0x60, parallel at 0x70; no
		colour or exponent source; each noise's period at 0xC0/0x108, its
		direction, its scale) */
		writef(region_at(r, root + 0x60), sh->shell_colors[0][3]);
		writef(region_at(r, root + 0x70), sh->shell_colors[1][3]);
		for (c = 0; c < 3; c++)
		{
			writef(region_at(r, root + 0x64 + 4 * c), sh->shell_colors[0][c]);
			writef(region_at(r, root + 0x74 + 4 * c), sh->shell_colors[1][c]);
		}
		write16(region_at(r, root + 0x80), 0);
		/* (a level object's shield damaged shell: its intensity the object's
		current_shield_damage function) */
		write16(region_at(r, root + 0x2C), (unsigned short)sh->shell_input);
		write16(region_at(r, root + 0x34), 0);
		for (n = 0; n < 2; n++)
		{
			unsigned long at = root + (n ? 0x108 : 0xC0);

			writef(region_at(r, at), 1.0f);
			for (c = 0; c < 3; c++)
				writef(region_at(r, at + 4 + 4 * c), sh->noise_velocity[n][c]);
			writef(region_at(r, at + 0x10), sh->noise_scale[n]);
		}
		/* (Halo 2's pass beside the CE fields, h2_plasma_shield.h: the
		desktop's program; no input - the orbs' functions run by time) */
		if (sh->shield_pass_read && !getenv("HALO_H2_PLASMA_SHIELD_OLD"))
			h2_plasma_shield_write(region_at(r, root), &sh->shield_pass, sh->shell_input);
		if (!from)
			platform_log("h2 weapons: %s: the kit has no plasma (no noise volume): not drawn", sh->path);
		return TRUE;
	}
	/* (glass: no background tint; a flat cube reflection, perpendicular and
	parallel colours (a, r, g, b) at 0x8C and 0x9C, its map at 0xAC; the
	diffuse map at 0x158, scale 1, its detail none) */
	writef(region_at(r, root + 0x54), 1.0f);
	writef(region_at(r, root + 0x58), 1.0f);
	writef(region_at(r, root + 0x5C), 1.0f);
	region_reference(r, root + 0x64, 0, 0xFFFFFFFFUL);
	write16(region_at(r, root + 0x8A), 1);
	for (n = 0; n < 2; n++)
	{
		writef(region_at(r, root + 0x8C + 0x10 * n), sh->shell_colors[n][3]);
		for (c = 0; c < 3; c++)
			writef(region_at(r, root + 0x90 + 0x10 * n + 4 * c), sh->shell_colors[n][c]);
	}
	if (sh->cube_bitmap != NONE)
		region_reference(r, root + 0xAC, 'bitm', tag_id(bitmap_first + sh->cube_bitmap));
	region_reference(r, root + 0xC0, 0, 0xFFFFFFFFUL);
	writef(region_at(r, root + 0x154), 1.0f);
	region_reference(r, root + 0x158, sh->bitmap != NONE ? 'bitm' : 0, sh->bitmap != NONE ? tag_id(bitmap_first + sh->bitmap) : 0xFFFFFFFFUL);
	region_reference(r, root + 0x16C, sh->glass_pass && sh->glass_specular_bitmap != NONE ? 'bitm' : 0,
		sh->glass_pass && sh->glass_specular_bitmap != NONE ? tag_id(bitmap_first + sh->glass_specular_bitmap) : 0xFFFFFFFFUL);
	/* (Halo 2's pass beside the CE fields, in the padding before the tint
	colour: the renderer's h2_glass_fragment_source) */
	if (sh->glass_pass)
	{
		memcpy(region_at(r, root + 0x2C), "H2GL", 4);
		for (c = 0; c < 3; c++)
		{
			writef(region_at(r, root + 0x30 + 4 * c), sh->glass_environment[c]);
			writef(region_at(r, root + 0x3C + 4 * c), sh->glass_alpha_blend[c]);
		}
		writef(region_at(r, root + 0x48), sh->glass_opacity);
		writef(region_at(r, root + 0x4C), sh->glass_brightness[0]);
		writef(region_at(r, root + 0x50), sh->glass_brightness[1]);
	}
	return TRUE;
}

/* the stand-in for Halo 2's plasma shaders (a sword's blade, a plasma
weapon's current: drawn by Halo 2 from noise and masks this engine has no
shader for) - its glow, a light blue added; NONE */
H2_ALIGN static short h2w_plasma_shader(struct build *build, int pink)
{
	static unsigned char const colours[3][3] = { { 110, 190, 255 }, { 230, 90, 255 }, { 70, 82, 90 } };   /* R G B */
	static char const *const names[3] = { "plasma glow", "plasma glow pink", "scope screen" };
	char path[64];
	struct shader *sh;
	unsigned char *pixels;
	long b, k;

	/* (once a build: the bitmaps and shaders are the build's; the needler's
	crystals pink, the others' light blue, as Halo 2's are) */
	pink = pink < 0 || pink > 2 ? 0 : pink;
	if (build->h2w_plasma_plus1[pink])
		return (short)(build->h2w_plasma_plus1[pink] - 1);
	if (build->shader_count >= BUILD_SHADER_ROOM(build) || !(pixels = malloc(8 * 8 * 4)))
		return NONE;
	for (k = 0; k < 64; k++)
	{
		pixels[4 * k] = colours[pink][2];
		pixels[4 * k + 1] = colours[pink][1];
		pixels[4 * k + 2] = colours[pink][0];
		pixels[4 * k + 3] = 255;
	}
	snprintf(path, sizeof(path), "h2\\bitmaps\\%s", names[pink]);
	b = bitmap_made(build, path, 8, 8, 11, 0, 0, pixels, 8 * 8 * 4);
	if (b == NONE)
		return NONE;
	sh = &build->shaders[build->shader_count];
	memset(sh, 0, sizeof(*sh));
	sh->h2_tag = NONE;
	sh->bitmap = b;
	sh->alpha_bitmap = sh->ripple_bitmap = sh->reflection_bitmap = NONE;
	sh->group = 'schi';
	sh->blend = 3;          /* added */
	sh->rendered = TRUE;
	snprintf(sh->path, sizeof(sh->path), "h2\\shaders\\%s", names[pink]);
	build->h2w_plasma_plus1[pink] = (short)(build->shader_count + 1);
	return (short)build->shader_count++;
}

/* (the PC tier's: the Vita tier keeps the dim glass - its renderer's
copy splits the screen's scene and waits for the GPU, and its shipped
programs (shaders.pak) have only what its levels drew; the tier is in the
level's header, so a level of the other tier is made again) */
H2_ALIGN static boolean h2w_scope_camera_wanted(void)
{
	/* (every tier: Halo 2's lens shows the view down the barrel on the Vita
	too - it had the flat "scope screen" glow, no view at all (Fable audit
	Nov 7). Its cost, a copy of the screen while the rifle is out unzoomed,
	is the PC's; HALO_H2_SCOPE_CAMERA_PC_ONLY=1: the PC tier's alone, as
	before) */
	return h2_caps_tier() == _h2_caps_tier_pc || !getenv("HALO_H2_SCOPE_CAMERA_PC_ONLY");
}

/* Halo 2's scope screen (the sniper rifle's: template texture_camera\scope,
its one map of its own the scope_view - the reticle's ticks and frame on a
light grey, which Halo 2 multiplies over the view its texture camera draws
down the barrel): a transparent chicago shader of two maps, the first
"h2\bitmaps\scope camera" - an 8x8 dim grey, which the renderer swaps for
its copy of the screen about the crosshair (rasterizer_scope_camera_*,
rasterizer_xbox_active_camouflage.c) - times the scope_view; drawn over
the lens (alpha blended, the scope_view's alpha: opaque). Where the copy
is off (HALO_SCOPE_CAMERA=0) the grey under the overlay is the dim glass
it was, now with its reticle. The PC tier's (h2w_scope_camera_wanted);
NONE if it cannot be made */
H2_ALIGN static short h2w_scope_camera_shader(struct build *build, long shader_tag)
{
	/* (made once a build: the build's own list, not statics - the derive workers make them on builds of their own at
	once, and a static list kept every build's entries: past its few, later levels made none) */
	struct h2w_made_list *list = &build->h2w_made[0];
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	long definition_count, bitmap_count, k, view = NONE, overlay, camera;
	unsigned long definitions, bitmaps;
	unsigned char *pixels;
	struct shader *sh;

	for (k = 0; k < list->made_count; k++)
	{
		if (list->made[k].shader_tag == shader_tag)
			return list->made[k].shader;
	}
	if (list->made_count >= 4 || build->shader_count >= BUILD_SHADER_ROOM(build) || !h2_cache_tag_get(cache, shader_tag, &t) ||
		!h2_cache_block(cache, t.address + 0x20, 0x7C, &definition_count, &definitions) || definition_count < 1 ||
		!h2_cache_block(cache, definitions + 4, 0xC, &bitmap_count, &bitmaps))
	{
		return NONE;
	}
	/* (its own map: not one of the template's defaults) */
	for (k = 0; k < bitmap_count && view == NONE; k++)
	{
		long candidate = h2_cache_tag_from_handle(cache, h2_cache_read32(cache, bitmaps + 0xC * k));

		if (candidate != NONE && !strstr(h2_cache_tag_name(cache, candidate), "\\default_bitmaps\\"))
			view = candidate;
	}
	if (view == NONE || (overlay = bitmap_for(build, view)) == NONE || !(pixels = malloc(8 * 8 * 4)))
		return NONE;
	for (k = 0; k < 64; k++)
	{
		pixels[4 * k] = 90;     /* B G R A: the glass's dim grey */
		pixels[4 * k + 1] = 82;
		pixels[4 * k + 2] = 70;
		pixels[4 * k + 3] = 255;
	}
	camera = bitmap_made(build, H2_SCOPE_CAMERA_BITMAP, 8, 8, 11, 0, 0, pixels, 8 * 8 * 4);
	if (camera == NONE || build->shader_count >= BUILD_SHADER_ROOM(build))
		return NONE;
	sh = &build->shaders[build->shader_count];
	memset(sh, 0, sizeof(*sh));
	sh->h2_tag = NONE;
	sh->bitmap = camera;
	sh->scope_overlay_bitmap = overlay;
	sh->scope_camera = TRUE;
	sh->alpha_bitmap = sh->ripple_bitmap = sh->reflection_bitmap = NONE;
	sh->group = 'schi';
	sh->blend = 0;          /* alpha blended */
	sh->rendered = TRUE;
	snprintf(sh->path, sizeof(sh->path), "h2\\shaders\\%s camera", tag_short_name(h2_cache_tag_name(cache, shader_tag)));
	list->made[list->made_count].shader_tag = shader_tag;
	list->made[list->made_count].shader = (short)build->shader_count++;
	return list->made[list->made_count++].shader;
}

/* a weapon's counter's digits (Halo 2's illum_detail shaders 'tens' and
'ones': its numbers bitmap's ten images, the one shown picked by the
weapon's rounds - its shader's animated bitmap index, input
primary_ammunition_tens/ones - glowing its colour, the first pixel constant):
twenty added shaders, the tens' 0..9 then the ones' 0..9, consecutive (their
tags too: models.c draws a part of the first with the one its weapon's
rounds pick, h2_weapon_counter.c); the first's index, place 0 the tens' and
1 the ones', NONE if they cannot be made */
H2_ALIGN static short h2w_digit_shader_made(struct build *build, long shader_tag, int place);

/* (piece J: its bitmaps made again alone at load - derived_recipe) */
H2_ALIGN static short h2w_digit_shader(struct build *build, long shader_tag, int place)
{
	struct derived_recipe saved = build->making;
	short made;

	memset(&build->making, 0, sizeof(build->making));
	build->making.kind_plus1 = _derived_recipe_weapon_digits + 1;
	build->making.args[0] = (unsigned long)shader_tag;
	made = h2w_digit_shader_made(build, shader_tag, place);
	build->making = saved;
	return made;
}

H2_ALIGN static short h2w_digit_shader_made(struct build *build, long shader_tag, int place)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	long definition_count, bitmap_count, constant_count, numbers = NONE, count, d, k;
	unsigned long definitions, bitmaps, constants, images, colour = 0x8EE0F4UL;
	int p;

	if (build->h2w_digit_tried)
		return build->h2w_digit_first == NONE ? NONE : (short)(build->h2w_digit_first + 10 * place);
	build->h2w_digit_tried = TRUE;
	build->h2w_digit_first = NONE;
	if (getenv("HALO_H2_NO_WEAPON_COUNTERS") || build->shader_count + 20 > BUILD_SHADER_ROOM(build) || !h2_cache_tag_get(cache, shader_tag, &t) ||
		!h2_cache_block(cache, t.address + 0x20, 0x7C, &definition_count, &definitions) || definition_count < 1 ||
		!h2_cache_block(cache, definitions + 4, 0xC, &bitmap_count, &bitmaps) || bitmap_count < 1)
	{
		return NONE;
	}
	numbers = h2_cache_tag_from_handle(cache, h2_cache_read32(cache, bitmaps));
	if (h2_cache_block(cache, definitions + 0xC, 4, &constant_count, &constants) && constant_count > 0)
		colour = h2_cache_read32(cache, constants) & 0xFFFFFFUL;
	if (numbers == NONE || !h2_cache_tag_get(cache, numbers, &t) ||
		!h2_cache_block(cache, t.address + 0x44, IMAGE_SIZE(build), &count, &images) || count < 10)
	{
		return NONE;
	}
	for (p = 0; p < 2; p++)
	{
		for (d = 0; d < 10; d++)
		{
			unsigned char const *header = h2_cache_at(cache, images + IMAGE_SIZE(build) * d, IMAGE_SIZE(build));
			unsigned long w = header ? read16(header + 4) : 0, h = header ? read16(header + 6) : 0, got = 0, n;
			unsigned short format = header ? read16(header + 0xC) : 0;
			unsigned char *raw = header && w && h && w <= 256 && h <= 256 ?
				h2_cache_texture_read(cache, read32(header + IMAGE_REFERENCE(build)), read32(header + IMAGE_BYTES(build)), &got) : NULL;
			unsigned char *argb, *swizzled;
			char path[64];
			struct shader *sh;
			long b;

			n = w * h;
			argb = raw ? malloc(n * 4) : NULL;
			swizzled = raw ? malloc(n * 4) : NULL;
			if (!argb || !swizzled)
			{
				free(raw);
				free(argb);
				free(swizzled);
				return NONE;
			}
			if (format == 14 && got >= n / 2)
				dxt1_decode(raw, w, h, argb);
			else if ((format == 10 || format == 11) && got >= n * 4)
				memcpy(argb, raw, n * 4);
			else
				memset(argb, 0, n * 4);
			free(raw);
			/* (its brightness the glow's, times the colour) */
			for (k = 0; k < (long)n; k++)
			{
				unsigned char *q = argb + 4 * k;
				unsigned long light = (q[0] + 2UL * q[1] + q[2]) / 4;

				q[0] = (unsigned char)((colour & 0xFF) * light / 255);
				q[1] = (unsigned char)(((colour >> 8) & 0xFF) * light / 255);
				q[2] = (unsigned char)(((colour >> 16) & 0xFF) * light / 255);
				q[3] = 255;
			}
			swizzle_into(swizzled, argb, w, h, 4);
			free(argb);
			snprintf(path, sizeof(path), "h2\\bitmaps\\digits %s %ld", p ? "ones" : "tens", d);
			b = bitmap_made(build, path, (unsigned short)w, (unsigned short)h, 11, 0, 0, swizzled, n * 4);
			if (b == NONE)
				return NONE;
			sh = &build->shaders[build->shader_count];
			memset(sh, 0, sizeof(*sh));
			sh->h2_tag = NONE;
			sh->bitmap = b;
			sh->alpha_bitmap = sh->ripple_bitmap = sh->reflection_bitmap = NONE;
			sh->group = 'schi';
			sh->blend = 3;          /* added */
			sh->rendered = TRUE;
			snprintf(sh->path, sizeof(sh->path), "h2\\shaders\\digits %s %ld", p ? "ones" : "tens", d);
			if (build->h2w_digit_first == NONE)
				build->h2w_digit_first = (short)build->shader_count;
			build->shader_count++;
		}
	}
	return (short)(build->h2w_digit_first + 10 * place);
}

/* a Halo 2 meter (meter_active_camo: the plasma rifle's heat gauge, the
plasma pistol's, the carbine's rounds display) made this engine's
shader_transparent_meter: its map's alpha the meter map's gradient inside
its alpha map's shape (0 outside: not drawn), its colour the shader's
brightest pixel constant (the masks 00FF00 and FF0000 left out), its value
the weapon's (mode: weapon_export_function_mode - the carbine's rounds,
the others' heat), read live; NONE if it cannot be (HALO_H2_NO_METERS:
none - not drawn, as at rest) */
H2_ALIGN static short h2w_meter_shader_made(struct build *build, long shader_tag, char const *name);

/* (piece J: its bitmaps made again alone at load - derived_recipe) */
H2_ALIGN static short h2w_meter_shader(struct build *build, long shader_tag, char const *name)
{
	struct derived_recipe saved = build->making;
	short made;

	memset(&build->making, 0, sizeof(build->making));
	build->making.kind_plus1 = _derived_recipe_weapon_meter + 1;
	build->making.args[0] = (unsigned long)shader_tag;
	made = h2w_meter_shader_made(build, shader_tag, name);
	build->making = saved;
	return made;
}

H2_ALIGN static short h2w_meter_shader_made(struct build *build, long shader_tag, char const *name)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	long definition_count, bitmap_count, constant_count, k, meter = NONE, shape = NONE;
	unsigned long definitions, bitmaps, constants, best = 0, best_sum = 0;
	unsigned short w = 0, h = 0, sw = 0, sh_ = 0;
	unsigned char *gradient, *mask, *argb, *pixels;
	struct shader *sh;
	char path[128];
	long b;
	boolean gradient_alpha = FALSE, mask_alpha = FALSE;
	/* (made once a build: the build's own list, not statics - the derive workers make them on builds of their own at
	once, and a static list kept every build's entries: past its few, later levels made none) */
	struct h2w_made_list *list = &build->h2w_made[1];

	/* (one a shader: the first-person and world models share it) */
	for (k = 0; k < list->made_count; k++)
	{
		/* (and still this build's: its shader there, named so - a build
		made at the same address keeps no stale index) */
		if (list->made[k].shader_tag == shader_tag &&
			list->made[k].shader < build->shader_count && build->shaders[list->made[k].shader].group == 'smet' &&
			strstr(build->shaders[list->made[k].shader].path, tag_short_name(h2_cache_tag_name(cache, shader_tag))))
		{
			return list->made[k].shader;
		}
	}
	if (getenv("HALO_H2_NO_METERS") || build->shader_count >= BUILD_SHADER_ROOM(build) || !h2_cache_tag_get(cache, shader_tag, &t) ||
		!h2_cache_block(cache, t.address + 0x20, 0x7C, &definition_count, &definitions) || definition_count < 1 ||
		!h2_cache_block(cache, definitions + 4, 0xC, &bitmap_count, &bitmaps) || bitmap_count < 2)
	{
		return NONE;
	}
	for (k = 0; k < bitmap_count && k < 2; k++)
	{
		long candidate = h2_cache_tag_from_handle(cache, h2_cache_read32(cache, bitmaps + 0xC * k));
		char const *n = candidate != NONE ? h2_cache_tag_name(cache, candidate) : "";
		size_t length = strlen(n);

		if (candidate == NONE)
			continue;
		if ((length > 6 && !strcmp(n + length - 6, "_alpha")) || (shape == NONE && meter != NONE && strstr(h2_cache_tag_name(cache, meter), "_illum")))
			shape = candidate;
		else if (meter == NONE && !(length > 6 && !strcmp(n + length - 6, "_alpha")))
			meter = candidate;
		else
			shape = candidate;
	}
	if (meter == NONE || shape == NONE)
		return NONE;
	if (h2_cache_block(cache, definitions + 0xC, 4, &constant_count, &constants))
	{
		for (k = 0; k < constant_count; k++)
		{
			unsigned long colour = h2_cache_read32(cache, constants + 4 * k) & 0xFFFFFFUL;
			unsigned long sum = ((colour >> 16) & 255) + ((colour >> 8) & 255) + (colour & 255);

			if (colour != 0x00FF00UL && colour != 0xFF0000UL && sum > best_sum)
				best = colour, best_sum = sum;
		}
	}
	if (!best_sum)
		best = 0xFFFFFFUL;
	gradient = image_decoded_alpha(build, meter, &w, &h);
	mask = image_decoded_alpha(build, shape, &sw, &sh_);
	if (!gradient || !mask || !w || !h || !sw || !sh_ || !(argb = malloc((unsigned long)w * h * 4)))
	{
		free(gradient);
		free(mask);
		return NONE;
	}
	/* (each map's alpha when it varies, else its brightness) */
	for (k = 1; k < (long)w * h && !gradient_alpha; k++)
		gradient_alpha = gradient[4 * k + 3] != gradient[3];
	for (k = 1; k < (long)sw * sh_ && !mask_alpha; k++)
		mask_alpha = mask[4 * k + 3] != mask[3];
	for (k = 0; k < (long)w * h; k++)
	{
		long x = k % w, y = k / w;
		unsigned char const *g = gradient + 4 * k, *m = mask + 4 * ((y * sh_ / h) * sw + x * sw / w);
		int level = gradient_alpha ? g[3] : (g[0] > g[1] ? (g[0] > g[2] ? g[0] : g[2]) : (g[1] > g[2] ? g[1] : g[2]));
		int inside = mask_alpha ? m[3] : (m[0] > m[1] ? (m[0] > m[2] ? m[0] : m[2]) : (m[1] > m[2] ? m[1] : m[2]));

		/* (its colour the meter's - B G R -, its alpha the gradient; 0
		outside: never shown) */
		argb[4 * k + 0] = (unsigned char)(best & 255);
		argb[4 * k + 1] = (unsigned char)((best >> 8) & 255);
		argb[4 * k + 2] = (unsigned char)((best >> 16) & 255);
		argb[4 * k + 3] = inside < 64 ? 255 : (unsigned char)(level < 1 ? 1 : level > 254 ? 254 : level);
		if (inside < 64)
			argb[4 * k + 0] = argb[4 * k + 1] = argb[4 * k + 2] = 0;
	}
	if (getenv("HALO_H2_DEBUG"))
	{
		long shown = 0, lo = 255, hi = 0;

		for (k = 0; k < (long)w * h; k++)
		{
			if (argb[4 * k + 0] | argb[4 * k + 1] | argb[4 * k + 2])
			{
				shown++;
				lo = argb[4 * k + 3] < lo ? argb[4 * k + 3] : lo;
				hi = argb[4 * k + 3] > hi ? argb[4 * k + 3] : hi;
			}
		}
		platform_log("h2 weapons: meter %s: %ld of %ld texels inside, gradient %ld..%ld", name, shown, (long)w * h, lo, hi);
	}
	free(gradient);
	free(mask);
	if (!(pixels = malloc((unsigned long)w * h * 4)))
	{
		free(argb);
		return NONE;
	}
	swizzle_into(pixels, argb, w, h, 4);
	free(argb);
	snprintf(path, sizeof(path), "h2\\bitmaps\\%s meter", tag_short_name(h2_cache_tag_name(cache, shader_tag)));
	b = bitmap_made(build, path, w, h, 11, 0, 0, pixels, (unsigned long)w * h * 4);
	if (b == NONE)
		return NONE;
	sh = &build->shaders[build->shader_count];
	memset(sh, 0, sizeof(*sh));
	sh->h2_tag = NONE;
	sh->bitmap = b;
	sh->alpha_bitmap = sh->ripple_bitmap = sh->reflection_bitmap = NONE;
	sh->detail_bitmap[0] = sh->detail_bitmap[1] = sh->bump_bitmap = NONE;
	sh->cube_bitmap = sh->illum_bitmap = sh->multipurpose_bitmap = sh->specular_multipurpose = NONE;
	sh->group = 'smet';
	sh->rendered = TRUE;
	sh->meter_colour[0] = ((best >> 16) & 255) / 255.0f;
	sh->meter_colour[1] = ((best >> 8) & 255) / 255.0f;
	sh->meter_colour[2] = (best & 255) / 255.0f;
	sh->meter_mode = strstr(name, "carbine") || strstr(name, "display") ? 2 : 1;   /* primary ammunition, else heat */
	snprintf(sh->path, sizeof(sh->path), "h2\\shaders\\%s meter", tag_short_name(h2_cache_tag_name(cache, shader_tag)));
	if (getenv("HALO_H2_DEBUG"))
		platform_log("h2 weapons: %s: a meter of %s inside %s (%s, %s), %ux%u, colour %06lx, mode %d", h2_cache_tag_name(cache, shader_tag),
			h2_cache_tag_name(cache, meter), h2_cache_tag_name(cache, shape), gradient_alpha ? "alpha" : "brightness",
			mask_alpha ? "alpha" : "brightness", w, h, best, sh->meter_mode);
	if (list->made_count < 32)
	{
			list->made[list->made_count].shader_tag = shader_tag;
		list->made[list->made_count++].shader = (short)build->shader_count;
	}
	return (short)build->shader_count++;
}

/* a Halo 2 plasma shader's glow made from its mask (the shader's first
bitmap named "mask": its brightness the glow's, times the colour - kind 0
light blue, 1 the needler's pink, 2 a sword's blade, blue to white): a
32-bit bitmap and an added shader_transparent_chicago; NONE if it cannot be
(then the flat glow) */
/* whether a plasma shader's first map is only noise - a default bitmap or
a clouds_ map (its postprocess definition's bitmaps, 0x20 then 4, 0xC
each); HALO_H2_PLASMA_NOISE_CARD: never */
H2_ALIGN static boolean h2w_plasma_noise_only(struct h2_cache *cache, long shader_tag)
{
	struct h2_cache_tag t;
	long definition_count, bitmap_count, map;
	unsigned long definitions, bitmaps;
	char const *name;

	if (getenv("HALO_H2_PLASMA_NOISE_CARD") || !h2_cache_tag_get(cache, shader_tag, &t) ||
		!h2_cache_block(cache, t.address + 0x20, 0x7C, &definition_count, &definitions) || definition_count < 1 ||
		!h2_cache_block(cache, definitions + 4, 0xC, &bitmap_count, &bitmaps) || bitmap_count < 1 ||
		(map = h2_cache_tag_from_handle(cache, h2_cache_read32(cache, bitmaps))) == NONE)
	{
		return FALSE;
	}
	name = h2_cache_tag_name(cache, map);
	return strstr(name, "default_bitmaps\\") != NULL || strstr(name, "\\clouds_") != NULL;
}

H2_ALIGN static short h2w_plasma_masked_made(struct build *build, long shader_tag, int kind);

/* (piece J: its bitmaps made again alone at load - derived_recipe) */
H2_ALIGN static short h2w_plasma_masked(struct build *build, long shader_tag, int kind)
{
	struct derived_recipe saved = build->making;
	short made;

	memset(&build->making, 0, sizeof(build->making));
	build->making.kind_plus1 = _derived_recipe_weapon_masked + 1;
	build->making.args[0] = (unsigned long)shader_tag;
	build->making.args[1] = (unsigned long)kind;
	made = h2w_plasma_masked_made(build, shader_tag, kind);
	build->making = saved;
	return made;
}

H2_ALIGN static short h2w_plasma_masked_made(struct build *build, long shader_tag, int kind)
{
	/* (made once a build: the build's own list, not statics - the derive workers make them on builds of their own at
	once, and a static list kept every build's entries: past its few, later levels made none) */
	struct h2w_made_list *list = &build->h2w_made[2];
	static unsigned char const colours[2][3] = { { 110, 190, 255 }, { 210, 70, 255 } };   /* R G B */
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	long definition_count, bitmap_count, k, mask = NONE, b;
	unsigned long definitions, bitmaps, size = 0;
	unsigned short w = 0, h = 0, format = 0;
	unsigned char *raw, *rgba, *pixels;
	struct shader *sh;
	char path[96];
	unsigned long blade_colours[2] = { 0x5BFFFFUL, 0x15061AUL };
	unsigned char *veins = NULL;
	/* (a current - the plasma rifle's and pistol's coil, plasma_alpha_active_camo:
	no mask, its noise map the current's shape, its first pixel constant
	its colour - the rifle's cyan, the pistol's green; drawn flat it was a
	solid cyan sheet) */
	boolean current = FALSE;
	unsigned long current_colour = 0x9EE7E7UL;
	unsigned long current_colours[4] = { 0x9EE7E7UL, 0x00409FUL, 0, 0xE1FFFFUL };

	for (k = 0; k < list->made_count; k++)
	{
		if (list->made[k].shader_tag == shader_tag)
			return list->made[k].shader;
	}
	if (list->made_count >= 16 || build->shader_count >= BUILD_SHADER_ROOM(build) || !h2_cache_tag_get(cache, shader_tag, &t) ||
		!h2_cache_block(cache, t.address + 0x20, 0x7C, &definition_count, &definitions) || definition_count < 1 ||
		!h2_cache_block(cache, definitions + 4, 0xC, &bitmap_count, &bitmaps))
	{
		return NONE;
	}
	for (k = 0; k < bitmap_count && mask == NONE; k++)
	{
		long candidate = h2_cache_tag_from_handle(cache, h2_cache_read32(cache, bitmaps + 0xC * k));

		if (candidate != NONE && strstr(h2_cache_tag_name(cache, candidate), "mask"))
			mask = candidate;
	}
	if (mask == NONE && kind != 2 && !getenv("HALO_H2_PLASMA_CURRENT_FLAT"))
	{
		for (k = 0; k < bitmap_count && mask == NONE; k++)
		{
			long candidate = h2_cache_tag_from_handle(cache, h2_cache_read32(cache, bitmaps + 0xC * k));

			/* (its first map a current's noise - not the Phantom antigrav's
			clouds, h2w_plasma_noise_only) */
			if (k == 0 && candidate != NONE && strstr(h2_cache_tag_name(cache, candidate), "\\plasma_current"))
			{
				long constant_count;
				unsigned long constants;

				mask = candidate;
				current = TRUE;
				if (h2_cache_block(cache, definitions + 0xC, 4, &constant_count, &constants) && constant_count >= 1)
				{
					long c;

					current_colour = h2_cache_read32(cache, constants) & 0xFFFFFFUL;
					for (c = 0; c < 4 && c < constant_count; c++)
						current_colours[c] = h2_cache_read32(cache, constants + 4 * c) & 0xFFFFFFUL;
				}
			}
		}
	}
	if (mask == NONE || !(raw = image_pixels(build, mask, &w, &h, &format, &size)))
		return NONE;
	if (!w || !h || w > 256 || h > 256 || (w & 3) || (h & 3) || !(rgba = malloc((unsigned long)w * h * 4)))
	{
		free(raw);
		return NONE;
	}
	if (format == 14 && size >= (unsigned long)w * h / 2)
		dxt1_decode(raw, w, h, rgba);
	else if ((format == 15 || format == 16) && size >= (unsigned long)w * h)
	{
		/* (DXT3/5: their colour halves as a DXT1 image) */
		unsigned long blocks = ((unsigned long)w / 4) * (h / 4), n;
		unsigned char *colour = malloc(blocks * 8);

		if (colour)
		{
			for (n = 0; n < blocks; n++)
				memcpy(colour + 8 * n, raw + 16 * n + 8, 8);
			dxt1_decode(colour, w, h, rgba);
			free(colour);
		}
	}
	else if ((format == 10 || format == 11) && size >= (unsigned long)w * h * 4)
		memcpy(rgba, raw, (unsigned long)w * h * 4);
	else
	{
		free(raw);
		free(rgba);
		return NONE;
	}
	free(raw);
	/* (the sword's: its third and fourth pixel constants, the plasma's
	colour and the colour under it) */
	if (kind == 2)
	{
		long constant_count;
		unsigned long constants;

		if (h2_cache_block(cache, definitions + 0xC, 4, &constant_count, &constants) && constant_count >= 4)
		{
			blade_colours[0] = h2_cache_read32(cache, constants + 8) & 0xFFFFFFUL;
			blade_colours[1] = h2_cache_read32(cache, constants + 12) & 0xFFFFFFUL;
		}
	}
	pixels = rgba;
	/* (the sword's veins: its offset map's dark lines, at the mask's size) */
	if (kind == 2 && (veins = malloc((unsigned long)w * h)) != NULL)
	{
		long offset = NONE, x, y;
		unsigned short ow = 0, oh = 0;
		unsigned char *o;

		for (k = 0; k < bitmap_count && offset == NONE; k++)
		{
			long candidate = h2_cache_tag_from_handle(cache, h2_cache_read32(cache, bitmaps + 0xC * k));

			if (candidate != NONE && strstr(h2_cache_tag_name(cache, candidate), "offset"))
				offset = candidate;
		}
		o = offset != NONE ? image_decoded(build, offset, &ow, &oh) : NULL;
		for (y = 0; y < h; y++)
		{
			for (x = 0; x < w; x++)
			{
				unsigned char const *q = o ? o + 4 * ((y * oh / h) * ow + x * ow / w) : NULL;
				float l = q ? (q[0] + q[1] + q[2]) / (3.0f * 255.0f) : 0.5f, v = 1.0f - l / 0.3f;

				veins[y * w + x] = (unsigned char)(v <= 0.0f ? 0 : v >= 1.0f ? 255 : (int)(v * 255.0f));
			}
		}
		free(o);
	}
	for (k = 0; k < (long)w * h; k++)
	{
		unsigned char *p = pixels + 4 * k;   /* B G R A */
		int light = p[0] > p[1] ? p[0] : p[1];

		light = p[2] > light ? p[2] : light;
		if (kind == 2)
		{
			/* (a sword's blade - plasma_mask_offset - where the pass itself
			cannot be drawn (h2_level.c plasma_mask_offset_fill: the PC's
			and the Vita's renderers draw it; this is only its fallback): the
			pass's colour as it is with its plasma full on - the mask's
			colour times the tint, plus the glow where the mask's alpha is
			not (Halo 2's "dull and colorize": tint 5bffff, glow 15061a) -
			lightened along the offset map's veins, alpha blended by the
			mask's body. No colour of its own: the old stand-in's
			pink-magenta edge (255,110,225) over a deep blue read purple) */
			float body = p[2] / 255.0f, edge = p[1] / 255.0f;
			float vein = veins ? veins[k] / 255.0f : 0.0f;
			float rgb[3], a, mask_a = p[3] / 255.0f;
			int c;

			for (c = 0; c < 3; c++)
			{
				float tint = (float)((blade_colours[0] >> (16 - 8 * c)) & 255);
				float glow = (float)((blade_colours[1] >> (16 - 8 * c)) & 255);

				rgb[c] = (p[2 - c] / 255.0f) * tint + (1.0f - mask_a) * glow;
				rgb[c] += (255.0f - rgb[c]) * 0.5f * vein;
			}
			a = body * 1.3f + edge * edge;
			a = a > 1.0f ? 1.0f : a;
			for (c = 0; c < 3; c++)
				p[2 - c] = (unsigned char)(rgb[c] > 255.0f ? 255.0f : rgb[c]);
			p[3] = (unsigned char)(a * 255.0f);
			continue;
		}
		if (current)
		{
			/* (the current: Halo 2's plasma - bands about its noise's
			middle, where its two scrolling noises meet: a sharp core near
			white (the fourth constant), a medium band its first colour, a
			wide one its second, dark blue; nothing between the arcs - drawn
			as its noise's light it was a broad pale sheet, the Xbox's coil
			thin bright arcs) */
			float d = (float)light / 255.0f, sharp, medium, wide, rgb[3];
			int c;

			d = d > 0.5f ? (d - 0.5f) * 2.0f : (0.5f - d) * 2.0f;
			sharp = d < 0.06f ? 1.0f - d / 0.06f : 0.0f;
			medium = d < 0.18f ? 1.0f - d / 0.18f : 0.0f;
			wide = d < 0.40f ? 1.0f - d / 0.40f : 0.0f;
			for (c = 0; c < 3; c++)
			{
				int shift = 16 - 8 * c;

				rgb[c] = ((current_colours[3] >> shift) & 255) * sharp + ((current_colours[0] >> shift) & 255) * medium * 0.8f +
					((current_colours[1] >> shift) & 255) * wide * 0.6f;
				rgb[c] = rgb[c] > 255.0f ? 255.0f : rgb[c];
			}
			if (getenv("HALO_H2_PLASMA_CURRENT_SHEET"))
			{
				for (c = 0; c < 3; c++)
					rgb[c] = ((current_colour >> (16 - 8 * c)) & 255) * light / 255.0f;
			}
			p[0] = (unsigned char)rgb[2];
			p[1] = (unsigned char)rgb[1];
			p[2] = (unsigned char)rgb[0];
			p[3] = 255;
			continue;
		}
		/* (a third of the colour throughout, the mask's light over it: the
		crystals lit as Halo 2's are, their tips brighter) */
		light = 90 + light * (255 - 90) / 255;
		p[0] = (unsigned char)(colours[kind][2] * light / 255);
		p[1] = (unsigned char)(colours[kind][1] * light / 255);
		p[2] = (unsigned char)(colours[kind][0] * light / 255);
		p[3] = 255;
	}
	/* (swizzled, as the cache file has a 32-bit map: decoded in rows, the
	mask's shape came out in stripes - the sword's blade a striped card) */
	free(veins);
	if (!(pixels = malloc((unsigned long)w * h * 4)))
	{
		free(rgba);
		return NONE;
	}
	swizzle_into(pixels, rgba, w, h, 4);
	free(rgba);
	if (current)
		snprintf(path, sizeof(path), "h2\\bitmaps\\%s glow %06lx", tag_short_name(h2_cache_tag_name(cache, mask)), current_colour);
	else
		snprintf(path, sizeof(path), "h2\\bitmaps\\%s glow", tag_short_name(h2_cache_tag_name(cache, mask)));
	b = bitmap_made(build, path, w, h, 11, 0, 0, pixels, (unsigned long)w * h * 4);
	if (b == NONE)
		return NONE;
	sh = &build->shaders[build->shader_count];
	memset(sh, 0, sizeof(*sh));
	sh->h2_tag = NONE;
	sh->bitmap = b;
	sh->alpha_bitmap = sh->ripple_bitmap = sh->reflection_bitmap = NONE;
	sh->group = 'schi';
	sh->blend = kind == 2 ? 0 : 3;   /* added (the sword's blade: blended by its alpha) */
	sh->rendered = TRUE;
	/* (the current flows, as Halo 2's noise moves) */
	if (current)
		sh->scroll_u = 1.5f;
	snprintf(sh->path, sizeof(sh->path), "h2\\shaders\\%s glow", tag_short_name(h2_cache_tag_name(cache, shader_tag)));
	list->made[list->made_count].shader_tag = shader_tag;
	list->made[list->made_count].shader = (short)build->shader_count++;
	return list->made[list->made_count++].shader;
}

/* Halo 2's crystal shell (the needler's needles: one_alpha_env - a
reflection cube, a specular mask and a colour map that Halo 2 draws glassy,
each facet its own shade, the edges brightest) as a model shader, lit: a
magenta base (Halo 2's needles as the owner's MCC frames show them, its own
colour map a dark gradient for its blend), part of it glowing (the
multipurpose map's green: the side away from the light still pink, the lit
side paler), the cube over every facet (red: the reflection's mask) faint
looked into and bright along the facets - the crystal's edges; NONE if it
cannot be made */
H2_ALIGN static short h2w_crystal_shader(struct build *build, long shader_tag)
{
	/* (made once a build: the build's own list, not statics - the derive workers make them on builds of their own at
	once, and a static list kept every build's entries: past its few, later levels made none) */
	struct h2w_made_list *list = &build->h2w_made[3];
	static float const glow[3] = { 0.9f, 0.25f, 1.0f };            /* R G B: magenta, a little blue */
	static unsigned char const colour[3] = { 219, 92, 242 };      /* R G B */
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	long definition_count, bitmap_count, k, cube = NONE, base, multipurpose;
	unsigned long definitions, bitmaps;
	unsigned char *pixels;
	struct shader *sh;
	int c;

	for (k = 0; k < list->made_count; k++)
	{
		if (list->made[k].shader_tag == shader_tag)
			return list->made[k].shader;
	}
	if (list->made_count >= 8 || build->shader_count >= BUILD_SHADER_ROOM(build) || !h2_cache_tag_get(cache, shader_tag, &t) ||
		!h2_cache_block(cache, t.address + 0x20, 0x7C, &definition_count, &definitions) || definition_count < 1 ||
		!h2_cache_block(cache, definitions + 4, 0xC, &bitmap_count, &bitmaps))
	{
		return NONE;
	}
	/* (its cube) */
	for (k = 0; k < bitmap_count && cube == NONE; k++)
	{
		long b = h2_cache_tag_from_handle(cache, h2_cache_read32(cache, bitmaps + 0xC * k));

		if (b != NONE && !strstr(h2_cache_tag_name(cache, b), "\\default_") && bitmap_tag_is_cube(build, b))
			cube = b;
	}
	/* (its base and multipurpose maps: one colour each, small; B G R A) */
	if (!(pixels = malloc(8 * 8 * 4)))
		return NONE;
	for (k = 0; k < 64; k++)
	{
		pixels[4 * k] = colour[2];
		pixels[4 * k + 1] = colour[1];
		pixels[4 * k + 2] = colour[0];
		pixels[4 * k + 3] = 255;
	}
	if ((base = bitmap_made(build, "h2\\bitmaps\\crystal colour", 8, 8, 11, 0, 0, pixels, 8 * 8 * 4)) == NONE ||
		!(pixels = malloc(8 * 8 * 4)))
	{
		return NONE;
	}
	for (k = 0; k < 64; k++)
	{
		pixels[4 * k] = 0;          /* B: no change colour */
		pixels[4 * k + 1] = 110;    /* G: the glow */
		pixels[4 * k + 2] = 255;    /* R: the reflection's mask */
		pixels[4 * k + 3] = 0;      /* A: auxiliary */
	}
	if ((multipurpose = bitmap_made(build, "h2\\bitmaps\\crystal multipurpose", 8, 8, 11, 0, 0, pixels, 8 * 8 * 4)) == NONE)
		return NONE;
	sh = &build->shaders[build->shader_count];
	memset(sh, 0, sizeof(*sh));
	sh->h2_tag = NONE;
	sh->group = 'senv';
	sh->rendered = TRUE;
	sh->bitmap = base;
	sh->alpha_bitmap = sh->ripple_bitmap = sh->reflection_bitmap = NONE;
	sh->detail_bitmap[0] = sh->detail_bitmap[1] = sh->bump_bitmap = NONE;
	sh->cube_bitmap = sh->illum_bitmap = NONE;
	sh->specular_multipurpose = NONE;
	sh->multipurpose_bitmap = multipurpose;
	if (cube != NONE)
	{
		long made_cube = bitmap_for(build, cube);

		if (made_cube != NONE && build->bitmaps[made_cube].type == 2)
		{
			sh->cube_bitmap = made_cube;
			for (c = 0; c < 3; c++)
			{
				sh->cube_tint[0][c] = 0.9f + 0.1f * glow[c];    /* along the surface: the edges */
				sh->cube_tint[1][c] = 0.05f * glow[c];          /* looked into */
			}
		}
	}
	for (c = 0; c < 3; c++)
		sh->illum_color[c] = glow[c];
	snprintf(sh->path, sizeof(sh->path), "h2\\shaders\\%s crystal", tag_short_name(h2_cache_tag_name(cache, shader_tag)));
	list->made[list->made_count].shader_tag = shader_tag;
	list->made[list->made_count].shader = (short)build->shader_count++;
	return list->made[list->made_count++].shader;
}

/* Halo 2's first-person crystals (the needler's needles, one_alpha_env:
"one alpha" - their colour added, what is behind them dimmed by their
alpha, a half) as a transparent shader of their own colour map (a dark root
to a magenta tip, its alpha the half) blended so - this engine's alpha
multiply add (one, one less the alpha): the crystals see-through as the
owner's MCC frames show them, the gun and the world behind them; NONE if
it cannot be (then the lit crystal) */
H2_ALIGN static short h2w_crystal_transparent(struct build *build, long shader_tag)
{
	/* (made once a build: the build's own list, not statics - the derive workers make them on builds of their own at
	once, and a static list kept every build's entries: past its few, later levels made none) */
	struct h2w_made_list *list = &build->h2w_made[4];
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	long definition_count, bitmap_count, k, colour = NONE, b;
	unsigned long definitions, bitmaps;
	struct shader *sh;

	for (k = 0; k < list->made_count; k++)
	{
		if (list->made[k].shader_tag == shader_tag)
			return list->made[k].shader;
	}
	if (getenv("HALO_H2_OPAQUE_CRYSTALS") || list->made_count >= 8 || build->shader_count >= BUILD_SHADER_ROOM(build) ||
		!h2_cache_tag_get(cache, shader_tag, &t) ||
		!h2_cache_block(cache, t.address + 0x20, 0x7C, &definition_count, &definitions) || definition_count < 1 ||
		!h2_cache_block(cache, definitions + 4, 0xC, &bitmap_count, &bitmaps))
	{
		return NONE;
	}
	/* (its colour map: not its reflection's cube, its specular mask or a
	default) */
	for (k = 0; k < bitmap_count && colour == NONE; k++)
	{
		long candidate = h2_cache_tag_from_handle(cache, h2_cache_read32(cache, bitmaps + 0xC * k));
		char const *name = candidate != NONE ? h2_cache_tag_name(cache, candidate) : "";

		if (candidate != NONE && !strstr(name, "default_") && !strstr(name, "reflection_maps") && !strstr(name, "specular") &&
			!strstr(name, "mask") && !bitmap_tag_is_cube(build, candidate))
		{
			colour = candidate;
		}
	}
	if (colour == NONE || (b = bitmap_for(build, colour)) == NONE)
		return NONE;
	sh = &build->shaders[build->shader_count];
	memset(sh, 0, sizeof(*sh));
	sh->h2_tag = NONE;
	sh->bitmap = b;
	sh->alpha_bitmap = sh->ripple_bitmap = sh->reflection_bitmap = NONE;
	sh->group = 'schi';
	sh->blend = 7;          /* alpha multiply add: one, one less the alpha */
	sh->rendered = TRUE;
	snprintf(sh->path, sizeof(sh->path), "h2\\shaders\\%s glass", tag_short_name(h2_cache_tag_name(cache, shader_tag)));
	list->made[list->made_count].shader_tag = shader_tag;
	list->made[list->made_count].shader = (short)build->shader_count++;
	return list->made[list->made_count++].shader;
}

/* the model's node of that name, NONE if none */
H2_ALIGN static short h2w_model_node(struct h2w_model const *m, char const *name)
{
	long i;

	for (i = 0; i < m->node_count; i++)
	{
		if (!strcmp(m->nodes[i].name, name))
			return (short)i;
	}
	return NONE;
}

/* a section's parts added (the render model's node numbers mapped to the
model's by node_map), compressed model vertices; each vertex's two nodes
and weight kept after the part's vertices (4 bytes each) until
h2w_model_finish puts them in place */
H2_ALIGN static void h2w_model_add_section(struct h2w_model *m, struct h2w_section const *s,
	short const *materials, long material_count, short const *node_map, long node_map_count)
{
	long p, n;
	long *remap = malloc((s->vertex_count + 1) * sizeof(long));
	struct model_made *model = &m->made;

	if (!remap)
		return;
	for (p = 0; p < s->part_count; p++)
	{
		short shader = s->parts[p].material >= 0 && s->parts[p].material < material_count ? materials[s->parts[p].material] : NONE;
		long first = s->parts[p].first, count = s->parts[p].count - s->parts[p].count % 3, t, used = 0;
		struct model_part_made *part, *grown;
		unsigned short *strip;
		double centroid[3] = { 0.0, 0.0, 0.0 };

		if (shader == NONE || count < 3)
			continue;
		grown = realloc(model->parts, (model->part_count + 1) * sizeof(*grown));
		if (!grown)
			break;
		model->parts = grown;
		for (n = 0; n < s->vertex_count; n++)
			remap[n] = -1;
		for (t = 0; t < count; t++)
		{
			long v = s->indices[first + t];

			if (remap[v] < 0)
				remap[v] = used++;
		}
		part = &model->parts[model->part_count];
		memset(part, 0, sizeof(*part));
		part->shader = shader;
		part->vertices = malloc((unsigned long)used * 36);
		strip = malloc((unsigned long)count * 2 * sizeof(unsigned short) + 8);
		if (!part->vertices || !strip)
		{
			free(part->vertices);
			free(strip);
			continue;
		}
		for (n = 0; n < s->vertex_count; n++)
		{
			unsigned char *out, *side;
			float normal[3], binormal[3], tangent[3];
			int k, best[2] = { -1, -1 };
			short node0, node1;
			float w0, w1, weight;

			if (remap[n] < 0)
				continue;
			out = part->vertices + 32 * remap[n];
			side = part->vertices + (unsigned long)used * 32 + 4 * remap[n];
			for (k = 0; k < 3; k++)
			{
				float v = s->positions[3 * n + k];

				writef(out + 4 * k, v);
				centroid[k] += v;
				if (v < model->extent[2 * k])
					model->extent[2 * k] = v;
				if (v > model->extent[2 * k + 1])
					model->extent[2 * k + 1] = v;
			}
			{
				float d = sqrtf(s->positions[3 * n] * s->positions[3 * n] + s->positions[3 * n + 1] * s->positions[3 * n + 1] +
					s->positions[3 * n + 2] * s->positions[3 * n + 2]);

				if (d > model->radius)
					model->radius = d;
			}
			unpack_vector(s->packed[3 * n], normal);
			unpack_vector(s->packed[3 * n + 1], binormal);
			unpack_vector(s->packed[3 * n + 2], tangent);
			write32(out + 12, compress_model_vector(normal));
			write32(out + 16, compress_model_vector(binormal));
			write32(out + 20, compress_model_vector(tangent));
			writef(out + 24, s->coordinates[2 * n]);
			writef(out + 28, s->coordinates[2 * n + 1]);
			/* its two heaviest nodes (this engine skins two) */
			for (k = 0; k < 4; k++)
			{
				unsigned char w = s->weights[4 * n + k];

				if (!w)
					continue;
				if (best[0] < 0 || w > s->weights[4 * n + best[0]])
					best[1] = best[0], best[0] = k;
				else if (best[1] < 0 || w > s->weights[4 * n + best[1]])
					best[1] = k;
			}
			node0 = best[0] >= 0 && s->nodes[4 * n + best[0]] < node_map_count ? node_map[s->nodes[4 * n + best[0]]] : 0;
			node1 = best[1] >= 0 && s->nodes[4 * n + best[1]] < node_map_count ? node_map[s->nodes[4 * n + best[1]]] : node0;
			w0 = best[0] >= 0 ? (float)s->weights[4 * n + best[0]] : 1.0f;
			w1 = best[1] >= 0 ? (float)s->weights[4 * n + best[1]] : 0.0f;
			if (node0 < 0)
				node0 = 0;
			if (node1 < 0)
				node1 = node0;
			weight = w0 + w1 > 0.0f ? w0 / (w0 + w1) : 1.0f;
			side[0] = (unsigned char)node0;
			side[1] = (unsigned char)node1;
			write16(side + 2, (long)(weight * 32767.0f + 0.5f));
		}
		{
			long indices = 0;

			for (t = 0; t + 2 < count; t += 3)
			{
				strip_triangle(strip, &indices, (unsigned short)remap[s->indices[first + t]],
					(unsigned short)remap[s->indices[first + t + 1]], (unsigned short)remap[s->indices[first + t + 2]]);
			}
			part->strip = (unsigned char *)strip;
			part->strip_bytes = (unsigned long)indices * 2;
			part->triangle_count = indices - 2;
		}
		part->vertex_count = used;
		part->vertex_bytes = (unsigned long)used * 32;
		for (n = 0; n < 3; n++)
			part->centroid[n] = used ? (float)(centroid[n] / used) : 0.0f;
		model->part_count++;
	}
	free(remap);
}

/* (perf/h2-object-lods) a render model's section (section_index of its
sections) added, its vertices moved by offset: its finest (its bounds m's),
or a coarser level's (m's bounds kept its finest's); FALSE if unread */
struct h2w_section_adder
{
	struct build *build;
	struct h2w_model *m;
	unsigned long render_model, sections;
	short const *materials;
	long material_count;
	short const *node_map;
	long node_map_count;
	float const *offset;
};

H2_ALIGN static boolean h2w_model_add_section_at(struct h2w_section_adder const *a, short section_index, boolean finest)
{
	struct h2w_model *m = a->m;
	struct h2w_section s;
	float radius = m->made.radius, extent[6];

	if (!h2w_section_read(a->build, a->render_model, a->sections + 0x5C * section_index, &s))
		return FALSE;
	if (a->offset)
	{
		long v;

		for (v = 0; v < s.vertex_count; v++)
		{
			s.positions[3 * v] += a->offset[0];
			s.positions[3 * v + 1] += a->offset[1];
			s.positions[3 * v + 2] += a->offset[2];
		}
	}
	memcpy(extent, m->made.extent, sizeof(extent));
	h2w_model_add_section(m, &s, a->materials, a->material_count, a->node_map, a->node_map_count);
	h2w_section_free(&s);
	if (!finest)
	{
		m->made.radius = radius;
		memcpy(m->made.extent, extent, sizeof(extent));
	}
	return TRUE;
}

H2_ALIGN static boolean h2w_model_add_coarser(void *context, short section)
{
	return h2w_model_add_section_at(context, section, FALSE);
}

/* (perf/h2-object-lods) the sections of render model tag's permutation
(its address) m's levels draw (h2_level.c model_level_sections: level 4
the detail asked for; every level it without levels) */
H2_ALIGN static void h2w_level_sections(struct build *build, struct h2w_model *m, long tag, unsigned long permutation, short detail,
	long section_count, short sections[5])
{
	float reduce[5];
	boolean have = m->levels && model_reduce_distances(build->cache, tag, reduce);
	int c;

	model_level_sections(build->cache, permutation, detail, m->made.h2_reduce, reduce, have, sections);
	for (c = 0; c < 4; c++)
	{
		if (sections[c] < 0 || sections[c] >= section_count)
			sections[c] = sections[4];
	}
}

/* (a vehicle's model, m->regioned) the region of m's regions named name,
added (its first permutation "base") if new; NONE if there is no room */
H2_ALIGN static long h2w_model_region(struct h2w_model *m, char const *name)
{
	struct model_regions_made *regions = m->made.regions;
	long i;

	if (!regions && !(regions = m->made.regions = calloc(1, sizeof(*regions))))
		return NONE;
	for (i = 0; i < regions->count && strcmp(regions->names[i], name); i++)
		;
	if (i == regions->count)
	{
		if (i >= MODEL_MAXIMUM_REGIONS)
			return NONE;
		snprintf(regions->names[i], 32, "%s", name);
		snprintf(regions->permutation_names[i][0], 32, "base");
		regions->permutation_count[i] = 1;
		regions->count++;
	}
	return i;
}

/* (a vehicle's model) region r's permutation named name, added if new;
NONE if there is no room */
H2_ALIGN static long h2w_model_permutation(struct h2w_model *m, long r, char const *name)
{
	struct model_regions_made *regions = m->made.regions;
	long p;

	for (p = 0; p < regions->permutation_count[r] && strcmp(regions->permutation_names[r][p], name); p++)
		;
	if (p == regions->permutation_count[r])
	{
		if (p >= MODEL_MAXIMUM_PERMUTATIONS)
			return NONE;
		snprintf(regions->permutation_names[r][p], 32, "%s", name);
		regions->permutation_count[r]++;
	}
	return p;
}

/* (a vehicle's model) the render model's regions added as m's: each of
their permutations (Halo 2's damage states; not the blurred wheels'), a
section of that detail each, its parts that region's and permutation's;
the piece's (m->piece_region) all one region's, in each of its
permutations; and every region a permutation "gone" of nothing (a part
come off) */
H2_ALIGN static void h2w_model_add_regions(struct build *build, struct h2w_model *m, long tag, struct h2_cache_tag const *t,
	unsigned long regions, long region_count, unsigned long sections, long section_count, short const *shaders, long material_count,
	short const *node_map, long node_count, short detail, float const *offset)
{
	struct h2_cache *cache = build->cache;
	long i, j, e;
	/* (perf/h2-object-lods) each permutation added: its region and
	permutation, its sections at this engine's levels, its finest's parts */
	struct h2w_region_entry
	{
		long r, p;
		short sections[5];
		long begin, end;
	} *entries = NULL;
	long entry_count = 0, finest_from = m->made.part_count, finest_end;
	struct h2w_section_adder adder;

	adder.build = build;
	adder.m = m;
	adder.render_model = t->address;
	adder.sections = sections;
	adder.materials = shaders;
	adder.material_count = material_count;
	adder.node_map = node_map;
	adder.node_map_count = node_count < 256 ? node_count : 256;
	adder.offset = offset;
	for (i = 0; i < region_count; i++)
	{
		long permutation_count, r;
		unsigned long permutations;
		char name[32];
		boolean whole, has_base;

		h2w_name(cache, h2_cache_read32(cache, regions + 0x10 * i), name, sizeof(name));
		if (m->merge)
		{
			char const *const *merge;

			for (merge = m->merge; merge[0] && strcmp(merge[0], name); merge += 2)
				;
			if (merge[0])
				snprintf(name, sizeof(name), "%s", merge[1]);
		}
		whole = m->piece_region != NULL;
		r = h2w_model_region(m, m->piece_region ? m->piece_region : name);
		/* (past an object's regions: the first's, as it is) */
		if (r == NONE && m->made.regions && m->made.regions->count)
		{
			r = 0;
			whole = TRUE;
		}
		if (r == NONE || !h2_cache_block(cache, regions + 0x10 * i + 8, 0x10, &permutation_count, &permutations))
			continue;
		/* (whether one of the region's permutations is named "base": the
		Scorpion's "front" lists "destroyed" first and "base" second) */
		has_base = FALSE;
		for (j = 0; j < permutation_count && !has_base; j++)
		{
			char permutation_name[32];

			h2w_name(cache, h2_cache_read32(cache, permutations + 0x10 * j), permutation_name, sizeof(permutation_name));
			has_base = !strcmp(permutation_name, "base");
		}
		for (j = 0; j < permutation_count; j++)
		{
			char permutation_name[32];
			short section;
			long p, before = m->made.part_count, k;
			struct h2w_region_entry *grown;

			h2w_name(cache, h2_cache_read32(cache, permutations + 0x10 * j), permutation_name, sizeof(permutation_name));
			if (strstr(permutation_name, "blur") || (whole && (has_base ? strcmp(permutation_name, "base") != 0 : j != 0)))
				continue;
			/* (a region's first permutation its undamaged one, the object's
			"base", whatever Halo 2 names it: the Pelican's hull's "standard"
			was drawn by no object - unless the region has a "base" of its
			own: then each keeps its name (the Scorpion's front drew its
			"destroyed" plate over its "base" one, a block on the hull)) */
			if (j == 0 && !has_base)
				snprintf(permutation_name, sizeof(permutation_name), "%s", "base");
			p = whole ? MODEL_PERMUTATION_ALL : h2w_model_permutation(m, r, permutation_name);
			section = read16((unsigned char const *)h2_cache_at(cache, permutations + 0x10 * j + 4 + 2 * detail, 2));
			if (p == NONE || section < 0 || section >= section_count || !h2w_model_add_section_at(&adder, section, TRUE))
				continue;
			for (k = before; k < m->made.part_count; k++)
			{
				m->made.parts[k].region = (unsigned char)r;
				m->made.parts[k].permutation = (unsigned char)p;
			}
			/* (its sections at this engine's levels, its coarser ones added
			after every finest one - perf/h2-object-lods) */
			if ((grown = realloc(entries, (unsigned long)(entry_count + 1) * sizeof(*entries))) != NULL)
			{
				entries = grown;
				entries[entry_count].r = r;
				entries[entry_count].p = p;
				entries[entry_count].begin = before;
				entries[entry_count].end = m->made.part_count;
				h2w_level_sections(build, m, tag, permutations + 0x10 * j, detail, section_count, entries[entry_count].sections);
				entries[entry_count].sections[4] = section;
				entry_count++;
			}
		}
	}
	finest_end = m->made.part_count;
	/* (each permutation's coarser sections: model_level_effective - one
	drawing with the finest's shaders alone, else the next finer level's) */
	{
		long *begin = malloc(((unsigned long)section_count + 1) * sizeof(long));
		long *end = malloc(((unsigned long)section_count + 1) * sizeof(long));
		unsigned char *tried = malloc((unsigned long)section_count + 1);

		for (e = 0; begin && end && tried && e < entry_count; e++)
		{
			struct h2w_region_entry const *entry = &entries[e];
			short effective[5];
			long before = m->made.part_count, k;

			for (k = 0; k <= section_count; k++)
				begin[k] = end[k] = -1;
			memset(tried, 0, (unsigned long)section_count + 1);
			begin[entry->sections[4]] = entry->begin;
			end[entry->sections[4]] = entry->end;
			tried[entry->sections[4]] = 1;
			if (m->levels && model_levels_wanted())
				model_level_effective(&m->made, entry->sections, section_count, finest_from, finest_end, begin, end, tried,
					h2w_model_add_coarser, &adder, effective);
			else
				effective[0] = effective[1] = effective[2] = effective[3] = effective[4] = entry->sections[4];
			for (k = before; k < m->made.part_count; k++)
			{
				m->made.parts[k].region = (unsigned char)entry->r;
				m->made.parts[k].permutation = (unsigned char)entry->p;
			}
			model_level_mark(&m->made, effective, section_count, begin, end);
		}
		free(begin);
		free(end);
		free(tried);
	}
	model_level_normalize(&m->made, finest_from);
	free(entries);
	if (m->made.regions)
	{
		for (i = 0; i < m->made.regions->count; i++)
			h2w_model_permutation(m, i, "gone");
	}
}

/* (fold_tips: 1 the arms' fingertips move with their fingers' middles; 2
the little fingers' middles too, with their lowest joints; 3 the ring
fingers' as well - for a gun of many nodes (the needler's sixteen crystals),
which this engine's 43 would not hold) */
H2_ALIGN static boolean h2w_node_folds(char const *name, int fold)
{
	static char const *const ends[] = { " tip", " pinky mid", " ring mid" };
	unsigned long length = strlen(name);
	int k;

	for (k = 0; k < fold && k < 3; k++)
	{
		unsigned long end = strlen(ends[k]);

		if (length > end && !strcmp(name + length - end, ends[k]))
			return TRUE;
	}
	return FALSE;
}

/* how far a first-person arms' nodes fold (h2w_node_folds) to leave the gun
(render model tag gun) room among this engine's nodes: the arms' tag arms */
H2_ALIGN static int h2w_arms_fold(struct h2_cache *cache, long arms, long gun)
{
	struct h2_cache_tag t;
	long arms_count = 0, gun_count = 0, i;
	unsigned long nodes, gun_nodes;
	int fold;

	if (!h2_cache_tag_get(cache, arms, &t) || !h2_cache_block(cache, t.address + 0x48, 0x60, &arms_count, &nodes) ||
		!h2_cache_tag_get(cache, gun, &t) || !h2_cache_block(cache, t.address + 0x48, 0x60, &gun_count, &gun_nodes))
	{
		return 1;
	}
	for (fold = 1; fold < 3; fold++)
	{
		long kept = 0;

		for (i = 0; i < arms_count; i++)
		{
			char name[32];

			h2w_name(cache, h2_cache_read32(cache, nodes + 0x60 * i), name, sizeof(name));
			kept += !h2w_node_folds(name, fold);
		}
		if (kept + gun_count <= H2W_MAXIMUM_NODES)
			break;
	}
	return fold;
}

/* a Halo 2 render model added to m: its nodes (by name: one already there
is shared), markers, and each region's first permutation's section of that
detail (0 super low to 5 the highest); FALSE if it has none drawn. skip: a
material's shader name part not drawn (NULL none) */
/* (h2w_model_add with its nodes renamed - names, NULL-ended, Halo 2's
name (its '_' made ' ') then the model's - and its vertices moved by offset,
which places its nodes' inverses with them: a piece seated in another's
skeleton, h2_vehicles.c) */
H2_ALIGN static boolean h2w_model_add_renamed(struct build *build, struct h2w_model *m, long tag, char const *skip, short detail,
	boolean fold_tips, void const *names, float const *offset);

H2_ALIGN static boolean h2w_model_add(struct build *build, struct h2w_model *m, long tag, char const *skip, short detail,
	boolean fold_tips)
{
	return h2w_model_add_renamed(build, m, tag, skip, detail, fold_tips, NULL, NULL);
}

H2_ALIGN static boolean h2w_model_add_renamed(struct build *build, struct h2w_model *m, long tag, char const *skip, short detail,
	boolean fold_tips, void const *names, float const *offset)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	long region_count, section_count, material_count, node_count, group_count, i;
	unsigned long regions, sections, materials, nodes, groups;
	short node_map[256];
	short *shaders;
	unsigned char *done;
	long parts_before = m->made.part_count;

	if (!h2_cache_tag_get(cache, tag, &t) ||
		!h2_cache_block(cache, t.address + 0x1C, 0x10, &region_count, &regions) ||
		!h2_cache_block(cache, t.address + 0x24, 0x5C, &section_count, &sections) || section_count < 1 ||
		!h2_cache_block(cache, t.address + 0x60, 0x20, &material_count, &materials) ||
		!h2_cache_block(cache, t.address + 0x48, 0x60, &node_count, &nodes))
	{
		return FALSE;
	}
	/* (perf/h2-object-lods: the first render model's distances the
	object's - a vehicle's hull's, its turret then drawn by them) */
	if (m->levels && !m->reduce_known)
	{
		float reduce[5];

		m->reduce_known = TRUE;
		if (model_reduce_distances(cache, tag, reduce))
			memcpy(m->made.h2_reduce, reduce, sizeof(m->made.h2_reduce));
	}
	/* nodes */
	for (i = 0; i < node_count && i < 256; i++)
	{
		unsigned char const *c = h2_cache_at(cache, nodes + 0x60 * i, 0x60);
		char name[32];
		short n;

		node_map[i] = NONE;
		if (!c)
			continue;
		h2w_name(cache, read32(c), name, sizeof(name));
		if (names)
		{
			struct { char const *h2, *ce; } const *rename = names;

			for (; rename->h2 && strcmp(rename->h2, name); rename++)
				;
			if (rename->h2)
				snprintf(name, sizeof(name), "%s", rename->ce);
		}
		n = h2w_model_node(m, name);
		/* (the arms' fingertips move with their fingers' middles: ten nodes
		fewer for the weapon's own - this engine skins 43 at most) */
		if (n == NONE && fold_tips && h2w_node_folds(name, fold_tips) &&
			read16(c + 4) >= 0 && read16(c + 4) < i && node_map[read16(c + 4)] != NONE)
		{
			n = node_map[read16(c + 4)];
		}
		else if (n == NONE && m->node_count < H2W_MAXIMUM_NODES)
		{
			struct h2w_node *o = &m->nodes[m->node_count];
			short parent = read16(c + 4);

			memset(o, 0, sizeof(*o));
			snprintf(o->name, sizeof(o->name), "%s", name);
			o->parent = parent >= 0 && parent < i ? node_map[parent] : NONE;
			memcpy(o->translation, c + 0xC, 12);
			memcpy(o->rotation, c + 0x18, 16);
			memcpy(&o->scale, c + 0x28, 4);
			memcpy(o->inverse, c + 0x2C, 48);
			/* (moved with its vertices: the inverse's position less the
			offset taken through its axes) */
			if (offset)
			{
				int a;

				for (a = 0; a < 3; a++)
					o->inverse[9 + a] -= o->inverse[a] * offset[0] + o->inverse[3 + a] * offset[1] + o->inverse[6 + a] * offset[2];
			}
			n = (short)m->node_count++;
		}
		node_map[i] = n;
	}
	for (; i < 256; i++)
		node_map[i] = NONE;
	/* markers */
	if (h2_cache_block(cache, t.address + 0x58, 0xC, &group_count, &groups))
	{
		for (i = 0; i < group_count; i++)
		{
			long marker_count, k;
			unsigned long markers;
			char name[32];

			h2w_name(cache, h2_cache_read32(cache, groups + 0xC * i), name, sizeof(name));
			if (!h2_cache_block(cache, groups + 0xC * i + 4, 0x24, &marker_count, &markers))
				continue;
			for (k = 0; k < marker_count && m->marker_count < H2W_MAXIMUM_MARKERS; k++)
			{
				unsigned char const *c = h2_cache_at(cache, markers + 0x24 * k, 0x24);
				struct h2w_marker *o = &m->markers[m->marker_count];
				signed char node = c ? (signed char)c[2] : -1;

				if (!c || node < 0 || node >= node_count || node_map[(int)node] == NONE)
					continue;
				/* (a marker of a permutation other than its region's first - a
				damaged piece's copy - where the first has one: Halo 2's model
				has only the current permutation's. Both, the Banshee's tips had
				two contrails and two lights each) */
				if ((signed char)c[1] > 0)
				{
					long other;
					boolean covered = FALSE;

					for (other = 0; other < marker_count && !covered; other++)
					{
						unsigned char const *q = h2_cache_at(cache, markers + 0x24 * other, 0x24);

						covered = q && q[0] == c[0] && (signed char)q[1] <= 0;
					}
					if (covered)
						continue;
				}
				snprintf(o->name, sizeof(o->name), "%s", name);
				o->node = node_map[(int)node];
				memcpy(o->translation, c + 4, 12);
				memcpy(o->rotation, c + 0x10, 16);
				m->marker_count++;
				/* (this engine's names for a Halo 2 marker: the hand a biped's
				left hand takes) */
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
	/* sections */
	shaders = malloc((material_count + 1) * sizeof(short));
	done = calloc(section_count + 1, 1);
	if (!shaders || !done)
	{
		free(shaders);
		free(done);
		return FALSE;
	}
	for (i = 0; i < material_count; i++)
	{
		long shader = h2_cache_reference(cache, materials + 0x20 * i + 8);
		char const *name;

		if (shader == NONE)
			shader = h2_cache_reference(cache, materials + 0x20 * i);
		name = shader != NONE ? tag_short_name(h2_cache_tag_name(cache, shader)) : "";
		shaders[i] = skip && strstr(skip, name) && name[0] ? NONE : shader_for(build, shader);
		/* (a counter's digits: the weapon's rounds' - h2w_digit_shader) */
		if (shader != NONE && (!strcmp(name, "tens") || !strcmp(name, "ones")))
		{
			struct h2_cache_tag shader_tag;
			long template_tag = h2_cache_tag_get(cache, shader, &shader_tag) ? h2_cache_reference(cache, shader_tag.address) : NONE;

			if (template_tag != NONE && strstr(h2_cache_tag_name(cache, template_tag), "illum_detail"))
			{
				short digit = h2w_digit_shader(build, shader, !strcmp(name, "ones"));

				if (digit != NONE)
					shaders[i] = digit;
			}
		}
		/* (Halo 2's meters - a weapon's charge or heat gauge, drawn by its
		value - are not this engine's shaders: not drawn) */
		if (shader != NONE)
		{
			struct h2_cache_tag shader_tag;
			long template_tag = h2_cache_tag_get(cache, shader, &shader_tag) ? h2_cache_reference(cache, shader_tag.address) : NONE;

			if (template_tag != NONE && strstr(h2_cache_tag_name(cache, template_tag), "\\meter"))
				shaders[i] = h2w_meter_shader(build, shader, h2_cache_tag_name(cache, shader));
			/* (a powerup's orb and shell, S7: h2w_shell_shaders) */
			else if (h2w_powerup_materials && template_tag != NONE &&
				h2w_shell_shader(build, shader, h2_cache_tag_name(cache, template_tag)) != NONE)
			{
				shaders[i] = h2w_shell_shader(build, shader, h2_cache_tag_name(cache, template_tag));
			}
			/* (the PC draws Halo 2's plasma passes themselves - the sword's
			blade, the needler's core, the plasma rifle's and pistol's currents:
			h2_level.c plasma_pass_fill / plasma_mask_offset_fill set the
			shader up; the stand-ins below are the Vita's) */
			else if (template_tag != NONE && strstr(h2_cache_tag_name(cache, template_tag), "\\transparent\\plasma") &&
				shaders[i] != NONE && build->shaders[shaders[i]].plasma_pass)
			{
			}
			else if (template_tag != NONE && strstr(h2_cache_tag_name(cache, template_tag), "\\transparent\\plasma"))
			{
				/* (its mask's glow, else the flat one) */
				shaders[i] = h2w_plasma_masked(build, shader, strstr(name, "needle") ? 1 : strstr(name, "blade") ? 2 : 0);
				/* (nor, without a mask, one whose map is only its noise - the
				Phantom's antigrav_effects over the default clouds_256: the flat
				glow made a light blue box of each antigrav volume under its
				hull; not drawn, as h2_level.c leaves the capital ship's) */
				if (shaders[i] == NONE && !h2w_plasma_noise_only(cache, shader))
					shaders[i] = h2w_plasma_shader(build, strstr(name, "needle") != NULL);
			}
			/* (a crystal - the needler's needles, one_alpha_env: drawn glassy
			by Halo 2, a flat white multiply here, which left only their
			plasma cores, two crossed cards each; the world model's, an
			illum template, a dark gradient unlit - both the crystal) */
			else if (strstr(name, "needle") && ((template_tag != NONE &&
				strstr(h2_cache_tag_name(cache, template_tag), "\\transparent\\one_alpha_env")) || strstr(name, "luminous")))
			{
				/* (the first-person crystals see-through, as Halo 2's
				one_alpha_env; the world model's, an illum template, lit) */
				short crystal = template_tag != NONE && strstr(h2_cache_tag_name(cache, template_tag), "\\transparent\\one_alpha_env") ?
					h2w_crystal_transparent(build, shader) : NONE;

				if (crystal == NONE)
					crystal = h2w_crystal_shader(build, shader);
				if (crystal != NONE)
					shaders[i] = crystal;
			}
			/* (a scope's screen - Halo 2's texture_camera\scope: the view
			down the barrel under its scope_view overlay - the screen's copy
			through the overlay, h2w_scope_camera_shader; else a dim lit
			glass) */
			else if (strstr(name, "_screen"))
			{
				short camera = template_tag != NONE && strstr(h2_cache_tag_name(cache, template_tag), "texture_camera\\scope") &&
					h2w_scope_camera_wanted() ? h2w_scope_camera_shader(build, shader) : NONE;

				shaders[i] = camera != NONE ? camera : h2w_plasma_shader(build, 2);
			}
		}
	}
	if (m->regioned)
	{
		h2w_model_add_regions(build, m, tag, &t, regions, region_count, sections, section_count, shaders, material_count, node_map,
			node_count, detail, offset);
		region_count = 0;
	}
	/* (each region's first permutation's sections at m's levels - level 4
	the detail asked for -, each section once: the finest sections added
	first, in the regions' order as before, then each coarser one
	(model_level_effective: one drawing with the finest's shaders alone,
	else the next finer level's drawn there) - perf/h2-object-lods) */
	{
		short (*level_sections)[5] = region_count > 0 ? calloc((unsigned long)region_count, sizeof(*level_sections)) : NULL;
		long *begin = malloc(((unsigned long)section_count + 1) * sizeof(long));
		long *end = malloc(((unsigned long)section_count + 1) * sizeof(long));
		long finest_from = m->made.part_count, finest_end, k;
		struct h2w_section_adder adder;
		int c;

		adder.build = build;
		adder.m = m;
		adder.render_model = t.address;
		adder.sections = sections;
		adder.materials = shaders;
		adder.material_count = material_count;
		adder.node_map = node_map;
		adder.node_map_count = node_count < 256 ? node_count : 256;
		adder.offset = offset;
		for (k = 0; begin && end && k <= section_count; k++)
			begin[k] = end[k] = -1;
		for (i = 0; level_sections && begin && end && i < region_count; i++)
		{
			long permutation_count;
			unsigned long permutations;
			short section;

			for (c = 0; c < 5; c++)
				level_sections[i][c] = -1;
			if (!h2_cache_block(cache, regions + 0x10 * i + 8, 0x10, &permutation_count, &permutations) || permutation_count < 1)
				continue;
			section = read16((unsigned char const *)h2_cache_at(cache, permutations + 4 + 2 * detail, 2));
			if (section < 0 || section >= section_count)
				continue;
			h2w_level_sections(build, m, tag, permutations, detail, section_count, level_sections[i]);
			level_sections[i][4] = section;
			if (done[section])
				continue;
			done[section] = 1;
			begin[section] = m->made.part_count;
			h2w_model_add_section_at(&adder, section, TRUE);
			end[section] = m->made.part_count;
		}
		finest_end = m->made.part_count;
		for (i = 0; level_sections && begin && end && i < region_count; i++)
		{
			short effective[5];

			if (level_sections[i][4] < 0 || level_sections[i][4] >= section_count)
				continue;
			if (m->levels && model_levels_wanted())
				model_level_effective(&m->made, level_sections[i], section_count, finest_from, finest_end, begin, end, done,
					h2w_model_add_coarser, &adder, effective);
			else
				effective[0] = effective[1] = effective[2] = effective[3] = effective[4] = level_sections[i][4];
			model_level_mark(&m->made, effective, section_count, begin, end);
		}
		model_level_normalize(&m->made, finest_from);
		free(level_sections);
		free(begin);
		free(end);
	}
	free(shaders);
	free(done);

	return m->made.part_count > parts_before;
}

H2_ALIGN static void h2w_model_begin(struct h2w_model *m)
{
	memset(m, 0, sizeof(*m));
	m->model = NONE;
	m->made.extent[0] = m->made.extent[2] = m->made.extent[4] = 1e30f;
	m->made.extent[1] = m->made.extent[3] = m->made.extent[5] = -1e30f;
}

/* the model finished (its coordinates compressed over its scale, each
vertex's nodes and weight in place: model_finish's, keeping the nodes) and
added to the build's models */
H2_ALIGN static boolean h2w_model_finish(struct build *build, struct h2w_model *m)
{
	struct model_made *model = &m->made, *grown;
	long p, n;
	float largest = 0.0f;

	if (!model->part_count)
		return FALSE;
	/* (its finest level's parts' first - perf/h2-object-lods: the scale as
	before -, then a coarser one's past it grows it) */
	for (p = 0; p < 2 * model->part_count; p++)
	{
		struct model_part_made const *part = &model->parts[p % model->part_count];
		boolean finest = !part->levels || (part->levels & 0x10);

		if (p == model->part_count)
		{
			for (model->scale = 1.0f; model->scale < largest && model->scale < 65536.0f; model->scale *= 2.0f)
				;
		}
		if (finest != (p < model->part_count))
			continue;
		for (n = 0; n < part->vertex_count; n++)
		{
			unsigned char *v = part->vertices + 32 * n;
			float u = fabsf(readf(v + 24)), w = fabsf(readf(v + 28));

			largest = u > largest ? u : largest;
			largest = w > largest ? w : largest;
		}
	}
	/* (a coarser level's past the finest's scale by a hair - 1.00001 -
	clamped, compress_model_real: the finest's scale kept) */
	if (largest > 1.01f * model->scale && getenv("HALO_H2_DEBUG_LODS"))
		platform_log("h2 lods: a model's coarser levels' texture coordinates up to %.3f (its finest's scale %.0f)", largest, model->scale);
	for (; 1.01f * model->scale < largest && model->scale < 65536.0f; model->scale *= 2.0f)
		;
	for (p = 0; p < model->part_count; p++)
	{
		struct model_part_made *part = &model->parts[p];
		unsigned char const *side = part->vertices + (unsigned long)part->vertex_count * 32;

		for (n = 0; n < part->vertex_count; n++)
		{
			unsigned char *v = part->vertices + 32 * n;
			float u = readf(v + 24), w = readf(v + 28);

			write16(v + 24, compress_model_real(u / model->scale));
			write16(v + 26, compress_model_real(w / model->scale));
			v[28] = (unsigned char)(3 * side[4 * n]);
			v[29] = (unsigned char)(3 * side[4 * n + 1]);
			v[30] = side[4 * n + 2];
			v[31] = side[4 * n + 3];
		}
	}
	grown = realloc(build->models, (build->model_count + 1) * sizeof(*grown));
	if (!grown)
		return FALSE;
	build->models = grown;
	build->models[build->model_count] = *model;
	memset(model, 0, sizeof(*model));
	m->model = build->model_count++;
	/* (in the spill file until its tags are written) */
	models_spill(&build->models[m->model], 1);

	return TRUE;
}

/* (h2w_model_write's markers' names: models.c's model_find_marker's order,
_stricmp's) */
H2_ALIGN static int h2w_marker_name_compare(void const *a, void const *b)
{
	char const *x = (char const *)a, *y = (char const *)b;

	for (;; x++, y++)
	{
		int cx = *x >= 'A' && *x <= 'Z' ? *x - 'A' + 'a' : *x, cy = *y >= 'A' && *y <= 'Z' ? *y - 'A' + 'a' : *y;

		if (cx != cy || !cx)
			return cx - cy;
	}
}

/* the model's tag (its root at root, an offset of r) given its nodes and
markers, after h2_level_build wrote it with one node */
H2_ALIGN static void h2w_model_write(struct region *r, unsigned long root, struct h2w_model const *m)
{
	unsigned long nodes, markers;
	long i, k;
	char names[H2W_MAXIMUM_MARKERS][32];
	long name_count = 0;

	/* (no root: a model made at load, h2_models_load.inc - written there) */
	if (!m->node_count || !root)
		return;
	nodes = region_block(r, root + offsetof(struct model, nodes), m->node_count, sizeof(struct model_node));
	for (i = 0; i < m->node_count && !r->b.failed; i++)
	{
		struct h2w_node const *n = &m->nodes[i];
		unsigned long o = nodes + sizeof(struct model_node) * i;
		short child = NONE, sibling = NONE;
		float d;

		for (k = 0; k < m->node_count; k++)
		{
			if (m->nodes[k].parent == i && child == NONE)
				child = (short)k;
		}
		for (k = i + 1; k < m->node_count; k++)
		{
			if (m->nodes[k].parent == n->parent)
			{
				sibling = (short)k;
				break;
			}
		}
		memcpy(region_at(r, o), n->name, strlen(n->name) + 1);
		write16(region_at(r, o + offsetof(struct model_node, next_sibling_node_index)), sibling);
		write16(region_at(r, o + offsetof(struct model_node, first_child_node_index)), child);
		write16(region_at(r, o + offsetof(struct model_node, parent_node_index)), n->parent);
		for (k = 0; k < 3; k++)
			writef(region_at(r, o + offsetof(struct model_node, default_translation) + 4 * k), n->translation[k]);
		/* (conjugated: this engine's turn) */
		for (k = 0; k < 3; k++)
			writef(region_at(r, o + offsetof(struct model_node, default_rotation) + 4 * k), -n->rotation[k]);
		writef(region_at(r, o + offsetof(struct model_node, default_rotation) + 12), n->rotation[3]);
		d = sqrtf(n->translation[0] * n->translation[0] + n->translation[1] * n->translation[1] + n->translation[2] * n->translation[2]);
		writef(region_at(r, o + offsetof(struct model_node, node_distance_from_parent)), d);
		writef(region_at(r, o + offsetof(struct model_node, runtime_default_inverse_matrix)), 1.0f);
		for (k = 0; k < 12; k++)
			writef(region_at(r, o + offsetof(struct model_node, runtime_default_inverse_matrix) + 4 + 4 * k), n->inverse[k]);
	}
	/* markers: one a name, an instance each */
	for (i = 0; i < m->marker_count; i++)
	{
		for (k = 0; k < name_count && strcmp(names[k], m->markers[i].name); k++)
			;
		if (k == name_count)
			snprintf(names[name_count++], 32, "%s", m->markers[i].name);
	}
	/* (sorted as models.c's model_find_marker searches them - a binary
	search, _stricmp: unsorted, a name was found or not by chance - the
	Warthog's "gunner" was not, its gunner seated at the hull's origin) */
	qsort(names, (size_t)name_count, sizeof(names[0]), h2w_marker_name_compare);
	markers = region_block(r, root + offsetof(struct model, markers), name_count, sizeof(struct model_marker));
	for (k = 0; k < name_count && !r->b.failed; k++)
	{
		unsigned long instances;
		long count = 0, done = 0;

		memcpy(region_at(r, markers + sizeof(struct model_marker) * k), names[k], strlen(names[k]) + 1);
		for (i = 0; i < m->marker_count; i++)
			count += !strcmp(m->markers[i].name, names[k]);
		instances = region_block(r, markers + sizeof(struct model_marker) * k + offsetof(struct model_marker, instances),
			count, sizeof(struct model_marker_instance));
		for (i = 0; i < m->marker_count && !r->b.failed; i++)
		{
			struct h2w_marker const *mk = &m->markers[i];
			unsigned long o = instances + sizeof(struct model_marker_instance) * done;
			int c;

			if (strcmp(mk->name, names[k]))
				continue;
			done++;
			region_at(r, o)[0] = 0;
			/* (any permutation of it: models.c) */
			region_at(r, o)[1] = 0xFF;
			region_at(r, o)[2] = (unsigned char)mk->node;
			for (c = 0; c < 3; c++)
				writef(region_at(r, o + offsetof(struct model_marker_instance, translation) + 4 * c), mk->translation[c]);
			for (c = 0; c < 3; c++)
				writef(region_at(r, o + offsetof(struct model_marker_instance, rotation) + 4 * c), -mk->rotation[c]);
			writef(region_at(r, o + offsetof(struct model_marker_instance, rotation) + 12), mk->rotation[3]);
		}
	}
}

/* ---------- private code: animations */

struct h2w_samples
{
	long frames;
	float *rotation[H2W_MAXIMUM_GRAPH_NODES];      /* 4 a frame */
	float *translation[H2W_MAXIMUM_GRAPH_NODES];   /* 3 a frame */
	float *scale[H2W_MAXIMUM_GRAPH_NODES];         /* 1 a frame */
	boolean rotation_animated[H2W_MAXIMUM_GRAPH_NODES];
	boolean translation_animated[H2W_MAXIMUM_GRAPH_NODES];
	boolean scale_animated[H2W_MAXIMUM_GRAPH_NODES];
};

H2_ALIGN static void h2w_samples_free(struct h2w_samples *s)
{
	long i;

	for (i = 0; i < H2W_MAXIMUM_GRAPH_NODES; i++)
	{
		free(s->rotation[i]);
		free(s->translation[i]);
		free(s->scale[i]);
	}
	memset(s, 0, sizeof(*s));
}

/* the nth set bit of flags (bytes), its node; NONE */
H2_ALIGN static short h2w_flag_node(unsigned char const *flags, long bytes, long n)
{
	long bit;

	for (bit = 0; bit < 8 * bytes && bit < H2W_MAXIMUM_GRAPH_NODES; bit++)
	{
		if (flags[bit >> 3] & (1 << (bit & 7)))
		{
			if (!n--)
				return (short)bit;
		}
	}
	return NONE;
}

/* one value read: component 0 rotation (four shorts), 1 translation (three
floats), 2 scale (a float) */
H2_ALIGN static void h2w_value(unsigned char const *p, int component, float *out)
{
	int k;

	if (component == 0)
	{
		for (k = 0; k < 4; k++)
			out[k] = (float)read16(p + 2 * k) / 32767.0f;
		h2w_normalize4(out);
	}
	else if (component == 1)
	{
		for (k = 0; k < 3; k++)
			out[k] = readf(p + 4 * k);
	}
	else
		out[0] = readf(p);
}

/* a codec block (at d + offset, in the data of size bytes) read into the
samples of the nodes flags (three sets: rotation, translation, scale) set:
static (frames 1, every frame the value) or animated; FALSE if it cannot */
H2_ALIGN static boolean h2w_codec_read(unsigned char const *d, unsigned long size, unsigned long offset,
	unsigned char const *flags, long flag_bytes, boolean animated, struct h2w_samples *s)
{
	static int const widths[3] = { 4, 3, 1 };
	static int const sizes[3] = { 8, 12, 4 };
	unsigned char const *b = d + offset;
	int codec, component;
	long counts[3], frames = s->frames;

	if (offset + 0x30 > size)
		return FALSE;
	codec = b[0];
	counts[0] = b[1];
	counts[1] = b[2];
	counts[2] = b[3];
	for (component = 0; component < 3; component++)
	{
		long i;

		for (i = 0; i < counts[component]; i++)
		{
			short node = h2w_flag_node(flags + component * flag_bytes, flag_bytes, i);
			float *out;
			long f;

			if (node == NONE)
				return FALSE;
			out = malloc((unsigned long)frames * widths[component] * sizeof(float) + 16);
			if (!out)
				return FALSE;
			if (component == 0)
				free(s->rotation[node]), s->rotation[node] = out, s->rotation_animated[node] = animated;
			else if (component == 1)
				free(s->translation[node]), s->translation[node] = out, s->translation_animated[node] = animated;
			else
				free(s->scale[node]), s->scale[node] = out, s->scale_animated[node] = animated;
			if (codec == 1)
			{
				/* static: one value */
				unsigned long at = component == 0 ? 0x20 + 8 * i : read32(b + (component == 1 ? 0xC : 0x10)) + sizes[component] * i;

				if (offset + at + sizes[component] > size)
					return FALSE;
				h2w_value(b + at, component, out);
				for (f = 1; f < frames; f++)
					memcpy(out + widths[component] * f, out, widths[component] * sizeof(float));
			}
			else if (codec == 2 || codec == 3)
			{
				/* every frame, node by node */
				unsigned long base = component == 0 ? 0x20 : read32(b + (component == 1 ? 0xC : 0x10));
				unsigned long stride = read32(b + 0x14 + 4 * component);

				for (f = 0; f < frames; f++)
				{
					unsigned long at = base + stride * i + sizes[component] * f;

					if (offset + at + sizes[component] > size)
						return FALSE;
					h2w_value(b + at, component, out + widths[component] * f);
				}
			}
			else if (codec == 8)
			{
				/* every frame, full floats (Halo 2's blend screens -
				pitch_and_turn): quaternions 16 bytes from 0x20, the
				translations and scales (as codec 2's) where the header
				says, node by node then frame by frame */
				unsigned long base = component == 0 ? 0x20 : read32(b + (component == 1 ? 0xC : 0x10));
				int const size8[3] = { 16, 12, 4 };

				for (f = 0; f < frames; f++)
				{
					unsigned long at = base + (unsigned long)(i * frames + f) * size8[component];

					if (offset + at + size8[component] > size)
						return FALSE;
					if (component == 0)
					{
						int w;

						for (w = 0; w < 4; w++)
							memcpy(out + 4 * f + w, b + at + 4 * w, 4);
						h2w_normalize4(out + 4 * f);
					}
					else
						h2w_value(b + at, component, out + widths[component] * f);
				}
			}
			else if (codec >= 4 && codec <= 7)
			{
				/* keyframed: the node's keys (a byte or a word each), its
				values, the frames between them linear */
				unsigned long headers = component == 0 ? 0x30 : read32(b + 0xC + 4 * (component - 1));
				unsigned long keys = read32(b + 0x14 + 4 * component);
				unsigned long values = read32(b + 0x20 + 4 * component);
				int key_size = codec == 4 || codec == 6 ? 1 : 2;
				unsigned long header, count, first_key;
				long k = 0;

				if (offset + headers + 4 * i + 4 > size)
					return FALSE;
				header = read32(b + headers + 4 * i);
				count = header & 0xFFF;
				first_key = header >> 12;
				if (!count || offset + keys + key_size * (first_key + count) > size ||
					offset + values + sizes[component] * (first_key + count) > size)
				{
					return FALSE;
				}
				for (f = 0; f < frames; f++)
				{
					long key_frame, next_frame;
					float a[4], c[4];
					int w;

					while (k + 1 < (long)count)
					{
						next_frame = key_size == 1 ? b[keys + first_key + k + 1] : (unsigned short)read16(b + keys + 2 * (first_key + k + 1));
						if (next_frame > f)
							break;
						k++;
					}
					key_frame = key_size == 1 ? b[keys + first_key + k] : (unsigned short)read16(b + keys + 2 * (first_key + k));
					h2w_value(b + values + sizes[component] * (first_key + k), component, a);
					if (k + 1 < (long)count)
					{
						float fraction;

						next_frame = key_size == 1 ? b[keys + first_key + k + 1] : (unsigned short)read16(b + keys + 2 * (first_key + k + 1));
						h2w_value(b + values + sizes[component] * (first_key + k + 1), component, c);
						fraction = next_frame > key_frame ? (float)(f - key_frame) / (float)(next_frame - key_frame) : 0.0f;
						if (fraction < 0.0f)
							fraction = 0.0f;
						if (component == 0 && a[0] * c[0] + a[1] * c[1] + a[2] * c[2] + a[3] * c[3] < 0.0f)
						{
							for (w = 0; w < 4; w++)
								c[w] = -c[w];
						}
						for (w = 0; w < widths[component]; w++)
							a[w] += (c[w] - a[w]) * fraction;
						if (component == 0)
							h2w_normalize4(a);
					}
					memcpy(out + widths[component] * f, a, widths[component] * sizeof(float));
				}
			}
			else
				return FALSE;
		}
	}
	return TRUE;
}

/* a jmad's animation (its entry at entry) decoded; FALSE if it cannot be */
H2_ALIGN static boolean h2w_animation_decode(struct build *build, unsigned long entry, long node_count, struct h2w_samples *s)
{
	struct h2_cache *cache = build->cache;
	unsigned char const *e = h2_cache_at(cache, entry, 0x60);
	unsigned long size, address, static_size, animated_size;
	unsigned char const *d;
	long static_flag_bytes, animated_flag_bytes;

	memset(s, 0, sizeof(*s));
	if (!e)
		return FALSE;
	s->frames = read16(e + 0x14);
	size = read32(e + 0x28);
	address = read32(e + 0x2C);
	static_flag_bytes = e[0x30];
	animated_flag_bytes = e[0x31];
	static_size = (unsigned short)read16(e + 0x36);
	/* (read before the data's h2_cache_at, which may let e's piece go) */
	animated_size = read32(e + 0x3C);
	if (s->frames < 1 || s->frames > 2048 || !size || size > 4 * 1024 * 1024 || node_count > H2W_MAXIMUM_GRAPH_NODES ||
		(static_flag_bytes % 3) || (animated_flag_bytes % 3) || (unsigned long)(static_flag_bytes + animated_flag_bytes) > size ||
		static_size > size || !(d = h2_cache_at(cache, address, size)))
	{
		return FALSE;
	}
	/* (the data: the static codec, the animated codec - its size at 0x3C -,
	the static and the animated node flags, then any movement data: the
	flags are not at the end when the animation moves - frame info, the
	Elite's single SMG first_person:fire_1 -, as h2_animation.c reads them) */
	{
		unsigned char const *flags =
			animated_size <= size && static_size + animated_size + static_flag_bytes + animated_flag_bytes <= size ?
				d + static_size + animated_size :
				d + size - static_flag_bytes - animated_flag_bytes;

		if (static_size && static_flag_bytes &&
			!h2w_codec_read(d, size, 0, flags, static_flag_bytes / 3, FALSE, s))
		{
			h2w_samples_free(s);
			return FALSE;
		}
		if (animated_flag_bytes &&
			!h2w_codec_read(d, size, static_size, flags + static_flag_bytes, animated_flag_bytes / 3, TRUE, s))
		{
			h2w_samples_free(s);
			return FALSE;
		}
	}
	return TRUE;
}

/* a quaternion as this engine stores it (conjugated, 8 bytes) */
H2_ALIGN static void h2w_put_rotation(unsigned char *p, float const *q)
{
	float v[4];
	int k;

	v[0] = -q[0];
	v[1] = -q[1];
	v[2] = -q[2];
	v[3] = q[3];
	for (k = 0; k < 4; k++)
	{
		float c = v[k] < -1.0f ? -1.0f : v[k] > 1.0f ? 1.0f : v[k];

		write16(p + 2 * k, (long)floorf(c * 32767.0f + 0.5f));
	}
}

/* Halo 2's ammunition animation (a replacement: the needler's crystals,
each drawn back into the gun as its rounds go - Halo 2 plays it by the
rounds loaded) as this engine's ammunition overlay, whose frame is the
rounds loaded (first_person_weapons.c): rounds + 1 frames, each its nodes'
pose less their default (the model's node), frame f Halo 2's frame f; its
nodes that do not move (the arms, the gun) left out. FALSE if it cannot be */
H2_ALIGN static boolean h2w_replacement_to_overlay(struct h2w_samples *s, struct h2w_graph const *graph,
	struct h2w_model const *model, short rounds)
{
	struct h2w_samples out;
	long n, f, frames = s->frames;
	float near_first = 0.0f, near_last = 0.0f;
	boolean full_first, linear;

	if (rounds < 1 || rounds > 255 || frames < 2)
		return FALSE;
	/* (which end is the full gun: the one nearer the nodes' defaults) */
	for (n = 0; n < graph->node_count; n++)
	{
		short m = h2w_model_node(model, graph->nodes[n].name);
		int k;

		if (m == NONE || !s->translation[n] || !s->translation_animated[n])
			continue;
		for (k = 0; k < 3; k++)
		{
			near_first += fabsf(s->translation[n][k] - model->nodes[m].translation[k]);
			near_last += fabsf(s->translation[n][3 * (frames - 1) + k] - model->nodes[m].translation[k]);
		}
	}
	for (n = 0; n < graph->node_count; n++)
	{
		short m = h2w_model_node(model, graph->nodes[n].name);

		if (m == NONE || !s->scale[n] || !s->scale_animated[n])
			continue;
		near_first += fabsf(s->scale[n][0] - model->nodes[m].scale);
		near_last += fabsf(s->scale[n][frames - 1] - model->nodes[m].scale);
	}
	full_first = near_first <= near_last;
	linear = getenv("HALO_H2_NEEDLER_LINEAR") && atoi(getenv("HALO_H2_NEEDLER_LINEAR"));
	/* (HALO_H2_DEBUG=2: each moving node's travel along Halo 2's frames -
	whether the crystals go one at a time or together) */
	if (getenv("HALO_H2_DEBUG") && atoi(getenv("HALO_H2_DEBUG")) >= 2)
	{
		for (n = 0; n < graph->node_count; n++)
		{
			short m = h2w_model_node(model, graph->nodes[n].name);
			long first_moved = NONE;
			float travel = 0.0f;

			if (m == NONE || !s->translation[n] || !s->translation_animated[n])
				continue;
			for (f = 1; f < frames; f++)
			{
				float d = 0.0f;
				int k;

				for (k = 0; k < 3; k++)
					d += fabsf(s->translation[n][3 * f + k] - s->translation[n][3 * (f - 1) + k]);
				travel += d;
				if (d > 0.0005f && first_moved == NONE)
					first_moved = f;
			}
			fprintf(stderr, "h2 weapons: ammunition node %s: first moves at frame %ld of %ld, travel %.3f (full %s)\n", graph->nodes[n].name, first_moved,
				frames, travel, full_first ? "first" : "last");
		}
	}
	memset(&out, 0, sizeof(out));
	out.frames = rounds + 1;
	for (n = 0; n < graph->node_count; n++)
	{
		short m = h2w_model_node(model, graph->nodes[n].name);
		boolean r = s->rotation[n] && s->rotation_animated[n], t = s->translation[n] && s->translation_animated[n];
		boolean c = s->scale[n] && s->scale_animated[n];

		if (m == NONE || (!r && !t && !c))
			continue;
		if ((r && !(out.rotation[n] = malloc(out.frames * 4 * sizeof(float)))) ||
			(t && !(out.translation[n] = malloc(out.frames * 3 * sizeof(float)))) ||
			(c && !(out.scale[n] = malloc(out.frames * sizeof(float)))))
		{
			h2w_samples_free(&out);
			return FALSE;
		}
		out.rotation_animated[n] = r;
		out.translation_animated[n] = t;
		out.scale_animated[n] = c;
		for (f = 0; f < out.frames; f++)
		{
			/* (rounds f: Halo 2's frame f itself, its last past it - the
			first-person update plays the ammunition animation at the frame
			the rounds loaded count, min(rounds, frames - 1), as Halo CE's
			first_person_weapons.c does: Xbox 0x167d20..0x167d41, the frame
			then set by 0x1c6560; the needler's 33 frames take the crystals
			back one a frame over 17..32, so its 30 rounds show 14 crystals
			full and lose one a shot, none at 15 - the H2EK tag test's
			needler: 14 at 30 rounds, 9 at 25, 4 at 20, none at 15.
			HALO_H2_NEEDLER_LINEAR=1: the rounds spread over every frame,
			before) */
			float at = linear ? (full_first ? (float)(rounds - f) : (float)f) * (float)(frames - 1) / (float)rounds :
				(float)(f < frames - 1 ? f : frames - 1);
			long a = (long)at, b = a + 1 < frames ? a + 1 : a;
			float u = at - (float)a;
			int k;

			if (t)
			{
				for (k = 0; k < 3; k++)
				{
					out.translation[n][3 * f + k] = s->translation[n][3 * a + k] + (s->translation[n][3 * b + k] - s->translation[n][3 * a + k]) * u -
						model->nodes[m].translation[k];
				}
			}
			if (c)
			{
				float v = s->scale[n][a] + (s->scale[n][b] - s->scale[n][a]) * u;

				out.scale[n][f] = model->nodes[m].scale > 0.0f ? v / model->nodes[m].scale : v;
			}
			if (r)
			{
				float q[4], length = 0.0f, dot = 0.0f;
				float const *p = s->rotation[n] + 4 * a, *p2 = s->rotation[n] + 4 * b, *d = model->nodes[m].rotation;
				float *o = out.rotation[n] + 4 * f;

				for (k = 0; k < 4; k++)
					dot += p[k] * p2[k];
				for (k = 0; k < 4; k++)
					q[k] = p[k] + ((dot < 0.0f ? -p2[k] : p2[k]) - p[k]) * u, length += q[k] * q[k];
				length = length > 0.0f ? 1.0f / sqrtf(length) : 1.0f;
				for (k = 0; k < 4; k++)
					q[k] *= length;
				/* (the default's conjugate times the pose: this engine's overlay
				turns the pose it is laid on by it) */
				o[3] = d[3] * q[3] + d[0] * q[0] + d[1] * q[1] + d[2] * q[2];
				o[0] = d[3] * q[0] - d[0] * q[3] - d[1] * q[2] + d[2] * q[1];
				o[1] = d[3] * q[1] + d[0] * q[2] - d[1] * q[3] - d[2] * q[0];
				o[2] = d[3] * q[2] - d[0] * q[1] + d[1] * q[0] - d[2] * q[3];
			}
		}
	}
	h2w_samples_free(s);
	*s = out;
	return TRUE;
}

/* the graph's animation for one of Halo 2's (decoded samples): this
engine's frames (animated values) and defaults (the others: Halo 2's
static values, else the model's node's default, else none moved) */
H2_ALIGN static boolean h2w_animation_make(struct h2w_graph *graph, struct h2w_model const *model, struct h2w_samples const *s,
	short type, char const *name, struct h2w_animation *a)
{
	long n, f;
	unsigned long default_size = 0, frame_size = 0;
	unsigned char *p;

	memset(a, 0, sizeof(*a));
	snprintf(a->name, sizeof(a->name), "%s", name);
	a->type = type;
	a->frame_count = (short)s->frames;
	a->node_count = (short)graph->node_count;
	a->next = NONE;
	a->parent = NONE;
	a->weight = 1.0f;
	a->normalized_weight = 1.0f;
	for (n = 0; n < graph->node_count; n++)
	{
		boolean r = s->rotation[n] && (s->rotation_animated[n] || type != _animation_base);
		boolean t = s->translation[n] && (s->translation_animated[n] || type != _animation_base);
		boolean c = s->scale[n] && (s->scale_animated[n] || type != _animation_base);

		if (r)
			a->rotation_flags[n >> 5] |= 1UL << (n & 31), frame_size += 8;
		else if (type == _animation_base)
			default_size += 8;
		if (t)
			a->translation_flags[n >> 5] |= 1UL << (n & 31), frame_size += 12;
		else if (type == _animation_base)
			default_size += 12;
		if (c)
			a->scale_flags[n >> 5] |= 1UL << (n & 31), frame_size += 4;
		else if (type == _animation_base)
			default_size += 4;
	}
	a->frame_size = (short)frame_size;
	a->default_size = default_size;
	a->frame_bytes = frame_size * (unsigned long)s->frames;
	a->defaults = default_size ? malloc(default_size) : NULL;
	a->frames = a->frame_bytes ? malloc(a->frame_bytes) : NULL;
	if ((default_size && !a->defaults) || (a->frame_bytes && !a->frames))
		return FALSE;
	/* defaults, node by node */
	p = a->defaults;
	for (n = 0; n < graph->node_count && type == _animation_base; n++)
	{
		short m = h2w_model_node(model, graph->nodes[n].name);
		float identity[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		float zero[3] = { 0.0f, 0.0f, 0.0f };

		if (!(a->rotation_flags[n >> 5] & (1UL << (n & 31))))
		{
			h2w_put_rotation(p, s->rotation[n] ? s->rotation[n] : m != NONE ? model->nodes[m].rotation : identity);
			p += 8;
		}
		if (!(a->translation_flags[n >> 5] & (1UL << (n & 31))))
		{
			float const *v = s->translation[n] ? s->translation[n] : m != NONE ? model->nodes[m].translation : zero;
			int k;

			for (k = 0; k < 3; k++)
				writef(p + 4 * k, v[k]);
			p += 12;
		}
		if (!(a->scale_flags[n >> 5] & (1UL << (n & 31))))
		{
			writef(p, s->scale[n] ? s->scale[n][0] : m != NONE && model->nodes[m].scale > 0.0f ? model->nodes[m].scale : 1.0f);
			p += 4;
		}
	}
	/* frames */
	p = a->frames;
	for (f = 0; f < s->frames; f++)
	{
		for (n = 0; n < graph->node_count; n++)
		{
			if (a->rotation_flags[n >> 5] & (1UL << (n & 31)))
			{
				h2w_put_rotation(p, s->rotation[n] + 4 * f);
				p += 8;
			}
			if (a->translation_flags[n >> 5] & (1UL << (n & 31)))
			{
				int k;

				for (k = 0; k < 3; k++)
					writef(p + 4 * k, s->translation[n][3 * f + k]);
				p += 12;
			}
			if (a->scale_flags[n >> 5] & (1UL << (n & 31)))
			{
				writef(p, s->scale[n][f]);
				p += 4;
			}
		}
	}
	return TRUE;
}

H2_ALIGN static void h2w_graph_free(struct h2w_graph *graph)
{
	long i;

	if (!graph)
		return;
	for (i = 0; i < graph->animation_count; i++)
	{
		free(graph->animations[i].defaults);
		free(graph->animations[i].frames);
	}
	free(graph);
}

/* a weapon's first-person graph from Halo 2's (jmad), its nodes the
model's; NULL if it cannot be made */
/* a Halo 2 sound tag's (its tag index) place among the weapons' sounds,
added if new; NONE if it is not one of Halo 2's own ("sound\\...": not
MCC's remastered ones) or there is no room */
H2_ALIGN static short h2w_sound(struct h2_cache *cache, long tag)
{
	char const *name = tag != NONE ? h2_cache_tag_name(cache, tag) : NULL;
	long i;

	if (!name || strncmp(name, "sound\\", 6) || strlen(name + 6) >= sizeof(h2w.sounds[0]))
		return NONE;
	for (i = 0; i < h2w.sound_count; i++)
	{
		if (!strcmp(h2w.sounds[i], name + 6))
			return (short)i;
	}
	if (h2w.sound_count >= H2W_MAXIMUM_SOUNDS)
		return NONE;
	snprintf(h2w.sounds[h2w.sound_count], sizeof(h2w.sounds[0]), "%s", name + 6);
	return (short)h2w.sound_count++;
}

/* an effect's (Halo 2's effe: events at 0x14, 0x38 each; their parts at
0x18, 0x38 each, the part's type a reference at 0xC) first sound part's
sound, NONE if none */
H2_ALIGN static short h2w_effect_sound(struct h2_cache *cache, long effect)
{
	struct h2_cache_tag t;
	long event_count, e, part_count, p;
	unsigned long events, parts;

	if (effect == NONE || !h2_cache_tag_get(cache, effect, &t) || !h2_cache_block(cache, t.address + 0x14, 0x38, &event_count, &events))
		return NONE;
	for (e = 0; e < event_count; e++)
	{
		if (!h2_cache_block(cache, events + 0x38 * e + 0x18, 0x38, &part_count, &parts))
			continue;
		for (p = 0; p < part_count; p++)
		{
			unsigned char const *q = h2_cache_at(cache, parts + 0x38 * p + 0xC, 8);
			short sound;

			if (q && read32(q) == 'snd!' && (sound = h2w_sound(cache, h2_cache_tag_from_handle(cache, read32(q + 4)))) != NONE)
				return sound;
		}
	}
	return NONE;
}

H2_ALIGN static struct h2w_graph *h2w_graph_make(struct build *build, long jmad, struct h2w_model const *model, short rounds)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	struct h2w_graph *graph;
	long node_count, animation_count, i, k;
	unsigned long nodes, animations;
	boolean ammunition_nodes[2][H2W_MAXIMUM_GRAPH_NODES];

	if (!h2_cache_tag_get(cache, jmad, &t) ||
		!h2_cache_block(cache, t.address + 0xC, 0x20, &node_count, &nodes) ||
		!h2_cache_block(cache, t.address + 0x2C, 0x60, &animation_count, &animations) ||
		node_count < 1 || node_count > H2W_MAXIMUM_GRAPH_NODES || !(graph = calloc(1, sizeof(*graph))))
	{
		return NULL;
	}
	/* the graph's nodes: the jmad's the model has, and their parents (the
	fingertips, folded into the model's fingers, and the camera's control
	are left out: their frames are not carried) */
	{
		struct h2w_graph_node all[H2W_MAXIMUM_GRAPH_NODES];
		boolean keep[H2W_MAXIMUM_GRAPH_NODES];
		short map[H2W_MAXIMUM_GRAPH_NODES];

		for (i = 0; i < node_count; i++)
		{
			unsigned char const *c = h2_cache_at(cache, nodes + 0x20 * i, 0x20);

			h2w_name(cache, read32(c), all[i].name, sizeof(all[i].name));
			all[i].parent = read16(c + 8);
			keep[i] = h2w_model_node(model, all[i].name) != NONE;
		}
		for (i = node_count - 1; i >= 0; i--)
		{
			if (keep[i] && all[i].parent >= 0 && all[i].parent < node_count)
				keep[all[i].parent] = TRUE;
		}
		/* (a parent comes before its children in a jmad: kept in order) */
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
		}
		graph->source_count = node_count;
	}
	for (k = 0; k < H2W_FIRST_PERSON_SLOTS; k++)
		graph->slots[0][k] = graph->slots[1][k] = NONE;
	/* the nodes each set's ammunition animation moves (the needler's
	crystals): Halo 2's replacement sets them whatever the base animation
	has, this engine's overlay adds to the base - so the base animations
	leave them at the model's pose, and the overlay (its pose less that
	one) puts them where Halo 2 does. The single needler's idle and firing
	poses hold the crystals out: added to the overlay's, out twice as far
	(the dual-wield poses hold them in) */
	{
		long n;

		memset(ammunition_nodes, 0, sizeof(ammunition_nodes));
		for (i = 0; i < animation_count; i++)
		{
			unsigned char const *e = h2_cache_at(cache, animations + 0x60 * i, 0x60);
			char name[64];
			struct h2w_samples s;
			int set;

			if (!e || e[0x10] != 2)
				continue;
			h2_cache_string(cache, read32(e), name, sizeof(name));
			if (!strcmp(name, "first_person:ammunition"))
				set = 0;
			else if (!strcmp(name, "first_person:dual:ammunition"))
				set = 1;
			else
				continue;
			if (!h2w_animation_decode(build, animations + 0x60 * i, node_count, &s))
				continue;
			for (n = 0; n < graph->node_count; n++)
			{
				short from = graph->nodes[n].source;

				ammunition_nodes[set][n] = (s.rotation[from] && s.rotation_animated[from]) ||
					(s.translation[from] && s.translation_animated[from]) || (s.scale[from] && s.scale_animated[from]);
			}
			h2w_samples_free(&s);
		}
	}
	for (i = 0; i < animation_count && graph->animation_count < H2W_MAXIMUM_ANIMATIONS; i++)
	{
		unsigned char const *e = h2_cache_at(cache, animations + 0x60 * i, 0x60);
		char name[64], state[64];
		char const *colon;
		short slot = NONE, type;
		boolean replacement = FALSE;
		struct h2w_samples s;
		struct h2w_animation *a;
		short set = 0;

		if (!e)
			continue;
		h2_cache_string(cache, read32(e), name, sizeof(name));
		type = e[0x10];
		/* (HALO_H2_DEBUG=3: every animation of the graph, made or not) */
		if (getenv("HALO_H2_DEBUG") && atoi(getenv("HALO_H2_DEBUG")) >= 3)
			fprintf(stderr, "h2 weapons: all %s %ld '%s' type %d frames %d\n", h2_cache_tag_name(cache, jmad), i, name, (int)type,
				(int)h2w_read_short(cache, animations + 0x60 * i + 0x14));
		/* first_person:[dual:]<state>[:var<n>] */
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
		/* (this engine's kind: an overlay's slots are overlays; Halo 2's
		ammunition a replacement, made one - h2w_replacement_to_overlay) */
		if (type == 2 && slot != 17)
			continue;
		if (slot == 17 && type == 2)
			replacement = TRUE;
		else if ((slot == 3 || slot == 4 || slot == 15 || slot == 17 || slot == H2W_PITCH_AND_TURN_SLOT) != (type == 1))
			continue;
		/* (pitch_and_turn: its blend screen's deltas, one frame each way -
		the 3x3 first_person_weapons.c reads) */
		if (slot == H2W_PITCH_AND_TURN_SLOT)
		{
			long screen_count;
			unsigned long screens;
			int screen = (signed char)e[0x12];
			unsigned char const *b;

			if (screen < 0 || !h2_cache_block(cache, t.address + 0x24, 0x1C, &screen_count, &screens) || screen >= screen_count ||
				!(b = h2_cache_at(cache, screens + 0x1C * screen, 0x1C)) || read16(b + 12) != 1 || read16(b + 14) != 1 ||
				read16(b + 24) != 1 || read16(b + 26) != 1 || read16(e + 0x14) != 9)
			{
				continue;
			}
			graph->pitch_and_turn_screen[set][0] = readf(b + 4);
			graph->pitch_and_turn_screen[set][1] = readf(b + 8);
			graph->pitch_and_turn_screen[set][2] = readf(b + 16);
			graph->pitch_and_turn_screen[set][3] = readf(b + 20);
		}
		if (!h2w_animation_decode(build, animations + 0x60 * i, node_count, &s))
		{
			if (getenv("HALO_H2_DEBUG"))
				fprintf(stderr, "h2 weapons: animation %s not decoded\n", name);
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
		if (replacement)
		{
			if (!h2w_replacement_to_overlay(&s, graph, model, rounds))
			{
				h2w_samples_free(&s);
				continue;
			}
			type = 1;
		}
		else if (type == 0)
		{
			/* (a base pose: the ammunition's nodes at the model's - above) */
			long n;

			for (n = 0; n < graph->node_count; n++)
			{
				if (!ammunition_nodes[set][n])
					continue;
				free(s.rotation[n]);
				free(s.translation[n]);
				free(s.scale[n]);
				s.rotation[n] = s.translation[n] = s.scale[n] = NULL;
				s.rotation_animated[n] = s.translation_animated[n] = s.scale_animated[n] = FALSE;
			}
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
		/* its first sound event (0x48, 8 bytes each: the jmad's sound
		reference, the frame), its sound one of the weapons' */
		a->sound = a->graph_sound = NONE;
		{
			long event_count, sound_count;
			unsigned long events, sound_references;

			if (h2_cache_block(cache, animations + 0x60 * i + 0x48, 8, &event_count, &events) && event_count > 0 &&
				h2_cache_block(cache, t.address + 0x14, 0xC, &sound_count, &sound_references))
			{
				short reference = h2w_read_short(cache, events);
				unsigned char const *q = reference >= 0 && reference < sound_count ?
					h2_cache_at(cache, sound_references + 0xC * reference, 8) : NULL;

				a->sound_frame = h2w_read_short(cache, events + 2);
				if (q)
					a->sound = h2w_sound(cache, h2_cache_tag_from_handle(cache, read32(q + 4)));
				if (a->sound != NONE)
				{
					long k2;

					for (k2 = 0; k2 < graph->sound_count && graph->sounds[k2] != a->sound; k2++)
						;
					if (k2 == graph->sound_count && graph->sound_count < (long)(sizeof(graph->sounds) / sizeof(graph->sounds[0])))
						graph->sounds[graph->sound_count++] = a->sound;
					a->graph_sound = k2 < graph->sound_count ? (short)k2 : NONE;
				}
			}
		}
		/* its primary key frame (frame events at 0x40, 4 bytes each: type 0) -
		a melee's strike */
		{
			long event_count, e;
			unsigned long events;

			a->second_key_frame = NONE;
			if (h2_cache_block(cache, animations + 0x60 * i + 0x40, 4, &event_count, &events))
			{
				for (e = event_count - 1; e >= 0; e--)
				{
					/* (type 0 the primary key frame, 1 the secondary: the
					first of each) */
					if (h2w_read_short(cache, events + 4 * e) == 0)
						a->key_frame = h2w_read_short(cache, events + 4 * e + 2);
					else if (h2w_read_short(cache, events + 4 * e) == 1)
						a->second_key_frame = h2w_read_short(cache, events + 4 * e + 2);
				}
			}
		}
		/* (a variant: the slot's list, its weights shared) */
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
	/* the variants' normalized weights (their sums so far) and parents */
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
	/* (dual wielded: its own idle, or none; a slot it lacks the single
	one's - a melee, a grenade throw) */
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
	/* (a weapon with no idle has none drawn) */
	if (graph->slots[0][0] == NONE)
	{
		h2w_graph_free(graph);
		return NULL;
	}
	if (getenv("HALO_H2_DEBUG"))
	{
		fprintf(stderr, "h2 weapons: graph %s: %ld nodes, %ld animations:", h2_cache_tag_name(cache, jmad), graph->node_count,
			graph->animation_count);
		for (i = 0; i < graph->animation_count; i++)
			fprintf(stderr, " %s(%d,%d frames)", graph->animations[i].name, graph->animations[i].slot, graph->animations[i].frame_count);
		fprintf(stderr, "\n");
	}
	return graph;
}

/* the graph's tag (root at root) */
H2_ALIGN static void h2w_graph_write(struct region *r, unsigned long root, struct h2w_graph const *graph, long sound_first)
{
	unsigned long first_person, slots, nodes, animations;
	long i, k;
	short parents[H2W_MAXIMUM_ANIMATIONS];

	/* (a second set of slots: dual wielded - first_person_weapons.c) */
	first_person = region_block(r, root + offsetof(struct animation_graph, first_person_weapon_animations), graph->dual ? 2 : 1, 0x1C);
	for (i = 0; i < (graph->dual ? 2 : 1) && !r->b.failed; i++)
	{
		slots = region_block(r, first_person + 0x1C * i + 0x10, H2W_FIRST_PERSON_SLOTS, 2);
		/* (pitch_and_turn's screen in the set's unused words) */
		if (!r->b.failed && graph->slots[i][H2W_PITCH_AND_TURN_SLOT] != NONE)
		{
			int w;

			for (w = 0; w < 4; w++)
				writef(region_at(r, first_person + 0x1C * i + 4 * w), graph->pitch_and_turn_screen[i][w]);
		}
		for (k = 0; k < H2W_FIRST_PERSON_SLOTS && !r->b.failed; k++)
			write16(region_at(r, slots + 2 * k), graph->slots[i][k]);
	}
	nodes = region_block(r, root + offsetof(struct animation_graph, nodes), graph->node_count, 0x40);
	for (i = 0; i < graph->node_count && !r->b.failed; i++)
	{
		unsigned char *o = region_at(r, nodes + 0x40 * i);

		memcpy(o, graph->nodes[i].name, strlen(graph->nodes[i].name) + 1);
		write16(o + 0x20, graph->nodes[i].sibling);
		write16(o + 0x22, graph->nodes[i].child);
		write16(o + 0x24, graph->nodes[i].parent);
	}
	/* its sounds: the weapons' own sound tags */
	if (sound_first != NONE && graph->sound_count > 0)
	{
		unsigned long references = region_block(r, root + offsetof(struct animation_graph, sound_references), graph->sound_count,
			0x14);

		for (i = 0; i < graph->sound_count && !r->b.failed; i++)
			region_reference(r, references + 0x14 * i, 'snd!', tag_id(sound_first + graph->sounds[i]));
	}
	animations = region_block(r, root + offsetof(struct animation_graph, animations), graph->animation_count, sizeof(struct animation));
	/* (each one's runtime parent: the first of its permutations, which
	model_animations.c's animation_advance starts again from when one that
	does not loop ends - none left the weapon with no animation) */
	for (i = 0; i < graph->animation_count && i < H2W_MAXIMUM_ANIMATIONS; i++)
		parents[i] = (short)i;
	for (i = 0; i < graph->animation_count && i < H2W_MAXIMUM_ANIMATIONS; i++)
	{
		if (graph->animations[i].next > i && graph->animations[i].next < graph->animation_count &&
			graph->animations[i].next < H2W_MAXIMUM_ANIMATIONS)
		{
			parents[graph->animations[i].next] = parents[i];
		}
	}
	for (i = 0; i < graph->animation_count && !r->b.failed; i++)
	{
		struct h2w_animation const *a = &graph->animations[i];
		unsigned long o = animations + sizeof(struct animation) * i;
		int w;

		memcpy(region_at(r, o), a->name, strlen(a->name) + 1);
		write16(region_at(r, o + offsetof(struct animation, type)), a->type);
		write16(region_at(r, o + offsetof(struct animation, frame_count)), a->frame_count);
		write16(region_at(r, o + offsetof(struct animation, frame_size)), a->frame_size);
		write16(region_at(r, o + offsetof(struct animation, node_count)), a->node_count);
		write16(region_at(r, o + offsetof(struct animation, private_loop_frame_index)), 0);
		writef(region_at(r, o + offsetof(struct animation, weight)), a->weight);
		write16(region_at(r, o + offsetof(struct animation, private_key_frame_index)), a->key_frame);
		write16(region_at(r, o + offsetof(struct animation, private_second_key_frame_index)), a->second_key_frame);
		write16(region_at(r, o + offsetof(struct animation, next_animation_index)), a->next);
		write16(region_at(r, o + offsetof(struct animation, sound_index)), sound_first != NONE && a->graph_sound != NONE ? a->graph_sound : NONE);
		write16(region_at(r, o + offsetof(struct animation, private_sound_frame_index)),
			sound_first != NONE && a->graph_sound != NONE ? (a->sound_frame >= 0 && a->sound_frame < a->frame_count ? a->sound_frame : 0) : NONE);
		region_at(r, o + offsetof(struct animation, private_left_foot_frame_index))[0] = 0xFF;
		region_at(r, o + offsetof(struct animation, private_right_foot_frame_index))[0] = 0xFF;
		write16(region_at(r, o + offsetof(struct animation, runtime_parent_animation_index)),
			a->parent != NONE ? a->parent : i < H2W_MAXIMUM_ANIMATIONS ? parents[i] : (short)i);
		writef(region_at(r, o + offsetof(struct animation, runtime_normalized_weight)), a->normalized_weight);
		for (w = 0; w < 2; w++)
		{
			write32(region_at(r, o + offsetof(struct animation, nodes_with_translation_flags) + 4 * w), a->translation_flags[w]);
			write32(region_at(r, o + offsetof(struct animation, nodes_with_rotation_flags) + 4 * w), a->rotation_flags[w]);
			write32(region_at(r, o + offsetof(struct animation, nodes_with_scale_flags) + 4 * w), a->scale_flags[w]);
		}
		if (a->default_size)
			region_data(r, o + offsetof(struct animation, default_data), a->defaults, a->default_size);
		if (a->frame_bytes)
			region_data(r, o + offsetof(struct animation, data), a->frames, a->frame_bytes);
	}
}

/* ---------- private code: the weapons */

/* (h2_effects.c) the Halo 2 material names that stand for this engine's
material type on an impact, k-th (0..2), NULL past them */
H2_ALIGN static char const *h2fx_material_stand_in(short type, long k);

/* the global material (matg @150, 0xB4 each: its name 0, its parent's
index 8, its old material type 0xC) that stands for each of this engine's
material types: the first of h2_effects.c's names for it the map has (the
material its impacts' effects are found by), else the first whose old
material type it is - Halo 2's own tie between the two -; NONE none. Made
once a map (the map's globals and their first material's name its key) */
H2_ALIGN static short const *h2w_material_stand_ins(struct h2_cache *cache, unsigned long *materials_out, long *count_out)
{
	static struct h2_cache *key_cache;
	static unsigned long key_address, key_first, materials;
	static long count;
	static short stand_in[H2W_MATERIAL_TYPES];
	long matg = h2_cache_tag_find(cache, 'matg', "globals\\globals");
	struct h2_cache_tag t;
	unsigned long first;
	short type;

	*materials_out = 0;
	*count_out = 0;
	if (matg == NONE || !h2_cache_tag_get(cache, matg, &t))
		return NULL;
	first = h2_cache_read32(cache, t.address + 0x150 + 4);
	if (key_cache != cache || key_address != t.address || key_first != first)
	{
		key_cache = cache;
		key_address = t.address;
		key_first = first;
		if (!h2_cache_block(cache, t.address + 0x150, 0xB4, &count, &materials))
			count = 0, materials = 0;
		for (type = 0; type < H2W_MATERIAL_TYPES; type++)
		{
			long k, m;
			char const *want;

			stand_in[type] = NONE;
			/* (a character's type: the body material its model's damage
			info names, not its flesh. Halo 2 takes a hit's response from
			the hit region's material - an Elite's armored parts are
			hard_metal_thin_cov_elite (an impact), its skinned ones
			soft_organic_flesh_elite (a Battle Rifle round passes through,
			projectiles.cpp's overpenetrate); this engine's hit carries one
			type for the whole Elite, which h2_effects.c's table stands in
			for with its flesh (blue blood). The body material - the Elite's
			armor - is the region most of it is (4 of its 8 materials, the
			head, arms, torso and legs) and the material Halo 2's damage
			info gives its body (read from the Elite's own model); the Grunt's, Jackal's, Marine's body
			material is their flesh, as their tables have it) */
			if (type == _material_elite)
			{
				/* (the Elite's model - its biped's hlmt at 0x34 - damage
				info at 0x60, its body material at 4) */
				long biped = h2_cache_tag_find(cache, 'bipd', "objects\\characters\\elite\\elite_mp");
				struct h2_cache_tag b, model;
				long info_count = 0;
				unsigned long info = 0, body = 0;

				if (biped == NONE)
					biped = h2_cache_tag_find(cache, 'bipd', "objects\\characters\\elite\\elite");
				if (biped != NONE && h2_cache_tag_get(cache, biped, &b) && h2_cache_tag_get(cache, h2_cache_reference(cache, b.address + 0x34), &model) &&
					h2_cache_block(cache, model.address + 0x60, 0xF8, &info_count, &info) && info_count > 0)
				{
					body = h2_cache_read32(cache, info + 4);
				}
				for (m = 0; body && m < count && stand_in[type] == NONE; m++)
				{
					if (h2_cache_read32(cache, materials + 0xB4 * (unsigned long)m) == body)
						stand_in[type] = (short)m;
				}
			}
			for (k = 0; stand_in[type] == NONE && (want = h2fx_material_stand_in(type, k)) != NULL; k++)
			{
				for (m = 0; m < count && stand_in[type] == NONE; m++)
				{
					char name[64];

					if (h2_cache_string(cache, h2_cache_read32(cache, materials + 0xB4 * (unsigned long)m), name, sizeof(name)) &&
						!strcmp(name, want))
					{
						stand_in[type] = (short)m;
					}
				}
			}
			for (m = 0; m < count && stand_in[type] == NONE; m++)
			{
				if (h2w_read_short(cache, materials + 0xB4 * (unsigned long)m + 0xC) == type)
					stand_in[type] = (short)m;
			}
		}
	}
	*materials_out = materials;
	*count_out = count;
	return stand_in;
}

/* a Halo 2 projectile's (its proj at address) material responses read, and
the one each of this engine's material types gets: Halo 2's for the global
material standing for it (h2w_material_stand_ins), found as projectiles.cpp's
projectile_get_material_response finds it - the material's own, else its
parent's, and so on up; none at all: Halo 2's zero response, an impact */
H2_ALIGN static void h2w_responses_read(struct h2_cache *cache, unsigned long address, struct h2w_responses *out)
{
	long count, i, material_count;
	unsigned long block, materials;
	short const *stand_in;
	short type;

	memset(out, 0, sizeof(*out));
	for (type = 0; type < H2W_MATERIAL_TYPES; type++)
		out->of_type[type] = H2W_RESPONSE_STAND_IN;
	if (!h2_cache_block(cache, address + 0x19C, 0x58, &count, &block) || count < 1)
		return;
	for (i = 0; i < count && i < H2W_MAXIMUM_RESPONSES; i++)
	{
		unsigned long at = block + 0x58 * (unsigned long)i;
		struct h2w_response *o = &out->responses[i];

		o->flags = (unsigned short)h2w_read_short(cache, at);
		o->response = h2w_read_short(cache, at + 2);
		o->material = h2w_read_short(cache, at + 0x10);
		o->potential = h2w_read_short(cache, at + 0x14);
		o->potential_flags = (unsigned short)h2w_read_short(cache, at + 0x16);
		o->chance = h2w_read_real(cache, at + 0x18);
		o->angles[0] = h2w_read_real(cache, at + 0x1C);
		o->angles[1] = h2w_read_real(cache, at + 0x20);
		o->velocities[0] = h2w_read_real(cache, at + 0x24);
		o->velocities[1] = h2w_read_real(cache, at + 0x28);
		o->scale_by = h2w_read_short(cache, at + 0x34);
		o->angular_noise = h2w_read_real(cache, at + 0x38);
		o->velocity_noise = h2w_read_real(cache, at + 0x3C);
		o->initial_friction = h2w_read_real(cache, at + 0x48);
		o->maximum_distance = h2w_read_real(cache, at + 0x4C);
		o->frictions[0] = h2w_read_real(cache, at + 0x50);
		o->frictions[1] = h2w_read_real(cache, at + 0x54);
	}
	out->count = (short)i;
	if (count > H2W_MAXIMUM_RESPONSES)
		platform_log("h2 weapons: a projectile with %ld material responses: the first %d kept", count, (int)H2W_MAXIMUM_RESPONSES);
	if (!(stand_in = h2w_material_stand_ins(cache, &materials, &material_count)))
		return;
	for (type = 0; type < H2W_MATERIAL_TYPES; type++)
	{
		short material = stand_in[type];
		long depth;

		if (material == NONE)
			continue;
		out->of_type[type] = H2W_RESPONSE_NONE;
		for (depth = 0; depth < 32 && material >= 0 && material < material_count; depth++)
		{
			for (i = 0; i < out->count && out->responses[i].material != material; i++)
				;
			if (i < out->count)
			{
				out->of_type[type] = (signed char)i;
				break;
			}
			material = h2w_read_short(cache, materials + 0xB4 * (unsigned long)material + 8);
		}
	}
}

/* Halo 2's response (impact, fizzle, overpenetrate, attach, bounce, bounce
as a dud, fizzle with a ricochet - projectiles.cpp's switch on it) as this
engine's (disappear, detonate, reflect, overpenetrate, attach) */
H2_ALIGN static short h2w_response_type(short response)
{
	static short const types[] =
	{
		_projectile_material_response_detonate, _projectile_material_response_disappear, _projectile_material_response_overpenetrate,
		_projectile_material_response_attach, _projectile_material_response_reflect, _projectile_material_response_reflect,
		_projectile_material_response_disappear,
	};

	return response >= 0 && response < (short)NUMBEROF(types) ? types[response] : _projectile_material_response_detonate;
}

/* a projectile's material responses (the stand-in's, root's: a material
type each) Halo 2's (h2w_responses_read's): what each does - its response,
its potential one and when, the noises, frictions and how far it passes
through -; their effects stay (h2_effects.c writes Halo 2's by the response
each has now). HALO_H2_RESPONSES_OLD: the stand-in's as they were */
H2_ALIGN static void h2w_responses_write(struct region *r, unsigned long root, struct h2w_responses const *in)
{
	static struct h2w_response const zero;
	long count, e;
	unsigned long responses;

	if (!in->count || getenv("HALO_H2_RESPONSES_OLD"))
		return;
	count = (long)read32(region_at(r, root + offsetof(struct projectile_definition, projectile.material_responses)));
	responses = read32(region_at(r, root + offsetof(struct projectile_definition, projectile.material_responses) + 4));
	if (!responses)
		return;
	for (e = 0; e < count && e < H2W_MATERIAL_TYPES; e++)
	{
		unsigned long at = responses - r->link + sizeof(struct projectile_material_response_definition) * (unsigned long)e;
		struct h2w_response const *h = in->of_type[e] >= 0 ? &in->responses[(long)in->of_type[e]] : &zero;
		short potential = h2w_response_type(h->potential);

		if (in->of_type[e] == H2W_RESPONSE_STAND_IN)
			continue;
#define H2W_RESPONSE_AT(field) region_at(r, at + offsetof(struct projectile_material_response_definition, field))
		/* ("cannot be overpenetrated", bit 0 in both) */
		write16(H2W_RESPONSE_AT(flags), h->flags & 1);
		write16(H2W_RESPONSE_AT(default_response), h2w_response_type(h->response));
		/* (the potential one: Halo 2's a chance of it, this engine's a
		fraction skipped; none - no chance, or this engine's response 0,
		which it takes as none - skipped always. "Only against units" bit 0
		in both; Halo 2's "never against units", bit 1, kept for projectiles.c:
		a needle's bounce is never taken against a unit - it sticks) */
		write16(H2W_RESPONSE_AT(potential_response), potential);
		write16(H2W_RESPONSE_AT(potential_flags), h->potential_flags & 3);
		writef(H2W_RESPONSE_AT(potential_skip_fraction), h->chance > 0.0f && potential ? (h->chance >= 1.0f ? 0.0f : 1.0f - h->chance) : 1.0f);
		writef(H2W_RESPONSE_AT(potential_angle_lower_bound), h->angles[0]);
		writef(H2W_RESPONSE_AT(potential_angle_upper_bound), h->angles[1]);
		/* (speeds: Halo 2's world units a second, this engine's a tick) */
		writef(H2W_RESPONSE_AT(potential_velocity_lower_bound), h->velocities[0] / 30.0f);
		writef(H2W_RESPONSE_AT(potential_velocity_upper_bound), h->velocities[1] / 30.0f);
		write16(H2W_RESPONSE_AT(scale_effects_by), h->scale_by);
		writef(H2W_RESPONSE_AT(angular_noise), h->angular_noise);
		writef(H2W_RESPONSE_AT(velocity_noise), h->velocity_noise / 30.0f);
		writef(H2W_RESPONSE_AT(initial_friction), h->initial_friction);
		writef(H2W_RESPONSE_AT(maximum_distance), h->maximum_distance);
		writef(H2W_RESPONSE_AT(parallel_friction), h->frictions[0]);
		writef(H2W_RESPONSE_AT(perpendicular_friction), h->frictions[1]);
#undef H2W_RESPONSE_AT
	}
	if (getenv("HALO_H2_RESPONSES_LOG"))
	{
		char line[H2W_MATERIAL_TYPES * 4 + 1];

		for (e = 0; e < H2W_MATERIAL_TYPES; e++)
		{
			short type = e < count ? (short)read16(region_at(r, responses - r->link +
				sizeof(struct projectile_material_response_definition) * (unsigned long)e +
				offsetof(struct projectile_material_response_definition, default_response))) : -1;

			line[e] = in->of_type[e] == H2W_RESPONSE_STAND_IN ? '.' : "xdrpa"[type >= 0 && type < 5 ? type : 1];
		}
		line[H2W_MATERIAL_TYPES] = 0;
		platform_log("h2 weapons: responses %s (%d of Halo 2's)", line, (int)in->count);
	}
}

/* a Halo 2 projectile's fields (MCC's proj layout); p->tag NONE if none */
H2_ALIGN static void h2w_projectile_read(struct h2_cache *cache, long tag, struct h2w_projectile *p)
{
	struct h2_cache_tag t;

	memset(p, 0, sizeof(*p));
	p->tag = NONE;
	p->detonation_damage = p->attached_detonation_damage = p->super_detonation_damage = p->attached_super_detonation_damage =
		p->impact_damage = NONE;
	if (tag == NONE || !h2_cache_tag_get(cache, tag, &t))
		return;
	p->tag = tag;
	p->flags = h2_cache_read32(cache, t.address + 0xBC);
	p->timer_starts = h2w_read_short(cache, t.address + 0xC0);
	p->ai_perception_radius = h2w_read_real(cache, t.address + 0xC4);
	p->collision_radius = h2w_read_real(cache, t.address + 0xC8);
	p->arming_time = h2w_read_real(cache, t.address + 0xCC);
	p->danger_radius = h2w_read_real(cache, t.address + 0xD0);
	p->timer[0] = h2w_read_real(cache, t.address + 0xD4);
	p->timer[1] = h2w_read_real(cache, t.address + 0xD8);
	p->minimum_velocity = h2w_read_real(cache, t.address + 0xDC);
	p->maximum_range = h2w_read_real(cache, t.address + 0xE0);
	/* (the detonation noise a short at 0xE4, the super detonation's count
	of stuck projectiles at 0xE6 - projectiles.cpp's definition + 0xe6) */
	p->super_detonation_count = h2w_read_short(cache, t.address + 0xE6);
	p->detonation_damage = h2_cache_reference(cache, t.address + 0x100);
	p->attached_detonation_damage = h2_cache_reference(cache, t.address + 0x108);
	p->super_detonation_damage = h2_cache_reference(cache, t.address + 0x118);
	p->attached_super_detonation_damage = h2_cache_reference(cache, t.address + 0x12C);
	p->impact_damage = h2_cache_reference(cache, t.address + 0x148);
	p->air_gravity_scale = h2w_read_real(cache, t.address + 0x164);
	p->air_damage_range[0] = h2w_read_real(cache, t.address + 0x168);
	p->air_damage_range[1] = h2w_read_real(cache, t.address + 0x16C);
	p->water_gravity_scale = h2w_read_real(cache, t.address + 0x170);
	p->water_damage_range[0] = h2w_read_real(cache, t.address + 0x174);
	p->water_damage_range[1] = h2w_read_real(cache, t.address + 0x178);
	p->velocity[0] = h2w_read_real(cache, t.address + 0x17C);
	p->velocity[1] = h2w_read_real(cache, t.address + 0x180);
	p->guided_angular_velocity[0] = h2w_read_real(cache, t.address + 0x184);
	p->guided_angular_velocity[1] = h2w_read_real(cache, t.address + 0x188);
	p->acceleration_range[0] = h2w_read_real(cache, t.address + 0x18C);
	p->acceleration_range[1] = h2w_read_real(cache, t.address + 0x190);
	h2w_responses_read(cache, t.address, &p->responses);
	platform_log("h2 weapons: %s: flags %lx, timer %d %.2f..%.2f, range %.0f, super %d, velocity %.1f..%.1f over %.0f..%.0f, "
		"guided %.1f..%.1f deg/s, damage range %.1f..%.1f, gravity %.2f; damage %s%s%s%s", tag_short_name(h2_cache_tag_name(cache, tag)),
		p->flags, (int)p->timer_starts, p->timer[0], p->timer[1], p->maximum_range, (int)p->super_detonation_count, p->velocity[0], p->velocity[1],
		p->acceleration_range[0], p->acceleration_range[1], p->guided_angular_velocity[0] * 57.29578f, p->guided_angular_velocity[1] * 57.29578f,
		p->air_damage_range[0], p->air_damage_range[1], p->air_gravity_scale,
		p->impact_damage != NONE ? "impact " : "", p->detonation_damage != NONE ? "detonation " : "",
		p->attached_detonation_damage != NONE ? "attached " : "", p->super_detonation_damage != NONE ? "super" : "");
}

/* a Halo 2 weapon's (its weap at address) barrels: the one its first
trigger fires (the trigger's primary barrel, @8 of its 0x40; Halo 2's
latch-zoom fires its secondary, @A, when zoomed - the carbine's and the
battle rifle's the same barrel) and the second a trigger fires (the first
trigger's secondary barrel where it is another - the plasma pistol's
overcharge, the rocket launcher's locked rocket, the horn's charge -, else
the second trigger's primary - the Banshee's fuel rod, the Scorpion's
machine gun -; none: no trigger fires another - the flak cannon's second
barrel, empty in MCC, was taken as the barrel after the first), from the
barrels (@2D0, 0xEC each) */
H2_ALIGN static void h2w_barrels_read(struct h2_cache *cache, unsigned long address, struct h2w_weapon *w)
{
	long count, trigger_count;
	unsigned long barrels, triggers;
	short primary = 0, secondary = NONE;

	w->barrel = 0;
	w->barrel_index = w->second_barrel_index = NONE;
	if (!h2_cache_block(cache, address + 0x2D0, 0xEC, &count, &barrels) || count < 1)
		return;
	if (h2_cache_block(cache, address + 0x2C8, 0x40, &trigger_count, &triggers) && trigger_count > 0)
	{
		primary = h2w_read_short(cache, triggers + 8);
		secondary = h2w_read_short(cache, triggers + 0xA);
		if (primary < 0 || primary >= count)
			primary = 0;
		if ((secondary < 0 || secondary >= count || secondary == primary) && trigger_count > 1)
			secondary = h2w_read_short(cache, triggers + 0x40 + 8);
	}
	w->barrel_index = primary;
	w->barrel = barrels + 0xEC * (unsigned long)primary;
	if (secondary >= 0 && secondary < count && secondary != primary)
		w->second_barrel_index = secondary;
}

/* Halo 2's weapon of a kind made; NULL if it cannot be */
/* the barrel's fields items/weapons.c reads through h2_weapon_fields_get
(w->barrel, h2w_barrels_read's): dual wielded error and damage scale, recoil
- read as a level is made and as one is loaded (h2_weapon_fields_map_loaded) */
H2_ALIGN static void h2w_barrel_rules_read(struct h2_cache *cache, struct h2w_weapon *w)
{
	unsigned long block = w->barrel;

	w->dual_error_times[0] = h2w_read_real(cache, block + 0x48);
	w->dual_error_times[1] = h2w_read_real(cache, block + 0x4C);
	w->dual_error_minimum = h2w_read_real(cache, block + 0x58);
	w->dual_error_angle[0] = h2w_read_real(cache, block + 0x5C);
	w->dual_error_angle[1] = h2w_read_real(cache, block + 0x60);
	w->dual_damage_scale = h2w_read_real(cache, block + 0x64);
	if (getenv("HALO_H2_DUAL_LOG"))
		fprintf(stderr, "h2 weapons: barrel %08lx: dual error %.4f..%.4f (minimum %.4f), single %.4f..%.4f (minimum %.4f)\n", block,
			w->dual_error_angle[0], w->dual_error_angle[1], w->dual_error_minimum, h2w_read_real(cache, block + 0x74),
			h2w_read_real(cache, block + 0x78), h2w_read_real(cache, block + 0x70));
	w->recoil_angle[0] = h2w_read_real(cache, block + 0xB0);
	w->recoil_angle[1] = h2w_read_real(cache, block + 0xB4);
	w->recoil_times[0] = h2w_read_real(cache, block + 0xB8);
	w->recoil_times[1] = h2w_read_real(cache, block + 0xBC);
	w->recoil_function = h2w_read_short(cache, block + 0xC0);
}

/* its heat (weap @150, seven reals: recovery, overheated and detonation
thresholds, explosion fraction, loss a second, illumination, loss a second
while overheated) */
H2_ALIGN static void h2w_heat_read(struct h2_cache *cache, unsigned long address, struct h2w_weapon *w)
{
	long i;

	for (i = 0; i < 7; i++)
		w->heat[i] = h2w_read_real(cache, address + 0x150 + 4 * i);
}

/* a Halo 2 weapon's (its weap at address) values - its magazine, trigger,
barrels, projectile and damage, heat, age, melee, zoom, aim assist,
movement penalties, first-person offset: what h2_weapons_write writes of it
that is not a model, graph, sound or effect - read as a level is made and
as one is loaded (h2_weapons_map_loaded) */
H2_ALIGN static void h2w_weapon_values_read(struct h2_cache *cache, unsigned long address, long kind, struct h2w_weapon *w)
{
	struct h2_cache_tag model_tag;
	long i, count;
	unsigned long block;

	/* its fields: magazine (0x2C0), trigger (0x2C8), barrel (0x2D0) */
	if (h2_cache_block(cache, address + 0x2C0, 0x5C, &count, &block) && count > 0)
	{
		w->rounds_initial = h2w_read_short(cache, block + 6);
		w->rounds_maximum = h2w_read_short(cache, block + 8);
		w->rounds_loaded = h2w_read_short(cache, block + 0xA);
		w->rounds_held = h2w_read_short(cache, block + 0xC);
		w->reload_time = h2w_read_real(cache, block + 0x10);
		w->rounds_reloaded = h2w_read_short(cache, block + 0x14);
		{
			long box_count;
			unsigned long boxes;

			if (h2_cache_block(cache, block + 0x54, 0xC, &box_count, &boxes) && box_count > 0)
			{
				w->rounds_box = h2w_read_short(cache, boxes);
				w->ammo_tag = h2_cache_reference(cache, boxes + 4);
			}
		}
	}
	w->ready_time = h2w_read_real(cache, address + 0x13C);
	w->ground_scale = h2w_read_real(cache, address + 0xC4);
	if (h2_cache_block(cache, address + 0x2C8, 0x40, &count, &block) && count > 0)
		w->trigger_behavior = h2w_read_short(cache, block + 6);
	h2w_barrels_read(cache, address, w);
	if (w->barrel)
	{
		long projectile;

		block = w->barrel;

		w->barrel_flags = h2_cache_read32(cache, block);
		w->rounds_per_second = h2w_read_real(cache, block + 4);
		w->rounds_per_second_maximum = h2w_read_real(cache, block + 8);
		w->rate_times[0] = h2w_read_real(cache, block + 0xC);
		w->rate_times[1] = h2w_read_real(cache, block + 0x10);
		w->burst_shots = h2w_read_short(cache, block + 0x1C);
		w->burst_recovery = h2w_read_real(cache, block + 0x20);
		w->error_angle[0] = h2w_read_real(cache, block + 0x74);
		w->error_angle[1] = h2w_read_real(cache, block + 0x78);
		w->error_times[0] = h2w_read_real(cache, block + 0x38);
		w->error_times[1] = h2w_read_real(cache, block + 0x3C);
		w->error_minimum = h2w_read_real(cache, block + 0x70);
		h2w_barrel_rules_read(cache, w);
		w->projectiles_per_shot = h2w_read_short(cache, block + 0x6A);
		w->rounds_between_tracers = h2w_read_short(cache, block + 0x2E);
		w->heat_per_round = h2w_read_real(cache, block + 0xA4);
		w->age_per_round = h2w_read_real(cache, block + 0xA8);
		for (i = 0; i < 3; i++)
			w->barrel_offset[i] = h2w_read_real(cache, block + 0x7C + 4 * i);
		w->distribution_angle = h2w_read_real(cache, block + 0x6C);
		projectile = h2_cache_reference(cache, block + 0x8C);
		h2w_projectile_read(cache, projectile, &w->projectiles[0]);
		if (w->second_barrel_index != NONE)
		{
			unsigned long barrels = w->barrel - 0xEC * (unsigned long)w->barrel_index;

			h2w_projectile_read(cache, h2_cache_reference(cache, barrels + 0xEC * (unsigned long)w->second_barrel_index + 0x8C),
				&w->projectiles[1]);
		}
		if (projectile != NONE && h2_cache_tag_get(cache, projectile, &model_tag))
		{
			long damage = h2_cache_reference(cache, model_tag.address + 0x148);
			struct h2_cache_tag d;

			w->projectile_range = h2w_read_real(cache, model_tag.address + 0xE0);
			w->projectile_gravity = h2w_read_real(cache, model_tag.address + 0x164);
			w->projectile_velocity[0] = h2w_read_real(cache, model_tag.address + 0x17C);
			w->projectile_velocity[1] = h2w_read_real(cache, model_tag.address + 0x180);
			if (damage != NONE && h2_cache_tag_get(cache, damage, &d))
			{
				w->damage_tag = damage;
				w->damage[0] = h2w_read_real(cache, d.address + 0x1C);
				w->damage[1] = h2w_read_real(cache, d.address + 0x20);
				w->damage[2] = h2w_read_real(cache, d.address + 0x24);
				w->damage_acceleration = h2w_read_real(cache, d.address + 0x40);
				w->damage_flags = h2_cache_read32(cache, d.address + 0x14);
			}
		}
	}
	h2w_heat_read(cache, address, w);
	for (i = 0; i < 4; i++)
		w->age_penalties[i] = h2w_read_real(cache, address + 0x254 + 4 * i);
	/* its melee: the damage pyramid (0x1B0 width, 0x1B4 height, 0x1B8
	depth: the width and height its extents at that depth in world units -
	the kit's angles stored as tan(angle) x depth -, used halved over five
	rays each way as unit_object_type.cpp function_cf3d0 does, units.c), its first hit's damage (0x1BC) and its lunge's (0x1EC) */
	for (i = 0; i < 3; i++)
		w->melee_pyramid[i] = h2w_read_real(cache, address + 0x1B0 + 4 * i);
	for (i = 0; i < 2; i++)
	{
		long damage = h2_cache_reference(cache, address + (i ? 0x1EC : 0x1BC));
		float *hit = i ? w->lunge_hit : w->melee_hit;
		struct h2_cache_tag d;

		/* (a strike's entry - damage, then its self damage -, each
		missing one the weapon's own: 0x190 and 0x198, function_cf3d0. A
		lunge without its own damage strikes with its melee's here) */
		if (!i && damage == NONE)
			damage = h2_cache_reference(cache, address + 0x190);
		w->response_tags[i] = h2_cache_reference(cache, address + (i ? 0x1F4 : 0x1C4));
		if (w->response_tags[i] == NONE)
			w->response_tags[i] = h2_cache_reference(cache, address + 0x198);

		hit[0] = hit[1] = hit[2] = hit[3] = 0.0f;
		w->hit_tags[i] = NONE;
		w->hit_flags[i] = 0;
		if (damage != NONE && h2_cache_tag_get(cache, damage, &d))
		{
			w->hit_tags[i] = damage;
			hit[0] = h2w_read_real(cache, d.address + 0x1C);
			hit[1] = h2w_read_real(cache, d.address + 0x20);
			hit[2] = h2w_read_real(cache, d.address + 0x24);
			hit[3] = h2w_read_real(cache, d.address + 0x40);
			w->hit_flags[i] = h2_cache_read32(cache, d.address + 0x14);
		}
	}
	/* (a sword's lunge: at its aim assist's target - Halo 2's
	unknown_14b560.cpp: the target's autoaim level full, within the autoaim
	angle 0x208 and range 0x20C) */
	if (h2w_kinds[kind].sword)
	{
		w->lunge_angle = h2w_read_real(cache, address + 0x208);
		w->lunge_range = h2w_read_real(cache, address + 0x20C);
	}
	w->zoom_levels = h2w_read_short(cache, address + 0x1FE);
	/* (Halo 2's weapon flags: "can be dual wielded" bit 22) */
	{
		unsigned char const *f = h2_cache_at(cache, address + 0x12C, 4);

		w->h2_flags = f ? read32(f) : 0;
	}
	w->zoom[0] = h2w_read_real(cache, address + 0x200);
	w->zoom[1] = h2w_read_real(cache, address + 0x204);
	w->autoaim[0] = h2w_read_real(cache, address + 0x208);
	w->autoaim[1] = h2w_read_real(cache, address + 0x20C);
	w->magnetism[0] = h2w_read_real(cache, address + 0x210);
	w->magnetism[1] = h2w_read_real(cache, address + 0x214);
	w->deviation = h2w_read_real(cache, address + 0x218);
	w->movement_penalized = h2w_read_short(cache, address + 0x22C);
	w->movement_penalty[0] = h2w_read_real(cache, address + 0x230);
	w->movement_penalty[1] = h2w_read_real(cache, address + 0x234);
	for (i = 0; i < 3; i++)
		w->first_person_offset[i] = h2w_read_real(cache, address + 0x308 + 4 * i);
}

H2_ALIGN static struct h2w_weapon *h2w_weapon_make(struct build *build, long kind)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t, model_tag;
	struct h2w_weapon *w;
	long tag = NONE, i, count;
	unsigned long block;

	for (i = 0; i < h2_cache_tag_count(cache); i++)
	{
		struct h2_cache_tag c;

		if (h2_cache_tag_get(cache, i, &c) && c.group_tag == 'weap' && !strcmp(h2_cache_tag_name(cache, i), h2w_kinds[kind].h2))
		{
			tag = i;
			break;
		}
	}
	if (tag == NONE || !h2_cache_tag_get(cache, tag, &t) || !(w = calloc(1, sizeof(*w))))
		return NULL;
	w->kind = kind;
	w->h2_tag = tag;
	w->base_weapon = NONE;
	w->ammo_tag = w->ammo_slot = NONE;
	h2w_model_begin(&w->ammo_model);
	w->ammo_model.levels = TRUE;
	w->damage_tag = w->hit_tags[0] = w->hit_tags[1] = NONE;
	h2w_projectile_read(cache, NONE, &w->projectiles[0]);
	h2w_projectile_read(cache, NONE, &w->projectiles[1]);
	/* (its tracking, 0x294: none, human - the rocket's lock -, plasma) */
	w->tracking = h2w_read_short(cache, t.address + 0x294);
	snprintf(w->name, sizeof(w->name), "%s", tag_short_name(h2w_kinds[kind].h2));
	h2w_model_begin(&w->world);
	w->world.levels = TRUE;
	h2w_model_begin(&w->first_person);
	/* (the Elite's set begun below only where there is one: its model NONE
	before that, not 0 - h2_models_load.inc pairs the models by that) */
	h2w_model_begin(&w->first_person_elite);
	/* the world model (object 0x34: its hlmt, the render model first; an
	AI's only none - the Sentinels' beams: their body's gun) */
	if (!h2w_kinds[kind].ai_only && h2_cache_tag_get(cache, h2_cache_reference(cache, t.address + 0x34), &model_tag))
	{
		/* (the world model at Halo 2's low detail: a weapon held or lying is
		small on the Vita's screen) */
		if (h2w_model_add(build, &w->world, h2_cache_reference(cache, model_tag.address), "tens ones", h2w_detail("HALO_H2_WEAPON_DETAIL", H2W_WORLD_DETAIL_DEFAULT), FALSE))
			h2w_model_finish(build, &w->world);
	}
	/* the first-person model and graph (0x2A8: the Master Chief's first) */
	if (!h2w_kinds[kind].ai_only && h2_cache_block(cache, t.address + 0x2A8, 0x10, &count, &block) && count > 0)
	{
		long first_person_model = h2_cache_reference(cache, block);
		long jmad = h2_cache_reference(cache, block + 8);
		long arms = NONE;

		for (i = 0; i < h2_cache_tag_count(cache) && arms == NONE; i++)
		{
			struct h2_cache_tag c;

			if (h2_cache_tag_get(cache, i, &c) && c.group_tag == 'mode' &&
				!strcmp(h2_cache_tag_name(cache, i), "objects\\characters\\masterchief\\fp\\fp"))
			{
				arms = i;
			}
		}
		/* (its magazine's rounds: the ammunition animation's frames) */
		long magazine_count;
		unsigned long magazines;
		short rounds = h2_cache_block(cache, t.address + 0x2C0, 0x5C, &magazine_count, &magazines) && magazine_count > 0 ?
			h2w_read_short(cache, magazines + 0xA) : 0;

		if (arms != NONE && first_person_model != NONE && jmad != NONE &&
			h2w_model_add(build, &w->first_person, arms, NULL, 5, h2w_arms_fold(cache, arms, first_person_model)) &&
			h2w_model_add(build, &w->first_person, first_person_model, "tens ones", 5, FALSE))
		{
			w->graph = h2w_graph_make(build, jmad, &w->first_person, rounds);
			if (w->graph)
				h2w_model_finish(build, &w->first_person);
		}
		/* (an Arbiter level - 04a, 04b, 06a, 06b, 08a: no Master Chief's
		arms in the map - the Elite's entry is its first person: the
		dervish's arms, the gun, the dervish's animations; there was no
		weapon at all, the Xbox's Arbiter starts with the sword and the
		plasma rifle) */
		if (!w->graph && arms == NONE && count > 1)
		{
			long elite_model = h2_cache_reference(cache, block + 0x10);
			long elite_jmad = h2_cache_reference(cache, block + 0x10 + 8);
			long elite_arms = h2v_tag_named(cache, 'mode', "objects\\characters\\dervish\\fp\\fp");

			h2w_model_begin(&w->first_person);
			if (elite_arms != NONE && elite_model != NONE && elite_jmad != NONE &&
				h2w_model_add(build, &w->first_person, elite_arms, NULL, 5, h2w_arms_fold(cache, elite_arms, elite_model)) &&
				h2w_model_add(build, &w->first_person, elite_model, "tens ones", 5, FALSE))
			{
				w->graph = h2w_graph_make(build, elite_jmad, &w->first_person, rounds);
				if (w->graph)
					h2w_model_finish(build, &w->first_person);
			}
			if (getenv("HALO_H2_DEBUG"))
				platform_log("h2 weapons: %s: no Master Chief's arms: the dervish's first person %s", w->name, w->graph ? "made" : "not made");
		}
		/* the Elite's (the second entry: the dervish's arms, the same gun,
		the dervish's animations) */
		h2w_model_begin(&w->first_person_elite);
		if (count > 1 && w->graph && arms != NONE && !getenv("HALO_H2_NO_ELITE_ARMS"))
		{
			long elite_model = h2_cache_reference(cache, block + 0x10);
			long elite_jmad = h2_cache_reference(cache, block + 0x10 + 8);
			long elite_arms = h2v_tag_named(cache, 'mode', "objects\\characters\\dervish\\fp\\fp");

			if (elite_arms != NONE && elite_model != NONE && elite_jmad != NONE &&
				h2w_model_add(build, &w->first_person_elite, elite_arms, NULL, 5, h2w_arms_fold(cache, elite_arms, elite_model)) &&
				h2w_model_add(build, &w->first_person_elite, elite_model, "tens ones", 5, FALSE))
			{
				w->graph_elite = h2w_graph_make(build, elite_jmad, &w->first_person_elite, rounds);
				if (w->graph_elite)
					h2w_model_finish(build, &w->first_person_elite);
				else
					w->first_person_elite.model = NONE;
			}
			else
				w->first_person_elite.model = NONE;
		}
	}
	h2w_weapon_values_read(cache, t.address, kind, w);
	/* its firing effects' sounds (its first barrel's firing effects, 0xE4
	of the barrel, 0x34 each, the firing effect a reference at 4) */
	{
		long effect_count, k;
		unsigned long effects;

		w->firing_effect_count = 0;
		w->firing_sounds[0] = w->firing_sounds[1] = NONE;
		if (w->barrel && h2_cache_block(cache, w->barrel + 0xE4, 0x34, &effect_count, &effects))
		{
			for (k = 0; k < effect_count && w->firing_effect_count < H2W_FIRING_EFFECTS; k++)
			{
				unsigned char const *q = h2_cache_at(cache, effects + 0x34 * k + 4, 8);

				w->firing_sounds[w->firing_effect_count++] = q ? h2w_effect_sound(cache, h2_cache_tag_from_handle(cache, read32(q + 4))) : NONE;
			}
		}
	}
	if (getenv("HALO_H2_DEBUG"))
	{
		fprintf(stderr, "h2 weapons: %s: world model %ld (%ld nodes, %ld markers), first person %ld (%ld nodes, %ld markers), "
			"graph %s; magazine %d/%d/%d, %.1f rounds a second, bursts of %d (%.2f s), behavior %d, zoom %d x%.1f, "
			"projectile %.0f..%.0f, damage %.1f..%.1f\n", w->name, w->world.model, w->world.node_count, w->world.marker_count,
			w->first_person.model, w->first_person.node_count, w->first_person.marker_count, w->graph ? "made" : "none",
			w->rounds_loaded, w->rounds_initial, w->rounds_maximum, w->rounds_per_second, w->burst_shots, w->burst_recovery,
			w->trigger_behavior, w->zoom_levels, w->zoom[1], w->projectile_velocity[0], w->projectile_velocity[1],
			w->damage[0], w->damage[2]);
	}
	return w;
}

/* ---------- private code: textures */

/* a 4x4 block (16 B8G8R8A8 texels) as DXT1 */
H2_ALIGN static void h2w_dxt1_block(unsigned char const *texels, unsigned char *out)
{
	int lo[3] = { 255, 255, 255 }, hi[3] = { 0, 0, 0 }, c, k;
	unsigned long c0, c1, indices = 0;
	int palette[4][3];

	for (k = 0; k < 16; k++)
	{
		for (c = 0; c < 3; c++)
		{
			int v = texels[4 * k + 2 - c];   /* R, G, B */

			lo[c] = v < lo[c] ? v : lo[c];
			hi[c] = v > hi[c] ? v : hi[c];
		}
	}
	/* (the box's corners pulled in a little: the error spread) */
	for (c = 0; c < 3; c++)
	{
		int inset = (hi[c] - lo[c]) / 16;

		lo[c] += inset;
		hi[c] -= inset;
	}
	c0 = ((unsigned long)(hi[0] >> 3) << 11) | ((unsigned long)(hi[1] >> 2) << 5) | (unsigned long)(hi[2] >> 3);
	c1 = ((unsigned long)(lo[0] >> 3) << 11) | ((unsigned long)(lo[1] >> 2) << 5) | (unsigned long)(lo[2] >> 3);
	if (c0 < c1)
	{
		unsigned long swap = c0;

		c0 = c1;
		c1 = swap;
	}
	for (k = 0; k < 2; k++)
	{
		unsigned long v = k ? c1 : c0;

		palette[k][0] = (int)(((v >> 11) & 31) * 255 / 31);
		palette[k][1] = (int)(((v >> 5) & 63) * 255 / 63);
		palette[k][2] = (int)((v & 31) * 255 / 31);
	}
	for (c = 0; c < 3; c++)
	{
		palette[2][c] = (2 * palette[0][c] + palette[1][c]) / 3;
		palette[3][c] = (palette[0][c] + 2 * palette[1][c]) / 3;
	}
	for (k = 0; k < 16; k++)
	{
		int best = 0, best_distance = 1 << 30, p;

		for (p = 0; p < (c0 == c1 ? 1 : 4); p++)
		{
			int distance = 0;

			for (c = 0; c < 3; c++)
			{
				int d = texels[4 * k + 2 - c] - palette[p][c];

				distance += d * d;
			}
			if (distance < best_distance)
				best_distance = distance, best = p;
		}
		indices |= (unsigned long)best << (2 * k);
	}
	write16(out, (long)c0);
	write16(out + 2, (long)c1);
	write32(out + 4, indices);
}

/* the build's 32-bit textures from first on (the weapons' and vehicles'
own, made Halo 2's A8R8G8B8: four times DXT1's bytes) made DXT1 here, at
most limit a side, with their mips */
/* a 32-bit image (linear, w x h; halved in place as its levels are made)
as a DXT1 mip chain: *out (malloc'd), *total bytes, *levels; FALSE if not */
H2_ALIGN static boolean h2w_dxt1_chain(unsigned char *p, unsigned long w, unsigned long h, unsigned char **out,
	unsigned long *total, long *levels)
{
	unsigned long lw = w, lh = h, at = 0, x, y;
	long level;

	*total = 0;
	*levels = 0;
	for (;;)
	{
		*total += ((lw + 3) / 4) * ((lh + 3) / 4) * 8;
		(*levels)++;
		if (lw == 1 && lh == 1)
			break;
		lw = lw > 1 ? lw / 2 : 1;
		lh = lh > 1 ? lh / 2 : 1;
	}
	*out = malloc(*total);
	if (!*out)
		return FALSE;
	lw = w;
	lh = h;
	for (level = 0; level < *levels; level++)
	{
		unsigned long bx, by;

		for (by = 0; by < (lh + 3) / 4; by++)
		{
			for (bx = 0; bx < (lw + 3) / 4; bx++)
			{
				unsigned char texels[64];
				int k;

				for (k = 0; k < 16; k++)
				{
					unsigned long tx = bx * 4 + (k & 3), ty = by * 4 + (k >> 2);

					tx = tx < lw ? tx : lw - 1;
					ty = ty < lh ? ty : lh - 1;
					memcpy(texels + 4 * k, p + 4 * (ty * lw + tx), 4);
				}
				h2w_dxt1_block(texels, *out + at);
				at += 8;
			}
		}
		/* the next level, halved in place */
		if (lw > 1 || lh > 1)
		{
			unsigned long nw = lw > 1 ? lw / 2 : 1, nh = lh > 1 ? lh / 2 : 1, c;

			for (y = 0; y < nh; y++)
			{
				for (x = 0; x < nw; x++)
				{
					unsigned long x0 = 2 * x < lw ? 2 * x : lw - 1, x1 = 2 * x + 1 < lw ? 2 * x + 1 : lw - 1;
					unsigned long y0 = 2 * y < lh ? 2 * y : lh - 1, y1 = 2 * y + 1 < lh ? 2 * y + 1 : lh - 1;

					for (c = 0; c < 4; c++)
					{
						p[4 * (y * nw + x) + c] = (unsigned char)((p[4 * (y0 * lw + x0) + c] + p[4 * (y0 * lw + x1) + c] +
							p[4 * (y1 * lw + x0) + c] + p[4 * (y1 * lw + x1) + c] + 2) / 4);
					}
				}
			}
			lw = nw;
			lh = nh;
		}
	}
	return TRUE;
}

/* (piece J) a weapon's 32-bit image halved (2x2 averaged) to the limit
and made DXT1 with its levels - what h2w_textures_compress writes, and what
a level made with no file makes again at load (derived_make); p freed */
H2_ALIGN static boolean h2w_texture_reduced(unsigned char *p, unsigned long w, unsigned long h, unsigned long limit,
	unsigned char **out, unsigned long *total, long *levels, unsigned long *out_w, unsigned long *out_h)
{
	unsigned long x, y;

	/* halved (2x2 averaged) to the limit */
	while ((w > limit || h > limit) && w >= 8 && h >= 8)
	{
		unsigned long c;

		for (y = 0; y < h / 2; y++)
		{
			for (x = 0; x < w / 2; x++)
			{
				for (c = 0; c < 4; c++)
				{
					p[4 * (y * (w / 2) + x) + c] = (unsigned char)((p[4 * ((2 * y) * w + 2 * x) + c] + p[4 * ((2 * y) * w + 2 * x + 1) + c] +
						p[4 * ((2 * y + 1) * w + 2 * x) + c] + p[4 * ((2 * y + 1) * w + 2 * x + 1) + c] + 2) / 4);
				}
			}
		}
		w /= 2;
		h /= 2;
	}
	if (!h2w_dxt1_chain(p, w, h, out, total, levels))
	{
		free(p);
		return FALSE;
	}
	free(p);
	*out_w = w;
	*out_h = h;
	return TRUE;
}

H2_ALIGN static void h2w_textures_compress(struct build *build, long first, unsigned long limit)
{
	long i;

	for (i = first; i < build->bitmap_count; i++)
	{
		struct bitmap *b = &build->bitmaps[i];
		unsigned long got = 0, w = b->width, h = b->height, total = 0, x, y;
		unsigned char *p, *out;
		long levels = 0;

		if (b->pixels || !(b->format == 10 || b->format == 11) || b->type != 0 || (w & (w - 1)) || (h & (h - 1)) || w < 4 || h < 4)
			continue;
		p = h2_cache_texture_read(build->cache, b->reference, b->size, &got);
		if (!p || got < w * h * 4)
		{
			free(p);
			continue;
		}
		/* (the PC tier: the image at its own size too, for the level's
		sidecar - h2_texture_sidecar.h; the file's as before) */
		if ((w > limit || h > limit) && h2_texture_sidecar_enabled() && !b->sidecar_pixels)
		{
			unsigned char *copy = malloc(w * h * 4), *full;
			unsigned long full_total;
			long full_levels;

			if (copy)
			{
				memcpy(copy, p, w * h * 4);
				if (h2w_dxt1_chain(copy, w, h, &full, &full_total, &full_levels))
				{
					/* (how they are made again: a direct level's, h2_level.c) */
					memset(&b->sidecar_recipe, 0, sizeof(b->sidecar_recipe));
					b->sidecar_recipe.kind_plus1 = _derived_recipe_weapon_full + 1;
					b->sidecar_recipe.args[0] = b->reference;
					b->sidecar_recipe.args[1] = b->size;
					b->sidecar_recipe.args[2] = w | (h << 16);
					b->sidecar_pixels = full;
					b->sidecar_size = full_total;
					b->sidecar_width = (unsigned short)w;
					b->sidecar_height = (unsigned short)h;
					b->sidecar_mipmaps = (unsigned short)(full_levels - 1);
				}
				free(copy);
			}
		}
		/* halved to the limit and made DXT1 (h2w_texture_reduced) - made
		again alone at load from this recipe (piece J) */
		{
			unsigned long reduced_w = 0, reduced_h = 0;

			if (!h2w_texture_reduced(p, w, h, limit, &out, &total, &levels, &reduced_w, &reduced_h))
				continue;
			memset(&b->recipe, 0, sizeof(b->recipe));
			b->recipe.kind_plus1 = _derived_recipe_weapon_reduced + 1;
			b->recipe.args[0] = b->reference;
			b->recipe.args[1] = b->size;
			b->recipe.args[2] = w | (h << 16);
			b->recipe.args[3] = limit;
			w = reduced_w;
			h = reduced_h;
		}
		b->width = (unsigned short)w;
		b->height = (unsigned short)h;
		b->format = 14;
		b->mipmaps = (unsigned short)(levels - 1);
		b->pixels = out;
		b->pixels_size = total;
		b->size = total;
	}
}

/* ---------- private code: h2_level_build's hooks */

/* the weapons made (after the scenario's objects: their models join the
build's) */
/* whether one of the level's item collections (itmc: its permutations, 16
bytes each - weight, item reference) hands out the weapon named */
/* (a campaign level) the weapons its things start with (h2w_level_uses):
placed weapons (scenario 0x90, 0x54 each, their palette 0x98 - 0x28
each - index at 0), starting profiles (0xF8, 0x44 each: 0x28, 0x34),
squads (0x160, 0x74 each: weapon 0x3C, secondary 0x3E, character 0x36,
starting locations 0x48 - 0x64 each: character 0x20, weapon 0x22,
secondary 0x24), a squad's or location's character's default weapon when
it names none (character palette 0x178, 8 each; a character's weapons
block at 0xCC, 0xCC each, the weapon at 4; else its parent's, 4); how many
(at most maximum) */
H2_ALIGN static void h2w_weapon_add(long *out, long *count, long maximum, long tag)
{
	long k;

	if (tag == NONE)
		return;
	for (k = 0; k < *count && out[k] != tag; k++)
		;
	if (k == *count && *count < maximum)
		out[(*count)++] = tag;
}

H2_ALIGN static long h2w_character_weapon(struct h2_cache *cache, long character)
{
	int depth;

	for (depth = 0; character != NONE && depth < 8; depth++)
	{
		struct h2_cache_tag t;
		long count;
		unsigned long block;

		if (!h2_cache_tag_get(cache, character, &t))
			return NONE;
		if (h2_cache_block(cache, t.address + 0xCC, 0xCC, &count, &block) && count > 0)
			return h2_cache_reference(cache, block + 4);
		character = h2_cache_reference(cache, t.address + 4);
	}
	return NONE;
}

H2_ALIGN static long h2w_campaign_weapons(struct h2_cache *cache, long *out, long maximum)
{
	static struct h2_cache *cached_for;
	static unsigned long cached_scenario;
	static long cached[96], cached_count;
	struct h2_cache_tag scenario;
	long count = 0, palette_count, character_count, n, k;
	unsigned long palette, characters, block;

	if (!h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario))
		return 0;
	if (cached_for == cache && cached_scenario == scenario.address)
	{
		for (k = 0; k < cached_count && k < maximum; k++)
			out[k] = cached[k];
		return k;
	}
	if (!h2_cache_block(cache, scenario.address + 0x98, 0x28, &palette_count, &palette))
		palette_count = 0;
	if (!h2_cache_block(cache, scenario.address + 0x178, 8, &character_count, &characters))
		character_count = 0;
#define H2W_PALETTE_WEAPON(index) ((short)(index) >= 0 && (short)(index) < palette_count ? \
	h2_cache_reference(cache, palette + 0x28 * (unsigned long)(short)(index)) : NONE)
#define H2W_CHARACTER(index) ((short)(index) >= 0 && (short)(index) < character_count ? \
	h2_cache_reference(cache, characters + 8 * (unsigned long)(short)(index)) : NONE)
	/* placed */
	if (h2_cache_block(cache, scenario.address + 0x90, 0x54, &n, &block))
	{
		for (k = 0; k < n; k++)
			h2w_weapon_add(out, &count, maximum, H2W_PALETTE_WEAPON(h2w_read_short(cache, block + 0x54 * (unsigned long)k)));
	}
	/* the starting profiles */
	if (h2_cache_block(cache, scenario.address + 0xF8, 0x44, &n, &block))
	{
		for (k = 0; k < n; k++)
		{
			h2w_weapon_add(out, &count, maximum, h2_cache_reference(cache, block + 0x44 * (unsigned long)k + 0x28));
			h2w_weapon_add(out, &count, maximum, h2_cache_reference(cache, block + 0x44 * (unsigned long)k + 0x34));
		}
	}
	/* the squads and their starting locations */
	if (h2_cache_block(cache, scenario.address + 0x160, 0x74, &n, &block))
	{
		for (k = 0; k < n; k++)
		{
			unsigned long squad = block + 0x74 * (unsigned long)k, locations;
			short squad_weapon = h2w_read_short(cache, squad + 0x3C), squad_character = h2w_read_short(cache, squad + 0x36);
			long location_count, l;

			h2w_weapon_add(out, &count, maximum, H2W_PALETTE_WEAPON(squad_weapon));
			h2w_weapon_add(out, &count, maximum, H2W_PALETTE_WEAPON(h2w_read_short(cache, squad + 0x3E)));
			if (squad_weapon < 0)
				h2w_weapon_add(out, &count, maximum, h2w_character_weapon(cache, H2W_CHARACTER(squad_character)));
			if (!h2_cache_block(cache, squad + 0x48, 0x64, &location_count, &locations))
				continue;
			for (l = 0; l < location_count; l++)
			{
				unsigned long location = locations + 0x64 * (unsigned long)l;
				short weapon = h2w_read_short(cache, location + 0x22), character = h2w_read_short(cache, location + 0x20);

				h2w_weapon_add(out, &count, maximum, H2W_PALETTE_WEAPON(weapon));
				h2w_weapon_add(out, &count, maximum, H2W_PALETTE_WEAPON(h2w_read_short(cache, location + 0x24)));
				if (weapon < 0 && squad_weapon < 0)
					h2w_weapon_add(out, &count, maximum, h2w_character_weapon(cache, H2W_CHARACTER(character >= 0 ? character : squad_character)));
			}
		}
	}
#undef H2W_PALETTE_WEAPON
#undef H2W_CHARACTER
	cached_for = cache;
	cached_scenario = scenario.address;
	for (k = 0; k < count && k < 96; k++)
		cached[k] = out[k];
	cached_count = k;
	if (getenv("HALO_H2_DEBUG"))
	{
		for (k = 0; k < count; k++)
			platform_log("h2 weapons: the campaign's things start with %s", h2_cache_tag_name(cache, out[k]));
	}
	return count;
}

H2_ALIGN static boolean h2w_level_uses(struct h2_cache *cache, char const *weapon)
{
	long i;

	/* (a campaign level's weapons its things start with: the weapons it
	places, its starting profiles', its squads' and their starting
	locations' (and secondary), and the default of each character a squad
	or location names with none - the Marines' Halo 2 weapons a player can
	take, players.c. Not the whole weapon palette: 03a's grew its tags
	22 -> 27 MB, past the Vita's tag cache) */
	if (strstr(h2_cache_scenario_path(cache), "\\solo\\") && !getenv("HALO_H2_AI_STAND_IN_WEAPONS"))
	{
		long needed[96], needed_count = h2w_campaign_weapons(cache, needed, 96);

		for (i = 0; i < needed_count; i++)
		{
			if (!strcmp(h2_cache_tag_name(cache, needed[i]), weapon))
				return TRUE;
		}
		/* (its equipment palette: the ammunition boxes it places - 03a's
		69 smg, battle rifle, sniper, pistol and rocket ammo, which were
		left out as kinds with no stand-in) */
		{
			struct h2_cache_tag scenario;
			long palette_count;
			unsigned long palette;

			if (h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) &&
				h2_cache_block(cache, scenario.address + 0x88, 0x28, &palette_count, &palette))
			{
				for (i = 0; i < palette_count; i++)
				{
					long tag = h2_cache_reference(cache, palette + 0x28 * (unsigned long)i);

					if (tag != NONE && !strcmp(h2_cache_tag_name(cache, tag), weapon))
						return TRUE;
				}
			}
		}
	}
	for (i = 0; i < h2_cache_tag_count(cache); i++)
	{
		struct h2_cache_tag c;
		long count, k;
		unsigned long block;

		if (!h2_cache_tag_get(cache, i, &c) || c.group_tag != 'itmc' ||
			!h2_cache_block(cache, c.address, 16, &count, &block))
		{
			continue;
		}
		for (k = 0; k < count; k++)
		{
			unsigned char const *p = h2_cache_at(cache, block + 16 * k, 16);
			long item = p ? h2_cache_tag_from_handle(cache, read32(p + 8)) : NONE;

			if (item != NONE && !strcmp(h2_cache_tag_name(cache, item), weapon))
				return TRUE;
		}
	}
	return FALSE;
}

/* Halo 2's grenade projectile of the name (its model's) read: timer, arming,
frictions, the damage of its explosion and of a stuck one */
H2_ALIGN static void h2w_grenade_jpt_read(struct h2_cache *cache, long tag, struct h2w_grenade_jpt *out)
{
	struct h2_cache_tag t;

	out->valid = FALSE;
	out->tag = tag;
	if (tag == NONE || !h2_cache_tag_get(cache, tag, &t))
		return;
	out->radius[0] = h2w_read_real(cache, t.address + 0x00);
	out->radius[1] = h2w_read_real(cache, t.address + 0x04);
	out->core_radius = h2w_read_real(cache, t.address + 0x18);
	out->damage_lower = h2w_read_real(cache, t.address + 0x1C);
	out->damage_upper[0] = h2w_read_real(cache, t.address + 0x20);
	out->damage_upper[1] = h2w_read_real(cache, t.address + 0x24);
	out->acceleration = h2w_read_real(cache, t.address + 0x40);
	out->valid = out->damage_upper[1] > 0.0f;
}

H2_ALIGN static void h2w_grenade_read(struct h2_cache *cache, char const *name, struct h2w_grenade_data *out)
{
	long i;

	memset(out, 0, sizeof(*out));
	for (i = 0; i < h2_cache_tag_count(cache); i++)
	{
		struct h2_cache_tag t;
		long count;
		unsigned long block;

		if (!h2_cache_tag_get(cache, i, &t) || t.group_tag != 'proj' || strcmp(h2_cache_tag_name(cache, i), name))
			continue;
		out->flags = h2_cache_read32(cache, t.address + 0xBC);
		out->timer_starts = h2w_read_short(cache, t.address + 0xC0);
		out->arming = h2w_read_real(cache, t.address + 0xCC);
		out->danger_radius = h2w_read_real(cache, t.address + 0xD0);
		out->timer[0] = h2w_read_real(cache, t.address + 0xD4);
		out->timer[1] = h2w_read_real(cache, t.address + 0xD8);
		out->gravity_scale = h2w_read_real(cache, t.address + 0x164);
		h2w_grenade_jpt_read(cache, h2_cache_reference(cache, t.address + 0x100), &out->explosion);
		h2w_grenade_jpt_read(cache, h2_cache_reference(cache, t.address + 0x108), &out->attached);
		/* (its material responses: every one, each material type its own) */
		h2w_responses_read(cache, t.address, &out->responses);
		out->valid = out->explosion.valid;
		if (getenv("HALO_H2_DEBUG") || getenv("HALO_H2_GRENADE_LOG"))
			fprintf(stderr, "h2 grenade: %s: timer starts %d arming %.2f timer %.2f..%.2f danger %.2f friction %.2f/%.2f explosion %.2f..%.2f "
				"damage %.0f..%.0f..%.0f attached %.0f\n", name, out->timer_starts, out->arming, out->timer[0], out->timer[1],
				out->danger_radius, out->responses.count ? out->responses.responses[0].frictions[0] : -1.0f,
				out->responses.count ? out->responses.responses[0].frictions[1] : -1.0f, out->explosion.radius[0],
				out->explosion.radius[1], out->explosion.damage_lower, out->explosion.damage_upper[0], out->explosion.damage_upper[1],
				out->attached.valid ? out->attached.damage_upper[1] : 0.0f);
		return;
	}
}

/* a made weapon's (h2w.weapons' index) looping sounds: Halo 2's
attachments (weap @94, 0x18 each: the type at 0, its marker at 8, its
change colour at 0xC, its primary scale's function name at 0x10) of looping sounds scaled by its
firing (the Sentinel Beam's sentinel_gun: "audio_firing", its function of
primary_firing) or its overheat ("overheated"), their first track's (lsnd
@24, the tracks: gain in dB at 8, fades in and out at 0xC and 0x10, its in,
loop and out sounds at 0x14, 0x1C and 0x24) */
H2_ALIGN static void h2w_loops_read(struct h2_cache *cache, long weapon)
{
	struct h2w_weapon const *w = h2w.weapons[weapon];
	struct h2_cache_tag t, l;
	long count, i, track_count, k;
	unsigned long attachments, tracks;

	if (getenv("HALO_H2_NO_WEAPON_LOOPS") || !h2_cache_tag_get(cache, w->h2_tag, &t) ||
		!h2_cache_block(cache, t.address + 0x94, 0x18, &count, &attachments))
	{
		return;
	}
	h2w.weapons[weapon]->sounds_only = count > 0;
	for (i = 0; i < count; i++)
	{
		long type = h2_cache_reference(cache, attachments + 0x18 * (unsigned long)i);
		struct h2_cache_tag a;

		if (type == NONE || !h2_cache_tag_get(cache, type, &a) || a.group_tag != 'lsnd')
			h2w.weapons[weapon]->sounds_only = FALSE;
	}
	for (i = 0; i < count && h2w.loop_count < H2W_MAXIMUM_LOOPS; i++)
	{
		unsigned long q = attachments + 0x18 * (unsigned long)i;
		long lsnd = h2_cache_reference(cache, q);
		char scale[64];
		struct h2w_loop *o;
		short mode;

		h2w_name(cache, h2_cache_read32(cache, q + 0x10), scale, sizeof(scale));
		mode = strstr(scale, "firing") ? _weapon_function_primary_firing : strstr(scale, "overheat") ? _weapon_function_overheated :
			_weapon_function_none;
		if (mode == _weapon_function_none || lsnd == NONE || !h2_cache_tag_get(cache, lsnd, &l) || l.group_tag != 'lsnd' ||
			!h2_cache_block(cache, l.address + 0x24, 0x2C, &track_count, &tracks) || track_count < 1)
		{
			continue;
		}
		o = &h2w.loops[h2w.loop_count];
		memset(o, 0, sizeof(*o));
		o->weapon = weapon;
		o->mode = mode;
		snprintf(o->name, sizeof(o->name), "%s", mode == _weapon_function_primary_firing ? "firing loop" : "overheated loop");
		/* (Halo 2's gain in decibels: -3 the hum's, -9 the hiss's) */
		o->gain = (float)pow(10.0, h2w_read_real(cache, tracks + 8) / 20.0);
		if (!(o->gain > 0.0f) || o->gain > 1.0f)
			o->gain = 1.0f;
		o->fade[0] = h2w_read_real(cache, tracks + 0xC);
		o->fade[1] = h2w_read_real(cache, tracks + 0x10);
		for (k = 0; k < 3; k++)
		{
			unsigned char const *p = h2_cache_at(cache, tracks + 0x14 + 8 * (unsigned long)k, 8);

			o->sounds[k] = p && read32(p) == 'snd!' ? h2w_sound(cache, h2_cache_tag_from_handle(cache, read32(p + 4))) : NONE;
		}
		if (o->sounds[1] == NONE)
			continue;
		h2w.loop_count++;
		platform_log("h2 weapons: %s: %s %s (gain %.2f, fades %.2f %.2f s): in %s, loop %s, out %s", w->name, o->name,
			h2_cache_tag_name(cache, lsnd), o->gain, o->fade[0], o->fade[1], o->sounds[0] != NONE ? h2w.sounds[o->sounds[0]] : "-",
			h2w.sounds[o->sounds[1]], o->sounds[2] != NONE ? h2w.sounds[o->sounds[2]] : "-");
	}
}

/* a made weapon's (h2w.weapons' index) ammunition box made, where the
level places it (an item collection's permutation is its equipment):
its model (the equipment's hlmt, 0x34: its render model first) and its
multiplayer on-ground scale (@C4) */
H2_ALIGN static void h2w_ammo_read(struct build *build, long weapon)
{
	struct h2_cache *cache = build->cache;
	struct h2w_weapon *w = h2w.weapons[weapon];
	struct h2_cache_tag e, m;

	if (getenv("HALO_H2_NO_AMMO_BOXES") || w->ammo_tag == NONE || !h2_cache_tag_get(cache, w->ammo_tag, &e) || e.group_tag != 'eqip' ||
		!h2w_level_uses(cache, h2_cache_tag_name(cache, w->ammo_tag)) ||
		!h2_cache_tag_get(cache, h2_cache_reference(cache, e.address + 0x34), &m))
	{
		return;
	}
	w->ammo_scale = h2w_read_real(cache, e.address + 0xC4);
	if (h2w_model_add(build, &w->ammo_model, h2_cache_reference(cache, m.address), NULL,
		h2w_detail("HALO_H2_WEAPON_DETAIL", H2W_WORLD_DETAIL_DEFAULT), FALSE) && h2w_model_finish(build, &w->ammo_model))
	{
		w->ammo_slot = h2w.ammo_count;
		h2w.ammo_weapons[h2w.ammo_count++] = weapon;
		platform_log("h2 weapons: %s: its ammunition box %s (%d rounds) made", w->name, h2_cache_tag_name(cache, w->ammo_tag),
			(int)w->rounds_box);
	}
	else
		w->ammo_model.model = NONE;
}

/* a made weapon's (h2w.weapons' index) muzzle light: Halo 2's weapon
lights its surroundings by a light attached to it (weap @94, 0x18 each:
the type at 0, its marker at 8, its primary scale's function name at 0x10)
scaled by its "illumination" - full as a round fires, back to none over its
barrel's illumination recovery time (@A0 of the barrel) - the needler's
plasma_rifle\muzzle_flash, a pink light 0.8 units wide, the battle rifle's
orange, 0.85. Its light (ligh): radius @18, diffuse colour's upper bound
@64 (its lower bound none), brightness bounds @70 (the upper taken). The
stand-in's own muzzle light (the kit's: 2 units wide, the sniper rifle's
3, at full colour - the needler's magenta) is made Halo 2's
(h2w_light_attach, h2w_lights_write). Not an AI's only weapon's (of no
model); HALO_H2_STAND_IN_FIRING_LIGHTS: the stand-in's kept */
H2_ALIGN static void h2w_light_read(struct h2_cache *cache, long weapon)
{
	struct h2w_weapon *w = h2w.weapons[weapon];
	struct h2_cache_tag t, l;
	long count, i;
	unsigned long attachments;

	w->light_tag = NONE;
	w->light_slot = NONE;
	if (getenv("HALO_H2_STAND_IN_FIRING_LIGHTS") || h2w_kinds[w->kind].ai_only || !h2_cache_tag_get(cache, w->h2_tag, &t) ||
		!h2_cache_block(cache, t.address + 0x94, 0x18, &count, &attachments))
	{
		return;
	}
	for (i = 0; i < count; i++)
	{
		unsigned long q = attachments + 0x18 * (unsigned long)i;
		long light = h2_cache_reference(cache, q);
		char scale[64];
		float brightness;
		int k;

		h2w_name(cache, h2_cache_read32(cache, q + 0x10), scale, sizeof(scale));
		if (light == NONE || !h2_cache_tag_get(cache, light, &l) || l.group_tag != 'ligh' || strcmp(scale, "illumination"))
			continue;
		w->light_tag = light;
		h2w_name(cache, h2_cache_read32(cache, q + 8), w->light_marker, sizeof(w->light_marker));
		w->light_radius = h2w_read_real(cache, l.address + 0x18);
		brightness = h2w_read_real(cache, l.address + 0x74);
		for (k = 0; k < 3; k++)
			w->light_color[k] = h2w_read_real(cache, l.address + 0x64 + 4 * (unsigned long)k) * brightness;
		w->light_recovery = w->barrel ? h2w_read_real(cache, w->barrel + 0xA0) : 0.0f;
		w->light_slot = h2w.light_count;
		h2w.light_weapons[h2w.light_count++] = weapon;
		platform_log("h2 weapons: %s: its muzzle light %s at '%s': radius %.2f colour %.2f %.2f %.2f (brightness %.2f), "
			"illumination recovery %.3f s", w->name, h2_cache_tag_name(cache, light), w->light_marker, w->light_radius,
			w->light_color[0], w->light_color[1], w->light_color[2], brightness, w->light_recovery);
		break;
	}
}

/* (h2_models_load.inc) the weapons gathered for their models at load: no
HUD file made, no textures compressed - the level's are its own */
static boolean h2w_gather_models_only = FALSE;
/* (h2_models_load.inc, h2_level.c: HALO_H2_MODELS_AT_LOAD=1 as a level is
made) the weapons' first-person graphs - each weapon's second and seventh
tags - and the vehicles' graphs left out of the level, made from the map as
it loads: their instances with no data */
static boolean h2w_graphs_at_load = FALSE;
/* (the same) the vehicles' own collision models left out, a stub of each
in the level (h2_vehicles_write) */
static boolean h2w_collision_at_load = FALSE;

H2_ALIGN static void h2_weapons_gather(struct build *build)
{
	long k;

	long first_bitmap = build->bitmap_count;

	h2w.count = 0;
	h2w.sound_count = 0;
	h2w.loop_count = 0;
	h2w.ammo_count = 0;
	h2w.light_count = 0;
	if (getenv("HALO_H2_NO_WEAPONS") || !h2_cache_is_mcc(build->cache))
		return;
	for (k = 0; k < (long)(sizeof(h2w_kinds) / sizeof(h2w_kinds[0])) && h2w.count < H2W_MAXIMUM_WEAPONS; k++)
	{
		struct h2w_weapon *w;

		/* (only the weapons the level can hand out: its item collections',
		and the multiplayer start's - the cache file's room) */
		/* (an AI's only: a campaign level's, where its tag is - what its
		characters carry, their variants' too) */
		if (h2w_kinds[k].ai_only && !strstr(h2_cache_scenario_path(build->cache), "\\solo\\"))
			continue;
		/* (Halo 3's weapon takes this collection: h3_weapons_gather below) */
		if (h3_weapons_replaces(k))
			continue;
		if (!h2w_kinds[k].ai_only && !h2w_level_uses(build->cache, h2w_kinds[k].h2) &&
			strcmp(h2w_kinds[k].collection, "battle_rifle") && strcmp(h2w_kinds[k].collection, "smg") &&
			/* (the oddball: the game type's own, in no collection) */
			strcmp(h2w_kinds[k].collection, "ball") &&
			!getenv("HALO_H2_ALL_WEAPONS"))
		{
			continue;
		}
		/* (Halo 2's main menu: its scene places no weapon and its map holds
		no weapon's models - every one tried was "not made", the stand-in
		kept: not tried, piece H) */
		if (h2_level_is_main_menu(build->cache))
			continue;
		if (build->gather_trim_bytes)
			h2_cache_trim_past(build->cache, build->gather_trim_bytes);
		{
			char unit[96];

			snprintf(unit, sizeof(unit), "the weapon %s", h2w_kinds[k].h2);
			h2_build_heap_unit(unit);
		}
		w = h2w_weapon_make(build, k);

		/* (one with a world model and its first-person model and animations
		only: else the stand-in stays) */
		if (w && ((w->world.model != NONE && w->first_person.model != NONE && w->graph) ||
			(h2w_kinds[k].ai_only && w->world.model == NONE)))
		{
			h2w.weapons[h2w.count++] = w;
			h2w_loops_read(build->cache, h2w.count - 1);
			h2w_ammo_read(build, h2w.count - 1);
			h2w_light_read(build->cache, h2w.count - 1);
		}
		else if (w)
		{
			platform_log("h2 weapons: %s not made (its models or animations could not be read): the stand-in stays",
				h2w_kinds[k].h2);
			h2w_graph_free(w->graph);
			free(w);
		}
	}
	/* the grenades' models */
	for (k = 0; k < H2W_GRENADE_COUNT; k++)
	{
		long i, tag = NONE;

		h2w_model_begin(&h2w.grenades[k]);
		h2w.grenades[k].levels = TRUE;
		for (i = 0; i < h2_cache_tag_count(build->cache) && tag == NONE; i++)
		{
			struct h2_cache_tag c;

			if (h2_cache_tag_get(build->cache, i, &c) && c.group_tag == 'mode' && !strcmp(h2_cache_tag_name(build->cache, i), h2w_grenades[k].h2_model))
				tag = i;
		}
		if (tag != NONE && h2w_model_add(build, &h2w.grenades[k], tag, NULL, 5, FALSE))
			h2w_model_finish(build, &h2w.grenades[k]);
		/* (the equipment of the model's name: its multiplayer on-ground scale) */
		h2w.grenade_scale[k] = 0.0f;
		for (i = 0; i < h2_cache_tag_count(build->cache); i++)
		{
			struct h2_cache_tag c;

			if (h2_cache_tag_get(build->cache, i, &c) && c.group_tag == 'eqip' && !strcmp(h2_cache_tag_name(build->cache, i), h2w_grenades[k].h2_model))
			{
				h2w.grenade_scale[k] = h2w_read_real(build->cache, c.address + 0xC4);
				break;
			}
		}
	}
	/* the grenades' projectiles and damage */
	for (k = 0; k < H2W_GRENADE_COUNT; k++)
		h2w_grenade_read(build->cache, h2w_grenades[k].h2_model, &h2w.grenade_data[k]);
	/* the powerups' looks (where the level places them: an item
	collection's permutation is the equipment - the random powerup's too) */
	for (k = 0; k < H2W_POWERUP_COUNT; k++)
	{
		struct h2_cache_tag e, m;
		long tag = h2_cache_tag_find(build->cache, 'eqip', h2w_powerups[k].h2);

		h2w_model_begin(&h2w.powerups[k]);
		h2w.powerups[k].levels = TRUE;
		h2w.powerup_sound[k] = NONE;
		h2w.powerup_scale[k] = 0.0f;
		if (getenv("HALO_H2_NO_POWERUP_MODELS") || tag == NONE || !h2_cache_tag_get(build->cache, tag, &e) ||
			!h2w_level_uses(build->cache, h2w_powerups[k].h2) ||
			!h2_cache_tag_get(build->cache, h2_cache_reference(build->cache, e.address + 0x34), &m))
		{
			continue;
		}
		h2w.powerup_scale[k] = h2w_read_real(build->cache, e.address + 0xC4);
		/* (its pickup sound, eqip @134: the camouflage's pickup_invis) */
		h2w.powerup_sound[k] = h2w_sound(build->cache, h2_cache_reference(build->cache, e.address + 0x134));
		h2w_powerup_materials = !getenv("HALO_H2_NO_POWERUP_SHELLS");
		if (h2w_model_add(build, &h2w.powerups[k], h2_cache_reference(build->cache, m.address), NULL, 5, FALSE) &&
			(h2w_powerup_materials = FALSE, h2w_model_finish(build, &h2w.powerups[k])))
		{
			platform_log("h2 weapons: powerup %s: Halo 2's model on %s", h2w_powerups[k].h2, h2w_powerups[k].ce);
		}
		else
			h2w.powerups[k].model = NONE;
		h2w_powerup_materials = FALSE;
	}
	h3_weapons_gather(build);
	h2_vehicles_gather(build);
	if (h2w_gather_models_only)
		return;
	h2_hud_make(build);
	/* (their textures DXT1, 256 a side: the multiplayer cache's room) */
	if (h2_texture_sidecar_enabled() || getenv("HALO_H2_VITA_256"))
		h2w_textures_compress(build, first_bitmap, 256);
	if (getenv("HALO_H2_DEBUG"))
	{
		long i;

		for (i = first_bitmap; i < build->bitmap_count; i++)
			fprintf(stderr, "h2 weapons: bitmap %s %dx%d format %d mips %d size %lu%s\n", build->bitmaps[i].path,
				build->bitmaps[i].width, build->bitmaps[i].height, build->bitmaps[i].format, build->bitmaps[i].mipmaps,
				build->bitmaps[i].pixels ? build->bitmaps[i].pixels_size : build->bitmaps[i].size, build->bitmaps[i].pixels ? " made" : "");
	}
}

H2_ALIGN static long h2_weapons_tag_count(void)
{
	return h2w.count * H2W_TAGS_PER_WEAPON + h2_vehicles_tag_count() + h2w.sound_count + H2W_GRENADE_COUNT + h2w.loop_count +
		h2w.ammo_count + h2w.light_count;
}

/* the muzzle lights' first tag (from the weapons' first), after the ammunition boxes */
H2_ALIGN static long h2w_light_first(void)
{
	return h2w.count * H2W_TAGS_PER_WEAPON + h2_vehicles_tag_count() + h2w.sound_count + H2W_GRENADE_COUNT + h2w.loop_count +
		h2w.ammo_count;
}

/* the ammunition boxes' first tag (from the weapons' first), after the loops */
H2_ALIGN static long h2w_ammo_first(void)
{
	return h2w.count * H2W_TAGS_PER_WEAPON + h2_vehicles_tag_count() + h2w.sound_count + H2W_GRENADE_COUNT + h2w.loop_count;
}

/* the looping sounds' first tag (from the weapons' first) */
H2_ALIGN static long h2w_loop_first(void)
{
	return h2w.count * H2W_TAGS_PER_WEAPON + h2_vehicles_tag_count() + h2w.sound_count + H2W_GRENADE_COUNT;
}

/* the weapons' sounds' first tag (from the weapons' first) */
H2_ALIGN static long h2w_sound_first(void)
{
	return h2w.count * H2W_TAGS_PER_WEAPON + h2_vehicles_tag_count();
}

/* the tags' groups and names (index from the weapons' first) */
/* a made weapon's tags, H2W_TAGS_PER_WEAPON from its own: their groups and
their names after the weapon's (h2_weapons_tag_name; h2w_loaded_slot) */
static unsigned long const h2w_slot_groups[H2W_TAGS_PER_WEAPON] = { 'weap', 'antr', 'proj', 'jpt!', 'effe', 'effe', 'antr', 'jpt!', 'jpt!',
	'proj', 'jpt!', 'jpt!', 'jpt!', 'jpt!', 'jpt!', 'effe', 'effe', 'effe', 'effe', 'jpt!', 'jpt!' };
static char const *const h2w_slot_names[H2W_TAGS_PER_WEAPON] = { "", " first person", " bullet", " bullet damage", " fire", " fire 2",
	" first person elite", " melee", " lunge", " bullet 2", " bullet 2 damage", " detonation damage", " attached damage",
	" super damage", " attached super damage", " detonation", " super detonation", " ready", " overheated", " melee response",
	" lunge response" };

H2_ALIGN static void h2_weapons_tag_name(long index, unsigned long *group, char *text, long size)
{
	struct h2w_weapon const *w;

	/* (after the ammunition boxes: the muzzle lights) */
	if (index >= h2w_light_first())
	{
		*group = 'ligh';
		snprintf(text, size, "h2\\weapons\\%s muzzle light", h2w.weapons[h2w.light_weapons[index - h2w_light_first()]]->name);
		return;
	}
	/* (after the looping sounds: the ammunition boxes) */
	if (index >= h2w_ammo_first())
	{
		*group = 'eqip';
		snprintf(text, size, "h2\\weapons\\%s ammo", h2w.weapons[h2w.ammo_weapons[index - h2w_ammo_first()]]->name);
		return;
	}
	/* (after the grenades' stuck damage: the looping sounds) */
	if (index >= h2w_sound_first() + h2w.sound_count + H2W_GRENADE_COUNT)
	{
		struct h2w_loop const *l = &h2w.loops[index - h2w_sound_first() - h2w.sound_count - H2W_GRENADE_COUNT];

		*group = 'lsnd';
		snprintf(text, size, "h2\\weapons\\%s %s", h2w.weapons[l->weapon]->name, l->name);
		return;
	}
	/* (after the sounds: each grenade's stuck damage) */
	if (index >= h2w_sound_first() + h2w.sound_count)
	{
		*group = 'jpt!';
		snprintf(text, size, "h2\\weapons\\%s attached", h2w_grenades[index - h2w_sound_first() - h2w.sound_count].ce);
		return;
	}
	if (index >= h2w_sound_first())
	{
		*group = 'snd!';
		snprintf(text, size, "sound\\h2\\%s", h2w.sounds[index - h2w_sound_first()]);
		return;
	}
	if (index >= h2w.count * H2W_TAGS_PER_WEAPON)
	{
		h2_vehicles_tag_name(index - h2w.count * H2W_TAGS_PER_WEAPON, group, text, size);
		return;
	}
	w = h2w.weapons[index / H2W_TAGS_PER_WEAPON];
	*group = h2w_slot_groups[index % H2W_TAGS_PER_WEAPON];
	snprintf(text, size, "h2\\weapons\\%s%s", w->name, h2w_slot_names[index % H2W_TAGS_PER_WEAPON]);
}

/* the kit's recipe (h2_level.c's) with the models and first-person
animations of the stand-ins the made weapons replace left out (strip
lines: their pixels are not carried, the level's file stays within the
multiplayer cache) */
H2_ALIGN static char const *h2_weapons_kit_recipe(char const *recipe)
{
	static char text[SCENE_PACK_MAXIMUM_RECIPE_BYTES];
	char const *done[H2W_MAXIMUM_WEAPONS];
	long i, k, done_count = 0, length;

	snprintf(text, sizeof(text), "%s", recipe);
	length = (long)strlen(text);
	for (k = 0; k < H2W_GRENADE_COUNT; k++)
	{
		if (h2w.grenades[k].model != NONE)
			length += snprintf(text + length, sizeof(text) - length, "strip mode %s\n", h2w_grenades[k].ce);
	}
	length = h2_vehicles_kit_strips(text, (long)sizeof(text), length);
	/* (the powerups' shells: the kit's plasma - its noise volume - and glass) */
	length += snprintf(text + length, sizeof(text) - length,
		"tag spla characters\\cyborg\\shaders\\shield hit\ntag sgla powerups\\active camoflage\\shaders\\powerup glass\n");
	/* (HALO_H2_KIT_EXTRA: recipe lines of the player's own, ';' between them - tests) */
	if (getenv("HALO_H2_KIT_EXTRA") && length < (long)sizeof(text))
	{
		char const *c;

		for (c = getenv("HALO_H2_KIT_EXTRA"); *c && length < (long)sizeof(text) - 2; c++)
			text[length++] = *c == ';' ? '\n' : *c;
		text[length++] = '\n';
		text[length] = 0;
	}
	for (i = 0; i < h2w.count; i++)
	{
		char const *base = h2w_kinds[h2w.weapons[i]->kind].base;
		char folder[200];
		char const *slash = strrchr(base, '\\');

		for (k = 0; k < done_count && strcmp(done[k], base); k++)
			;
		if (k < done_count || !slash)
			continue;
		done[done_count++] = base;
		snprintf(folder, sizeof(folder), "%.*s", (int)(slash - base), base);
		length += snprintf(text + length, sizeof(text) - length, "strip mode %s\nstrip mode %s\\fp\\fp\nstrip antr %s\\fp\\fp\n",
			base, folder, folder);
		if (length >= (long)sizeof(text))
		{
			snprintf(text, sizeof(text), "%s", recipe);
			break;
		}
	}
	return text;
}

/* the kit's weapons the made ones come from (before the kit is moved) */
H2_ALIGN static void h2_weapons_kit_find(unsigned char *kit)
{
	long i;

	for (i = 0; i < h2w.count; i++)
		h2w.weapons[i]->base_weapon = scene_pack_find(kit, 'weap', h2w_kinds[h2w.weapons[i]->kind].base);
	h2_vehicles_kit_find(kit);
	/* (the weapons' sounds' template: a sound of the kit's, its fields) */
	h2w.kit_sound = scene_pack_find(kit, 'snd!', "sound\\sfx\\weapons\\pistol\\fire");
	h2w.kit_ammo = scene_pack_find(kit, 'eqip', "powerups\\rocket launcher ammo\\rocket launcher ammo");
	for (i = 0; i < H2W_POWERUP_COUNT; i++)
		h2w.powerup_equipment[i] = scene_pack_find(kit, 'eqip', h2w_powerups[i].ce);
	h2w.kit_plasma = scene_pack_find(kit, 'spla', "characters\\cyborg\\shaders\\shield hit");
	h2w.kit_glass = scene_pack_find(kit, 'sgla', "powerups\\active camoflage\\shaders\\powerup glass");
	platform_log("h2 weapons: the kit's plasma %ld, glass %ld (the powerups' shells)", h2w.kit_plasma, h2w.kit_glass);
	h2w.kit_pistol = scene_pack_find(kit, 'weap', "weapons\\pistol\\pistol");
	for (i = 0; i < H2W_GRENADE_COUNT; i++)
	{
		h2w.grenade_equipment[i] = scene_pack_find(kit, 'eqip', h2w_grenades[i].ce);
		h2w.grenade_projectile[i] = scene_pack_find(kit, 'proj', h2w_grenades[i].ce);
	}
}

/* the made weapon a netgame collection's name stands for (its tag index
from the weapons' first), NONE if none */
H2_ALIGN static long h2_weapons_for(char const *collection)
{
	long i;

	for (i = 0; i < h2w.count; i++)
	{
		if (!strcmp(collection, h2w_kinds[h2w.weapons[i]->kind].collection))
			return i * H2W_TAGS_PER_WEAPON;
		/* (its ammunition box's: "ammo_battle_rifle") */
		if (!strncmp(collection, "ammo_", 5) && !strcmp(collection + 5, h2w_kinds[h2w.weapons[i]->kind].collection))
			return h2w.weapons[i]->ammo_slot != NONE ? h2w_ammo_first() + h2w.weapons[i]->ammo_slot : NONE;
	}
	i = h2_vehicles_for(collection);
	return i != NONE ? h2w.count * H2W_TAGS_PER_WEAPON + i : NONE;
}

/* a tag's root (an offset of r) by its index; 0 if none */
H2_ALIGN static unsigned long h2w_root(struct region *r, unsigned long instances, long index)
{
	unsigned long address = read32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * index + 20));

	return address ? address - r->link : 0;
}

/* a cloned tag's blocks and data given storage of their own (the tag
check refuses two tags' blocks in one place): its layout's walk */
H2_ALIGN static void h2w_copy_walk(struct region *r, unsigned short layout, unsigned long at, int depth)
{
	long count = halo_tag_layout_field_count(layout), i;

	if (layout == HALO_TAG_LAYOUT_NONE || layout == HALO_TAG_LAYOUT_UNKNOWN || depth > 32)
		return;
	for (i = 0; i < count && !r->b.failed; i++)
	{
		struct halo_tag_layout_field field;
		unsigned long f;

		if (!halo_tag_layout_field(layout, i, &field))
			continue;
		f = at + field.offset;
		switch (field.kind)
		{
			case HALO_TAG_LAYOUT_FIELD_BLOCK:
			{
				long n = (long)read32(region_at(r, f));
				unsigned long address = read32(region_at(r, f + 4)), to, e;

				if (n <= 0 || !address || !field.child_size || address - r->link >= r->b.size)
					break;
				to = region_alloc(r, (unsigned long)n * field.child_size);
				if (r->b.failed)
					return;
				memcpy(region_at(r, to), region_at(r, address - r->link), (unsigned long)n * field.child_size);
				write32(region_at(r, f + 4), r->link + to);
				if (field.child != HALO_TAG_LAYOUT_NONE)
				{
					for (e = 0; e < (unsigned long)n; e++)
						h2w_copy_walk(r, field.child, to + field.child_size * e, depth + 1);
				}
				break;
			}
			case HALO_TAG_LAYOUT_FIELD_DATA:
			{
				unsigned long size = read32(region_at(r, f)), address = read32(region_at(r, f + 12)), to;

				if (!size || !address || address - r->link >= r->b.size)
					break;
				to = region_alloc(r, size);
				if (r->b.failed)
					return;
				memcpy(region_at(r, to), region_at(r, address - r->link), size);
				write32(region_at(r, f + 12), r->link + to);
				break;
			}
			case HALO_TAG_LAYOUT_FIELD_STRUCT:
				h2w_copy_walk(r, field.child, f, depth + 1);
				break;
		}
	}
}

/* a copy of the tag at from (an offset of r, size bytes, group), its own
blocks; its offset */
H2_ALIGN static unsigned long h2w_tag_copy(struct region *r, unsigned long from, unsigned long size, unsigned long group)
{
	unsigned long to = region_alloc(r, size);

	if (r->b.failed)
		return to;
	memcpy(region_at(r, to), region_at(r, from), size);
	h2w_copy_walk(r, halo_tag_layout_root(group), to, 0);
	/* (a damage effect: no sound - Halo 2's have none, their hits heard
	from the materials and the weapons' animations; the stand-in's own,
	a Custom Edition melee's impact, played on every hit) */
	if (group == 'jpt!' && read32(region_at(r, to + offsetof(struct damage_effect_definition, sound))) == 'snd!')
		write32(region_at(r, to + offsetof(struct damage_effect_definition, sound) + 12), 0xFFFFFFFFUL);
	return to;
}

/* a block's elements (an offset of r), 0 if none */
H2_ALIGN static unsigned long h2w_block_copy(struct region *r, unsigned long field, unsigned long size)
{
	long count = (long)read32(region_at(r, field));
	unsigned long address = read32(region_at(r, field + 4));

	(void)size;
	return count > 0 && address ? address - r->link : 0;
}

/* (HALO_H2_DEBUG) a kit model's markers and nodes logged */
H2_ALIGN static void h2w_debug_model(struct region *r, unsigned long instances, long index, char const *what)
{
	unsigned long root;
	long count, i;
	unsigned long elements;

	if (!getenv("HALO_H2_DEBUG") || index < 0 || !(root = h2w_root(r, instances, index)))
		return;
	count = (long)read32(region_at(r, root + offsetof(struct model, markers)));
	elements = read32(region_at(r, root + offsetof(struct model, markers) + 4));
	fprintf(stderr, "h2 weapons: %s: %ld markers:", what, count);
	for (i = 0; i < count && elements; i++)
		fprintf(stderr, " '%s'", (char const *)region_at(r, elements - r->link + sizeof(struct model_marker) * i));
	count = (long)read32(region_at(r, root + offsetof(struct model, nodes)));
	elements = read32(region_at(r, root + offsetof(struct model, nodes) + 4));
	fprintf(stderr, "; %ld nodes:", count);
	for (i = 0; i < count && elements; i++)
	{
		unsigned char const *n = region_at(r, elements - r->link + sizeof(struct model_node) * i);

		fprintf(stderr, " '%s'(parent %d at %.3f %.3f %.3f)", (char const *)n, read16(n + offsetof(struct model_node, parent_node_index)),
			readf(n + offsetof(struct model_node, default_translation)), readf(n + offsetof(struct model_node, default_translation) + 4),
			readf(n + offsetof(struct model_node, default_translation) + 8));
	}
	fprintf(stderr, "\n");
}

/* (HALO_H2_DEBUG) a kit graph's animations logged */
H2_ALIGN static void h2w_debug_graph(struct region *r, unsigned long instances, long index, char const *what)
{
	unsigned long root, elements;
	long count, i;

	if (!getenv("HALO_H2_DEBUG") || index < 0 || !(root = h2w_root(r, instances, index)))
		return;
	count = (long)read32(region_at(r, root + offsetof(struct animation_graph, animations)));
	elements = read32(region_at(r, root + offsetof(struct animation_graph, animations) + 4));
	fprintf(stderr, "h2 weapons: %s: %ld animations:", what, count);
	for (i = 0; i < count && elements; i++)
	{
		unsigned char const *a = region_at(r, elements - r->link + sizeof(struct animation) * i);

		fprintf(stderr, " '%s'(%d,%d frames,rot %lx trans %lx)", (char const *)a, read16(a + offsetof(struct animation, type)),
			read16(a + offsetof(struct animation, frame_count)), read32(a + offsetof(struct animation, nodes_with_rotation_flags)),
			read32(a + offsetof(struct animation, nodes_with_translation_flags)));
	}
	count = (long)read32(region_at(r, root + offsetof(struct animation_graph, vehicle_animations)));
	fprintf(stderr, "; vehicle animations blocks %ld\n", count);
}

/* a kit projectile's tag index (the first), for a slot that needs a
projectile's root; NONE */
H2_ALIGN static long h2w_any_projectile(struct region *r, unsigned long instances, long first)
{
	long i;

	for (i = 0; i < first; i++)
	{
		if (read32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * i)) == 'proj' && h2w_root(r, instances, i))
			return i;
	}
	return NONE;
}

/* the made weapons' tags written (their models are written: given their
nodes; the weapon, its graph, projectile and damage) - first the tag
index of the weapons' first, the models' first */
/* the effect (a tag index of r) copied into the tag at slot, its sound
parts playing the sound (h2w.sounds', its tag sound_first + it) - the first
of them; the others, or all when the sound is NONE, silent */
H2_ALIGN static void h2w_effect_make(struct region *r, unsigned long instances, long effect, long slot, short sound, long sound_first)
{
	unsigned long from = effect >= 0 ? h2w_root(r, instances, effect) : 0, root, events;
	long event_count, e;
	boolean placed = FALSE;

	root = from ? h2w_tag_copy(r, from, sizeof(struct effect_definition), 'effe') : region_alloc(r, sizeof(struct effect_definition));
	if (r->b.failed)
		return;
	write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * slot + 20), r->link + root);
	event_count = (long)read32(region_at(r, root + offsetof(struct effect_definition, events)));
	events = read32(region_at(r, root + offsetof(struct effect_definition, events) + 4));
	for (e = 0; e < event_count && events && !r->b.failed; e++)
	{
		unsigned long event = events - r->link + sizeof(struct effect_event_definition) * e;
		long part_count = (long)read32(region_at(r, event + offsetof(struct effect_event_definition, parts))), p;
		unsigned long parts = read32(region_at(r, event + offsetof(struct effect_event_definition, parts) + 4));

		for (p = 0; p < part_count && parts; p++)
		{
			unsigned long reference = parts - r->link + sizeof(struct effect_part_definition) * p +
				offsetof(struct effect_part_definition, reference);

			if (read32(region_at(r, reference)) != 'snd!')
				continue;
			/* (H2W_SOUND_KEEP: the stand-in effect's own sound stays) */
			if (sound == H2W_SOUND_KEEP)
				continue;
			if (sound != NONE && !placed)
			{
				region_reference(r, reference, 'snd!', tag_id(sound_first + sound));
				placed = TRUE;
			}
			else
				region_reference(r, reference, 0, 0xFFFFFFFFUL);
		}
	}
}

/* ---------- private code: Halo 2's projectiles and their damage */

/* an effect's (a kit tag; NONE) first damage part's damage effect (NONE) */
H2_ALIGN static long h2w_effect_damage_part(struct region *r, unsigned long instances, long effect)
{
	unsigned long root = effect >= 0 ? h2w_root(r, instances, effect) : 0, events;
	long event_count, e;

	if (!root)
		return NONE;
	event_count = (long)read32(region_at(r, root + offsetof(struct effect_definition, events)));
	events = read32(region_at(r, root + offsetof(struct effect_definition, events) + 4));
	for (e = 0; e < event_count && events; e++)
	{
		unsigned long event = events - r->link + sizeof(struct effect_event_definition) * e;
		long part_count = (long)read32(region_at(r, event + offsetof(struct effect_event_definition, parts))), p;
		unsigned long parts = read32(region_at(r, event + offsetof(struct effect_event_definition, parts) + 4));

		for (p = 0; p < part_count && parts; p++)
		{
			unsigned long reference = parts - r->link + sizeof(struct effect_part_definition) * p +
				offsetof(struct effect_part_definition, reference);

			if (read32(region_at(r, reference)) == 'jpt!')
				return (short)read16(region_at(r, reference + 12));
		}
	}
	return NONE;
}

/* a kit effect (NONE: an empty one) copied into slot, its damage parts the
damage effect damage_id (its first; the rest none - 0xFFFFFFFF: all none,
Halo 2's damage is elsewhere or none); the number of damage parts */
H2_ALIGN static long h2w_effect_damage_copy(struct region *r, unsigned long instances, long effect, long slot, unsigned long damage_id)
{
	unsigned long from = effect >= 0 ? h2w_root(r, instances, effect) : 0, root, events;
	long event_count, e, damage_parts = 0;

	root = from ? h2w_tag_copy(r, from, sizeof(struct effect_definition), 'effe') : region_alloc(r, sizeof(struct effect_definition));
	if (r->b.failed)
		return 0;
	write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * slot + 20), r->link + root);
	event_count = (long)read32(region_at(r, root + offsetof(struct effect_definition, events)));
	events = read32(region_at(r, root + offsetof(struct effect_definition, events) + 4));
	for (e = 0; e < event_count && events && !r->b.failed; e++)
	{
		unsigned long event = events - r->link + sizeof(struct effect_event_definition) * e;
		long part_count = (long)read32(region_at(r, event + offsetof(struct effect_event_definition, parts))), p;
		unsigned long parts = read32(region_at(r, event + offsetof(struct effect_event_definition, parts) + 4));

		for (p = 0; p < part_count && parts; p++)
		{
			unsigned long reference = parts - r->link + sizeof(struct effect_part_definition) * p +
				offsetof(struct effect_part_definition, reference);

			if (read32(region_at(r, reference)) != 'jpt!')
				continue;
			/* (Halo 2's detonation damages once: its first damage part,
			the others none) */
			if (damage_id != 0xFFFFFFFFUL && !damage_parts++)
				region_reference(r, reference, 'jpt!', damage_id);
			else
				region_reference(r, reference, 0, 0xFFFFFFFFUL);
		}
	}
	/* (a stand-in's detonation of no damage part - the needler's -: one
	added to its first event at its first location. Halo 2 damages the
	area about every detonation by its projectile's detonation damage,
	stuck or not - after the stuck one's parent takes its attached damage
	(projectiles.cpp function_fc330); this engine's area damage is its
	detonation effect's damage part's (effects.c). HALO_H2_NO_DETONATION_PART:
	none added, as before) */
	if (damage_id != 0xFFFFFFFFUL && !damage_parts && event_count > 0 && events &&
		read32(region_at(r, root + offsetof(struct effect_definition, locations))) > 0 && !getenv("HALO_H2_NO_DETONATION_PART"))
	{
		unsigned long event = events - r->link;
		unsigned long field = event + offsetof(struct effect_event_definition, parts);
		long part_count = (long)read32(region_at(r, field));
		unsigned long old = read32(region_at(r, field + 4)), block, part;

		block = region_block(r, field, part_count + 1, sizeof(struct effect_part_definition));
		if (r->b.failed)
			return 0;
		if (part_count > 0 && old)
			memcpy(region_at(r, block), region_at(r, old - r->link), sizeof(struct effect_part_definition) * (unsigned long)part_count);
		part = block + sizeof(struct effect_part_definition) * (unsigned long)part_count;
		memset(region_at(r, part), 0, sizeof(struct effect_part_definition));
		write32(region_at(r, part + offsetof(struct effect_part_definition, runtime_base_class_tag)), 'jpt!');
		region_reference(r, part + offsetof(struct effect_part_definition, reference), 'jpt!', damage_id);
		damage_parts = 1;
		platform_log("h2 weapons: detonation effect %ld: a damage part added (its stand-in's had none)", slot);
	}
	return damage_parts;
}

/* a damage effect's screen flashes Halo 2's: its player responses (0x6C,
0x4C each: response type - 0 shielded, 1 unshielded, 2 both - at 0, the
flash's type and priority at 4 and 6, duration 8, fade function 0xC,
maximum intensity 0x10, colour a r g b 0x14) - the unshielded one this
engine's screen flash, the shielded one beside it (damage_effect_definitions.h:
player_effects.c shows it while the shields hold). The stand-in's own
Halo CE flash is cleared: Halo 2 flashes for these alone (HALO_H2_NO_DAMAGE_FLASH:
none) */
H2_ALIGN static short h2w_transition_function(short h2);

H2_ALIGN static void h2w_damage_screen_flashes(struct h2_cache *cache, unsigned long damage, struct region *r, unsigned long root)
{
	unsigned long flash = root + offsetof(struct damage_effect_definition, screen_flash);
	unsigned long shielded = root + offsetof(struct damage_effect_definition, unused1);
	long count = 0, k;
	unsigned long responses = 0;
#define H2W_READ16(a) (h2_cache_at(cache, (a), 2) ? (short)(((unsigned char const *)h2_cache_at(cache, (a), 2))[0] | \
	((unsigned char const *)h2_cache_at(cache, (a), 2))[1] << 8) : (short)0)

	memset(region_at(r, flash), 0, sizeof(struct screen_flash_definition));
	memset(region_at(r, shielded), 0, sizeof(struct h2_shielded_screen_flash));
	if (getenv("HALO_H2_NO_DAMAGE_FLASH") || !h2_cache_block(cache, damage + 0x6C, 0x4C, &count, &responses))
		return;
	for (k = 0; k < count && k < 4; k++)
	{
		unsigned long e = responses + 0x4C * (unsigned long)k;
		short response = H2W_READ16(e), type = H2W_READ16(e + 4);
		unsigned long to[2];
		int n = 0, i, c;

		if (type <= 0 || type > 6)
			continue;
		if (response == 1 || response == 2)
			to[n++] = flash;
		if (response == 0 || response == 2)
		{
			write32(region_at(r, shielded), H2_SHIELDED_SCREEN_FLASH_MAGIC);
			to[n++] = shielded + offsetof(struct h2_shielded_screen_flash, screen_flash);
		}
		for (i = 0; i < n; i++)
		{
			write16(region_at(r, to[i] + offsetof(struct screen_flash_definition, type)), type);
			write16(region_at(r, to[i] + offsetof(struct screen_flash_definition, priority)), H2W_READ16(e + 6));
			writef(region_at(r, to[i] + offsetof(struct screen_flash_definition, duration)), h2w_read_real(cache, e + 8));
			/* (Halo 2's transition function as this engine's, as the camera's
			below: its 3, early - the square root of the time left, most bullets'
			and bolts' hit flashes - was read as this engine's 3, late - its
			square: the flash faded to a quarter by half its time) */
			write16(region_at(r, to[i] + offsetof(struct screen_flash_definition, fade_function)),
				h2w_transition_function(H2W_READ16(e + 0xC)));
			writef(region_at(r, to[i] + offsetof(struct screen_flash_definition, max_intensity)), h2w_read_real(cache, e + 0x10));
			for (c = 0; c < 4; c++)
				writef(region_at(r, to[i] + offsetof(struct screen_flash_definition, screen_flash_color) + 4 * (unsigned long)c),
					h2w_read_real(cache, e + 0x14 + 4 * (unsigned long)c));
		}
	}
#undef H2W_READ16
}

/* Halo 2's transition functions (linear, late, very late, early, very
early, cosine, zero, one) as this engine's (linear, early, very early,
late, very late, cosine); zero and one linear */
H2_ALIGN static short h2w_transition_function(short h2)
{
	static short const map[6] = { 0, 3, 4, 1, 2, 5 };

	return h2 >= 0 && h2 < 6 ? map[h2] : 0;
}

/* a damage effect's camera impulse and shaking Halo 2's (its jpt! at
damage, read with the kit's field names: after the player responses
(0x6C) its camera impulse - duration 0x74, fade function 0x78, rotation
0x7C, pushback 0x80, jitter bounds 0x84 - and camera shaking - duration
0x8C, falloff function 0x90, random translation 0x94, random rotation
0x98, wobble function 0x9C, its period 0xA0 and weight 0xA4) into this
engine's camera impulse (temporary: duration, transition, rotation,
translation, jitter; no permanent) and camera shake (duration, falloff,
random translation and rotation, periodic function, period, weight).
Halo 2 has no zero scale factors (none: the effect scales to nothing).
The stand-in's own (a Custom Edition grenade's, rocket's) replaced;
HALO_H2_NO_DAMAGE_CAMERA: kept. Not Halo 2's shake radius (0x64: its
shaking reaches past the damage radius - this engine shakes those it
damages) */
H2_ALIGN static void h2w_damage_camera(struct h2_cache *cache, unsigned long damage, struct region *r, unsigned long root, long tag)
{
	unsigned long impulse = root + offsetof(struct damage_effect_definition, camera_impulse);
	unsigned long shake = root + offsetof(struct damage_effect_definition, camera_shake);
	float period;

	if (getenv("HALO_H2_NO_DAMAGE_CAMERA"))
		return;
	memset(region_at(r, impulse), 0, sizeof(struct camera_impulse_definition));
	memset(region_at(r, shake), 0, sizeof(struct camera_shake_definition));
	writef(region_at(r, impulse + offsetof(struct camera_impulse_definition, temporary_duration)), h2w_read_real(cache, damage + 0x74));
	write16(region_at(r, impulse + offsetof(struct camera_impulse_definition, temporary_transition)),
		h2w_transition_function(h2w_read_short(cache, damage + 0x78)));
	writef(region_at(r, impulse + offsetof(struct camera_impulse_definition, temporary_rotation)), h2w_read_real(cache, damage + 0x7C));
	writef(region_at(r, impulse + offsetof(struct camera_impulse_definition, temporary_translation)), h2w_read_real(cache, damage + 0x80));
	writef(region_at(r, impulse + offsetof(struct camera_impulse_definition, temporary_jitter_lower_bound)), h2w_read_real(cache, damage + 0x84));
	writef(region_at(r, impulse + offsetof(struct camera_impulse_definition, temporary_jitter_upper_bound)), h2w_read_real(cache, damage + 0x88));
	writef(region_at(r, shake + offsetof(struct camera_shake_definition, duration)), h2w_read_real(cache, damage + 0x8C));
	write16(region_at(r, shake + offsetof(struct camera_shake_definition, falloff_transition_function)),
		h2w_transition_function(h2w_read_short(cache, damage + 0x90)));
	writef(region_at(r, shake + offsetof(struct camera_shake_definition, random_translation_magnitude)), h2w_read_real(cache, damage + 0x94));
	writef(region_at(r, shake + offsetof(struct camera_shake_definition, random_rotation_magnitude)), h2w_read_real(cache, damage + 0x98));
	{
		short wobble = h2w_read_short(cache, damage + 0x9C);

		write16(region_at(r, shake + offsetof(struct camera_shake_definition, periodic_function)),
			wobble >= 0 && wobble <= 11 ? wobble : 0);
	}
	/* (a period of none: one - player_effects.c divides the time by it;
	its weight none then, as Halo 2's are) */
	period = h2w_read_real(cache, damage + 0xA0);
	writef(region_at(r, shake + offsetof(struct camera_shake_definition, periodic_period)), period > 0.0f ? period : 1.0f);
	writef(region_at(r, shake + offsetof(struct camera_shake_definition, periodic_weight)), h2w_read_real(cache, damage + 0xA4));
	/* (Halo 2's shake radius, 0x64 - "player radius" in the decompilation's
	damage.cpp, function_d6c80: when it is past the damage radius, players
	between the two are added to the area damage at no damage, their
	screens shaken at 1 - distance / shake radius - the rocket's 12 against
	its damage's 2 -: kept in the tag's unused area bytes, marked, for
	damage.c's area_of_effect_cause_damage) */
	{
		struct h2_area_shake *area = (struct h2_area_shake *)region_at(r, root + offsetof(struct damage_effect_definition, area_of_effect_unused));
		float shake_radius = h2w_read_real(cache, damage + 0x64);

		area->magic = H2_AREA_SHAKE_MAGIC;
		area->shake_radius = shake_radius > 0.0f && shake_radius < 1000.0f ? shake_radius : 0.0f;
	}
	if (getenv("HALO_H2_WEAPON_LOG"))
		platform_log("h2 weapon fields: %s camera: impulse %.2f s fn %d rotation %.4f pushback %.3f jitter %.3f..%.3f; "
			"shake %.2f s fn %d translation %.3f rotation %.4f wobble %d period %.2f weight %.2f; shake radius %.2f, "
			"breaking forward %.1f outward %.1f", h2_cache_tag_name(cache, tag), h2w_read_real(cache, damage + 0x74),
			h2w_read_short(cache, damage + 0x78), h2w_read_real(cache, damage + 0x7C), h2w_read_real(cache, damage + 0x80),
			h2w_read_real(cache, damage + 0x84), h2w_read_real(cache, damage + 0x88), h2w_read_real(cache, damage + 0x8C),
			h2w_read_short(cache, damage + 0x90), h2w_read_real(cache, damage + 0x94), h2w_read_real(cache, damage + 0x98),
			h2w_read_short(cache, damage + 0x9C), h2w_read_real(cache, damage + 0xA0), h2w_read_real(cache, damage + 0xA4),
			h2w_read_real(cache, damage + 0x64), h2w_read_real(cache, damage + 0xB0), h2w_read_real(cache, damage + 0xBC));
}

/* a damage effect in slot: the kit's base (a jpt!; NONE: one of its own,
its materials all taking damage) with Halo 2's damage_tag's damage (its
bounds and multipliers h2_damage.c's), radius, impulse and stun */
/* a damage effect's values Halo 2's damage_tag's (NONE: none) - its
damage, radii, stun, acceleration, side effect, flags, category, screen
flashes and camera: as made (h2w_damage_make) and as loaded
(h2w_loaded_values_write) */
H2_ALIGN static void h2w_damage_values_write(struct h2_cache *cache, struct region *r, unsigned long root, long damage_tag)
{
	struct h2_cache_tag d;

	if (!cache || damage_tag == NONE || !h2_cache_tag_get(cache, damage_tag, &d))
		return;
	h2_damage_effect_write(cache, damage_tag, region_at(r, root), h2w_write_real);
	writef(region_at(r, root + offsetof(struct damage_effect_definition, falloff_radius)), h2w_read_real(cache, d.address));
	writef(region_at(r, root + offsetof(struct damage_effect_definition, cutoff_radius)), h2w_read_real(cache, d.address + 4));
	writef(region_at(r, root + offsetof(struct damage_effect_definition, cutoff_scale)), h2w_read_real(cache, d.address + 8));
	writef(region_at(r, root + offsetof(struct damage_effect_definition, damage.area_of_effect_core_radius)), h2w_read_real(cache, d.address + 0x18));
	writef(region_at(r, root + offsetof(struct damage_effect_definition, damage.active_camouflage_damage)), h2w_read_real(cache, d.address + 0x30));
	writef(region_at(r, root + offsetof(struct damage_effect_definition, damage.stun)), h2w_read_real(cache, d.address + 0x34));
	writef(region_at(r, root + offsetof(struct damage_effect_definition, damage.maximum_stun)), h2w_read_real(cache, d.address + 0x38));
	writef(region_at(r, root + offsetof(struct damage_effect_definition, damage.stun_time)), h2w_read_real(cache, d.address + 0x3C));
	/* (world units a second, as this engine's) */
	writef(region_at(r, root + offsetof(struct damage_effect_definition, damage.instantaneous_acceleration)), h2w_read_real(cache, d.address + 0x40));
	/* its side effect (0x10): "lethal to the unsuspecting", a melee from
	behind kills through the shield (MCC shared.map: strike, smash, slice and
	dash melee 2; the tag test's real engine: "object is killed instantly" on
	both species). The other values stay the stand-in's (harmless, emp are
	not this lane's) */
	if ((short)read16(h2_cache_at(cache, d.address + 0x10, 2)) == 2)
		write16(region_at(r, root + offsetof(struct damage_effect_definition, damage.side_effect)), 2);
	/* its flags: Halo 2's first ten are this engine's; its headshots are
	this engine's multiplayer ones too (bit 11) */
	{
		unsigned long flags = h2_cache_read32(cache, d.address + 0x14);

		write32(region_at(r, root + offsetof(struct damage_effect_definition, damage.flags)), (flags & 0x3FF) | ((flags & 2) ? 1UL << 11 : 0));
	}
	/* its category (0x12: Halo 2's enum is this engine's - none, falling,
	bullet, grenade, high explosive, sniper, melee, flame, mounted weapon,
	vehicle, plasma, needle), the kill's kind in Halo 2's kill lines
	(h2_hud.c): the sniper rifle's and beam rifle's damage the sniper's, as
	Halo 2 reports those two weapons' kills (unknown_157450.cpp: damage
	reporting types 13 and 14 are "sniped") though their category is the
	bullet's and plasma's */
	{
		short category = (short)h2w_read_short(cache, d.address + 0x12);
		char const *name = h2_cache_tag_name(cache, damage_tag);

		if (name && (strstr(name, "sniper_rifle\\") || strstr(name, "beam_rifle\\")) && category != 0)
			category = 5;
		if (category >= 0 && category < 13)
			write16(region_at(r, root + offsetof(struct damage_effect_definition, damage.category)), (unsigned short)category);
		/* (a grenade's attached detonation - the plasma grenade stuck to its
		victim -: this engine's damage flag 20, unused by it, marks it for
		Halo 2's "stuck" kill (units.c, h2_hud.c)) */
		if (name && strstr(name, "attached_explosion"))
		{
			unsigned long field = root + offsetof(struct damage_effect_definition, damage.flags);

			write32(region_at(r, field), read32(region_at(r, field)) | (1UL << 20));
		}
	}
	h2w_damage_screen_flashes(cache, d.address, r, root);
	h2w_damage_camera(cache, d.address, r, root, damage_tag);
}

H2_ALIGN static void h2w_damage_make(struct build *build, struct region *r, unsigned long instances, long slot, long base, long damage_tag)
{
	struct h2_cache *cache = build->cache;
	unsigned long from = base >= 0 ? h2w_root(r, instances, base) : 0, root;
	struct h2_cache_tag d;

	root = from ? h2w_tag_copy(r, from, sizeof(struct damage_effect_definition), 'jpt!') :
		region_alloc(r, sizeof(struct damage_effect_definition));
	if (r->b.failed)
		return;
	write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * slot + 20), r->link + root);
	if (!from)
	{
		long m;

		for (m = 0; m < MAXIMUM_NUMBER_OF_MATERIAL_TYPES; m++)
			writef(region_at(r, root + offsetof(struct damage_effect_definition, damage.material_modifiers) + 4 * m), 1.0f);
	}
	h2w_damage_values_write(cache, r, root, damage_tag);
}

/* a projectile made Halo 2's (its root a copy of the kit's stand-in's): its
speeds, ranges, timers, guidance and its damage effects - impact in
impact_slot, and (detonations: first the weapon's first tag, NONE none) its
detonation, attached, super and attached super damage and its detonation
and super detonation effects, the stand-in's with Halo 2's damage - and
the port data its rules read (h2_projectiles.h) */
/* a projectile's values Halo 2's (h2w_projectile_read's): its speeds,
range, gravity, timer, radii, guidance, flags and the port data
(h2_projectiles.h; attached_super its attached super detonation's damage
tag, NONE) - as made (h2w_projectile_write) and as loaded
(h2w_loaded_values_write) */
H2_ALIGN static void h2w_projectile_values_write(struct region *r, unsigned long root, struct h2w_projectile const *p,
	boolean detonations, long attached_super)
{
	/* ("faster when owned by player", bit 10: a player's at 1.5 times its
	speed - projectiles.cpp function_fd8b0; every shot here a player's) */
	float speed_scale = (p->flags & (1UL << 10)) ? 1.5f : 1.0f;
	struct h2_projectile_port port;
	unsigned long field, flags;

	writef(region_at(r, root + offsetof(struct projectile_definition, projectile.initial_velocity)), p->velocity[0] * speed_scale / 30.0f);
	writef(region_at(r, root + offsetof(struct projectile_definition, projectile.final_velocity)), p->velocity[1] * speed_scale / 30.0f);
	writef(region_at(r, root + offsetof(struct projectile_definition, projectile.minimum_velocity)), p->minimum_velocity / 30.0f);
	/* (its range; none: as far as its damage reaches, projectiles.cpp's
	disappearing past the air damage range's end) */
	writef(region_at(r, root + offsetof(struct projectile_definition, projectile.maximum_range)),
		p->maximum_range > 0.0f || p->timer[1] > 0.0f ? p->maximum_range : p->air_damage_range[1]);
	writef(region_at(r, root + offsetof(struct projectile_definition, projectile.air_gravity_scale)), p->air_gravity_scale);
	writef(region_at(r, root + offsetof(struct projectile_definition, projectile.water_gravity_scale)), p->water_gravity_scale);
	writef(region_at(r, root + offsetof(struct projectile_definition, projectile.timer_lower_bound)), p->timer[0]);
	writef(region_at(r, root + offsetof(struct projectile_definition, projectile.timer_upper_bound)), p->timer[1]);
	writef(region_at(r, root + offsetof(struct projectile_definition, projectile.arming_time)), p->arming_time);
	writef(region_at(r, root + offsetof(struct projectile_definition, projectile.collision_radius)), p->collision_radius);
	writef(region_at(r, root + offsetof(struct projectile_definition, projectile.danger_radius)), p->danger_radius);
	writef(region_at(r, root + offsetof(struct projectile_definition, projectile.ai_perception_radius)), p->ai_perception_radius);
	writef(region_at(r, root + offsetof(struct projectile_definition, projectile.guided_angular_velocity)), p->guided_angular_velocity[0]);
	/* (its timer: immediately, after the first bounce - "off any surface"
	too -, when at rest) */
	write16(region_at(r, root + offsetof(struct projectile_definition, projectile.detonation_timer_starts)),
		p->timer_starts == 3 ? 1 : p->timer_starts >= 0 && p->timer_starts <= 2 ? p->timer_starts : 0);
	/* its flags: Halo 2's first four are this engine's (oriented along its
	velocity - or its steering turning it, bit 6 -, ballistic aiming,
	detonation at the max time if attached, super-combining) */
	field = root + offsetof(struct projectile_definition, projectile.flags);
	flags = read32(region_at(r, field)) & ~0xFUL;
	flags |= p->flags & 0xFUL;
	if (p->flags & (1UL << 6))
		flags |= 1;
	write32(region_at(r, field), flags);
	memset(&port, 0, sizeof(port));
	port.tag = H2_PROJECTILE_PORT_TAG;
	port.attached_super_detonation_damage = attached_super;
	if (detonations && (p->flags & (1UL << 3)))
	{
		port.flags |= FLAG(_h2_projectile_super_combines_bit);
		port.super_detonation_count = p->super_detonation_count;
	}
	/* the port data (h2_projectiles.h) */
	port.guided_angular_velocity[0] = p->guided_angular_velocity[0];
	port.guided_angular_velocity[1] = p->guided_angular_velocity[1] > p->guided_angular_velocity[0] ?
		p->guided_angular_velocity[1] : p->guided_angular_velocity[0];
	port.acceleration_range[0] = p->acceleration_range[0];
	port.acceleration_range[1] = p->acceleration_range[1];
	if (p->velocity[0] != p->velocity[1])
		port.flags |= FLAG(_h2_projectile_accelerates_bit);
	/* ("travels instantaneously", bit 5: moved a tick's way as it is fired) */
	if ((p->flags & (1UL << 5)) && !getenv("HALO_H2_PROJECTILE_OLD_FIRST_MOVE"))
		port.flags |= FLAG(_h2_projectile_travels_instantaneously_bit);
	/* ("damage scales based on distance", bit 4: none left at the air
	damage range's end) */
	if ((p->flags & (1UL << 4)) && p->air_damage_range[1] > 0.0f)
	{
		port.flags |= FLAG(_h2_projectile_damage_scales_with_distance_bit);
		port.damage_range = p->air_damage_range[1];
	}
	memcpy(region_at(r, root + offsetof(struct projectile_definition, object.unused2) + 4 * H2_OBJECT_PORT_WORD), &port, sizeof(port));
	h2w_responses_write(r, root, &p->responses);
}

H2_ALIGN static void h2w_projectile_write(struct build *build, struct region *r, unsigned long instances, unsigned long root,
	struct h2w_projectile const *p, long impact_slot, long detonations, char const *name)
{
	unsigned long field;
	struct h2_projectile_port port;
	long stand_in_impact = (short)read16(region_at(r, root + offsetof(struct projectile_definition, projectile.impact_damage) + 12));
	long stand_in_attached = (short)read16(region_at(r, root + offsetof(struct projectile_definition, projectile.attached_detonation_damage) + 12));
	long stand_in_effect = (short)read16(region_at(r, root + offsetof(struct projectile_definition, projectile.effect) + 12));
	long stand_in_super = (short)read16(region_at(r, root + offsetof(struct projectile_definition, projectile.super_detonation) + 12));
	long effect_damage = h2w_effect_damage_part(r, instances, stand_in_effect);
	long super_damage = h2w_effect_damage_part(r, instances, stand_in_super);
	/* ("faster when owned by player", bit 10: a player's at 1.5 times its
	speed - projectiles.cpp function_fd8b0; every shot here a player's) */
	float speed_scale = (p->flags & (1UL << 10)) ? 1.5f : 1.0f;
	unsigned long flags;
	long damage_parts;

	if (p->tag == NONE)
		return;
	/* its damage effects */
	if (p->impact_damage != NONE)
	{
		h2w_damage_make(build, r, instances, impact_slot, stand_in_impact >= 0 ? stand_in_impact : effect_damage, p->impact_damage);
		region_reference(r, root + offsetof(struct projectile_definition, projectile.impact_damage), 'jpt!', tag_id(impact_slot));
	}
	else
		region_reference(r, root + offsetof(struct projectile_definition, projectile.impact_damage), 0, 0xFFFFFFFFUL);
	memset(&port, 0, sizeof(port));
	port.tag = H2_PROJECTILE_PORT_TAG;
	port.attached_super_detonation_damage = NONE;
	if (detonations != NONE)
	{
		/* (the detonation: the stand-in's effect, its damage Halo 2's) */
		if (p->detonation_damage != NONE)
			h2w_damage_make(build, r, instances, detonations + H2W_SLOT_DETONATION_DAMAGE, effect_damage, p->detonation_damage);
		damage_parts = h2w_effect_damage_copy(r, instances, stand_in_effect, detonations + H2W_SLOT_DETONATION_EFFECT,
			p->detonation_damage != NONE ? tag_id(detonations + H2W_SLOT_DETONATION_DAMAGE) : 0xFFFFFFFFUL);
		if (stand_in_effect >= 0)
			region_reference(r, root + offsetof(struct projectile_definition, projectile.effect), 'effe', tag_id(detonations + H2W_SLOT_DETONATION_EFFECT));
		if (p->detonation_damage != NONE && !damage_parts)
			platform_log("h2 weapons: %s: its stand-in's detonation has no damage part: no detonation damage", name);
		/* (attached: the parent's) */
		if (p->attached_detonation_damage != NONE)
		{
			h2w_damage_make(build, r, instances, detonations + H2W_SLOT_ATTACHED_DAMAGE,
				stand_in_attached >= 0 ? stand_in_attached : effect_damage, p->attached_detonation_damage);
			region_reference(r, root + offsetof(struct projectile_definition, projectile.attached_detonation_damage), 'jpt!',
				tag_id(detonations + H2W_SLOT_ATTACHED_DAMAGE));
		}
		else
			region_reference(r, root + offsetof(struct projectile_definition, projectile.attached_detonation_damage), 0, 0xFFFFFFFFUL);
		/* (the super detonation: its area damage the super effect's, the
		parent's its own) */
		if (p->flags & (1UL << 3))
		{
			if (p->super_detonation_damage != NONE)
				h2w_damage_make(build, r, instances, detonations + H2W_SLOT_SUPER_DAMAGE, super_damage, p->super_detonation_damage);
			damage_parts = h2w_effect_damage_copy(r, instances, stand_in_super, detonations + H2W_SLOT_SUPER_EFFECT,
				p->super_detonation_damage != NONE ? tag_id(detonations + H2W_SLOT_SUPER_DAMAGE) : 0xFFFFFFFFUL);
			if (stand_in_super >= 0)
				region_reference(r, root + offsetof(struct projectile_definition, projectile.super_detonation), 'effe', tag_id(detonations + H2W_SLOT_SUPER_EFFECT));
			if (p->super_detonation_damage != NONE && !damage_parts)
				platform_log("h2 weapons: %s: its stand-in's super detonation has no damage part: no super detonation area damage", name);
			if (p->attached_super_detonation_damage != NONE)
			{
				h2w_damage_make(build, r, instances, detonations + H2W_SLOT_ATTACHED_SUPER_DAMAGE,
					stand_in_attached >= 0 ? stand_in_attached : super_damage, p->attached_super_detonation_damage);
				port.attached_super_detonation_damage = (long)tag_id(detonations + H2W_SLOT_ATTACHED_SUPER_DAMAGE);
			}
		}
	}
	h2w_projectile_values_write(r, root, p, detonations != NONE, port.attached_super_detonation_damage);
}

/* a weapon's tags none was made for (the second trigger's projectile and
damage, the detonations) made: a copy of its projectile, damage effects
of no damage, empty effects - every tag of the level is one */
H2_ALIGN static void h2w_slots_fill(struct region *r, unsigned long instances, long weapon_index, unsigned long projectile_root)
{
	long k;

	for (k = 3; k < H2W_TAGS_PER_WEAPON && !r->b.failed; k++)
	{
		long slot = weapon_index + k;
		unsigned long root;

		/* (the bullet's damage, then the second trigger's and the rest -
		the melee's and lunge's h2w_melee_write's) */
		if (k > 3 && k < H2W_SLOT_SECOND_PROJECTILE)
			continue;

		if (read32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * slot + 20)))
			continue;
		if (k == H2W_SLOT_SECOND_PROJECTILE)
		{
			root = projectile_root ? h2w_tag_copy(r, projectile_root, sizeof(struct projectile_definition), 'proj') :
				region_alloc(r, sizeof(struct projectile_definition));
			write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * slot + 4), 'obje');
		}
		else if (k == H2W_SLOT_DETONATION_EFFECT || k == H2W_SLOT_SUPER_EFFECT || k == H2W_SLOT_READY_EFFECT || k == H2W_SLOT_OVERHEATED_EFFECT)
			root = region_alloc(r, sizeof(struct effect_definition));
		else
			root = region_alloc(r, sizeof(struct damage_effect_definition));
		if (r->b.failed)
			return;
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * slot + 20), r->link + root);
	}
}

/* the weapons' sound tags (their first: first), each the kit's template
sound with one pitch range and one permutation, no samples of its own: the
Halo 2 sound its name names is given it as the level loads (h2_sound.c) */
H2_ALIGN static void h2w_sounds_write(struct region *r, unsigned long instances, long first)
{
	unsigned long template_root = h2w.kit_sound != NONE ? h2w_root(r, instances, h2w.kit_sound) : 0;
	long i;

	for (i = 0; i < h2w.sound_count && !r->b.failed; i++)
	{
		unsigned long root, ranges, permutations;

		if (!template_root)
		{
			root = region_alloc(r, sizeof(struct sound_definition));
			write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (first + i) + 20), r->link + root);
			continue;
		}
		root = h2w_tag_copy(r, template_root, sizeof(struct sound_definition), 'snd!');
		if (r->b.failed)
			return;
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (first + i) + 20), r->link + root);
		ranges = read32(region_at(r, root + offsetof(struct sound_definition, pitch_ranges) + 4));
		if (!read32(region_at(r, root + offsetof(struct sound_definition, pitch_ranges))) || !ranges)
			continue;
		write32(region_at(r, root + offsetof(struct sound_definition, pitch_ranges)), 1);
		ranges -= r->link;
		permutations = read32(region_at(r, ranges + offsetof(struct sound_pitch_range, permutations) + 4));
		if (!read32(region_at(r, ranges + offsetof(struct sound_pitch_range, permutations))) || !permutations)
			continue;
		write32(region_at(r, ranges + offsetof(struct sound_pitch_range, permutations)), 1);
		write16(region_at(r, ranges + offsetof(struct sound_pitch_range, actual_permutation_count)), 1);
		permutations -= r->link;
		write16(region_at(r, permutations + offsetof(struct sound_permutation, next_permutation_index)), NONE);
		memset(region_at(r, permutations + offsetof(struct sound_permutation, samples)), 0, sizeof(struct tag_data));
		memset(region_at(r, permutations + offsetof(struct sound_permutation, mouth_data)), 0, sizeof(struct tag_data));
		memset(region_at(r, permutations + offsetof(struct sound_permutation, subtitle_data)), 0, sizeof(struct tag_data));
		write32(region_at(r, permutations + offsetof(struct sound_permutation, sample_buffer_size)), 0);
		/* (a permutation names its tag) */
		write32(region_at(r, permutations + offsetof(struct sound_permutation, unknown3)), tag_id(first + i));
	}
}

/* a weapon's melee Halo 2's (its weap root, its first tag weapon_index):
its melee damage and its lunge's (the stand-in's melee damage, Halo 2's
numbers - its minimum the jpt's lower bound, its bounds the upper bound's,
as damage.c's lerp between them by the strike's scale reads them; no
scale of this engine's own: the material modifiers h2_damage.c's), its damage
pyramid, _weapon_h2_melee_bit */
/* a made weapon's melee (k 0) or lunge (1) damage's values Halo 2's - as
made (h2w_melee_write) and as loaded (h2w_loaded_values_write) */
H2_ALIGN static void h2w_melee_values_write(struct h2_cache *cache, struct region *r, unsigned long damage_root,
	struct h2w_weapon const *w, int k)
{
	float const *hit = k ? w->lunge_hit : w->melee_hit;

	if (hit[2] > 0.0f)
	{
		writef(region_at(r, damage_root + offsetof(struct damage_effect_definition, damage.damage_minimum)), hit[0]);
		writef(region_at(r, damage_root + offsetof(struct damage_effect_definition, damage.damage_lower_bound)), hit[1]);
		writef(region_at(r, damage_root + offsetof(struct damage_effect_definition, damage.damage_upper_bound)), hit[2]);
		writef(region_at(r, damage_root + offsetof(struct damage_effect_definition, damage.instantaneous_acceleration)), hit[3]);
		/* (its flags: Halo 2's first ten this engine's, its headshots
		the multiplayer ones too - as the bullets' - none for the melee's:
		the stand-in's own made a strike to the head a kill) */
		write32(region_at(r, damage_root + offsetof(struct damage_effect_definition, damage.flags)),
			(w->hit_flags[k] & 0x3FF) | ((w->hit_flags[k] & 2) ? 1UL << 11 : 0));
		h2w_cache_damage_modifiers_write(cache, r, damage_root, w->hit_tags[k]);
	}
}

/* its melee's pyramid and Halo 2's melee flag, where Halo 2's melee is its
own (its pyramid and first hit's damage) */
H2_ALIGN static void h2w_melee_root_values_write(struct region *r, unsigned long root, struct h2w_weapon const *w)
{
	unsigned long flags;
	int k;

	if (w->melee_pyramid[2] <= 0.0f || w->melee_hit[2] <= 0.0f)
		return;
	for (k = 0; k < 3; k++)
		writef(region_at(r, root + offsetof(struct weapon_definition, weapon.port_melee_pyramid) + 4 * k), w->melee_pyramid[k]);
	flags = read32(region_at(r, root + offsetof(struct weapon_definition, weapon.flags)));
	write32(region_at(r, root + offsetof(struct weapon_definition, weapon.flags)), flags | FLAG(_weapon_h2_melee_bit));
}

H2_ALIGN static void h2w_melee_write(struct build *build, struct region *r, unsigned long instances, unsigned long root,
	long weapon_index, struct h2w_weapon const *w)
{
	long melee = (short)read16(region_at(r, root + offsetof(struct weapon_definition, weapon.melee_attack_damage) + 12));
	unsigned long melee_root = melee >= 0 ? h2w_root(r, instances, melee) : 0;
	unsigned long flags;
	int k;

	for (k = 0; k < 2 && !r->b.failed; k++)
	{
		float const *hit = k ? w->lunge_hit : w->melee_hit;
		long tag = weapon_index + (k ? H2W_TAG_LUNGE : H2W_TAG_MELEE);
		unsigned long damage_root = melee_root ? h2w_tag_copy(r, melee_root, sizeof(struct damage_effect_definition), 'jpt!') :
			region_alloc(r, sizeof(struct damage_effect_definition));

		if (r->b.failed)
			return;
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * tag + 20), r->link + damage_root);
		h2w_melee_values_write(build->cache, r, damage_root, w, k);
	}
	if (r->b.failed || w->melee_pyramid[2] <= 0.0f || w->melee_hit[2] <= 0.0f)
		return;
	region_reference(r, root + offsetof(struct weapon_definition, weapon.melee_attack_damage), 'jpt!',
		tag_id(weapon_index + H2W_TAG_MELEE));
	write32(region_at(r, root + offsetof(struct weapon_definition, weapon.port_lunge_damage)),
		w->lunge_hit[2] > 0.0f ? tag_id(weapon_index + H2W_TAG_LUNGE) : 0xFFFFFFFFUL);
	/* its strikes' self damage Halo 2's (none where Halo 2 has none, not
	the stand-in's melee response): the kit's response a base, Halo 2's
	values (h2w_damage_values_write: its camera's impulse and shaking, its
	flashes, no damage) */
	{
		long response = (short)read16(region_at(r, root + offsetof(struct weapon_definition, weapon.melee_attack_response) + 12));
		unsigned long response_root = response >= 0 ? h2w_root(r, instances, response) : 0;

		for (k = 0; k < 2 && !r->b.failed; k++)
		{
			long tag = weapon_index + (k ? H2W_SLOT_LUNGE_RESPONSE : H2W_SLOT_MELEE_RESPONSE);
			unsigned long damage_root;

			if (w->response_tags[k] == NONE)
				continue;
			damage_root = response_root ? h2w_tag_copy(r, response_root, sizeof(struct damage_effect_definition), 'jpt!') :
				region_alloc(r, sizeof(struct damage_effect_definition));
			if (r->b.failed)
				return;
			write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * tag + 20), r->link + damage_root);
			h2w_damage_values_write(build->cache, r, damage_root, w->response_tags[k]);
		}
		region_reference(r, root + offsetof(struct weapon_definition, weapon.melee_attack_response), w->response_tags[0] != NONE ? 'jpt!' : 0,
			w->response_tags[0] != NONE ? tag_id(weapon_index + H2W_SLOT_MELEE_RESPONSE) : 0xFFFFFFFFUL);
		write32(region_at(r, root + offsetof(struct weapon_definition, weapon.port_lunge_response)),
			w->response_tags[1] != NONE ? tag_id(weapon_index + H2W_SLOT_LUNGE_RESPONSE) : 0xFFFFFFFFUL);
	}
	h2w_melee_root_values_write(r, root, w);
	platform_log("h2 weapons: %s melee %.0f/%.0f-%.0f, lunge %.0f/%.0f-%.0f, pyramid %.3fx%.3f wu at %.2f deep", w->name,
		w->melee_hit[0], w->melee_hit[1], w->melee_hit[2], w->lunge_hit[0], w->lunge_hit[1], w->lunge_hit[2],
		w->melee_pyramid[0], w->melee_pyramid[1], w->melee_pyramid[2]);
}

/* the damage root an effect's parts name (the first damage), 0 if none */
H2_ALIGN static unsigned long h2w_effect_damage(struct region *r, unsigned long instances, long effect, long *tag)
{
	unsigned long root = effect >= 0 ? h2w_root(r, instances, effect) : 0, events;
	long event_count, e;

	if (!root)
		return 0;
	event_count = (long)read32(region_at(r, root + offsetof(struct effect_definition, events)));
	events = read32(region_at(r, root + offsetof(struct effect_definition, events) + 4));
	for (e = 0; e < event_count && events; e++)
	{
		unsigned long event = events - r->link + sizeof(struct effect_event_definition) * e;
		long part_count = (long)read32(region_at(r, event + offsetof(struct effect_event_definition, parts))), p;
		unsigned long parts = read32(region_at(r, event + offsetof(struct effect_event_definition, parts) + 4));

		for (p = 0; p < part_count && parts; p++)
		{
			unsigned long reference = parts - r->link + sizeof(struct effect_part_definition) * p +
				offsetof(struct effect_part_definition, reference);

			if (read32(region_at(r, reference)) == 'jpt!' && read32(region_at(r, reference + 12)) != 0xFFFFFFFFUL)
			{
				long index = (short)read16(region_at(r, reference + 12));
				unsigned long damage = h2w_root(r, instances, index);

				if (damage)
				{
					*tag = index;
					return damage;
				}
			}
		}
	}
	return 0;
}

/* an effect's (a kit tag) damage parts but those naming keep: none */
H2_ALIGN static void h2w_effect_other_damage_none(struct region *r, unsigned long instances, long effect, long keep)
{
	unsigned long root = effect >= 0 ? h2w_root(r, instances, effect) : 0, events;
	long event_count, e;

	if (!root)
		return;
	event_count = (long)read32(region_at(r, root + offsetof(struct effect_definition, events)));
	events = read32(region_at(r, root + offsetof(struct effect_definition, events) + 4));
	for (e = 0; e < event_count && events; e++)
	{
		unsigned long event = events - r->link + sizeof(struct effect_event_definition) * e;
		long part_count = (long)read32(region_at(r, event + offsetof(struct effect_event_definition, parts))), p;
		unsigned long parts = read32(region_at(r, event + offsetof(struct effect_event_definition, parts) + 4));

		for (p = 0; p < part_count && parts; p++)
		{
			unsigned long reference = parts - r->link + sizeof(struct effect_part_definition) * p +
				offsetof(struct effect_part_definition, reference);

			if (read32(region_at(r, reference)) != 'jpt!' || read32(region_at(r, reference + 12)) == 0xFFFFFFFFUL ||
				(short)read16(region_at(r, reference + 12)) == keep)
			{
				continue;
			}
			if (getenv("HALO_H2_DEBUG") || getenv("HALO_H2_GRENADE_LOG"))
				fprintf(stderr, "h2 grenade: effect %ld: damage part %d (event %ld part %ld) none\n", effect,
					(int)(short)read16(region_at(r, reference + 12)), e, p);
			region_reference(r, reference, 0, 0xFFFFFFFFUL);
		}
	}
}

/* a damage written with Halo 2's: the radii, the damage at the middle (its
upper bound) and at the edge (its lower bound, this engine's minimum) */
H2_ALIGN static void h2w_grenade_jpt_write(struct region *r, unsigned long damage_root, struct h2w_grenade_jpt const *j)
{
	writef(region_at(r, damage_root + offsetof(struct damage_effect_definition, falloff_radius)), j->radius[0]);
	writef(region_at(r, damage_root + offsetof(struct damage_effect_definition, cutoff_radius)), j->radius[1]);
	writef(region_at(r, damage_root + offsetof(struct damage_effect_definition, damage.area_of_effect_core_radius)), j->core_radius);
	writef(region_at(r, damage_root + offsetof(struct damage_effect_definition, damage.damage_minimum)), j->damage_lower);
	writef(region_at(r, damage_root + offsetof(struct damage_effect_definition, damage.damage_lower_bound)), j->damage_upper[0]);
	writef(region_at(r, damage_root + offsetof(struct damage_effect_definition, damage.damage_upper_bound)), j->damage_upper[1]);
	if (j->acceleration > 0.0f)
		writef(region_at(r, damage_root + offsetof(struct damage_effect_definition, damage.instantaneous_acceleration)), j->acceleration);
}

/* a grenade's damage category Halo 2's (jpt! 0x12, this engine's enum): the
kit's copies have none, and a kill's kind reads it (h2_hud.c) */
H2_ALIGN static void h2w_grenade_category_write(struct h2_cache *cache, struct region *r, unsigned long damage_root, long tag)
{
	struct h2_cache_tag d;

	if (cache && tag != NONE && h2_cache_tag_get(cache, tag, &d))
	{
		short category = (short)read16((unsigned char const *)h2_cache_at(cache, d.address + 0x12, 2));

		if (category >= 0 && category < 13)
			write16(region_at(r, damage_root + offsetof(struct damage_effect_definition, damage.category)), (unsigned short)category);
	}
}

/* the kit's grenade projectiles written with Halo 2's: when the detonation
timer runs and for how long, the arming, the explosion's radii and damage,
the damage of a stuck one, how the grenade bounces */
/* a grenade's explosion's damage values Halo 2's: its radii and damage,
material modifiers, camera and screen flashes */
H2_ALIGN static void h2w_grenade_explosion_values_write(struct h2_cache *cache, struct region *r, unsigned long damage_root,
	struct h2w_grenade_data const *g)
{
	struct h2_cache_tag jd;

	h2w_grenade_jpt_write(r, damage_root, &g->explosion);
	h2w_cache_damage_modifiers_write(cache, r, damage_root, g->explosion.tag);
	/* (its camera impulse, shaking and shake radius Halo 2's too: the
	kit's grenades' shook as the kit's) */
	if (cache && g->explosion.tag != NONE && h2_cache_tag_get(cache, g->explosion.tag, &jd))
	{
		h2w_damage_camera(cache, jd.address, r, damage_root, g->explosion.tag);
		/* (its flashes Halo 2's: the kit grenade's full yellow, 0.7 for a
		second, lit the whole screen from any blast in reach, the owner's
		playtest) */
		h2w_damage_screen_flashes(cache, jd.address, r, damage_root);
	}
}

/* a grenade's values Halo 2's (h2w_grenade_read's) on the kit's grenade
projectile at root, its explosion's damage at damage_root (0: none) and
its stuck one's at attached_root: as made (h2w_grenades_write, instances
the tag table's - the stuck one copied from the explosion here, slot its
tag) and as loaded (instances NONE: the stuck one's tag there already).
Returns the stuck one's root */
H2_ALIGN static unsigned long h2w_grenade_values_write(struct h2_cache *cache, struct region *r, unsigned long root,
	struct h2w_grenade_data const *g, unsigned long damage_root, unsigned long attached_root, long slot, unsigned long instances)
{
	unsigned long responses;
	long count, e;

	write16(region_at(r, root + offsetof(struct projectile_definition, projectile.detonation_timer_starts)), (unsigned short)g->timer_starts);
	writef(region_at(r, root + offsetof(struct projectile_definition, projectile.arming_time)), g->arming);
	writef(region_at(r, root + offsetof(struct projectile_definition, projectile.danger_radius)), g->danger_radius);
	writef(region_at(r, root + offsetof(struct projectile_definition, projectile.timer_lower_bound)), g->timer[0]);
	writef(region_at(r, root + offsetof(struct projectile_definition, projectile.timer_upper_bound)), g->timer[1]);
	writef(region_at(r, root + offsetof(struct projectile_definition, projectile.air_gravity_scale)), g->gravity_scale);
	/* (a stuck one's timer is the lower bound: "detonation max time if attached", bit 2 in both games) */
	{
		unsigned long flags_at = root + offsetof(struct projectile_definition, projectile.flags);

		write32(region_at(r, flags_at), (read32(region_at(r, flags_at)) & ~4UL) | (g->flags & 4UL));
	}
	if (damage_root)
	{
		h2w_grenade_explosion_values_write(cache, r, damage_root, g);
		/* (what a stuck one's tag has: the explosion's, its radii and damage Halo 2's attached) */
		if (g->attached.valid)
		{
			/* (made: a copy of the explosion's; loaded: the explosion's
			values written on it as they were before it was copied) */
			if (instances != NONE)
			{
				attached_root = h2w_tag_copy(r, damage_root, sizeof(struct damage_effect_definition), 'jpt!');
				write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * slot + 20), r->link + attached_root);
			}
			else if (attached_root)
				h2w_grenade_explosion_values_write(cache, r, attached_root, g);
			if (!attached_root)
				goto attached_done;
			h2w_grenade_jpt_write(r, attached_root, &g->attached);
			h2w_cache_damage_modifiers_write(cache, r, attached_root, g->attached.tag);
			writef(region_at(r, attached_root + offsetof(struct damage_effect_definition, damage.instantaneous_acceleration)),
				g->attached.acceleration);
			if (instances != NONE)
				region_reference(r, root + offsetof(struct projectile_definition, projectile.attached_detonation_damage), 'jpt!',
					tag_id(slot));
			/* (the stuck one's: this engine's flag 20, Halo 2's "stuck" kill, as
			h2w_damage_make marks a projectile's attached_explosion) */
			{
				unsigned long field = attached_root + offsetof(struct damage_effect_definition, damage.flags);

				write32(region_at(r, field), read32(region_at(r, field)) | (1UL << 20));
			}
			h2w_grenade_category_write(cache, r, attached_root, g->attached.tag);
		}
	attached_done:
		h2w_grenade_category_write(cache, r, damage_root, g->explosion.tag);
	}
	/* its material responses Halo 2's: the bounce (its frictions), passing
	through water, sticking, a material type each (h2w_responses_write) */
	h2w_responses_write(r, root, &g->responses);
	count = (long)read32(region_at(r, root + offsetof(struct projectile_definition, projectile.material_responses)));
	responses = read32(region_at(r, root + offsetof(struct projectile_definition, projectile.material_responses) + 4));
	for (e = 0; e < count && responses && getenv("HALO_H2_GRENADE_LOG"); e++)
	{
		unsigned long element = responses - r->link + sizeof(struct projectile_material_response_definition) * e;

		fprintf(stderr, "h2 grenade: slot %ld response %ld: default %d potential %d flags %04x chance-skip %.2f friction %.2f/%.2f\n", slot, e,
			(short)read16(region_at(r, element + offsetof(struct projectile_material_response_definition, default_response))),
			(short)read16(region_at(r, element + offsetof(struct projectile_material_response_definition, potential_response))),
			read16(region_at(r, element + offsetof(struct projectile_material_response_definition, potential_flags))),
			readf(region_at(r, element + offsetof(struct projectile_material_response_definition, potential_skip_fraction))),
			readf(region_at(r, element + offsetof(struct projectile_material_response_definition, parallel_friction))),
			readf(region_at(r, element + offsetof(struct projectile_material_response_definition, perpendicular_friction))));
	}
	return attached_root;
}

H2_ALIGN static void h2w_grenades_write(struct build *build, struct region *r, unsigned long instances, long first)
{
	long i;

	for (i = 0; i < H2W_GRENADE_COUNT; i++)
	{
		struct h2w_grenade_data const *g = &h2w.grenade_data[i];
		unsigned long root = h2w.grenade_projectile[i] >= 0 ? h2w_root(r, instances, h2w.grenade_projectile[i]) : 0;
		unsigned long damage_root = 0, attached_root, responses;
		long damage_tag = NONE, count, e;
		long slot = first + h2w_sound_first() + h2w.sound_count + i;

		if (g->valid && root)
		{
			long effect = (short)read16(region_at(r, root + offsetof(struct projectile_definition, projectile.effect) + 12));

			if (read32(region_at(r, root + offsetof(struct projectile_definition, projectile.effect) + 12)) == 0xFFFFFFFFUL)
				effect = NONE;
			damage_root = h2w_effect_damage(r, instances, effect, &damage_tag);
			/* (Halo 2's detonation damages once, its explosion's: the
			kit's effect's other damage parts - the plasma grenade's
			second, this engine's explosion of 120 and its own multipliers
			on top of Halo 2's - none) */
			if (damage_root && !getenv("HALO_H2_GRENADE_PARTS_AS_IS"))
				h2w_effect_other_damage_none(r, instances, effect, damage_tag);
		}
		/* (the stuck damage's tag: the explosion's copy, or an empty one) */
		attached_root = damage_root ? h2w_tag_copy(r, damage_root, sizeof(struct damage_effect_definition), 'jpt!') :
			region_alloc(r, sizeof(struct damage_effect_definition));
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * slot + 20), r->link + attached_root);
		if (!g->valid || !root || r->b.failed)
			continue;
		attached_root = h2w_grenade_values_write(build->cache, r, root, g, damage_root, attached_root, slot, instances);
		if (getenv("HALO_H2_DEBUG") || getenv("HALO_H2_GRENADE_LOG"))
			fprintf(stderr, "h2 grenade: kit %ld written: damage tag %ld, %ld responses\n", i, damage_tag, count);
	}
}

/* a time in seconds as a fraction a tick (0: all at once) */
H2_ALIGN static float h2w_per_tick(float seconds)
{
	return seconds > 0.0f ? 1.0f / (seconds * 30.0f) : 1.0f;
}

/* the made weapons' fields the engine reads (items/weapons.c: the dual
wielded error and damage scale, the recoil) by weapon tag, h2_weapons.h */
static struct h2_weapon_fields h2w_fields[H2W_MAXIMUM_WEAPONS];
static long h2w_fields_count;
H2_ALIGN static void h2w_fields_fill(struct h2_weapon_fields *f, struct h2w_weapon const *w, long definition_index);

H2_ALIGN static void h2w_fields_add(struct h2w_weapon const *w, long definition_index)
{
	if (h2w_fields_count >= H2W_MAXIMUM_WEAPONS)
		return;
	h2w_fields_fill(&h2w_fields[h2w_fields_count++], w, definition_index);
}

H2_ALIGN static void h2w_fields_fill(struct h2_weapon_fields *f, struct h2w_weapon const *w, long definition_index)
{
	memset(f, 0, sizeof(*f));
	f->definition_index = definition_index;
	f->dual_error_rise = h2w_per_tick(w->dual_error_times[0]);
	f->dual_error_fall = h2w_per_tick(w->dual_error_times[1]);
	f->dual_error_minimum = w->dual_error_minimum;
	f->dual_error_lower = w->dual_error_angle[0];
	f->dual_error_upper = w->dual_error_angle[1];
	f->dual_damage_scale = w->dual_damage_scale > 0.0f ? w->dual_damage_scale : 1.0f;
	f->h2_flags = w->h2_flags;
	f->recoil_lower = w->recoil_angle[0];
	f->recoil_upper = w->recoil_angle[1];
	f->recoil_rise = h2w_per_tick(w->recoil_times[0]);
	f->recoil_fall = h2w_per_tick(w->recoil_times[1]);
	f->recoil_function = w->recoil_function;
	f->fire_recovery_time = w->burst_recovery > 0.0f ? w->burst_recovery : 0.0f;
	/* (a single-shot weapon's rate was made from its recovery, h2w's "no rate, the shot's recovery its time") */
	f->paced_by_rounds_per_second = w->rounds_per_second > 0.0f &&
		!(w->burst_recovery > 0.0f && fabsf(w->rounds_per_second * w->burst_recovery - 1.0f) < 0.01f);
	/* (Halo 2's heat lost a second while overheated, weap @168: the
	plasma rifle's and the Sentinel Beam's 0.35, slower than their 0.85) */
	f->overheated_heat_loss = w->heat[1] > 0.0f && w->heat[6] > 0.0f && w->heat[6] <= 1.0f ? w->heat[6] : 0.0f;
}

/* the loaded level's (h2_weapon_fields_map_loaded): read from its MCC map
as it loads - a level loaded from its file has its weapons' fields too, not
only one made in this session (Nov 3 2026: they were filled only as a level
was made, so the Vita, which plays levels Setup made, and any session after
the first played Halo 2's weapons without their dual wielded error and
damage scale, recoil and overheated heat loss). NONE count: not read (the
reader off, HALO_H2_WEAPON_FIELDS_READER=0): the made level's table, as
before */
static struct h2_weapon_fields h2w_loaded_fields[H2W_MAXIMUM_WEAPONS];
static long h2w_loaded_count = NONE;

struct h2_weapon_fields const *h2_weapon_fields_get(long definition_index)
{
	struct h2_weapon_fields const *fields = h2w_loaded_count != NONE ? h2w_loaded_fields : h2w_fields;
	long count = h2w_loaded_count != NONE ? h2w_loaded_count : h2w_fields_count;
	long i;

	if (!h2_damage_rules_fast())
		return NULL;
	for (i = 0; i < count; i++)
	{
		if (fields[i].definition_index == definition_index)
			return &fields[i];
	}
	return NULL;
}

/* (cache_files.c, as a map's tags load) the loaded level's weapons' fields
read from its MCC map, as h2_weapons_write writes a level's: each made
weapon (h2\\weapons\\<name>, with triggers) its Halo 2 weapon's. Nothing for
a level that is not Halo 2's. HALO_H2_WEAPON_FIELDS_CHECK=1: in a session
that made the level, each compared with the made table's */
H2_ALIGN static void h2w_weapon_root_values_write(struct region *r, unsigned long root, struct h2w_weapon const *w);

H2_ALIGN static unsigned long h2w_grenade_values_write(struct h2_cache *cache, struct region *r, unsigned long root,
	struct h2w_grenade_data const *g, unsigned long damage_root, unsigned long attached_root, long slot, unsigned long instances);
H2_ALIGN static unsigned long h2w_effect_damage(struct region *r, unsigned long instances, long effect, long *tag);
H2_ALIGN static long h2v_values_map_loaded(struct h2_cache *cache, struct region *loaded);
H2_ALIGN static void h2w_magazine_values_write(struct region *r, unsigned long magazines, struct h2w_weapon const *w);
H2_ALIGN static void h2w_box_values_write(struct region *r, unsigned long boxes, struct h2w_weapon const *w);
H2_ALIGN static void h2w_trigger_values_write(struct region *r, unsigned long root, unsigned long triggers, struct h2w_weapon *w);

/* (h2_weapon_fields_map_loaded) a loaded made weapon's values written as
h2_weapons_write writes them: its root's, its first magazine's and box's
(a magazine's when it has rounds), its first trigger's (none: a sword).
HALO_H2_WEAPON_VALUES_CHECK=1: the bytes each changed - none on a level
this builder made */
/* (h2w_loaded_values_write) the loaded weapon made's slot's tag (its index
the weapon's and slot, the builder's own tag ids), checked by its name:
NULL if it is not that tag */
H2_ALIGN static unsigned char *h2w_loaded_slot(long made, long slot, char const *weapon_name)
{
	long index = (made & 0xFFFF) + slot;
	long datum = (long)((((0xE174UL + (unsigned long)index) & 0xFFFF) << 16) | (unsigned long)index);
	char expected[256];
	char const *name;

	if (slot < 0 || slot >= H2W_TAGS_PER_WEAPON)
		return NULL;
	snprintf(expected, sizeof(expected), "%s%s", weapon_name, h2w_slot_names[slot]);
	if (!(name = tag_get_name(datum)) || strcmp(name, expected))
		return NULL;
	return (unsigned char *)tag_get(h2w_slot_groups[slot], datum);
}

/* (h2w_loaded_values_write) a made damage slot's values Halo 2's
damage_tag's, where the builder made it (damage_tag not NONE) and, where
named, the reference to it is its (referenced NONE: not checked) */
H2_ALIGN static void h2w_loaded_damage(struct region *loaded, struct h2_cache *cache, long made, long slot, char const *name,
	long damage_tag, long referenced)
{
	unsigned char *damage;

	if (damage_tag == NONE || (referenced != NONE && (referenced & 0xFFFF) != (made & 0xFFFF) + slot))
		return;
	if ((damage = h2w_loaded_slot(made, slot, name)) != NULL)
		h2w_damage_values_write(cache, loaded, (unsigned long)damage - loaded->link, damage_tag);
}

H2_ALIGN static void h2w_loaded_values_write(struct region *loaded, struct h2_cache *cache, long made, struct weapon_definition *definition,
	struct h2w_weapon *w, char const *name)
{
	struct { unsigned char *at; unsigned long size; unsigned char *before; } parts[4];
	unsigned char *slots[H2W_TAGS_PER_WEAPON], *slot_before[H2W_TAGS_PER_WEAPON];
	boolean check = getenv("HALO_H2_WEAPON_VALUES_CHECK") != NULL;
	long part_count = 0, k;

	parts[part_count].at = (unsigned char *)definition, parts[part_count++].size = sizeof(*definition);
	if (w->rounds_loaded > 0 && definition->weapon.magazines.count > 0)
	{
		struct weapon_magazine_definition *magazine = (struct weapon_magazine_definition *)definition->weapon.magazines.address;

		parts[part_count].at = (unsigned char *)magazine, parts[part_count++].size = sizeof(*magazine);
		if (magazine->ammunition_objects.count > 0)
			parts[part_count].at = (unsigned char *)magazine->ammunition_objects.address, parts[part_count++].size = 2;
	}
	if (definition->weapon.triggers.count > 0)
		parts[part_count].at = (unsigned char *)definition->weapon.triggers.address,
			parts[part_count++].size = sizeof(struct weapon_trigger_definition);
	for (k = 0; k < part_count; k++)
	{
		parts[k].before = check ? malloc(parts[k].size) : NULL;
		if (parts[k].before)
			memcpy(parts[k].before, parts[k].at, parts[k].size);
	}
	/* (HALO_H2_WEAPON_VALUES_PERTURB=1, a test's: a value of each part the
	writes below set spoiled first - the game then plays as without it only if
	they all are written) */
	if (getenv("HALO_H2_WEAPON_VALUES_PERTURB"))
	{
		definition->weapon.aim_assist_parameters.autoaim_angle = 9.0f;
		if (w->rounds_loaded > 0 && definition->weapon.magazines.count > 0)
			((struct weapon_magazine_definition *)definition->weapon.magazines.address)->rounds_loaded_maximum = 1;
		if (definition->weapon.triggers.count > 0)
			((struct weapon_trigger_definition *)definition->weapon.triggers.address)->projectile_error_angle_upper_bound = 1.0f;
		for (k = 0; k < H2W_TAGS_PER_WEAPON; k++)
		{
			unsigned char *slot = h2w_slot_groups[k] == 'proj' || h2w_slot_groups[k] == 'jpt!' ? h2w_loaded_slot(made, k, name) : NULL;

			if (slot && h2w_slot_groups[k] == 'proj')
				((struct projectile_definition *)slot)->projectile.initial_velocity = 0.01f;
			else if (slot)
				((struct damage_effect_definition *)slot)->damage.damage_upper_bound = 999.0f;
		}
	}
	/* (the check: its projectiles' and damage tags' too) */
	for (k = 0; check && k < H2W_TAGS_PER_WEAPON; k++)
	{
		unsigned long size = h2w_slot_groups[k] == 'proj' ? sizeof(struct projectile_definition) :
			h2w_slot_groups[k] == 'jpt!' ? sizeof(struct damage_effect_definition) : 0;

		slots[k] = size ? h2w_loaded_slot(made, k, name) : NULL;
		slot_before[k] = slots[k] ? malloc(size) : NULL;
		if (slot_before[k])
			memcpy(slot_before[k], slots[k], size);
	}
	h2w_weapon_root_values_write(loaded, (unsigned long)definition - loaded->link, w);
	if (w->rounds_loaded > 0 && definition->weapon.magazines.count > 0)
	{
		struct weapon_magazine_definition *magazine = (struct weapon_magazine_definition *)definition->weapon.magazines.address;

		h2w_magazine_values_write(loaded, (unsigned long)magazine - loaded->link, w);
		if (magazine->ammunition_objects.count > 0)
			h2w_box_values_write(loaded, (unsigned long)magazine->ammunition_objects.address - loaded->link, w);
	}
	if (definition->weapon.triggers.count > 0)
		h2w_trigger_values_write(loaded, (unsigned long)definition - loaded->link,
			(unsigned long)definition->weapon.triggers.address - loaded->link, w);
	/* its melee's and lunge's damage, its melee's pyramid */
	{
		int k;

		for (k = 0; k < 2; k++)
		{
			unsigned char *damage = h2w_loaded_slot(made, k ? H2W_TAG_LUNGE : H2W_TAG_MELEE, name);

			if (damage)
				h2w_melee_values_write(cache, loaded, (unsigned long)damage - loaded->link, w, k);
			h2w_loaded_damage(loaded, cache, made, k ? H2W_SLOT_LUNGE_RESPONSE : H2W_SLOT_MELEE_RESPONSE, name, w->response_tags[k], NONE);
		}
		h2w_melee_root_values_write(loaded, (unsigned long)definition - loaded->link, w);
	}
	/* its projectiles and their damage, where its triggers fire the made ones */
	if (definition->weapon.triggers.count > 0)
	{
		struct weapon_trigger_definition const *triggers = (struct weapon_trigger_definition const *)definition->weapon.triggers.address;
		int k;

		for (k = 0; k < 2 && k < definition->weapon.triggers.count; k++)
		{
			long slot = k ? H2W_SLOT_SECOND_PROJECTILE : H2W_TAG_PROJECTILE;
			unsigned char *projectile = h2w_loaded_slot(made, slot, name);
			struct h2w_projectile const *p = &w->projectiles[k];
			struct h2_projectile_port const *port;
			struct projectile_definition const *pd;

			if (!projectile || p->tag == NONE || (triggers[k].projectile.index & 0xFFFF) != ((made & 0xFFFF) + slot))
				continue;
			pd = (struct projectile_definition const *)projectile;
			port = (struct h2_projectile_port const *)(projectile + offsetof(struct projectile_definition, object.unused2) +
				4 * H2_OBJECT_PORT_WORD);
			h2w_projectile_values_write(loaded, (unsigned long)projectile - loaded->link, p, k == 0,
				k == 0 ? port->attached_super_detonation_damage : NONE);
			h2w_loaded_damage(loaded, cache, made, k ? H2W_SLOT_SECOND_DAMAGE : H2W_TAG_PROJECTILE + 1, name, p->impact_damage,
				pd->projectile.impact_damage.index);
			if (k)
				continue;
			h2w_loaded_damage(loaded, cache, made, H2W_SLOT_DETONATION_DAMAGE, name, p->detonation_damage, NONE);
			h2w_loaded_damage(loaded, cache, made, H2W_SLOT_ATTACHED_DAMAGE, name, p->attached_detonation_damage,
				pd->projectile.attached_detonation_damage.index);
			if (p->flags & (1UL << 3))
			{
				h2w_loaded_damage(loaded, cache, made, H2W_SLOT_SUPER_DAMAGE, name, p->super_detonation_damage, NONE);
				h2w_loaded_damage(loaded, cache, made, H2W_SLOT_ATTACHED_SUPER_DAMAGE, name, p->attached_super_detonation_damage,
					port->attached_super_detonation_damage);
			}
		}
	}
	for (k = 0; k < part_count; k++)
	{
		unsigned long changed = 0, b;

		if (!parts[k].before)
			continue;
		for (b = 0; b < parts[k].size; b++)
			changed += parts[k].before[b] != parts[k].at[b];
		platform_log("h2 weapon values: check: %s: %lu bytes of its %s changed", name, changed,
			k == 0 ? "root" : parts[k].size == 2 ? "box" : parts[k].size == sizeof(struct weapon_trigger_definition) ? "trigger" : "magazine");
		free(parts[k].before);
	}
	for (k = 0; check && k < H2W_TAGS_PER_WEAPON; k++)
	{
		unsigned long size = h2w_slot_groups[k] == 'proj' ? sizeof(struct projectile_definition) : sizeof(struct damage_effect_definition);
		unsigned long changed = 0, b;

		if (!slot_before[k])
			continue;
		for (b = 0; b < size; b++)
			changed += slot_before[k][b] != slots[k][b];
		platform_log("h2 weapon values: check: %s%s: %lu bytes changed", name, h2w_slot_names[k], changed);
		free(slot_before[k]);
	}
}

/* (port, at load, no level rebuild) a weapon's first-person animations
given Halo 2's loop frame: the plasma pistol's first_person:overcharged is
34 frames looping from its 16th (the held charge), but the graph builder
wrote every first-person animation's loop frame as 0, so this engine
started the clip again from frame 0 every 34 frames - Ben's charge that
"repeats instead of looping" (insider.4, Nov 2026). The jmad record's loop
frame (+0x20) is read from the MCC map for each loaded animation of that
name (the Master Chief's graph from the weap's first-person block, the
Elite's from its second entry); only a loop frame of 0 is given one, and
only when the clip's frame count agrees. Levels of any builder. */
static long h2w_loop_frames_patch_graph(struct h2_cache *cache, long jmad, long graph_index)
{
	struct h2_cache_tag jt;
	struct animation_graph *graph;
	long animation_count, k, patched = 0;
	unsigned long animations;
	short i;

	if (jmad == NONE || graph_index == NONE || graph_index == 0 || !h2_cache_tag_get(cache, jmad, &jt) ||
		!h2_cache_block(cache, jt.address + 0x2C, 0x60, &animation_count, &animations) || !tag_get('antr', graph_index))
	{
		return 0;
	}
	graph = animation_graph_definition_get(graph_index);
	for (i = 0; i < graph->animations.count; i++)
	{
		struct animation *a = TAG_BLOCK_GET_ELEMENT(&graph->animations, i, struct animation);

		if (a->private_loop_frame_index != 0 || strncmp(a->name, "first_person", 12) != 0)
			continue;
		for (k = 0; k < animation_count; k++)
		{
			unsigned char const *e = h2_cache_at(cache, animations + 0x60 * k, 0x60);
			char name[64];
			short loop, frames;

			if (!e || !h2_cache_string(cache, read32(e), name, sizeof(name)) || strcmp(name, a->name) != 0)
				continue;
			loop = (short)read16(e + 0x20);
			frames = (short)read16(e + 0x14);
			if (loop > 0 && loop < a->frame_count && frames == a->frame_count)
			{
				a->private_loop_frame_index = loop;
				patched++;
			}
			break;
		}
	}
	return patched;
}

static long h2w_loop_frames_patch(struct h2_cache *cache, unsigned long weap_address, long made)
{
	struct weapon_definition *definition = weapon_definition_get(made);
	long count = 0;
	unsigned long block;

	if (!h2_cache_block(cache, weap_address + 0x2A8, 0x10, &count, &block) || count <= 0)
		return 0;
	return h2w_loop_frames_patch_graph(cache, h2_cache_reference(cache, block + 8), definition->weapon.interface_definition.first_person_animations.index) +
		(count > 1 ? h2w_loop_frames_patch_graph(cache, h2_cache_reference(cache, block + 0x10 + 8),
			definition->weapon.port_elite_first_person_animations) : 0);
}

void h2_weapon_fields_map_loaded(char const *map_name)
{
	char const *directory = map_name ? h2_maps_directory_of(map_name) : NULL;
	char const *off = getenv("HALO_H2_WEAPON_FIELDS_READER");
	char const *values_off = getenv("HALO_H2_WEAPON_VALUES_READER");
	/* (HALO_H2_WEAPON_VALUES_READER=0: the level's own values, as made) */
	boolean values = !(values_off && !strcmp(values_off, "0"));
	struct h2_cache *cache;
	struct h2w_weapon *w;
	struct region loaded;
	char why[160];
	long kind, count = 0, checked = 0, differ = 0, values_written = 0, loop_frames = 0;

	h2w_loaded_count = NONE;
	if (off && !strcmp(off, "0"))
		return;
	h2w_loaded_count = 0;
	if (!directory || !h2_level_is_halo2_map(directory, map_name))
		return;
	if (!(cache = h2_cache_open(directory, map_name, why, sizeof(why))))
	{
		platform_log("h2 weapon fields: %s.map cannot be read: %s (its weapons without Halo 2's dual wielding and recoil)",
			map_name, why);
		return;
	}
	/* (the loaded tags as a region: each tag's root at its address less the
	tag cache's - pointers there are the tags' own, relocated) */
	memset(&loaded, 0, sizeof(loaded));
	loaded.b.data = (unsigned char *)physical_memory_get_tag_cache_base_address();
	loaded.b.size = loaded.b.capacity = (unsigned long)TAG_CACHE_SIZE;
	loaded.link = (unsigned long)loaded.b.data;
	w = malloc(sizeof(*w));
	for (kind = 0; w && kind < (long)(sizeof(h2w_kinds) / sizeof(h2w_kinds[0])) && count < H2W_MAXIMUM_WEAPONS; kind++)
	{
		/* (the loading screen drawn between weapons: h2_level.h) */
		h2_load_draw_coarse();
		char name[160];
		struct tag_iterator iterator;
		struct h2_cache_tag t;
		long index, made = NONE, tag;

		snprintf(name, sizeof(name), "h2\\weapons\\%s", tag_short_name(h2w_kinds[kind].h2));
		tag_iterator_new(&iterator, WEAPON_DEFINITION_TAG);
		while ((index = tag_iterator_next(&iterator)) != NONE)
		{
			char const *loaded = tag_get_name(index);

			if (loaded && !strcmp(loaded, name))
			{
				made = index;
				break;
			}
		}
		if (made == NONE)
			continue;
		tag = h2_cache_tag_find(cache, 'weap', h2w_kinds[kind].h2);
		if (tag == NONE || !h2_cache_tag_get(cache, tag, &t))
			continue;
		/* (as h2w_weapon_make begins it, then its values) */
		memset(w, 0, sizeof(*w));
		w->kind = kind;
		w->h2_tag = tag;
		w->base_weapon = NONE;
		w->ammo_tag = w->ammo_slot = NONE;
		w->damage_tag = w->hit_tags[0] = w->hit_tags[1] = NONE;
		h2w_projectile_read(cache, NONE, &w->projectiles[0]);
		h2w_projectile_read(cache, NONE, &w->projectiles[1]);
		snprintf(w->name, sizeof(w->name), "%s", tag_short_name(h2w_kinds[kind].h2));
		h2w_weapon_values_read(cache, t.address, kind, w);
		loop_frames += h2w_loop_frames_patch(cache, t.address, made);
		/* its values written again on the loaded tags (the level's file
		may be of an older builder: a value fixed here needs no new level) */
		if (loop_frames)
		platform_log("h2 weapons: %ld first-person animations given Halo 2's loop frame at load (no rebuild)", loop_frames);
	if (values)
			h2w_loaded_values_write(&loaded, cache, made, weapon_definition_get(made), w, name);
		values_written += values;
		/* (its fields: a weapon with triggers, as h2_weapons_write adds them) */
		if (weapon_definition_get(made)->weapon.triggers.count <= 0)
			continue;
		h2w_fields_fill(&h2w_loaded_fields[count], w, made);
		if (getenv("HALO_H2_WEAPON_FIELDS_CHECK"))
		{
			long i;

			for (i = 0; i < h2w_fields_count; i++)
			{
				if (h2w_fields[i].definition_index == made)
				{
					checked++;
					if (memcmp(&h2w_fields[i], &h2w_loaded_fields[count], sizeof(h2w_fields[i])))
					{
						differ++;
						platform_log("h2 weapon fields: check: %s differs from the made table", name);
					}
				}
			}
		}
		count++;
	}
	/* the kit's grenades' values Halo 2's again (h2w_grenades_write's) */
	if (values)
	{
		extern struct cache_file_tag_instance *global_tag_instances;
		unsigned long instances = (unsigned long)global_tag_instances - loaded.link;

		/* (Halo 3's grenades written at the make, h3_weapons.c: Halo 2's
		values are not written over them) */
		for (kind = 0; kind < H2W_GRENADE_COUNT && !h3_weapons_grenades_h3(); kind++)
		{
			struct h2w_grenade_data g;
			struct tag_iterator iterator;
			char name[160];
			long index, projectile = NONE, attached = NONE, effect, damage_tag = NONE;
			unsigned long root, damage_root, attached_root = 0;

			h2w_grenade_read(cache, h2w_grenades[kind].h2_model, &g);
			tag_iterator_new(&iterator, 'proj');
			while ((index = tag_iterator_next(&iterator)) != NONE && projectile == NONE)
			{
				if (tag_get_name(index) && !strcmp(tag_get_name(index), h2w_grenades[kind].ce))
					projectile = index;
			}
			if (!g.valid || projectile == NONE)
				continue;
			root = (unsigned long)tag_get('proj', projectile) - loaded.link;
			effect = (short)read16(region_at(&loaded, root + offsetof(struct projectile_definition, projectile.effect) + 12));
			if (read32(region_at(&loaded, root + offsetof(struct projectile_definition, projectile.effect) + 12)) == 0xFFFFFFFFUL)
				effect = NONE;
			damage_root = h2w_effect_damage(&loaded, instances, effect, &damage_tag);
			/* (its stuck one's: the builder's "<grenade> attached", where its
			projectile names it) */
			snprintf(name, sizeof(name), "h2\\weapons\\%s attached", h2w_grenades[kind].ce);
			tag_iterator_new(&iterator, 'jpt!');
			while ((index = tag_iterator_next(&iterator)) != NONE && attached == NONE)
			{
				if (tag_get_name(index) && !strcmp(tag_get_name(index), name))
					attached = index;
			}
			if (attached != NONE && (((struct projectile_definition *)tag_get('proj', projectile))->projectile.attached_detonation_damage.index &
				0xFFFF) == (attached & 0xFFFF))
			{
				attached_root = (unsigned long)tag_get('jpt!', attached) - loaded.link;
			}
			/* (HALO_H2_WEAPON_VALUES_PERTURB=1, a test's: spoiled first, put back below) */
			if (getenv("HALO_H2_WEAPON_VALUES_PERTURB"))
			{
				writef(region_at(&loaded, root + offsetof(struct projectile_definition, projectile.timer_upper_bound)), 0.05f);
				if (damage_root)
					writef(region_at(&loaded, damage_root + offsetof(struct damage_effect_definition, damage.damage_upper_bound)), 999.0f);
				if (attached_root)
					writef(region_at(&loaded, attached_root + offsetof(struct damage_effect_definition, damage.damage_upper_bound)), 999.0f);
			}
			h2w_grenade_values_write(cache, &loaded, root, &g, damage_root, attached_root, NONE, NONE);
			values_written++;
			if (getenv("HALO_H2_WEAPON_VALUES_CHECK"))
				platform_log("h2 weapon values: check: grenade %s: projectile, explosion %s, stuck %s", h2w_grenades[kind].ce,
					damage_root ? "written" : "none", attached_root ? "written" : "none");
		}
	}
	/* the vehicles' own values (h2_vehicles.c) */
	if (values)
		values_written += h2v_values_map_loaded(cache, &loaded);
	if (w)
		free(w);
	h2_cache_close(cache);
	h2w_loaded_count = count;
	{
		/* (the table's hash: two sessions of one level - one that made it, one
		that loaded it - compared by their logs) */
		unsigned long hash = 2166136261UL;
		unsigned char const *p = (unsigned char const *)h2w_loaded_fields;
		size_t i;

		for (i = 0; i < (size_t)count * sizeof(h2w_loaded_fields[0]); i++)
			hash = (hash ^ p[i]) * 16777619UL;
		platform_log("h2 weapon fields: %s: %ld weapons' fields read from its map (table %08lx), %ld weapons' values written",
			map_name, count, hash, values_written);
	}
	if (getenv("HALO_H2_WEAPON_FIELDS_CHECK"))
		platform_log("h2 weapon fields: check: %ld of the made table's %ld compared, %ld differ", checked, h2w_fields_count, differ);
}

/* the ammunition boxes' tags (from first + h2w_ammo_first()): the kit's
rocket launcher ammunition each, its model Halo 2's, lying at Halo 2's
scale (0: as it is) */
H2_ALIGN static void h2w_ammo_write(struct build *build, struct region *r, unsigned long instances, long first, long model_first)
{
	unsigned long kit_root = h2w.kit_ammo != NONE ? h2w_root(r, instances, h2w.kit_ammo) : 0;
	long i;

	for (i = 0; i < h2w.ammo_count && !r->b.failed; i++)
	{
		struct h2w_weapon const *w = h2w.weapons[h2w.ammo_weapons[i]];
		unsigned long root = kit_root ? h2w_tag_copy(r, kit_root, sizeof(struct equipment_definition), 'eqip') :
			region_alloc(r, sizeof(struct equipment_definition));

		if (r->b.failed)
			return;
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (first + h2w_ammo_first() + i) + 20), r->link + root);
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (first + h2w_ammo_first() + i) + 4), 'item');
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (first + h2w_ammo_first() + i) + 8), 'obje');
		if (!kit_root)
			continue;
		h2w_model_write(r, h2w_root(r, instances, model_first + w->ammo_model.model), &w->ammo_model);
		region_reference(r, root + offsetof(struct equipment_definition, object.model), 'mode', tag_id(model_first + w->ammo_model.model));
		writef(region_at(r, root + offsetof(struct equipment_definition, object.bounding_radius)),
			build->models[w->ammo_model.model].radius + 0.05f);
		writef(region_at(r, root + offsetof(struct equipment_definition, item.scale)),
			w->ammo_scale >= 0.5f && w->ammo_scale <= 3.0f ? w->ammo_scale : 1.0f);
	}
}

/* a made weapon's magazine's ammunition box its own made one (Halo 2's
rounds), where it is made */
H2_ALIGN static void h2w_ammo_attach(struct region *r, unsigned long magazines, struct h2w_weapon const *w, long first)
{
	unsigned long field = magazines + offsetof(struct weapon_magazine_definition, ammunition_objects), box;

	if (w->ammo_slot == NONE || !magazines)
		return;
	box = region_block(r, field, 1, sizeof(struct weapon_ammunition_object));
	if (r->b.failed)
		return;
	write16(region_at(r, box + offsetof(struct weapon_ammunition_object, rounds)), w->rounds_box > 0 ? w->rounds_box : w->rounds_loaded);
	region_reference(r, box + offsetof(struct weapon_ammunition_object, object), 'eqip', tag_id(first + h2w_ammo_first() + w->ammo_slot));
}

H2_ALIGN static boolean h2w_export_attach(struct region *r, unsigned long root, long weapon, char const *what, short mode,
	unsigned long group, unsigned long id, char const *marker);

/* a made weapon's (h2w.weapons' index weapon, its weap root) muzzle
light attached (its tag id): the stand-in's own - a light attachment
scaled by an object function of the weapon's illumination export, the
assault rifle's and needler's at 'primary trigger' - made Halo 2's light at
Halo 2's marker; where the stand-in has none, an attachment of its own on
the illumination (h2w_export_attach). FALSE if neither can be */
H2_ALIGN static boolean h2w_light_attach(struct region *r, unsigned long instances, unsigned long root, long weapon, unsigned long id)
{
	struct h2w_weapon const *w = h2w.weapons[weapon];
	unsigned long modes = root + offsetof(struct weapon_definition, weapon.function_modes);
	unsigned long functions = read32(region_at(r, root + offsetof(struct weapon_definition, object.functions) + 4));
	unsigned long attachments = read32(region_at(r, root + offsetof(struct weapon_definition, object.attachments) + 4));
	long function_count = (long)read32(region_at(r, root + offsetof(struct weapon_definition, object.functions)));
	long attachment_count = (long)read32(region_at(r, root + offsetof(struct weapon_definition, object.attachments)));
	long k, made = 0;

	for (k = 0; k < attachment_count && attachments && functions; k++)
	{
		unsigned long a = attachments - r->link + sizeof(struct object_attachment_definition) * (unsigned long)k;
		short scale = (short)read16(region_at(r, a + offsetof(struct object_attachment_definition, primary_scale_function_reference)));
		short input;
		long old;

		if (read32(region_at(r, a)) != 'ligh' || scale < 1 || scale > function_count)
			continue;
		input = (short)read16(region_at(r, functions - r->link + sizeof(struct object_function_definition) * (unsigned long)(scale - 1) +
			offsetof(struct object_function_definition, scale_function_by_function_index)));
		if (input < 1 || input > 4 || (short)read16(region_at(r, modes + 2 * (input - 1))) != _weapon_function_illumination)
			continue;
		old = (short)read16(region_at(r, a + 12));
		if (old >= 0 && h2w_root(r, instances, old))
		{
			unsigned long o = h2w_root(r, instances, old);

			platform_log("h2 weapons: %s: the stand-in's muzzle light at '%.31s' (radius %.2f colour %.2f %.2f %.2f) made Halo 2's at '%s'",
				w->name, (char const *)region_at(r, a + offsetof(struct object_attachment_definition, marker_name)),
				readf(region_at(r, o + 4)), readf(region_at(r, o + 0x4C)), readf(region_at(r, o + 0x50)), readf(region_at(r, o + 0x54)),
				w->light_marker);
		}
		region_reference(r, a, 'ligh', id);
		snprintf((char *)region_at(r, a + offsetof(struct object_attachment_definition, marker_name)), TAG_STRING_LENGTH + 1, "%s",
			w->light_marker);
		made++;
	}
	return made > 0 || h2w_export_attach(r, root, weapon, "muzzle light", _weapon_function_illumination, 'ligh', id, w->light_marker);
}

/* a made weapon's (h2w.weapons' index weapon, its weap root) stand-in's
muzzle lights taken away where Halo 2's weapon has none: Halo 2's carbine,
Brute Shot and beam rifle have no light attachment at all (weap @94 holds
none, the beam rifle's only its overheat loop), where the kit's pistol,
rocket launcher and plasma rifle they are made on light their surroundings
2 units wide at each shot. The attachment's reference made none (objects.c
skips an attachment of no type); the count of lights taken */
H2_ALIGN static long h2w_light_strip(struct region *r, unsigned long root, long weapon)
{
	struct h2w_weapon const *w = h2w.weapons[weapon];
	unsigned long modes = root + offsetof(struct weapon_definition, weapon.function_modes);
	unsigned long functions = read32(region_at(r, root + offsetof(struct weapon_definition, object.functions) + 4));
	unsigned long attachments = read32(region_at(r, root + offsetof(struct weapon_definition, object.attachments) + 4));
	long function_count = (long)read32(region_at(r, root + offsetof(struct weapon_definition, object.functions)));
	long attachment_count = (long)read32(region_at(r, root + offsetof(struct weapon_definition, object.attachments)));
	long k, taken = 0;

	if (getenv("HALO_H2_STAND_IN_FIRING_LIGHTS"))
		return 0;
	for (k = 0; k < attachment_count && attachments && functions; k++)
	{
		unsigned long a = attachments - r->link + sizeof(struct object_attachment_definition) * (unsigned long)k;
		short scale = (short)read16(region_at(r, a + offsetof(struct object_attachment_definition, primary_scale_function_reference)));
		short input;

		if (read32(region_at(r, a)) != 'ligh' || scale < 1 || scale > function_count)
			continue;
		input = (short)read16(region_at(r, functions - r->link + sizeof(struct object_function_definition) * (unsigned long)(scale - 1) +
			offsetof(struct object_function_definition, scale_function_by_function_index)));
		if (input < 1 || input > 4 || (short)read16(region_at(r, modes + 2 * (input - 1))) != _weapon_function_illumination)
			continue;
		platform_log("h2 weapons: %s: the stand-in's muzzle light at '%.31s' taken away (Halo 2's has no muzzle light)", w->name,
			(char const *)region_at(r, a + offsetof(struct object_attachment_definition, marker_name)));
		region_reference(r, a, 0, 0xFFFFFFFFUL);
		taken++;
	}
	return taken;
}

/* the muzzle lights' tags (h2w.light_weapons', from first +
h2w_light_first()): a point light (0x160, object_lights.c's
point_light_definition) as h2_effects.c writes Halo 2's lights - lighting
the world and objects, every way - of Halo 2's radius and of its colour
from none to Halo 2's at its brightness by its attachment's scale (the
weapon's illumination); brighter than white as h2_effects.c's: the colour
at most white, the radius wider by the root of the excess, at most
twice */
H2_ALIGN static void h2w_lights_write(struct region *r, unsigned long instances, long first)
{
	long i, k;

	for (i = 0; i < h2w.light_count && !r->b.failed; i++)
	{
		struct h2w_weapon const *w = h2w.weapons[h2w.light_weapons[i]];
		unsigned long root = region_alloc(r, 0x160);
		unsigned char *o;
		float most = 0.0f, radius = w->light_radius;

		if (r->b.failed)
			return;
		for (k = 0; k < 3; k++)
			most = w->light_color[k] > most ? w->light_color[k] : most;
		if (most > 1.0f)
			radius *= most > 4.0f ? 2.0f : sqrtf(most);
		region_reference(r, root + 0x64, 'bitm', 0xFFFFFFFFUL);
		region_reference(r, root + 0x7C, 'bitm', 0xFFFFFFFFUL);
		region_reference(r, root + 0xAC, 'lens', 0xFFFFFFFFUL);
		o = region_at(r, root);
		write32(o + 0x00, 1);               /* (dynamic) */
		writef(o + 0x04, radius);
		writef(o + 0x08, 1.0f);             /* (radius modifier bounds: its radius at any scale) */
		writef(o + 0x0C, 1.0f);
		writef(o + 0x10, 3.14159265f);      /* (falloff and cutoff angles: a sphere) */
		writef(o + 0x14, 3.14159265f);
		writef(o + 0x1C, -1.0f);
		writef(o + 0x20, -1.0f);
		writef(o + 0x24, 1.0f);             /* (specular radius multiplier) */
		/* (the colour's bounds: none at scale 0, Halo 2's at 1 - their alphas
		none: an alpha blends an attached light's colour towards its change
		colour, white, bitmap_utilities.c rgb_colors_interpolate_and_scale) */
		for (k = 0; k < 3; k++)
		{
			float c = most > 1.0f ? w->light_color[k] / most : w->light_color[k];

			writef(o + 0x4C + 4 * k, c < 0.0f ? 0.0f : c > 1.0f ? 1.0f : c);
		}
		/* (the gel's animation periods: none, but never 0 - the rasterizer's
		check) */
		writef(o + 0x78, 1.0f);
		writef(o + 0x90, 1.0f);
		writef(o + 0x98, 1.0f);
		writef(o + 0xA0, 1.0f);
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)(first + h2w_light_first() + i) + 20), r->link + root);
		platform_log("h2 weapons: %s: its muzzle light made: radius %.2f colour %.2f %.2f %.2f, on its illumination (%.3f s)", w->name,
			radius, readf(o + 0x4C), readf(o + 0x50), readf(o + 0x54), w->light_recovery);
	}
}

/* the looping sounds' tags (h2w.loops', from first + h2w_loop_first()):
a track each, Halo 2's in, loop and out (their sound tags h2w.sounds',
from sound_first), gain and fades; heard as far as the weapons' sounds'
template is */
H2_ALIGN static void h2w_loops_write(struct region *r, unsigned long instances, long first)
{
	unsigned long template_root = h2w.kit_sound != NONE ? h2w_root(r, instances, h2w.kit_sound) : 0;
	float distance = template_root ? readf(region_at(r, template_root + offsetof(struct sound_definition, maximum_distance))) : 0.0f;
	long i, k;

	for (i = 0; i < h2w.loop_count && !r->b.failed; i++)
	{
		struct h2w_loop const *l = &h2w.loops[i];
		unsigned long root = region_alloc(r, sizeof(struct looping_sound_definition)), track;

		if (r->b.failed)
			return;
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (first + h2w_loop_first() + i) + 20), r->link + root);
		write32(region_at(r, root + offsetof(struct looping_sound_definition, runtime_scripting_sound_index)), 0xFFFFFFFFUL);
		writef(region_at(r, root + offsetof(struct looping_sound_definition, runtime_maximum_distance)), distance > 0.0f ? distance : 40.0f);
		region_reference(r, root + offsetof(struct looping_sound_definition, continuous_damage_effect), 0, 0xFFFFFFFFUL);
		track = region_block(r, root + offsetof(struct looping_sound_definition, tracks), 1, sizeof(struct looping_sound_track));
		if (r->b.failed)
			return;
		writef(region_at(r, track + offsetof(struct looping_sound_track, gain)), l->gain);
		writef(region_at(r, track + offsetof(struct looping_sound_track, fade_in_duration)), l->fade[0]);
		writef(region_at(r, track + offsetof(struct looping_sound_track, fade_out_duration)), l->fade[1]);
		for (k = 0; k < 3; k++)
		{
			unsigned long field = track + (k == 0 ? offsetof(struct looping_sound_track, start_sound) :
				k == 1 ? offsetof(struct looping_sound_track, loop_sound) : offsetof(struct looping_sound_track, stop_sound));

			region_reference(r, field, l->sounds[k] != NONE ? 'snd!' : 0,
				l->sounds[k] != NONE ? tag_id(first + h2w_sound_first() + l->sounds[k]) : 0xFFFFFFFFUL);
		}
		region_reference(r, track + offsetof(struct looping_sound_track, alternate_loop_sound), 0, 0xFFFFFFFFUL);
		region_reference(r, track + offsetof(struct looping_sound_track, alternate_stop_sound), 0, 0xFFFFFFFFUL);
	}
}

/* an attachment (group, its tag id; at marker, NULL the origin) on a
made weapon's (h2w.weapons' index weapon, its weap root) function of its
own - the weapon's export mode (function_modes: an A..D in), an object
function (A..D out) of that in alone; FALSE where its four of either are
taken (logged: what is left out) */
H2_ALIGN static boolean h2w_export_attach(struct region *r, unsigned long root, long weapon, char const *what, short mode,
	unsigned long group, unsigned long id, char const *marker)
{
	unsigned long modes = root + offsetof(struct weapon_definition, weapon.function_modes);
	unsigned long functions_field = root + offsetof(struct weapon_definition, object.functions);
	unsigned long attachments_field = root + offsetof(struct weapon_definition, object.attachments);
	long function_count = (long)read32(region_at(r, functions_field));
	long attachment_count = (long)read32(region_at(r, attachments_field));
	unsigned long old, block, function;
	short input = NONE, k;

	for (k = 0; k < 4 && input == NONE; k++)
	{
		if ((short)read16(region_at(r, modes + 2 * k)) == mode)
			input = k;
	}
	for (k = 0; k < 4 && input == NONE; k++)
	{
		if ((short)read16(region_at(r, modes + 2 * k)) == _weapon_function_none)
		{
			write16(region_at(r, modes + 2 * k), mode);
			input = k;
		}
	}
	if (getenv("HALO_H2_WEAPON_LOG"))
	{
		unsigned long fs = read32(region_at(r, functions_field + 4));

		platform_log("h2 weapon fields: %s exports %d %d %d %d", h2w.weapons[weapon]->name, (short)read16(region_at(r, modes)),
			(short)read16(region_at(r, modes + 2)), (short)read16(region_at(r, modes + 4)), (short)read16(region_at(r, modes + 6)));
		for (k = 0; k < function_count && fs; k++)
		{
			struct object_function_definition *f = (struct object_function_definition *)region_at(r, fs - r->link +
				sizeof(struct object_function_definition) * (unsigned long)k);

			platform_log("h2 weapon fields: %s function %d: flags %lx type %d scale by %d period by %d add %d result by %d map %d "
				"bounds %d %.2f..%.2f wobble %.2f square %.2f off with %d usage '%.31s'", h2w.weapons[weapon]->name, k,
				(unsigned long)read32(&f->flags), (short)read16(&f->function_type), (short)read16(&f->scale_function_by_function_index),
				(short)read16(&f->scale_period_by_function_index), (short)read16(&f->add_function_index),
				(short)read16(&f->scale_result_by_function_index), (short)read16(&f->map_result_to_transition_function),
				(short)read16(&f->bounds_mode), readf(&f->lower_bound), readf(&f->upper_bound), readf(&f->wobble_magnitude),
				readf(&f->square_wave_threshold), (short)read16(&f->turn_off_with_function_index), f->usage);
		}
		fs = read32(region_at(r, attachments_field + 4));
		for (k = 0; k < attachment_count && fs; k++)
		{
			unsigned char *a = region_at(r, fs - r->link + sizeof(struct object_attachment_definition) * (unsigned long)k);

			platform_log("h2 weapon fields: %s attachment %d: %.4s '%.31s' scales %d %d color %d", h2w.weapons[weapon]->name, k,
				(char const *)a, (char const *)a + 0x10, (short)read16(a + 0x30), (short)read16(a + 0x32), (short)read16(a + 0x34));
		}
	}
	if (input == NONE || function_count >= 4 || function_count < 0)
	{
		platform_log("h2 weapons: %s: its %s left out (no function free: %ld object functions)", h2w.weapons[weapon]->name, what,
			function_count);
		return FALSE;
	}
	/* (the functions: the stand-in's and one more) */
	old = read32(region_at(r, functions_field + 4));
	block = region_block(r, functions_field, function_count + 1, sizeof(struct object_function_definition));
	if (r->b.failed)
		return FALSE;
	if (function_count && old)
		memcpy(region_at(r, block), region_at(r, old - r->link), sizeof(struct object_function_definition) * (unsigned long)function_count);
	function = block + sizeof(struct object_function_definition) * (unsigned long)function_count;
	{
		struct object_function_definition *f = (struct object_function_definition *)region_at(r, function);

		write16(&f->function_type, 0);                       /* (one) */
		write16(&f->scale_function_by_function_index, (short)(input + 1));
		write16(&f->map_result_to_transition_function, 0);   /* (linear) */
		writef(&f->lower_bound, 0.0f);
		writef(&f->upper_bound, 1.0f);
		writef(&f->runtime_reciprocal_bounds_range, 1.0f);
		write16(&f->turn_off_with_function_index, NONE);
		snprintf(f->usage, sizeof(f->usage), "%s", what);
	}
	/* (the attachments: the stand-in's and this one, at its marker (none:
	the object's origin), scaled by the function) */
	old = read32(region_at(r, attachments_field + 4));
	block = region_block(r, attachments_field, attachment_count + 1, sizeof(struct object_attachment_definition));
	if (r->b.failed)
		return FALSE;
	if (attachment_count && old)
		memcpy(region_at(r, block), region_at(r, old - r->link),
			sizeof(struct object_attachment_definition) * (unsigned long)attachment_count);
	{
		unsigned long o = block + sizeof(struct object_attachment_definition) * (unsigned long)attachment_count;

		memset(region_at(r, o), 0, sizeof(struct object_attachment_definition));
		region_reference(r, o, group, id);
		snprintf((char *)region_at(r, o + offsetof(struct object_attachment_definition, marker_name)), TAG_STRING_LENGTH + 1, "%s",
			marker ? marker : "");
		write16(region_at(r, o + offsetof(struct object_attachment_definition, primary_scale_function_reference)),
			(short)(function_count + 1));
	}
	platform_log("h2 weapons: %s: its %s on its export %c in, object function %c out", h2w.weapons[weapon]->name, what,
		'A' + input, (int)('A' + function_count));
	return TRUE;
}

/* a made weapon's (h2w.weapons' index weapon, its weap root) looping
sounds attached: each on a function of its own - the weapon's export
(function_modes: an A..D in) of its firing or overheat, an object function
(A..D out) of that in alone, active while it is above none - the loop
starting as it fires and its out playing as it stops; a weapon whose four
of either are taken keeps none (logged) */
H2_ALIGN static void h2w_loops_attach(struct region *r, unsigned long root, long weapon, long first)
{
	long i;

	/* (Halo 2's attachments all looping sounds - the Sentinel Beam's: the
	stand-in's lights - the plasma rifle's heat flares, which Halo 2's
	beam has none of - and the functions and exports they and its model's
	shaders read given up, every one free for the loops) */
	for (i = 0; i < h2w.loop_count; i++)
	{
		if (h2w.loops[i].weapon == weapon && h2w.weapons[weapon]->sounds_only)
		{
			int k;

			write32(region_at(r, root + offsetof(struct weapon_definition, object.attachments)), 0);
			write32(region_at(r, root + offsetof(struct weapon_definition, object.attachments) + 4), 0);
			write32(region_at(r, root + offsetof(struct weapon_definition, object.functions)), 0);
			write32(region_at(r, root + offsetof(struct weapon_definition, object.functions) + 4), 0);
			for (k = 0; k < 4; k++)
				write16(region_at(r, root + offsetof(struct weapon_definition, weapon.function_modes) + 2 * k), _weapon_function_none);
			break;
		}
	}

	for (i = 0; i < h2w.loop_count && !r->b.failed; i++)
	{
		struct h2w_loop const *l = &h2w.loops[i];

		if (l->weapon == weapon)
			h2w_export_attach(r, root, weapon, l->name, l->mode, 'lsnd', tag_id(first + h2w_loop_first() + i), NULL);
	}
}

/* a made weapon's magazine's values Halo 2's (its rounds; as made and as
loaded, h2_weapons_map_loaded) */
H2_ALIGN static void h2w_magazine_values_write(struct region *r, unsigned long magazines, struct h2w_weapon const *w)
{
	short held = w->rounds_held > 0 ? w->rounds_held : (short)(w->rounds_maximum - w->rounds_loaded);
	short reloaded = w->rounds_reloaded > 0 ? w->rounds_reloaded : w->rounds_loaded;

	write16(region_at(r, magazines + offsetof(struct weapon_magazine_definition, rounds_total_initial)), w->rounds_initial);
	write16(region_at(r, magazines + offsetof(struct weapon_magazine_definition, rounds_total_maximum)), held);
	write16(region_at(r, magazines + offsetof(struct weapon_magazine_definition, rounds_loaded_maximum)), w->rounds_loaded);
	write16(region_at(r, magazines + offsetof(struct weapon_magazine_definition, rounds_reloaded)), reloaded);
}

/* its ammunition box's rounds Halo 2's */
H2_ALIGN static void h2w_box_values_write(struct region *r, unsigned long boxes, struct h2w_weapon const *w)
{
	short held = w->rounds_held > 0 ? w->rounds_held : (short)(w->rounds_maximum - w->rounds_loaded);
	short box = w->rounds_box > 0 ? w->rounds_box : held;

	write16(region_at(r, boxes), box);
}

/* its first trigger's values Halo 2's - its flags, rate, burst, error,
heat and age a round, tracers, barrel offset - and its triggers' tracking
(as made and as loaded, h2_weapons_map_loaded; the same writes, none
reading what another wrote but the flags, whose result is the same again) */
H2_ALIGN static void h2w_trigger_values_write(struct region *r, unsigned long root, unsigned long triggers, struct h2w_weapon *w)
{
	long k;
	{
		unsigned long flags = read32(region_at(r, triggers));
		static int const barrel_bits[8] = { 0, 1, 2, 5, 7, 10, 11, 12 };

		flags &= FLAG(_weapon_trigger_latched_bit) | FLAG(_weapon_trigger_toggles_bit) | FLAG(_weapon_trigger_sticks_when_dropped_bit) |
			FLAG(_weapon_trigger_discharging_spews_bit) | FLAG(_weapon_trigger_analog_rate_of_fire_bit);
		/* (not Halo 2's "tracks fired projectile", bit 0: its projectile
		follows the aim's target - this engine's guided projectiles do -
		where this engine's tracking trigger holds itself, unable to fire
		again, while its trigger is down: the needler fired one needle a
		pull) */
		for (k = 1; k < 8; k++)
		{
			if (w->barrel_flags & (1UL << k))
				flags |= 1UL << barrel_bits[k];
		}
		/* (Halo 2's latch: one shot - one burst - a pull; its
		latch-zoom, 4, the battle rifle's and carbine's, latches too:
		MCC's battle rifle fires one 3-round burst a pull, held or
		tapped - the owner's Outskirts footage) */
		if (w->trigger_behavior == 1 || w->trigger_behavior == 4 || w->trigger_behavior == 5)
			flags |= FLAG(_weapon_trigger_latched_bit);
		else
			flags &= ~FLAG(_weapon_trigger_latched_bit);
		write32(region_at(r, triggers), flags);
		/* (Halo 2's single shots: no rate, the shot's recovery its time) */
		if (w->rounds_per_second <= 0.0f && w->burst_shots <= 1 && w->burst_recovery > 0.0f)
			w->rounds_per_second = w->rounds_per_second_maximum = 1.0f / w->burst_recovery;
		if (w->rounds_per_second > 0.0f)
		{
			writef(region_at(r, triggers + offsetof(struct weapon_trigger_definition, initial_rate_of_fire)), w->rounds_per_second);
			writef(region_at(r, triggers + offsetof(struct weapon_trigger_definition, final_rate_of_fire)),
				w->rounds_per_second_maximum > w->rounds_per_second ? w->rounds_per_second_maximum : w->rounds_per_second);
			/* (the rate's rise and fall Halo 2's, not the stand-in's: the
			plasma rifle from 6 to 9 rounds a second in 0.8 s held, back in
			0.6 s - the kit's 1.8 s and 0.25 s; a tick's share as the tags'
			postprocess makes it) */
			writef(region_at(r, triggers + offsetof(struct weapon_trigger_definition, rate_of_fire_acceleration_time)), w->rate_times[0]);
			writef(region_at(r, triggers + offsetof(struct weapon_trigger_definition, rate_of_fire_deceleration_time)), w->rate_times[1]);
			writef(region_at(r, triggers + offsetof(struct weapon_trigger_definition, runtime_rate_of_fire_acceleration_time)),
				h2w_per_tick(w->rate_times[0]));
			writef(region_at(r, triggers + offsetof(struct weapon_trigger_definition, runtime_rate_of_fire_deceleration_time)),
				h2w_per_tick(w->rate_times[1]));
		}
		/* (a round a shot from a magazine; a battery weapon - Halo 2's plasma
		rifle, no rounds in its magazine - none: the kit's plasma rifle's
		magazine is never loaded, one a shot it never fired, its trigger
		locked at the first pull) */
		write16(region_at(r, triggers + offsetof(struct weapon_trigger_definition, rounds_per_shot)), w->rounds_loaded > 0 ? 1 : 0);
		write16(region_at(r, triggers + offsetof(struct weapon_trigger_definition, projectiles_per_shot)),
			w->projectiles_per_shot > 0 ? w->projectiles_per_shot : 1);
		/* (its tracers Halo 2's: the rounds between two tracer rounds, the
		same count in both engines - items/weapons.c fires a tracer when as
		many have gone since the last; the stand-in's own stayed) */
		if (getenv("HALO_H2_WEAPON_LOG"))
			platform_log("h2 weapon fields: %s rounds between tracers %d -> %d", w->name,
				(short)read16(region_at(r, triggers + offsetof(struct weapon_trigger_definition, rounds_between_tracers))),
				w->rounds_between_tracers);
		write16(region_at(r, triggers + offsetof(struct weapon_trigger_definition, rounds_between_tracers)),
			w->rounds_between_tracers > 0 ? w->rounds_between_tracers : 0);
		writef(region_at(r, triggers + offsetof(struct weapon_trigger_definition, projectile_distribution_angle)), w->distribution_angle);
		writef(region_at(r, triggers + offsetof(struct weapon_trigger_definition, projectile_error_angle_lower_bound)), w->error_angle[0]);
		writef(region_at(r, triggers + offsetof(struct weapon_trigger_definition, projectile_error_angle_upper_bound)), w->error_angle[1]);
		/* the barrel's error: it rises (error 0 to 1, the angles above
		at 0 and 1) in Halo 2's acceleration time and falls in its
		deceleration time, shots never nearer the aim than its minimum
		error; the tag's times, and the engine's (a fraction a tick) */
		{
			struct weapon_trigger_definition *t = (struct weapon_trigger_definition *)region_at(r, triggers);

			if (getenv("HALO_H2_WEAPON_LOG"))
				platform_log("h2 weapon fields: %s error: rise %.3f s fall %.3f s (a tick %.4f %.4f) minimum %.3f deg angles %.3f..%.3f deg "
					"-> rise %.3f s fall %.3f s minimum %.3f deg angles %.3f..%.3f deg; dual wielded: rise %.3f s fall %.3f s minimum %.3f deg "
					"angles %.3f..%.3f deg damage x%.2f; recoil %.3f..%.3f deg (rise %.2f s fall %.2f s curve %d)", w->name,
					readf(&t->error_acceleration_time), readf(&t->error_deceleration_time),
					readf(&t->runtime_error_acceleration_time), readf(&t->runtime_error_deceleration_time),
					readf(&t->projectile_error_inner_cone_angle) * 57.29578f,
					readf(&t->projectile_error_angle_lower_bound) * 57.29578f, readf(&t->projectile_error_angle_upper_bound) * 57.29578f,
					w->error_times[0], w->error_times[1], w->error_minimum * 57.29578f,
					w->error_angle[0] * 57.29578f, w->error_angle[1] * 57.29578f,
					w->dual_error_times[0], w->dual_error_times[1], w->dual_error_minimum * 57.29578f,
					w->dual_error_angle[0] * 57.29578f, w->dual_error_angle[1] * 57.29578f, w->dual_damage_scale,
					w->recoil_angle[0] * 57.29578f, w->recoil_angle[1] * 57.29578f, w->recoil_times[0], w->recoil_times[1], w->recoil_function);
			writef(&t->error_acceleration_time, w->error_times[0]);
			writef(&t->error_deceleration_time, w->error_times[1]);
			writef(&t->runtime_error_acceleration_time, h2w_per_tick(w->error_times[0]));
			writef(&t->runtime_error_deceleration_time, h2w_per_tick(w->error_times[1]));
			writef(&t->projectile_error_inner_cone_angle, w->error_minimum);
			writef(&t->initial_error, 0.0f);
			writef(&t->final_error, 1.0f);
		}
		/* (a burst past this engine's 15 with no recovery - the Hunter's
		cannon, 30 rounds a fire at 30 a second -: fired as long as held,
		not in 15-round pieces, which ran a held 1.75 s burst on to 2 s) */
		if (w->burst_shots > MAXIMUM_WEAPON_TRIGGER_BURST_SHOTS && w->burst_recovery <= 0.0f)
		{
			if (getenv("HALO_H2_WEAPON_LOG"))
				platform_log("h2 weapon fields: %s burst of %d with no recovery: fired as held", w->name, w->burst_shots);
		}
		else if (w->burst_shots > 1)
		{
			write32(region_at(r, triggers + offsetof(struct weapon_trigger_definition, port_burst_tag)), WEAPON_TRIGGER_BURST_TAG);
			write16(region_at(r, triggers + offsetof(struct weapon_trigger_definition, burst_shots)), w->burst_shots);
			writef(region_at(r, triggers + offsetof(struct weapon_trigger_definition, burst_recovery_time)), w->burst_recovery);
		}
		if (w->heat[1] > 0.0f)
			writef(region_at(r, triggers + offsetof(struct weapon_trigger_definition, heat_generated_per_round)), w->heat_per_round);
		/* (a battery's age a round, Halo 2's: the Sentinel Beam's 0.003,
		333 rounds - eleven seconds of its beam) */
		if (w->rounds_loaded <= 0 && w->age_per_round > 0.0f)
			writef(region_at(r, triggers + offsetof(struct weapon_trigger_definition, age_generated_per_round)), w->age_per_round);
		/* (a player's projectile starting at Halo 2's barrel offset,
		where it has one - the Sentinel Beam's beam from its muzzle, not
		the eye: its contrail drawn from there; the rest keep the
		stand-in's) */
		if (w->barrel_offset[0] != 0.0f || w->barrel_offset[1] != 0.0f || w->barrel_offset[2] != 0.0f)
		{
			for (k = 0; k < 3; k++)
				writef(region_at(r, triggers + offsetof(struct weapon_trigger_definition, first_person_weapon_offset) + 4 * k),
					w->barrel_offset[k]);
		}
		if (getenv("HALO_H2_WEAPON_LOG"))
			platform_log("h2 weapon fields: %s heat: recovery %.3f overheated %.3f loss %.4f/s (overheated %.4f/s) %.4f a round; "
				"age %.4f a round (penalties %.2f %.2f %.2f %.2f), %.1f rounds a second, behavior %d", w->name, w->heat[0], w->heat[1],
				w->heat[4], w->heat[6], w->heat_per_round, w->age_per_round, w->age_penalties[0], w->age_penalties[1],
				w->age_penalties[2], w->age_penalties[3], w->rounds_per_second, (int)w->trigger_behavior);
	}
	/* (its triggers' tracking: the rocket's lock, the plasma
	pistol's bipeds - h2_projectiles.c) */
	if (w->tracking == _h2_tracking_human || w->tracking == _h2_tracking_plasma)
	{
		long trigger_count = (long)read32(region_at(r, root + offsetof(struct weapon_definition, weapon.triggers)));

		for (k = 0; k < trigger_count && k < 2; k++)
		{
			unsigned long at = triggers + sizeof(struct weapon_trigger_definition) * k;

			write32(region_at(r, at + offsetof(struct weapon_trigger_definition, port_tracking_tag)), H2_TRACKING_PORT_TAG);
			write16(region_at(r, at + offsetof(struct weapon_trigger_definition, port_tracking_type)), w->tracking);
		}
	}
}

/* a made weapon's own values Halo 2's (h2w_weapon_values_read): its item
scale, zoom, aim assist, movement penalties, heat and age - written as a
level is made and again as one is loaded (h2_weapons_map_loaded), the same
writes: none of them reads what another wrote */
H2_ALIGN static void h2w_weapon_root_values_write(struct region *r, unsigned long root, struct h2w_weapon const *w)
{
	/* (lying in a multiplayer game at its own size: the game engine
	draws an item there at its item scale, the kit's 1.8 - Halo 2 does
	not; HALO_H2_ITEM_KIT_SCALE keeps it, a test's) */
	if (!getenv("HALO_H2_ITEM_KIT_SCALE"))
	{
		/* (Halo 2's multiplayer on-ground scale, weap @C4; the game
		engine accepts 0.5 to 3) */
		float scale = w->ground_scale >= 0.5f && w->ground_scale <= 3.0f ? w->ground_scale : 1.0f;

		if (getenv("HALO_H2_WEAPON_LOG"))
			platform_log("h2 weapon fields: %s on-ground scale %.2f -> %.2f", w->name,
				readf(region_at(r, root + offsetof(struct weapon_definition, item.scale))), scale);
		writef(region_at(r, root + offsetof(struct weapon_definition, item.scale)), scale);
	}
	/* (its zoom Halo 2's - none where it has none: the magnum on the
	kit's pistol and the Brute Shot on its rocket launcher zoomed as their
	stand-ins do; Halo 2's have no zoom) */
	if (getenv("HALO_H2_WEAPON_LOG") && w->zoom_levels <= 0 &&
		(short)read16(region_at(r, root + offsetof(struct weapon_definition, weapon.zoom_level_count))) > 0)
	{
		platform_log("h2 weapon fields: %s zoom levels %d -> 0 (Halo 2's has none)", w->name,
			(short)read16(region_at(r, root + offsetof(struct weapon_definition, weapon.zoom_level_count))));
	}
	write16(region_at(r, root + offsetof(struct weapon_definition, weapon.zoom_level_count)), w->zoom_levels > 0 ? w->zoom_levels : 0);
	if (w->zoom_levels > 0)
	{
		writef(region_at(r, root + offsetof(struct weapon_definition, weapon.zoom_magnification_minimum)), w->zoom[0]);
		writef(region_at(r, root + offsetof(struct weapon_definition, weapon.zoom_magnification_maximum)), w->zoom[1]);
	}
	writef(region_at(r, root + offsetof(struct weapon_definition, weapon.aim_assist_parameters.autoaim_angle)), w->autoaim[0]);
	writef(region_at(r, root + offsetof(struct weapon_definition, weapon.aim_assist_parameters.autoaim_distance)), w->autoaim[1]);
	writef(region_at(r, root + offsetof(struct weapon_definition, weapon.aim_assist_parameters.magnetism_angle)), w->magnetism[0]);
	writef(region_at(r, root + offsetof(struct weapon_definition, weapon.aim_assist_parameters.magnetism_distance)), w->magnetism[1]);
	/* (none read - a Halo 3 weapon's, h3_weapons.c -: the stand-in's kept) */
	if (w->deviation >= 0.0f)
		writef(region_at(r, root + offsetof(struct weapon_definition, weapon.aim_assist_parameters.deviation_angle)), w->deviation);
	/* (its movement penalties Halo 2's - none for any multiplayer weapon,
	where the kit's rocket launcher's 0.25 stayed on the rocket launcher
	and the Brute Shot) */
	if (getenv("HALO_H2_WEAPON_LOG"))
		platform_log("h2 weapon fields: %s movement penalty %.2f %.2f -> %.2f %.2f (mode %d)", w->name,
			readf(region_at(r, root + offsetof(struct weapon_definition, weapon.forward_movement_penalty))),
			readf(region_at(r, root + offsetof(struct weapon_definition, weapon.sideways_movement_penalty))),
			w->movement_penalty[0], w->movement_penalty[1], w->movement_penalized);
	writef(region_at(r, root + offsetof(struct weapon_definition, weapon.forward_movement_penalty)), w->movement_penalty[0]);
	writef(region_at(r, root + offsetof(struct weapon_definition, weapon.sideways_movement_penalty)), w->movement_penalty[1]);
	/* heat (Halo 2's plasma weapons) */
	if (w->heat[1] > 0.0f)
	{
		writef(region_at(r, root + offsetof(struct weapon_definition, weapon.heat_recovery_threshold)), w->heat[0]);
		writef(region_at(r, root + offsetof(struct weapon_definition, weapon.heat_overheated_threshold)), w->heat[1]);
		writef(region_at(r, root + offsetof(struct weapon_definition, weapon.heat_detonation_threshold)), w->heat[2]);
		writef(region_at(r, root + offsetof(struct weapon_definition, weapon.overheated_explosion_fraction)), w->heat[3]);
		writef(region_at(r, root + offsetof(struct weapon_definition, weapon.heat_loss_per_second)), w->heat[4]);
		writef(region_at(r, root + offsetof(struct weapon_definition, weapon.heat_illumination)), w->heat[5]);
	}
	/* its battery (Halo 2's battery weapons: no magazine rounds, an age
	a shot - the plasma rifle, the Sentinel Beam): the age's penalties
	Halo 2's, and its "cannot fire at maximum age" (bit 11 in both
	games) - a dead Sentinel Beam does not fire */
	if (w->rounds_loaded <= 0 && w->age_per_round > 0.0f)
	{
		unsigned long flags = read32(region_at(r, root + offsetof(struct weapon_definition, weapon.flags)));

		writef(region_at(r, root + offsetof(struct weapon_definition, weapon.age_heat_recovery_penalty)), w->age_penalties[0]);
		writef(region_at(r, root + offsetof(struct weapon_definition, weapon.age_rate_of_fire_penalty)), w->age_penalties[1]);
		writef(region_at(r, root + offsetof(struct weapon_definition, weapon.age_misfire_start)), w->age_penalties[2]);
		writef(region_at(r, root + offsetof(struct weapon_definition, weapon.age_misfire_chance)), w->age_penalties[3]);
		flags &= ~FLAG(_weapon_cannot_fire_at_maximum_age_bit);
		if (w->h2_flags & FLAG(_weapon_cannot_fire_at_maximum_age_bit))
			flags |= FLAG(_weapon_cannot_fire_at_maximum_age_bit);
		write32(region_at(r, root + offsetof(struct weapon_definition, weapon.flags)), flags);
	}
}

/* whether made weapon i takes every reference to the kit's tag index (its
stand-in): a Halo 2 level's rule, the first replaces_base weapon made on
that stand-in (the battle rifle on the assault rifle, the magnum on the
pistol); a Halo 3 level's (h3_sandbox.c), the Halo 3 weapon of the kit
weapon's own kind where it is made - the assault rifle's, the pistol's magnum,
the plasma pistol's... (the game engine's weapon list and the Spartan's own
pistol then start the players with Halo 3's) -, else Halo 2's rule */
H2_ALIGN static boolean h2w_replaces_base(long i, long index)
{
	static struct { char const *base; char const *collection; } const twins[] =
	{
		{ "weapons\\assault rifle\\assault rifle", "assault_rifle" },
		{ "weapons\\pistol\\pistol", "magnum" },
		{ "weapons\\plasma pistol\\plasma pistol", "plasma_pistol" },
		{ "weapons\\plasma rifle\\plasma rifle", "plasma_rifle" },
		{ "weapons\\needler\\needler", "needler" },
		{ "weapons\\shotgun\\shotgun", "shotgun" },
		{ "weapons\\sniper rifle\\sniper rifle", "sniper_rifle" },
		{ "weapons\\rocket launcher\\rocket launcher", "rocket_launcher" },
	};
	struct h2w_weapon const *w = h2w.weapons[i];

	if (index < 0 || index != w->base_weapon)
		return FALSE;
	if (h3_weapons_level_on())
	{
		unsigned long t;

		for (t = 0; t < sizeof(twins) / sizeof(twins[0]); t++)
		{
			if (!strcmp(h2w_kinds[w->kind].base, twins[t].base))
			{
				long twin = h2_weapons_for(twins[t].collection);

				if (twin != NONE)
					return twin == i * H2W_TAGS_PER_WEAPON;
				break;
			}
		}
	}
	return h2w_kinds[w->kind].replaces_base;
}

H2_ALIGN static void h2_weapons_write(struct build *build, struct region *r, unsigned long instances, long first, long model_first,
	unsigned long scenario_root)
{
	long i;

	h2w_fields_count = 0;
	if (getenv("HALO_H2_DEBUG"))
	{
		long g;

		for (g = 0; g < first; g++)
		{
			unsigned long group = read32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * g));
			unsigned long root = h2w_root(r, instances, g);

			if (group == 'vehi' && root)
			{
				h2w_debug_model(r, instances, (short)read16(region_at(r, root + offsetof(struct object_definition, object.model) + 12)), "kit vehicle model");
				h2w_debug_graph(r, instances, (short)read16(region_at(r, root + offsetof(struct object_definition, object.animation_graph) + 12)), "kit vehicle graph");
			}
		}
	}
	/* the grenades: Halo 2's models on the kit's */
	for (i = 0; i < H2W_GRENADE_COUNT; i++)
	{
		long objects[2], k;

		if (h2w.grenades[i].model == NONE)
			continue;
		h2w_model_write(r, h2w_root(r, instances, model_first + h2w.grenades[i].model), &h2w.grenades[i]);
		objects[0] = h2w.grenade_equipment[i];
		objects[1] = h2w.grenade_projectile[i];
		for (k = 0; k < 2; k++)
		{
			unsigned long root = objects[k] >= 0 ? h2w_root(r, instances, objects[k]) : 0;

			if (root)
				region_reference(r, root + offsetof(struct object_definition, object.model), 'mode', tag_id(model_first + h2w.grenades[i].model));
			/* (lying in a multiplayer game at its own size: Halo 2 does not
			draw items larger, the kit's equipment's item scale would) */
			if (root && k == 0 && !getenv("HALO_H2_ITEM_KIT_SCALE"))
			{
				float scale = h2w.grenade_scale[i] >= 0.5f && h2w.grenade_scale[i] <= 3.0f ? h2w.grenade_scale[i] : 1.0f;

				if (getenv("HALO_H2_WEAPON_LOG"))
					platform_log("h2 weapon fields: %s on-ground scale %.2f -> %.2f", h2w_grenades[i].ce,
						readf(region_at(r, root + offsetof(struct item_definition, item.scale))), scale);
				writef(region_at(r, root + offsetof(struct item_definition, item.scale)), scale);
			}
		}
	}
	h2w_grenades_write(build, r, instances, first);
	/* the powerups: Halo 2's models, scale and pickup sound on the kit's */
	for (i = 0; i < H2W_POWERUP_COUNT; i++)
	{
		unsigned long root = h2w.powerup_equipment[i] >= 0 ? h2w_root(r, instances, h2w.powerup_equipment[i]) : 0;

		if (h2w.powerups[i].model == NONE || !root)
			continue;
		h2w_model_write(r, h2w_root(r, instances, model_first + h2w.powerups[i].model), &h2w.powerups[i]);
		region_reference(r, root + offsetof(struct equipment_definition, object.model), 'mode', tag_id(model_first + h2w.powerups[i].model));
		writef(region_at(r, root + offsetof(struct equipment_definition, object.bounding_radius)),
			build->models[h2w.powerups[i].model].radius + 0.05f);
		if (!getenv("HALO_H2_ITEM_KIT_SCALE"))
			writef(region_at(r, root + offsetof(struct equipment_definition, item.scale)),
				h2w.powerup_scale[i] >= 0.5f && h2w.powerup_scale[i] <= 3.0f ? h2w.powerup_scale[i] : 1.0f);
		if (h2w.powerup_sound[i] != NONE)
			region_reference(r, root + offsetof(struct equipment_definition, equipment.pickup_sound), 'snd!',
				tag_id(first + h2w_sound_first() + h2w.powerup_sound[i]));
	}
	/* the globals' multiplayer weapons (the list the game engines start
	players with and weapon sets choose from, the assault rifle first): each
	made weapon in its stand-in's place */
	{
		long g, k;
		unsigned long globals = 0;

		for (g = 0; g < first && !globals; g++)
		{
			if (read32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * g)) == 'matg')
				globals = h2w_root(r, instances, g);
		}
		if (globals)
		{
			long count = (long)read32(region_at(r, globals + offsetof(struct game_globals, weapon_list)));
			unsigned long list = read32(region_at(r, globals + offsetof(struct game_globals, weapon_list) + 4));

			for (k = 0; k < count && list; k++)
			{
				unsigned long entry = list - r->link + 16 * k;
				long index = (short)read16(region_at(r, entry + 12));

				if (getenv("HALO_H2_DEBUG"))
					fprintf(stderr, "h2 weapons: globals weapon %ld: %08lx tag %ld\n", k, read32(region_at(r, entry)), index);

				for (i = 0; i < h2w.count; i++)
				{
					if (h2w_replaces_base(i, index))
						region_reference(r, entry, 'weap', tag_id(first + H2W_TAGS_PER_WEAPON * i));
				}
			}
		}
	}
	/* every other weapon reference to a stand-in a made weapon replaces
	(the multiplayer cyborg's own starting weapons, the profiles'...): the
	made weapon */
	{
		unsigned long at;
		long smg_made = h2_weapons_for("smg");

		unsigned long table_end = instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)(first + h2_weapons_tag_count());

		for (at = 0; at + 16 <= r->b.size; at += 4)
		{
			unsigned char *p = region_at(r, at);
			long index;

			/* (not the tag table's own entries) */
			if (at >= instances && at < table_end)
				continue;
			if (read32(p) != 'weap' || read32(p + 12) == 0xFFFFFFFFUL)
				continue;
			index = (long)(read32(p + 12) & 0xFFFF);
			for (i = 0; i < h2w.count; i++)
			{
				if (h2w_replaces_base(i, index) &&
					read32(p + 12) == read32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * index + 12)))
				{
					region_reference(r, at, 'weap', tag_id(first + H2W_TAGS_PER_WEAPON * i));
					if (getenv("HALO_H2_DEBUG"))
						fprintf(stderr, "h2 weapons: a reference to %s at %08lx made %s's\n", h2w_kinds[h2w.weapons[i]->kind].base,
							r->link + at, h2w.weapons[i]->name);
					break;
				}
			}
			/* (the kit's pistol no made weapon replaces - Halo 2's magnum is
			made only where the level hands it out -: Halo 2's SMG, the
			multiplayer start's second weapon - the cyborg's own pistol, which
			the game engine starts a player with, and the weapon sets' pistol) */
			if (i == h2w.count && index == h2w.kit_pistol && smg_made != NONE && !h3_weapons_level_on() &&
				read32(p + 12) == read32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * index + 12)))
			{
				region_reference(r, at, 'weap', tag_id(first + smg_made));
				if (getenv("HALO_H2_DEBUG"))
					fprintf(stderr, "h2 weapons: a reference to the pistol at %08lx made the SMG's\n", r->link + at);
			}
		}
	}
	/* the players' starting weapons: on a campaign level Halo 2's own
	starting profile (scenario 0xF8, 0x44 each: primary weapon 0x28, its
	rounds 0x30/0x32, secondary 0x34, rounds 0x3C/0x3E, frag and plasma
	grenades 0x40/0x41) - 01a starts unarmed, 01b with nothing, 03a with
	what Halo 2 gives -; a weapon not made here, or "unarmed", none */
	if (build->cache && strstr(h2_cache_scenario_path(build->cache), "\\solo\\") && read32(region_at(r, scenario_root + 0x348)))
	{
		struct h2_cache *cache = build->cache;
		struct h2_cache_tag scenario;
		long count, k;
		unsigned long h2_profiles, profiles = read32(region_at(r, scenario_root + 0x348 + 4)) - r->link;

		if (h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) &&
			h2_cache_block(cache, scenario.address + 0xF8, 0x44, &count, &h2_profiles) && count > 0)
		{
			unsigned char const *h = h2_cache_at(cache, h2_profiles, 0x44);

			for (k = 0; h && k < 2; k++)
			{
				long tag = h2_cache_reference(cache, h2_profiles + (k ? 0x34 : 0x28));
				char const *name = tag != NONE ? h2_cache_tag_name(cache, tag) : NULL;
				long made = NONE, i;
				unsigned long at = profiles + (k ? 0x3C : 0x28);

				for (i = 0; name && i < h2w.count; i++)
				{
					if (!strcmp(name, h2w_kinds[h2w.weapons[i]->kind].h2))
						made = i * H2W_TAGS_PER_WEAPON;
				}
				region_reference(r, at, 'weap', made != NONE ? tag_id(first + made) : 0xFFFFFFFFUL);
				write16(region_at(r, at + 0x10), made != NONE ? read16(h + (k ? 0x3C : 0x30)) : 0);
				/* (Halo 2's "rounds total" counts the loaded ones too - its
				weapons keep total minus loaded unloaded -, this engine's
				profile the unloaded ones: 03a's battle rifle 36 + 108) */
				{
					int unloaded = (short)read16(h + (k ? 0x3E : 0x32)) - (short)read16(h + (k ? 0x3C : 0x30));

					write16(region_at(r, at + 0x12), made != NONE && unloaded > 0 ? unloaded : 0);
				}
				platform_log("h2 weapons: campaign start %s: %s", k ? "secondary" : "primary",
					made != NONE ? name : name ? "(not made here: none)" : "none");
			}
			if (h)
			{
				region_at(r, profiles)[0x50] = h[0x40];
				region_at(r, profiles)[0x51] = h[0x41];
			}
		}
	}
	/* the players' starting weapons: Halo 2's battle rifle and SMG (its
	multiplayer games' default start) where they are made (a campaign
	level's profiles are its own, Halo 2's: h2_level.c) */
	/* (a Halo 3 level's: Halo 3's default start, the assault rifle and the
	magnum - h3_weapons.c) */
	if (build->multiplayer)
	{
		static char const *const start[2] = { "battle_rifle", "smg" };
		static char const *const h3_start[2] = { "assault_rifle", "magnum" };
		long k;
		unsigned long profiles = read32(region_at(r, scenario_root + 0x348 + 4));

		for (k = 0; k < 2; k++)
		{
			long made = h2_weapons_for(h3_weapons_level_on() ? h3_start[k] : start[k]);

			if (made != NONE && profiles && read32(region_at(r, scenario_root + 0x348)))
			{
				struct h2w_weapon const *w = h2w.weapons[made / H2W_TAGS_PER_WEAPON];
				unsigned long at = profiles - r->link + (k ? 0x3C : 0x28);

				region_reference(r, at, 'weap', tag_id(first + made));
				write16(region_at(r, at + 0x10), w->rounds_loaded);
				write16(region_at(r, at + 0x12), w->rounds_initial);
			}
		}
	}

	for (i = 0; i < h2w.count && !r->b.failed; i++)
	{
		struct h2w_weapon *w = h2w.weapons[i];
		long weapon_index = first + H2W_TAGS_PER_WEAPON * i;
		unsigned long base_root = w->base_weapon != NONE ? h2w_root(r, instances, w->base_weapon) : 0;
		unsigned long root, graph_root, projectile_root, damage_root, triggers, magazines;
		unsigned long base_projectile_root = 0, base_damage_root = 0;
		long base_projectile = NONE, base_damage = NONE;
		long k;
		boolean own_light = FALSE;

		/* the models' nodes and markers */
		if (w->world.model != NONE)
			h2w_model_write(r, h2w_root(r, instances, model_first + w->world.model), &w->world);
		if (w->first_person.model != NONE)
			h2w_model_write(r, h2w_root(r, instances, model_first + w->first_person.model), &w->first_person);
		if (w->first_person_elite.model != NONE)
			h2w_model_write(r, h2w_root(r, instances, model_first + w->first_person_elite.model), &w->first_person_elite);
		if (!base_root)
		{
			platform_log("h2 weapons: %s: the kit has no %s", w->name, h2w_kinds[w->kind].base);
			continue;
		}
		h2w_debug_model(r, instances, (short)read16(region_at(r, base_root + offsetof(struct weapon_definition, object.model) + 12)),
			"stand-in world model");
		h2w_debug_model(r, instances, (short)read16(region_at(r, base_root +
			offsetof(struct weapon_definition, weapon.interface_definition.first_person_model) + 12)), "stand-in first-person model");
		/* the weapon: the stand-in's, its fields Halo 2's */
		root = h2w_tag_copy(r, base_root, sizeof(struct weapon_definition), 'weap');
		if (r->b.failed)
			break;
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * weapon_index + 20), r->link + root);
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * weapon_index + 4), 'item');
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * weapon_index + 8), 'obje');
		/* its muzzle light: Halo 2's, on its illumination (h2w_light_read) */
		own_light = w->light_slot != NONE && h2w_light_attach(r, instances, root, i, tag_id(first + h2w_light_first() + w->light_slot));
		/* (none where Halo 2's weapon has none: h2w_light_strip) */
		/* (a Halo 3 weapon's kept: its own light is not read yet, h3_weapons.c) */
		if (w->light_slot == NONE && !h2w_kinds[w->kind].ai_only && !w->h3)
			h2w_light_strip(r, root, i);
		/* its firing effects: the stand-in's first trigger's firing effect
		copied twice, with Halo 2's firing sounds (a burst's second silent:
		Halo 2 plays a burst's sound once) */
		{
			unsigned long stand_in_triggers = read32(region_at(r, root + offsetof(struct weapon_definition, weapon.triggers) + 4));
			unsigned long stand_in_effects = 0;
			long stand_in_effect = NONE, k;

			if (read32(region_at(r, root + offsetof(struct weapon_definition, weapon.triggers))) && stand_in_triggers)
			{
				unsigned long field = stand_in_triggers - r->link + offsetof(struct weapon_trigger_definition, firing_effects);

				if (read32(region_at(r, field)) && (stand_in_effects = read32(region_at(r, field + 4))) != 0)
					stand_in_effect = (short)read16(region_at(r, stand_in_effects - r->link + H2W_FIRING_EFFECT_EFFECT + 12));
			}
			for (k = 0; k < H2W_FIRING_EFFECTS; k++)
			{
				/* (a burst's: the sounding effect SECOND - items/weapons.c
				starts a trigger's firing effects at the element after its
				index, 0 at first, so the second element serves the first
				shot; with the sound first, each battle rifle burst's sound
				came on its third round, 0.13 s late - owner playtest Oct 30) */
				short sound = w->burst_shots > 1 ? (k == 1 ? w->firing_sounds[0] : NONE) :
					k < w->firing_effect_count ? w->firing_sounds[k] : w->firing_sounds[0];

				h2w_effect_make(r, instances, stand_in_effect, weapon_index + 4 + k, sound, first + h2w_sound_first());
			}
		}
		region_reference(r, root + offsetof(struct weapon_definition, object.model), 'mode',
			w->world.model != NONE ? tag_id(model_first + w->world.model) : 0xFFFFFFFFUL);
		/* the Elite's first-person set (its graph the weapon's seventh tag;
		none made - a level whose player is the Master Chief -: an empty
		graph, unused. Not the Chief's graph again: 80-210 KB a weapon, 03a's
		ten 1.3 MB of the Vita's tag cache) */
		{
			boolean elite = w->first_person_elite.model != NONE && w->graph_elite;

			if (!h2w_graphs_at_load)
			{
				unsigned long elite_root = region_alloc(r, sizeof(struct animation_graph));

				write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (weapon_index + 6) + 20), r->link + elite_root);
				if (elite)
					h2w_graph_write(r, elite_root, w->graph_elite, first + h2w_sound_first());
			}
			write32(region_at(r, root + offsetof(struct weapon_definition, weapon.port_elite_first_person_model)),
				elite ? tag_id(model_first + w->first_person_elite.model) : 0xFFFFFFFFUL);
			write32(region_at(r, root + offsetof(struct weapon_definition, weapon.port_elite_first_person_animations)),
				elite ? tag_id(weapon_index + 6) : 0xFFFFFFFFUL);
		}
		writef(region_at(r, root + offsetof(struct weapon_definition, object.bounding_radius)),
			w->world.model != NONE ? build->models[w->world.model].radius + 0.05f : 0.5f);
		region_reference(r, root + offsetof(struct weapon_definition, weapon.interface_definition.first_person_model), 'mode',
			w->first_person.model != NONE ? tag_id(model_first + w->first_person.model) : 0xFFFFFFFFUL);
		/* (an AI's only: gone when its holder dies - no detonation of the
		stand-in's) */
		if (h2w_kinds[w->kind].ai_only)
		{
			write32(region_at(r, root + offsetof(struct weapon_definition, weapon.flags)),
				read32(region_at(r, root + offsetof(struct weapon_definition, weapon.flags))) | FLAG(_weapon_detonates_when_dropped_bit));
			region_reference(r, root + offsetof(struct weapon_definition, item.detonating_effect), 0, 0xFFFFFFFFUL);
			region_reference(r, root + offsetof(struct weapon_definition, item.detonation_effect), 0, 0xFFFFFFFFUL);
		}
		region_reference(r, root + offsetof(struct weapon_definition, weapon.interface_definition.first_person_animations), 'antr',
			tag_id(weapon_index + 1));
		{
			unsigned long flags = read32(region_at(r, root + offsetof(struct weapon_definition, weapon.flags)));

			flags |= FLAG(_weapon_first_person_model_has_hands_bit);
			/* (dual wielded - with its graph's dual-wielded animations) */
			if ((w->h2_flags & (1UL << 22)) && w->graph && w->graph->dual)
				flags |= FLAG(_weapon_can_be_dual_wielded_bit);
			/* (its aim assist only when zoomed: the sniper rifle and the
			beam rifle - bit 5 in both games) */
			if (w->h2_flags & (1UL << _weapon_aim_assists_only_when_zoomed_bit))
				flags |= FLAG(_weapon_aim_assists_only_when_zoomed_bit);
			write32(region_at(r, root + offsetof(struct weapon_definition, weapon.flags)), flags);
		}
		/* (Halo 2's first-person weapon offset: forward, left, up) */
		{
			int c;

			for (c = 0; c < 3; c++)
				writef(region_at(r, root + offsetof(struct weapon_definition, weapon.port_first_person_offset) + 4 * c), w->first_person_offset[c]);
		}
		h2w_melee_write(build, r, instances, root, weapon_index, w);
		/* (a Halo 3 melee weapon's strike explosion - the gravity hammer's) */
		if (w->h3)
			h3_weapons_melee_area_write(r, instances, root, weapon_index, w);
		/* its ready effect: Halo 2's where it has one (h2_effects.c fills the
		slot: the sword's blade_activate_fp ignites it with its sound as it is
		picked up or drawn - owner playtest Oct 30 item 3 -, the SMG's and
		needler's "ready"); none where Halo 2 has none (the kit stand-in's
		own left behind) */
		{
			struct h2_cache_tag ready_tag;

			if (build->cache && h2_cache_tag_get(build->cache, w->h2_tag, &ready_tag) && h2_cache_reference(build->cache, ready_tag.address + 0x140) != NONE)
				region_reference(r, root + offsetof(struct weapon_definition, weapon.ready_effect), 'effe', tag_id(weapon_index + H2W_SLOT_READY_EFFECT));
			else
				region_reference(r, root + offsetof(struct weapon_definition, weapon.ready_effect), 0, 0xFFFFFFFFUL);
			/* (its overheated effect the same way: Halo 2's weap @16C, no
			stand-in's) */
			if (build->cache && h2_cache_tag_get(build->cache, w->h2_tag, &ready_tag) && h2_cache_reference(build->cache, ready_tag.address + 0x16C) != NONE)
				region_reference(r, root + offsetof(struct weapon_definition, weapon.overheated_effect), 'effe', tag_id(weapon_index + H2W_SLOT_OVERHEATED_EFFECT));
			else
				region_reference(r, root + offsetof(struct weapon_definition, weapon.overheated_effect), 0, 0xFFFFFFFFUL);
		}
		h2w_weapon_root_values_write(r, root, w);
		/* magazine */
		magazines = h2w_block_copy(r, root + offsetof(struct weapon_definition, weapon.magazines), sizeof(struct weapon_magazine_definition));
		if (magazines && w->rounds_loaded > 0)
		{
			/* (this engine's total is the reserve: rounds_total_initial the
			rounds in all at the start, the magazine's among them, and
			rounds_total_maximum the reserve's most - Halo 2's rounds held,
			not its rounds in all; a reload moves Halo 2's rounds reloaded:
			the shotgun's one shell) */
			short held = w->rounds_held > 0 ? w->rounds_held : (short)(w->rounds_maximum - w->rounds_loaded);
			short reloaded = w->rounds_reloaded > 0 ? w->rounds_reloaded : w->rounds_loaded;
			unsigned long box_field = magazines + offsetof(struct weapon_magazine_definition, ammunition_objects);

			if (getenv("HALO_H2_WEAPON_LOG"))
			{
				platform_log("h2 weapon fields: %s magazine: initial %d -> %d, reserve most %d -> %d, loaded %d -> %d, reloaded %d -> %d "
					"(Halo 2: reload %.2f s, ready %.2f s, box %d rounds)", w->name,
					(short)read16(region_at(r, magazines + offsetof(struct weapon_magazine_definition, rounds_total_initial))), w->rounds_initial,
					(short)read16(region_at(r, magazines + offsetof(struct weapon_magazine_definition, rounds_total_maximum))), held,
					(short)read16(region_at(r, magazines + offsetof(struct weapon_magazine_definition, rounds_loaded_maximum))), w->rounds_loaded,
					(short)read16(region_at(r, magazines + offsetof(struct weapon_magazine_definition, rounds_reloaded))), reloaded,
					w->reload_time, w->ready_time, w->rounds_box);
			}
			h2w_magazine_values_write(r, magazines, w);
			/* (the ammunition box: Halo 2's rounds, its equipment the kit's; 0
			for the most: the magazine's reserve) */
			if (read32(region_at(r, box_field)) > 0)
			{
				unsigned long boxes = h2w_block_copy(r, box_field, sizeof(struct weapon_ammunition_object));
				short box = w->rounds_box > 0 ? w->rounds_box : held;

				if (boxes)
				{
					if (getenv("HALO_H2_WEAPON_LOG"))
						platform_log("h2 weapon fields: %s ammunition box %d -> %d rounds", w->name,
							(short)read16(region_at(r, boxes)), box);
					h2w_box_values_write(r, boxes, w);
				}
			}
		}
		/* (its ammunition box Halo 2's, where the level places one) */
		if (magazines && w->rounds_loaded > 0)
			h2w_ammo_attach(r, magazines, w, first);
		/* (its animation label Halo 2's type's; a Halo 3 weapon's its own) */
		if (h3_weapons_label(w))
		{
			memset(region_at(r, root + offsetof(struct weapon_definition, weapon.label)), 0, 32);
			memcpy(region_at(r, root + offsetof(struct weapon_definition, weapon.label)), h3_weapons_label(w),
				strlen(h3_weapons_label(w)) < 32 ? strlen(h3_weapons_label(w)) : 31);
		}
		/* a sword: no triggers, no magazine; its melee lunges (its melee's
		and lunge's damage h2w_melee_write's) */
		if (h2w_kinds[w->kind].sword)
		{
			unsigned long flags = read32(region_at(r, root + offsetof(struct weapon_definition, weapon.flags)));

			write32(region_at(r, root + offsetof(struct weapon_definition, weapon.flags)), flags | FLAG(_weapon_melee_lunges_bit));
			/* (its label Halo 2's class's - h2_animation.c's "sword" class,
			its type the class's name - not the stand-in's pistol's: its
			third-person melee, dash and lunge are the sword class's) */
			/* (both tiers since Oct 24 2026: the Vita's graphs have the
			sword class now) */
			{
				memset(region_at(r, root + offsetof(struct weapon_definition, weapon.label)), 0, 32);
				/* (a Halo 3 melee weapon's its class's - "sword", "hammer") */
				memcpy(region_at(r, root + offsetof(struct weapon_definition, weapon.label)), h3_weapons_label(w) ? h3_weapons_label(w) : "sword",
					h3_weapons_label(w) && strlen(h3_weapons_label(w)) < 32 ? strlen(h3_weapons_label(w)) : 5);
			}
			writef(region_at(r, root + offsetof(struct weapon_definition, weapon.port_lunge_range)), w->lunge_range);
			writef(region_at(r, root + offsetof(struct weapon_definition, weapon.port_lunge_angle)), w->lunge_angle);
			write32(region_at(r, root + offsetof(struct weapon_definition, weapon.triggers)), 0);
			write32(region_at(r, root + offsetof(struct weapon_definition, weapon.triggers) + 4), 0);
			write32(region_at(r, root + offsetof(struct weapon_definition, weapon.magazines)), 0);
			write32(region_at(r, root + offsetof(struct weapon_definition, weapon.magazines) + 4), 0);
			if (!h2w_graphs_at_load)
			{
				graph_root = region_alloc(r, sizeof(struct animation_graph));
				write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (weapon_index + 1) + 20), r->link + graph_root);
				h2w_graph_write(r, graph_root, w->graph, first + h2w_sound_first());
			}
			/* (the projectile slot: the stand-in's own projectile, unused) */
			projectile_root = h2w_tag_copy(r, h2w_root(r, instances, h2w_any_projectile(r, instances, first)), sizeof(struct projectile_definition), 'proj');
			write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (weapon_index + 2) + 20), r->link + projectile_root);
			write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (weapon_index + 2) + 4), 'obje');
			/* (the damage slot: nothing - its melee's and lunge's are
			h2w_melee_write's) */
			damage_root = region_alloc(r, sizeof(struct damage_effect_definition));
			write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (weapon_index + 3) + 20), r->link + damage_root);
			h2w_slots_fill(r, instances, weapon_index, projectile_root);
			platform_log("h2 weapons: %s made (a sword: lunges %.1f units, %.0f degrees), %ld animations", w->name, w->lunge_range,
				w->lunge_angle * 57.29578f, w->graph->animation_count);
			continue;
		}
		/* the trigger: rate, burst, error, the projectile */
		triggers = h2w_block_copy(r, root + offsetof(struct weapon_definition, weapon.triggers), sizeof(struct weapon_trigger_definition));
		/* (its firing effects: the two made - a burst's first shot's and the
		rest's, or Halo 2's two barrels' firing effects, or the one) */
		if (triggers)
		{
			unsigned long field = triggers + offsetof(struct weapon_trigger_definition, firing_effects);
			unsigned long elements = read32(region_at(r, field + 4));

			if (read32(region_at(r, field)) && elements)
			{
				unsigned char element[H2W_FIRING_EFFECT_SIZE];
				long want = w->burst_shots > 1 || w->firing_effect_count > 1 ? 2 : 1, k;
				unsigned long block;

				memcpy(element, region_at(r, elements - r->link), sizeof(element));
				block = region_block(r, field, want, H2W_FIRING_EFFECT_SIZE);
				for (k = 0; k < want && !r->b.failed; k++)
				{
					unsigned long at = block + H2W_FIRING_EFFECT_SIZE * k;

					memcpy(region_at(r, at), element, sizeof(element));
					region_reference(r, at + H2W_FIRING_EFFECT_EFFECT, 'effe', tag_id(weapon_index + 4 + k));
					if (want == 2)
					{
						/* (a burst: the first element its silent rounds, the
						second its sounding first round - see the effects made) */
						short shots = w->burst_shots > 1 && k == 0 ? (short)(w->burst_shots - 1) : 1;

						write16(region_at(r, at), shots);
						write16(region_at(r, at + 2), shots);
					}
				}
			}
		}
		if (triggers)
		{
			base_projectile = (short)read16(region_at(r, triggers + offsetof(struct weapon_trigger_definition, projectile) + 12));
			h2w_trigger_values_write(r, root, triggers, w);
			/* (a Halo 3 weapon's second trigger: its charge, heat and
			battery; a barrel of no shot) */
			if (w->h3)
				h3_weapons_triggers_write(r, root, triggers, w);
			/* (its illumination - its muzzle light's scale: full as a round
			fires, none again after Halo 2's barrel's recovery time, items/
			weapons.c - on each trigger; none recovers in a tick) */
			if (own_light)
			{
				long trigger_count = (long)read32(region_at(r, root + offsetof(struct weapon_definition, weapon.triggers)));
				float recovery = w->light_recovery > 0.0f ? w->light_recovery : 1.0f / 30.0f;

				for (k = 0; k < trigger_count && k < 2; k++)
				{
					unsigned long at = triggers + sizeof(struct weapon_trigger_definition) * k;

					writef(region_at(r, at + offsetof(struct weapon_trigger_definition, illumination_recovery_time)), recovery);
					writef(region_at(r, at + offsetof(struct weapon_trigger_definition, runtime_illumination_recovery_time)),
						h2w_per_tick(recovery));
				}
			}
			h2w_fields_add(w, tag_id(weapon_index));
			region_reference(r, triggers + offsetof(struct weapon_trigger_definition, projectile), 'proj', tag_id(weapon_index + 2));
			/* (its triggers' magazines ones it has: items/weapons.c never
			fires from a magazine a weapon does not have - the trigger locked at
			its first pull; NONE, a battery's) */
			{
				long trigger_count = (long)read32(region_at(r, root + offsetof(struct weapon_definition, weapon.triggers)));
				long magazine_count = (long)read32(region_at(r, root + offsetof(struct weapon_definition, weapon.magazines)));

				for (k = 0; k < trigger_count && k < 2; k++)
				{
					unsigned long at = triggers + sizeof(struct weapon_trigger_definition) * k +
						offsetof(struct weapon_trigger_definition, magazine_index);
					short magazine = (short)read16(region_at(r, at));

					if (magazine != NONE && magazine >= magazine_count)
						write16(region_at(r, at), 0xFFFF);
				}
			}
		}
		/* its looping sounds (the Sentinel Beam's hum and hiss) */
		h2w_loops_attach(r, root, i, first);
		/* the graph (at load, h2_models_load.inc: not here) */
		if (!h2w_graphs_at_load)
		{
			graph_root = region_alloc(r, sizeof(struct animation_graph));
			write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (weapon_index + 1) + 20), r->link + graph_root);
			if (w->graph)
				h2w_graph_write(r, graph_root, w->graph, first + h2w_sound_first());
		}
		/* the projectile and its damage: the stand-in's, Halo 2's (its
		speeds, ranges, guidance, damage effects - h2w_projectile_write) */
		base_projectile_root = base_projectile >= 0 ? h2w_root(r, instances, base_projectile) : 0;
		projectile_root = base_projectile_root ? h2w_tag_copy(r, base_projectile_root, sizeof(struct projectile_definition), 'proj') :
			region_alloc(r, sizeof(struct projectile_definition));
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (weapon_index + 2) + 20), r->link + projectile_root);
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (weapon_index + 2) + 4), 'obje');
		if (base_projectile_root)
		{
			base_damage = (short)read16(region_at(r, projectile_root + offsetof(struct projectile_definition, projectile.impact_damage) + 12));
			if (getenv("HALO_H2_DEBUG") && base_damage >= 0 && (base_damage_root = h2w_root(r, instances, base_damage)) != 0)
				fprintf(stderr, "h2 weapons: %s: stand-in damage min %.2f lower %.2f upper %.2f, velocity %.3f, range %.1f\n", w->name,
					readf(region_at(r, base_damage_root + offsetof(struct damage_effect_definition, damage.damage_minimum))),
					readf(region_at(r, base_damage_root + offsetof(struct damage_effect_definition, damage.damage_lower_bound))),
					readf(region_at(r, base_damage_root + offsetof(struct damage_effect_definition, damage.damage_upper_bound))),
					readf(region_at(r, base_projectile_root + offsetof(struct projectile_definition, projectile.initial_velocity))),
					readf(region_at(r, base_projectile_root + offsetof(struct projectile_definition, projectile.maximum_range))));
			h2w_projectile_write(build, r, instances, projectile_root, &w->projectiles[0], weapon_index + 3, weapon_index, w->name);
			/* (a Halo 3 weapon's impact damage: h3_weapons.c's, no Halo 2 tag) */
			if (w->h3)
				h3_weapons_damage_write(build, r, instances, projectile_root, base_projectile_root, weapon_index + 3, w, 0);
		}
		/* the second trigger's (the plasma pistol's charged bolt: its
		second barrel's projectile, made from the stand-in's second
		trigger's) */
		if (triggers && base_projectile_root && w->projectiles[1].tag != NONE && w->projectiles[1].tag != w->projectiles[0].tag &&
			(long)read32(region_at(r, root + offsetof(struct weapon_definition, weapon.triggers))) > 1)
		{
			unsigned long second = triggers + sizeof(struct weapon_trigger_definition);
			long stand_in = (short)read16(region_at(r, second + offsetof(struct weapon_trigger_definition, projectile) + 12));
			unsigned long stand_in_root = stand_in >= 0 ? h2w_root(r, instances, stand_in) : 0;

			if (stand_in_root)
			{
				unsigned long second_root = h2w_tag_copy(r, stand_in_root, sizeof(struct projectile_definition), 'proj');

				write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (weapon_index + H2W_SLOT_SECOND_PROJECTILE) + 20), r->link + second_root);
				write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (weapon_index + H2W_SLOT_SECOND_PROJECTILE) + 4), 'obje');
				h2w_projectile_write(build, r, instances, second_root, &w->projectiles[1], weapon_index + H2W_SLOT_SECOND_DAMAGE, NONE, w->name);
				/* (a Halo 3 weapon's second barrel's impact damage: the
				charged bolt's, the laser's beam's) */
				if (w->h3)
					h3_weapons_damage_write(build, r, instances, second_root, stand_in_root, weapon_index + H2W_SLOT_SECOND_DAMAGE, w, 1);
				region_reference(r, second + offsetof(struct weapon_trigger_definition, projectile), 'proj',
					tag_id(weapon_index + H2W_SLOT_SECOND_PROJECTILE));
			}
		}
		h2w_slots_fill(r, instances, weapon_index, projectile_root);
		if (h2w_kinds[w->kind].ai_only)
			platform_log("h2 weapons: %s made: an AI's only, of no model (its holder's markers fire it)", w->name);
		else
			platform_log("h2 weapons: %s made: world model %ld parts, first person %ld nodes %ld parts, %ld animations",
				w->name, build->models[w->world.model].part_count, w->first_person.node_count,
				build->models[w->first_person.model].part_count, w->graph->animation_count);
	}
	/* the vehicles' tags after the weapons' */
	h2_vehicles_write(build, r, instances, first + h2w.count * H2W_TAGS_PER_WEAPON, model_first);
	h2w_sounds_write(r, instances, first + h2w_sound_first());
	h2w_loops_write(r, instances, first);
	h2w_ammo_write(build, r, instances, first, model_first);
	h2w_lights_write(r, instances, first);
}

H2_ALIGN static void h2_weapons_dispose(void)
{
	long i;

	h2_vehicles_dispose();
	for (i = 0; i < h2w.count; i++)
	{
		h2w_graph_free(h2w.weapons[i]->graph);
		h2w_graph_free(h2w.weapons[i]->graph_elite);
		free(h2w.weapons[i]->h3);
		free(h2w.weapons[i]);
	}
	h2w.count = 0;
}

/* the vehicles (h2_vehicles.c: its code uses this file's) and the HUD
(h2_hud_make.c) */
#include "h2_vehicles.c"
#include "h2_hud_make.c"

#else

/* (built alone: nothing - h2_level.c compiles it) */
typedef int h2_weapons_compiled_in_h2_level;

#endif
