/*
OPENSAUCE_TRANSFORM_TAGS.H

OpenSauce's actor variant transform tags (actor_variant_transform_collection,
actor_variant_transform_in and actor_variant_transform_out: avtc, avti,
avto), as the tag validator checks them (port/linux/game/tag_schema_opensauce.c)
and the transforms read them (port/linux/game/opensauce_transforms.c). Their
layouts, sizes and block maxima are OpenSauce's (Kornner Studios,
GPL-3.0-or-later): its unit_transform_definition.hpp, which asserts their
sizes, and its actor_variant_transform_*.xml tag definitions, read as facts.
*/

#ifndef OPENSAUCE_TRANSFORM_TAGS_H
#define OPENSAUCE_TRANSFORM_TAGS_H

#include "tag_files/tag_groups.h"

/* ---------- structures */

struct opensauce_transform_keyframe_action
{
	short keyframe;
	short rider_handling;
	short target;
	word pad;
	struct tag_reference damage_effect;
	struct tag_reference effect;
	char effect_marker[32];
};

struct opensauce_transform_in_target
{
	char target_name[32];
	word flags;
	word pad;
	real selection_chance[4];
	struct tag_reference actor_variant;
	short encounter_squad_handling;
	word pad2;
	short team_handling;
	short team_override;
	short initial_state_handling;
	short initial_state_override;
	short return_state_handling;
	short return_state_override;
	word pad3[2];
	char transform_in_animation[32];
	struct tag_block keyframe_actions;
	unsigned long pad4;
	short vitality_inheritance;
	short vitality_override;
	real shield_override;
	real health_override;
	byte pad5[24];
};

struct opensauce_transform_in
{
	struct tag_block targets;
	byte pad[24];
};

struct opensauce_transform_out_instigator
{
	struct tag_reference unit;
	short damage_type;
	word pad;
	byte pad2[12];
};

struct opensauce_transform_out_attachment
{
	struct tag_reference object_type;
	char object_marker[32];
	char destination_marker[32];
	short destination_marker_count;
	word pad;
	short team_handling;
	short team_override;
	real attachment_scale[2];
	byte pad2[24];
};

struct opensauce_transform_out
{
	word flags;
	word pad;
	word criteria_flags;
	word pad2;
	word actor_action;
	word actor_state;
	real shield_range[2];
	real health_range[2];
	byte pad3[24];
	struct tag_block instigators;
	char transform_out_animation[32];
	struct tag_block keyframe_actions;
	word attachment_flags;
	word pad4;
	struct tag_block attachments;
	byte pad5[24];
};

struct opensauce_transform_collection_transform
{
	word flags;
	word pad;
	char transform_name[32];
	real selection_chance[4];
	struct tag_reference transform_out;
	/* (runtime: OpenSauce's pointer to the tag's definition) */
	unsigned long transform_out_definition;
	struct tag_reference transform_in;
	unsigned long transform_in_definition;
	byte pad2[24];
};

struct opensauce_transform_collection_entry
{
	struct tag_reference actor_variant;
	struct tag_block transforms;
	byte pad[24];
};

struct opensauce_transform_collection
{
	struct tag_block actor_variant_transforms;
	byte pad[24];
};

/* (OpenSauce's sizes: its structures' asserts and its tag definitions) */
typedef char verify_opensauce_keyframe_action_size[sizeof(struct opensauce_transform_keyframe_action) == 0x48 ? 1 : -1];
typedef char verify_opensauce_transform_in_target_size[sizeof(struct opensauce_transform_in_target) == 0xAC ? 1 : -1];
typedef char verify_opensauce_transform_in_size[sizeof(struct opensauce_transform_in) == 0x24 ? 1 : -1];
typedef char verify_opensauce_instigator_size[sizeof(struct opensauce_transform_out_instigator) == 0x20 ? 1 : -1];
typedef char verify_opensauce_attachment_size[sizeof(struct opensauce_transform_out_attachment) == 0x78 ? 1 : -1];
typedef char verify_opensauce_transform_out_size[sizeof(struct opensauce_transform_out) == 0x94 ? 1 : -1];
typedef char verify_opensauce_collection_transform_size[
	sizeof(struct opensauce_transform_collection_transform) == 0x74 ? 1 : -1];
typedef char verify_opensauce_collection_entry_size[sizeof(struct opensauce_transform_collection_entry) == 0x34 ? 1 : -1];
typedef char verify_opensauce_collection_size[sizeof(struct opensauce_transform_collection) == 0x24 ? 1 : -1];

#endif
