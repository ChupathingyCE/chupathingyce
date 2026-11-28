/*
H2_VEHICLE_DAMAGE.H

port: Halo 2's vehicle damage sections on a Halo 2 level (h2_vehicle_damage.c).

The level builder (h2_vehicles.c) reads each converted vehicle's model
(hlmt) damage info - its damage sections, their instant responses, and its
default variant's regions' permutations' damage states - from the player's
MCC maps and hands them here (h2_vehicle_damage_define); the game applies
them as a vehicle is hurt: a section's health falls, its responses set
their regions' damage states (the region's permutation for that state, its
looping effect), play their transition effects and throw Halo 2's garbage.
*/

#ifndef __H2_VEHICLE_DAMAGE_H
#define __H2_VEHICLE_DAMAGE_H

/* ---------- constants */

enum
{
	H2VD_DEFINITION_SECTIONS = 16,
	H2VD_DEFINITION_RESPONSES = 10,
	H2VD_DEFINITION_REGIONS = 16,
	H2VD_DEFINITION_NAMES = 64,             /* effect and debris names a vehicle's damage uses */
	H2VD_DEFINITION_DEBRIS = 3,             /* pieces a response's transition effect throws */
	H2VD_STATES = 5,                        /* default, minor, medium, major damage, destroyed */

	/* a response's type (Halo 2's) */
	_h2vd_response_all_damage = 0,
	_h2vd_response_area_damage,
	_h2vd_response_local_damage,

	/* a section's flags (Halo 2's, those read here) */
	_h2vd_section_absorbs_body_damage_bit = 0,
	_h2vd_section_takes_full_damage_when_object_dies_bit = 1,
	_h2vd_section_cannot_die_with_riders_bit = 2,

	/* a response's flags (Halo 2's, those read here) */
	_h2vd_response_kills_object_bit = 0,
	_h2vd_response_inhibits_melee_attack_bit = 1,
	_h2vd_response_inhibits_weapon_attack_bit = 2,
	_h2vd_response_inhibits_walking_bit = 3,
	_h2vd_response_forces_drop_weapon_bit = 4,
	_h2vd_response_kills_weapon_primary_trigger_bit = 5,
	_h2vd_response_kills_weapon_secondary_trigger_bit = 6,
	_h2vd_response_destroys_object_bit = 7,
	_h2vd_response_damages_weapon_primary_trigger_bit = 8,
	_h2vd_response_damages_weapon_secondary_trigger_bit = 9,
	_h2vd_response_light_damage_left_turn_bit = 10,
	_h2vd_response_major_damage_left_turn_bit = 11,
	_h2vd_response_light_damage_right_turn_bit = 12,
	_h2vd_response_major_damage_right_turn_bit = 13,
	_h2vd_response_light_damage_engine_bit = 14,
	_h2vd_response_major_damage_engine_bit = 15,
	_h2vd_response_kills_object_no_player_solo_bit = 16,
	_h2vd_response_causes_detonation_bit = 17,
	_h2vd_response_destroy_all_group_constraints_bit = 18,
	_h2vd_response_kills_variant_objects_bit = 19,
	_h2vd_response_forces_unattached_effects_bit = 20,
	_h2vd_response_fires_under_threshold_bit = 21,
	_h2vd_response_triggers_special_death_bit = 22,
	_h2vd_response_only_on_special_death_bit = 23,
	_h2vd_response_only_not_on_special_death_bit = 24,
};

/* ---------- structures */

/* a piece of garbage a response's transition effect throws (Halo 2's
effect part of a crate or garbage object: effect_part_create's 'obje' case,
unknown_175bd0.cpp 0x17a8a0 - made at its location's marker, facing the
marker's way, its speed between the part's velocity bounds along the
marker's forward turned by up to its cone angle, its spin a random axis at
between its angular velocity bounds, the effect's velocity added) */
struct h2vd_definition_debris
{
	short name;                     /* (names[]: h2\vehicles\debris\<name>; NONE: none) */
	char marker[24];                /* its location's marker (the effect's "replace" its response's effect marker; empty: its region's middle) */
	float velocity[2];              /* world units a second */
	float cone;                     /* radians */
	float angular_velocity[2];      /* radians a second */
};

struct h2vd_definition_response
{
	short type;                     /* _h2vd_response_... */
	short state;                    /* the region's new damage state (0..4) */
	unsigned long flags;
	float threshold;                /* its section's health (a fraction) at or under which it fires */
	float total_threshold;          /* (nonzero) one damage of at least this much of the vehicle's vitality fires it */
	float delay;                    /* seconds */
	char region[24];                /* (empty: none) */
	short effect;                   /* its transition effect (names[]; NONE none) */
	short damage_effect;            /* its transition damage effect (names[]; NONE) */
	short delay_effect;             /* (names[]; NONE) */
	struct h2vd_definition_debris debris[H2VD_DEFINITION_DEBRIS];   /* garbage its transition effect throws (name NONE: no more) */
	char effect_marker[24];         /* the marker its transition effect is made at (Halo 2's effect marker name; empty: its region's middle) */
	char eject[24];                 /* the seat whose rider it throws out (empty: none) */
};

struct h2vd_definition_section
{
	char name[24];
	float vitality;                 /* its share of the vehicle's */
	unsigned long flags;
	short response_count;
	struct h2vd_definition_response responses[H2VD_DEFINITION_RESPONSES];
};

struct h2vd_definition_region
{
	char name[24];
	char permutations[H2VD_STATES][24];     /* its base permutation's permutation for each state (empty: none) */
	short looping_effects[H2VD_STATES];     /* (names[]; NONE) */
	char looping_markers[H2VD_STATES][24];  /* the marker each plays at (its state's looping effect marker name; empty: the region's middle) */
};

struct h2vd_definition
{
	char vehicle[48];               /* the converted vehicle's tag (h2\vehicles\<name>) */
	float vitality;
	short indirect_section;         /* (NONE) the section area damage goes to */
	short section_count;
	struct h2vd_definition_section sections[H2VD_DEFINITION_SECTIONS];
	short region_count;
	struct h2vd_definition_region regions[H2VD_DEFINITION_REGIONS];
	short name_count;
	char names[H2VD_DEFINITION_NAMES][80];  /* effects: h2\effects\... (h2_effects.c's names); debris: their names */
};

/* ---------- prototypes */

/* (the builder) a converted vehicle's damage, kept for the level */
void h2_vehicle_damage_define(struct h2vd_definition const *definition);
/* (the builder) a vehicle's (its tag's) name in Halo 2's words */
void h2_vehicle_prompt_name_define(char const *vehicle, char const *name);
void h2_vehicle_prompt_names_clear(void);
/* (the builder) a definition packed to be kept in the level (the second
string of the vehicle's "<name> physics" list): its bytes in out; 0 if
size is too small */
long h2_vehicle_damage_pack(struct h2vd_definition const *definition, unsigned char *out, long size);
/* (the builder) the definitions forgotten: a new level is being made */
void h2_vehicle_damage_definitions_clear(void);

#endif // __H2_VEHICLE_DAMAGE_H
