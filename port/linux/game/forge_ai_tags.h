/*
FORGE_AI_TAGS.H

AI characters on the multiplayer maps in Forge (forge_ai_tags.c): their tags
brought in from the player's own campaign maps as a Forge game of a
multiplayer map loads, beside the map's.

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef __FORGE_AI_TAGS_H
#define __FORGE_AI_TAGS_H

#include "cseries.h"

/* a pack's category in the Forge menu: AI, CAMPAIGN VEHICLES, CAMPAIGN
OBJECTS */
enum
{
	FORGE_AI_PACK_AI = 0,
	FORGE_AI_PACK_VEHICLES,
	FORGE_AI_PACK_OBJECTS,
};

/* (cache_files.c, the end of scenario_tags_load) the characters brought in,
if this is a Forge game of a multiplayer map; (scenario_tags_unload, after
the texture and sound caches close) all of it let go */
void forge_ai_tags_loaded(
	char const *scenario_name);
void forge_ai_tags_unloaded(
	void);
void forge_ai_tags_map_unloaded(
	void);

/* (forge.c, each update before the session starts) the characters noted at
the map's load brought in, once: TRUE if they were this time; whether they
wait */
boolean forge_ai_tags_update(
	void);
boolean forge_ai_tags_pending(
	void);

/* (cache_files_windows.c, cache_file_read) a read of one of the brought-in
tags' pixels or samples, served at once (*read: whether it was kept); FALSE
if the tag is the map's own */
boolean forge_ai_tags_read(
	long tag_index,
	long offset,
	long size,
	void *buffer,
	boolean *read);

/* whether any characters were brought in with this map */
boolean forge_ai_tags_active(
	void);

/* the campaign map a brought-in tag came from ("b30"), NULL for the map's */
char const *forge_ai_tags_source_map(
	long tag_index);

/* the factions: how many, each one's name and the campaign map its
characters came from (NULL: none of its maps is there, or it could not be
read) */
short forge_ai_tags_faction_count(
	void);
boolean forge_ai_tags_faction(
	short faction,
	char const **name,
	char const **map);
/* a faction's (a pack's) category: FORGE_AI_PACK_*; whether the Forge menu
offers a tag (the map's, a character, or a pack's own object) */
short forge_ai_tags_faction_category(
	short faction);
boolean forge_ai_tags_offered(
	long tag_index);
/* the faction an actor variant (or a pack's object) is one of, NONE */
short forge_ai_tags_faction_of(
	long tag_index);

/* the memory the brought-in tags, pixels and samples hold, and how long
bringing them in took */
unsigned long forge_ai_tags_bytes(
	void);
unsigned long forge_ai_tags_data_bytes(
	void);
unsigned long forge_ai_tags_load_milliseconds(
	void);

#endif
