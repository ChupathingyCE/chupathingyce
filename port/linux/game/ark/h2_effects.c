/*
H2_EFFECTS.C

Halo 2's effects made into this engine's, as a Halo 2 level is made
(h2_level.c, which compiles this file as a part of itself, after
h2_weapons.c: it uses the level build's buffers, bitmaps and tag writing;
built alone it is empty).

What Halo 2's effects are (Assembly's Halo 2 plugins, checked against the
owner's MCC shared.map; a local reading tool):
- an effect (effe): its locations (marker names, string ids at 0xC), its
  events (0x14, 0x38 each: skip fraction, delay and duration bounds), each
  event's parts (0x18: sounds, lights, decals, damage...) and particle
  systems (0x30, 0x38 each: the particle (prt3) at 0, location at 8,
  coordinate system 0xC (world, local), camera mode 0x12, its emitters at
  0x30, 0xB8 each);
- an emitter: its particle physics (pmov) at 0, then nine properties, each
  a function of an input (16 bytes: input, range input, output modifier,
  the function's bytes as a block): emission rate (particles a tick),
  lifespan, velocity, angular velocity (degrees a second), size (world
  units), tint, alpha, then its shape (0x78: sprayer, disc, globe...),
  emission radius and angle, its offset (0x9C) and direction (0xA8);
- a particle (prt3): flags, its billboard style (0x4: screen facing,
  parallel or perpendicular to its direction), its first sequence and
  sequence count (0x8), its shader template (0xC: the blend - additive,
  alpha blend, alpha multiply add, distortion) and parameters (0x14, 0x28
  each: the bitmap at 8), then colour, alpha, scale, rotation and frame
  functions (0x1C..);
- a function (Halo 2's mapping function bytes): type, flags (1 ranged:
  two curves the range input chooses between; 0x10/0x20 a colour), two
  shape bytes, then min and max (or colours at 4 and 16), then by type:
  0 identity, 1 constant (a value a curve), 2 transition (two values a
  curve, eased by the shape byte), 3 periodic (frequency, phase, min,
  max), 4 linear (two points, slope, intercept), 5 linear key (four
  points...), 7 and 10 spline (four points, then a cubic's coefficients).
  These were read off the data (h2effects/tools/funcs.py) - every one the
  multiplayer effects use evaluates within its range.

What this engine's are: an effect (effe) with locations and events, each
event's parts (sounds, lights, damage, decals: kept from the effect it
stands in for) and particles (0xE8 each: a particle tag, counts, emission
radius, velocity, cone, angular velocity, radius and tint bounds); a
particle (part) with a sprite bitmap's sequences, lifespan, radius from and
to over its life, frames a second, orientation, and its shader's blend.

Made here, for each effect a purpose below names (h2fx_purposes): an
effect whose events are Halo 2's with an entry for each emitter, a particle
for each emitter (its lifespan, frames, blend, orientation; its size,
alpha and colour over its life sampled into the tag's free bytes:
h2_particle_styles.h, read by the renderer), a bitmap for each sprite sheet
(Halo 2's, its sequences and sprites). Where Halo 2's effect stands in this
engine's place (a weapon's firing effect, a projectile's detonation, its
impacts on each material, a vehicle's death), the effect there is made
again: Halo 2's particles, and the sounds, lights, damage and decals of the
effect that was there (their location added).

Halo 2's lights (ligh: a point light, its colour at its brightness, its
duration and falloff), lens flares (lens: a light's, an effect's lens part
as a light of the flare alone), light volumes (MGS2: each volume a light
volume of one frame - its offset, radius, brightness and colour at its
ends) and decals (deca: h2_level.c's decals, an effect's a sprite sheet's)
are made this engine's too: an effect's lights and decals its parts (the
stand-in's of the kind dropped where Halo 2's effect has its own), an
object's lights and light volumes its attachments (the stand-in's then
dropped). HALO_H2_NO_LIGHTS, HALO_H2_NO_LENS_FLARES,
HALO_H2_NO_LIGHT_VOLUMES, HALO_H2_NO_EFFECT_DECALS: each left as before.

Left out (logged): beams, the
particles' attached particle systems and collision/death effects, a
weapon's lights (its illumination, which this engine's weapons have not:
the stand-in's firing light kept), lights scaled by functions the made
objects have not, sweeteners beyond the first.
HALO_H2_NO_EFFECTS: none of it (the stand-ins as they were).

Distortion particles (generic\distortion\*): made on the desktop tier for
the effects that hold them - energy and shield impacts, the gauss fire, lifts,
teleports. Halo 2's rocket and grenade detonations hold none (H2EK and
the player's maps); their shockwave is additive.
HALO_H2_EFFECTS_DEBUG=1 logs each one, and each emitter left out and why.
*/

#ifdef H2_LEVEL_INTERNAL

#include "h2_particle_styles.h"
#include "h2_lighting.h"
#include <errno.h>

#include <math.h>
#ifdef HALO_LINUX
#include "render_epoch.h" /* HALO_ENV_ONCE */
#endif

void h2_level_folder_create(void);

/* ---------- constants */

enum
{
	/* (the made levels' files are made again when it changes: h2_effects_signature) */
	H2_EFFECTS_BUILDER_VERSION = 57,   /* (57 (h2/render-accuracy: a light's periodic brightness animation kept, H2BA at the made light's 0x120)) (56 (fix/h2-vehicle-fx): explosive objects' stages) (55 (fix/h2-vehicle-fx r4); 54 (h2cine r5); 53 (fix/h2-vehicle-fx): the shields' recharge rings drawn, 220 sprite sheets) (52 (fix/h2-vehicle-fx): the accelerations of effects, the Warthog's tail and gated head lights) (50 (fix/h2-rocket-hunter): cloth impacts on type 24, the Hunter armor and skin types 12, 13 their own; 49 (fix/h2-vehicle-fx r2): effect limit, water contacts; 48 (fix/h2-map-parity): level objects' marker names spaced; 47 (fix/h2-vehicle-fx): boost flares, glass by name, every shader's material; 46: glints, a looped emitter's chance, the Banshee's wingtip contrails; 45 (fix/h2-sky-weather): the weather's plates on, their planes' fields; 44: decals merged; 43 (fix/h2-sky-weather): lens flares' sun flag, corona rotation function and reflection flags, up to 12 reflections, the skies' sun flares; 42 (fix/h2-mapfx-11): particle_plasma models, shader flag 12; 41 (fix/h2-mapfx-11): each sprite 2 x its size along its longer side, inputs A and B at 1; 40 (h2bugs): lights black at no intensity, vehicles' attachments off where flashlights_allowed is; 39: 38 (fix/h2-weapons-11): light volumes' x-buffer spacing and iterations kept, their radius world units with no view floor; 38 (h2glow): a vehicle attachment scaled by no function of its vehicle left out, the Warthog's "jump" tail lights; 37 (fix/h2-weapons-11): light volumes in world units with Halo 2's radius floor (1/32 of the view); 36 (fix/h2-weapons-11): vehicle guns' firing effects Halo 2's own; 35 (h2animfx): emitters' emission shapes (line, disc, tube, halo) placed by effects.c, attached particle systems; 34 (h2fx2): impacts' "forward" this engine's negative incident (Halo 2's bullet holes made), particle systems' sort bias; 33 (h2fxparts): sprites that do not spin upright, vertically authored oriented ones along their height; 32 (h2fxparts): no tint from the ground's texture (HALO_H2_DIFFUSE_TINT); 31 (h2fxparts): prt3 flags read by their bits - lightmap lighting 7, deaths 9..12; 30 (h2fxparts): a projectile's material effects by its response - a bounce's, sticking's, a timed detonation's - and the stand-in's own impacts taken out; 29 (h2fx): the weather's background plates; 28 (h2fx): light volumes scaled by marker in world units; 27 (h2fx): distortion particles (the desktop's); 26 (h2fx): particle models' meshes; 25 (h2fx): bipeds' shield effects; 24 (h2fx): crates thrown by effects as debris objects; 23 (h2fx): weapons' ready flares, third-person-only flares; 22 (h2fx): the additive tint pass (flag 14), still oriented particles along their emission; 21: 20 (h2fx): weather; 19 (h2fx): vehicle guns' bolts, attached lens flares; 18 (h2fx): contrails' blend and fade at 0x66 and 0x68; 17 (h2fx): lens flares' corona scale; 16 (h2fx): light volumes as widgets, an impact's forward its incident; 15 (h2fx): Halo 2's lights, lens flares, light volumes and effects' decals made this engine's; 14 (r03a): a level object's attachments at its model's markers, the origin without; 13: r03a merged - 12 (r03a): level objects' attachments at their origin; 12: t6 - an object's attachments at most 8 (05b's gondola); 11: fix/h2-vehicle-damage - the effect limit 320, vehicles' death blasts; 10: S2 - the Banshee bomb's detonation; 9: S2 - the made vehicles' attachments; 7: S7, the Sentinel Beam's liquid beam as contrails; 7 (s1): the level objects' attachments, 224 effects; 8: both merged) */

	H2FX_MAXIMUM_EFFECTS = 420,         /* (160 was full on Coagulation: its objects' attachments left out; 224 with the vehicles' damage effects) */
	H2FX_MAXIMUM_EMITTERS = 2400,   /* (each a particle tag; Containment makes 1305. The Vita's capped tier stops at 1200) */
	H2FX_MAXIMUM_EMITTERS_CAPPED = 1200,
	H2FX_MAXIMUM_SHEETS = 220,
	H2FX_MAXIMUM_SHEET_IMAGES = 8,
	H2FX_MAXIMUM_SHEET_SEQUENCES = 96,
	H2FX_MAXIMUM_SHEET_SPRITES = 64,
	H2FX_MAXIMUM_LOCATIONS = 16,
	H2FX_MAXIMUM_EVENTS = 32,
	H2FX_MAXIMUM_REPEATS = 8,           /* entries an emitter writes (this engine counts 255 at most an entry) */
	H2FX_ENTRY_COUNT = 250,
	H2FX_MAXIMUM_SOURCES = 2,           /* Halo 2 effects merged into one (an impact and its sweetener) */
	H2FX_MAXIMUM_TARGETS = 1200,        /* (512 was full once grenades' bounces were made: Zanzibar 512+) */
	H2FX_FUNCTION_FLOATS = 40,

	/* this engine's effect (effects.c's layouts) */
	H2FX_EFFECT_SIZE = 0x40,
	H2FX_EFFECT_LOCATIONS = 0x28,
	H2FX_EFFECT_EVENTS = 0x34,
	H2FX_EVENT_SIZE = 0x44,
	H2FX_DAMAGE_REGION_SIZE = 0x54,            /* (damage.c's damage_region) */
	H2FX_DAMAGE_REGION_DESTROYED_EFFECT = 0x38,
	H2FX_COLLISION_BODY_DESTROYED_EFFECT = 0xBC,   /* (a collision model's: damage.c's assert) */
	H2FX_EVENT_PARTS = 0x2C,
	H2FX_EVENT_PARTICLES = 0x38,
	H2FX_PART_SIZE = 0x68,
	H2FX_PART_REFERENCE = 0x18,
	H2FX_PARTICLES_SIZE = 0xE8,
	H2FX_LOCATION_SIZE = 32,
	H2FX_PARTICLE_SIZE = 0x164,         /* sizeof(struct particle_definition) */

	/* Halo 2's */
	H2FX_H2_EVENT_SIZE = 0x38,
	H2FX_H2_PART_SIZE = 0x38,
	H2FX_H2_SYSTEM_SIZE = 0x38,
	H2FX_H2_EMITTER_SIZE = 0xB8,
	H2FX_H2_SPRITE_SIZE = 0x20,
	H2FX_H2_SEQUENCE_SIZE = 0x3C,
};

/* this engine's particle (effects/particles.h's struct particle_definition:
its header's shader layout is not this file's to include) */
enum
{
	PART_FLAGS = 0x00,
	PART_MESH = 0x34,                   /* (reserved34: a particle model's mesh, h2_particle_styles.h) */
	PART_BITMAP = 0x04,
	PART_PHYSICS = 0x14,
	PART_COLLISION_MATERIAL_EFFECTS = 0x24,
	PART_LIFE = 0x38,
	PART_FADE_IN = 0x40,
	PART_FADE_OUT = 0x44,
	PART_COLLISION_EFFECT = 0x48,
	PART_EFFECT = 0x58,
	PART_MINIMUM_PIXELS = 0x68,
	PART_RADIUS = 0x74,
	PART_FRAMES_PER_SECOND = 0x80,
	PART_CONTACT_DETERIORATION = 0x88,
	PART_LOD_FALLOFF = 0x8C,
	PART_LOD_CUTOFF = 0x90,
	PART_FIRST_SEQUENCE = 0x98,
	PART_INITIAL_SEQUENCES = 0x9A,
	PART_LOOPING_SEQUENCES = 0x9C,
	PART_FINAL_SEQUENCES = 0x9E,
	PART_OO_WIDTH = 0xA8,
	PART_ORIENTATION = 0xAC,
	PART_SHADER = 0xB0,
	PART_SHADER_FLAGS = PART_SHADER + 0x28,
	PART_SHADER_BLEND = PART_SHADER + 0x2A,
	PART_SHADER_FADE = PART_SHADER + 0x2C,
	PART_SHADER_MAP_FLAGS = PART_SHADER + 0x2E,
	PART_SHADER_RESERVED = PART_SHADER + 0x30,      /* 28 bytes */
	PART_SHADER_SECONDARY_MAP = PART_SHADER + 0x4C,
	PART_SHADER_RESERVED_AFTER = PART_SHADER + 0xA0 /* 20 bytes */
};

/* what a converted effect replaces */
enum h2fx_target_kind
{
	_h2fx_target_effect = 0,            /* an effect tag (by name): made again */
	_h2fx_target_projectile_effect,     /* a projectile's detonation effect (proj 'effect') */
	_h2fx_target_projectile_super,      /* its super detonation */
	_h2fx_target_projectile_started,    /* its detonation started effect */
	_h2fx_target_projectile_material,   /* a projectile's material response's effect on its impact (material, the event: h2fx_response_event) */
	_h2fx_target_projectile_detonation, /* a material response's detonation effect (material): a timed one's, the material under it */
	_h2fx_target_vehicle_destroyed,     /* a vehicle's collision model's body depleted effect */
	_h2fx_target_vehicle_damaged,       /* its body damaged effect */
	_h2fx_target_named,                 /* none: found by name at run time (h2_vehicle_wrecks.c) */
	_h2fx_target_projectile_attachments, /* a projectile's attachments: Halo 2's effects and contrails (its lights and sounds kept) */
	_h2fx_target_biped_shield,          /* a biped's collision model's shield damaged (material 0), depleted (1), recharging (2) effect */
	_h2fx_target_biped_region,          /* a biped's collision model's region (marker: its name) destroyed effect */
	_h2fx_target_object_stage,          /* a level object's damage stage's looping effect (material: the stage) */
	_h2fx_target_object_destroyed,      /* a level object's collision model's body destroyed effect (an explosive's) */
	_h2fx_target_object_shield          /* a level object's collision model's shield damaged (material 0), depleted (1), recharging (2) effect */
};

/* this engine's material types and Halo 2's material names that stand for
them on an impact (the first the weapon's material effects have) */
static struct h2fx_material
{
	short ce;
	char const *h2[3];
} const h2fx_materials[] =
{
	{ 0, { "tough_terrain_dirt", "tough_terrain", "soft_terrain" } },        /* dirt */
	{ 1, { "tough_terrain_sand", "tough_terrain", "soft_terrain" } },        /* sand */
	{ 2, { "hard_terrain_stone", "hard_terrain_concrete", "hard_terrain" } },/* stone */
	{ 3, { "soft_terrain_snow", "soft_terrain", "hard_terrain_ice" } },      /* snow */
	{ 4, { "tough_organic_wood", "soft_organic_plant", NULL } },             /* wood */
	{ 5, { "hard_metal_thin", "hard_metal_thick", NULL } },                  /* metal hollow */
	{ 6, { "hard_metal_thin", "hard_metal_thick", NULL } },                  /* metal thin */
	{ 7, { "hard_metal_thick", "hard_metal_solid", NULL } },                 /* metal thick */
	{ 8, { "tough_inorganic_rubber", "tough_inorganic", NULL } },            /* rubber */
	{ 9, { "brittle_glass", "brittle", NULL } },                             /* glass */
	{ 10, { "energy", NULL, NULL } },                                        /* force field */
	{ 14, { "soft_organic_flesh_elite", "hard_metal_thin_cov_elite", "hard_metal_thin" } }, /* elite (its flesh: Halo 2 Elites bleed blue) */
	{ 21, { "hard_metal_thin_hum_masterchief", "hard_metal_thin", NULL } },  /* cyborg armor */
	{ 22, { "energy_shield_thin", "energy", NULL } },                        /* cyborg energy shield */
	{ 23, { "hard_metal_thin_hum_masterchief", "hard_metal_thin", NULL } },  /* human armor */
	{ 25, { "hard_metal_solid", "hard_metal_thick", NULL } },                /* sentinel (Halo 2's Scorpion's armor, h2_damage.c) */
	{ 26, { "hard_metal_solid", "hard_metal_thick", NULL } },                /* monitor (the Wraith's) */
	{ 27, { "tough_inorganic_plastic", "tough_inorganic", NULL } },          /* plastic */
	{ 28, { "liquid_thin_water", "liquid_thin", "liquid" } },                /* water (Halo 2's materials' parents, in turn) */
	/* (slots no Halo 2 surface used, for its families with effects of their
	own: h2fx_surface_material) */
	{ 18, { "liquid_thin_water_shallow", "liquid_thin", "liquid" } },        /* shallow water, a puddle */
	{ 17, { "brittle_elec", "brittle", NULL } },                             /* electronics */
	{ 24, { "soft_organic_cloth", "soft_organic", NULL } },                  /* cloth (24, the human skin's, which no Halo 2 surface has: 13 is the Hunter's skin) */
	{ 12, { "hard_metal_solid_cov_hunter", "hard_metal_solid", "hard_metal_thick" } }, /* the Hunter's armor (h2_damage.c) */
	{ 13, { "soft_organic_flesh_hunter", "soft_organic_flesh", "soft_organic" } },     /* the Hunter's skin */
	{ 29, { "soft_organic_plant_leafy", "soft_organic_plant", NULL } },      /* leaves */
	{ 30, { "energy_shield_thin_cov", "energy_shield_thin", NULL } },        /* elite energy shield */
	{ 31, { "hard_terrain_ice", "hard_terrain", NULL } },                    /* ice */
	{ 32, { "hard_metal_thick", "hard_metal_thin", NULL } },                 /* hunter shield (the plasma cannon's armor) */
};

/* the Halo 2 material names that stand for this engine's material type on
an impact (h2fx_materials'), k-th, NULL past them (h2_weapons.c: the
material responses by type) */
H2_ALIGN static char const *h2fx_material_stand_in(short type, long k)
{
	long m;

	for (m = 0; m < (long)NUMBEROF(h2fx_materials); m++)
	{
		if (h2fx_materials[m].ce == type)
			return k >= 0 && k < 3 ? h2fx_materials[m].h2[k] : NULL;
	}
	return NULL;
}

/* the effects made and where they go: an entry a weapon, projectile or
vehicle of this engine's that has a Halo 2 one (h2fx_purposes_gather) */
struct h2fx_target
{
	boolean off;                /* (a vehicle attachment off on this level) */
	boolean gated;                  /* (a vehicle's light by flashlights_allowed's flag, h2_vehicle_flashlight_value) */
	short kind;                 /* h2fx_target_kinds */
	short material;             /* (_h2fx_target_projectile_material) this engine's material type */
	short event;                /* (_h2fx_target_projectile_material) Halo 2's material effects' event: 15 impact, 18 attach, 19 bounce */
	char name[128];             /* the tag of this engine's (a tag name of the level's tags) */
	unsigned long group;        /* its group */
	short effect;               /* h2fx.effects' (an attachment's: NONE when a contrail) */
	short contrail;             /* (an attachment) h2fx.contrails', NONE */
	short light;                /* (an attachment) h2fx.lights', NONE */
	short volume;               /* (an attachment) h2fx.volumes', NONE */
	char marker[32];            /* (an attachment) its marker */
	char scale[32];             /* (an attachment) Halo 2's scale function's name, "" none */
	char markers[256];          /* (a level object's attachment) its model's markers, ";name;name;" (h2_level.c writes them) */
};

/* a Halo 2 contrail (cont) made this engine's: its bitmap a sheet */
struct h2fx_contrail
{
	long h2_tag;
	short sheet;
	unsigned char fields[0x30];     /* Halo 2's flags..frames a second (0..0x2F: this engine's the same) */
	short first_sequence, sequence_count;
	short blend, fade;
	long state_count;
	struct h2fx_contrail_state
	{
		float duration[2], transition[2];
		short physics;              /* h2fx_physics' */
		float width;
		float color[2][4];          /* a, r, g, b */
		unsigned long scale_flags;
	} states[4];
};

/* a Halo 2 light (ligh) made this engine's: a point light (0x160) - its
radius, its diffuse colour at its brightness, how long it lasts and fades,
its lens flare (h2fx.lenses', NONE). Halo 2's light, read off the MCC
data with the kit's field names: flags 0, type 4, radius 0x18, diffuse
colour bounds 0x58 and 0x64, brightness bounds 0x70, lens flare 0x90,
light volume 0x9C, duration 0xB0, its falloff function 0xB4 */
struct h2fx_light
{
	long h2_tag;
	char name[48];              /* (its tag's short name) */
	float radius;
	float color[3];             /* (diffuse upper times brightness upper, its largest channel 1) */
	float strength;             /* (that largest channel before: brighter than 1 widens the radius) */
	float duration;
	short falloff;
	short lens;
	boolean lens_only;          /* (made for a lens flare alone: an effect's lens part) */
	/* (its brightness animation, 0xC4: a function of the game's time in
	seconds - a periodic one alone is kept, written into the made light's
	free bytes, H2_LIGHT_ANIMATION_*: Cairo's emergency lights flash) */
	boolean animation;
	unsigned char animation_type, animation_flags, animation_shape[2];
	float animation_range[2], animation_data[8];
	long animation_count;
	float brightness;           /* (the brightness the colour was made at) */
};

/* a Halo 2 lens flare (lens) made this engine's (0xF0): its bitmap a sheet,
its reflections (0x40, 0x30 each: bitmap index 4, position 8, rotation 0xC,
radius bounds 0x10, brightness bounds 0x18, colour 0x24); occlusion radius
0x10, near and far fade 0x18 */
struct h2fx_lens
{
	long h2_tag;
	char name[48];              /* (its tag's short name) */
	short sheet;
	float occlusion_radius, near_fade, far_fade;
	float corona_scale[2];      /* (0x34: its first reflection stretched - the anamorphic streaks) */
	boolean third_person_only;  /* (its flags 0x28, bit 3: not on a first-person weapon) */
	boolean sun;                /* (its flags 0x28, bit 0: "sun" - a sky light's, the sun_* flares; this engine's bit 0 too) */
	short rotation_function;    /* (0x2C: its corona's rotation function - none, A, B, rotation-translation, translation: this engine's enum) */
	float rotation_scale;       /* (0x30: that function's scale, an angle) */
	long reflection_count;
	/* (a reflection's flags, 0: align rotation with screen center 1, radius
	NOT scaled by distance 2, radius scaled by occlusion factor 4, occluded by
	solid objects 8 - this engine's bits, rasterizer_lights.c) */
	struct h2fx_reflection { short sequence; unsigned short flags; float position, rotation, radius[2], brightness[2], color[3]; } reflections[12];
};

/* a scenario sky's light with a lens flare (the sun: its direction and its
sun_* lens flare), made with the effects for h2_level.c's sky tags
(h2_effects_sky_light); Halo 2's sky tag: its lights block at 0x78 (0x24
each: direction vector 0, direction (yaw, pitch) 0xC, lens flare 0x14,
radiosity block 0x1C) */
struct h2fx_sky_light
{
	short scenario_sky;         /* (the scenario's skies' index) */
	short lens;                 /* (h2fx.lenses') */
	float direction[2];         /* (yaw, pitch, radians: towards the sun) */
};

/* a Halo 2 light volume's (MGS2) volume made this engine's light volume
(mgs2, 0x14C, one frame): Halo 2's volumes (8, 0x98 each: bitmap 4, sprite
count 0xC, then its offset, radius, brightness and colour functions of the
place along it at 0x10, 0x18, 0x20, 0x28 - data blocks); the near and far
fade 0 and 4. This engine's frame: the functions at the volume's ends */
struct h2fx_volume
{
	long h2_tag;
	char name[48];              /* (its tag's short name) */
	long volume;                /* (Halo 2's volume index) */
	char marker[32];            /* (its attachment's) */
	boolean world;              /* (Halo 2's "scaled by marker", flag 5: its radius world units - the Phantom's antigrav glow) */
	float view_floor;           /* (Halo 2's "radius frac min", kept in the tag, not applied) */
	short xbuffer_spacing;      /* (Halo 2's "x-buffer spacing", "x-buffer min iterations", "x-buffer max iterations": light_volumes.c) */
	short xbuffer_min, xbuffer_max;
	short sheet, sequence;
	short count;
	float near_fade, far_fade;
	float offset[2], radius[2], alpha[2], color[2][3];
};

/* a Halo 2 weather system (weat) made this engine's weather particle
system (rain, 0x30, one particle type of 0x25C): Halo 2's first particle
system (its block at 0, 0x?? each: sprites 0, view box 8, maximum velocity
0x18, mass 0x1C, size bounds 0x24, most particles 0x2C, type 0x6C - 1
snow, 2 rain -, opacity bounds 0x70, rain streak scale 0x78); a weather
palette entry of the scenario's (0x260, 0x88 each: name 0, weather system
0x20) each */
struct h2fx_weather
{
	long h2_tag;
	char name[32];              /* (the palette entry's) */
	short sheet;
	boolean rain;
	float box, velocity, size[2], opacity[2], streak;
	long most;
	/* (its background plates - the distant rain or snow beyond the near
	particles: Halo 2's first plate block's three textured planes, 0x18
	positions, 0x24 move speeds, 0x48 texture scales, 0x78 z near and far,
	0x88 opacities, 0x98 tints) */
	/* (its primary wind: the weat tag's direction vector at 0x14 - cos/sin of
	the kit's "wind primary heading/pitch" - and strength at 0x28; Lockout 15
	along +x, Backwash and Warlock 5 at heading 0.7. Its units are not known
	(the tag's gravity constant is 9.8, metric; the flakes' max velocity 1.0),
	so it is written only with HALO_H2_WEATHER_WIND=1, as metres a second) */
	float wind[3], wind_strength;
	short plate_count;
	struct h2fx_plate
	{
		short sheet;
		float z, speed[3], scale, opacity, tint[3];
		float position, z_near, z_far, mass;   /* (0..1 of plate z near..far, Halo 2's) */
		unsigned long flags;    /* (0 forward motion, 1 auto position planes, 2 auto scale planes) */
		float blend[2];         /* (depth blend z near and far, 0x80 and 0x84: the sheet blends in over the scene between them) */
	} plates[3];
};

/* a part of a Halo 2 effect's event made this engine's (a light, a lens
flare as a light, a decal): Halo 2's part (0x38: create in 0 and 2,
location 4, flags 6, its reference 0xC, velocity bounds 0x14, cone 0x1C,
angular velocity 0x20, radius modifier 0x28, A and B scales 0x30, 0x34) */
struct h2fx_part
{
	short event;                /* (this engine's event, the effect's) */
	short location;
	unsigned long group;        /* 'ligh' (index: h2fx.lights') or 'deca' (build->decals') */
	short index;
	short environment, disposition;
	unsigned short flags;
	float velocity[2], cone, angular_velocity[2], radius_modifier[2];
	unsigned long scale_a, scale_b;
};

/* a Halo 2 function, read */
struct h2fx_function
{
	boolean present;
	short input, range;
	unsigned char type, flags, shape[2];
	float minimum, maximum;
	float colors[4][3];
	float data[H2FX_FUNCTION_FLOATS];
	long data_count;
};

/* a sprite sheet: a Halo 2 bitmap's images (build->bitmaps'), its
sequences and sprites */
struct h2fx_sheet
{
	long h2_tag;
	long image_count;
	long images[H2FX_MAXIMUM_SHEET_IMAGES];
	long sequence_count;
	struct h2fx_sequence { short first, count; } sequences[H2FX_MAXIMUM_SHEET_SEQUENCES];
	/* (a sequence's sprites each a sequence of its own, after the rest: a
	random frame chosen as a random sequence - h2fx_sheet_split) */
	short split[H2FX_MAXIMUM_SHEET_SEQUENCES];
	long real_sequence_count;
	long sprite_count;
	struct h2fx_sprite { short image; float bounds[4]; float registration[2]; } sprites[H2FX_MAXIMUM_SHEET_SPRITES];
};

/* an emitter: a particle of this engine's (its tag) and an effect's
particles entry */
struct h2fx_emitter
{
	short effect, event;
	/* the particle */
	long prt3;
	long mesh;                  /* (a particle model, PRTM: the prt3 field's tag; its mesh drawn - render_sprite.c) NONE */
	char name[48];              /* (its tag's: Halo 2's particle's) */
	short sheet;
	short blend;                /* this engine's framebuffer blend function */
	short orientation;
	short first_sequence, sequence_count;
	boolean random_frame;
	float frames_per_second;
	unsigned long flags;        /* this engine's particle flags */
	float life[2];
	short physics;              /* h2fx_physics' */
	struct h2_particle_style style;
	boolean spins;              /* (Halo 2's prt3 flag 0: turned any way as it is born; else upright) */
	boolean tint_white_pass;    /* (particle_additive_tint: its pass's white stays white, shader flag 14) */
	/* (a particle_plasma model: its second noise's sheet, NONE not one; the
	pass's medium, wide and sharp colours, X8R8G8B8, and the two noises'
	scales - shader flag 12, the renderer's particle_plasma program) */
	short plasma_sheet;
	unsigned long plasma_colours[3];
	float plasma_scale[2];
	boolean distortion;         /* (a distortion particle: the screen behind it offset, shader flag 13 - the desktop's) */
	float distortion_scale;     /* (its Halo 2 distortion_scale parameter) */
	long death_h2, collision_h2;    /* (Halo 2's prt3 death and collision effects - effe alone -, NONE) */
	long child;                 /* (its attached particle system's emitter, h2fx.emitters' - made with no event of its own: a tag alone, emitted by particles.c from each particle; NONE) */
	float child_rate;           /* (a child: Halo 2's rate, particles a tick) */
	long death_target, collision_target;    /* (h2fx_particle_effects: their targets' (named) indices, NONE) */
	/* the effect's entry */
	short location;
	short camera_mode;
	short environment, disposition;
	boolean attached;
	short shape;                /* Halo 2's emission shape (0 sprayer, 1 disc, 2 globe, 3 implode, 4 tube, 5 halo, 6 impact contour, 7 impact area, 8 debris, 9 line, 10 custom points): effects.c places the particle by it */
	short count[2];
	short repeat;               /* entries written of it (its count each) */
	float chance;               /* (its rate under a particle a tick: that fraction, 0 none) */
	short distribution;
	float emission_radius[2];
	float velocity[2];
	float cone;
	float angular_velocity[2];
	float radius[2];
	float tint[2][4];           /* a, r, g, b */
	float offset[3];
	float direction[2];
};

struct h2fx_effect
{
	long sources[H2FX_MAXIMUM_SOURCES];
	long source_count;
	char name[128];
	unsigned long flags;
	short loop_start;
	long location_count;
	char locations[H2FX_MAXIMUM_LOCATIONS][32];
	long event_count;
	struct h2fx_event
	{
		float skip, delay[2], duration[2];
		/* (its accelerations, Halo 2's event's 0x28, 0x14 each: the push its
		object takes; two locations of one value, as Halo 2's vehicles' have) */
		float acceleration, cone[2];
		short acceleration_location[2];
	} events[H2FX_MAXIMUM_EVENTS];
	long first_emitter, emitter_count;
	long first_part, part_count;    /* (h2fx.parts') */
	boolean has_lights, has_decals; /* (Halo 2's own: the stand-in's of the kind dropped) */
	boolean written;
	/* (Halo 2's always and never play distances, 0x28 and 0x2C: kept in this
	engine's effect's unused00c[0] and [1] for h2_play_distance.c; merged
	sources: 0 - no rule - when any has none, else each the larger: it
	makes an effect Halo 2 would make, never drops one it would not) */
	float play_always, play_never;
};

/* particle physics: what kind of movement - this engine's point physics
of the kit's for Halo 2's particle physics (pmov), by name (Halo 2's
movements - gravity scale, drag, collision - are not read field by field:
the kit's that move alike) */
enum h2fx_physics
{
	_h2fx_physics_drift = 0,    /* flashes, explosions: no gravity */
	_h2fx_physics_debris,       /* casings, chips, sparks: falls, bounces */
	_h2fx_physics_smoke,        /* smoke, steam, dust: rises slowly */
	_h2fx_physics_hot_smoke,    /* hot smoke: rises */
	_h2fx_physics_fire,         /* flames: rise fast */
	_h2fx_physics_light,        /* sparks, light debris: falls slowly */
	NUMBER_OF_H2FX_PHYSICS
};

/* the kit's point physics for each (the first there) */
static char const *const h2fx_physics_names[NUMBER_OF_H2FX_PHYSICS][3] =
{
	{ "effects\\point physics\\vacuum particle", NULL, NULL },
	{ "effects\\point physics\\stones", "effects\\point physics\\dirt burst", "effects\\point physics\\debris shirt" },
	{ "effects\\point physics\\warm smoke cloud", "effects\\point physics\\cold smoke cloud", NULL },
	{ "effects\\point physics\\hot smoke cloud", "effects\\point physics\\hot smoke puff", NULL },
	{ "effects\\point physics\\fire", "effects\\point physics\\hot smoke puff", NULL },
	{ "effects\\point physics\\lightweight particle", "effects\\point physics\\stones", NULL },
};

/* ---------- globals */

static struct
{
	boolean enabled;
	short tier;                 /* h2_particle_style_tier's: 0 full (every platform), 1 capped (HALO_H2_EFFECTS=capped, measurements) */
	long effect_count;
	struct h2fx_effect *effects;
	long emitter_count;
	struct h2fx_emitter *emitters;
	long sheet_count;
	struct h2fx_sheet *sheets;
	long target_count;
	struct h2fx_target *targets;
	long effect_capacity, emitter_capacity, sheet_capacity, target_capacity;
	long left_out_beams, left_out_distortion, left_out_systems, left_out_emitters, left_out_light_volumes;
	long left_out_models;           /* (particle models not drawn: the Vita's tier, none of their map) */
	long mesh_count;                /* (h2_effects_write) the particle models' meshes written */
	long mesh_tags[64];
	unsigned long mesh_roots[64];
	long child_count, left_out_children;   /* (attached particle systems made; the systems and emitters past the first) */
	long contrail_count, contrail_capacity;
	long left_out_boost;
	struct h2fx_contrail *contrails;
	long kit_contrail;
	/* (h2_effects_write) the kit's tags the particles copy and move with */
	long kit_particle;
	long kit_physics[NUMBER_OF_H2FX_PHYSICS];
	/* Halo 2's lights, lens flares, light volumes and effects' parts
	(lights, decals) made this engine's */
	long light_count, light_capacity;
	struct h2fx_light *lights;
	long lens_count, lens_capacity;
	struct h2fx_lens *lenses;
	long volume_count, volume_capacity;
	struct h2fx_volume *volumes;
	long part_count, part_capacity;
	struct h2fx_part *parts;
	long effect_decal_count;     /* (made into build->decals) */
	long effect_debris_count;    /* (made into h2v.debris) */
	long weather_count;
	struct h2fx_weather weathers[16];   /* (the scenario's weather palette's entries, in order) */
	long sky_light_count;
	struct h2fx_sky_light sky_lights[16];   /* (the scenario's skies' suns) */
	long left_out_parts;
} h2fx;

/* (h2_effects_write: the sheets' first bitmap tag - the weather's plates) */
static long h2fx_bitmap_first;

H2_ALIGN static short h2fx_debris_for(struct build *build, long crate);
H2_ALIGN static boolean h2fx_vehicle_damage_effect(long effect);

/* (h2_effects_write) the made lights' first tag, the level's decals' */
static long h2fx_light_first, h2fx_decal_first;
/* (h2_effects_write, for the parts naming a tag by name) */
static unsigned long h2fx_write_instances;
static long h2fx_write_tag_count;
/* (h2_effects_write, for bsp_write after h2fx is let go) the weather
palette's tags (NONE: an entry not made) and how many */
static long h2fx_weather_tags[16], h2fx_weather_tag_count;

/* ---------- private code: room */

/* room for one more element of an array grown as it fills (by step); FALSE
when full (maximum) or out of memory */
H2_ALIGN static boolean h2fx_room(void **array, long *capacity, long count, long step, long maximum, unsigned long size)
{
	void *grown;

	if (count < *capacity)
		return TRUE;
	if (count >= maximum)
		return FALSE;
	grown = realloc(*array, (unsigned long)(*capacity + step) * size);
	if (!grown)
		return FALSE;
	memset((char *)grown + (unsigned long)*capacity * size, 0, (unsigned long)step * size);
	*array = grown;
	*capacity += step;
	return TRUE;
}

/* ---------- private code: reading */

H2_ALIGN static float h2fx_clamp01(float x)
{
	return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

H2_ALIGN static float h2fx_read_real(struct h2_cache *cache, unsigned long address)
{
	unsigned char const *p = h2_cache_at(cache, address, 4);

	return p ? readf(p) : 0.0f;
}

H2_ALIGN static short h2fx_read_short(struct h2_cache *cache, unsigned long address)
{
	unsigned char const *p = h2_cache_at(cache, address, 2);

	return p ? read16(p) : 0;
}

/* a colour of a function's (its bytes b, g, r, a) */
H2_ALIGN static void h2fx_color(unsigned char const *p, float rgb[3])
{
	rgb[0] = p[2] / 255.0f;
	rgb[1] = p[1] / 255.0f;
	rgb[2] = p[0] / 255.0f;
}

/* a property at address (input, range input, output modifier, the
function's block) */
H2_ALIGN static void h2fx_function_data(struct h2_cache *cache, unsigned long block, struct h2fx_function *f);

H2_ALIGN static void h2fx_function_read(struct h2_cache *cache, unsigned long address, struct h2fx_function *f)
{
	h2fx_function_data(cache, address + 8, f);
	f->input = h2fx_read_short(cache, address);
	f->range = h2fx_read_short(cache, address + 2);
}

/* a function's data block alone (at block: its count and address), its
input and range none (the light volumes': of the place along the volume) */
H2_ALIGN static void h2fx_function_data(struct h2_cache *cache, unsigned long block, struct h2fx_function *f)
{
	long count;
	unsigned long data;
	unsigned char const *p;

	memset(f, 0, sizeof(*f));
	if (!h2_cache_block(cache, block, 1, &count, &data) || count < 20 || count > 20 + 4 * H2FX_FUNCTION_FLOATS ||
		!(p = h2_cache_at(cache, data, (unsigned long)count)))
	{
		return;
	}
	f->present = TRUE;
	f->type = p[0];
	f->flags = p[1];
	f->shape[0] = p[2];
	f->shape[1] = p[3];
	f->minimum = readf(p + 4);
	f->maximum = readf(p + 8);
	h2fx_color(p + 4, f->colors[0]);
	h2fx_color(p + 8, f->colors[1]);
	h2fx_color(p + 12, f->colors[2]);
	h2fx_color(p + 16, f->colors[3]);
	f->data_count = (count - 20) / 4;
	for (count = 0; count < f->data_count; count++)
		f->data[count] = readf(p + 20 + 4 * count);
}

H2_ALIGN static boolean h2fx_function_is_color(struct h2fx_function const *f)
{
	return (f->flags & 0x70) != 0;
}

/* a colour function's colours (its flags' bits 4..6: one to four) */
H2_ALIGN static int h2fx_color_count(struct h2fx_function const *f)
{
	int n = (f->flags >> 4) & 7;

	return n < 1 ? 1 : n > 4 ? 4 : n;
}

/* Halo 2's transition shapes */
H2_ALIGN static float h2fx_shape(float x, unsigned char shape)
{
	x = h2fx_clamp01(x);
	switch (shape)
	{
		case 1: return 1.0f - (1.0f - x) * (1.0f - x);                       /* early */
		case 2: { float y = 1.0f - x; return 1.0f - y * y * y * y; }          /* very early */
		case 3: return x * x;                                                 /* late */
		case 4: return x * x * x * x;                                         /* very late */
		case 5: return 0.5f - 0.5f * cosf(3.14159265f * x);                   /* cosine */
		case 6: return 1.0f;                                                  /* one */
		case 7: return 0.0f;                                                  /* zero */
		default: return x;                                                    /* linear */
	}
}

/* one curve's value (0..1 mostly) at x: curve 0 or 1 of a ranged one */
H2_ALIGN static float h2fx_curve(struct h2fx_function const *f, long curve, float x)
{
	float const *d;
	long per;

	switch (f->type)
	{
		case 0:
			return x;
		case 1:
			per = 1;
			break;
		case 2:
			per = 2;
			break;
		case 3:
			per = 4;
			break;
		case 4:
			per = 6;
			break;
		case 5:
			per = 20;
			break;
		case 7:
		case 10:
			per = 12;
			break;
		default:
			return 1.0f;
	}
	if ((curve + 1) * per > f->data_count)
		curve = 0;
	if (per > f->data_count)
		return 1.0f;
	d = f->data + curve * per;
	switch (f->type)
	{
		case 1:
			return d[0];
		case 2:
			return d[0] + (d[1] - d[0]) * h2fx_shape(x, f->shape[curve]);
		case 3:
		{
			/* (frequency, phase, min, max: a cosine; Halo 2's other periodic
			shapes - noise, jitter, spark - their middle) */
			float wave = f->shape[curve] == 2 || f->shape[curve] == 3 ? 0.5f - 0.5f * cosf(6.2831853f * (d[0] * x + d[1])) : 0.5f;

			return d[2] + (d[3] - d[2]) * wave;
		}
		case 4:
			if (d[2] != d[0])
				return d[1] + (d[3] - d[1]) * h2fx_clamp01((x - d[0]) / (d[2] - d[0]));
			return d[1];
		case 5:
		{
			/* (four keys: x, y each) */
			long k;

			if (x <= d[0])
				return d[1];
			for (k = 0; k < 3; k++)
			{
				if (x <= d[2 * k + 2])
				{
					float span = d[2 * k + 2] - d[2 * k];

					return span > 0.0f ? d[2 * k + 1] + (d[2 * k + 3] - d[2 * k + 1]) * (x - d[2 * k]) / span : d[2 * k + 3];
				}
			}
			return d[7];
		}
		case 7:
		case 10:
		{
			/* (four points, then the cubic through them: c3 x^3 + c2 x^2 +
			c1 x + c0, x 0..1) */
			float xx = h2fx_clamp01(x);

			return ((d[8] * xx + d[9]) * xx + d[10]) * xx + d[11];
		}
	}
	return 1.0f;
}

/* (the part of a long event being made: Halo 2's emitter age, 0..1 -
h2fx_effect_for slices such an event) */
static float h2fx_emitter_age;
/* (the effects made while it is set - a held weapon's attachments: the
sword's blade glow - leave their particles in the world where they are
born, Halo 2's local ones too: the blade's glow stays behind it as it
swings, its arc) */
static boolean h2fx_world_particles;
/* (h2fx_parts_read: Halo 2's sound parts kept as this engine's - the ready
effects' alone: the sword's ignition, owner playtest Oct 30 item 3. The
firing and impact effects get their sound from the stand-in's sound part,
h2w_effect_sound, and would sound twice) */
static boolean h2fx_sound_parts;

/* the function's value (0..1 of its range) at x, the range input at r
(an input of the emitter's age: the slice's) */
H2_ALIGN static float h2fx_fraction(struct h2fx_function const *f, float x, float r)
{
	float a;

	if (f->input == 4)
		x = h2fx_emitter_age;
	if (f->range == 4)
		r = h2fx_emitter_age;
	a = h2fx_curve(f, 0, x);

	if (f->flags & 1)
		a = a + (h2fx_curve(f, 1, x) - a) * h2fx_clamp01(r);
	return a;
}

/* a scalar function's value */
H2_ALIGN static float h2fx_scalar(struct h2fx_function const *f, float x, float r, float none)
{
	if (!f->present)
		return none;
	return f->minimum + (f->maximum - f->minimum) * h2fx_fraction(f, x, r);
}

/* a colour function's value (white if none) */
H2_ALIGN static void h2fx_rgb(struct h2fx_function const *f, float x, float r, float rgb[3])
{
	float g;
	int c;

	if (!f->present || !h2fx_function_is_color(f))
	{
		rgb[0] = rgb[1] = rgb[2] = 1.0f;
		return;
	}
	g = h2fx_clamp01(h2fx_fraction(f, x, r));
	/* (its colours by their count - the flags' bits 4..6, read off the
	data: one the first; two the first and the last; three the first, the
	second and the last (the third slot unused, often left black); four a
	gradient through them all. Four were taken for none - the sword's
	purple and blue glow, the fires' black to orange to white drawn white -
	and three as four - a dip to the unused black mid-life) */
	{
		static int const slots[5][4] = { { 0 }, { 0 }, { 0, 3 }, { 0, 1, 3 }, { 0, 1, 2, 3 } };
		int n = h2fx_color_count(f);

		if (n == 1)
		{
			for (c = 0; c < 3; c++)
				rgb[c] = f->colors[0][c];
		}
		else
		{
			float at = g * (float)(n - 1);
			int k = at >= (float)(n - 1) ? n - 2 : (int)at;
			float const *from = f->colors[slots[n][k]], *to = f->colors[slots[n][k + 1]];

			for (c = 0; c < 3; c++)
				rgb[c] = from[c] + (to[c] - from[c]) * (at - (float)k);
		}
	}
}

/* whether a function changes over the particle's life (its input or range
the particle's age) */
H2_ALIGN static boolean h2fx_over_life(struct h2fx_function const *f)
{
	return f->present && f->type != 1 && f->input == 0;
}

/* a function's bounds over the inputs a particle is made with (randoms,
the emitter's age...: 0 and 1), the particle's age 0 */
H2_ALIGN static void h2fx_bounds(struct h2fx_function const *f, float none, float bounds[2])
{
	float a, b;

	if (!f->present)
	{
		bounds[0] = bounds[1] = none;
		return;
	}
	if (f->input == 0)
		a = h2fx_scalar(f, 0.0f, 0.0f, none), b = h2fx_scalar(f, 0.0f, 1.0f, none);
	else if (f->input == 9 || f->input == 10)
		/* (the effect's A or B scale - 1 unless its creator says less: at 1,
		its spread the range input's; this engine scales counts by A itself.
		Taken as a random 0..1 such a size or alpha came out anything down to
		nothing. Decomp function_248df1 cases 9, 10) */
		a = h2fx_scalar(f, 1.0f, 0.0f, none), b = h2fx_scalar(f, 1.0f, 1.0f, none);
	else
		a = h2fx_scalar(f, 0.0f, 0.5f, none), b = h2fx_scalar(f, 1.0f, 0.5f, none);
	bounds[0] = a < b ? a : b;
	bounds[1] = a < b ? b : a;
}

/* ---------- private code: sprite sheets */

/* an image of a bitmap (as bitmap_for does for the first: one this engine
draws), build->bitmaps' index, NONE */
H2_ALIGN static long h2fx_image(struct build *build, long tag, long image)
{
	struct h2_cache_tag t;
	long count, i;
	unsigned long images;
	unsigned char const *p;
	struct bitmap *b;

	if (image == 0)
		return bitmap_for(build, tag);
	for (i = 0; i < build->bitmap_count; i++)
	{
		if (build->bitmaps[i].h2_tag == tag && build->bitmaps[i].usage == 0x80 + image)
			return i;
	}
	if (!h2_cache_tag_get(build->cache, tag, &t) ||
		!h2_cache_block(build->cache, t.address + 0x44, IMAGE_SIZE(build), &count, &images) || image >= count ||
		!(p = h2_cache_at(build->cache, images + IMAGE_SIZE(build) * (unsigned long)image, IMAGE_SIZE(build))))
	{
		return NONE;
	}
	b = realloc(build->bitmaps, (build->bitmap_count + 1) * sizeof(*b));
	if (!b)
		return NONE;
	build->bitmaps = b;
	b = &build->bitmaps[build->bitmap_count];
	memset(b, 0, sizeof(*b));
	b->h2_tag = tag;
	b->width = (unsigned short)read16(p + 4);
	b->height = (unsigned short)read16(p + 6);
	b->format = (unsigned short)read16(p + 0xC);
	b->flags = (unsigned short)read16(p + 0xE);
	b->mipmaps = (unsigned short)read16(p + 0x14);
	b->reference = read32(p + IMAGE_REFERENCE(build));
	b->size = read32(p + IMAGE_BYTES(build));
	if (read16(p + 0xA) != 0 || !b->size || b->reference == 0xFFFFFFFFUL || b->width == 0 || b->height == 0 ||
		b->width > 4096 || b->height > 4096 ||
		!(b->format == 14 || b->format == 15 || b->format == 16 || b->format == 0 || b->format == 1 || b->format == 2 ||
			b->format == 3 || b->format == 6 || b->format == 8 || b->format == 9 || b->format == 10 || b->format == 11))
	{
		return NONE;
	}
	snprintf(b->path, sizeof(b->path), "h2\\bitmaps\\%s %ld", tag_short_name(h2_cache_tag_name(build->cache, tag)), image);
	/* (its usage marks which image it is: written as 0 - h2_effects_bitmaps_write) */
	b->usage = (unsigned short)(0x80 + image);
	return build->bitmap_count++;
}

/* a DXT3 block's explicit alpha (16 four-bit values) as a DXT5 block's
interpolated one (0, 255 and six between): its colour block as it is */
H2_ALIGN static void h2fx_dxt3_to_dxt5(unsigned char *block)
{
	static unsigned char const levels[8] = { 255, 0, 219, 182, 146, 109, 73, 36 };
	unsigned long long bits = 0;
	int i, k;

	for (i = 0; i < 16; i++)
	{
		int a = ((block[i / 2] >> ((i & 1) * 4)) & 0xF) * 17, best = 0, error = 1000;

		for (k = 0; k < 8; k++)
		{
			int d = a > levels[k] ? a - levels[k] : levels[k] - a;

			if (d < error)
				error = d, best = k;
		}
		bits |= (unsigned long long)best << (3 * i);
	}
	block[0] = 255;
	block[1] = 0;
	for (i = 0; i < 6; i++)
		block[2 + i] = (unsigned char)(bits >> (8 * i));
}

/* (a DXT3 image: made DXT5 here, its pixels read now - this engine's
OpenGL draws Halo 2's DXT3 sheets' alpha as none) */
H2_ALIGN static void h2fx_image_dxt3(struct build *build, long index)
{
	struct bitmap *b = &build->bitmaps[index];
	unsigned long got = 0, i;
	unsigned char *p;

	if (b->format != 15 || b->pixels || b->width > 256 || b->height > 256 || getenv("HALO_H2_EFFECTS_DXT3"))
		return;
	unsigned long entry_bytes = b->size;

	p = h2_cache_texture_read(build->cache, b->reference, b->size, &got);
	if (!p)
		return;
	{
		unsigned char *pixels = malloc(got);

		if (pixels)
		{
			memcpy(pixels, p, got);
			for (i = 0; i + 16 <= got; i += 16)
				h2fx_dxt3_to_dxt5(pixels + i);
			b->pixels = pixels;
			b->pixels_size = got;
			b->size = got;
			b->format = 16;
			/* (made again alone at load from this: piece J, h2_level.c derived_make) */
			memset(&b->recipe, 0, sizeof(b->recipe));
			b->recipe.kind_plus1 = _derived_recipe_effect_dxt3 + 1;
			b->recipe.args[0] = b->reference;
			b->recipe.args[1] = entry_bytes;
		}
	}
	h2_cache_texture_free(build->cache, p);
}

/* the sheet of a Halo 2 bitmap (h2fx.sheets' index), NONE if it cannot
be drawn here */
H2_ALIGN static short h2fx_sheet_for(struct build *build, long tag)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	struct h2fx_sheet *s;
	long i, count, image_count;
	unsigned long sequences, images;

	if (tag == NONE)
		return NONE;
	for (i = 0; i < h2fx.sheet_count; i++)
	{
		if (h2fx.sheets[i].h2_tag == tag)
			return (short)i;
	}
	if (!h2fx_room((void **)&h2fx.sheets, &h2fx.sheet_capacity, h2fx.sheet_count, 8, H2FX_MAXIMUM_SHEETS, sizeof(*h2fx.sheets)) ||
		!h2_cache_tag_get(cache, tag, &t) ||
		!h2_cache_block(cache, t.address + 0x44, IMAGE_SIZE(build), &image_count, &images) || image_count < 1)
	{
		return NONE;
	}
	s = &h2fx.sheets[h2fx.sheet_count];
	memset(s, 0, sizeof(*s));
	s->h2_tag = tag;
	if (image_count > H2FX_MAXIMUM_SHEET_IMAGES)
		image_count = H2FX_MAXIMUM_SHEET_IMAGES;
	for (i = 0; i < image_count; i++)
	{
		s->images[i] = h2fx_image(build, tag, i);
		if (s->images[i] == NONE)
			return NONE;
		h2fx_image_dxt3(build, s->images[i]);
	}
	s->image_count = image_count;
	/* its sequences: their sprites, or each its first image whole */
	if (h2_cache_block(cache, t.address + 0x3C, H2FX_H2_SEQUENCE_SIZE, &count, &sequences))
	{
		for (i = 0; i < count && s->sequence_count < H2FX_MAXIMUM_SHEET_SEQUENCES; i++)
		{
			unsigned long q = sequences + H2FX_H2_SEQUENCE_SIZE * (unsigned long)i, sprites;
			long sprite_count, k;
			struct h2fx_sequence *o = &s->sequences[s->sequence_count];

			o->first = (short)s->sprite_count;
			o->count = 0;
			if (h2_cache_block(cache, q + 0x34, H2FX_H2_SPRITE_SIZE, &sprite_count, &sprites) && sprite_count > 0)
			{
				for (k = 0; k < sprite_count && s->sprite_count < H2FX_MAXIMUM_SHEET_SPRITES; k++)
				{
					unsigned char const *p = h2_cache_at(cache, sprites + H2FX_H2_SPRITE_SIZE * (unsigned long)k, H2FX_H2_SPRITE_SIZE);
					struct h2fx_sprite *sp = &s->sprites[s->sprite_count];

					if (!p || read16(p) < 0 || read16(p) >= image_count)
						continue;
					sp->image = read16(p);
					sp->bounds[0] = readf(p + 8);
					sp->bounds[1] = readf(p + 0xC);
					sp->bounds[2] = readf(p + 0x10);
					sp->bounds[3] = readf(p + 0x14);
					sp->registration[0] = readf(p + 0x18);
					sp->registration[1] = readf(p + 0x1C);
					s->sprite_count++;
					o->count++;
				}
			}
			else if (s->sprite_count < H2FX_MAXIMUM_SHEET_SPRITES)
			{
				short first = h2fx_read_short(cache, q + 0x20);
				struct h2fx_sprite *sp = &s->sprites[s->sprite_count];

				sp->image = first >= 0 && first < image_count ? first : 0;
				sp->bounds[0] = 0.0f, sp->bounds[1] = 1.0f, sp->bounds[2] = 0.0f, sp->bounds[3] = 1.0f;
				sp->registration[0] = sp->registration[1] = 0.5f;
				s->sprite_count++;
				o->count = 1;
			}
			if (o->count > 0)
				s->sequence_count++;
		}
	}
	if (s->sequence_count == 0)
	{
		/* (none: a sequence of each image whole) */
		for (i = 0; i < image_count && s->sprite_count < H2FX_MAXIMUM_SHEET_SPRITES; i++)
		{
			struct h2fx_sprite *sp = &s->sprites[s->sprite_count];

			sp->image = (short)i;
			sp->bounds[0] = 0.0f, sp->bounds[1] = 1.0f, sp->bounds[2] = 0.0f, sp->bounds[3] = 1.0f;
			sp->registration[0] = sp->registration[1] = 0.5f;
			s->sequences[s->sequence_count].first = (short)s->sprite_count++;
			s->sequences[s->sequence_count++].count = 1;
		}
	}
	s->real_sequence_count = s->sequence_count;
	return (short)h2fx.sheet_count++;
}

/* ---------- private code: effects gathered */

/* a string id's text, '_' made ' ' (this engine's marker names) */
H2_ALIGN static void h2fx_marker_name(struct h2_cache *cache, unsigned long id, char *out, long size)
{
	char *p;

	out[0] = 0;
	if (id == 0 || !h2_cache_string(cache, id, out, size))
		out[0] = 0;
	for (p = out; *p; p++)
	{
		if (*p == '_')
			*p = ' ';
	}
}

/* the location index of a marker name in the effect (added), 0 if full */
H2_ALIGN static short h2fx_location(struct h2fx_effect *e, char const *name)
{
	long i;

	for (i = 0; i < e->location_count; i++)
	{
		if (!strcmp(e->locations[i], name))
			return (short)i;
	}
	if (e->location_count >= H2FX_MAXIMUM_LOCATIONS)
		return 0;
	snprintf(e->locations[e->location_count], 32, "%s", name);
	return (short)e->location_count++;
}

/* the shader template's blend (this engine's framebuffer blend function),
NONE: not drawn here */
H2_ALIGN static short h2fx_blend(struct h2_cache *cache, long prt3_address)
{
	long template_tag = h2_cache_reference(cache, (unsigned long)prt3_address + 0xC);
	char const *name = template_tag != NONE ? h2_cache_tag_name(cache, template_tag) : "";

	if (strstr(name, "distortion"))
		return NONE;
	if (strstr(name, "alpha_multiply_add"))
		return 7;
	if (strstr(name, "additive"))
		return 3;
	if (strstr(name, "multiply"))
		return 1;
	return 0;   /* alpha blend (and the glass's alpha test) */
}

/* the particle physics' kind: Halo 2's particle physics by its name (and
its flags: bit 1, it collides - debris) */
H2_ALIGN static short h2fx_physics_kind(struct h2_cache *cache, long pmov)
{
	struct h2_cache_tag t;
	unsigned char const *p;
	char const *name;

	if (pmov == NONE || !h2_cache_tag_get(cache, pmov, &t))
		return _h2fx_physics_drift;
	name = tag_short_name(h2_cache_tag_name(cache, pmov));
	if (strstr(name, "spark") || strstr(name, "leaf") || strstr(name, "snow") || strstr(name, "light_debris"))
		return _h2fx_physics_light;
	if (strstr(name, "fire"))
		return _h2fx_physics_fire;
	if (strstr(name, "hot_smoke") || strstr(name, "engine_smoke"))
		return _h2fx_physics_hot_smoke;
	if (strstr(name, "smoke") || strstr(name, "steam") || strstr(name, "dust") || strstr(name, "cloud"))
		return _h2fx_physics_smoke;
	p = h2_cache_at(cache, t.address + 8, 4);
	if ((p && (read32(p) & 2)) || strstr(name, "stone") || strstr(name, "debris") || strstr(name, "shrapnel") || strstr(name, "clip") ||
		strstr(name, "glass") || strstr(name, "wood") || strstr(name, "rubber") || strstr(name, "water"))
	{
		return _h2fx_physics_debris;
	}
	return _h2fx_physics_drift;
}

/* a sheet's sequence's sprites each a sequence of its own (their first's
index), NONE if the sheet is full */
H2_ALIGN static short h2fx_sheet_split(struct h2fx_sheet *s, short sequence)
{
	long k, count = s->sequences[sequence].count;

	if (s->split[sequence] > 0)
		return s->split[sequence];
	if (s->sequence_count + count > H2FX_MAXIMUM_SHEET_SEQUENCES)
		return NONE;
	s->split[sequence] = (short)s->sequence_count;
	for (k = 0; k < count; k++)
	{
		s->sequences[s->sequence_count].first = (short)(s->sequences[sequence].first + k);
		s->sequences[s->sequence_count++].count = 1;
	}
	return s->split[sequence];
}

/* Halo 2's particle physics' gravity (parameter 0 of its first
movement: 1 falls as the world's, negative rises) and drag (parameter 1,
a second's: at most 6) - none: 0 and 0 */
H2_ALIGN static void h2fx_motion(struct h2_cache *cache, long pmov, float *gravity, float *drag)
{
	struct h2_cache_tag t;
	long count, parameter_count, k;
	unsigned long movements, parameters;

	*gravity = *drag = 0.0f;
	if (pmov == NONE || !h2_cache_tag_get(cache, pmov, &t) || !h2_cache_block(cache, t.address + 0xC, 0x10, &count, &movements) ||
		count < 1 || !h2_cache_block(cache, movements + 4, 0x14, &parameter_count, &parameters))
	{
		return;
	}
	for (k = 0; k < parameter_count && k < 8; k++)
	{
		unsigned long q = parameters + 0x14 * (unsigned long)k;
		long id = (long)h2_cache_read32(cache, q);
		struct h2fx_function f;
		float bounds[2];

		h2fx_function_read(cache, q + 4, &f);
		h2fx_bounds(&f, 0.0f, bounds);
		if (id == 0)
			*gravity = 0.5f * (bounds[0] + bounds[1]);
		else if (id == 1)
			*drag = 0.5f * (bounds[0] + bounds[1]);
	}
	if (*drag < 0.0f)
		*drag = 0.0f;
	if (*drag > 6.0f)
		*drag = 6.0f;
}

/* a particle model's (PRTM) shader's map and blend for an emitter: its
shader (0x18) - its template's blend (an alpha test's alpha blend, an
additive or plasma's add), its first map (its runtime properties' (0xC,
0x50 each) first reference, else its postprocess definition's (0x20, 0x7C
each) first bitmap, 4: 0xC each, the handle first); FALSE if none */
H2_ALIGN static boolean h2fx_mesh_model(struct build *build, struct h2fx_emitter *m, long prtm, struct h2_cache_tag const *p)
{
	struct h2_cache *cache = build->cache;
	long shader = h2_cache_reference(cache, p->address + 0x18), count, i, bitmap = NONE, vertices, indices;
	unsigned long block;
	struct h2_cache_tag st;
	char const *template_name;
	long template_tag;

	if (shader == NONE || !h2_cache_tag_get(cache, shader, &st) ||
		!h2_cache_block(cache, p->address + 0x88, 56, &vertices, &block) || vertices < 3 || vertices > 4096 ||
		!h2_cache_block(cache, p->address + 0x90, 2, &indices, &block) || indices < 3)
	{
		return FALSE;
	}
	template_tag = h2_cache_reference(cache, st.address);
	template_name = template_tag != NONE ? h2_cache_tag_name(cache, template_tag) : "";
	if (strstr(template_name, "distortion"))
		return FALSE;
	m->blend = strstr(template_name, "additive") || strstr(template_name, "plasma") || strstr(template_name, "_add") ? 3 :
		strstr(template_name, "multiply") ? 1 : 0;
	if (h2_cache_block(cache, st.address + 0x0C, 0x50, &count, &block) && count > 0)
		bitmap = h2_cache_reference(cache, block);
	if (bitmap == NONE)
	{
		long definition_count;
		unsigned long definitions;

		if (h2_cache_block(cache, st.address + 0x20, 0x7C, &definition_count, &definitions) && definition_count > 0 &&
			h2_cache_block(cache, definitions + 4, 0xC, &count, &block))
		{
			for (i = 0; i < count && bitmap == NONE; i++)
				bitmap = h2_cache_tag_from_handle(cache, h2_cache_read32(cache, block + 0xC * (unsigned long)i));
		}
	}
	m->sheet = bitmap != NONE ? h2fx_sheet_for(build, bitmap) : NONE;
	if (m->sheet == NONE)
		return FALSE;
	m->mesh = prtm;
	/* (H2EK particle_plasma: noise_map_a, noise_map_b, pixel constants
	color_medium, color_wide, color_sharp; vertex constants the noises'
	scales. MCC shield_recharge: df832f 000000 f7e093, 6 and 3) */
	m->plasma_sheet = NONE;
	if (strstr(template_name, "particle_plasma") && strstr(h2_cache_tag_name(cache, prtm), "shield_recharge") &&
		!getenv("HALO_H2_NO_PARTICLE_PLASMA"))
	{
		long definition_count, pixel_count, vertex_count;
		unsigned long definitions, pixels, vertex_block;

		if (h2_cache_block(cache, st.address + 0x20, 0x7C, &definition_count, &definitions) && definition_count > 0 &&
			h2_cache_block(cache, definitions + 4, 0xC, &count, &block) && count >= 2 &&
			h2_cache_block(cache, definitions + 0xC, 4, &pixel_count, &pixels) && pixel_count >= 3 &&
			h2_cache_block(cache, definitions + 0x14, 0x10, &vertex_count, &vertex_block) && vertex_count >= 2)
		{
			long second = h2_cache_tag_from_handle(cache, h2_cache_read32(cache, block + 0xC));

			if (second != NONE)
				m->plasma_sheet = h2fx_sheet_for(build, second);
			for (i = 0; i < 3; i++)
				m->plasma_colours[i] = h2_cache_read32(cache, pixels + 4 * (unsigned long)i);
			for (i = 0; i < 2; i++)
			{
				unsigned char const *v = h2_cache_at(cache, vertex_block + 0x10 * (unsigned long)i, 4);

				m->plasma_scale[i] = v ? readf(v) : 1.0f;
			}
		}
	}
	return TRUE;
}

/* a particle model's mesh written (once a model) into the tag data: a
header (its models, vertices and indices, short each, and a zero), its
models (index start and count, short each), its vertices (position and
texture coordinates, 5 floats), its indices (shorts, a triangle list) -
h2_particle_styles.h's; its offset in r, 0 none */
H2_ALIGN static unsigned long h2fx_mesh_write(struct h2_cache *cache, struct region *r, long prtm)
{
	struct h2_cache_tag p;
	long model_count, vertex_count, index_count, i;
	unsigned long models, vertices, indices, root, at;

	for (i = 0; i < h2fx.mesh_count; i++)
	{
		if (h2fx.mesh_tags[i] == prtm)
			return h2fx.mesh_roots[i];
	}
	if (!h2_cache_tag_get(cache, prtm, &p) || !h2_cache_block(cache, p.address + 0x80, 8, &model_count, &models) ||
		!h2_cache_block(cache, p.address + 0x88, 56, &vertex_count, &vertices) ||
		!h2_cache_block(cache, p.address + 0x90, 2, &index_count, &indices) ||
		vertex_count < 3 || vertex_count > 4096 || index_count < 3 || index_count > 30000)
	{
		return 0;
	}
	if (model_count < 1 || model_count > 16)
		model_count = 0;
	root = region_alloc(r, 8 + 4 * (unsigned long)(model_count ? model_count : 1) + 20 * (unsigned long)vertex_count +
		2 * (unsigned long)index_count + 4);
	if (r->b.failed)
		return 0;
	write16(region_at(r, root), (unsigned short)(model_count ? model_count : 1));
	write16(region_at(r, root + 2), (unsigned short)vertex_count);
	write16(region_at(r, root + 4), (unsigned short)index_count);
	write16(region_at(r, root + 6), 0);
	at = root + 8;
	if (model_count)
	{
		for (i = 0; i < model_count; i++, at += 4)
		{
			long start = h2fx_read_short(cache, models + 8 * (unsigned long)i + 4);
			long count = h2fx_read_short(cache, models + 8 * (unsigned long)i + 6);

			if (start < 0 || start >= index_count)
				start = 0;
			if (count < 0 || start + count > index_count)
				count = index_count - start;
			write16(region_at(r, at), (unsigned short)start);
			write16(region_at(r, at + 2), (unsigned short)(count - count % 3));
		}
	}
	else
	{
		write16(region_at(r, at), 0);
		write16(region_at(r, at + 2), (unsigned short)(index_count - index_count % 3));
		at += 4;
	}
	for (i = 0; i < vertex_count; i++, at += 20)
	{
		unsigned long v = vertices + 56 * (unsigned long)i;

		writef(region_at(r, at), h2fx_read_real(cache, v));
		writef(region_at(r, at + 4), h2fx_read_real(cache, v + 4));
		writef(region_at(r, at + 8), h2fx_read_real(cache, v + 8));
		writef(region_at(r, at + 12), h2fx_read_real(cache, v + 48));
		writef(region_at(r, at + 16), h2fx_read_real(cache, v + 52));
	}
	for (i = 0; i < index_count; i++, at += 2)
	{
		long index = (unsigned short)h2fx_read_short(cache, indices + 2 * (unsigned long)i);

		write16(region_at(r, at), (unsigned short)(index < vertex_count ? index : 0));
	}
	if (h2fx.mesh_count < (long)NUMBEROF(h2fx.mesh_tags))
	{
		h2fx.mesh_tags[h2fx.mesh_count] = prtm;
		h2fx.mesh_roots[h2fx.mesh_count++] = root;
	}
	return root;
}

/* (tests) whether a name has one of a comma-separated list's in it */
H2_ALIGN static boolean h2fx_name_listed(char const *name, char const *list)
{
	char item[64];

	while (list && *list)
	{
		size_t n = strcspn(list, ",");

		snprintf(item, sizeof(item), "%.*s", (int)(n < sizeof(item) - 1 ? n : sizeof(item) - 1), list);
		if (item[0] && strstr(name, item))
			return TRUE;
		list += n;
		if (*list == ',')
			list++;
	}
	return FALSE;
}

/* an emitter of a particle system made (h2fx.emitters'): FALSE if it is
not drawn here */
/* (an attached particle system's emitter being made: one deep) */
static int h2fx_child_depth;

static int h2fx_emitter_refused;

H2_ALIGN static boolean h2fx_emitter_make(struct build *build, struct h2fx_effect *e, short effect_index, short event_index,
	unsigned long system, unsigned long emitter, float event_duration, boolean looping)
{
	struct h2_cache *cache = build->cache;
	struct h2fx_emitter *m;
	struct h2_cache_tag p;
	long prt3 = h2_cache_reference(cache, system);
	long count, i;
	unsigned long parameters;
	struct h2fx_function rate, life, velocity, spin, size, tint, alpha, radius, angle;
	struct h2fx_function color, p_alpha, p_scale, frame;
	unsigned long prt3_flags;
	short shape;
	float bounds[2];

	if (prt3 == NONE || !h2_cache_tag_get(cache, prt3, &p) ||
		!h2fx_room((void **)&h2fx.emitters, &h2fx.emitter_capacity, h2fx.emitter_count, 64,
			h2fx.tier == H2_PARTICLE_STYLE_TIER_CAPPED ? H2FX_MAXIMUM_EMITTERS_CAPPED : H2FX_MAXIMUM_EMITTERS, sizeof(*h2fx.emitters)))
		return h2fx_emitter_refused = 1, FALSE;
	m = &h2fx.emitters[h2fx.emitter_count];
	memset(m, 0, sizeof(*m));
	m->effect = effect_index;
	m->event = event_index;
	m->prt3 = prt3;
	m->mesh = NONE;
	m->plasma_sheet = NONE;
	m->death_h2 = m->collision_h2 = m->death_target = m->collision_target = NONE;
	m->child = NONE;
	snprintf(m->name, sizeof(m->name), "%s", tag_short_name(h2_cache_tag_name(cache, prt3)));
	/* (HALO_H2_SKIP_EMITTERS=name[,name...]: particles so named left out;
	HALO_H2_ONLY_EMITTERS=name[,name...]: those alone - tests) */
	if (h2fx_name_listed(m->name, HALO_ENV_ONCE("HALO_H2_SKIP_EMITTERS")) ||
		(HALO_ENV_ONCE("HALO_H2_ONLY_EMITTERS") && !h2fx_name_listed(m->name, HALO_ENV_ONCE("HALO_H2_ONLY_EMITTERS"))))
	{
		return h2fx_emitter_refused = 2, FALSE;
	}
	/* (a particle model - PRTM: chunks of concrete and Covenant metal, the
	explosions' blast rings, the shields' recharge shell - its mesh drawn
	for the particle, textured by its shader's map: the PC tier;
	HALO_H2_NO_PARTICLE_MODELS none) */
	if (p.group_tag == 'PRTM')
	{
		if (h2fx.tier == H2_PARTICLE_STYLE_TIER_CAPPED || getenv("HALO_H2_NO_PARTICLE_MODELS") ||
			!h2fx_mesh_model(build, m, prt3, &p))
		{
			h2fx.left_out_models++;
			return h2fx_emitter_refused = 3, FALSE;
		}
	}
	else
	{
		m->blend = h2fx_blend(cache, (long)p.address);
		/* (a distortion particle - the heat haze over plasma, a shock
		wave's ring: the screen behind it drawn again offset by its bump
		map, render's shader flag 13 - the desktop's tier; its distortion
		scale its parameter's) */
		if (m->blend == NONE && h2fx.tier != H2_PARTICLE_STYLE_TIER_CAPPED && !getenv("HALO_H2_NO_DISTORTION") &&
			h2_cache_block(cache, p.address + 0x14, 0x28, &count, &parameters))
		{
			m->distortion = TRUE;
			m->distortion_scale = 5.0f;
			for (i = 0; i < count; i++)
			{
				char parameter[48];

				if (h2_cache_string(cache, h2_cache_read32(cache, parameters + 0x28 * (unsigned long)i), parameter, sizeof(parameter)) &&
					!strcmp(parameter, "distortion_scale"))
				{
					m->distortion_scale = h2fx_read_real(cache, parameters + 0x28 * (unsigned long)i + 0x10);
				}
			}
			m->blend = 0;
			if (getenv("HALO_H2_EFFECTS_DEBUG"))
				fprintf(stderr, "h2 effects: distortion emitter %s of %s event %d, event duration %.2f scale %.2f\n", m->name,
					h2_cache_tag_name(cache, e->sources[0]), (int)event_index, event_duration, m->distortion_scale);
		}
		if (m->blend == NONE)
		{
			h2fx.left_out_distortion++;
			return h2fx_emitter_refused = 4, FALSE;
		}
		/* its bitmap: the shader's first bitmap parameter */
		m->sheet = NONE;
		if (h2_cache_block(cache, p.address + 0x14, 0x28, &count, &parameters))
		{
			for (i = 0; i < count && m->sheet == NONE; i++)
				m->sheet = h2fx_sheet_for(build, h2_cache_reference(cache, parameters + 0x28 * (unsigned long)i + 8));
		}
	}
	if (m->sheet == NONE)
		return h2fx_emitter_refused = 5, FALSE;
	prt3_flags = m->mesh != NONE ? 0 : h2_cache_read32(cache, p.address);
	/* (its collision effect 0x6C and death effect 0x74 - the dirt clumps'
	shrapnel and puff as they die, the blood's splash as it lands -: the
	effects alone, h2fx_particle_effects; sounds and material effects not.
	HALO_H2_NO_PARTICLE_EFFECTS none) */
	if (m->mesh == NONE && !HALO_ENV_ONCE("HALO_H2_NO_PARTICLE_EFFECTS"))
	{
		struct h2_cache_tag et;
		long death = h2_cache_reference(cache, p.address + 0x74), collision = h2_cache_reference(cache, p.address + 0x6C);

		if (death != NONE && h2_cache_tag_get(cache, death, &et) && et.group_tag == 'effe')
			m->death_h2 = death;
		if (collision != NONE && h2_cache_tag_get(cache, collision, &et) && et.group_tag == 'effe')
			m->collision_h2 = collision;
	}
	m->orientation = h2fx_read_short(cache, p.address + 4);
	if (m->orientation < 0 || m->orientation > 2)
		m->orientation = 0;

	h2fx_function_read(cache, emitter + 0x08, &rate);
	h2fx_function_read(cache, emitter + 0x18, &life);
	h2fx_function_read(cache, emitter + 0x28, &velocity);
	h2fx_function_read(cache, emitter + 0x38, &spin);
	h2fx_function_read(cache, emitter + 0x48, &size);
	h2fx_function_read(cache, emitter + 0x58, &tint);
	h2fx_function_read(cache, emitter + 0x68, &alpha);
	h2fx_function_read(cache, emitter + 0x7C, &radius);
	h2fx_function_read(cache, emitter + 0x8C, &angle);
	if (m->mesh == NONE)
	{
		h2fx_function_read(cache, p.address + 0x1C, &color);
		h2fx_function_read(cache, p.address + 0x2C, &p_alpha);
		h2fx_function_read(cache, p.address + 0x3C, &p_scale);
		h2fx_function_read(cache, p.address + 0x5C, &frame);
	}
	else
	{
		/* (a model's own functions - its scale on each axis, its spin -
		are not read: one, and the emitter's) */
		memset(&color, 0, sizeof(color));
		memset(&p_alpha, 0, sizeof(p_alpha));
		memset(&p_scale, 0, sizeof(p_scale));
		memset(&frame, 0, sizeof(frame));
	}
	shape = h2fx_read_short(cache, emitter + 0x78);
	m->shape = shape >= 0 && shape <= 10 ? shape : 0;

	/* its life */
	h2fx_bounds(&life, 1.0f, m->life);
	if (m->life[1] < 0.034f)
		m->life[0] = m->life[1] = 0.034f;
	if (m->life[0] < 0.034f)
		m->life[0] = 0.034f;

	/* how many: Halo 2's rate is particles a tick, for the event's ticks
	(more than an entry holds: more entries; capped, half - HALO_H2_EFFECTS=capped) */
	h2fx_bounds(&rate, 1.0f, bounds);
	/* (a rate of the effect's A or B scale - an attachment's: the sword's
	glow by its turned_on - at its full scale: this engine scales the count
	by A itself where it has the function, else it is always full) */
	if (rate.present && (rate.input == 9 || rate.input == 10))
	{
		float a = h2fx_scalar(&rate, 1.0f, 0.0f, 1.0f), b = h2fx_scalar(&rate, 1.0f, 1.0f, 1.0f);

		bounds[0] = a < b ? a : b;
		bounds[1] = a < b ? b : a;
	}
	{
		float ticks = event_duration * 30.0f, n[2];
		long most = h2fx.tier == H2_PARTICLE_STYLE_TIER_CAPPED ? 2 : H2FX_MAXIMUM_REPEATS;

		if (ticks < 1.0f)
			ticks = 1.0f;
		for (i = 0; i < 2; i++)
		{
			n[i] = bounds[i] * ticks;
			if (h2fx.tier == H2_PARTICLE_STYLE_TIER_CAPPED && n[i] > 1.0f)
				n[i] *= 0.5f;
			if (n[i] < 0.0f)
				n[i] = 0.0f;
		}
		/* (a child - an attached system's, made over one tick: its rate a
		tick kept whole, the fractions of it accumulating in particles.c) */
		if (h2fx_child_depth)
			m->child_rate = 0.5f * (n[0] + n[1]);
		m->repeat = (short)(n[1] > H2FX_ENTRY_COUNT ? (long)(n[1] / H2FX_ENTRY_COUNT) + 1 : 1);
		if (m->repeat > most)
			m->repeat = (short)most;
		for (i = 0; i < 2; i++)
		{
			float each = n[i] / (float)m->repeat;

			m->count[i] = (short)(each > H2FX_ENTRY_COUNT ? H2FX_ENTRY_COUNT : (long)(each + 0.5f));
		}
		if (m->count[1] == 0 && bounds[1] > 0.0f)
			m->count[1] = 1;
		/* (under a particle a tick: its fraction, h2fx_effect_write's chance
		for a looped one) */
		m->chance = n[1] < 1.0f ? 0.5f * (n[0] + n[1]) : 0.0f;
		m->distribution = ticks > 1.5f ? 2 : 0;   /* constant over the event, else all at its start */
	}
	(void)looping;
	if (m->count[1] == 0)
		return h2fx_emitter_refused = 6, FALSE;

	/* where and how fast */
	h2fx_bounds(&radius, 0.0f, m->emission_radius);
	h2fx_bounds(&velocity, 0.0f, m->velocity);
	/* (one turned to its direction - parallel or perpendicular to it - and
	still: this engine's direction is its velocity, none drew it on edge
	(the plasma rifle's first-person flame along the shot missing): a
	crawl along its emission's way. HALO_H2_STILL_DIRECTIONS: as before) */
	if (m->orientation != 0 && m->velocity[1] < 0.001f && !getenv("HALO_H2_STILL_DIRECTIONS"))
		m->velocity[0] = m->velocity[1] = 0.001f;
	h2fx_bounds(&spin, 0.0f, m->angular_velocity);
	m->angular_velocity[0] *= 3.14159265f / 180.0f;
	m->angular_velocity[1] *= 3.14159265f / 180.0f;
	/* (one that does not spin - Halo 2's prt3 flag 0 - keeps its sprite
	upright: the dirt bursts, authored vertically (flag 13) as a spray
	rising from a flat foot, drawn turned any way showed that foot as a
	straight hard edge at any angle - the rocket's and grenades' "dust
	cards". HALO_H2_SPIN_ALL: every one turned any way, as before) */
	m->spins = m->mesh != NONE || (prt3_flags & 1) || getenv("HALO_H2_SPIN_ALL");
	if (!m->spins)
		m->angular_velocity[0] = m->angular_velocity[1] = 0.0f;
	if (m->mesh == NONE && (prt3_flags & (1UL << 13)) && m->orientation != 0)
		m->style.flags |= H2_PARTICLE_STYLE_VERTICAL;
	h2fx_bounds(&angle, 0.0f, bounds);
	m->cone = bounds[1] * 3.14159265f / 180.0f;
	/* (a globe, implosion, halo or debris: every way) */
	if ((shape == 2 || shape == 3 || shape == 5 || shape == 8) && m->cone <= 0.0f)
		m->cone = 3.14159265f;
	if (m->cone > 3.14159265f)
		m->cone = 3.14159265f;
	for (i = 0; i < 3; i++)
		m->offset[i] = h2fx_read_real(cache, emitter + 0x9C + 4 * (unsigned long)i);
	m->direction[0] = h2fx_read_real(cache, emitter + 0xA8);
	m->direction[1] = h2fx_read_real(cache, emitter + 0xAC);

	/* its size, alpha and colour: over its life sampled (the emitter's and
	the particle's functions of its age), the rest its bounds */
	{
		float peak = 0.0f, start_rgb[3], k;
		long s;

		for (s = 0; s < H2_PARTICLE_STYLE_SAMPLES; s++)
		{
			float t = (float)s / (float)(H2_PARTICLE_STYLE_SAMPLES - 1);
			float sz = (h2fx_over_life(&size) ? h2fx_scalar(&size, t, 0.5f, 1.0f) : 1.0f) *
				(p_scale.present && (p_scale.input == 0) ? h2fx_scalar(&p_scale, t, 0.5f, 1.0f) : 1.0f);
			float al = (h2fx_over_life(&alpha) ? h2fx_scalar(&alpha, t, 0.5f, 1.0f) : 1.0f) *
				(p_alpha.present && p_alpha.input == 0 ? h2fx_scalar(&p_alpha, t, 0.5f, 1.0f) : 1.0f);
			float rgb[3], prgb[3];

			if (h2fx_over_life(&tint) && h2fx_function_is_color(&tint))
				h2fx_rgb(&tint, t, 0.5f, rgb);
			else
				rgb[0] = rgb[1] = rgb[2] = 1.0f;
			if (color.present && color.input == 0)
				h2fx_rgb(&color, t, 0.5f, prgb);
			else
				prgb[0] = prgb[1] = prgb[2] = 1.0f;
			if (sz < 0.0f)
				sz = 0.0f;
			m->style.scale[s] = sz;
			m->style.alpha[s] = h2fx_clamp01(al);
			for (i = 0; i < 3; i++)
				m->style.color[s][i] = h2fx_clamp01(rgb[i] * prgb[i]);
			if (sz > peak)
				peak = sz;
		}
		/* (the scale's samples as fractions of its peak; the peak the
		entry's radius) */
		for (s = 0; s < H2_PARTICLE_STYLE_SAMPLES; s++)
			m->style.scale[s] = peak > 0.0f ? m->style.scale[s] / peak : 0.0f;
		/* (the colour's start the tint; the samples relative to it) */
		for (i = 0; i < 3; i++)
			start_rgb[i] = 1.0f;
		(void)start_rgb;
		/* its size: Halo 2's world units, its half width (this engine's
		radius: the burning Warthog's flames and the muzzle flashes against
		the owner's MCC frames) */
		if (h2fx_over_life(&size))
			bounds[0] = bounds[1] = 1.0f;
		else
			h2fx_bounds(&size, 1.0f, bounds);
		k = peak * h2_particle_style_size_scale();
		m->radius[0] = bounds[0] * k;
		m->radius[1] = bounds[1] * k;
		if (!(p_scale.present && p_scale.input == 0) && p_scale.present)
		{
			float ps[2];

			h2fx_bounds(&p_scale, 1.0f, ps);
			m->radius[0] *= ps[0];
			m->radius[1] *= ps[1];
		}
		if (m->radius[1] <= 0.0f)
			return h2fx_emitter_refused = 7, FALSE;
	}
	/* its tint: the emitter's tint and alpha at birth (randoms); the
	particle's colour when it is not of its age */
	{
		float rgb0[3], rgb1[3], al[2];

		if (tint.present && h2fx_function_is_color(&tint) && !h2fx_over_life(&tint))
		{
			h2fx_rgb(&tint, 0.0f, 0.0f, rgb0);
			h2fx_rgb(&tint, 1.0f, 1.0f, rgb1);
		}
		else
			rgb0[0] = rgb0[1] = rgb0[2] = rgb1[0] = rgb1[1] = rgb1[2] = 1.0f;
		if (color.present && color.input != 0)
		{
			float c0[3], c1[3];

			h2fx_rgb(&color, 0.0f, 0.0f, c0);
			h2fx_rgb(&color, 1.0f, 1.0f, c1);
			for (i = 0; i < 3; i++)
				rgb0[i] *= c0[i], rgb1[i] *= c1[i];
		}
		if (h2fx_over_life(&alpha))
			al[0] = al[1] = 1.0f;
		else
			h2fx_bounds(&alpha, 1.0f, al);
		if (p_alpha.present && p_alpha.input != 0)
		{
			float pa[2];

			h2fx_bounds(&p_alpha, 1.0f, pa);
			al[0] *= pa[0], al[1] *= pa[1];
		}
		m->tint[0][0] = h2fx_clamp01(al[0]);
		m->tint[1][0] = h2fx_clamp01(al[1]);
		for (i = 0; i < 3; i++)
		{
			m->tint[0][1 + i] = h2fx_clamp01(rgb0[i]);
			m->tint[1][1 + i] = h2fx_clamp01(rgb1[i]);
		}
	}

	/* its sprites: the sheet's sequences from the first, as many as
	Halo 2's count (0: all); a frame function of its age plays the
	sequence once over its life, else a random frame stays */
	{
		struct h2fx_sheet const *sh = &h2fx.sheets[m->sheet];
		short first = h2fx_read_short(cache, p.address + 8), n = h2fx_read_short(cache, p.address + 0xA);
		long frames;

		if (first < 0 || first >= sh->real_sequence_count)
			first = 0;
		if (n <= 0 || first + n > sh->real_sequence_count)
			n = (short)(sh->real_sequence_count - first);
		m->first_sequence = first;
		m->sequence_count = n;
		frames = sh->sequences[first].count;
		if (frames > 1 && frame.present && frame.type != 1 && (frame.input == 0 || frame.input == 1))
			m->frames_per_second = (float)frames / (0.5f * (m->life[0] + m->life[1]));
		else if (frames > 1)
		{
			/* (a random frame: a random one of its sprites made sequences
			- this engine's random first frame left none drawn with these
			sheets) */
			short split = h2fx_sheet_split(&h2fx.sheets[m->sheet], first);

			if (split != NONE)
			{
				m->first_sequence = split;
				m->sequence_count = (short)frames;
			}
			else
				m->random_frame = TRUE;
		}
	}

	/* this engine's particle flags from Halo 2's (prt3 flags, as the kit's
	tags name them: 0 spins, 1 and 2 random u and v mirror, 3 frame
	animation one shot, 4 select random sequence, 5 disable frame blending,
	6 can animate backwards, 7 receive lightmap lighting, 8 tint from
	diffuse texture, 9 dies at rest, 10 dies on structure collision, 11
	dies in media, 12 dies in air): self-illuminated unless it takes the
	lightmap's light; the mirrors; its deaths (at rest 4, on structure 5,
	in water 7, in air 8); animating backwards (0). (Bit 6, animating
	backwards, was read as the lightmap's light: the casings, metal chips
	and shrapnel sparks lit black.) Its colour of the ground's texture under
	it (this engine's tint from diffuse, bit 6) only with
	HALO_H2_DIFFUSE_TINT: a Halo 2 level's base maps read black here
	(light_particle's sample) - the dirt clumps black */
	m->flags = 1UL << 9;
	if (prt3_flags & (1UL << 7))
		m->flags = 0;
	if (prt3_flags & (1UL << 1))
		m->flags |= 1UL << 10;
	if (prt3_flags & (1UL << 2))
		m->flags |= 1UL << 11;
	if (prt3_flags & (1UL << 6))
		m->flags |= 1UL << 0;
	if ((prt3_flags & (1UL << 8)) && getenv("HALO_H2_DIFFUSE_TINT"))
		m->flags |= 1UL << 6;
	if (prt3_flags & (1UL << 9))
		m->flags |= 1UL << 4;
	if (prt3_flags & (1UL << 10))
		m->flags |= 1UL << 5;
	if (prt3_flags & (1UL << 11))
		m->flags |= 1UL << 7;
	if (prt3_flags & (1UL << 12))
		m->flags |= 1UL << 8;
	if (m->random_frame)
		m->flags |= 1UL << 2;
	m->physics = h2fx_physics_kind(cache, h2_cache_reference(cache, emitter));
	/* (one that does not collide moves by Halo 2's own gravity and drag -
	its particle physics' first movement's parameters 0 and 1 -, this
	engine's point physics none) */
	if (m->physics != _h2fx_physics_debris && m->physics != _h2fx_physics_light)
	{
		h2fx_motion(cache, h2_cache_reference(cache, emitter), &m->style.gravity, &m->style.drag);
		m->physics = _h2fx_physics_drift;
		m->style.flags |= H2_PARTICLE_STYLE_MOTION;
	}

	/* the entry: its location, camera mode, attached when local */
	{
		short location = h2fx_read_short(cache, system + 8);
		short coordinates = h2fx_read_short(cache, system + 0xC);

		m->location = location >= 0 && location < e->location_count ? location : 0;
		m->environment = h2fx_read_short(cache, system + 0xE);
		m->disposition = h2fx_read_short(cache, system + 0x10);
		m->camera_mode = h2fx_read_short(cache, system + 0x12);
		/* (its sort bias, 0x14 - Halo 2's "-10 to 10, nearer and
		farther": an explosion's fire -10, its smoke 4 and 6 - drawn
		over its own smoke, not under it; render_particles.c) */
		m->style.sort_bias = (float)h2fx_read_short(cache, system + 0x14);
		if (m->style.sort_bias < -100.0f || m->style.sort_bias > 100.0f)
			m->style.sort_bias = 0.0f;
		if (m->environment < 0 || m->environment > 3)
			m->environment = 0;
		if (m->disposition < 0 || m->disposition > 2)
			m->disposition = 0;
		if (m->camera_mode < 0 || m->camera_mode > 3)
			m->camera_mode = 0;
		m->attached = coordinates == 1 && !h2fx_world_particles;
		if (HALO_ENV_ONCE("HALO_H2_SKIP_CAMERA") && m->camera_mode == atoi(HALO_ENV_ONCE("HALO_H2_SKIP_CAMERA")))
			m->count[0] = m->count[1] = 0;
	}
	m->style.flags |= H2_PARTICLE_STYLE_SCALE | H2_PARTICLE_STYLE_ALPHA | H2_PARTICLE_STYLE_COLOR;
	/* (an additive tint's - its template's modulation_factor 0, the
	default: white stays white - its colour toward white: HALO_H2_TINT_WHITE,
	0.5; 0 none) */
	if (m->mesh == NONE)
	{
		long template_tag = h2_cache_reference(cache, p.address + 0xC);

		if (template_tag != NONE && strstr(h2_cache_tag_name(cache, template_tag), "particle_additive_tint"))
		{
			float w = getenv("HALO_H2_TINT_WHITE") ? (float)atof(getenv("HALO_H2_TINT_WHITE")) : 0.5f;

			/* (the pass itself, per texel - the renderer's combiners; the
			colour drawn toward white instead, HALO_H2_TINT_WHITE, an
			approximation kept for tests) */
			if (getenv("HALO_H2_TINT_WHITE") && w > 0.0f)
			{
				m->style.flags |= H2_PARTICLE_STYLE_TINT;
				m->style.tint_white = w > 1.0f ? 1.0f : w;
			}
			else if (!getenv("HALO_H2_NO_TINT_PASS"))
				m->tint_white_pass = TRUE;
		}
	}
	/* (HALO_H2_EFFECTS_DUMP=name: the emitters of the effects so named, as made) */
	if (HALO_ENV_ONCE("HALO_H2_EFFECTS_DUMP") && strstr(h2_cache_tag_name(cache, e->sources[0]), HALO_ENV_ONCE("HALO_H2_EFFECTS_DUMP")))
	{
		fprintf(stderr, "h2 effects: %s event %d: %s blend %d orient %d seq %d+%d fps %.1f%s life %.2f-%.2f count %d-%d dist %d "
			"radius %.3f-%.3f velocity %.2f-%.2f cone %.2f emission %.2f-%.2f tint a%.2f/%.2f rgb %.2f %.2f %.2f / %.2f %.2f %.2f "
			"attached %d camera %d physics %d direction %.1f %.1f offset %.2f %.2f %.2f sort %.0f\n", h2_cache_tag_name(cache, e->sources[0]), event_index, m->name, m->blend,
			m->orientation, m->first_sequence, m->sequence_count, m->frames_per_second, m->random_frame ? " random" : "", m->life[0],
			m->life[1], m->count[0], m->count[1], m->distribution, m->radius[0], m->radius[1], m->velocity[0], m->velocity[1], m->cone,
			m->emission_radius[0], m->emission_radius[1], m->tint[0][0], m->tint[1][0], m->tint[0][1], m->tint[0][2], m->tint[0][3],
			m->tint[1][1], m->tint[1][2], m->tint[1][3], m->attached, m->camera_mode, m->physics, m->direction[0] * 57.29578f,
			m->direction[1] * 57.29578f, m->offset[0], m->offset[1], m->offset[2], m->style.sort_bias);
		fprintf(stderr, "    size: present %d input %d range %d type %d flags %x min %.3f max %.3f; particle scale: present %d input %d range %d type %d "
			"min %.3f max %.3f; alpha: present %d input %d type %d min %.3f max %.3f; particle alpha present %d input %d type %d min %.3f max %.3f\n",
			size.present, size.input, size.range, size.type, size.flags, size.minimum, size.maximum, p_scale.present, p_scale.input,
			p_scale.range, p_scale.type, p_scale.minimum, p_scale.maximum, alpha.present, alpha.input, alpha.type, alpha.minimum, alpha.maximum,
			p_alpha.present, p_alpha.input, p_alpha.type, p_alpha.minimum, p_alpha.maximum);
		for (i = 0; i < H2_PARTICLE_STYLE_SAMPLES; i++)
			fprintf(stderr, "    %ld: scale %.2f alpha %.2f rgb %.2f %.2f %.2f\n", i, m->style.scale[i], m->style.alpha[i],
				m->style.color[i][0], m->style.color[i][1], m->style.color[i][2]);
	}
	h2fx.emitter_count++;
	/* (its attached particle systems, 0x84 - the prt3's own emitters, as
	an effect's: the rocket's fallout sparks trailing flint, debris'
	smoke -: the first system's first emitter made a particle tag with no
	entry in any event (event NONE), kept in this particle's tag for
	particles.c to emit from each particle alive. One deep: a child's own
	are not made. HALO_H2_NO_ATTACHED_SYSTEMS: none, as before) */
	if (m->mesh == NONE && !h2fx_child_depth && !HALO_ENV_ONCE("HALO_H2_NO_ATTACHED_SYSTEMS"))
	{
		long self = h2fx.emitter_count - 1, systems_count;
		unsigned long systems;

		if (h2_cache_block(cache, p.address + 0x84, H2FX_H2_SYSTEM_SIZE, &systems_count, &systems) && systems_count > 0)
		{
			long n, made = 0;

			for (n = 0; n < systems_count && !made; n++)
			{
				unsigned long child_system = systems + H2FX_H2_SYSTEM_SIZE * (unsigned long)n, child_emitters;
				long child_emitter_count, j;
				unsigned long child_flags = h2_cache_read32(cache, child_system + 0x16) & 0xFFFF;

				/* (one disabled for debugging, bit 3, or not rendered, bit
				5, as Halo 2 leaves it: the rocket's fallout sparks' flint
				is disabled in the MCC data - the multiplayer maps' only
				attached system; Outskirts' flak bursts and the Wraith
				mortar's spill have theirs on) */
				if ((child_flags & ((1UL << 3) | (1UL << 5))) ||
					!h2_cache_block(cache, child_system + 0x30, H2FX_H2_EMITTER_SIZE, &child_emitter_count, &child_emitters))
				{
					continue;
				}
				for (j = 0; j < child_emitter_count && !made; j++)
				{
					long child_index = h2fx.emitter_count;

					h2fx_child_depth++;
					if (h2fx_emitter_make(build, e, effect_index, NONE, child_system,
						child_emitters + H2FX_H2_EMITTER_SIZE * (unsigned long)j, 1.0f / 30.0f, FALSE))
					{
						h2fx.emitters[self].child = child_index;
						made = 1;
					}
					h2fx_child_depth--;
					if (getenv("HALO_H2_DEBUG"))
						fprintf(stderr, "h2 effects: %s: attached system %ld emitter %ld: %s\n", m->name, n, j, made ? "made" : "not made");
				}
				if (systems_count > 1 || child_emitter_count > 1)
					h2fx.left_out_children += (systems_count - 1) + (child_emitter_count - 1);
			}
			if (made)
				h2fx.child_count++;
		}
	}
	return TRUE;
}

/* whether an event's emitters (or their particles) change with the
emitter's age (an input or range 4) */
H2_ALIGN static boolean h2fx_event_ages(struct h2_cache *cache, unsigned long systems, long system_count)
{
	static unsigned long const emitter_fields[] = { 0x08, 0x18, 0x28, 0x38, 0x48, 0x58, 0x68, 0x7C, 0x8C };
	static unsigned long const particle_fields[] = { 0x1C, 0x2C, 0x3C, 0x5C };
	long n, j, f;

	for (n = 0; n < system_count; n++)
	{
		unsigned long system = systems + H2FX_H2_SYSTEM_SIZE * (unsigned long)n, emitters;
		long emitter_count;
		struct h2_cache_tag p;
		long prt3 = h2_cache_reference(cache, system);

		if (prt3 != NONE && h2_cache_tag_get(cache, prt3, &p))
		{
			for (f = 0; f < (long)(sizeof(particle_fields) / sizeof(particle_fields[0])); f++)
			{
				if (h2fx_read_short(cache, p.address + particle_fields[f]) == 4 || h2fx_read_short(cache, p.address + particle_fields[f] + 2) == 4)
					return TRUE;
			}
		}
		if (!h2_cache_block(cache, system + 0x30, H2FX_H2_EMITTER_SIZE, &emitter_count, &emitters))
			continue;
		for (j = 0; j < emitter_count; j++)
		{
			for (f = 0; f < (long)(sizeof(emitter_fields) / sizeof(emitter_fields[0])); f++)
			{
				unsigned long a = emitters + H2FX_H2_EMITTER_SIZE * (unsigned long)j + emitter_fields[f];

				if (h2fx_read_short(cache, a) == 4 || h2fx_read_short(cache, a + 2) == 4)
					return TRUE;
			}
		}
	}
	return FALSE;
}

/* ---------- private code: lights, lens flares, light volumes, decals */

/* a Halo 2 lens flare made (h2fx.lenses'), NONE if it cannot be */
H2_ALIGN static short h2fx_lens_for(struct build *build, long tag)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	struct h2fx_lens *l;
	long count, i;
	unsigned long reflections;
	short sheet;

	for (i = 0; i < h2fx.lens_count; i++)
	{
		if (h2fx.lenses[i].h2_tag == tag)
			return (short)i;
	}
	if (tag == NONE || getenv("HALO_H2_NO_LENS_FLARES") || !h2_cache_tag_get(cache, tag, &t) || t.group_tag != 'lens' ||
		!h2_cache_block(cache, t.address + 0x40, 0x30, &count, &reflections) || count < 1 ||
		(sheet = h2fx_sheet_for(build, h2_cache_reference(cache, t.address + 0x20))) == NONE ||
		!h2fx_room((void **)&h2fx.lenses, &h2fx.lens_capacity, h2fx.lens_count, 8, 64, sizeof(*h2fx.lenses)))
	{
		return NONE;
	}
	l = &h2fx.lenses[h2fx.lens_count];
	memset(l, 0, sizeof(*l));
	l->h2_tag = tag;
	snprintf(l->name, sizeof(l->name), "%s", tag_short_name(h2_cache_tag_name(cache, tag)));
	l->sheet = sheet;
	l->occlusion_radius = h2fx_read_real(cache, t.address + 0x10);
	l->near_fade = h2fx_read_real(cache, t.address + 0x18);
	l->far_fade = h2fx_read_real(cache, t.address + 0x1C);
	l->third_person_only = (h2fx_read_short(cache, t.address + 0x28) & 8) != 0;
	l->sun = (h2fx_read_short(cache, t.address + 0x28) & 1) != 0;
	l->rotation_function = h2fx_read_short(cache, t.address + 0x2C);
	if (l->rotation_function < 0 || l->rotation_function > 4)
		l->rotation_function = 0;
	l->rotation_scale = h2fx_read_real(cache, t.address + 0x30);
	if (!(l->rotation_scale > -1000.0f && l->rotation_scale < 1000.0f))
		l->rotation_scale = 0.0f;
	l->corona_scale[0] = h2fx_read_real(cache, t.address + 0x34);
	l->corona_scale[1] = h2fx_read_real(cache, t.address + 0x38);
	if (l->corona_scale[0] <= 0.0f || l->corona_scale[1] <= 0.0f)
		l->corona_scale[0] = l->corona_scale[1] = 1.0f;
	if (getenv("HALO_H2_DEBUG"))
	{
		fprintf(stderr, "h2 effects: lens flare %s: %ld reflections, flags %04x (sun %d), rotation function %d scale %.3f, occlusion %.1f, fade %.1f..%.1f\n",
			h2_cache_tag_name(cache, tag), count, h2fx_read_short(cache, t.address + 0x28) & 0xFFFF, l->sun, l->rotation_function,
			l->rotation_scale, l->occlusion_radius, l->near_fade, l->far_fade);
	}
	for (i = 0; i < count && l->reflection_count < (long)(sizeof(l->reflections) / sizeof(l->reflections[0])); i++)
	{
		unsigned long q = reflections + 0x30 * (unsigned long)i;
		struct h2fx_reflection *o = &l->reflections[l->reflection_count];
		short image = h2fx_read_short(cache, q + 4);
		int k;

		o->flags = (unsigned short)(h2fx_read_short(cache, q) & 0xF);
		o->sequence = image >= 0 && image < h2fx.sheets[sheet].real_sequence_count ? image : 0;
		o->position = h2fx_read_real(cache, q + 8);
		o->rotation = h2fx_read_real(cache, q + 0xC);
		o->radius[0] = h2fx_read_real(cache, q + 0x10);
		o->radius[1] = h2fx_read_real(cache, q + 0x14);
		o->brightness[0] = h2fx_clamp01(h2fx_read_real(cache, q + 0x18));
		o->brightness[1] = h2fx_clamp01(h2fx_read_real(cache, q + 0x1C));
		for (k = 0; k < 3; k++)
			o->color[k] = h2fx_clamp01(h2fx_read_real(cache, q + 0x24 + 4 * (unsigned long)k));
		if (o->radius[1] <= 0.0f || o->brightness[1] <= 0.0f)
			continue;
		if (getenv("HALO_H2_DEBUG"))
		{
			fprintf(stderr, "h2 effects:   reflection %ld: flags %x image %d position %.2f radius %.3f..%.3f brightness %.2f..%.2f colour %.2f %.2f %.2f\n",
				i, o->flags, image, o->position, o->radius[0], o->radius[1], o->brightness[0], o->brightness[1], o->color[0], o->color[1], o->color[2]);
		}
		l->reflection_count++;
	}
	if (l->reflection_count == 0)
		return NONE;
	return (short)h2fx.lens_count++;
}

/* the scenario's skies' lights with a lens flare (the sun) gathered
(h2fx.sky_lights): Halo 2's sky tag lights block at 0x78. HALO_H2_NO_SUN:
none. Source: the H2EK's sky definition (lights: direction vector, direction,
lens flare, radiosity), the kit's sun_* lens flares (flag "sun"); drawn as
this engine draws a sky light's lens flare - render_sky.c, at 1023.875 along
the direction */
H2_ALIGN static void h2fx_sky_lights_gather(struct build *build)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag scenario, sky;
	long count, i;
	unsigned long skies;

	h2fx.sky_light_count = 0;
	if (getenv("HALO_H2_NO_SUN") || !h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) ||
		!h2_cache_block(cache, scenario.address + 0x08, 8, &count, &skies))
	{
		return;
	}
	for (i = 0; i < count && h2fx.sky_light_count < (long)(sizeof(h2fx.sky_lights) / sizeof(h2fx.sky_lights[0])); i++)
	{
		long tag = h2_cache_reference(cache, skies + 8UL * (unsigned long)i), light_count, k;
		unsigned long lights;

		if (tag == NONE || !h2_cache_tag_get(cache, tag, &sky) || sky.group_tag != 'sky ' ||
			!h2_cache_block(cache, sky.address + 0x78, 0x24, &light_count, &lights))
		{
			continue;
		}
		for (k = 0; k < light_count; k++)
		{
			unsigned long light = lights + 0x24 * (unsigned long)k;
			long lens_tag = h2_cache_reference(cache, light + 0x14);
			float yaw = h2fx_read_real(cache, light + 0xC), pitch = h2fx_read_real(cache, light + 0x10);
			short lens;

			if (getenv("HALO_H2_DEBUG"))
			{
				fprintf(stderr, "h2 effects: sky %ld (%s) light %ld: direction %.1f %.1f degrees, lens flare %s\n", i, h2_cache_tag_name(cache, tag), k,
					yaw * 57.29578f, pitch * 57.29578f, lens_tag != NONE ? h2_cache_tag_name(cache, lens_tag) : "none");
			}
			if (lens_tag == NONE || !(yaw > -7.0f && yaw < 7.0f && pitch > -7.0f && pitch < 7.0f))
				continue;
			lens = h2fx_lens_for(build, lens_tag);
			if (lens == NONE)
				continue;
			h2fx.sky_lights[h2fx.sky_light_count].scenario_sky = (short)i;
			h2fx.sky_lights[h2fx.sky_light_count].lens = lens;
			h2fx.sky_lights[h2fx.sky_light_count].direction[0] = yaw;
			h2fx.sky_lights[h2fx.sky_light_count].direction[1] = pitch;
			h2fx.sky_light_count++;
			platform_log("h2 effects: sky %ld's sun: %s at yaw %.0f pitch %.0f, %ld reflections", i, h2fx.lenses[lens].name, yaw * 57.29578f,
				pitch * 57.29578f, h2fx.lenses[lens].reflection_count);
			break;
		}
	}
}

/* (h2_level.c, between h2_effects_gather and h2_effects_write) a scenario
sky's sun: its lens flare tag's index among the effects' tags (fx_first +
it) and its direction (yaw, pitch); FALSE when the sky has none */
H2_ALIGN boolean h2_effects_sky_light(long scenario_sky, long *lens_tag_index, float direction[2])
{
	long i;

	for (i = 0; i < h2fx.sky_light_count; i++)
	{
		struct h2fx_sky_light const *s = &h2fx.sky_lights[i];

		if (s->scenario_sky == scenario_sky && h2fx.enabled)
		{
			*lens_tag_index = h2fx.target_count + h2fx.emitter_count + h2fx.contrail_count + h2fx.light_count + s->lens;
			direction[0] = s->direction[0];
			direction[1] = s->direction[1];
			return TRUE;
		}
	}
	return FALSE;
}

/* a Halo 2 light made (h2fx.lights'), NONE if it cannot be; lens_only: a
lens flare tag (an effect's lens part) made a light of it alone */
H2_ALIGN static short h2fx_light_for(struct build *build, long tag)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	struct h2fx_light *o;
	long i;
	float most;
	short lens;
	boolean lens_only;

	for (i = 0; i < h2fx.light_count; i++)
	{
		if (h2fx.lights[i].h2_tag == tag)
			return (short)i;
	}
	if (tag == NONE || getenv("HALO_H2_NO_LIGHTS") || !h2_cache_tag_get(cache, tag, &t) || (t.group_tag != 'ligh' && t.group_tag != 'lens'))
		return NONE;
	lens_only = t.group_tag == 'lens';
	lens = h2fx_lens_for(build, lens_only ? tag : h2_cache_reference(cache, t.address + 0x90));
	if (lens_only && lens == NONE)
		return NONE;
	if (!h2fx_room((void **)&h2fx.lights, &h2fx.light_capacity, h2fx.light_count, 16, 256, sizeof(*h2fx.lights)))
		return NONE;
	o = &h2fx.lights[h2fx.light_count];
	memset(o, 0, sizeof(*o));
	o->h2_tag = tag;
	snprintf(o->name, sizeof(o->name), "%s", tag_short_name(h2_cache_tag_name(cache, tag)));
	o->lens = lens;
	o->lens_only = lens_only;
	if (lens_only)
	{
		/* (a lens flare alone: its reflections' colour, a quarter second) */
		memcpy(o->color, h2fx.lenses[lens].reflections[0].color, sizeof(o->color));
		o->strength = 1.0f;
		o->duration = 0.25f;
		o->radius = 0.0f;
	}
	else
	{
		float brightness = h2fx_read_real(cache, t.address + 0x74);
		int k;

		if (brightness < h2fx_read_real(cache, t.address + 0x70))
			brightness = h2fx_read_real(cache, t.address + 0x70);
		for (k = 0; k < 3; k++)
			o->color[k] = h2fx_read_real(cache, t.address + 0x64 + 4 * (unsigned long)k) * brightness;
		o->radius = h2fx_read_real(cache, t.address + 0x18);
		o->brightness = brightness;
		/* (Halo 2's brightness animation: 0xC4, a block of functions, the
		first its own; 0xC0 bit 0 "synchronized" - read off the MCC data:
		the fades 0xB8..0xBC, the colour and gel animations 0xCC, 0xD4) */
		{
			long animation_count;
			unsigned long animations;

			if (!getenv("HALO_H2_LIGHT_ANIMATION_OLD") && h2_cache_block(cache, t.address + 0xC4, 8, &animation_count, &animations) &&
				animation_count >= 1)
			{
				struct h2fx_function animation;

				h2fx_function_data(cache, animations, &animation);
				o->animation = animation.present;
				o->animation_type = animation.type;
				o->animation_flags = animation.flags;
				o->animation_shape[0] = animation.shape[0];
				o->animation_shape[1] = animation.shape[1];
				o->animation_range[0] = animation.minimum;
				o->animation_range[1] = animation.maximum;
				o->animation_count = animation.data_count;
				for (k = 0; k < 8; k++)
					o->animation_data[k] = k < animation.data_count ? animation.data[k] : 0.0f;
				if (getenv("HALO_H2_DEBUG") || getenv("HALO_H2_LIGHT_ANIMATION_DEBUG"))
					platform_log("h2 effects: light %s: brightness animation type %d flags %02x shapes %d %d range %.2f %.2f data %.2f %.2f %.2f %.2f (%ld floats), synchronized %d",
						h2_cache_tag_name(cache, tag), animation.type, animation.flags, animation.shape[0], animation.shape[1],
						animation.minimum, animation.maximum, animation.data[0], animation.data[1], animation.data[2],
						animation.data[3], animation.data_count, (int)(h2fx_read_short(cache, t.address + 0xC0) & 1));
			}
		}
		o->duration = h2fx_read_real(cache, t.address + 0xB0);
		o->falloff = h2fx_read_short(cache, t.address + 0xB4);
		if (o->falloff < 0 || o->falloff > 5)
			o->falloff = 0;
	}
	most = o->color[0] > o->color[1] ? o->color[0] : o->color[1];
	most = most > o->color[2] ? most : o->color[2];
	if (!lens_only)
		o->strength = most;
	for (i = 0; i < 3; i++)
		o->color[i] = most > 1.0f ? o->color[i] / most : o->color[i] < 0.0f ? 0.0f : o->color[i];
	if (getenv("HALO_H2_DEBUG"))
		fprintf(stderr, "h2 effects: light %s: radius %.2f colour %.2f %.2f %.2f strength %.2f duration %.2f falloff %d lens %d\n",
			h2_cache_tag_name(cache, tag), o->radius, o->color[0], o->color[1], o->color[2], o->strength, o->duration, o->falloff, lens);
	return (short)h2fx.light_count++;
}

/* a Halo 2 light volume's volumes made (h2fx.volumes', the first and how
many: count), NONE if none can be; marker: its attachment's */
H2_ALIGN static short h2fx_volumes_for(struct build *build, long tag, char const *marker, long *made)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	long count, i, first = h2fx.volume_count;
	unsigned long volumes;

	*made = 0;
	for (i = 0; i < h2fx.volume_count; i++)
	{
		if (h2fx.volumes[i].h2_tag == tag && !strcmp(h2fx.volumes[i].marker, marker))
		{
			long n;

			for (n = i; n < h2fx.volume_count && h2fx.volumes[n].h2_tag == tag && !strcmp(h2fx.volumes[n].marker, marker); n++)
				;
			*made = n - i;
			return (short)i;
		}
	}
	if (tag == NONE || getenv("HALO_H2_NO_LIGHT_VOLUMES") || !h2_cache_tag_get(cache, tag, &t) || t.group_tag != 'MGS2' ||
		!h2_cache_block(cache, t.address + 8, 0x98, &count, &volumes))
	{
		if (getenv("HALO_H2_EFFECTS_DEBUG"))
			platform_log("h2 effects: light volume tag %ld (%s): not read (group %08lx)", tag, tag != NONE ? h2_cache_tag_name(cache, tag) : "none",
				tag != NONE && h2_cache_tag_get(cache, tag, &t) ? (unsigned long)t.group_tag : 0ul);
		return NONE;
	}
	for (i = 0; i < count && i < 4; i++)
	{
		unsigned long v = volumes + 0x98 * (unsigned long)i;
		struct h2fx_function offset, radius, brightness, color;
		struct h2fx_volume *o;
		short sheet = h2fx_sheet_for(build, h2_cache_reference(cache, v + 4));
		long sprites = (long)h2_cache_read32(cache, v + 0xC);
		int end, k;

		/* (no bitmap: Halo 2's default glow - the needle's, the plasma
		bolt's first volume) */
		if (sheet == NONE && h2_cache_reference(cache, v + 4) == NONE)
			sheet = h2fx_sheet_for(build, h2_cache_tag_find(cache, 'bitm', "effects\\bitmaps\\lens_flares\\light_volume"));
		if (sheet == NONE || sprites < 1 ||
			!h2fx_room((void **)&h2fx.volumes, &h2fx.volume_capacity, h2fx.volume_count, 8, 64, sizeof(*h2fx.volumes)))
		{
			if (getenv("HALO_H2_EFFECTS_DEBUG"))
				platform_log("h2 effects: light volume %s %ld: sheet %d (bitmap %s), %ld sprites, %ld volumes made", h2_cache_tag_name(cache, tag), i,
					(int)sheet, h2_cache_reference(cache, v + 4) != NONE ? h2_cache_tag_name(cache, h2_cache_reference(cache, v + 4)) : "none",
					sprites, h2fx.volume_count);
			continue;
		}
		h2fx_function_data(cache, v + 0x10, &offset);
		h2fx_function_data(cache, v + 0x18, &radius);
		h2fx_function_data(cache, v + 0x20, &brightness);
		h2fx_function_data(cache, v + 0x28, &color);
		o = &h2fx.volumes[h2fx.volume_count];
		memset(o, 0, sizeof(*o));
		o->h2_tag = tag;
		snprintf(o->name, sizeof(o->name), "%s", tag_short_name(h2_cache_tag_name(cache, tag)));
		o->volume = i;
		/* (its radius: world units on an object's marker - the Pelican's
		thrusters, the watchtowers' antigrav beams, the Scarab's lights, and
		any Halo 2 scales by its marker -, a fraction of the view on a
		projectile (no marker: the plasma bolts' glows, which at world units
		are a pixel or two). As fractions of the view the 03a intro's
		Pelicans and Phantoms wore white discs filling the frame) */
		o->world = ((h2fx_read_short(cache, v) & (1 << 5)) != 0 || marker[0]) && !getenv("HALO_H2_VOLUMES_ALL_VIEW");
		/* (Halo 2's radius is world units for every volume: as a fraction of
		the view the Ghost bolt's 20 sprites of 0.065 along its 1.5 wu tail
		made a white dome over a firing Ghost - owner playtest Oct 30 item 1,
		runs/ghC vs ghD; and with the view-fraction floor of 1/32 the plasma
		rifle's 0.01 wu streak was a white disc, where the owner's MCC frame
		213722 shows a bolt in flight a few pixels wide. Halo 2 bounds the
		volume's overdraw with its x-buffer: the volume's "x-buffer spacing"
		(0x4C), "x-buffer min iterations" (0x50) and "x-buffer max
		iterations" (0x54) - 32 pixels, 4 and 32 in every weapon's and
		vehicle's volume - go to light_volumes.c, which draws that many
		slices and bounds their sum. "radius frac min" (0x40) is kept in
		the tag and not applied: what Halo 2 measures it against is not
		known from the data) */
		if (!o->world && !getenv("HALO_H2_VOLUMES_ALL_VIEW"))
			o->world = TRUE;
		o->view_floor = h2fx_read_real(cache, v + 0x40);
		o->xbuffer_spacing = (short)h2_cache_read32(cache, v + 0x4C);
		o->xbuffer_min = (short)h2_cache_read32(cache, v + 0x50);
		o->xbuffer_max = (short)h2_cache_read32(cache, v + 0x54);
		if (o->xbuffer_spacing < 1 || o->xbuffer_spacing > 1024 || o->xbuffer_min < 1 || o->xbuffer_max < o->xbuffer_min || o->xbuffer_max > 256)
		{
			if (getenv("HALO_H2_DEBUG"))
				fprintf(stderr, "h2 effects: light volume %s %ld: x-buffer fields out of range (%d %d %d), Halo 2's usual taken\n",
					h2_cache_tag_name(cache, tag), i, (int)o->xbuffer_spacing, (int)o->xbuffer_min, (int)o->xbuffer_max);
			o->xbuffer_spacing = 32;
			o->xbuffer_min = 4;
			o->xbuffer_max = 32;
		}
		/* (HALO_H2_SKIP_VOLUMES=name: those so named left out - tests) */
		if (getenv("HALO_H2_SKIP_VOLUMES") && strstr(o->name, getenv("HALO_H2_SKIP_VOLUMES")))
			continue;
		snprintf(o->marker, sizeof(o->marker), "%s", marker);
		o->sheet = sheet;
		o->sequence = 0;
		o->count = (short)(sprites > 32 ? 32 : sprites);
		o->near_fade = h2fx_read_real(cache, t.address);
		o->far_fade = h2fx_read_real(cache, t.address + 4);
		/* (the functions at the volume's ends: this engine's frame
		between them) */
		for (end = 0; end < 2; end++)
		{
			o->offset[end] = h2fx_scalar(&offset, (float)end, 0.0f, 0.0f);
			o->radius[end] = h2fx_scalar(&radius, (float)end, 0.0f, 0.0f);
			o->alpha[end] = h2fx_clamp01(h2fx_scalar(&brightness, (float)end, 0.0f, 1.0f));
			h2fx_rgb(&color, (float)end, 0.0f, o->color[end]);
		}
		if (o->radius[0] <= 0.0f && o->radius[1] <= 0.0f)
			continue;
		if (getenv("HALO_H2_DEBUG"))
			fprintf(stderr, "h2 effects: light volume%s %s %ld at '%s': %d sprites offset %.3f..%.3f radius %.3f..%.3f alpha %.2f..%.2f "
				"colour %.2f %.2f %.2f; radius frac min %.4f, x-buffer spacing %d iterations %d..%d\n", o->world ? " (world units)" : "",
				h2_cache_tag_name(cache, tag), i, marker, o->count, o->offset[0], o->offset[1], o->radius[0],
				o->radius[1], o->alpha[0], o->alpha[1], o->color[0][0], o->color[0][1], o->color[0][2], o->view_floor,
				(int)o->xbuffer_spacing, (int)o->xbuffer_min, (int)o->xbuffer_max);
		for (k = 0; k < 2; k++)
			o->radius[k] *= 1.0f;
		h2fx.volume_count++;
	}
	*made = h2fx.volume_count - first;
	return *made > 0 ? (short)first : NONE;
}

/* a Halo 2 decal an effect makes, made this engine's (build->decals', the
scenario's decals' list - h2_level.c writes them): its bitmap a sprite
sheet, a sprite each of its sequences; NONE if it cannot be */
H2_ALIGN static short h2fx_decal_for(struct build *build, long tag)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	struct decal_made *d, *grown;
	struct h2fx_sheet *sh;
	unsigned char const *h;
	long n;
	short sheet;
	float extent = 0.0f;
	int c;

	for (n = 0; n < build->decal_count; n++)
	{
		if (build->decals[n].h2_tag == tag && build->decals[n].effect)
			return (short)n;
	}
	if (tag == NONE || getenv("HALO_H2_NO_EFFECT_DECALS") || build->decal_count >= 1024 || !h2_cache_tag_get(cache, tag, &t) ||
		t.group_tag != 'deca' || !(h = h2_cache_at(cache, t.address, 0xAC)) ||
		(sheet = h2fx_sheet_for(build, h2_cache_reference(cache, t.address + 0x88))) == NONE)
	{
		return NONE;
	}
	sh = &h2fx.sheets[sheet];
	/* (a sequence of many sprites - bullet holes: each its own, this
	engine choosing a sequence at random and its first sprite) */
	if (sh->real_sequence_count == 1 && sh->sequences[0].count > 1)
		h2fx_sheet_split(sh, 0);
	for (n = 0; n < sh->sprite_count; n++)
	{
		struct h2fx_sprite const *sp = &sh->sprites[n];
		struct bitmap const *b = &build->bitmaps[sh->images[sp->image]];
		float w = (sp->bounds[1] - sp->bounds[0]) * (float)b->width, hh = (sp->bounds[3] - sp->bounds[2]) * (float)b->height;

		if (w > extent)
			extent = w;
		if (hh > extent)
			extent = hh;
	}
	grown = realloc(build->decals, (size_t)(build->decal_count + 1) * sizeof(*build->decals));
	if (!grown)
		return NONE;
	build->decals = grown;
	d = &build->decals[build->decal_count];
	memset(d, 0, sizeof(*d));
	d->h2_tag = tag;
	d->bitmap = sh->images[0];
	d->effect = TRUE;
	d->extent = extent > 0.0f ? extent : (float)build->bitmaps[d->bitmap].width;
	/* (h2/render-accuracy: Halo 2's sprite decal is its sprite's bitmap's
	size times radius / the decal's +0xA4, its "maximum sprite extent" - 16
	in every MCC decal (halo2-decomp unknown_17cb00.cpp 0x17cb00, factor =
	scale / pixels per unit), which this engine's decals.c's
	decal_sprite_get_bounds reads as runtime_maximum_sprite_extent - not
	Halo CE's largest sprite in pixels: a grenade's 64-pixel scorch 4 radii
	across in Halo 2, one here. The desktop's alone;
	HALO_H2_DECAL_SPRITE_SIZE_OLD=1 the largest sprite, as before, which the
	Vita and its null build keep) */
#if !defined(HALO_VITA) && !defined(HALO_GXM_NULL)
	if (readf(h + 0xA4) > 0.0f && !getenv("HALO_H2_DECAL_SPRITE_SIZE_OLD"))
		d->extent = readf(h + 0xA4);
#endif
	d->chain = NONE;
	d->flags = (unsigned short)read16(h);
	d->type = (short)read16(h + 2);
	d->layer = (short)read16(h + 4);
	d->overlap_limit = (short)read16(h + 6);
	d->radius[0] = readf(h + 0x10);
	d->radius[1] = readf(h + 0x14);
	d->overlap_scale = readf(h + 0x18);
	for (c = 0; c < 3; c++)
	{
		d->color[0][c] = readf(h + 0x1C + 4 * c);
		d->color[1][c] = readf(h + 0x28 + 4 * c);
	}
	d->lifetime[0] = readf(h + 0x34);
	d->lifetime[1] = readf(h + 0x38);
	d->decay[0] = readf(h + 0x3C);
	d->decay[1] = readf(h + 0x40);
	if (getenv("HALO_H2_DEBUG"))
		fprintf(stderr, "h2 effects: decal %s: flags %x type %d layer %d radius %.3f..%.3f overlap %d x %.2f, %ld sequences, extent %.0f\n",
			h2_cache_tag_name(cache, tag), d->flags, d->type, d->layer, d->radius[0], d->radius[1], d->overlap_limit, d->overlap_scale,
			sh->sequence_count, d->extent);
	h2fx.effect_decal_count++;
	n = build->decal_count++;
	/* (fix/h2-decals) its "next decal in chain" (0x08), made after it (the
	plasma burn's inner glow, then its outer glow): Halo 2 places the whole
	chain (halo2-decomp unknown_17d9a0.cpp 0x17ee20), as decals.c does from
	the tag's reference - build->decals may move, so by index */
	{
		long chain = h2_cache_reference(cache, t.address + 0x08);

		if (chain != NONE && chain != tag)
			build->decals[n].chain = h2fx_decal_for(build, chain);
	}
	return (short)n;
}

/* (h2fx_parts_read) a light volume an effect fires - 05a's pods' retro rockets,
the white ring about each: this engine's effects make no widgets, so a sprite
of the volume's look at its place, attached, its widest radius: what a volume
is seen as end on. Its life a fit to the walkthrough's ring, 1.5 s */
H2_ALIGN static void h2fx_volume_emitter(struct build *build, struct h2fx_effect *e, short event, long tag, short location)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	struct h2fx_emitter *m;
	struct h2fx_function radius, brightness, color;
	unsigned long volumes;
	long count, k;
	short sheet;
	float widest, alpha, rgb[3];

	/* (an A/B switch for the Vita, HALO_H2_NO_LIGHT_VOLUME_SPRITES=1: none) */
	if (getenv("HALO_H2_NO_LIGHT_VOLUME_SPRITES"))
		return;

	if (!h2_cache_tag_get(cache, tag, &t) || t.group_tag != 'MGS2' || !h2_cache_block(cache, t.address + 8, 0x98, &count, &volumes) ||
		count < 1)
	{
		return;
	}
	sheet = h2fx_sheet_for(build, h2_cache_reference(cache, volumes + 4));
	if (sheet == NONE && h2_cache_reference(cache, volumes + 4) == NONE)
		sheet = h2fx_sheet_for(build, h2_cache_tag_find(cache, 'bitm', "effects\\bitmaps\\lens_flares\\light_volume"));
	if (sheet == NONE || !h2fx_room((void **)&h2fx.emitters, &h2fx.emitter_capacity, h2fx.emitter_count, 64,
			h2fx.tier == H2_PARTICLE_STYLE_TIER_CAPPED ? H2FX_MAXIMUM_EMITTERS_CAPPED : H2FX_MAXIMUM_EMITTERS, sizeof(*h2fx.emitters)))
	{
		return;
	}
	h2fx_function_data(cache, volumes + 0x18, &radius);
	h2fx_function_data(cache, volumes + 0x20, &brightness);
	h2fx_function_data(cache, volumes + 0x28, &color);
	widest = h2fx_scalar(&radius, 0.0f, 0.0f, 0.0f);
	if (h2fx_scalar(&radius, 1.0f, 0.0f, 0.0f) > widest)
		widest = h2fx_scalar(&radius, 1.0f, 0.0f, 0.0f);
	alpha = h2fx_clamp01(h2fx_scalar(&brightness, 0.0f, 0.0f, 1.0f));
	h2fx_rgb(&color, 0.0f, 0.0f, rgb);
	if (getenv("HALO_H2_DEBUG"))
		fprintf(stderr, "h2 effects: %s: light volume part %s: radius %.2f alpha %.2f colour %.2f %.2f %.2f\n", e->name,
			h2_cache_tag_name(cache, tag), widest, alpha, rgb[0], rgb[1], rgb[2]);
	if (widest <= 0.0f)
		return;
	m = &h2fx.emitters[h2fx.emitter_count++];
	memset(m, 0, sizeof(*m));
	m->effect = (short)(e - h2fx.effects);
	m->event = event;
	m->prt3 = NONE;
	m->mesh = NONE;
	m->plasma_sheet = NONE;
	m->death_h2 = m->collision_h2 = m->death_target = m->collision_target = NONE;
	m->child = NONE;
	snprintf(m->name, sizeof(m->name), "volume %s", tag_short_name(h2_cache_tag_name(cache, tag)));
	m->sheet = sheet;
	m->blend = 3;
	m->sequence_count = 1;
	m->flags = 1UL << 9;
	m->life[0] = m->life[1] = 1.5f;
	m->physics = _h2fx_physics_drift;
	m->count[0] = m->count[1] = 1;
	m->repeat = 1;
	m->attached = TRUE;
	m->spins = TRUE;
	m->location = location;
	m->radius[0] = m->radius[1] = widest * h2_particle_style_size_scale();
	m->tint[0][0] = m->tint[1][0] = alpha > 0.0f ? alpha : 1.0f;
	for (k = 0; k < 3; k++)
		m->tint[0][1 + k] = m->tint[1][1 + k] = rgb[k];
	m->style.flags = H2_PARTICLE_STYLE_SCALE | H2_PARTICLE_STYLE_ALPHA | H2_PARTICLE_STYLE_COLOR;
	for (k = 0; k < H2_PARTICLE_STYLE_SAMPLES; k++)
	{
		m->style.scale[k] = 1.0f;
		m->style.alpha[k] = 1.0f - (float)k / (float)H2_PARTICLE_STYLE_SAMPLES;
		m->style.color[k][0] = m->style.color[k][1] = m->style.color[k][2] = 1.0f;
	}
	e->emitter_count++;
}

/* an event's parts (at parts, count of them) that Halo 2 makes and this
engine can: its lights (and lens flares as lights) and decals, kept as the
effect's parts (h2fx.parts) of this engine's event */
H2_ALIGN static void h2fx_parts_read(struct build *build, struct h2fx_effect *e, short event, unsigned long parts, long count,
	short const *location_map, long location_count)
{
	struct h2_cache *cache = build->cache;
	long k;

	for (k = 0; k < count; k++)
	{
		unsigned long q = parts + 0x38 * (unsigned long)k;
		long reference = h2_cache_reference(cache, q + 0xC);
		struct h2_cache_tag t;
		struct h2fx_part *o;
		short index = NONE, location;
		unsigned long group;

		if (reference == NONE || !h2_cache_tag_get(cache, reference, &t))
			continue;
		group = t.group_tag;
		if (group == 'MGS2')
		{
			location = h2fx_read_short(cache, q + 4);
			h2fx_volume_emitter(build, e, event, reference,
				location >= 0 && location < location_count && location < H2FX_MAXIMUM_LOCATIONS ? location_map[location] : 0);
			continue;
		}
		if (group == 'ligh' || group == 'lens')
		{
			index = h2fx_light_for(build, reference);
			group = 'ligh';
		}
		else if (group == 'deca')
			index = h2fx_decal_for(build, reference);
		else if (group == 'bloc')
		{
			index = h2fx_vehicle_damage_effect(e->sources[0]) ? NONE : h2fx_debris_for(build, reference);
			group = 'garb';
		}
		else if (group == 'snd!' && h2fx_sound_parts)
		{
			index = h2w_sound(cache, reference);
			if (getenv("HALO_H2_DEBUG"))
				fprintf(stderr, "h2 effects: %s: sound part %s: %d\n", e->name, h2_cache_tag_name(cache, reference), (int)index);
		}
		else
			continue;
		if (index == NONE ||
			!h2fx_room((void **)&h2fx.parts, &h2fx.part_capacity, h2fx.part_count, 32, 2048, sizeof(*h2fx.parts)))
		{
			h2fx.left_out_parts++;
			continue;
		}
		o = &h2fx.parts[h2fx.part_count++];
		memset(o, 0, sizeof(*o));
		o->event = event;
		location = h2fx_read_short(cache, q + 4);
		o->location = location >= 0 && location < location_count && location < H2FX_MAXIMUM_LOCATIONS ? location_map[location] : location_map[0];
		o->group = group;
		o->index = index;
		o->environment = h2fx_read_short(cache, q);
		o->disposition = h2fx_read_short(cache, q + 2);
		if (o->environment < 0 || o->environment > 3)
			o->environment = 0;
		if (o->disposition < 0 || o->disposition > 2)
			o->disposition = 0;
		o->flags = (unsigned short)(h2fx_read_short(cache, q + 6) & 7);
		o->velocity[0] = h2fx_read_real(cache, q + 0x14);
		o->velocity[1] = h2fx_read_real(cache, q + 0x18);
		o->cone = h2fx_read_real(cache, q + 0x1C);
		o->angular_velocity[0] = h2fx_read_real(cache, q + 0x20);
		o->angular_velocity[1] = h2fx_read_real(cache, q + 0x24);
		o->radius_modifier[0] = h2fx_read_real(cache, q + 0x28);
		o->radius_modifier[1] = h2fx_read_real(cache, q + 0x2C);
		/* (a radius modifier of none: as the decal is) */
		if (o->radius_modifier[0] <= 0.0f && o->radius_modifier[1] <= 0.0f)
			o->radius_modifier[0] = o->radius_modifier[1] = 1.0f;
		o->scale_a = h2_cache_read32(cache, q + 0x30) & 0x3F;
		o->scale_b = h2_cache_read32(cache, q + 0x34) & 0x3F;
		if (group == 'ligh')
			e->has_lights = TRUE;
		else if (group == 'deca')
			e->has_decals = TRUE;
		e->part_count++;
	}
}

/* a Halo 2 effect (and another merged with it: an impact's sweetener)
made into one (h2fx.effects' index), NONE if none */
H2_ALIGN static short h2fx_effect_for(struct build *build, long tag, long other)
{
	struct h2_cache *cache = build->cache;
	struct h2fx_effect *e;
	long sources[H2FX_MAXIMUM_SOURCES], source_count = 0, i, s;
	short event_base[H2FX_MAXIMUM_EVENTS];
	long first_event_count = 0;

	if (tag == NONE && other == NONE)
		return NONE;
	if (tag != NONE)
		sources[source_count++] = tag;
	if (other != NONE && other != tag)
		sources[source_count++] = other;
	for (i = 0; i < h2fx.effect_count; i++)
	{
		if (h2fx.effects[i].source_count == source_count && h2fx.effects[i].sources[0] == sources[0] &&
			(source_count < 2 || h2fx.effects[i].sources[1] == sources[1]))
		{
			if (h2fx_sound_parts && getenv("HALO_H2_DEBUG"))
				fprintf(stderr, "h2 effects: %s made already (its sound parts not read)\n", h2fx.effects[i].name);
			return (short)i;
		}
	}
	if (!h2fx_room((void **)&h2fx.effects, &h2fx.effect_capacity, h2fx.effect_count, 16, H2FX_MAXIMUM_EFFECTS, sizeof(*h2fx.effects)))
		return NONE;
	e = &h2fx.effects[h2fx.effect_count];
	memset(e, 0, sizeof(*e));
	memcpy(e->sources, sources, sizeof(sources));
	e->source_count = source_count;
	e->loop_start = NONE;
	e->first_emitter = h2fx.emitter_count;
	e->first_part = h2fx.part_count;
	snprintf(e->name, sizeof(e->name), "h2\\%s%s", h2_cache_tag_name(cache, sources[0]), source_count > 1 ? " +" : "");
	for (s = 0; s < source_count; s++)
	{
		struct h2_cache_tag t;
		long location_count, event_count, k;
		unsigned long locations, events;
		short location_map[H2FX_MAXIMUM_LOCATIONS];

		if (!h2_cache_tag_get(cache, sources[s], &t) || t.group_tag != 'effe')
			continue;
		if (s == 0)
		{
			e->flags = h2_cache_read32(cache, t.address) & 1;
			e->loop_start = h2fx_read_short(cache, t.address + 4);
		}
		{
			float always = h2fx_read_real(cache, t.address + 0x28), never = h2fx_read_real(cache, t.address + 0x2C);

			if (!(always > 0.0f && never > 0.0f) || (s > 0 && e->play_never == 0.0f))
				e->play_always = e->play_never = 0.0f;
			else
			{
				e->play_always = s == 0 || always > e->play_always ? always : e->play_always;
				e->play_never = s == 0 || never > e->play_never ? never : e->play_never;
			}
		}
		/* its locations (merged by name) */
		if (!h2_cache_block(cache, t.address + 0xC, 4, &location_count, &locations))
			location_count = 0;
		if (location_count > H2FX_MAXIMUM_LOCATIONS)
			location_count = H2FX_MAXIMUM_LOCATIONS;
		for (k = 0; k < location_count; k++)
		{
			char name[64];

			h2fx_marker_name(cache, h2_cache_read32(cache, locations + 4 * (unsigned long)k), name, sizeof(name));
			/* (an impact's "forward" - the way the projectile went, into the
			surface - is this engine's "negative incident" (projectiles.c:
			its "incident" the way back out, toward the shooter): a decal
			along it meets the surface; along "incident" its ray (the
			part's velocity, 0.5 wu) went back out and met nothing - Halo
			2's bullet holes were never made. Unmatched, a location is the
			first, "normal", away from it too. HALO_H2_FORWARD_INCIDENT:
			"incident", as before) */
			if (!strcmp(name, "forward") && !getenv("HALO_H2_FORWARD_AS_IS"))
				snprintf(name, sizeof(name), getenv("HALO_H2_FORWARD_INCIDENT") ? "incident" : "negative incident");
			location_map[k] = h2fx_location(e, name);
		}
		if (location_count == 0)
			location_map[0] = h2fx_location(e, "");
		/* its events: Halo 2's timing (the first's), each system's emitters */
		if (!h2_cache_block(cache, t.address + 0x14, H2FX_H2_EVENT_SIZE, &event_count, &events))
			event_count = 0;
		for (k = 0; k < event_count && k < H2FX_MAXIMUM_EVENTS; k++)
		{
			unsigned long ev = events + H2FX_H2_EVENT_SIZE * (unsigned long)k, systems;
			long system_count, beam_count, n, slice, slices = 1, base;
			unsigned long beams;
			float duration = h2fx_read_real(cache, ev + 0x14);

			if (!h2_cache_block(cache, ev + 0x30, H2FX_H2_SYSTEM_SIZE, &system_count, &systems))
				system_count = 0;
			/* (a long event whose emitters change with their age - a
			wreck's fire burning down: made as slices of it, this engine's
			events one after another, each its part of Halo 2's) */
			if (s == 0 && duration > 1.5f && h2fx_event_ages(cache, systems, system_count))
			{
				slices = (long)(duration / 1.5f + 0.5f);
				if (slices > (h2fx.tier == H2_PARTICLE_STYLE_TIER_CAPPED ? 3 : 8))
					slices = h2fx.tier == H2_PARTICLE_STYLE_TIER_CAPPED ? 3 : 8;
				if (slices < 2)
					slices = 2;
			}
			if (s == 0)
			{
				if (e->event_count + slices > H2FX_MAXIMUM_EVENTS)
					break;
				base = e->event_count;
				event_base[k] = (short)base;
				for (slice = 0; slice < slices; slice++)
				{
					struct h2fx_event *o = &e->events[base + slice];

					o->skip = h2fx_read_real(cache, ev + 4);
					o->delay[0] = slice ? 0.0f : h2fx_read_real(cache, ev + 8);
					o->delay[1] = slice ? 0.0f : h2fx_read_real(cache, ev + 0xC);
					o->duration[0] = h2fx_read_real(cache, ev + 0x10) / (float)slices;
					o->duration[1] = duration / (float)slices;
					o->acceleration_location[0] = o->acceleration_location[1] = NONE;
					{
						long acceleration_count, q;
						unsigned long accelerations;

						if (h2_cache_block(cache, ev + 0x28, 0x14, &acceleration_count, &accelerations))
						{
							for (q = 0; q < acceleration_count && q < 2; q++)
							{
								unsigned long a = accelerations + 0x14 * (unsigned long)q;
								short location = h2fx_read_short(cache, a + 4);

								o->acceleration = h2fx_read_real(cache, a + 8);
								o->cone[0] = h2fx_read_real(cache, a + 0xC);
								o->cone[1] = h2fx_read_real(cache, a + 0x10);
								o->acceleration_location[q] = location >= 0 && location < location_count ? location_map[location] : location_map[0];
							}
						}
					}
				}
				e->event_count += slices;
			}
			else
			{
				/* (a merged one's events: the first's of the same index) */
				if (k >= H2FX_MAXIMUM_EVENTS || k >= first_event_count)
					continue;
				base = event_base[k];
			}
			if (h2_cache_block(cache, ev + 0x20, 0x3C, &beam_count, &beams))
				h2fx.left_out_beams += beam_count;
			/* (its lights, lens flares and decals: Halo 2's, h2fx_parts_read) */
			{
				long part_count;
				unsigned long parts;

				if (h2_cache_block(cache, ev + 0x18, H2FX_H2_PART_SIZE, &part_count, &parts) && part_count > 0)
					h2fx_parts_read(build, e, (short)base, parts, part_count, location_map, location_count);
			}
			for (slice = 0; slice < slices; slice++)
			{
				struct h2fx_event *o = &e->events[base + slice];

				h2fx_emitter_age = slices > 1 ? ((float)slice + 0.5f) / (float)slices : 0.0f;
				for (n = 0; n < system_count; n++)
				{
					unsigned long system = systems + H2FX_H2_SYSTEM_SIZE * (unsigned long)n, emitters;
					long emitter_count, j;
					short location = h2fx_read_short(cache, system + 8);
					unsigned long flags = h2_cache_read32(cache, system + 0x16) & 0xFFFF;

					/* (disabled, not drawn) */
					if (flags & ((1UL << 3) | (1UL << 5)))
						continue;
					if (!h2_cache_block(cache, system + 0x30, H2FX_H2_EMITTER_SIZE, &emitter_count, &emitters))
						continue;
					for (j = 0; j < emitter_count; j++)
					{
						long before = h2fx.emitter_count;

						if (!h2fx_emitter_make(build, e, (short)h2fx.effect_count, (short)(base + slice), system,
							emitters + H2FX_H2_EMITTER_SIZE * (unsigned long)j, o->duration[1], e->loop_start != NONE))
						{
							h2fx.left_out_emitters++;
							/* (HALO_H2_EFFECTS_DEBUG: which) */
							if (getenv("HALO_H2_EFFECTS_DEBUG"))
							{
								struct h2fx_function rate;
								float rb[2];

								h2fx_function_read(cache, emitters + H2FX_H2_EMITTER_SIZE * (unsigned long)j + 0x08, &rate);
								h2fx_bounds(&rate, 1.0f, rb);
								fprintf(stderr, "h2 effects: left out emitter (why %d) of %s event %ld: %s, rate input %d range %d present %d type %d bounds %.3f %.3f\n",
									h2fx_emitter_refused, h2_cache_tag_name(cache, e->sources[0]), k, h2_cache_tag_name(cache, h2_cache_reference(cache, system)),
									rate.input, rate.range, rate.present, rate.type, rb[0], rb[1]);
							}
							continue;
						}
						/* (its location this effect's) */
						h2fx.emitters[before].location = location >= 0 && location < location_count && location < H2FX_MAXIMUM_LOCATIONS ?
							location_map[location] : location_map[0];
					}
				}
			}
			h2fx_emitter_age = 0.0f;
		}
		if (s == 0)
			first_event_count = event_count < H2FX_MAXIMUM_EVENTS ? event_count : H2FX_MAXIMUM_EVENTS;
	}
	if (e->event_count == 0)
	{
		h2fx.part_count = e->first_part;
		return NONE;
	}
	/* (Halo 2's loop's first event: its first slice) */
	if (e->loop_start >= 0)
		e->loop_start = e->loop_start < first_event_count ? event_base[e->loop_start] : NONE;
	if (e->location_count == 0)
		h2fx_location(e, "");
	e->emitter_count = h2fx.emitter_count - e->first_emitter;
	return (short)h2fx.effect_count++;
}

/* ---------- private code: purposes */

H2_ALIGN static void h2fx_target_add(short kind, short material, char const *name, unsigned long group, short effect)
{
	struct h2fx_target *t;

	if (effect == NONE ||
		!h2fx_room((void **)&h2fx.targets, &h2fx.target_capacity, h2fx.target_count, 32, H2FX_MAXIMUM_TARGETS, sizeof(*h2fx.targets)))
		return;
	/* (one found by name: once - two tags of one name) */
	if (kind == _h2fx_target_named)
	{
		long n;

		for (n = 0; n < h2fx.target_count; n++)
		{
			if (h2fx.targets[n].kind == _h2fx_target_named && h2fx.targets[n].effect == effect)
				return;
		}
	}
	t = &h2fx.targets[h2fx.target_count++];
	t->kind = kind;
	t->material = material;
	t->event = 15;
	snprintf(t->name, sizeof(t->name), "%s", name);
	t->group = group;
	t->effect = effect;
	t->contrail = NONE;
	t->light = t->volume = NONE;
	t->marker[0] = 0;
}

/* a material effects (foot) tag's effect of an event for a material name:
its effect and its sweetener (the events a projectile's: 15 its impact or
detonation, 16 fizzle, 17 overpenetrate, 18 attach, 19 bounce) */
H2_ALIGN static boolean h2fx_material_effect(struct h2_cache *cache, long foot, long event, char const *material, long *effect, long *sweetener)
{
	struct h2_cache_tag t;
	long count, entries, i;
	unsigned long events, list;

	*effect = *sweetener = NONE;
	if (foot == NONE || !h2_cache_tag_get(cache, foot, &t) || !h2_cache_block(cache, t.address, 0x18, &count, &events) || count <= event ||
		!h2_cache_block(cache, events + 0x18 * (unsigned long)event + 0x10, 0x18, &entries, &list))
	{
		return FALSE;
	}
	for (i = 0; i < entries; i++)
	{
		char name[64];

		if (!h2_cache_string(cache, h2_cache_read32(cache, list + 0x18 * (unsigned long)i + 0x10), name, sizeof(name)) || strcmp(name, material))
			continue;
		*effect = h2_cache_reference(cache, list + 0x18 * (unsigned long)i);
		*sweetener = h2_cache_reference(cache, list + 0x18 * (unsigned long)i + 8);
		return *effect != NONE || *sweetener != NONE;
	}
	return FALSE;
}

/* a Halo 2 contrail made (h2fx.contrails' index), NONE if it cannot be */
H2_ALIGN static short h2fx_contrail_for(struct build *build, long tag)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	struct h2fx_contrail *c;
	unsigned char const *p;
	long i, count;
	unsigned long states;

	for (i = 0; i < h2fx.contrail_count; i++)
	{
		if (h2fx.contrails[i].h2_tag == tag)
			return (short)i;
	}
	if (tag == NONE || getenv("HALO_H2_NO_CONTRAILS") || !h2_cache_tag_get(cache, tag, &t) || !(p = h2_cache_at(cache, t.address, 0xF0)) ||
		!h2fx_room((void **)&h2fx.contrails, &h2fx.contrail_capacity, h2fx.contrail_count, 8, 32, sizeof(*h2fx.contrails)))
	{
		return NONE;
	}
	c = &h2fx.contrails[h2fx.contrail_count];
	memset(c, 0, sizeof(*c));
	c->h2_tag = tag;
	memcpy(c->fields, p, 0x30);
	c->first_sequence = read16(p + 0x38);
	c->sequence_count = read16(p + 0x3A);
	/* (its shader: flags 0x64, framebuffer blend 0x66, fade mode 0x68 - read
	off the MCC data against the kit's (the plasma rifle's streak: add, fade
	when perpendicular); 0x62 and 0x64 were read before, every contrail
	alpha blended - a dull smoky ribbon for the plasma bolts' streaks) */
	c->blend = read16(p + (getenv("HALO_H2_CONTRAIL_BLEND_OLD") ? 0x62 : 0x66));
	c->fade = read16(p + (getenv("HALO_H2_CONTRAIL_BLEND_OLD") ? 0x64 : 0x68));
	if (c->blend < 0 || c->blend > 7)
		c->blend = 0;
	if (c->fade < 0 || c->fade > 2)
		c->fade = 0;
	c->sheet = h2fx_sheet_for(build, h2_cache_reference(cache, t.address + 0x30));
	if (c->sheet == NONE)
		return NONE;
	if (c->first_sequence < 0 || c->first_sequence >= h2fx.sheets[c->sheet].real_sequence_count)
		c->first_sequence = 0;
	if (c->sequence_count <= 0 || c->first_sequence + c->sequence_count > h2fx.sheets[c->sheet].real_sequence_count)
		c->sequence_count = (short)(h2fx.sheets[c->sheet].real_sequence_count - c->first_sequence);
	if (h2_cache_block(cache, t.address + 0xE8, 0x40, &count, &states))
	{
		for (i = 0; i < count && i < 4; i++)
		{
			unsigned long q = states + 0x40 * (unsigned long)i;
			struct h2fx_contrail_state *o = &c->states[c->state_count++];
			long pphy = h2_cache_reference(cache, q + 0x10);
			char const *name = pphy != NONE ? h2_cache_tag_name(cache, pphy) : "";
			int k;

			o->duration[0] = h2fx_read_real(cache, q);
			o->duration[1] = h2fx_read_real(cache, q + 4);
			o->transition[0] = h2fx_read_real(cache, q + 8);
			o->transition[1] = h2fx_read_real(cache, q + 0xC);
			o->physics = strstr(name, "smoke") ? _h2fx_physics_smoke : _h2fx_physics_drift;
			o->width = h2fx_read_real(cache, q + 0x18);
			for (k = 0; k < 4; k++)
			{
				o->color[0][k] = h2fx_read_real(cache, q + 0x1C + 4 * (unsigned long)k);
				o->color[1][k] = h2fx_read_real(cache, q + 0x2C + 4 * (unsigned long)k);
			}
			o->scale_flags = h2_cache_read32(cache, q + 0x3C);
		}
	}
	return (short)h2fx.contrail_count++;
}

/* a Halo 2 lens flare (lens) attached to an object made a glint: an
effect of one additive sprite (its first reflection's bitmap, radius,
brightness and tint) at its marker, made again every tick (a vehicle's
running lights - "the little shine" on the Ghost); NONE if it cannot be */
H2_ALIGN static short h2fx_glint_for(struct build *build, long lens, char const *marker, boolean boost)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	struct h2fx_effect *e;
	struct h2fx_emitter *m;
	long count, k;
	unsigned long reflections;
	short sheet, image;
	float radius, brightness, tint[3];

	if (lens == NONE || !h2_cache_tag_get(cache, lens, &t) || !h2_cache_block(cache, t.address + 0x40, 0x30, &count, &reflections) ||
		count < 1 || (sheet = h2fx_sheet_for(build, h2_cache_reference(cache, t.address + 0x20))) == NONE)
	{
		return NONE;
	}
	image = h2fx_read_short(cache, reflections + 4);
	radius = h2fx_read_real(cache, reflections + 0x14);
	brightness = h2fx_read_real(cache, reflections + 0x1C);
	for (k = 0; k < 3; k++)
		tint[k] = h2fx_read_real(cache, reflections + 0x24 + 4 * (unsigned long)k);
	if (tint[0] + tint[1] + tint[2] <= 0.0f)
		tint[0] = tint[1] = tint[2] = 1.0f;
	if (!h2fx_room((void **)&h2fx.effects, &h2fx.effect_capacity, h2fx.effect_count, 16, H2FX_MAXIMUM_EFFECTS, sizeof(*h2fx.effects)) ||
		!h2fx_room((void **)&h2fx.emitters, &h2fx.emitter_capacity, h2fx.emitter_count, 64,
			h2fx.tier == H2_PARTICLE_STYLE_TIER_CAPPED ? H2FX_MAXIMUM_EMITTERS_CAPPED : H2FX_MAXIMUM_EMITTERS, sizeof(*h2fx.emitters)))
	{
		return NONE;
	}
	e = &h2fx.effects[h2fx.effect_count];
	memset(e, 0, sizeof(*e));
	e->sources[0] = lens;
	e->source_count = 1;
	e->loop_start = 0;
	snprintf(e->name, sizeof(e->name), "h2\\glints\\%s", tag_short_name(h2_cache_tag_name(cache, lens)));
	h2fx_location(e, marker);
	e->event_count = 1;
	e->events[0].duration[0] = e->events[0].duration[1] = 1.0f / 30.0f;
	e->first_emitter = h2fx.emitter_count;
	e->emitter_count = 1;
	m = &h2fx.emitters[h2fx.emitter_count++];
	memset(m, 0, sizeof(*m));
	m->effect = (short)h2fx.effect_count;
	m->prt3 = NONE;
	/* (none of them, as every emitter (h2fx_emitter_for): left 0, the glint
	drew particle model 0 - a hard white card the size of its flare at each
	of the Banshee's wing lights - with emitter 0 its child and effect 0 at
	its death; particle models came after the glints) */
	m->mesh = NONE;
	m->plasma_sheet = NONE;
	m->death_h2 = m->collision_h2 = m->death_target = m->collision_target = NONE;
	m->child = NONE;
	snprintf(m->name, sizeof(m->name), "glint %s", tag_short_name(h2_cache_tag_name(cache, lens)));
	m->sheet = sheet;
	m->blend = 3;
	m->first_sequence = image >= 0 && image < h2fx.sheets[sheet].real_sequence_count ? image : 0;
	m->sequence_count = 1;
	m->flags = 1UL << 9;
	m->life[0] = m->life[1] = 1.0f / 30.0f + 0.005f;
	m->physics = _h2fx_physics_drift;
	m->count[0] = m->count[1] = 1;
	m->repeat = 1;
	m->attached = TRUE;
	m->spins = TRUE;   /* (turned any way, as it was) */
	/* (a running light's flare's radius is its width, as this engine's
	particle's is half it: the Banshee's wing lights twice the tag test's. A
	boost's whole: MCC's vents' halo, owner session 01:45 and 13:24, no kit frame) */
	m->radius[0] = m->radius[1] = (boost ? 1.0f : 0.5f) * radius * h2_particle_style_size_scale();
	m->tint[0][0] = m->tint[1][0] = brightness > 0.0f ? (brightness > 1.0f ? 1.0f : brightness) : 1.0f;
	for (k = 0; k < 3; k++)
		m->tint[0][1 + k] = m->tint[1][1 + k] = tint[k];
	m->style.flags = H2_PARTICLE_STYLE_SCALE | H2_PARTICLE_STYLE_ALPHA | H2_PARTICLE_STYLE_COLOR;
	for (k = 0; k < H2_PARTICLE_STYLE_SAMPLES; k++)
	{
		m->style.scale[k] = m->style.alpha[k] = 1.0f;
		m->style.color[k][0] = m->style.color[k][1] = m->style.color[k][2] = 1.0f;
	}
	return (short)h2fx.effect_count++;
}

/* a level object's render model's marker names, ";name;name;" (object
0x34 its model, the model's render model at 0, its marker groups at 0x58,
0xC each, the name's string id first - h2_level.c model_markers_write) */
H2_ALIGN static void h2fx_object_markers(struct h2_cache *cache, long tag, char *out, size_t size)
{
	struct h2_cache_tag t, model, render;
	long count = 0, k;
	unsigned long groups = 0;
	size_t used;

	snprintf(out, size, ";");
	if (tag == NONE || !h2_cache_tag_get(cache, tag, &t) || !h2_cache_tag_get(cache, h2_cache_reference(cache, t.address + 0x34), &model) ||
		!h2_cache_tag_get(cache, h2_cache_reference(cache, model.address), &render) ||
		!h2_cache_block(cache, render.address + 0x58, 0xC, &count, &groups))
	{
		return;
	}
	for (k = 0; k < count && k < 256; k++)
	{
		char name[64];

		used = strlen(out);
		if (h2_cache_string(cache, h2_cache_read32(cache, groups + 0xC * (unsigned long)k), name, sizeof(name)) && name[0] &&
			used + strlen(name) + 2 < size)
		{
			char *c;

			/* (spaced as the effects' locations are, h2fx_marker_name, and the
			model's, model_markers_write: "direction up" never matched
			";direction_up;", so every lift's steam stood at the object's
			origin, blowing along its forward) */
			for (c = name; *c; c++)
			{
				if (*c == '_')
					*c = ' ';
			}
			snprintf(out + used, size - used, "%s;", name);
		}
	}
}

/* an attachment of Halo 2's made for this engine's object named name (an
effect, a contrail, a light or a light volume) */
H2_ALIGN static void h2fx_attachment_add(struct build *build, long tag, char const *name, unsigned long group, short effect, short contrail,
	short light, short volume, char const *marker, char const *scale)
{
	struct h2fx_target *o;

	if (!h2fx_room((void **)&h2fx.targets, &h2fx.target_capacity, h2fx.target_count, 32, H2FX_MAXIMUM_TARGETS, sizeof(*h2fx.targets)))
		return;
	o = &h2fx.targets[h2fx.target_count++];
	memset(o, 0, sizeof(*o));
	o->kind = _h2fx_target_projectile_attachments;
	snprintf(o->name, sizeof(o->name), "%s", name);
	o->group = group;
	o->effect = effect;
	o->contrail = contrail;
	o->light = light;
	o->volume = volume;
	snprintf(o->marker, sizeof(o->marker), "%s", marker);
	snprintf(o->scale, sizeof(o->scale), "%s", scale);
	/* (off on this level, h2_vehicle_function_flashlight_scaled: not made, but
	it still stands for the stand-in's light it replaced) */
	o->off = group == 'vehi' && h2_vehicle_function_flashlight_scaled(build, scale);
	o->gated = group == 'vehi' && h2_vehicle_function_flashlight_gated(build, scale);
	if (group == 'scen' || group == 'mach')
		h2fx_object_markers(build->cache, tag, o->markers, sizeof(o->markers));
}

/* whether a vehicle's attachment's scale has a value: a function of the
vehicle's (object 0x64, 0x20 each, export name at 8) or one every vehicle has.
Named by neither it is nothing, and so is the attachment: the Warthog's
"jump" is every vehicle's own (h2_vehicle_tail_value) */
H2_ALIGN static boolean h2fx_vehicle_scale_known(struct h2_cache *cache, unsigned long vehicle, char const *scale)
{
	static char const *const inputs[] = { "driver seat power", "boost", "hover", "engine power", "thrust", "jump" };
	long count, i;
	unsigned long functions;

	for (i = 0; i < (long)NUMBEROF(inputs); i++)
	{
		if (!strcmp(scale, inputs[i]))
			return TRUE;
	}
	if (h2_cache_block(cache, vehicle + 0x64, 0x20, &count, &functions))
	{
		for (i = 0; i < count; i++)
		{
			char name[64];

			h2fx_marker_name(cache, h2_cache_read32(cache, functions + 0x20 * (unsigned long)i + 8), name, sizeof(name));
			if (!strcmp(name, scale))
				return TRUE;
		}
	}
	return FALSE;
}

/* a Halo 2 object's attachments (effects, contrails) for this engine's
object named name (a projectile, a vehicle: group); its light volumes and
lens flares left (the stand-in's) */
H2_ALIGN static void h2fx_attachment_purposes(struct build *build, long tag, char const *name, unsigned long group)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	long count, i;
	unsigned long attachments;

	if (tag == NONE || !h2_cache_tag_get(cache, tag, &t) || !h2_cache_block(cache, t.address + 0x94, 0x18, &count, &attachments))
		return;
	if (getenv("HALO_H2_DEBUG") && (group == 'scen' || group == 'mach'))
		fprintf(stderr, "h2 effects: %s: %ld attachments\n", name, count);
	for (i = 0; i < count; i++)
	{
		unsigned long q = attachments + 0x18 * (unsigned long)i;
		long type = h2_cache_reference(cache, q);
		struct h2_cache_tag a;
		short effect = NONE, contrail = NONE;
		char marker[64], scale[64];

		if (type == NONE || !h2_cache_tag_get(cache, type, &a))
			continue;
		h2fx_marker_name(cache, h2_cache_read32(cache, q + 8), marker, sizeof(marker));
		{
			long k;

			scale[0] = 0;
			for (k = 0; k < 3 && !scale[0]; k++)
				h2fx_marker_name(cache, h2_cache_read32(cache, q + 0xC + 4 * (unsigned long)k), scale, sizeof(scale));
		}
		/* (a boost's - the Ghost's plasma vent flash and its contrail: kept
		for a vehicle, scaled by its boost, the C out h2_vehicles.c gives a
		boosting vehicle (h2fx_attachments_write); a projectile has none) */
		if (strstr(scale, "boost") && group != 'vehi')
		{
			h2fx.left_out_boost++;
			continue;
		}
		if (group == 'vehi' && scale[0] && !h2fx_vehicle_scale_known(cache, t.address, scale))
			continue;
		if (a.group_tag == 'effe')
			effect = h2fx_effect_for(build, type, NONE);
		else if (a.group_tag == 'lens' && group == 'vehi')
			effect = h2fx_glint_for(build, type, marker, strstr(scale, "boost") != NULL);
		else if (a.group_tag == 'lens')
		{
			/* (a lens flare on a projectile or a level object - the Ghost's
			and Banshee's bolts', a lamp's: a light of the flare alone, as
			Halo 2's attached flare is; one scaled by a function this
			engine's object has not - a plasma weapon's heat vents by its
			readiness - left out) */
			/* (a weapon's by its readiness - "ready": this engine's weapon is
			always ready, its export 1 - as always on) */
			short light = (scale[0] && strcmp(scale, "ready")) || getenv("HALO_H2_NO_ATTACHED_FLARES") ? NONE : h2fx_light_for(build, type);

			if (light != NONE)
				h2fx_attachment_add(build, tag, name, group, NONE, NONE, light, NONE, marker, scale);
			continue;
		}
		else if (a.group_tag == 'cont')
			contrail = h2fx_contrail_for(build, type);
		else if (a.group_tag == 'MGS2')
		{
			/* (a light volume - a plasma bolt's glow, a needle's: its volumes
			this engine's light volumes, an attachment each) */
			long made = 0, n;
			short first = h2fx_volumes_for(build, type, marker, &made);

			if (first == NONE)
			{
				h2fx.left_out_light_volumes++;
				if (getenv("HALO_H2_EFFECTS_DEBUG"))
					platform_log("h2 effects: light volume %s of %s left out", h2_cache_tag_name(build->cache, type), name);
			}
			for (n = 0; n < made; n++)
				h2fx_attachment_add(build, tag, name, group, NONE, NONE, NONE, (short)(first + n), marker, scale);
			continue;
		}
		else if (a.group_tag == 'ligh' && group != 'weap')
		{
			/* (a light - a projectile's glow, a vehicle's lamps: Halo 2's,
			the stand-in's of its kind then dropped. A weapon's - its muzzle
			flash, by its illumination - this engine's weapons have not:
			the stand-in's firing effect's light kept) */
			short light;

			/* (one scaled by a function this engine's object has not - a
			Warthog's headlights -: left out, not on always; a boost's - the
			Ghost's boost_light too, its boost through a function - by the
			vehicle's boost) */
			if (scale[0] && !strstr(scale, "boost") && !h2_vehicle_function_flashlight_gated(build, scale))
				continue;
			light = h2fx_light_for(build, type);
			if (light != NONE && h2fx.lights[light].radius <= 0.0f && h2fx.lights[light].lens == NONE)
				light = NONE;
			if (light != NONE)
				h2fx_attachment_add(build, tag, name, group, NONE, NONE, light, NONE, marker, scale);
			continue;
		}
		if (getenv("HALO_H2_DEBUG") && (group == 'scen' || group == 'mach'))
			fprintf(stderr, "h2 effects: %s: attachment %s (%c%c%c%c) at '%s': effect %d\n", name, h2_cache_tag_name(cache, type),
				(int)(a.group_tag >> 24), (int)(a.group_tag >> 16 & 255), (int)(a.group_tag >> 8 & 255), (int)(a.group_tag & 255), marker, effect);
		if (effect == NONE && contrail == NONE)
			continue;
		h2fx_attachment_add(build, tag, name, group, effect, contrail, NONE, NONE, marker, scale);
	}
}

/* Halo 2's liquid (tdtl) beam of a weapon - the Sentinel Beam's, a
"weapon to projectile" liquid among its widgets (weap @9C, a reference
each) - made contrails on its projectile named name: each of the
liquid's first arc's cores (the arcs at 0x68, 0xEC each; an arc's cores at 0xD4,
0x38 each: its bitmap index, then its functions as data blocks, 8 bytes
apart from 0x10 - thickness, colour, ...) a contrail as thick as the
core, of the core's colour at its full brightness (Halo 2 lays an arc's
128 additive sprites over each other, this engine one strip), between
the projectile's start and its end: its projectile travels at once, its
contrail's first point where it is made (contrail_new) and its last
where it hits (contrail_owner_collision), each shot a strip from the
muzzle to the impact a tick long, the beam whole while the trigger is
held. The liquid has no bitmap: Halo 2's covenant beam contrail's.
HALO_H2_NO_LIQUID_BEAM: none (S7) */
H2_ALIGN static void h2fx_liquid_purposes(struct build *build, long weapon, char const *name)
{
	static char const *const bitmaps[] =
	{
		"effects\\bitmaps\\contrails\\covenant_beam",
		"effects\\bitmaps\\contrails\\plasma_cannon_contrail",
		"effects\\bitmaps\\contrails\\sniper_contrail",
	};
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t, l;
	long widget_count, arc_count, core_count, i, k;
	unsigned long widgets, arcs, cores;
	short sheet = NONE;

	if (getenv("HALO_H2_NO_LIQUID_BEAM") || weapon == NONE || !h2_cache_tag_get(cache, weapon, &t) ||
		!h2_cache_block(cache, t.address + 0x9C, 8, &widget_count, &widgets))
	{
		return;
	}
	for (i = 0; i < (long)(sizeof(bitmaps) / sizeof(bitmaps[0])) && sheet == NONE; i++)
		sheet = h2fx_sheet_for(build, h2_cache_tag_find(cache, 'bitm', bitmaps[i]));
	if (sheet == NONE)
		return;
	for (i = 0; i < widget_count; i++)
	{
		long liquid = h2_cache_reference(cache, widgets + 8 * (unsigned long)i);

		/* (a "weapon to projectile" liquid: its type, at 2 after its flags, 1) */
		if (liquid == NONE || !h2_cache_tag_get(cache, liquid, &l) || l.group_tag != 'tdtl' || h2fx_read_short(cache, l.address + 2) != 1 ||
			!h2_cache_block(cache, l.address + 0x68, 0xEC, &arc_count, &arcs) || arc_count < 1 ||
			!h2_cache_block(cache, arcs + 0xD4, 0x38, &core_count, &cores))
		{
			continue;
		}
		for (k = 0; k < core_count && k < 4; k++)
		{
			unsigned long core = cores + 0x38 * (unsigned long)k;
			struct h2fx_function thickness, color;
			struct h2fx_contrail *c;
			struct h2fx_target *o;
			float rgb[3], most;
			int n;

			h2fx_function_read(cache, core + 0x10 - 8, &thickness);
			h2fx_function_read(cache, core + 0x18 - 8, &color);
			if (!thickness.present || thickness.minimum <= 0.0f || thickness.minimum > 1.0f ||
				!h2fx_room((void **)&h2fx.contrails, &h2fx.contrail_capacity, h2fx.contrail_count, 8, 32, sizeof(*h2fx.contrails)) ||
				!h2fx_room((void **)&h2fx.targets, &h2fx.target_capacity, h2fx.target_count, 32, H2FX_MAXIMUM_TARGETS, sizeof(*h2fx.targets)))
			{
				continue;
			}
			rgb[0] = rgb[1] = rgb[2] = 1.0f;
			if (color.present && h2fx_function_is_color(&color))
				memcpy(rgb, color.colors[0], sizeof(rgb));
			most = rgb[0] > rgb[1] ? rgb[0] : rgb[1];
			most = most > rgb[2] ? most : rgb[2];
			for (n = 0; n < 3; n++)
				rgb[n] = most > 0.0f ? rgb[n] / most : 1.0f;
			c = &h2fx.contrails[h2fx.contrail_count];
			memset(c, 0, sizeof(*c));
			c->h2_tag = liquid;
			c->sheet = sheet;
			/* (first point unfaded; a point at every one of its owner's
			moves; viewer facing; the bitmap once along it) */
			write16(c->fields, 1);
			writef(c->fields + 0x04, 300.0f);
			write16(c->fields + 0x18, 4);
			writef(c->fields + 0x1C, 1.0f);
			writef(c->fields + 0x20, 1.0f);
			c->first_sequence = 0;
			c->sequence_count = 1;
			c->blend = 3;
			c->fade = 0;
			/* (its points a tick and a little, then fading out in a tick:
			a shot's strip meets the next's) */
			c->state_count = 2;
			for (n = 0; n < 2; n++)
			{
				struct h2fx_contrail_state *st = &c->states[n];
				int q;

				st->duration[0] = st->duration[1] = n ? 0.0f : 0.04f;
				st->transition[0] = st->transition[1] = n ? 0.0f : 0.034f;
				st->physics = _h2fx_physics_drift;
				st->width = thickness.minimum;
				for (q = 0; q < 2; q++)
				{
					st->color[q][0] = n ? 0.0f : 1.0f;
					st->color[q][1] = rgb[0];
					st->color[q][2] = rgb[1];
					st->color[q][3] = rgb[2];
				}
			}
			o = &h2fx.targets[h2fx.target_count++];
			memset(o, 0, sizeof(*o));
			o->kind = _h2fx_target_projectile_attachments;
			snprintf(o->name, sizeof(o->name), "%s", name);
			o->group = 'proj';
			o->effect = NONE;
			o->contrail = (short)h2fx.contrail_count++;
			o->light = o->volume = NONE;
			platform_log("h2 effects: %s: liquid %s core %ld a contrail %.3f wide, colour %.2f %.2f %.2f", name,
				tag_short_name(h2_cache_tag_name(cache, liquid)), k, thickness.minimum, rgb[0], rgb[1], rgb[2]);
		}
	}
}

/* a projectile's effects (Halo 2's projectile at tag) for this engine's
projectile named name; explosive: its ground detonation on every material */
H2_ALIGN static void h2fx_projectile_purposes(struct build *build, long tag, char const *name)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	long airborne, ground, impact, super, started, foot;
	long m;

	if (tag == NONE || !h2_cache_tag_get(cache, tag, &t))
		return;
	started = h2_cache_reference(cache, t.address + 0xE8);
	airborne = h2_cache_reference(cache, t.address + 0xF0);
	ground = h2_cache_reference(cache, t.address + 0xF8);
	super = h2_cache_reference(cache, t.address + 0x110);
	impact = h2_cache_reference(cache, t.address + 0x140);
	foot = h2_cache_reference(cache, t.address + 0x54);
	/* (its detonation: Halo 2's ground one - a detonation with a material
	under it, or without a probe for one, the most (projectiles.c); an
	airborne one only without) */
	h2fx_target_add(_h2fx_target_projectile_effect, 0, name, 'proj',
		h2fx_effect_for(build, ground != NONE ? ground : airborne != NONE ? airborne : impact, NONE));
	h2fx_attachment_purposes(build, tag, name, 'proj');
	h2fx_target_add(_h2fx_target_projectile_super, 0, name, 'proj', h2fx_effect_for(build, super, NONE));
	h2fx_target_add(_h2fx_target_projectile_started, 0, name, 'proj', h2fx_effect_for(build, started, NONE));
	/* its impacts, a material each (its material effects'): what Halo 2
	plays as it hits by its response - event 15 as it detonates (or a
	bullet hits), 18 as it sticks, 19 as it bounces - and, a timed one's
	(a grenade's), event 15 again as it detonates on the material under it
	(the stand-in's response's detonation effect). Where Halo 2 has none,
	none: the stand-in's own are taken out as they are written
	(h2fx_projectile_responses_clean). Sticking, bouncing and a detonation
	on the material under it: a projectile with detonation effects of its
	own alone (grenades, needles - not rockets and bullets, Halo 2's impact
	effect theirs) */
	for (m = 0; m < (long)(sizeof(h2fx_materials) / sizeof(h2fx_materials[0])); m++)
	{
		static short const events[] = { 15, 18, 19, 17 };   /* (17: as it passes through - a grenade's bounce_water, Halo 2's water "bounce") */
		long e;

		for (e = 0; e < (airborne != NONE || ground != NONE ? (long)NUMBEROF(events) : 1); e++)
		{
			long effect = NONE, sweetener = NONE, k;

			for (k = 0; k < 3 && h2fx_materials[m].h2[k]; k++)
			{
				if (h2fx_material_effect(cache, foot, events[e], h2fx_materials[m].h2[k], &effect, &sweetener))
					break;
			}
			if (effect == NONE && sweetener == NONE)
				h2fx_material_effect(cache, foot, events[e], "default_material", &effect, &sweetener);
			if (effect == NONE && sweetener == NONE)
				continue;
			k = h2fx.target_count;
			h2fx_target_add(_h2fx_target_projectile_material, h2fx_materials[m].ce, name, 'proj',
				h2fx_effect_for(build, effect != NONE ? effect : sweetener, effect != NONE ? sweetener : NONE));
			if (h2fx.target_count > k)
				h2fx.targets[k].event = events[e];
			if (events[e] == 15 && (airborne != NONE || ground != NONE))
				h2fx_target_add(_h2fx_target_projectile_detonation, h2fx_materials[m].ce, name, 'proj',
					h2fx_effect_for(build, effect != NONE ? effect : sweetener, effect != NONE ? sweetener : NONE));
		}
	}
}

/* the particles' death and collision effects (Halo 2's prt3's: the dirt
clumps' terrain_clumps_death - shrapnel and a puff as each dies -, the
blood's collision splash): each made, a tag of its own found by name
(_h2fx_target_named), the particle's death effect (this engine's particle
'effect', played as it dies: particles.c's particle_die) or collision effect.
Those the made effects' own particles have, in turn (the list grows as it
is walked) */
H2_ALIGN static void h2fx_particle_effects(struct build *build)
{
	long i, made = 0;

	for (i = 0; i < h2fx.emitter_count; i++)
	{
		long k;

		for (k = 0; k < 2; k++)
		{
			long h2 = k ? h2fx.emitters[i].collision_h2 : h2fx.emitters[i].death_h2, n, target = NONE;
			short effect;

			if (h2 == NONE)
				continue;
			/* (made: h2fx.emitters may move) */
			effect = h2fx_effect_for(build, h2, NONE);
			if (effect == NONE)
				continue;
			h2fx_target_add(_h2fx_target_named, 0, "", 0, effect);
			for (n = 0; n < h2fx.target_count && target == NONE; n++)
			{
				if (h2fx.targets[n].kind == _h2fx_target_named && h2fx.targets[n].effect == effect)
					target = n;
			}
			if (k)
				h2fx.emitters[i].collision_target = target;
			else
				h2fx.emitters[i].death_target = target;
			if (target != NONE)
				made++;
		}
	}
	if (made)
		platform_log("h2 effects: %ld particles' death and collision effects", made);
}

/* the effects this level's weapons, projectiles and vehicles have of Halo
2's (the weapons and vehicles h2_weapons.c made) */
H2_ALIGN static void h2fx_purposes_gather(struct build *build)
{
	struct h2_cache *cache = build->cache;
	long i;
	char name[160];

	/* the weapons: their firing effects (the first barrel's, two at most),
	their projectiles' */
	for (i = 0; i < h2w.count; i++)
	{
		struct h2w_weapon const *w = h2w.weapons[i];
		long effect_count, k;
		unsigned long barrels = w->barrel, effects;

		/* (its attachments: the sword's blade_on - the blade's glow, its
		purple and blue haze and the arc it leaves as it swings - Halo 2's
		attachment at its glow markers; its lights and sounds the stand-in's) */
		snprintf(name, sizeof(name), "h2\\weapons\\%s", w->name);
		h2fx_world_particles = TRUE;
		h2fx_attachment_purposes(build, w->h2_tag, name, 'weap');
		h2fx_world_particles = FALSE;
		/* (its ready effect, weap @140: the sword's blade_activate_fp, the
		SMG's and needler's ready - h2_weapons.c's " ready" slot; before the
		barrels: the sword has none, and was skipped with its ignition unmade -
		owner playtest Oct 30 item 3) */
		{
			struct h2_cache_tag wt;
			long ready;

			if (h2_cache_tag_get(cache, w->h2_tag, &wt) && (ready = h2_cache_reference(cache, wt.address + 0x140)) != NONE)
			{
				snprintf(name, sizeof(name), "h2\\weapons\\%s ready", w->name);
				h2fx_sound_parts = TRUE;
				h2fx_target_add(_h2fx_target_effect, 0, name, 'effe', h2fx_effect_for(build, ready, NONE));
				h2fx_sound_parts = FALSE;
			}
			/* (its overheated effect, weap @16C: the plasma rifle's
			effects\objects\weapons\rifle\plasma_rifle\overheated - h2_weapons.c's
			" overheated" slot; its sound stays the weapon's overheated loop) */
			if (h2_cache_tag_get(cache, w->h2_tag, &wt) && (ready = h2_cache_reference(cache, wt.address + 0x16C)) != NONE)
			{
				snprintf(name, sizeof(name), "h2\\weapons\\%s overheated", w->name);
				h2fx_target_add(_h2fx_target_effect, 0, name, 'effe', h2fx_effect_for(build, ready, NONE));
				if (getenv("HALO_H2_EFFECTS_DEBUG"))
					fprintf(stderr, "h2 effects: %s overheated effect %s\n", w->name, h2_cache_tag_name(cache, ready));
			}
		}
		/* (its trigger's barrel: the carbine's second, h2w_barrels_read) */
		if (!barrels)
			continue;
		if (h2_cache_block(cache, barrels + 0xE4, 0x34, &effect_count, &effects))
		{
			for (k = 0; k < effect_count && k < H2W_FIRING_EFFECTS; k++)
			{
				snprintf(name, sizeof(name), "h2\\weapons\\%s%s", w->name, k ? " fire 2" : " fire");
				h2fx_target_add(_h2fx_target_effect, 0, name, 'effe',
					h2fx_effect_for(build, h2_cache_reference(cache, effects + 0x34 * (unsigned long)k + 4), NONE));
			}
			/* (a burst's second effect, when Halo 2 has one: its first) */
			if (effect_count == 1 && (w->burst_shots > 1 || w->firing_effect_count > 1))
			{
				snprintf(name, sizeof(name), "h2\\weapons\\%s fire 2", w->name);
				h2fx_target_add(_h2fx_target_effect, 0, name, 'effe', h2fx_effect_for(build, h2_cache_reference(cache, effects + 4), NONE));
			}
		}
		if (getenv("HALO_H2_DEBUG"))
		{
			long m;

			fprintf(stderr, "h2 effects: %s first person markers:", w->name);
			for (m = 0; m < w->first_person.marker_count; m++)
				fprintf(stderr, " '%s'(%d %s)", w->first_person.markers[m].name, w->first_person.markers[m].node,
					w->first_person.markers[m].node >= 0 && w->first_person.markers[m].node < w->first_person.node_count ?
						w->first_person.nodes[w->first_person.markers[m].node].name : "?");
			fprintf(stderr, "; world:");
			for (m = 0; m < w->world.marker_count; m++)
				fprintf(stderr, " '%s'", w->world.markers[m].name);
			fprintf(stderr, "\n");
		}
		snprintf(name, sizeof(name), "h2\\weapons\\%s bullet", w->name);
		h2fx_projectile_purposes(build, h2_cache_reference(cache, barrels + 0x8C), name);
		/* (a liquid beam: the Sentinel Beam's, S7) */
		h2fx_liquid_purposes(build, w->h2_tag, name);
	}
	/* the grenades (the kit's projectiles: Halo 2's in their place) */
	h2fx_projectile_purposes(build, h2_cache_tag_find(cache, 'proj', "objects\\weapons\\grenade\\frag_grenade\\frag_grenade"),
		"weapons\\frag grenade\\frag grenade");
	h2fx_projectile_purposes(build, h2_cache_tag_find(cache, 'proj', "objects\\weapons\\grenade\\plasma_grenade\\plasma_grenade"),
		"weapons\\plasma grenade\\plasma grenade");
	/* the vehicles: their deaths and damage */
	for (i = 0; i < h2v.count; i++)
	{
		struct h2v_vehicle const *v = h2v.vehicles[i];
		char const *destroyed = NULL, *damaged = NULL;

		if (!strncmp(v->name, "warthog_gauss", 13))
		{
			destroyed = "effects\\objects\\vehicles\\warthog\\trans_main_destroyed";
			damaged = "effects\\objects\\vehicles\\warthog\\perm_medium_hull";
		}
		else if (!strncmp(v->name, "warthog", 7))
		{
			destroyed = "effects\\objects\\vehicles\\warthog\\trans_main_destroyed";
			damaged = "effects\\objects\\vehicles\\warthog\\perm_medium_hull";
		}
		else if (!strncmp(v->name, "ghost", 5))
		{
			/* (its explosion: the hlmt's hull section's destroyed response's
			transition effect, hull_destroyed_damage - fired after the
			response's 8 s delay, whose delay effect hull_destroyed_initial_damage
			plays first, by name: h2_vehicle_wrecks.c's warm-up. Halo 2 has no
			trans_hull_destroyed; perm_hull_destroyed_damage is the wreck's
			sparks: "the ghost is missing its explosion effects", the owner) */
			destroyed = "effects\\objects\\vehicles\\ghost\\hull_destroyed_damage";
			damaged = "effects\\objects\\vehicles\\ghost\\perm_hull_med_damage";
		}
		/* (its gun's bolt - the Ghost's and Banshee's plasma, the Warthog's
		chaingun rounds, the Scorpion's shell: Halo 2's projectile's
		impacts, detonation and attachments (glows, flares, trails) on the
		made gun's, "<name> gun bolt") */
		if (h2v_kinds[v->kind].gun && !getenv("HALO_H2_NO_VEHICLE_GUN_EFFECTS"))
		{
			long gun = h2_cache_tag_find(cache, 'weap', h2v_kinds[v->kind].gun);
			struct h2_cache_tag gt;
			long barrel_count;
			unsigned long barrels;

			if (gun != NONE && h2_cache_tag_get(cache, gun, &gt) && h2_cache_block(cache, gt.address + 0x2D0, 0xEC, &barrel_count, &barrels) &&
				barrel_count > 0)
			{
				snprintf(name, sizeof(name), "h2\\vehicles\\%s gun bolt", v->name);
				h2fx_projectile_purposes(build, h2_cache_reference(cache, barrels + 0x8C), name);
				/* (its gun's firing effect Halo 2's own - the Ghost's
				ghost_gun fire: its flash and bolt glow -, as the held weapons'
				"fire" above; the made gun's effect, "<name> gun fire", was the
				kit stand-in's whole effect with Halo 2's sound, whose CE
				energy smoke heaped up into a white dome over a firing Ghost
				(owner playtest Oct 30 item 1)) */
				{
					long effect_count;
					unsigned long effects;

					if (h2_cache_block(cache, barrels + 0xE4, 0x34, &effect_count, &effects) && effect_count > 0 &&
						h2_cache_reference(cache, effects + 4) != NONE)
					{
						snprintf(name, sizeof(name), "h2\\vehicles\\%s gun fire", v->name);
						h2fx_target_add(_h2fx_target_effect, 0, name, 'effe', h2fx_effect_for(build, h2_cache_reference(cache, effects + 4), NONE));
					}
				}
			}
		}
		/* (its second barrel's bomb: Halo 2's detonation - the Banshee's) */
		if (v->gun2_projectile != NONE)
		{
			snprintf(name, sizeof(name), "h2\\vehicles\\%s gun bomb", v->name);
			h2fx_projectile_purposes(build, v->gun2_projectile, name);
		}
		snprintf(name, sizeof(name), "h2\\vehicles\\%s", v->name);
		/* (its attachments: the Ghost's antigravity glow and sparks, its
		plasma vent contrail - looping while it exists) */
		{
			char const *h2 = !strncmp(v->name, "warthog_gauss", 13) ? "objects\\vehicles\\warthog\\turrets\\gauss\\gauss" :
				!strncmp(v->name, "warthog", 7) ? "objects\\vehicles\\warthog\\warthog" :
				!strncmp(v->name, "ghost", 5) ? "objects\\vehicles\\ghost\\ghost" :
				/* (the vehicles made since: their vehicle tag their first piece's name) */
				h2v_kinds[v->kind].placed_only ? h2v_kinds[v->kind].pieces[0].render_model : NULL;

			if (h2)
				h2fx_attachment_purposes(build, h2_cache_tag_find(cache, 'vehi', h2), name, 'vehi');
		}
		if (destroyed)
		{
			long tag = h2_cache_tag_find(cache, 'effe', destroyed);

			if (tag == NONE && !strncmp(v->name, "ghost", 5))
				tag = h2_cache_tag_find(cache, 'effe', "effects\\objects\\vehicles\\ghost\\hull_destroyed_initial_damage");
			if (tag == NONE && !strncmp(v->name, "ghost", 5))
				tag = h2_cache_tag_find(cache, 'effe', "effects\\objects\\vehicles\\ghost\\perm_hull_destroyed_damage");
			h2fx_target_add(_h2fx_target_vehicle_destroyed, 0, name, 'vehi', h2fx_effect_for(build, tag, NONE));
		}
		if (damaged)
			h2fx_target_add(_h2fx_target_vehicle_damaged, 0, name, 'vehi', h2fx_effect_for(build, h2_cache_tag_find(cache, 'effe', damaged), NONE));
		/* (its damage sections' effects - their responses' transition and
		delay effects, its regions' damage states' looping effects -, by
		name: h2_vehicle_damage.c) */
		if (v->damage && !getenv("HALO_H2_NO_VEHICLE_DAMAGE_EFFECTS"))
		{
			long k;

			for (k = 0; k < v->damage->name_count; k++)
			{
				short effect = v->damage_effects[k] != NONE ? h2fx_effect_for(build, v->damage_effects[k], NONE) : NONE;
				long n;

				/* (once: a Gauss Warthog's are the Warthog's) */
				for (n = 0; effect != NONE && n < h2fx.target_count && !(h2fx.targets[n].kind == _h2fx_target_named &&
					h2fx.targets[n].effect == effect); n++)
					;
				if (effect != NONE && n == h2fx.target_count)
					h2fx_target_add(_h2fx_target_named, 0, "", 0, effect);
			}
		}
	}
	/* the bipeds' shields: Halo 2's model's shield damaged, depleted and
	recharging effects (its damage info, 0x60: 0xA4, 0xAC, 0xB4) on the
	made biped's collision model ("h2\\characters\\<name>_mp\\<name>_mp") -
	the Elite's flare, the shields' pop and their recharge's shimmer.
	HALO_H2_NO_BIPED_EFFECTS: the stand-in's */
	if (!getenv("HALO_H2_NO_BIPED_EFFECTS"))
	{
		short b;

		for (b = 0; h2_bipeds_source_name(b); b++)
		{
			long biped = h2_bipeds_made_from(cache, b), model, count, k;
			struct h2_cache_tag bt, mt;
			unsigned long info;

			if (biped == NONE || !h2_cache_tag_get(cache, biped, &bt) || (model = h2_cache_reference(cache, bt.address + 0x34)) == NONE ||
				!h2_cache_tag_get(cache, model, &mt) || !h2_cache_block(cache, mt.address + 0x60, 0xF8, &count, &info) || count < 1)
			{
				continue;
			}
			snprintf(name, sizeof(name), "h2\\characters\\%s_mp\\%s_mp", h2_bipeds_source_name(b), h2_bipeds_source_name(b));
			/* (its damage sections' (0xBC, 0x38 each) instant responses (0xC,
			0x50 each) destroying a region (its name a string id at 0x1C) with
			a transition effect (0xC): the Marine's helmet thrown off at a
			headshot - on the collision model's region of that name) */
			{
				long section_count, response_count, s2, j2;
				unsigned long sections, responses;

				if (h2_cache_block(cache, info + 0xBC, 0x38, &section_count, &sections))
				{
					for (s2 = 0; s2 < section_count; s2++)
					{
						if (!h2_cache_block(cache, sections + 0x38 * (unsigned long)s2 + 0xC, 0x50, &response_count, &responses))
							continue;
						for (j2 = 0; j2 < response_count; j2++)
						{
							unsigned long response = responses + 0x50 * (unsigned long)j2;
							long effect = h2_cache_reference(cache, response + 0xC);
							char region[32];
							short made;

							if (effect == NONE)
								continue;
							h2_cache_string(cache, h2_cache_read32(cache, response + 0x1C), region, sizeof(region));
							if (!region[0])
								continue;
							made = h2fx_effect_for(build, effect, NONE);
							if (made == NONE)
								continue;
							h2fx_target_add(_h2fx_target_biped_region, 0, name, 'bipd', made);
							if (h2fx.target_count > 0 && h2fx.targets[h2fx.target_count - 1].effect == made)
								snprintf(h2fx.targets[h2fx.target_count - 1].marker, sizeof(h2fx.targets[0].marker), "%s", region);
							if (getenv("HALO_H2_DEBUG"))
								fprintf(stderr, "h2 effects: %s region %s destroyed effect %ld -> %d\n", name, region, effect, made);
						}
					}
				}
			}
			for (k = 0; k < 3; k++)
			{
				long effect = h2_cache_reference(cache, info + 0xA4 + 8 * (unsigned long)k);

				if (effect != NONE)
				{
					short made = h2fx_effect_for(build, effect, NONE);

					if (getenv("HALO_H2_DEBUG"))
						fprintf(stderr, "h2 effects: %s shield effect %ld: %ld -> %d\n", name, k, effect, made);
					h2fx_target_add(_h2fx_target_biped_shield, (short)k, name, 'bipd', made);
				}
			}
		}
	}
	/* (found by name at run time: a burning wreck's fire and smoke,
	h2_vehicle_wrecks.c) */
	h2fx_target_add(_h2fx_target_named, 0, "", 0,
		h2fx_effect_for(build, h2_cache_tag_find(cache, 'effe', "effects\\objects\\vehicles\\warthog\\looping_engine_smoke"), NONE));
	h2fx_target_add(_h2fx_target_named, 0, "", 0,
		h2fx_effect_for(build, h2_cache_tag_find(cache, 'effe', "effects\\objects\\vehicles\\warthog\\looping_universal_smoke"), NONE));
	/* (a foot or a vehicle on water, by name: h2_vehicle_wrecks.c's h2_water_contact) */
	{
		static char const *const contacts[] = { "effects\\contact\\walk\\footsteps_water", "effects\\contact\\run\\footsteps_water",
			"effects\\contact\\jump_land\\water_land", "effects\\contact\\rolling\\vehicle_water" };
		long n;

		for (n = 0; n < (long)NUMBEROF(contacts); n++)
			h2fx_target_add(_h2fx_target_named, 0, "", 0, h2fx_effect_for(build, h2_cache_tag_find(cache, 'effe', contacts[n]), NONE));
	}
	/* (a pane's break, by name too: h2_vehicle_wrecks.c's h2_breakable_broke) */
	h2fx_target_add(_h2fx_target_named, 0, "", 0,
		h2fx_effect_for(build, h2_cache_tag_find(cache, 'effe', "effects\\impact\\impact_bullet_large\\brittle_glass"), NONE));
	h2fx_target_add(_h2fx_target_named, 0, "", 0,
		h2fx_effect_for(build, h2_cache_tag_find(cache, 'effe', "effects\\generic\\solids\\glass_break_death"), NONE));
	/* (a Ghost's explosion, by name too: the depleted effect's particles
	stand at Halo 2's markers, which the stand-in Ghost lacks - its middle,
	h2_vehicle_wrecks.c) */
	h2fx_target_add(_h2fx_target_named, 0, "", 0,
		h2fx_effect_for(build, h2_cache_tag_find(cache, 'effe', "effects\\objects\\vehicles\\ghost\\hull_destroyed_damage"), NONE));
	/* (a critical Ghost's warm-up: its hull's destroyed response's delay
	effect - the whine, sparks, electric discharge, fiery smoke) */
	h2fx_target_add(_h2fx_target_named, 0, "", 0,
		h2fx_effect_for(build, h2_cache_tag_find(cache, 'effe', "effects\\objects\\vehicles\\ghost\\hull_destroyed_initial_damage"), NONE));
	/* (a dead Ghost's: its destroyed hull's sparks and smoke) */
	h2fx_target_add(_h2fx_target_named, 0, "", 0,
		h2fx_effect_for(build, h2_cache_tag_find(cache, 'effe', "effects\\objects\\vehicles\\ghost\\perm_hull_destroyed_damage"), NONE));
	/* (the fusion coils' (Zanzibar's explosive canisters): their damaged
	sparks and their blast, by name for the object conversion - h2\\effects\\
	objects\\gear\\human\\military\\fusion_coil_major and _destroyed) */
	h2fx_target_add(_h2fx_target_named, 0, "", 0,
		h2fx_effect_for(build, h2_cache_tag_find(cache, 'effe', "effects\\objects\\gear\\human\\military\\fusion_coil_major"), NONE));
	h2fx_target_add(_h2fx_target_named, 0, "", 0,
		h2fx_effect_for(build, h2_cache_tag_find(cache, 'effe', "effects\\objects\\gear\\human\\military\\fusion_coil_destroyed"), NONE));
	/* (a campaign level's scripts' effects, by name for effect_new and
	effect_new_on_object_marker - h2_scripts.c; HALO_H2_SCRIPT_EFFECTS: at
	most so many, 48 by default) */
	if (!build->multiplayer)
	{
		long tags[256], count, maximum = getenv("HALO_H2_SCRIPT_EFFECTS") ? atol(getenv("HALO_H2_SCRIPT_EFFECTS")) : 48, k;

		count = h2_scripts_effect_tags(cache, tags, maximum < 256 ? maximum : 256);
		for (k = 0; k < count; k++)
			h2fx_target_add(_h2fx_target_named, 0, "", 0, h2fx_effect_for(build, tags[k], NONE));
		if (count)
			platform_log("h2 effects: %ld of the scripts' effects made", count);
	}
	/* (the level's explosive objects - the fusion coils, 03a's battery:
	their damage sections' responses' (0xBC: 0x38, 0xC: 0x50) transition
	effect (0xC) destroying them, their collision model's body destroyed
	effect, the stand-in explosion's damage and sound kept) */
	if (!getenv("HALO_H2_NO_OBJECT_DESTROYED_EFFECTS"))
	{
		long i;

		for (i = 0; i < build->object_count; i++)
		{
			struct h2_cache_tag ot, mt;
			long model, info_count, section_count, response_count, s2, j2, effect = NONE;
			unsigned long info, sections, responses;
			char object_name[96];

			if (!build->objects[i].explosive || build->objects[i].collision == NONE ||
				!h2_cache_tag_get(cache, build->objects[i].h2_tag, &ot) || (model = h2_cache_reference(cache, ot.address + 0x34)) == NONE ||
				!h2_cache_tag_get(cache, model, &mt) || !h2_cache_block(cache, mt.address + 0x60, 0xF8, &info_count, &info) || info_count < 1 ||
				!h2_cache_block(cache, info + 0xBC, 0x38, &section_count, &sections))
			{
				continue;
			}
			for (s2 = 0; s2 < section_count && effect == NONE; s2++)
			{
				if (!h2_cache_block(cache, sections + 0x38 * (unsigned long)s2 + 0xC, 0x50, &response_count, &responses))
					continue;
				for (j2 = 0; j2 < response_count && effect == NONE; j2++)
					effect = h2_cache_reference(cache, responses + 0x50 * (unsigned long)j2 + 0xC);
			}
			if (effect == NONE)
				continue;
			snprintf(object_name, sizeof(object_name), "h2\\objects\\%s collision", build->objects[i].name);
			if (getenv("HALO_H2_DEBUG"))
				fprintf(stderr, "h2 effects: %s destroyed effect %ld\n", object_name, effect);
			h2fx_target_add(_h2fx_target_object_destroyed, 0, object_name, 'coll', h2fx_effect_for(build, effect, NONE));
			{
				short stage;

				for (stage = 0; stage < 3; stage++)
				{
					if (build->objects[i].stage_effect[stage] != NONE)
						h2fx_target_add(_h2fx_target_object_stage, stage, object_name, 'coll', h2fx_effect_for(build, build->objects[i].stage_effect[stage], NONE));
				}
			}
		}
	}
	/* (the level's shielded objects - the Enforcer's frontal shields: their
	damage info's shield damaged, depleted and recharging effects (0xA4,
	0xAC, 0xB4) on "h2\\objects\\NAME collision" - the shield's pop,
	effects\\objects\\characters\\jackal\\shield_depletion) */
	if (!getenv("HALO_H2_NO_OBJECT_SHIELDS"))
	{
		long i, k;

		for (i = 0; i < build->object_count; i++)
		{
			struct h2_cache_tag ot, mt;
			long model, info_count;
			unsigned long info;
			char object_name[96];

			if (!object_shielded(&build->objects[i]) || build->objects[i].collision == NONE ||
				!h2_cache_tag_get(cache, build->objects[i].h2_tag, &ot) || (model = h2_cache_reference(cache, ot.address + 0x34)) == NONE ||
				!h2_cache_tag_get(cache, model, &mt) || !h2_cache_block(cache, mt.address + 0x60, 0xF8, &info_count, &info) || info_count < 1)
			{
				continue;
			}
			snprintf(object_name, sizeof(object_name), "h2\\objects\\%s collision", build->objects[i].name);
			for (k = 0; k < 3; k++)
			{
				long effect = h2_cache_reference(cache, info + 0xA4 + 8 * (unsigned long)k);
				short made = effect != NONE ? h2fx_effect_for(build, effect, NONE) : NONE;

				if (getenv("HALO_H2_DEBUG"))
					fprintf(stderr, "h2 effects: %s shield effect %ld: %ld -> %d\n", object_name, k, effect, made);
				if (made != NONE)
					h2fx_target_add(_h2fx_target_object_shield, (short)k, object_name, 'coll', made);
			}
		}
	}
	/* (the level's objects' own attachments - the teleporters' plasma
	drift: on h2\objects\NAME, looping while it stands) */
	if (!getenv("HALO_H2_NO_OBJECT_ATTACHMENTS"))
	{
		long i;

		for (i = 0; i < build->object_count; i++)
		{
			char object_name[96];

			snprintf(object_name, sizeof(object_name), "h2\\objects\\%s", build->objects[i].name);
			h2fx_attachment_purposes(build, build->objects[i].h2_tag, object_name, build->objects[i].group);
		}
	}
}

/* (a collision model's damage resistance) */
#define H2FX_COLLISION_RESISTANCE (H2FX_COLLISION_BODY_DESTROYED_EFFECT - offsetof(struct damage_resistance, body_destroyed_effect))

/* ---------- private code: debris */

/* whether an effect is a vehicle's damage effect (its garbage thrown by
h2_vehicle_damage.c already, its responses' debris) */
H2_ALIGN static boolean h2fx_vehicle_damage_effect(long effect)
{
	long i, k;

	for (i = 0; i < h2v.count; i++)
	{
		struct h2v_vehicle const *v = h2v.vehicles[i];

		for (k = 0; v->damage && k < v->damage->name_count; k++)
		{
			if (v->damage_effects[k] == effect)
				return TRUE;
		}
	}
	return FALSE;
}

/* the debris made of a crate (bloc) an effect throws - concrete chunks
off a wall, a fallen Marine's helmet, a battery's fragments, a turret's
parts: the vehicles' debris (h2v.debris', an object of its render model,
"h2\\vehicles\\debris\\fx <crate>") made of it, at most 16; NONE. The PC
tier only (the objects a firefight throws: HALO_H2_NO_EFFECT_DEBRIS none) */
H2_ALIGN static short h2fx_debris_for(struct build *build, long crate)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag c, m;
	struct h2v_debris *d;
	long k, model, render;
	char name[96];
	static long made;

	if (getenv("HALO_H2_NO_EFFECT_DEBRIS") || h2fx.tier != H2_PARTICLE_STYLE_TIER_FULL)
		return NONE;
	snprintf(name, sizeof(name), "fx %.24s", tag_short_name(h2_cache_tag_name(cache, crate)));
	for (k = 0; k < h2v.debris_count; k++)
	{
		if (!strncmp(h2v.debris[k]->name, name, sizeof(h2v.debris[k]->name) - 1))
			return (short)k;
	}
	if (h2fx.effect_debris_count >= 16 || h2v.debris_count >= H2V_MAXIMUM_DEBRIS || !h2_cache_tag_get(cache, crate, &c) ||
		c.group_tag != 'bloc' || (model = h2_cache_reference(cache, c.address + 0x34)) == NONE || !h2_cache_tag_get(cache, model, &m) ||
		(render = h2_cache_reference(cache, m.address)) == NONE || !(d = calloc(1, sizeof(*d))))
	{
		return NONE;
	}
	(void)made;
	snprintf(d->name, sizeof(d->name), "%s", name);
	h2w_model_begin(&d->model);
	d->model.levels = TRUE;
	if (!h2w_model_add(build, &d->model, render, NULL, h2w_detail("HALO_H2_VEHICLE_DETAIL", H2W_VEHICLE_DETAIL_DEFAULT), FALSE) ||
		!h2w_model_finish(build, &d->model))
	{
		free(d);
		return NONE;
	}
	h2v.debris[h2v.debris_count] = d;
	h2fx.effect_debris_count++;
	platform_log("h2 effects: %s a debris object (%s)", name, h2_cache_tag_name(cache, render));
	return (short)h2v.debris_count++;
}

/* ---------- private code: weather */

/* the scenario's weather palette's systems made this engine's (h2fx.weathers,
in the palette's order; a system not made: its sheet NONE). HALO_H2_NO_WEATHER:
none */
H2_ALIGN static void h2fx_weather_gather(struct build *build)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag scenario;
	long count, k;
	unsigned long palette;

	if (getenv("HALO_H2_NO_WEATHER") || !h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) ||
		!h2_cache_block(cache, scenario.address + 0x260, 0x88, &count, &palette) || count <= 0)
	{
		return;
	}
	for (k = 0; k < count && k < 16; k++)
	{
		unsigned long entry = palette + 0x88 * (unsigned long)k, systems;
		long tag = h2_cache_reference(cache, entry + 0x20), system_count;
		struct h2fx_weather *w = &h2fx.weathers[h2fx.weather_count++];
		struct h2_cache_tag t;
		unsigned char const *name = h2_cache_at(cache, entry, 32);
		long bitmap;

		memset(w, 0, sizeof(*w));
		w->h2_tag = tag;
		w->sheet = NONE;
		if (name)
			snprintf(w->name, sizeof(w->name), "%.31s", (char const *)name);
		if (tag == NONE || !h2_cache_tag_get(cache, tag, &t) || t.group_tag != 'weat' ||
			!h2_cache_block(cache, t.address, 0x90, &system_count, &systems) || system_count < 1)
		{
			continue;
		}
		w->rain = h2fx_read_short(cache, systems + 0x6C) == 2;
		w->wind[0] = h2fx_read_real(cache, t.address + 0x14);
		w->wind[1] = h2fx_read_real(cache, t.address + 0x18);
		w->wind[2] = h2fx_read_real(cache, t.address + 0x1C);
		w->wind_strength = h2fx_read_real(cache, t.address + 0x28);
		if (!(w->wind_strength >= 0.0f && w->wind_strength < 1000.0f) || !(fabsf(w->wind[0]) <= 1.0f && fabsf(w->wind[1]) <= 1.0f && fabsf(w->wind[2]) <= 1.0f))
			w->wind_strength = 0.0f;
		w->box = h2fx_read_real(cache, systems + 8);
		w->velocity = h2fx_read_real(cache, systems + 0x18);
		w->size[0] = h2fx_read_real(cache, systems + 0x24);
		w->size[1] = h2fx_read_real(cache, systems + 0x28);
		w->most = (long)h2_cache_read32(cache, systems + 0x2C);
		w->opacity[0] = h2fx_clamp01(h2fx_read_real(cache, systems + 0x70));
		w->opacity[1] = h2fx_clamp01(h2fx_read_real(cache, systems + 0x74));
		/* (none given - 06a's snow: Halo 2's flakes at their bitmap's own) */
		if (w->opacity[1] <= 0.0f)
			w->opacity[0] = w->opacity[1] = 1.0f;
		w->streak = h2fx_read_real(cache, systems + 0x78);
		if (w->box < 1.0f || w->box > 50.0f)
			w->box = 5.0f;
		if (w->most < 1 || w->most > 4096)
			w->most = 256;
		/* (its sprites; rain has none - Halo 2 draws its drops as lines:
		a soft glow stretched along their fall) */
		bitmap = h2_cache_reference(cache, systems);
		if (bitmap == NONE)
			bitmap = h2_cache_tag_find(cache, 'bitm', "effects\\bitmaps\\lens_flares\\light_volume");
		w->sheet = h2fx_sheet_for(build, bitmap);
		/* (its background plates, Halo 2's three planes: the desktop's tier -
		HALO_H2_NO_WEATHER_PLATES: none) */
		{
			long plate_count;
			unsigned long plates;

			if (h2fx.tier != H2_PARTICLE_STYLE_TIER_CAPPED && !getenv("HALO_H2_NO_WEATHER_PLATES") &&
				h2_cache_block(cache, t.address + 8, 0xC8, &plate_count, &plates) && plate_count > 0)
			{
				float z_near = h2fx_read_real(cache, plates + 0x78), z_far = h2fx_read_real(cache, plates + 0x7C);
				float blend_near = h2fx_read_real(cache, plates + 0x80), blend_far = h2fx_read_real(cache, plates + 0x84);
				long n, c;

				if (!(z_far > z_near))
					z_near = 10.0f, z_far = 80.0f;
				if (!(blend_far > blend_near && blend_near >= 0.0f))
					blend_near = blend_far = 0.0f;
				for (n = 0; n < 3; n++)
				{
					struct h2fx_plate *pl = &w->plates[w->plate_count];
					long texture = h2_cache_reference(cache, plates + 8 * (unsigned long)n);

					if (texture == NONE)
						continue;
					pl->sheet = h2fx_sheet_for(build, texture);
					pl->opacity = h2fx_clamp01(h2fx_read_real(cache, plates + 0x88 + 4 * (unsigned long)n));
					if (pl->sheet == NONE || pl->opacity <= 0.0f)
						continue;
					/* (HALO_H2_PLATE_ONLY=n: that plate alone - tests) */
					if (getenv("HALO_H2_PLATE_ONLY") && atoi(getenv("HALO_H2_PLATE_ONLY")) != n)
						continue;
					/* (HALO_H2_DUMP_BITMAPS: the plate's texture decoded, RGBA, into
					the level folder's dump - tests) */
					if (getenv("HALO_H2_DUMP_BITMAPS"))
					{
						unsigned short dw, dh;
						unsigned char *rgba = image_decoded(build, texture, &dw, &dh);

						if (rgba)
						{
							char dump[512];
							FILE *file;

							h2_level_folder_create(), CreateDirectoryA(H2_LEVEL_FOLDER "\\dump", NULL);
							snprintf(dump, sizeof(dump), "%s\\dump\\plate_%d_%d_%s.raw", H2_LEVEL_FOLDER, dw, dh, tag_short_name(h2_cache_tag_name(cache, texture)));
							if ((file = fopen(dump, "wb")) != NULL)
							{
								fwrite(rgba, 1, (size_t)dw * dh * 4, file);
								fclose(file);
							}
							free(rgba);
						}
						if ((rgba = image_alpha(build, texture, &dw, &dh)) != NULL)
						{
							char dump[512];
							FILE *file;

							snprintf(dump, sizeof(dump), "%s\\dump\\plate_%d_%d_%s_alpha.raw", H2_LEVEL_FOLDER, dw, dh, tag_short_name(h2_cache_tag_name(cache, texture)));
							if ((file = fopen(dump, "wb")) != NULL)
							{
								fwrite(rgba, 1, (size_t)dw * dh, file);
								fclose(file);
							}
							free(rgba);
						}
					}
					pl->position = h2fx_clamp01(h2fx_read_real(cache, plates + 0x18 + 4 * (unsigned long)n));
					pl->z = z_near + pl->position * (z_far - z_near);
					pl->z_near = z_near;
					pl->z_far = z_far;
					pl->flags = (unsigned long)h2_cache_read32(cache, plates + 0x94);
					pl->mass = h2fx_read_real(cache, plates + 0xBC + 4 * (unsigned long)n);
					for (c = 0; c < 3; c++)
					{
						pl->speed[c] = h2fx_read_real(cache, plates + 0x24 + 12 * (unsigned long)n + 4 * (unsigned long)c);
						pl->tint[c] = h2fx_clamp01(h2fx_read_real(cache, plates + 0x98 + 12 * (unsigned long)n + 4 * (unsigned long)c));
					}
					pl->scale = h2fx_read_real(cache, plates + 0x48 + 4 * (unsigned long)n);
					if (!(pl->scale > 0.0f))
						pl->scale = 0.1f;
					pl->blend[0] = blend_near;
					pl->blend[1] = blend_far;
					platform_log("h2 effects: weather %ld plate %ld: %s (%dx%d format %d) at %.1f (z %.0f..%.0f, depth blend %.0f..%.0f), scale %.3f, speed %.1f %.1f %.1f, opacity %.2f, tint %.2f %.2f %.2f",
						k, n, h2_cache_tag_name(cache, texture), build->bitmaps[h2fx.sheets[pl->sheet].images[0]].width,
						build->bitmaps[h2fx.sheets[pl->sheet].images[0]].height, build->bitmaps[h2fx.sheets[pl->sheet].images[0]].format, pl->z, z_near,
						z_far, blend_near, blend_far, pl->scale, pl->speed[0], pl->speed[1], pl->speed[2], pl->opacity, pl->tint[0], pl->tint[1], pl->tint[2]);
					w->plate_count++;
				}
			}
		}
		platform_log("h2 effects: weather %ld '%s': %s, %s %ld in a %.1f box, size %.3f..%.3f, opacity %.2f..%.2f, falls at most %.1f", k,
			w->name, h2_cache_tag_name(cache, tag), w->rain ? "rain" : "snow", w->most, w->box, w->size[0], w->size[1], w->opacity[0],
			w->opacity[1], w->velocity);
	}
}

/* a weather system's tag written (rain: 0x30, its particle type 0x25C,
the kit particle's shader its shader) */
H2_ALIGN static void h2fx_weather_write(struct region *r, unsigned long root, struct h2fx_weather const *w, unsigned long bitmap_id,
	long pixels_across, unsigned long kit_particle_root)
{
	struct h2fx_sheet const *sh = &h2fx.sheets[w->sheet];
	struct h2fx_sprite const *sprite = &sh->sprites[sh->sequences[0].first];
	float width = sprite->bounds[1] - sprite->bounds[0];
	long physics = h2fx.kit_physics[w->rain ? _h2fx_physics_debris : _h2fx_physics_light];
	unsigned long type = region_block(r, root + 0x24, 1 + w->plate_count, 0x25C);
	unsigned char *o;
	int k;

	if (r->b.failed)
		return;
	/* (the shader: the kit particle's, alpha blended) */
	memcpy(region_at(r, type + 0x1A8), region_at(r, kit_particle_root + PART_SHADER), 0xB4);
	region_reference(r, type + 0x1A8 + 0x4C, 0, 0xFFFFFFFFUL);
	region_reference(r, type + 0xAC, 'pphy', physics != NONE ? tag_id(physics) : 0xFFFFFFFFUL);
	region_reference(r, type + 0x194, 'bitm', bitmap_id);
	o = region_at(r, type);
	snprintf((char *)o, 32, "h2 %s", w->rain ? "rain" : "snow");
	/* (falling at Halo 2's speed: the top bit, the speed in the first
	unused word - weather_particle_systems.c) */
	write32(o + 0x20, (w->rain ? 0 : 1UL << 2) | 0x80000000UL);
	writef(o + 0x44, w->velocity > 0.0f ? w->velocity : 1.0f);
	/* (near it fades in, at its box's edge out: Halo 2's view box) */
	writef(o + 0x24, 0.1f);
	writef(o + 0x28, 0.5f);
	writef(o + 0x2C, w->box * 0.7f);
	writef(o + 0x30, w->box);
	/* (at every height) */
	writef(o + 0x34, -10000.0f);
	writef(o + 0x38, -9999.0f);
	writef(o + 0x3C, 10000.0f);
	writef(o + 0x40, 10001.0f);
	/* (so many a cubic world unit: Halo 2's most in its box) */
	writef(o + 0xA4, (float)w->most / (w->box * w->box * w->box));
	writef(o + 0xA8, (float)w->most / (w->box * w->box * w->box));
	{
		/* (HALO_H2_WEATHER_TEST: ten times as large, opaque - tests) */
		float k2 = getenv("HALO_H2_WEATHER_TEST") ? 10.0f : 1.0f;

		writef(o + 0xFC, k2 * w->size[0] * (w->rain ? (w->streak > 0.0f ? w->streak : 1.0f) : 1.0f));
		writef(o + 0x100, k2 * w->size[1] * (w->rain ? (w->streak > 0.0f ? w->streak : 1.0f) : 1.0f));
	}
	for (k = 0; k < 2; k++)
	{
		unsigned char *c = o + 0x134 + 0x10 * k;

		/* (snow: Halo 2's opacity bounds - Lockout's 0.05-0.06 - are not an
		alpha its flakes are drawn at: the kit draws them white; drawn at
		0.05 here they vanished. Full, as the kit shows them; rain keeps its
		own, which reads right on Backwash) */
		writef(c, getenv("HALO_H2_WEATHER_TEST") || !w->rain ? 1.0f : w->opacity[k]);
		writef(c + 4, w->rain ? 0.8f : 1.0f);
		writef(c + 8, w->rain ? 0.85f : 1.0f);
		writef(c + 12, w->rain ? 0.9f : 1.0f);
	}
	writef(o + 0x154, width > 0.001f && pixels_across > 0 ? 1.0f / (width * (float)pixels_across) : 1.0f);
	/* (rain along its fall, snow facing the viewer) */
	write16(o + 0x1A4, w->rain ? 1 : 0);
	write16(o + 0x1A6, 0);
	/* (rain additive - Halo 2's lines -, snow alpha blended) */
	write16(o + 0x1A8 + 0x2A, w->rain ? 3 : 0);
	/* its background plates: a type each, no particles, the flags' bit 30
	(weather_particle_systems.c draws it as a plane across the view at its
	distance, its texture tiled and moving); unused[1..] its distance, its
	move speed, its texture scale */
	for (k = 0; k < w->plate_count; k++)
	{
		struct h2fx_plate const *pl = &w->plates[k];
		unsigned long plate = type + 0x25C * (unsigned long)(k + 1);
		unsigned long plate_bitmap = tag_id(h2fx_bitmap_first + h2fx.sheets[pl->sheet].images[0]);
		int c;

		memcpy(region_at(r, plate + 0x1A8), region_at(r, kit_particle_root + PART_SHADER), 0xB4);
		region_reference(r, plate + 0x1A8 + 0x4C, 0, 0xFFFFFFFFUL);
		region_reference(r, plate + 0xAC, 0, 0xFFFFFFFFUL);
		region_reference(r, plate + 0x194, 'bitm', plate_bitmap);
		o = region_at(r, plate);
		snprintf((char *)o, 32, "h2 %s plate %d", w->rain ? "rain" : "snow", k);
		write32(o + 0x20, 0x40000000UL);
		writef(o + 0x48, pl->position);
		for (c = 0; c < 3; c++)
			writef(o + 0x4C + 4 * c, pl->speed[c]);
		writef(o + 0x58, pl->scale);
		writef(o + 0x5C, pl->blend[0]);
		writef(o + 0x60, pl->blend[1]);
		writef(o + 0x64, pl->z_near);
		writef(o + 0x68, pl->z_far);
		write32(o + 0x6C, pl->flags);
		writef(o + 0x70, pl->mass);
		writef(o + 0x2C, 1.0e6f);
		writef(o + 0x30, 1.0e6f);
		writef(o + 0x34, -10000.0f);
		writef(o + 0x38, -9999.0f);
		writef(o + 0x3C, 10000.0f);
		writef(o + 0x40, 10001.0f);
		writef(o + 0xA4, 0.0f);
		writef(o + 0xA8, 0.0f);
		for (c = 0; c < 2; c++)
		{
			unsigned char *color = o + 0x134 + 0x10 * c;

			writef(color, getenv("HALO_H2_WEATHER_TEST") ? 1.0f : pl->opacity);
			writef(color + 4, pl->tint[0]);
			writef(color + 8, pl->tint[1]);
			writef(color + 12, pl->tint[2]);
		}
		writef(o + 0x154, 1.0f);
		write16(o + 0x1A8 + 0x2A, w->rain ? 3 : 0);
		/* (its map tiled: wrapped, not the kit's clamp) */
		write16(o + 0x1A8 + 0x2E, 0);
	}
}

/* (bsp_write, after the clusters) the structure BSP's weather: its palette
the scenario's (its systems' tags: h2_effects_write), its clusters' entries
Halo 2's (the scenario's cluster data, 0x290: 0x34 each - the BSP 0, its
weather properties at 0x24, a short each cluster), its weather polyhedra
Halo 2's (sbsp 0x8C, 0x18 each: centre, radius, planes at 0x10) - where no
weather falls */
/* (h2_world_load.inc: the world read from the map writes the BSP's
weather palette again at load, from the level's weather tags, found by
their names "h2\\weather\\<name> <k>") */
H2_ALIGN void h2_effects_weather_tags_set(long count, long const *tag_indices)
{
	long k;

	h2fx_weather_tag_count = count > 16 ? 16 : count < 0 ? 0 : count;
	for (k = 0; k < h2fx_weather_tag_count; k++)
		h2fx_weather_tags[k] = tag_indices[k];
}

H2_ALIGN void h2_effects_weather_bsp_write(struct build *build, struct region *r, unsigned long root, unsigned long clusters, long cluster_count)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag scenario;
	long count, k, used = -1, poly_count;
	unsigned long data, weathers = 0, polys;
	long weather_count = 0;

	if (h2fx_weather_tag_count <= 0 || !h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) ||
		!h2_cache_block(cache, scenario.address + 0x290, 0x34, &count, &data))
	{
		return;
	}
	/* (this BSP's cluster data: the one naming it, else the only one) */
	for (k = 0; k < count; k++)
	{
		struct h2_cache_tag b;

		if (h2_cache_tag_get(cache, h2_cache_reference(cache, data + 0x34 * (unsigned long)k), &b) && b.address == build->sbsp)
			used = k;
	}
	if (used < 0 && count == 1)
		used = 0;
	/* (else the one with as many clusters: a campaign level's BSPs are
	loaded where their tags' addresses differ) */
	for (k = 0; k < count && used < 0; k++)
	{
		long n;
		unsigned long at;

		if (h2_cache_block(cache, data + 0x34 * (unsigned long)k + 0x24, 2, &n, &at) && n == cluster_count)
			used = k;
	}
	if (used < 0 || !h2_cache_block(cache, data + 0x34 * (unsigned long)used + 0x24, 2, &weather_count, &weathers))
		return;
	{
		unsigned long palette = region_block(r, root + 0x1B4, h2fx_weather_tag_count, 0xF0);

		for (k = 0; k < h2fx_weather_tag_count && !r->b.failed; k++)
		{
			snprintf((char *)region_at(r, palette + 0xF0 * (unsigned long)k), 32, "h2 weather %ld", k);
			region_reference(r, palette + 0xF0 * (unsigned long)k + 0x20, 'rain',
				h2fx_weather_tags[k] != NONE ? tag_id(h2fx_weather_tags[k]) : 0xFFFFFFFFUL);
			region_reference(r, palette + 0xF0 * (unsigned long)k + 0x80, 'wind', 0xFFFFFFFFUL);
			/* (HALO_H2_WEATHER_WIND=1: Halo 2's primary wind as this entry's
			wind direction and magnitude (0x90, 0x9C - point_physics.c blows
			the particles along it), the strength taken as metres a second) */
			if (getenv("HALO_H2_WEATHER_WIND") && k < h2fx.weather_count && h2fx.weathers[k].wind_strength > 0.0f)
			{
				unsigned char *e = region_at(r, palette + 0xF0 * (unsigned long)k);
				int c;

				for (c = 0; c < 3; c++)
					writef(e + 0x90 + 4 * c, h2fx.weathers[k].wind[c]);
				writef(e + 0x9C, h2fx.weathers[k].wind_strength / 3.048f);
				platform_log("h2 effects: weather %ld: wind %.2f %.2f %.2f at %.1f (%.2f world units a second)", k, h2fx.weathers[k].wind[0],
					h2fx.weathers[k].wind[1], h2fx.weathers[k].wind[2], h2fx.weathers[k].wind_strength, h2fx.weathers[k].wind_strength / 3.048f);
			}
		}
	}
	for (k = 0; k < cluster_count && !r->b.failed; k++)
	{
		short type = k < weather_count ? h2fx_read_short(cache, weathers + 2 * (unsigned long)k) : NONE;

		write16(region_at(r, clusters + 104 * (unsigned long)k + 8), type >= 0 && type < h2fx_weather_tag_count ? type : NONE);
	}
	if (h2_cache_block(cache, build->sbsp + 0x8C, 0x18, &poly_count, &polys) && poly_count > 0 && poly_count < 1024)
	{
		unsigned long out = region_block(r, root + 0x1C0, poly_count, 0x20);

		for (k = 0; k < poly_count && !r->b.failed; k++)
		{
			unsigned long q = polys + 0x18 * (unsigned long)k, planes, out_planes;
			long plane_count, n, c;

			for (c = 0; c < 4; c++)
				writef(region_at(r, out + 0x20 * (unsigned long)k + 4 * c), h2fx_read_real(cache, q + 4 * (unsigned long)c));
			if (!h2_cache_block(cache, q + 0x10, 0x10, &plane_count, &planes) || plane_count <= 0 || plane_count > 64)
				continue;
			out_planes = region_block(r, out + 0x20 * (unsigned long)k + 0x14, plane_count, 0x10);
			for (n = 0; n < plane_count && !r->b.failed; n++)
			{
				for (c = 0; c < 4; c++)
					writef(region_at(r, out_planes + 0x10 * (unsigned long)n + 4 * c), h2fx_read_real(cache, planes + 0x10 * (unsigned long)n + 4 * c));
			}
		}
	}
	platform_log("h2 effects: weather: %ld palette entries, %ld clusters given theirs (cluster data %ld), %ld polyhedra", h2fx_weather_tag_count,
		weather_count < cluster_count ? weather_count : cluster_count, used, poly_count);
}

/* ---------- private code: gathered */

H2_ALIGN static void h2_effects_gather(struct build *build)
{
	char const *setting = getenv("HALO_H2_EFFECTS");
	long first_bitmap = build->bitmap_count;

	free(h2fx.effects);
	free(h2fx.emitters);
	free(h2fx.sheets);
	free(h2fx.targets);
	free(h2fx.contrails);
	free(h2fx.lights);
	free(h2fx.lenses);
	free(h2fx.volumes);
	free(h2fx.parts);
	memset(&h2fx, 0, sizeof(h2fx));
	h2fx.tier = h2_particle_style_tier();
	h2fx_weather_tag_count = 0;
	if (getenv("HALO_H2_NO_EFFECTS") || (setting && !strcmp(setting, "off")) || !h2_cache_is_mcc(build->cache))
		return;
	h2fx.enabled = TRUE;
	h2fx_purposes_gather(build);
	h2fx_particle_effects(build);
	h2fx_weather_gather(build);
	h2fx_sky_lights_gather(build);
	{
		long bytes = 0, i;

		for (i = first_bitmap; i < build->bitmap_count; i++)
			bytes += (long)build->bitmaps[i].size;
		platform_log("h2 effects: %ld effects, %ld particles, %ld sprite sheets (%ld images, %ld KB as stored) for %ld places; "
			"%ld contrails, %ld attached particle systems (%ld more left out); left out: %ld beams, %ld distortion particles, %ld emitters, %ld light volumes, %ld boost attachments, %ld particle models", h2fx.effect_count,
			h2fx.emitter_count, h2fx.sheet_count, build->bitmap_count - first_bitmap, bytes / 1024, h2fx.target_count, h2fx.contrail_count, h2fx.child_count, h2fx.left_out_children,
			h2fx.left_out_beams, h2fx.left_out_distortion, h2fx.left_out_emitters, h2fx.left_out_light_volumes, h2fx.left_out_boost,
			h2fx.left_out_models);
		platform_log("h2 effects: %ld lights, %ld lens flares, %ld light volumes, %ld effect decals, %ld effects' parts (lights, decals); "
			"left out: %ld parts", h2fx.light_count, h2fx.lens_count, h2fx.volume_count, h2fx.effect_decal_count, h2fx.part_count,
			h2fx.left_out_parts);
	}
}

/* the made tags: an effect each place (a target), a particle each emitter */
H2_ALIGN static long h2_effects_tag_count(void)
{
	return h2fx.enabled ? h2fx.target_count + h2fx.emitter_count + h2fx.contrail_count + h2fx.light_count + h2fx.lens_count +
		h2fx.volume_count + h2fx.weather_count : 0;
}

H2_ALIGN static void h2_effects_tag_name(long index, unsigned long *group, char *text, long size)
{
	if (index < h2fx.target_count)
	{
		struct h2fx_target const *t = &h2fx.targets[index];

		*group = 'effe';
		/* (one found by name at run time: Halo 2's name) */
		if (t->effect == NONE)
			snprintf(text, size, "h2\\attachments\\%ld", index);
		else if (t->kind == _h2fx_target_named)
			snprintf(text, size, "%s", h2fx.effects[t->effect].name);
		else
			snprintf(text, size, "%s %ld", h2fx.effects[t->effect].name, index);
		return;
	}
	index -= h2fx.target_count;
	if (index >= h2fx.emitter_count + h2fx.contrail_count)
	{
		index -= h2fx.emitter_count + h2fx.contrail_count;
		if (index < h2fx.light_count)
		{
			*group = 'ligh';
			snprintf(text, size, "h2\\lights\\%s %ld", h2fx.lights[index].name, index);
		}
		else if (index < h2fx.light_count + h2fx.lens_count)
		{
			index -= h2fx.light_count;
			*group = 'lens';
			snprintf(text, size, "h2\\lens flares\\%s %ld", h2fx.lenses[index].name, index);
		}
		else if (index >= h2fx.light_count + h2fx.lens_count + h2fx.volume_count)
		{
			index -= h2fx.light_count + h2fx.lens_count + h2fx.volume_count;
			*group = 'rain';
			snprintf(text, size, "h2\\weather\\%s %ld", h2fx.weathers[index].name, index);
		}
		else
		{
			index -= h2fx.light_count + h2fx.lens_count;
			*group = 'mgs2';
			snprintf(text, size, "h2\\light volumes\\%s %ld", h2fx.volumes[index].name, index);
		}
		return;
	}
	if (index >= h2fx.emitter_count)
	{
		*group = 'cont';
		snprintf(text, size, "h2\\contrails\\%s", tag_short_name(h2fx.contrails[index - h2fx.emitter_count].h2_tag != NONE ? "contrail" : ""));
		snprintf(text, size, "h2\\contrails\\%ld", index - h2fx.emitter_count);
		return;
	}
	*group = 'part';
	snprintf(text, size, "h2\\particles\\%s %ld", h2fx.emitters[index].name, index);
}

/* ---------- private code: written */

/* a tag of the level's by group and name (its index), NONE */
H2_ALIGN static long h2fx_tag_named(struct region *r, unsigned long instances, long tag_count, unsigned long group, char const *name)
{
	long i;

	for (i = 0; i < tag_count; i++)
	{
		unsigned char *instance = region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)i);
		unsigned long address;

		if (read32(instance) != group || !(address = read32(instance + 16)) || address - r->link >= r->b.size)
			continue;
		if (!strcmp((char const *)region_at(r, address - r->link), name))
			return i;
	}
	return NONE;
}

/* the first of the level's tags of a group whose name has any of the
names in it (NULL-ended), NONE */
H2_ALIGN static long h2fx_tag_like(struct region *r, unsigned long instances, long tag_count, unsigned long group,
	char const *const *names)
{
	long k, i;

	for (k = 0; names[k]; k++)
	{
		for (i = 0; i < tag_count; i++)
		{
			unsigned char *instance = region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)i);
			unsigned long address;

			if (read32(instance) != group || !(address = read32(instance + 16)) || address - r->link >= r->b.size ||
				!read32(instance + 20))
			{
				continue;
			}
			if (strstr((char const *)region_at(r, address - r->link), names[k]))
				return i;
		}
	}
	return NONE;
}

/* the kit's tags the particles copy (a particle's fields, its shader's
type) and move with (point physics) */
H2_ALIGN static void h2fx_kit_find(struct region *r, unsigned long instances, long tag_count)
{
	static char const *const particle[] = { "effects\\particles\\", "", NULL };
	static char const *const any[] = { "", NULL };
	long i, k;

	h2fx.kit_particle = h2fx_tag_like(r, instances, tag_count, 'part', particle);
	{
		static char const *const contrail[] = { "weapons\\assault rifle\\bullet", "", NULL };

		h2fx.kit_contrail = h2fx_tag_like(r, instances, tag_count, 'cont', contrail);
	}
	for (i = 0; i < NUMBER_OF_H2FX_PHYSICS; i++)
	{
		h2fx.kit_physics[i] = NONE;
		for (k = 0; k < 3 && h2fx_physics_names[i][k] && h2fx.kit_physics[i] == NONE; k++)
			h2fx.kit_physics[i] = h2fx_tag_named(r, instances, tag_count, 'pphy', h2fx_physics_names[i][k]);
	}
	if (h2fx.kit_physics[_h2fx_physics_drift] == NONE)
		h2fx.kit_physics[_h2fx_physics_drift] = h2fx_tag_like(r, instances, tag_count, 'pphy', any);
	for (i = 1; i < NUMBER_OF_H2FX_PHYSICS; i++)
	{
		if (h2fx.kit_physics[i] == NONE)
			h2fx.kit_physics[i] = h2fx.kit_physics[_h2fx_physics_drift];
	}
	if (getenv("HALO_H2_DEBUG"))
	{
		for (i = 0; i < tag_count; i++)
		{
			unsigned char *instance = region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)i);
			unsigned long group = read32(instance), address = read32(instance + 16);

			if ((group == 'pphy' || group == 'part' || group == 'pctl' || group == 'cont') && address && address - r->link < r->b.size)
				fprintf(stderr, "h2 effects: kit %c%c%c%c %ld %s\n", (int)(group >> 24), (int)(group >> 16 & 0xFF), (int)(group >> 8 & 0xFF),
					(int)(group & 0xFF), i, (char const *)region_at(r, address - r->link));
		}
	}
	for (i = 0; i < NUMBER_OF_H2FX_PHYSICS && getenv("HALO_H2_DEBUG"); i++)
	{
		unsigned long root = h2fx.kit_physics[i] != NONE ? h2w_root(r, instances, h2fx.kit_physics[i]) : 0;

		if (root)
			fprintf(stderr, "h2 effects: physics %ld: flags %lx mass/r3 %g water %g air buoyancy %g density %g air friction %g\n", i,
				read32(region_at(r, root)), readf(region_at(r, root + 4)), readf(region_at(r, root + 8)), readf(region_at(r, root + 12)),
				readf(region_at(r, root + 0x20)), readf(region_at(r, root + 0x24)));
	}
	platform_log("h2 effects: particles copy %s; point physics: %ld %ld %ld %ld %ld %ld",
		h2fx.kit_particle != NONE ? (char const *)region_at(r, read32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)h2fx.kit_particle + 16)) - r->link) : "(none)",
		h2fx.kit_physics[0], h2fx.kit_physics[1], h2fx.kit_physics[2], h2fx.kit_physics[3], h2fx.kit_physics[4], h2fx.kit_physics[5]);
}

/* an emitter's particle tag written (its root at root, a copy of the kit's) */
H2_ALIGN static void h2fx_particle_write(struct region *r, unsigned long root, struct h2fx_emitter const *m, unsigned long bitmap_id,
	long pixels_across, long first)
{
	struct h2fx_sheet const *sh = &h2fx.sheets[m->sheet];
	struct h2fx_sprite const *sprite = &sh->sprites[sh->sequences[m->first_sequence].first];
	float width = sprite->bounds[1] - sprite->bounds[0];
	long physics = h2fx.kit_physics[m->physics] != NONE ? h2fx.kit_physics[m->physics] : h2fx.kit_physics[0];
	unsigned char *p;

	region_reference(r, root + PART_BITMAP, 'bitm', bitmap_id);
	region_reference(r, root + PART_PHYSICS, 'pphy', physics != NONE ? tag_id(physics) : 0xFFFFFFFFUL);
	region_reference(r, root + PART_COLLISION_MATERIAL_EFFECTS, 0, 0xFFFFFFFFUL);
	region_reference(r, root + PART_COLLISION_EFFECT, m->collision_target != NONE ? 'effe' : 0,
		m->collision_target != NONE ? tag_id(first + m->collision_target) : 0xFFFFFFFFUL);
	region_reference(r, root + PART_EFFECT, m->death_target != NONE ? 'effe' : 0,
		m->death_target != NONE ? tag_id(first + m->death_target) : 0xFFFFFFFFUL);
	region_reference(r, root + PART_SHADER_SECONDARY_MAP, 0, 0xFFFFFFFFUL);
	p = region_at(r, root);
	write32(p + PART_FLAGS, m->flags);
	write32(p + PART_MESH, 0);
	writef(p + PART_LIFE, m->life[0]);
	writef(p + PART_LIFE + 4, m->life[1]);
	writef(p + PART_FADE_IN, 0.0f);
	writef(p + PART_FADE_OUT, 0.0f);
	writef(p + PART_MINIMUM_PIXELS, 0.0f);
	writef(p + PART_RADIUS, 1.0f);
	writef(p + PART_RADIUS + 4, 1.0f);
	writef(p + PART_FRAMES_PER_SECOND, m->frames_per_second > 60.0f ? 60.0f : m->frames_per_second);
	writef(p + PART_FRAMES_PER_SECOND + 4, m->frames_per_second > 60.0f ? 60.0f : m->frames_per_second);
	writef(p + PART_CONTACT_DETERIORATION, 0.0f);
	writef(p + PART_LOD_FALLOFF, 0.0f);
	writef(p + PART_LOD_CUTOFF, 0.0f);
	write16(p + PART_FIRST_SEQUENCE, m->first_sequence);
	write16(p + PART_INITIAL_SEQUENCES, m->sequence_count);
	write16(p + PART_LOOPING_SEQUENCES, 0);
	write16(p + PART_FINAL_SEQUENCES, 0);
	/* (the renderer's sprite is its bounds times its bitmap's width in pixels,
	over the sprite's longer side (build_sprite's true aspect): one over the
	bitmap's pixels across makes every sprite 2 x its size long, Halo 2's rule
	(decomp function_224550: corners -1..1 x size), not only the sheet's first) */
	(void)width;
	writef(p + PART_OO_WIDTH, pixels_across > 0 ? 1.0f / (float)pixels_across : 1.0f);
	write16(p + PART_ORIENTATION, m->orientation);
	/* (Halo 2's additive particles: their bitmap's alpha masks their
	colour - this engine's shader flag 15, render: h2_particle_styles.h) */
	/* (and a particle_additive_tint's pass, white staying white: flag 14 -
	rasterizer_xbox_transparent_geometry.c) */
	write16(p + PART_SHADER_FLAGS, (m->blend == 3 ? (1 << 15) : 0) | (m->tint_white_pass ? (1 << 14) : 0) |
		(m->distortion ? (1 << 13) : 0) | (m->mesh != NONE && m->plasma_sheet != NONE ? (1 << 12) : 0));
	write16(p + PART_SHADER_BLEND, m->blend);
	write16(p + PART_SHADER_FADE, 0);
	write16(p + PART_SHADER_MAP_FLAGS, 0);
	memset(p + PART_SHADER_RESERVED, 0, 28);
	memset(p + PART_SHADER_RESERVED_AFTER, 0, 20);
	if (m->mesh != NONE && m->plasma_sheet != NONE)
	{
		int k;

		region_reference(r, root + PART_SHADER_SECONDARY_MAP, 'bitm', tag_id(h2fx_bitmap_first + h2fx.sheets[m->plasma_sheet].images[0]));
		p = region_at(r, root);
	}
	h2_particle_style_write(p, &m->style);
	/* (a particle_plasma model's colours and scales: past the style's samples,
	which fill the shader's reserved bytes, written after it: under them the
	renderer read white and no scale - one texel of each noise, white rings) */
	if (m->mesh != NONE && m->plasma_sheet != NONE)
	{
		int k;

		for (k = 0; k < 3; k++)
			write32(p + PART_SHADER_RESERVED_AFTER + 8 + 4 * k, m->plasma_colours[k]);
		write16(p + PART_SHADER_RESERVED + 24, (unsigned short)(m->plasma_scale[0] * 256.0f));
		write16(p + PART_SHADER_RESERVED + 26, (unsigned short)(m->plasma_scale[1] * 256.0f));
	}
	if (m->child != NONE)
	{
		struct h2fx_emitter const *c = &h2fx.emitters[m->child];
		struct h2_particle_child child;

		child.definition_index = (long)tag_id(first + h2fx.target_count + m->child);
		child.rate = c->child_rate;
		child.velocity[0] = c->velocity[0];
		child.velocity[1] = c->velocity[1];
		child.cone = c->cone;
		child.radius[0] = c->radius[0];
		child.radius[1] = c->radius[1];
		h2_particle_style_child_write(p, &child);
	}
	/* (a distortion particle's scale: its shader's reserved bytes after the
	alpha samples - rasterizer_xbox_transparent_geometry.c reads it) */
	if (m->distortion)
		writef(p + PART_SHADER_RESERVED_AFTER + 8, m->distortion_scale);
}

/* an effect's location by name in a locations list being made (added) */
H2_ALIGN static short h2fx_name_index(char (*names)[32], long *count, char const *name)
{
	long i;

	for (i = 0; i < *count; i++)
	{
		if (!strncmp(names[i], name, 31))
			return (short)i;
	}
	if (*count >= MAXIMUM_EFFECT_LOCATIONS)
		return 0;
	snprintf(names[*count], 32, "%s", name);
	return (short)(*count)++;
}

/* an effect made of Halo 2's (h2fx.effects[index]) and, at old (an effect
root of r, 0 none), this engine's effect it stands in for: that one's
parts (sounds, lights, damage, decals - not its particle systems) kept
event by event at their locations, Halo 2's particles; its root */
/* (h2fx_effect_write: the effect looped from its first event whatever
Halo 2's loop - an object's attachment) */
static boolean h2fx_loop_all;
/* (set while a level object's attachment is written: its locations at the
object's origin - h2_level.c writes the level objects' models without
Halo 2's markers, so a location named for one (the fires' "marker") found
none and the effect made nothing: 03a's burning rubble. HALO_H2_OBJECT_MARKERS_OLD
keeps the names) */
static boolean h2fx_origin_locations;
static char const *h2fx_origin_markers;   /* (its object's markers: those kept) */
/* (h2fx_effect_write: its particles' counts scaled by the effect's A scale -
an attachment's scale function) */
static boolean h2fx_scale_counts;

/* whether the stand-in effect's part of a group is kept: not its particles,
nor its lights or decals when Halo 2's effect has its own */
H2_ALIGN static boolean h2fx_old_part_kept(struct h2fx_effect const *e, unsigned long group)
{
	if (group == 'pctl' || group == 'part')
		return FALSE;
	if (group == 'ligh' && e->has_lights)
		return FALSE;
	if (group == 'deca' && e->has_decals)
		return FALSE;
	return TRUE;
}

H2_ALIGN static unsigned long h2fx_effect_write(struct region *r, short index, unsigned long old, long particle_first, boolean once)
{
	struct h2fx_effect const *e = &h2fx.effects[index];
	char names[MAXIMUM_EFFECT_LOCATIONS][32];
	short old_map[MAXIMUM_EFFECT_LOCATIONS];
	long name_count = 0, i, k, event_count, old_event_count = 0, old_location_count = 0;
	unsigned long root, events, old_events = 0, old_locations = 0, locations;

	/* (whole: the names are written 32 bytes each, a short one's tail the
	stack's garbage otherwise - two machines' levels then differ) */
	memset(names, 0, sizeof(names));
	for (i = 0; i < e->location_count && i < MAXIMUM_EFFECT_LOCATIONS; i++)
		h2fx_name_index(names, &name_count, e->locations[i]);
	if (old)
	{
		old_location_count = (long)read32(region_at(r, old + H2FX_EFFECT_LOCATIONS));
		old_locations = read32(region_at(r, old + H2FX_EFFECT_LOCATIONS + 4));
		old_event_count = (long)read32(region_at(r, old + H2FX_EFFECT_EVENTS));
		old_events = read32(region_at(r, old + H2FX_EFFECT_EVENTS + 4));
		if (old_location_count < 0 || old_location_count > MAXIMUM_EFFECT_LOCATIONS || !old_locations)
			old_location_count = 0;
		if (old_event_count < 0 || old_event_count > MAXIMUM_EFFECT_EVENTS || !old_events)
			old_event_count = 0;
		for (i = 0; i < old_location_count; i++)
		{
			char name[32];

			memcpy(name, region_at(r, old_locations - r->link + H2FX_LOCATION_SIZE * (unsigned long)i), 31);
			name[31] = 0;
			old_map[i] = h2fx_name_index(names, &name_count, name);
		}
	}
	event_count = e->event_count > old_event_count ? e->event_count : old_event_count;
	if (event_count > MAXIMUM_EFFECT_EVENTS)
		event_count = MAXIMUM_EFFECT_EVENTS;
	root = region_alloc(r, H2FX_EFFECT_SIZE);
	write32(region_at(r, root), e->flags);
	writef(region_at(r, root + 0x0C), e->play_always);
	writef(region_at(r, root + 0x10), e->play_never);
	write16(region_at(r, root + 4), h2fx_loop_all ? 0 : !once && e->loop_start >= 0 && e->loop_start < event_count ? e->loop_start : NONE);
	write16(region_at(r, root + 6), h2fx_loop_all || (!once && e->loop_start >= 0 && e->loop_start < event_count) ? (short)(event_count - 1) : NONE);
	locations = region_block(r, root + H2FX_EFFECT_LOCATIONS, name_count, H2FX_LOCATION_SIZE);
	for (i = 0; i < name_count && !r->b.failed; i++)
	{
		/* (a level object's: a location its model has a marker for kept,
		the others at its origin) */
		char key[40];
		boolean keep;

		snprintf(key, sizeof(key), ";%s;", names[i]);
		keep = !h2fx_origin_locations || (h2fx_origin_markers && names[i][0] && strstr(h2fx_origin_markers, key));
		memcpy(region_at(r, locations + H2FX_LOCATION_SIZE * (unsigned long)i), keep ? names[i] : "", keep ? 32 : 1);
	}
	events = region_block(r, root + H2FX_EFFECT_EVENTS, event_count, H2FX_EVENT_SIZE);
	for (k = 0; k < event_count && !r->b.failed; k++)
	{
		unsigned long event = events + H2FX_EVENT_SIZE * (unsigned long)k, old_event = 0, parts, entries;
		long part_count = 0, old_part_count = 0, entry_count = 0, n;
		unsigned long old_parts = 0;

		if (k < old_event_count)
		{
			old_event = old_events - r->link + H2FX_EVENT_SIZE * (unsigned long)k;
			old_part_count = (long)read32(region_at(r, old_event + H2FX_EVENT_PARTS));
			old_parts = read32(region_at(r, old_event + H2FX_EVENT_PARTS + 4));
			if (old_part_count < 0 || old_part_count > MAXIMUM_EFFECT_PARTS_PER_EVENT || !old_parts)
				old_part_count = 0;
			for (n = 0; n < old_part_count; n++)
			{
				unsigned long group = read32(region_at(r, old_parts - r->link + H2FX_PART_SIZE * (unsigned long)n + H2FX_PART_REFERENCE));

				if (h2fx_old_part_kept(e, group))
					part_count++;
				if (getenv("HALO_H2_EFFECTS_DUMP") && strstr(e->name, getenv("HALO_H2_EFFECTS_DUMP")))
				{
					unsigned char const *q = region_at(r, old_parts - r->link + H2FX_PART_SIZE * (unsigned long)n);

					fprintf(stderr, "h2 effects: %s stand-in event %ld part %ld: %c%c%c%c (runtime %08lx, index %04x) flags %x velocity %.2f..%.2f cone %.2f radius %.2f..%.2f\n",
						e->name, k, n, (int)(group >> 24), (int)(group >> 16 & 255), (int)(group >> 8 & 255), (int)(group & 255),
						(unsigned long)read32(q + 0x14), (unsigned)read16(q + 0x18 + 12), read16(q + 6),
						readf(q + 0x40), readf(q + 0x44), readf(q + 0x48), readf(q + 0x54), readf(q + 0x58));
				}
			}
		}
		/* (Halo 2's own parts of this event: its lights and decals) */
		for (n = 0; n < e->part_count; n++)
		{
			if (h2fx.parts[e->first_part + n].event == k)
				part_count++;
		}
		if (part_count > MAXIMUM_EFFECT_PARTS_PER_EVENT)
			part_count = MAXIMUM_EFFECT_PARTS_PER_EVENT;
		/* its timing: Halo 2's, else the old one's */
		if (k < e->event_count)
		{
			struct h2fx_event const *ev = &e->events[k];
			unsigned char *o = region_at(r, event);

			writef(o + 4, ev->skip);
			writef(o + 8, ev->delay[0]);
			writef(o + 0xC, ev->delay[1]);
			writef(o + 0x10, ev->duration[0]);
			writef(o + 0x14, ev->duration[1]);
			/* (its accelerations: effects.c's effect_h2_acceleration) */
			if (ev->acceleration != 0.0f && !h2fx_origin_locations)
			{
				write32(o + 0x18, H2_EFFECT_ACCELERATION_MARK);
				writef(o + 0x1C, ev->acceleration);
				writef(o + 0x20, ev->cone[0]);
				writef(o + 0x24, ev->cone[1]);
				write16(o + 0x28, (unsigned short)(ev->acceleration_location[0] & 0xFFFF));
				write16(o + 0x2A, (unsigned short)(ev->acceleration_location[1] & 0xFFFF));
			}
		}
		else
			memcpy(region_at(r, event), region_at(r, old_event), 0x18);
		/* the old one's parts but its particle systems */
		parts = region_block(r, event + H2FX_EVENT_PARTS, part_count, H2FX_PART_SIZE);
		for (n = 0, i = 0; n < old_part_count && !r->b.failed; n++)
		{
			unsigned long from = old_parts - r->link + H2FX_PART_SIZE * (unsigned long)n;
			unsigned long group = read32(region_at(r, from + H2FX_PART_REFERENCE));
			short location;

			if (!h2fx_old_part_kept(e, group) || i >= part_count)
				continue;
			memcpy(region_at(r, parts + H2FX_PART_SIZE * (unsigned long)i), region_at(r, from), H2FX_PART_SIZE);
			location = read16(region_at(r, from + 4));
			write16(region_at(r, parts + H2FX_PART_SIZE * (unsigned long)i + 4),
				location >= 0 && location < old_location_count ? old_map[location] : 0);
			i++;
		}
		for (n = 0; n < e->part_count && i < part_count && !r->b.failed; n++)
		{
			struct h2fx_part const *q = &h2fx.parts[e->first_part + n];
			unsigned long at = parts + H2FX_PART_SIZE * (unsigned long)i;
			unsigned char *o;

			if (q->event != k)
				continue;
			if (q->group == 'garb')
			{
				/* (a crate thrown: the debris made of it, an object part) */
				char debris_name[96];
				long debris_tag;

				snprintf(debris_name, sizeof(debris_name), "h2\\vehicles\\debris\\%s", h2v.debris[q->index]->name);
				debris_tag = h2fx_tag_named(r, h2fx_write_instances, h2fx_write_tag_count, 'garb', debris_name);
				region_reference(r, at + H2FX_PART_REFERENCE, 'garb', debris_tag != NONE ? tag_id(debris_tag) : 0xFFFFFFFFUL);
			}
			else if (q->group == 'snd!')
			{
				/* (one of Halo 2's sounds, h2_weapons.c's sound tags by name) */
				char sound_name[160];
				long sound_tag;

				snprintf(sound_name, sizeof(sound_name), "sound\\h2\\%s", h2w.sounds[q->index]);
				sound_tag = h2fx_tag_named(r, h2fx_write_instances, h2fx_write_tag_count, 'snd!', sound_name);
				region_reference(r, at + H2FX_PART_REFERENCE, 'snd!', sound_tag != NONE ? tag_id(sound_tag) : 0xFFFFFFFFUL);
			}
			else
				region_reference(r, at + H2FX_PART_REFERENCE, q->group,
					tag_id(q->group == 'ligh' ? h2fx_light_first + q->index : h2fx_decal_first + q->index));
			o = region_at(r, at);
			write16(o + 0, q->environment);
			write16(o + 2, q->disposition);
			write16(o + 4, q->location < e->location_count ? q->location : 0);
			write16(o + 6, q->flags);
			write32(o + 0x14, q->group == 'garb' ? 'obje' : q->group);
			writef(o + 0x40, q->velocity[0]);
			writef(o + 0x44, q->velocity[1]);
			writef(o + 0x48, q->cone);
			writef(o + 0x4C, q->angular_velocity[0]);
			writef(o + 0x50, q->angular_velocity[1]);
			writef(o + 0x54, q->radius_modifier[0]);
			writef(o + 0x58, q->radius_modifier[1]);
			write32(o + 0x60, q->scale_a);
			write32(o + 0x64, q->scale_b);
			if (getenv("HALO_H2_EFFECTS_DUMP") && strstr(e->name, getenv("HALO_H2_EFFECTS_DUMP")))
				fprintf(stderr, "h2 effects: %s event %ld part %ld: %c%c%c%c %d at location %d (of %ld), flags %x velocity %.2f..%.2f "
					"radius %.2f..%.2f, %ld parts in all\n", e->name, k, i, (int)(q->group >> 24), (int)(q->group >> 16 & 255),
					(int)(q->group >> 8 & 255), (int)(q->group & 255), q->index, q->location, e->location_count, q->flags, q->velocity[0],
					q->velocity[1], q->radius_modifier[0], q->radius_modifier[1], part_count);
			i++;
		}
		/* Halo 2's particles: an entry each emitter of this event */
		for (n = 0; n < e->emitter_count; n++)
		{
			if (h2fx.emitters[e->first_emitter + n].event == k)
				entry_count += h2fx.emitters[e->first_emitter + n].repeat > 0 ? h2fx.emitters[e->first_emitter + n].repeat : 1;
		}
		if (entry_count > MAXIMUM_EFFECT_PARTICLES_PER_EVENT)
			entry_count = MAXIMUM_EFFECT_PARTICLES_PER_EVENT;
		entries = region_block(r, event + H2FX_EVENT_PARTICLES, entry_count, H2FX_PARTICLES_SIZE);
		for (n = 0, i = 0; n < e->emitter_count && i < entry_count && !r->b.failed; n++)
		{
			long emitter = e->first_emitter + n;
			struct h2fx_emitter const *m = &h2fx.emitters[emitter];
			unsigned long entry = entries + H2FX_PARTICLES_SIZE * (unsigned long)i;
			unsigned char *o;
			float yaw = m->direction[0], pitch = m->direction[1];
			int c;

			if (m->event != k)
				continue;
			region_reference(r, entry + 0x54, 'part', tag_id(particle_first + emitter));
			o = region_at(r, entry);
			write16(o + 0, m->environment);
			write16(o + 2, m->disposition);
			write16(o + 4, m->camera_mode);
			write16(o + 8, m->location < e->location_count ? m->location : 0);
			writef(o + 0xC, yaw);
			writef(o + 0x10, pitch);
			writef(o + 0x14, m->offset[0]);
			writef(o + 0x18, m->offset[1]);
			writef(o + 0x1C, m->offset[2]);
			writef(o + 0x20, cosf(yaw) * cosf(pitch));
			writef(o + 0x24, sinf(yaw) * cosf(pitch));
			writef(o + 0x28, sinf(pitch));
			/* (attached: it moves with its marker - a muzzle flash; random
			orientation: a screen-facing sprite turned any way) */
			write32(o + 0x64, (m->attached ? 1UL : 0) | (m->orientation == 0 && m->spins ? 2UL : 0));
			write16(o + 0x68, m->distribution);
			/* (Halo 2's emission shape, for effects.c's placing of the
			particle - a line along the direction, a disc or tube's disc
			across it, a halo's ring -, marked as Halo 2's: no Halo CE
			effect has the mark, and it places as before) */
			write16(o + 0x6A, m->shape);
			write32(o + 0x78, H2_EFFECT_SHAPE_MARK);
			write16(o + 0x6C, m->count[0]);
			write16(o + 0x6E, m->count[1]);
			/* (a looped effect's rate under a particle a tick - the Ghost's
			antigravity plasma's 0.5, its sparks' 0.75, the damage smokes':
			each tick's one a chance of that, effects.c. The count rounded up
			drew them each tick, a lavender haze under a driven Ghost where
			MCC has thin lines of sparks) */
			if (m->chance > 0.0f && (h2fx_loop_all || e->loop_start != NONE))
			{
				write16(o + 0x6C, 1);
				write16(o + 0x6E, 1);
				writef(o + 0x7C, m->chance);
			}
			if (h2fx_scale_counts)
				write32(o + 0xE0, 1UL << 5);   /* (count scaled by A: the attachment's scale) */
			writef(o + 0x70, m->emission_radius[0]);
			writef(o + 0x74, m->emission_radius[1]);
			writef(o + 0x84, m->velocity[0]);
			writef(o + 0x88, m->velocity[1]);
			writef(o + 0x8C, m->cone);
			writef(o + 0x90, m->angular_velocity[0]);
			writef(o + 0x94, m->angular_velocity[1]);
			writef(o + 0xA0, m->radius[0]);
			writef(o + 0xA4, m->radius[1]);
			/* (additive: its alpha its brightness - this engine draws an
			additive sprite's alpha as its fade only) */
			for (c = 0; c < 4; c++)
			{
				float lower = m->tint[0][c], upper = m->tint[1][c];

				if (c > 0 && m->blend == 3)
					lower *= m->tint[0][0], upper *= m->tint[1][0];
				writef(o + 0xB0 + 4 * c, lower);
				writef(o + 0xC0 + 4 * c, upper);
			}
			i++;
			/* (more than an entry's count: copies of it) */
			for (c = 1; c < m->repeat && i < entry_count; c++, i++)
				memcpy(region_at(r, entries + H2FX_PARTICLES_SIZE * (unsigned long)i), region_at(r, entry), H2FX_PARTICLES_SIZE);
		}
	}
	return root;
}

/* a field's tag reference's tag index (NONE none) */
H2_ALIGN static long h2fx_referenced(struct region *r, unsigned long field)
{
	return read32(region_at(r, field)) == 0xFFFFFFFFUL ? NONE : (short)read16(region_at(r, field + 12));
}

/* a contrail's tag written (its root a copy of the kit's: its shader's
type and fields) */
H2_ALIGN static void h2fx_contrail_write(struct region *r, unsigned long root, struct h2fx_contrail const *c, unsigned long bitmap_id)
{
	unsigned long states;
	long i;
	int k;

	memcpy(region_at(r, root), c->fields, 0x30);
	region_reference(r, root + 0x30, 'bitm', bitmap_id);
	write16(region_at(r, root + 0x40), c->first_sequence);
	write16(region_at(r, root + 0x42), c->sequence_count);
	memset(region_at(r, root + 0x44), 0, 64);
	write16(region_at(r, root + 0x84 + 0x28), 0);
	write16(region_at(r, root + 0x84 + 0x2A), c->blend);
	write16(region_at(r, root + 0x84 + 0x2C), c->fade);
	write16(region_at(r, root + 0x84 + 0x2E), 0);
	/* (the shader's secondary map none: 0x4C of it) */
	region_reference(r, root + 0x84 + 0x4C, 0, 0xFFFFFFFFUL);
	states = region_block(r, root + 0x138, c->state_count, 0x68);
	for (i = 0; i < c->state_count && !r->b.failed; i++)
	{
		struct h2fx_contrail_state const *st = &c->states[i];
		unsigned long o = states + 0x68 * (unsigned long)i;
		long physics = h2fx.kit_physics[st->physics] != NONE ? h2fx.kit_physics[st->physics] : h2fx.kit_physics[0];

		writef(region_at(r, o), st->duration[0]);
		writef(region_at(r, o + 4), st->duration[1]);
		writef(region_at(r, o + 8), st->transition[0]);
		writef(region_at(r, o + 0xC), st->transition[1]);
		region_reference(r, o + 0x10, 'pphy', physics != NONE ? tag_id(physics) : 0xFFFFFFFFUL);
		writef(region_at(r, o + 0x40), st->width * h2_particle_style_size_scale());
		for (k = 0; k < 4; k++)
		{
			writef(region_at(r, o + 0x44 + 4 * k), st->color[0][k]);
			writef(region_at(r, o + 0x54 + 4 * k), st->color[1][k]);
		}
		write32(region_at(r, o + 0x64), st->scale_flags);
	}
}

/* a projectile's attachments made again: its lights, sounds and light
volumes kept, Halo 2's effects and contrails (the targets so named, from
first) instead of the rest */
H2_ALIGN static void h2fx_attachments_write(struct region *r, unsigned long root, long first, long target_first, long contrail_first,
	char const *name)
{
	unsigned long field = root + offsetof(struct projectile_definition, object.attachments), old, block;
	long old_count = (long)read32(region_at(r, field)), kept = 0, added = 0, i, n;
	unsigned char kept_entries[12][72];
	/* (the stand-in's contrails' and effects' scale functions: Halo 2's
	attachments of the kind scale as they did - a vehicle's speed) */
	unsigned char contrail_scales[8], effect_scales[8], light_scales[8];

	memset(light_scales, 0, sizeof(light_scales));
	memset(contrail_scales, 0, sizeof(contrail_scales));
	memset(effect_scales, 0, sizeof(effect_scales));
	boolean own_lights = FALSE, own_volumes = FALSE, lights_off = FALSE;

	for (i = first; i < h2fx.target_count; i++)
	{
		if (h2fx.targets[i].kind == _h2fx_target_projectile_attachments && !strcmp(h2fx.targets[i].name, name))
		{
			own_lights |= h2fx.targets[i].light != NONE;
			own_volumes |= h2fx.targets[i].volume != NONE;
			lights_off |= h2fx.targets[i].off;
		}
	}
	old = read32(region_at(r, field + 4));
	for (i = 0; i < old_count && old; i++)
	{
		unsigned char *e = region_at(r, old - r->link + 72 * (unsigned long)i);
		unsigned long group = read32(e);

		if (getenv("HALO_H2_DEBUG"))
			fprintf(stderr, "h2 effects: %s stand-in attachment %c%c%c%c at '%.31s' scales %d %d color %d\n", name, (int)(group >> 24) & 0xFF,
				(int)(group >> 16) & 0xFF, (int)(group >> 8) & 0xFF, (int)group & 0xFF, (char const *)e + 0x10, read16(e + 0x30), read16(e + 0x32),
				read16(e + 0x34));
		if (group == 'ligh' && !light_scales[0] && !light_scales[2])
			memcpy(light_scales, e + 0x30, 8);
		/* (the stand-in's lights and light volumes: not where Halo 2's
		object has its own) */
		if ((group == 'ligh' && own_lights) || (group == 'mgs2' && own_volumes))
			continue;
		/* (the stand-in's headlights, off with Halo 2's) */
		if (group == 'ligh' && lights_off && strstr((char const *)e + 0x10, "headlight"))
			continue;
		if ((group == 'ligh' || group == 'lsnd' || group == 'mgs2' || group == 'lens') && kept < 12)
			memcpy(kept_entries[kept++], e, 72);
		else if (group == 'cont' && !contrail_scales[0] && !contrail_scales[2])
			memcpy(contrail_scales, e + 0x30, 8);
		else if ((group == 'effe' || group == 'pctl') && !effect_scales[0] && !effect_scales[2])
			memcpy(effect_scales, e + 0x30, 8);
	}
	for (i = first; i < h2fx.target_count; i++)
	{
		if (h2fx.targets[i].kind == _h2fx_target_projectile_attachments && !strcmp(h2fx.targets[i].name, name) &&
			h2fx.targets[i].volume == NONE && !h2fx.targets[i].off)
			added++;
	}
	/* (no more than an object holds - 8: the tag check cut the rest
	anyway, the last ones; Delta Towers' gondola has 9 - the stand-in's
	lights and sounds first, then Halo 2's in their order) */
	if (kept > MAXIMUM_NUMBER_OF_ATTACHMENTS_PER_OBJECT)
		kept = MAXIMUM_NUMBER_OF_ATTACHMENTS_PER_OBJECT;
	if (kept + added > MAXIMUM_NUMBER_OF_ATTACHMENTS_PER_OBJECT)
	{
		platform_log("h2 effects: %s: %ld attachments, %d kept (an object's most)", name, kept + added,
			MAXIMUM_NUMBER_OF_ATTACHMENTS_PER_OBJECT);
		added = MAXIMUM_NUMBER_OF_ATTACHMENTS_PER_OBJECT - kept;
	}
	block = region_block(r, field, kept + added, 72);
	for (i = 0; i < kept && !r->b.failed; i++)
		memcpy(region_at(r, block + 72 * (unsigned long)i), kept_entries[i], 72);
	for (i = first, n = kept; i < h2fx.target_count && n < kept + added && !r->b.failed; i++)
	{
		struct h2fx_target const *t = &h2fx.targets[i];
		unsigned long o;

		if (t->kind != _h2fx_target_projectile_attachments || strcmp(t->name, name) || t->volume != NONE || t->off)
			continue;
		o = block + 72 * (unsigned long)n++;
		if (getenv("HALO_H2_DEBUG"))
			fprintf(stderr, "h2 effects: %s attachment at '%s' by '%s': effect %d contrail %d light %d\n", name, t->marker, t->scale, t->effect,
				t->contrail, t->light);
		if (t->contrail != NONE)
			region_reference(r, o, 'cont', tag_id(contrail_first + t->contrail));
		else if (t->light != NONE)
			region_reference(r, o, 'ligh', tag_id(h2fx_light_first + t->light));
		else if (t->volume != NONE)
			region_reference(r, o, 'mgs2', tag_id(h2fx_light_first + h2fx.light_count + h2fx.lens_count + t->volume));
		else
			region_reference(r, o, 'effe', tag_id(target_first + i));
		snprintf((char *)region_at(r, o + 0x10), 32, "%s", t->marker);
		/* (scaled in Halo 2 - a driver's seat's power: as the stand-in's
		light is, its running lights) */
		memcpy(region_at(r, o + 0x30), t->scale[0] && (light_scales[0] || light_scales[2]) ? light_scales :
			t->contrail != NONE ? contrail_scales : effect_scales, 6);
		/* (a light or light volume of Halo 2's: on while it exists unless
		scaled, as the stand-in's light is) */
		if ((t->light != NONE || t->volume != NONE) && !(t->scale[0] && (light_scales[0] || light_scales[2])))
			memset(region_at(r, o + 0x30), 0, 6);
		/* (a weapon's - the sword's glow, Halo 2's by its turned_on: always
		on here, the stand-in's light's function is its firing) */
		if (t->group == 'weap')
			memset(region_at(r, o + 0x30), 0, 6);
		/* (a boost's - the Ghost's plasma vent flash and contrail -: by the
		vehicle's C out, its boost power: h2_vehicles.c, vehicles.c's mode
		13; none while it does not boost) */
		if (strstr(t->scale, "boost"))
		{
			/* (the Spectre's boost its B, h2_vehicles.c) */
			write16(region_at(r, o + 0x30), strstr(name, "\\spectre") ? 2 : 3);
			write16(region_at(r, o + 0x32), 0);
			write16(region_at(r, o + 0x34), 0);
		}
		/* (the Banshee's wingtip contrails, by "wingtips": hard turns, the
		fifth function object_get_function_value reads, h2_vehicle_wingtip_value) */
		if (strstr(t->scale, "wingtip"))
		{
			write16(region_at(r, o + 0x30), 5);
			write16(region_at(r, o + 0x32), 0);
			write16(region_at(r, o + 0x34), 0);
		}
		/* (the Warthog's tail lights by "jump", the sixth; its headlights on a
		level whose scripts light them, the seventh) */
		if (!strcmp(t->scale, "jump") || t->gated)
		{
			write16(region_at(r, o + 0x30), !strcmp(t->scale, "jump") ? 6 : 7);
			write16(region_at(r, o + 0x32), 0);
			write16(region_at(r, o + 0x34), 0);
		}
	}
	/* (Halo 2's light volumes: this engine's are the object's widgets
	(0x20 each), not its attachments - the stand-in's light volumes
	dropped, its other widgets kept) */
	if (own_volumes)
	{
		unsigned long widgets_field = root + offsetof(struct projectile_definition, object.widgets), old_widgets, widgets;
		long old_widget_count = (long)read32(region_at(r, widgets_field)), widget_count = 0, volume_count = 0;
		unsigned char kept_widgets[8][0x20];

		old_widgets = read32(region_at(r, widgets_field + 4));
		for (i = 0; i < old_widget_count && old_widgets && widget_count < 8; i++)
		{
			unsigned char *w = region_at(r, old_widgets - r->link + 0x20 * (unsigned long)i);

			if (getenv("HALO_H2_DEBUG"))
				fprintf(stderr, "h2 effects: %s stand-in widget %c%c%c%c\n", name, (int)(read32(w) >> 24), (int)(read32(w) >> 16 & 255),
					(int)(read32(w) >> 8 & 255), (int)(read32(w) & 255));
			if (read32(w) != 'mgs2')
				memcpy(kept_widgets[widget_count++], w, 0x20);
		}
		for (i = first; i < h2fx.target_count; i++)
		{
			if (h2fx.targets[i].kind == _h2fx_target_projectile_attachments && !strcmp(h2fx.targets[i].name, name) &&
				h2fx.targets[i].volume != NONE && !h2fx.targets[i].off)
				volume_count++;
		}
		if (widget_count + volume_count > 8)
			volume_count = 8 - widget_count;
		widgets = region_block(r, widgets_field, widget_count + volume_count, 0x20);
		for (i = 0; i < widget_count && !r->b.failed; i++)
			memcpy(region_at(r, widgets + 0x20 * (unsigned long)i), kept_widgets[i], 0x20);
		for (i = first, n = widget_count; i < h2fx.target_count && n < widget_count + volume_count && !r->b.failed; i++)
		{
			struct h2fx_target const *t = &h2fx.targets[i];

			if (t->kind != _h2fx_target_projectile_attachments || strcmp(t->name, name) || t->volume == NONE || t->off)
				continue;
			region_reference(r, widgets + 0x20 * (unsigned long)n++, 'mgs2',
				tag_id(h2fx_light_first + h2fx.light_count + h2fx.lens_count + t->volume));
		}
	}
}

/* a target's place written: its slot's tag the effect, the place's
reference to it (the merged effects' keys: an effect and the old one) */
/* Halo 2's material effects' event for this engine's projectile response
(a stand-in's): a bounce (reflect) 19, sticking (attach) 18, the rest 15 -
an impact or a detonation */
H2_ALIGN static short h2fx_response_event(short response)
{
	return response == _projectile_material_response_reflect ? 19 : response == _projectile_material_response_attach ? 18 : 15;
}

H2_ALIGN static void h2fx_target_write(struct region *r, unsigned long instances, long tag_count, long first, long particle_first,
	long index, long *keys)
{
	struct h2fx_target const *t = &h2fx.targets[index];
	long slot = first + index, target_tag = NONE, i;
	unsigned long root = 0, fields[64];
	long field_count = 0, old = NONE, uses = NONE;
	unsigned long effect_root;

	keys[2 * index] = keys[2 * index + 1] = NONE;
	if (t->kind == _h2fx_target_projectile_attachments)
	{
		/* (its slot: Halo 2's effect, looping as Halo 2's - or none, a contrail's) */
		if (t->effect != NONE)
		{
			/* (a vehicle's: looping while it exists - Halo 2 starts an
			attachment's effect again as it ends) */
			h2fx_loop_all = t->group == 'vehi' || t->group == 'weap' || t->group == 'scen' || t->group == 'mach';
			h2fx_origin_locations = (t->group == 'scen' || t->group == 'mach') && !getenv("HALO_H2_OBJECT_MARKERS_OLD");
			h2fx_origin_markers = getenv("HALO_H2_NO_OBJECT_MARKERS") ? NULL : t->markers;
			h2fx_scale_counts = t->group == 'vehi' && t->scale[0];
			effect_root = h2fx_effect_write(r, t->effect, 0, particle_first, FALSE);
			h2fx_loop_all = FALSE;
			h2fx_origin_locations = FALSE;
			h2fx_origin_markers = NULL;
			h2fx_scale_counts = FALSE;
		}
		else
		{
			effect_root = region_alloc(r, H2FX_EFFECT_SIZE);
			write16(region_at(r, effect_root + 4), NONE);
			write16(region_at(r, effect_root + 6), NONE);
		}
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)slot + 20), r->link + effect_root);
		/* (the projectile's block made once, at its first) */
		for (i = 0; i < index; i++)
		{
			if (h2fx.targets[i].kind == _h2fx_target_projectile_attachments && !strcmp(h2fx.targets[i].name, t->name))
				return;
		}
		target_tag = h2fx_tag_named(r, instances, tag_count, t->group, t->name);
		if (getenv("HALO_H2_DEBUG") && (t->group == 'scen' || t->group == 'mach'))
			fprintf(stderr, "h2 effects: %s's attachments: its tag %ld\n", t->name, target_tag);
		if (target_tag != NONE && (root = h2w_root(r, instances, target_tag)) != 0)
			h2fx_attachments_write(r, root, index, first, particle_first + h2fx.emitter_count, t->name);
		return;
	}
	if (t->kind != _h2fx_target_named)
	{
		target_tag = h2fx_tag_named(r, instances, tag_count, t->group, t->name);
		root = target_tag != NONE ? h2w_root(r, instances, target_tag) : 0;
	}
	/* the reference fields it goes in */
	switch (t->kind)
	{
		case _h2fx_target_projectile_effect:
		case _h2fx_target_projectile_super:
		case _h2fx_target_projectile_started:
			if (root)
				fields[field_count++] = root + (t->kind == _h2fx_target_projectile_effect ? offsetof(struct projectile_definition, projectile.effect) :
					t->kind == _h2fx_target_projectile_super ? offsetof(struct projectile_definition, projectile.super_detonation) :
					offsetof(struct projectile_definition, projectile.detonation_started));
			break;
		case _h2fx_target_projectile_material:
		case _h2fx_target_projectile_detonation:
			if (root)
			{
				long count = (long)read32(region_at(r, root + offsetof(struct projectile_definition, projectile.material_responses)));
				unsigned long responses = read32(region_at(r, root + offsetof(struct projectile_definition, projectile.material_responses) + 4));
				static unsigned long shallow_root;

				for (i = 0; i < count && responses && field_count < 64; i++)
				{
					unsigned long response = responses - r->link + sizeof(struct projectile_material_response_definition) * (unsigned long)i;
					short type = (short)read16(region_at(r, response + offsetof(struct projectile_material_response_definition, default_response)));

					if (i != t->material)
						continue;
					/* (the shallow water slot: its response water's, once a
					projectile - the stand-in's there is an engineer's force
					field's, a bullet stopped at the surface. Halo 2's liquid_thin
					overpenetrates, the tag test's data) */
					if (i == 18 && i < count && shallow_root != root)
					{
						shallow_root = root;
						memcpy(region_at(r, response), region_at(r, response + sizeof(struct projectile_material_response_definition) * 10),
							sizeof(struct projectile_material_response_definition));
						type = (short)read16(region_at(r, response + offsetof(struct projectile_material_response_definition, default_response)));
					}
					/* (the stand-in's response the way it goes: Halo 2's event
					of it - a bounce's 19, sticking's 18, the rest's 15; a
					timed one's detonation on it, 15) */
					if (t->kind == _h2fx_target_projectile_material && (h2fx_response_event(type) == t->event ||
						(t->event == 17 && type == _projectile_material_response_overpenetrate)))
					{
						fields[field_count++] = response + offsetof(struct projectile_material_response_definition, default_effect);
					}
					else if (t->kind == _h2fx_target_projectile_detonation &&
						(type == _projectile_material_response_reflect || type == _projectile_material_response_attach))
					{
						fields[field_count++] = response + offsetof(struct projectile_material_response_definition, detonation_effect);
					}
				}
			}
			break;
		case _h2fx_target_biped_shield:
			if (root)
			{
				long coll = (short)read16(region_at(r, root + offsetof(struct unit_definition, object.collision_model) + 12));
				unsigned long coll_root = coll >= 0 ? h2w_root(r, instances, coll) : 0;

				if (coll_root)
					fields[field_count++] = coll_root + (t->material == 0 ? offsetof(struct damage_resistance, shield_damaged_effect) :
						t->material == 1 ? offsetof(struct damage_resistance, shield_depleted_effect) :
						offsetof(struct damage_resistance, shield_recharging_effect));
			}
			break;
		case _h2fx_target_biped_region:
			if (root)
			{
				long coll = (short)read16(region_at(r, root + offsetof(struct unit_definition, object.collision_model) + 12));
				unsigned long coll_root = coll >= 0 ? h2w_root(r, instances, coll) : 0;
				long count = coll_root ? (long)read32(region_at(r, coll_root + H2FX_COLLISION_RESISTANCE + offsetof(struct damage_resistance, regions))) : 0;
				unsigned long regions = coll_root ? read32(region_at(r, coll_root + H2FX_COLLISION_RESISTANCE + offsetof(struct damage_resistance, regions) + 4)) : 0;

				for (i = 0; i < count && regions >= r->link && field_count < (long)NUMBEROF(fields); i++)
				{
					unsigned long region = regions - r->link + H2FX_DAMAGE_REGION_SIZE * (unsigned long)i;

					if (!strcmp((char const *)region_at(r, region), t->marker))
					{
						fields[field_count++] = region + H2FX_DAMAGE_REGION_DESTROYED_EFFECT;
						if (getenv("HALO_H2_DEBUG"))
							fprintf(stderr, "h2 effects: %s region %s (%ld of %ld): threshold %.2f\n", t->name, t->marker, i, count, readf(region_at(r, region + 0x28)));
					}
				}
			}
			break;
		case _h2fx_target_object_destroyed:
			if (root)
				fields[field_count++] = root + H2FX_COLLISION_BODY_DESTROYED_EFFECT;
			break;
		case _h2fx_target_object_shield:
			if (root)
				fields[field_count++] = root + H2FX_COLLISION_RESISTANCE + (t->material == 0 ? offsetof(struct damage_resistance, shield_damaged_effect) :
					t->material == 1 ? offsetof(struct damage_resistance, shield_depleted_effect) :
					offsetof(struct damage_resistance, shield_recharging_effect));
			break;
		case _h2fx_target_object_stage:
			if (root)
				fields[field_count++] = root + offsetof(struct damage_resistance, unused) +
					offsetof(struct h2_explosive_port, stage_effect) + 16 * (unsigned long)t->material;
			break;
		case _h2fx_target_vehicle_destroyed:
		case _h2fx_target_vehicle_damaged:
			if (root)
			{
				long coll = (short)read16(region_at(r, root + offsetof(struct unit_definition, object.collision_model) + 12));
				unsigned long coll_root = coll >= 0 ? h2w_root(r, instances, coll) : 0;

				if (coll_root)
					fields[field_count++] = coll_root + (t->kind == _h2fx_target_vehicle_destroyed ?
						offsetof(struct damage_resistance, body_depleted_effect) : offsetof(struct damage_resistance, body_damaged_effect));
			}
			break;
	}
	/* the effect it stands in for (the first field's), its parts kept */
	for (i = 0; i < field_count && old == NONE; i++)
		old = h2fx_referenced(r, fields[i]);
	if (t->kind == _h2fx_target_effect)
		old = target_tag;
	/* (made already for another place: that one) */
	for (i = 0; i < index; i++)
	{
		if (keys[2 * i] == t->effect && keys[2 * i + 1] == old && t->kind != _h2fx_target_effect && h2fx.targets[i].kind != _h2fx_target_effect &&
			h2fx.targets[i].kind != _h2fx_target_named && t->kind != _h2fx_target_named)
		{
			uses = first + i;
			break;
		}
	}
	keys[2 * index] = t->effect;
	keys[2 * index + 1] = old;
	if (uses == NONE && t->kind != _h2fx_target_effect)
	{
		unsigned long old_root = old != NONE ? h2w_root(r, instances, old) : 0;

		effect_root = h2fx_effect_write(r, t->effect, old_root, particle_first, t->kind == _h2fx_target_named);
		uses = slot;
	}
	else
	{
		/* (an empty effect in the slot) */
		effect_root = region_alloc(r, H2FX_EFFECT_SIZE);
		write16(region_at(r, effect_root + 4), NONE);
		write16(region_at(r, effect_root + 6), NONE);
	}
	write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)slot + 20), r->link + effect_root);
	if (t->kind == _h2fx_target_effect)
	{
		/* (an effect of this engine's made again, in place) */
		if (target_tag != NONE && root)
		{
			unsigned long made = h2fx_effect_write(r, t->effect, root, particle_first, FALSE);

			write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)target_tag + 20), r->link + made);
			if (getenv("HALO_H2_DEBUG"))
			{
				long ev, events_n = (long)read32(region_at(r, made + H2FX_EFFECT_EVENTS)), locs = (long)read32(region_at(r, made + H2FX_EFFECT_LOCATIONS));
				unsigned long evs = read32(region_at(r, made + H2FX_EFFECT_EVENTS + 4)), la = read32(region_at(r, made + H2FX_EFFECT_LOCATIONS + 4));

				fprintf(stderr, "h2 effects: %s made again as %s (%ld emitters): %ld events, %ld locations:", t->name, h2fx.effects[t->effect].name,
					h2fx.effects[t->effect].emitter_count, events_n, locs);
				for (ev = 0; ev < locs; ev++)
					fprintf(stderr, " '%s'", (char const *)region_at(r, la - r->link + 32 * (unsigned long)ev));
				for (ev = 0; ev < events_n; ev++)
				{
					unsigned long e0 = evs - r->link + H2FX_EVENT_SIZE * (unsigned long)ev;
					fprintf(stderr, " | event %ld delay %.2f dur %.2f parts %ld particles %ld", ev, readf(region_at(r, e0 + 8)), readf(region_at(r, e0 + 0x14)),
						(long)read32(region_at(r, e0 + H2FX_EVENT_PARTS)), (long)read32(region_at(r, e0 + H2FX_EVENT_PARTICLES)));
				}
				fprintf(stderr, "\n");
			}
		}
		else
			platform_log("h2 effects: %s: no effect %s here", h2fx.effects[t->effect].name, t->name);
		return;
	}
	for (i = 0; i < field_count; i++)
		region_reference(r, fields[i], 'effe', tag_id(uses));
	if (t->kind != _h2fx_target_named && field_count == 0 && getenv("HALO_H2_DEBUG"))
		fprintf(stderr, "h2 effects: %s (kind %d) has no place in %s\n", h2fx.effects[t->effect].name, t->kind, t->name);
	else if ((t->kind == _h2fx_target_projectile_material || t->kind == _h2fx_target_projectile_detonation) && getenv("HALO_H2_DEBUG") &&
		atoi(getenv("HALO_H2_DEBUG")) >= 2)
	{
		fprintf(stderr, "h2 effects: %s (kind %d, material %d, event %d) in %s: %ld fields\n", h2fx.effects[t->effect].name, t->kind, t->material,
			t->event, t->name, field_count);
	}
}

/* a light's tag written (0x160: point_light_definition) */
H2_ALIGN static void h2fx_light_write(struct region *r, unsigned long root, struct h2fx_light const *l, long lens_first)
{
	unsigned char *o;
	float flare = 0.0f;
	long k;

	if (l->lens != NONE)
	{
		for (k = 0; k < h2fx.lenses[l->lens].reflection_count; k++)
		{
			if (h2fx.lenses[l->lens].reflections[k].radius[1] > flare)
				flare = h2fx.lenses[l->lens].reflections[k].radius[1];
		}
	}
	region_reference(r, root + 0x64, 'bitm', 0xFFFFFFFFUL);
	region_reference(r, root + 0x7C, 'bitm', 0xFFFFFFFFUL);
	region_reference(r, root + 0xAC, 'lens', l->lens != NONE ? tag_id(lens_first + l->lens) : 0xFFFFFFFFUL);
	o = region_at(r, root);
	/* (dynamic: it lights the world and objects; a lens flare's alone
	not) */
	write32(o + 0x00, l->lens_only || l->radius <= 0.0f ? 0 : 1);
	/* (Halo 2's brighter than white: a wider light, at most twice) */
	writef(o + 0x04, l->radius * (l->strength > 1.0f ? (l->strength > 4.0f ? 2.0f : sqrtf(l->strength)) : 1.0f));
	writef(o + 0x08, 1.0f);
	writef(o + 0x0C, 1.0f);
	/* (a sphere: every way) */
	writef(o + 0x10, 3.14159265f);
	writef(o + 0x14, 3.14159265f);
	writef(o + 0x18, flare);
	writef(o + 0x1C, -1.0f);
	writef(o + 0x20, -1.0f);
	writef(o + 0x24, 1.0f);
	writef(o + 0x28, 0.0f);
	/* (black at no intensity, and no alpha in its bounds: an alpha is a share of
	the object's change colour - white, a light with no change colour - past
	the bounds' colour: rgb_colors_interpolate_and_scale. With upper alpha 1 a
	light scaled by a function at 0 - the boost's, on an empty Banshee and
	Spectre - was white, and lit the ground under them) */
	writef(o + 0x38, 0.0f);
	writef(o + 0x48, 0.0f);
	for (k = 0; k < 3; k++)
	{
		writef(o + 0x3C + 4 * k, 0.0f);
		writef(o + 0x4C + 4 * k, h2fx_clamp01(l->color[k]));
	}
	/* (the gel's animation periods: none, but never 0 - the rasterizer's
	check) */
	writef(o + 0x78, 1.0f);
	writef(o + 0x90, 1.0f);
	writef(o + 0x98, 1.0f);
	writef(o + 0xA0, 1.0f);
	/* (how long it lasts, in ticks as the tool writes it - an effect's
	light is gone once so many have passed: none, a tick) */
	writef(o + 0xF4, l->duration > 0.0f ? l->duration * 30.0f : 1.0f);
	write16(o + 0xFA, l->falloff);
	/* (its brightness animation, a periodic one - object_lights.c reads it:
	'H2BA' at 0x120, then type, flags, shapes, its range at 0x128, its
	curves' four floats each at 0x130, and the brightness the colour was
	made at, 0x150) */
	if (l->animation && l->animation_type == 3 && l->animation_count >= 4 && !(l->animation_flags & 0x70) &&
		l->brightness > 0.0f)
	{
		write32(o + H2_LIGHT_ANIMATION_OFFSET, H2_LIGHT_ANIMATION_MAGIC);
		o[H2_LIGHT_ANIMATION_OFFSET + 4] = l->animation_type;
		o[H2_LIGHT_ANIMATION_OFFSET + 5] = l->animation_flags;
		o[H2_LIGHT_ANIMATION_OFFSET + 6] = l->animation_shape[0];
		o[H2_LIGHT_ANIMATION_OFFSET + 7] = l->animation_shape[1];
		writef(o + H2_LIGHT_ANIMATION_OFFSET + 0x08, l->animation_range[0]);
		writef(o + H2_LIGHT_ANIMATION_OFFSET + 0x0C, l->animation_range[1]);
		for (k = 0; k < 8; k++)
			writef(o + H2_LIGHT_ANIMATION_OFFSET + 0x10 + 4 * k, l->animation_data[k]);
		writef(o + H2_LIGHT_ANIMATION_OFFSET + 0x30, l->brightness);
	}
}

/* a lens flare's tag written (0xF0: lens_flare_definition, its
reflections 0x80 each) */
H2_ALIGN static void h2fx_lens_write(struct region *r, unsigned long root, struct h2fx_lens const *l, unsigned long bitmap_id)
{
	unsigned long reflections;
	unsigned char *o;
	long k;
	int c;

	region_reference(r, root + 0x20, 'bitm', bitmap_id);
	reflections = region_block(r, root + 0xC4, l->reflection_count, 0x80);
	if (r->b.failed)
		return;
	o = region_at(r, root);
	/* (no falloff by the light's direction: a cutoff a right angle,
	this engine's scale functions none) */
	writef(o + 0x00, 0.0f);
	writef(o + 0x04, 1.5707963f);
	writef(o + 0x08, 1.0f);
	writef(o + 0x0C, 0.0f);
	writef(o + 0x10, l->occlusion_radius > 0.0f ? l->occlusion_radius : 0.1f);
	writef(o + 0x18, l->near_fade);
	writef(o + 0x1C, l->far_fade);
	/* (the sun flag: this engine's sun bit; the corona's rotation function
	and scale as Halo 2's - the enum is this engine's) */
	write16(o + 0x30, (l->third_person_only ? 0x8000 : 0) | (l->sun ? 1 : 0));
	write16(o + 0x80, l->rotation_function);
	writef(o + 0x84, l->rotation_scale);
	writef(o + 0xA0, l->corona_scale[0]);
	writef(o + 0xA4, l->corona_scale[1]);
	for (k = 0; k < l->reflection_count; k++)
	{
		struct h2fx_reflection const *f = &l->reflections[k];
		unsigned char *q = region_at(r, reflections + 0x80 * (unsigned long)k);

		write16(q + 0x00, f->flags);
		write16(q + 0x04, f->sequence);
		writef(q + 0x1C, f->position);
		writef(q + 0x20, f->rotation);
		writef(q + 0x28, f->radius[0]);
		writef(q + 0x2C, f->radius[1]);
		writef(q + 0x34, f->brightness[0]);
		writef(q + 0x38, f->brightness[1]);
		writef(q + 0x40, 1.0f);
		for (c = 0; c < 3; c++)
			writef(q + 0x44 + 4 * c, f->color[c]);
	}
}

/* a light volume's tag written (0x14C: light_volume_definition, a frame
of 0xB0) */
H2_ALIGN static void h2fx_volume_write(struct region *r, unsigned long root, struct h2fx_volume const *v, unsigned long bitmap_id)
{
	unsigned long frames;
	unsigned char *o, *f;
	int end, c;

	region_reference(r, root + 0x5C, 'bitm', bitmap_id);
	frames = region_block(r, root + 0x120, 1, 0xB0);
	if (r->b.failed)
		return;
	o = region_at(r, root);
	memcpy(o, v->marker, 31);
	/* (its sprites' radius a fraction of the view's width: light_volumes.c;
	one scaled by its marker in world units - the Phantom's antigrav glow,
	the Scarab's lights: at a fraction of the view, white discs filling the
	03a intro) */
	write16(o + 0x22, v->world ? 0 : 0x8000);
	writef(o + 0x24, v->view_floor);
	/* (Halo 2's x-buffer slices: light_volumes.c's bounded overdraw; a
	volume drawn as a fraction of the view keeps every sprite) */
	write16(o + 0x28, v->world ? v->xbuffer_spacing : 0);
	write16(o + 0x2A, v->xbuffer_min);
	write16(o + 0x2C, v->xbuffer_max);
	writef(o + 0x34, v->near_fade);
	writef(o + 0x38, v->far_fade > v->near_fade ? v->far_fade : 0.0f);
	writef(o + 0x3C, 1.0f);
	writef(o + 0x40, 1.0f);
	write16(o + 0x6C, v->sequence);
	write16(o + 0x6E, v->count);
	f = region_at(r, frames);
	writef(f + 0x10, v->offset[0]);
	writef(f + 0x14, 1.0f);
	writef(f + 0x18, v->offset[1] - v->offset[0]);
	{
		/* (HALO_H2_LIGHT_VOLUME_SCALE: its sprites' radius times so much - tests) */
		float k = getenv("HALO_H2_LIGHT_VOLUME_SCALE") ? (float)atof(getenv("HALO_H2_LIGHT_VOLUME_SCALE")) : 1.0f;

		writef(f + 0x3C, v->radius[0] * k);
		writef(f + 0x40, v->radius[1] * k);
	}
	writef(f + 0x44, 1.0f);
	for (end = 0; end < 2; end++)
	{
		writef(f + 0x68 + 0x10 * end, v->alpha[end]);
		for (c = 0; c < 3; c++)
			writef(f + 0x6C + 0x10 * end + 4 * c, h2fx_clamp01(v->color[end][c]));
	}
	writef(f + 0x88, 1.0f);
	writef(f + 0x8C, 1.0f);
}

/* the effects' tags (from first: a target's effect each, then a particle
each emitter), the places they go */
/* (after the targets are written) a Halo 2 projectile's stand-in's own
effects left in its material responses - its impacts, ricochets and
detonations on the materials Halo 2's has none for: the kit's rocket's
"impact metal" chips on a boulder, lit black here - taken out: Halo 2
plays its material effects alone (none where it has none). One with a
damage part is kept (none of the kit's have) */
H2_ALIGN static void h2fx_projectile_responses_clean(struct region *r, unsigned long instances, long tag_count)
{
	static unsigned long const fields[] =
	{
		offsetof(struct projectile_material_response_definition, default_effect),
		offsetof(struct projectile_material_response_definition, potential_effect),
		offsetof(struct projectile_material_response_definition, detonation_effect),
	};
	long n, cleared = 0;

	if (getenv("HALO_H2_KEEP_STAND_IN_IMPACTS"))
		return;
	for (n = 0; n < h2fx.target_count; n++)
	{
		struct h2fx_target const *t = &h2fx.targets[n];
		long tag, count, i, k;
		unsigned long root, responses;

		if (t->kind != _h2fx_target_projectile_effect ||
			(tag = h2fx_tag_named(r, instances, tag_count, 'proj', t->name)) == NONE || !(root = h2w_root(r, instances, tag)))
		{
			continue;
		}
		count = (long)read32(region_at(r, root + offsetof(struct projectile_definition, projectile.material_responses)));
		responses = read32(region_at(r, root + offsetof(struct projectile_definition, projectile.material_responses) + 4));
		for (i = 0; i < count && responses >= r->link; i++)
		{
			unsigned long response = responses - r->link + sizeof(struct projectile_material_response_definition) * (unsigned long)i;

			for (k = 0; k < (long)NUMBEROF(fields); k++)
			{
				long effect = h2fx_referenced(r, response + fields[k]);
				unsigned long effect_root, address, events;
				long event_count, e;
				boolean damage = FALSE;

				if (effect == NONE || effect >= tag_count)
					continue;
				address = read32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)effect + 16));
				if (address && address - r->link < r->b.size && !strncmp((char const *)region_at(r, address - r->link), "h2\\", 3))
					continue;
				/* (a damage part: kept) */
				effect_root = h2w_root(r, instances, effect);
				event_count = effect_root ? (long)read32(region_at(r, effect_root + H2FX_EFFECT_EVENTS)) : 0;
				events = effect_root ? read32(region_at(r, effect_root + H2FX_EFFECT_EVENTS + 4)) : 0;
				for (e = 0; e < event_count && e < MAXIMUM_EFFECT_EVENTS && events >= r->link && !damage; e++)
				{
					unsigned long event = events - r->link + H2FX_EVENT_SIZE * (unsigned long)e;
					long part_count = (long)read32(region_at(r, event + H2FX_EVENT_PARTS)), p;
					unsigned long parts = read32(region_at(r, event + H2FX_EVENT_PARTS + 4));

					for (p = 0; p < part_count && p < MAXIMUM_EFFECT_PARTS_PER_EVENT && parts >= r->link; p++)
					{
						if (read32(region_at(r, parts - r->link + H2FX_PART_SIZE * (unsigned long)p + H2FX_PART_REFERENCE)) == 'jpt!')
							damage = TRUE;
					}
				}
				if (damage)
					continue;
				region_reference(r, response + fields[k], 0, 0xFFFFFFFFUL);
				cleared++;
			}
		}
	}
	if (getenv("HALO_H2_DEBUG"))
		fprintf(stderr, "h2 effects: %ld of the stand-in projectiles' own impact effects taken out\n", cleared);
}

H2_ALIGN static void h2_effects_write(struct build *build, struct region *r, unsigned long instances, long first, long tag_count,
	long bitmap_first, long decal_first)
{
	long particle_first = first + h2fx.target_count, i;
	long *keys;

	if (!h2fx.enabled)
		return;
	h2fx_kit_find(r, instances, tag_count);
	if (h2fx.kit_particle == NONE || h2fx.kit_physics[0] == NONE)
	{
		build_fail(build, "Halo 2's effects: the globals' pack has no particle to make theirs of");
		return;
	}
	/* the particles */
	h2fx_bitmap_first = bitmap_first;
	h2fx.mesh_count = 0;
	for (i = 0; i < h2fx.emitter_count && !r->b.failed; i++)
	{
		struct h2fx_emitter const *m = &h2fx.emitters[i];
		unsigned long root = h2w_tag_copy(r, h2w_root(r, instances, h2fx.kit_particle), H2FX_PARTICLE_SIZE, 'part');

		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)(particle_first + i) + 20), r->link + root);
		h2fx_particle_write(r, root, m, tag_id(bitmap_first + h2fx.sheets[m->sheet].images[0]),
			(long)build->bitmaps[h2fx.sheets[m->sheet].images[0]].width, first);
		if (m->mesh != NONE)
		{
			unsigned long mesh = h2fx_mesh_write(build->cache, r, m->mesh);

			write32(region_at(r, root + PART_MESH), mesh ? r->link + mesh : 0);
		}
	}
	/* the contrails (copies of the kit's) */
	for (i = 0; i < h2fx.contrail_count && !r->b.failed; i++)
	{
		struct h2fx_contrail const *c = &h2fx.contrails[i];
		unsigned long root;

		if (h2fx.kit_contrail == NONE)
			break;
		root = h2w_tag_copy(r, h2w_root(r, instances, h2fx.kit_contrail), 0x144, 'cont');
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)(particle_first + h2fx.emitter_count + i) + 20), r->link + root);
		h2fx_contrail_write(r, root, c, tag_id(bitmap_first + h2fx.sheets[c->sheet].images[0]));
	}
	/* the lights, lens flares and light volumes */
	h2fx_write_instances = instances;
	h2fx_write_tag_count = tag_count;
	h2fx_light_first = particle_first + h2fx.emitter_count + h2fx.contrail_count;
	h2fx_decal_first = decal_first;
	for (i = 0; i < h2fx.light_count + h2fx.lens_count + h2fx.volume_count && !r->b.failed; i++)
	{
		long slot = h2fx_light_first + i;
		unsigned long root;

		if (i < h2fx.light_count)
		{
			root = region_alloc(r, 0x160);
			h2fx_light_write(r, root, &h2fx.lights[i], h2fx_light_first + h2fx.light_count);
		}
		else if (i < h2fx.light_count + h2fx.lens_count)
		{
			struct h2fx_lens const *l = &h2fx.lenses[i - h2fx.light_count];

			root = region_alloc(r, 0xF0);
			h2fx_lens_write(r, root, l, tag_id(bitmap_first + h2fx.sheets[l->sheet].images[0]));
		}
		else
		{
			struct h2fx_volume const *v = &h2fx.volumes[i - h2fx.light_count - h2fx.lens_count];

			root = region_alloc(r, 0x14C);
			h2fx_volume_write(r, root, v, tag_id(bitmap_first + h2fx.sheets[v->sheet].images[0]));
		}
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)slot + 20), r->link + root);
	}
	/* the weather systems (an empty one where Halo 2's could not be made) */
	h2fx_weather_tag_count = h2fx.weather_count;
	for (i = 0; i < h2fx.weather_count && !r->b.failed; i++)
	{
		struct h2fx_weather const *w = &h2fx.weathers[i];
		long slot = h2fx_light_first + h2fx.light_count + h2fx.lens_count + h2fx.volume_count + i;

		h2fx_bitmap_first = bitmap_first;
		unsigned long root = region_alloc(r, 0x30);

		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)slot + 20), r->link + root);
		h2fx_weather_tags[i] = w->sheet != NONE ? slot : NONE;
		if (w->sheet != NONE)
			h2fx_weather_write(r, root, w, tag_id(bitmap_first + h2fx.sheets[w->sheet].images[0]),
				(long)build->bitmaps[h2fx.sheets[w->sheet].images[0]].width, h2w_root(r, instances, h2fx.kit_particle));
	}
	/* the effects, in their places */
	keys = malloc(sizeof(long) * 2 * (unsigned long)(h2fx.target_count + 1));
	if (!keys)
	{
		build_fail(build, "out of memory");
		return;
	}
	for (i = 0; i < h2fx.target_count && !r->b.failed; i++)
		h2fx_target_write(r, instances, tag_count, first, particle_first, i, keys);
	free(keys);
	h2fx_projectile_responses_clean(r, instances, tag_count);
	/* (HALO_H2_DEBUG: every made weapon's attachments and widgets as written) */
	if (getenv("HALO_H2_DEBUG"))
	{
		long w;

		for (w = 0; w < h2w.count; w++)
		{
			char wname[160];
			long tag;
			unsigned long root;

			snprintf(wname, sizeof(wname), "h2\\weapons\\%s%s", h2w.weapons[w]->name, getenv("HALO_H2_DEBUG_BULLETS") ? " bullet" : "");
			tag = h2fx_tag_named(r, instances, tag_count, getenv("HALO_H2_DEBUG_BULLETS") ? 'proj' : 'weap', wname);
			root = tag != NONE ? h2w_root(r, instances, tag) : 0;
			if (root)
			{
				unsigned long blocks[2] = { offsetof(struct projectile_definition, object.attachments),
					offsetof(struct projectile_definition, object.widgets) };
				unsigned long sizes[2] = { 72, 0x20 };
				int b;

				for (b = 0; b < 2; b++)
				{
					long count = (long)read32(region_at(r, root + blocks[b])), k;
					unsigned long at = read32(region_at(r, root + blocks[b] + 4));

					for (k = 0; k < count && at; k++)
					{
						unsigned char *e = region_at(r, at - r->link + sizes[b] * (unsigned long)k);
						long ref = (short)read16(e + 12);

						fprintf(stderr, "h2 effects: weapon %s %s %ld: %c%c%c%c %ld at '%.31s'\n", wname, b ? "widget" : "attachment", k,
							(int)(read32(e) >> 24), (int)(read32(e) >> 16 & 255), (int)(read32(e) >> 8 & 255), (int)(read32(e) & 255), ref,
							b ? "" : (char const *)e + 0x10);
					}
				}
			}
		}
	}
}

H2_ALIGN static void h2_effects_surfaces_write(struct build *build, struct region *r, unsigned long instances, long bitmap_first);

/* (after the bitmaps are written) the sprite sheets' bitmaps: their
sequences and sprites, Halo 2's, and all their images */
H2_ALIGN static void h2_effects_bitmaps_write(struct build *build, struct region *r, unsigned long instances, long bitmap_first)
{
	long i;

	if (!h2fx.enabled)
		return;
	h2_effects_surfaces_write(build, r, instances, bitmap_first);
	for (i = 0; i < h2fx.sheet_count && !r->b.failed; i++)
	{
		struct h2fx_sheet const *s = &h2fx.sheets[i];
		long tag = bitmap_first + s->images[0], k, q;
		unsigned long root = h2w_root(r, instances, tag), sequences;

		if (!root)
			continue;
		/* (every image its data, the first's tag's: each image's own tag's
		copied) */
		if (s->image_count > 1)
		{
			unsigned long from[H2FX_MAXIMUM_SHEET_IMAGES], data;

			/* (where each image's data is, before the first's moves) */
			for (k = 0; k < s->image_count; k++)
			{
				unsigned long image_root = h2w_root(r, instances, bitmap_first + s->images[k]);

				from[k] = image_root ? read32(region_at(r, image_root + 0x60 + 4)) : 0;
			}
			data = region_block(r, root + 0x60, s->image_count, 48);
			for (k = 0; k < s->image_count && !r->b.failed; k++)
			{
				if (from[k])
					memcpy(region_at(r, data + 48 * (unsigned long)k), region_at(r, from[k] - r->link), 48);
				write32(region_at(r, data + 48 * (unsigned long)k + 0x20), tag_id(tag));
			}
		}
		/* (the image tags' usage marks back to none) */
		for (k = 0; k < s->image_count; k++)
		{
			unsigned long image_root = h2w_root(r, instances, bitmap_first + s->images[k]);

			if (image_root)
				write16(region_at(r, image_root + 4), 0);
		}
		sequences = region_block(r, root + 0x54, s->sequence_count, 64);
		for (q = 0; q < s->sequence_count && !r->b.failed; q++)
		{
			struct h2fx_sequence const *sq = &s->sequences[q];
			unsigned long sequence = sequences + 64 * (unsigned long)q, sprites;

			write16(region_at(r, sequence + 0x20), s->sprites[sq->first].image);
			write16(region_at(r, sequence + 0x22), 1);
			sprites = region_block(r, sequence + 0x34, sq->count, 32);
			for (k = 0; k < sq->count && !r->b.failed; k++)
			{
				struct h2fx_sprite const *sp = &s->sprites[sq->first + k];
				unsigned char *o = region_at(r, sprites + 32 * (unsigned long)k);

				write16(o, sp->image);
				writef(o + 8, sp->bounds[0]);
				writef(o + 0xC, sp->bounds[1]);
				writef(o + 0x10, sp->bounds[2]);
				writef(o + 0x14, sp->bounds[3]);
				writef(o + 0x18, sp->registration[0]);
				writef(o + 0x1C, sp->registration[1]);
			}
		}
	}
}

/* this engine's material type of a Halo 2 shader's material name (what
an impact, a footstep and a bounce on its surfaces play); NONE: as it is */
H2_ALIGN static short h2fx_surface_material(char const *name)
{
	static struct { char const *h2; short ce; } const names[] =
	{
		{ "shallow", 18 }, { "puddle", 18 }, { "brittle_elec", 17 }, { "cloth", 24 }, { "wood", 4 }, { "underbrush", 29 },
		{ "sand", 1 }, { "snow", 3 }, { "_ice", 31 }, { "dirt", 0 }, { "mud", 0 }, { "gravel", 0 }, { "grass", 0 },
		{ "stone", 2 }, { "concrete", 2 }, { "hard_terrain", 2 }, { "tough_terrain", 0 }, { "soft_terrain", 0 }, { "leafy", 29 }, { "plant", 29 },
		{ "glass", 9 }, { "liquid", 28 }, { "water", 28 }, { "rubber", 8 }, { "plastic", 27 },
		{ "metal_thin", 6 }, { "grating", 6 }, { "metal", 7 }, { "energy", 10 },
	};
	long i;

	for (i = 0; i < (long)(sizeof(names) / sizeof(names[0])); i++)
	{
		if (strstr(name, names[i].h2))
			return names[i].ce;
	}
	return NONE;
}

/* (after the shaders are written) the level's environment shaders' material
types from Halo 2's shaders' material names (h2_level.c writes every one
metal: impacts on sand threw sparks); HALO_H2_NO_SURFACE_MATERIALS: as made */
H2_ALIGN static void h2_effects_surfaces_write(struct build *build, struct region *r, unsigned long instances, long bitmap_first)
{
	long shader_first = bitmap_first - build->shader_count, i, changed = 0;

	if (!h2fx.enabled || getenv("HALO_H2_NO_SURFACE_MATERIALS"))
		return;
	for (i = 0; i < build->shader_count; i++)
	{
		struct h2_cache_tag t;
		unsigned long root;
		char name[64];
		short type;

		/* (every shader group's, the type at the same place: a glass (sgla) or
		transparent one's left 0 was dirt - Ivory Tower's panes threw dirt and
		broke at nothing, a liquid surface had no water) */
		if (build->shaders[i].h2_tag == NONE ||
			!h2_cache_tag_get(build->cache, build->shaders[i].h2_tag, &t) ||
			!h2_cache_string(build->cache, h2_cache_read32(build->cache, t.address + 8), name, sizeof(name)) ||
			(type = h2fx_surface_material(name)) == NONE || !(root = h2w_root(r, instances, shader_first + i)))
		{
			continue;
		}
		write16(region_at(r, root + 0x22), type);
		changed++;
	}
	platform_log("h2 effects: %ld of %ld shaders given their Halo 2 material's type", changed, build->shader_count);
}

H2_ALIGN static void h2_effects_dispose(void)
{
	free(h2fx.effects);
	free(h2fx.emitters);
	free(h2fx.sheets);
	free(h2fx.targets);
	free(h2fx.contrails);
	free(h2fx.lights);
	free(h2fx.lenses);
	free(h2fx.volumes);
	free(h2fx.parts);
	memset(&h2fx, 0, sizeof(h2fx));
}

/* the level file's mark of this builder (h2_level_cache_path: made again
when it changes) */
H2_ALIGN static unsigned long h2_effects_signature(void)
{
	char const *setting = getenv("HALO_H2_EFFECTS");
	boolean off = getenv("HALO_H2_NO_EFFECTS") || (setting && !strcmp(setting, "off"));

	return ('h' << 24) | ((unsigned long)H2_EFFECTS_BUILDER_VERSION << 8) | (off ? 0xFF : (unsigned long)h2_particle_style_tier());
}

/* ---------- private code: the effects made as a level loads */

/* (HALO_H2_EFFECTS_AT_LOAD=1) the effects' tags made as a level loads
(h2_load_tags.h): the builder leaves their range of the table with no data
and records what of the build their gathering and writing find - the
bitmaps and decals before them, the objects, the shaders, the scenario's
structure BSP - in a string list, H2_EFFECTS_CONTEXT_NAME; as the level
loads, h2_effects_load_make gathers and writes them with that, from the
player's own MCC map: the same tags, never saved. Their sprite sheets'
bitmaps are still the level's (made by the builder) */
#define H2_EFFECTS_CONTEXT_NAME "h2\\effects context"

enum
{
	FXC_MAGIC = 0x68326663,   /* 'h2fc' */
	FXC_VERSION = 3,
	/* (the room the effects take at load - their own tags and the blocks
	their writes add to other tags -: Lockout's 1770 tags took 969 KB; a
	tag and a half a KB each and this much more, pages) */
	FXC_RESERVE_PER_TAG = 0x600,
	FXC_RESERVE_MORE = 0x80000,
};

struct fxc_header
{
	unsigned long magic, version, sizes;
	long fx_first, vessel_first, bitmap_first, decal_first;
	long bitmaps_before, decals_before, object_count, shader_count;
	long bitmaps_after, decals_after, tag_count;
	unsigned long sbsp;
	long multiplayer;
	long tier;                   /* (h2_particle_style_tier's as made: the gather's counts depend on it) */
	long weapon_count, vehicle_count;   /* (the weapons and vehicles made with it, h2_weapons_gather's) */
	long pixels_at_load;         /* (the sheets' pixels in the cache beside the level, not its file) */
};

struct fxc_bitmap
{
	long h2_tag;
	unsigned long reference, size;
	unsigned short width, height, mipmaps, format, flags, usage;
	unsigned char tile_u, pad[3];
};

struct fxc_decal
{
	long h2_tag, bitmap, effect;
	float extent;
	unsigned short flags;
	short chain;
};

struct fxc_object
{
	long h2_tag, collision, explosive, stage_effect[3];
	unsigned long group;
	char name[64];
};

struct fxc_shader
{
	long h2_tag;
	unsigned long group;
};

/* (the context's layouts: a context of another build of the game is not read) */
#define FXC_SIZES ((unsigned long)(sizeof(struct fxc_header) + 3 * sizeof(struct fxc_bitmap) + 5 * sizeof(struct fxc_decal) + \
	7 * sizeof(struct fxc_object) + 11 * sizeof(struct fxc_shader)))

H2_ALIGN static boolean h2_effects_at_load(void)
{
	char const *setting = getenv("HALO_H2_EFFECTS_AT_LOAD");

	return setting && !strcmp(setting, "1");
}

H2_ALIGN static unsigned long h2_effects_load_reserve(struct build const *build)
{
	(void)build;
	return ((unsigned long)h2_effects_tag_count() * FXC_RESERVE_PER_TAG + FXC_RESERVE_MORE + 0xFFFUL) & ~0xFFFUL;
}

/* (the builder, in place of h2_effects_write) the context, its string list's
one string, at the table's index context */
H2_ALIGN static void h2_effects_context_write(struct build *build, struct region *r, unsigned long instances, long context,
	long fx_first, long vessel_first, long bitmap_first, long decal_first, long bitmaps_before, long decals_before,
	long bitmaps_after, long decals_after, boolean pixels_at_load)
{
	struct fxc_header h;
	unsigned long size = sizeof(h) + (unsigned long)bitmaps_before * sizeof(struct fxc_bitmap) +
		(unsigned long)decals_before * sizeof(struct fxc_decal) + (unsigned long)build->object_count * sizeof(struct fxc_object) +
		(unsigned long)build->shader_count * sizeof(struct fxc_shader);
	unsigned char *blob = calloc(1, size), *at;
	unsigned long list, entry;
	long i;

	if (!blob)
	{
		build_fail(build, "the effects' context: out of memory");
		return;
	}
	memset(&h, 0, sizeof(h));
	h.magic = FXC_MAGIC;
	h.version = FXC_VERSION;
	h.sizes = FXC_SIZES;
	h.fx_first = fx_first, h.vessel_first = vessel_first, h.bitmap_first = bitmap_first, h.decal_first = decal_first;
	h.bitmaps_before = bitmaps_before, h.decals_before = decals_before;
	h.object_count = build->object_count, h.shader_count = build->shader_count;
	h.bitmaps_after = bitmaps_after, h.decals_after = decals_after, h.tag_count = h2_effects_tag_count();
	h.sbsp = build->sbsp;
	h.multiplayer = build->multiplayer;
	h.tier = h2fx.tier;
	h.weapon_count = h2w.count, h.vehicle_count = h2v.count;
	h.pixels_at_load = pixels_at_load;
	memcpy(blob, &h, sizeof(h));
	at = blob + sizeof(h);
	for (i = 0; i < bitmaps_before; i++, at += sizeof(struct fxc_bitmap))
	{
		struct fxc_bitmap *f = (struct fxc_bitmap *)at;
		struct bitmap const *b = &build->bitmaps[i];

		f->h2_tag = b->h2_tag, f->reference = b->reference, f->size = b->size;
		f->width = b->width, f->height = b->height, f->mipmaps = b->mipmaps, f->format = b->format, f->flags = b->flags;
		f->usage = b->usage, f->tile_u = b->tile_u;
	}
	for (i = 0; i < decals_before; i++, at += sizeof(struct fxc_decal))
	{
		struct fxc_decal *f = (struct fxc_decal *)at;
		struct decal_made const *d = &build->decals[i];

		f->h2_tag = d->h2_tag, f->bitmap = d->bitmap, f->effect = d->effect, f->extent = d->extent, f->flags = d->flags;
		f->chain = d->chain;
	}
	for (i = 0; i < build->object_count; i++, at += sizeof(struct fxc_object))
	{
		struct fxc_object *f = (struct fxc_object *)at;
		struct object_made const *o = &build->objects[i];
		int k;

		f->h2_tag = o->h2_tag, f->collision = o->collision, f->explosive = o->explosive, f->group = o->group;
		for (k = 0; k < 3; k++)
			f->stage_effect[k] = o->stage_effect[k];
		snprintf(f->name, sizeof(f->name), "%s", o->name);
	}
	for (i = 0; i < build->shader_count; i++, at += sizeof(struct fxc_shader))
	{
		struct fxc_shader *f = (struct fxc_shader *)at;

		f->h2_tag = build->shaders[i].h2_tag, f->group = build->shaders[i].group;
	}
	list = region_alloc(r, 12);
	entry = region_block(r, list, 1, 20);
	region_data(r, entry, blob, size);
	write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)context + 20), r->link + list);
	free(blob);
	platform_log("h2 effects: %ld tags to be made as the level loads (their context %lu KB: %ld bitmaps, %ld decals, %ld objects, "
		"%ld shaders)", h.tag_count, size / 1024, bitmaps_before, decals_before, build->object_count, build->shader_count);
}

/* ---------- the effects' pixel cache (h2_texture_sidecar.h's second file)

A level made with its effects at load carries no pixels of their sprite
sheets: the sheets' images (the bitmaps the gather adds, h2fx_image's) keep
their tags in the level, their pixels left out of its file, and lie beside
it in <level>.fxcache - the sidecar's format, read on every platform by
h2_texture_sidecar.c -, made by the builder with the level (fx_cache_* from
h2_level.c's bitmaps loop) or, when it is missing or another level's, by
the maker as the level loads (fx_pixels_cache_make), the same bytes: the
builder's own pixel path (bitmap_pixels) from the player's textures.dat. */

struct fx_cache
{
	FILE *file;
	unsigned long at, crc, bytes;
	unsigned char *entries;
	long count, capacity;
	boolean failed;
	char path[600], temporary[600];
};

H2_ALIGN static boolean bitmap_pixels(struct build *build, struct bitmap *b, unsigned long limit, unsigned char **pixels,
	unsigned long *bytes);
H2_ALIGN static void bitmap_limit_dimensions(struct build const *build, struct bitmap *b, unsigned long limit,
	struct h2_cache_texture_options *options, boolean *reduced);

/* whether the gather's bitmap index (from before, the bitmaps the gather
found) is an image of one of its sprite sheets: its pixels the cache's */
H2_ALIGN static boolean h2_effects_bitmap_at_load(long index, long before)
{
	long i, k;

	if (!h2fx.enabled || index < before)
		return FALSE;
	for (i = 0; i < h2fx.sheet_count; i++)
	{
		for (k = 0; k < h2fx.sheets[i].image_count; k++)
		{
			if (h2fx.sheets[i].images[k] == index)
				return TRUE;
		}
	}
	return FALSE;
}

H2_ALIGN static void fx_cache_write(struct fx_cache *w, void const *bytes, unsigned long size)
{
	if (!w->file || w->failed)
		return;
	if (fwrite(bytes, 1, size, w->file) != size)
	{
		w->failed = TRUE;
		return;
	}
	w->crc = crc32(w->crc, bytes, (uInt)size);
	w->at += size;
}

/* the cache begun beside the level (level_path) as <level>.fxcache.tmp */
H2_ALIGN static boolean fx_cache_begin(struct fx_cache *w, char const *level_path)
{
	unsigned char header[H2_TEXTURE_SIDECAR_HEADER_SIZE];

	memset(w, 0, sizeof(*w));
	h2_texture_sidecar_fx_path(level_path, w->path, sizeof(w->path) - 4);
	snprintf(w->temporary, sizeof(w->temporary), "%s.tmp", w->path);
	w->file = fopen(w->temporary, "wb");
	memset(header, 0, sizeof(header));
	if (!w->file || fwrite(header, 1, sizeof(header), w->file) != sizeof(header))
	{
		if (w->file)
			fclose(w->file);
		w->file = NULL;
		w->failed = TRUE;
		return FALSE;
	}
	w->at = H2_TEXTURE_SIDECAR_HEADER_SIZE;
	w->crc = crc32(0L, Z_NULL, 0);
	return TRUE;
}

/* an entry: the bitmap (name, as the level's tag has it) and its pixels;
where in the cache its pixels are (0: not written) - the image's record's
pixels offset, the key its reads find it by (h2_texture_sidecar_read: a
sprite sheet's images all carry the sheet's tag index) */
H2_ALIGN static unsigned long fx_cache_entry(struct fx_cache *w, char const *name, struct bitmap const *b,
	unsigned char const *pixels, unsigned long size)
{
	static unsigned char const zeros[128];
	unsigned char *e;
	unsigned short flags = b->ce_flags ? b->ce_flags : b->format >= 14 && b->format <= 16 ? 0x83 : 0x89;
	unsigned long at;

	if (!w->file || w->failed)
		return 0;
	if (w->at & 0x7F)
		fx_cache_write(w, zeros, 0x80 - (w->at & 0x7F));
	if (w->count >= w->capacity)
	{
		long capacity = w->capacity ? 2 * w->capacity : 256;
		unsigned char *grown = realloc(w->entries, (size_t)capacity * H2_TEXTURE_SIDECAR_ENTRY_SIZE);

		if (!grown)
		{
			w->failed = TRUE;
			return 0;
		}
		w->entries = grown;
		w->capacity = capacity;
	}
	e = w->entries + H2_TEXTURE_SIDECAR_ENTRY_SIZE * w->count;
	memset(e, 0, H2_TEXTURE_SIDECAR_ENTRY_SIZE);
	snprintf((char *)e, H2_TEXTURE_SIDECAR_NAME_SIZE, "%s", name);
	write16(e + 200, b->width);
	write16(e + 202, b->height);
	write16(e + 204, b->mipmaps);
	write16(e + 206, b->format);
	write16(e + 208, flags);
	write16(e + 210, H2_TEXTURE_SIDECAR_FULL_SIZE);
	at = w->at;
	write32(e + 212, at);
	write32(e + 216, size);
	fx_cache_write(w, pixels, size);
	if (w->failed)
		return 0;
	w->count++;
	w->bytes += size;
	return at;
}

/* the cache finished: its entries, its header tied to the level's file
(which is in place), put in place - or let go (ok FALSE, or nothing in it) */
H2_ALIGN static boolean fx_cache_end(struct fx_cache *w, char const *level_path, boolean ok)
{
	unsigned char header[H2_TEXTURE_SIDECAR_HEADER_SIZE], tail[4];
	unsigned long entries_offset, level_size = 0;
	FILE *level;
	boolean placed = FALSE;

	if (w->file && ok && !w->failed && w->count)
	{
		entries_offset = w->at;
		fx_cache_write(w, w->entries, (unsigned long)w->count * H2_TEXTURE_SIDECAR_ENTRY_SIZE);
		level = fopen(level_path, "rb");
		ok = level && fseek(level, -4, SEEK_END) == 0 && fread(tail, 1, 4, level) == 4;
		if (level)
		{
			level_size = (unsigned long)ftell(level);
			fclose(level);
		}
		memset(header, 0, sizeof(header));
		write32(header, H2_TEXTURE_SIDECAR_SIGNATURE);
		write32(header + 4, H2_TEXTURE_SIDECAR_VERSION);
		write32(header + 8, (unsigned long)w->count);
		write32(header + 12, entries_offset);
		write32(header + 16, level_size);
		memcpy(header + 20, tail, 4);
		write32(header + 24, w->crc);
		write32(header + 28, w->bytes);
		ok = ok && !w->failed && fseek(w->file, 0, SEEK_SET) == 0 && fwrite(header, 1, sizeof(header), w->file) == sizeof(header);
		ok = fclose(w->file) == 0 && ok;
		w->file = NULL;
		remove(w->path);
		if (ok && rename(w->temporary, w->path) == 0)
		{
			platform_log("h2 effects: pixel cache %s: %ld sprite sheet images, %lu KB", w->path, w->count, w->at / 1024);
			placed = TRUE;
		}
		else
			remove(w->temporary);
	}
	else
	{
		if (w->file)
			fclose(w->file);
		w->file = NULL;
		if (w->temporary[0])
			remove(w->temporary);
	}
	free(w->entries);
	w->entries = NULL;
	return placed;
}

/* whether the cache beside the level is the level's (its header: the
level file's size and last four bytes) and has entries */
H2_ALIGN static boolean fx_cache_matches(char const *level_path)
{
	char path[600];
	unsigned char header[H2_TEXTURE_SIDECAR_HEADER_SIZE], tail[4];
	unsigned long level_size;
	FILE *level, *file;
	boolean ok;

	level = fopen(level_path, "rb");
	if (!level)
		return FALSE;
	ok = fseek(level, -4, SEEK_END) == 0 && fread(tail, 1, 4, level) == 4;
	level_size = (unsigned long)ftell(level);
	fclose(level);
	if (!ok)
		return FALSE;
	h2_texture_sidecar_fx_path(level_path, path, sizeof(path));
	file = fopen(path, "rb");
	if (!file)
		return FALSE;
	ok = fread(header, 1, sizeof(header), file) == sizeof(header) && read32(header) == H2_TEXTURE_SIDECAR_SIGNATURE &&
		read32(header + 4) == H2_TEXTURE_SIDECAR_VERSION && read32(header + 8) > 0 && read32(header + 16) == level_size &&
		!memcmp(header + 20, tail, 4);
	fclose(file);
	return ok;
}

/* (the maker, at load) the level's effects' pixel cache in place: left as
it is when it is the level's own, else made from the gather's sheets
(standin's bitmaps from before on) through the builder's pixel path -
textures.dat read, reduced to the level's limit, as the level's file would
hold them */
H2_ALIGN static boolean fx_pixels_cache_make(struct build *standin, char const *map_name, long before,
	struct bitmap const *originals)
{
	char file[96], level_path[512];
	struct fx_cache w;
	unsigned long limit = h2_level_texture_limit(standin);
	unsigned long long started = vita_host_time_us();
	long i, images = 0;
	boolean ok = TRUE;

	snprintf(file, sizeof(file), "%s.map", map_name);
	h2_level_folder_file(file, level_path, sizeof(level_path));
	if (fx_cache_matches(level_path))
	{
		platform_log("h2 effects: %s: its effects' pixel cache is in place", map_name);
		return TRUE;
	}
	if (!fx_cache_begin(&w, level_path))
	{
		platform_log("h2 effects: %s: its effects' pixel cache cannot be written", map_name);
		return FALSE;
	}
	for (i = before; i < standin->bitmap_count && ok; i++)
	{
		struct bitmap b;
		unsigned char *p;
		unsigned long got;

		if (!h2_effects_bitmap_at_load(i, before))
			continue;
		/* (as the gather described it - its full size; bitmap_pixels sizes
		it down as the builder did, reading as much) */
		b = originals[i - before];
		/* (one the gather made - a DXT3 sheet made DXT5, h2fx_image_dxt3 -:
		its pixels as they are, as the builder writes them) */
		if (b.pixels)
		{
			b.size = b.pixels_size;
			fx_cache_entry(&w, b.path, &b, b.pixels, b.pixels_size);
			images++;
			continue;
		}
		if (!bitmap_pixels(standin, &b, limit, &p, &got))
		{
			platform_log("h2 effects: %s: the pixels of %s cannot be read from textures.dat (reference %08lx, %lu bytes, %ux%u, %u mips, "
				"format %u, type %d, tile %u, usage %u, h2 tag %ld)", map_name, b.path, b.reference, b.size, (unsigned)b.width,
				(unsigned)b.height, (unsigned)b.mipmaps, (unsigned)b.format, (int)b.type, (unsigned)b.tile_u, (unsigned)b.usage, b.h2_tag);
			ok = FALSE;
			break;
		}
		fx_cache_entry(&w, b.path, &b, p, got);
		window_free(p, 0);
		images++;
	}
	ok = fx_cache_end(&w, level_path, ok && !w.failed) && ok;
	platform_log("h2 effects: %s: its effects' pixel cache %s (%ld sprite sheet images) in %.0f ms", map_name,
		ok ? "made from textures.dat" : "could NOT be made", images, (double)(vita_host_time_us() - started) / 1000.0);
	return ok;
}

/* (at load) the models a gather decoded into the stand-in build, and their
textures, let go; its bitmaps' and shaders' numbering starts again (the
context's are placed after) */
H2_ALIGN static void fx_standin_models_free(struct build *b)
{
	long i, p;

	for (i = 0; i < b->model_count; i++)
	{
		for (p = 0; p < b->models[i].part_count; p++)
		{
			free(b->models[i].parts[p].vertices);
			free(b->models[i].parts[p].strip);
			free(b->models[i].parts[p].nodes);
		}
		free(b->models[i].parts);
		free(b->models[i].regions);
	}
	free(b->models);
	b->models = NULL;
	b->model_count = 0;
	for (i = 0; i < b->bitmap_count; i++)
	{
		free(b->bitmaps[i].pixels);
		free(b->bitmaps[i].sidecar_pixels);
	}
	free(b->bitmaps);
	b->bitmaps = NULL;
	b->bitmap_count = 0;
	b->shader_count = 0;
}

/* (h2_load_tags.c, maker 'h2fx') the effects' tags first..first + count - 1
made: gathered from the level's MCC map with its context, written by
h2_effects_write and h2_effects_bitmaps_write on the loaded tags (a region
of the tag cache, its room after the map's tags) */
boolean h2_effects_load_make(struct h2_load_tags *make, long first, long count, char const *map_name)
{
	long tags = h2_load_tags_count(make), i, context = NONE;
	unsigned char *base = (unsigned char *)h2_load_tags_link(make);
	unsigned char const *blob = NULL;
	unsigned long blob_size = 0, used, limit, instances;
	struct fxc_header h;
	struct build *standin = NULL;
	struct bitmap *originals = NULL;
	struct h2_level_build_result result;
	struct region loaded;
	unsigned long *names = NULL, *names_before = reference_names;
	long name_count_before = reference_name_count;
	char const *directory = h2_maps_directory_of(map_name);
	char why[160];
	boolean ok = FALSE;
	unsigned long long started = vita_host_time_us();

	/* its context: the string list of its name */
	for (i = 0; i < tags && context == NONE; i++)
	{
		unsigned char const *instance = h2_load_tags_instance(make, i);
		char const *name = (char const *)read32(instance + 16);

		if (read32(instance) == 'str#' && name && !strcmp(name, H2_EFFECTS_CONTEXT_NAME))
			context = i;
	}
	if (context != NONE)
	{
		unsigned char const *list = (unsigned char const *)read32(h2_load_tags_instance(make, context) + 20);
		unsigned char const *element = list && read32(list) == 1 ? (unsigned char const *)read32(list + 4) : NULL;

		if (element)
		{
			blob_size = read32(element);
			blob = (unsigned char const *)read32(element + 12);
		}
	}
	if (!blob || blob_size < sizeof(h))
	{
		platform_log("h2 effects: %s: no context to make its effects with", map_name);
		return FALSE;
	}
	memcpy(&h, blob, sizeof(h));
	if (h.magic != FXC_MAGIC || h.version != FXC_VERSION || h.sizes != FXC_SIZES || h.fx_first != first ||
		h.tag_count != count || h.bitmaps_before < 0 || h.decals_before < 0 || h.object_count < 0 || h.shader_count < 0 ||
		blob_size != sizeof(h) + (unsigned long)h.bitmaps_before * sizeof(struct fxc_bitmap) +
			(unsigned long)h.decals_before * sizeof(struct fxc_decal) + (unsigned long)h.object_count * sizeof(struct fxc_object) +
			(unsigned long)h.shader_count * sizeof(struct fxc_shader))
	{
		platform_log("h2 effects: %s: its effects' context is of another build of the game", map_name);
		return FALSE;
	}
	/* the build as the effects' gathering found it */
	standin = calloc(1, sizeof(*standin));
	memset(&result, 0, sizeof(result));
	if (!standin || !directory || !(standin->cache = h2_cache_open(directory, map_name, why, sizeof(why))))
		goto done;
	standin->result = &result;
	standin->sbsp = h.sbsp;
	standin->multiplayer = (boolean)h.multiplayer;
	/* the weapons and vehicles the gathering reads (their models' markers and
	nodes, barrels and bursts, the vehicles' damage definitions, their
	sounds): gathered from the map as h2_models_load.inc's maker does, then
	their models' geometry, textures and shaders let go - the context's
	bitmaps and shaders are the build's, in their numbering */
	h2w_gather_models_only = TRUE;
	h2_weapons_gather(standin);
	h2w_gather_models_only = FALSE;
	fx_standin_models_free(standin);
	/* (the map's chunks the weapons' gather inflated let go before the
	effects' gather reads across the map: on the Vita the C heap is small at
	load, and a chunk that cannot be inflated reads as nothing - a tag the
	gather then misses, and the level is refused) */
	h2_cache_trim(standin->cache);
	if (result.why[0])
	{
		platform_log("h2 effects: %s: its weapons could not be gathered for its effects: %s", map_name, result.why);
		goto done;
	}
	if (h2w.count != h.weapon_count || h2v.count != h.vehicle_count)
	{
		platform_log("h2 effects: %s: %ld weapons and %ld vehicles gathered - the level was made with %ld and %ld: made again",
			map_name, h2w.count, h2v.count, h.weapon_count, h.vehicle_count);
		goto done;
	}
	standin->bitmaps = calloc((size_t)h.bitmaps_before + 1, sizeof(*standin->bitmaps));
	standin->decals = calloc((size_t)h.decals_before + 1, sizeof(*standin->decals));
	standin->objects = calloc((size_t)h.object_count + 1, sizeof(*standin->objects));
	if (!standin->bitmaps || !standin->decals || !standin->objects ||
		h.shader_count > (long)(sizeof(standin->shaders) / sizeof(standin->shaders[0])))
	{
		goto done;
	}
	{
		unsigned char const *at = blob + sizeof(h);

		for (i = 0; i < h.bitmaps_before; i++, at += sizeof(struct fxc_bitmap))
		{
			struct fxc_bitmap const *f = (struct fxc_bitmap const *)at;
			struct bitmap *b = &standin->bitmaps[i];

			b->h2_tag = f->h2_tag, b->reference = f->reference, b->size = f->size;
			b->width = f->width, b->height = f->height, b->mipmaps = f->mipmaps, b->format = f->format, b->flags = f->flags;
			b->usage = f->usage, b->tile_u = f->tile_u;
		}
		standin->bitmap_count = h.bitmaps_before;
		for (i = 0; i < h.decals_before; i++, at += sizeof(struct fxc_decal))
		{
			struct fxc_decal const *f = (struct fxc_decal const *)at;
			struct decal_made *d = &standin->decals[i];

			d->h2_tag = f->h2_tag, d->bitmap = f->bitmap, d->effect = (boolean)f->effect, d->extent = f->extent, d->flags = f->flags;
			d->chain = f->chain;
		}
		standin->decal_count = h.decals_before;
		for (i = 0; i < h.object_count; i++, at += sizeof(struct fxc_object))
		{
			struct fxc_object const *f = (struct fxc_object const *)at;
			struct object_made *o = &standin->objects[i];
			int k;

			o->h2_tag = f->h2_tag, o->collision = f->collision, o->explosive = (boolean)f->explosive, o->group = f->group;
			for (k = 0; k < 3; k++)
				o->stage_effect[k] = f->stage_effect[k];
			snprintf(o->name, sizeof(o->name), "%s", f->name);
		}
		standin->object_count = h.object_count;
		for (i = 0; i < h.shader_count; i++, at += sizeof(struct fxc_shader))
		{
			struct fxc_shader const *f = (struct fxc_shader const *)at;

			standin->shaders[i].h2_tag = f->h2_tag, standin->shaders[i].group = f->group;
		}
		standin->shader_count = h.shader_count;
	}
	h2_effects_gather(standin);
	if (h2fx.tier != h.tier)
	{
		platform_log("h2 effects: %s: its effects were made for particle tier %ld, this game's is %d: made again", map_name, h.tier,
			(int)h2fx.tier);
		goto done;
	}
	if (standin->bitmap_count != h.bitmaps_after || standin->decal_count != h.decals_after || h2_effects_tag_count() != count)
	{
		platform_log("h2 effects: %s: gathered %ld bitmaps, %ld decals, %ld tags - the level was made with %ld, %ld, %ld: "
			"made again", map_name, standin->bitmap_count, standin->decal_count, h2_effects_tag_count(), h.bitmaps_after,
			h.decals_after, count);
		goto done;
	}
	/* the sprite sheets' images as the level's file keeps them - no more
	than its texture limit a side: the builder wrote their particles (a
	sprite's size is of its sheet's width) after its bitmaps' pixels were
	made, sized so; what the gather described (their full size) kept for
	the pixel cache, which reads from that */
	originals = calloc((size_t)(standin->bitmap_count - h.bitmaps_before) + 1, sizeof(*originals));
	if (!originals)
		goto done;
	for (i = h.bitmaps_before; i < standin->bitmap_count; i++)
	{
		originals[i - h.bitmaps_before] = standin->bitmaps[i];
		if (h2_effects_bitmap_at_load(i, h.bitmaps_before))
			bitmap_limit_dimensions(standin, &standin->bitmaps[i], h2_level_texture_limit(standin), NULL, NULL);
	}
	/* the references' names: the loaded table's */
	names = calloc((size_t)tags + 1, sizeof(*names));
	if (!names)
		goto done;
	for (i = 0; i < tags; i++)
		names[i] = read32(h2_load_tags_instance(make, i) + 16);
	reference_names = names;
	reference_name_count = tags;
	/* the loaded tags as a region: offsets from the tag cache's base, its
	room after the map's tags, never moved */
	h2_load_tags_room(make, &used, &limit);
	/* (from a 16-byte boundary, as the builder's region and the models'
	makers: the room's end after a maker is any byte - every block made here
	then lies as it would in a saved level, which the Vita's readers need) */
	used = (used + 15) & ~15UL;
	if (used > limit)
		goto done;
	memset(&loaded, 0, sizeof(loaded));
	loaded.b.data = base;
	loaded.b.size = used;
	loaded.b.capacity = limit;
	loaded.b.fixed = TRUE;
	loaded.link = (unsigned long)base;
	instances = read32(base) - (unsigned long)base;
	h2_effects_write(standin, &loaded, instances, h.fx_first, h.vessel_first, h.bitmap_first, h.decal_first);
	if (!loaded.b.failed && !result.why[0])
		h2_effects_bitmaps_write(standin, &loaded, instances, h.bitmap_first);
	if (loaded.b.failed || result.why[0])
	{
		platform_log("h2 effects: %s: its effects could not be made at load: %s", map_name,
			result.why[0] ? result.why : "no room left in the tag cache");
		goto done;
	}
	h2_load_tags_room_used(make, loaded.b.size);
	platform_log("h2 effects: %s: %ld tags made at load (%lu KB; %ld weapons, %ld vehicles gathered for them) in %.0f ms", map_name,
		count, (loaded.b.size - used) / 1024, h2w.count, h2v.count, (double)(vita_host_time_us() - started) / 1000.0);
	/* (their sprite sheets' pixels, which the level does not carry: the
	cache beside it, made now when it is not there - its reads
	h2_texture_sidecar.c's once the level is up; a cache that cannot be made
	leaves the sheets without pixels, not the level unplayable) */
	if (h.pixels_at_load)
		fx_pixels_cache_make(standin, map_name, h.bitmaps_before, originals);
	ok = TRUE;
done:
	reference_names = names_before;
	reference_name_count = name_count_before;
	free(names);
	free(originals);
	h2_effects_dispose();
	h2_weapons_dispose();
	if (standin)
	{
		if (standin->cache)
			h2_cache_close(standin->cache);
		/* (the debris the gather made of the map's crates: their models) */
		fx_standin_models_free(standin);
		free(standin->bitmaps);
		free(standin->decals);
		free(standin->objects);
		free(standin);
	}
	return ok;
}

/* ---------- the proof: every tag of a loaded level hashed, position-free

HALO_H2_TAGS_HASH=<file>: as a level's tags are loaded (after those made at
load, before the tag check), each tag of its table is hashed - its root's
bytes with its pointers as zeros (the layouts' words: blocks' addresses,
datas', references' names, pointers), then what its blocks and datas hold,
in their order - and a line a tag appended to the file: its index, group,
name, bytes hashed, hash. Two loads of one level whose tags were made
differently (saved in the level, or made as it loads) agree line for line
when the tags are the same bytes wherever they lie. */

struct fx_hash
{
	unsigned char const *lo, *hi;   /* the tag cache's bytes */
	unsigned long long h;
	unsigned long bytes, outside;
	/* (words not hashed: a bitmap's images' pixel offsets, file positions) */
	unsigned char const *skip[16];
	long skip_count;
};

H2_ALIGN static void fx_hash_bytes(struct fx_hash *x, unsigned char const *p, unsigned long n)
{
	unsigned long i;

	for (i = 0; i < n; i++)
		x->h = (x->h ^ p[i]) * 0x100000001B3ULL;
	x->bytes += n;
}

H2_ALIGN static void fx_hash_mark(unsigned short layout, unsigned char *mask, unsigned long size, unsigned long at, int depth)
{
	long count, i;

	if (layout == HALO_TAG_LAYOUT_NONE || layout == HALO_TAG_LAYOUT_UNKNOWN || depth > 16)
		return;
	count = halo_tag_layout_field_count(layout);
	for (i = 0; i < count; i++)
	{
		struct halo_tag_layout_field field;
		unsigned long f, word = 0;

		if (!halo_tag_layout_field(layout, i, &field))
			continue;
		f = at + field.offset;
		switch (field.kind)
		{
		case HALO_TAG_LAYOUT_FIELD_BLOCK: word = f + 4; break;
		case HALO_TAG_LAYOUT_FIELD_DATA: word = f + 12; break;
		case HALO_TAG_LAYOUT_FIELD_REFERENCE: word = f + 4; break;
		case HALO_TAG_LAYOUT_FIELD_POINTER: word = f; break;
		case HALO_TAG_LAYOUT_FIELD_STRUCT:
			fx_hash_mark(field.child, mask, size, f, depth + 1);
			continue;
		default:
			continue;
		}
		if (word + 4 <= size)
			memset(mask + word, 255, 4);
	}
}

H2_ALIGN static void fx_hash_children(struct fx_hash *x, unsigned short layout, unsigned char const *p, unsigned long size, int depth);

H2_ALIGN static void fx_hash_struct(struct fx_hash *x, unsigned short layout, unsigned char const *p, unsigned long size, int depth)
{
	unsigned char *mask;
	unsigned long b;

	if (p < x->lo || p >= x->hi || size > (unsigned long)(x->hi - p))
	{
		x->outside++;
		return;
	}
	mask = calloc(1, size ? size : 1);
	if (!mask)
		return;
	fx_hash_mark(layout, mask, size, 0, 0);
	{
		long k;

		for (k = 0; k < x->skip_count; k++)
		{
			if (x->skip[k] >= p && x->skip[k] + 4 <= p + size)
				memset(mask + (x->skip[k] - p), 255, 4);
		}
	}
	for (b = 0; b < size; b++)
	{
		unsigned char c = mask[b] == 255 ? 0 : p[b];

		x->h = (x->h ^ c) * 0x100000001B3ULL;
	}
	x->bytes += size;
	free(mask);
	fx_hash_children(x, layout, p, size, depth);
}

H2_ALIGN static void fx_hash_children(struct fx_hash *x, unsigned short layout, unsigned char const *p, unsigned long size, int depth)
{
	long count, i;

	if (layout == HALO_TAG_LAYOUT_NONE || layout == HALO_TAG_LAYOUT_UNKNOWN || depth > 16)
		return;
	count = halo_tag_layout_field_count(layout);
	for (i = 0; i < count; i++)
	{
		struct halo_tag_layout_field field;
		unsigned long f;

		if (!halo_tag_layout_field(layout, i, &field) || (unsigned long)field.offset + 16 > size)
			continue;
		f = field.offset;
		if (field.kind == HALO_TAG_LAYOUT_FIELD_STRUCT)
			fx_hash_children(x, field.child, p + f, size - f, depth + 1);
		else if (field.kind == HALO_TAG_LAYOUT_FIELD_BLOCK)
		{
			long n = (long)read32(p + f), e;
			unsigned char const *a = (unsigned char const *)read32(p + f + 4);

			fx_hash_bytes(x, p + f, 4);
			if (n <= 0 || !field.child_size)
				continue;
			if (a < x->lo || a >= x->hi || (unsigned long)n * field.child_size > (unsigned long)(x->hi - a))
			{
				x->outside++;
				continue;
			}
			for (e = 0; e < n; e++)
				fx_hash_struct(x, field.child, a + field.child_size * (unsigned long)e, field.child_size, depth + 1);
		}
		else if (field.kind == HALO_TAG_LAYOUT_FIELD_DATA)
		{
			unsigned long n = read32(p + f);
			unsigned char const *a = (unsigned char const *)read32(p + f + 12);

			fx_hash_bytes(x, p + f, 4);
			if (!n)
				continue;
			if (a < x->lo || a >= x->hi || n > (unsigned long)(x->hi - a))
			{
				x->outside++;
				continue;
			}
			fx_hash_bytes(x, a, n);
		}
	}
}

/* a group's root size: the validator's schema's */
H2_ALIGN static long fx_hash_root_size(unsigned long group)
{
	long l, g;

	for (l = 0; tag_schema_group_lists[l]; l++)
	{
		for (g = 0; tag_schema_group_lists[l][g].group_tag; g++)
		{
			if (tag_schema_group_lists[l][g].group_tag == group && tag_schema_group_lists[l][g].definition)
				return tag_schema_group_lists[l][g].definition->size;
		}
	}
	return 0;
}

static char const *fx_hash_suffix = "";
void h2_tags_hash_dump(unsigned char *tags, unsigned long tag_data_size, char const *map_name);

/* (diagnostic) a bitmap tag's images: their records' words and the CRC-32
of the pixels at each's offset in the level's file */
H2_ALIGN static void fx_hash_images(FILE *f, long tag, char const *name, unsigned char const *images, long n,
	char const *map_name)
{
	char file[96], level_path[512];
	FILE *level;
	long k;

	snprintf(file, sizeof(file), "%s.map", map_name ? map_name : "?");
	h2_level_folder_file(file, level_path, sizeof(level_path));
	level = fopen(level_path, "rb");
	for (k = 0; k < n; k++)
	{
		unsigned char const *o = images + 48 * (unsigned long)k;
		unsigned long offset = read32(o + 0x18), size = read32(o + 0x1C), crc = 0;
		unsigned char *p = size && size < (8UL << 20) ? malloc(size) : NULL;

		if (p && level && fseek(level, (long)offset, SEEK_SET) == 0 && fread(p, 1, size, level) == size)
			crc = crc32(crc32(0L, Z_NULL, 0), p, (uInt)size);
		free(p);
		fprintf(f, "IMG %ld %s #%ld %dx%d d%d t%d f%d fl%04x reg%d,%d m%d ld%d size %lu tag %08lx cbi %08lx hw %08lx base %08lx crc %08lx\n",
			tag, name ? name : "?", k, (short)read16(o + 4), (short)read16(o + 6), (short)read16(o + 8), (short)read16(o + 0xA),
			(short)read16(o + 0xC), (unsigned)read16(o + 0xE), (short)read16(o + 0x10), (short)read16(o + 0x12), (short)read16(o + 0x14),
			(short)read16(o + 0x16), size, read32(o + 0x20), read32(o + 0x24), read32(o + 0x28), read32(o + 0x2C), crc);
	}
	if (level)
		fclose(level);
}

void h2_tags_hash_dump_after(unsigned char *tags, unsigned long tag_data_size, char const *map_name)
{
	fx_hash_suffix = ".after";
	h2_tags_hash_dump(tags, tag_data_size, map_name);
	fx_hash_suffix = "";
}

void h2_tags_hash_dump(unsigned char *tags, unsigned long tag_data_size, char const *map_name)
{
	char const *path = getenv("HALO_H2_TAGS_HASH");
	FILE *f;
	unsigned char const *instances;
	long count, i;
	unsigned long long whole = 0xCBF29CE484222325ULL;
	unsigned long sizeless = 0;
	char file[300];

	if (!path || !path[0] || !tags)
		return;
	count = (long)read32(tags + 12);
	if (read32(tags) < (unsigned long)tags || read32(tags) - (unsigned long)tags > tag_data_size)
	{
		platform_log("h2 tags hash: %s: the tag table's address %08lx is not in the tags (%08lx, %lu bytes)", map_name ? map_name : "?",
			read32(tags), (unsigned long)tags, tag_data_size);
		return;
	}
	instances = tags + (read32(tags) - (unsigned long)tags);
	/* (the game's file paths: the levels' folder, HALO_H2_TAGS_HASH its file's name) */
	h2_level_folder_create();
	snprintf(file, sizeof(file), "%s\\%s%s", H2_LEVEL_FOLDER, path, fx_hash_suffix);
	if (!(f = fopen(file, "a")))
	{
		platform_log("h2 tags hash: %s cannot be written (%s)", file, strerror(errno));
		return;
	}
	fprintf(f, "# %s: %ld tags, %lu bytes of tag data\n", map_name ? map_name : "?", count, tag_data_size);
	for (i = 0; i < count; i++)
	{
		unsigned char const *instance = instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)i;
		unsigned long group = read32(instance);
		char const *name = (char const *)read32(instance + 16);
		unsigned char const *data = (unsigned char const *)read32(instance + 20);
		long size = fx_hash_root_size(group);
		struct fx_hash x;
		char pixels[64];

		memset(&x, 0, sizeof(x));
		x.lo = tags;
		x.hi = tags + tag_data_size;
		x.h = 0xCBF29CE484222325ULL;
		pixels[0] = 0;
		if (!size)
			sizeless++;
		/* (a bitmap's images' pixel offsets are positions in the level's file,
		not its content: left out of the hash, the first's printed) */
		if (group == 'bitm' && data && data + 0x68 <= x.hi && data >= x.lo)
		{
			long n = (long)read32(data + 0x60), k;
			unsigned char const *images = (unsigned char const *)read32(data + 0x64);

			if (n > 0 && images >= x.lo && images + 48 * (unsigned long)n <= x.hi)
			{
				for (k = 0; k < n && k < 16; k++)
					x.skip[x.skip_count++] = images + 48 * (unsigned long)k + 0x18;
				snprintf(pixels, sizeof(pixels), " pixels %lu %lu", read32(images + 0x18), read32(images + 0x1C));
				/* (diagnostic, HALO_H2_TAGS_HASH_IMAGES=1: each image's record
				and the CRC-32 of its pixels as the level's file has them) */
				if (getenv("HALO_H2_TAGS_HASH_IMAGES"))
					fx_hash_images(f, i, name, images, n, map_name);
			}
		}
		/* (a structure BSP's data is not among the tags) */
		if (size > 0 && group != 'sbsp' && data)
			fx_hash_struct(&x, halo_tag_layout_root(group), data, (unsigned long)size, 0);
		fprintf(f, "%ld %c%c%c%c %s %lu %016llx%s%s%s\n", i, (int)(group >> 24), (int)(group >> 16 & 255), (int)(group >> 8 & 255),
			(int)(group & 255), name ? name : "?", x.bytes, (unsigned long long)x.h, data ? "" : " (no data)",
			x.outside ? " (pointers outside)" : "", pixels);
		whole = (whole ^ x.h) * 0x100000001B3ULL;
	}
	fprintf(f, "# whole %016llx (%lu tags of groups without a schema size)\n", whole, sizeless);
	fclose(f);
	platform_log("h2 tags hash: %s: %ld tags hashed to %s", map_name ? map_name : "?", count, file);
}

#endif /* H2_LEVEL_INTERNAL */
