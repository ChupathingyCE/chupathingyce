/*
H2_LOAD_TAGS.H

Tags made as a level loads (h2_load_tags.c): a Halo 2 level's builder
reserves ranges of its tag table - their instances named and grouped, each
with no data (base address 0) - and says so in the level's header (0x780:
'h2lt', a range count, then up to H2_LOAD_TAGS_MAXIMUM_RANGES (4) ranges of four
words: maker, first index, count, the room it reserved). As the level's tags
load (cache_files.c's scenario_tags_load: read and relocated, before they are
checked), each range's maker makes its tags' data after the map's tags in the
tag cache, from the player's own MCC map, and places each: the tags then are
checked as any (tag_validate.c) and play as made ones. So what a maker makes
is never saved in the level, and a fix to it needs no new level.

The builder (h2_level.c):
	h2_load_tags_header_add(header, maker, first, count, reserve)
		a range of the table, its tags' instances written with base 0
	h2_load_tags_header_reserve(header)
		the room all ranges reserved: added to the header's tag cache room
		(0x730)
A maker (its case in h2_load_tags.c's h2_load_tags_maker_call):
	boolean maker(struct h2_load_tags *make, long first, long count, char const *map_name)
	h2_load_tags_alloc(make, size)      zeroed, 16-aligned room, NULL if none
	h2_load_tags_place(make, index, root)  an index of its range given its data
	h2_load_tags_link(make)             the tag cache's address (pointers written
	                                     in made data are real addresses)
	h2_load_tags_instance(make, index)  an instance (32 bytes: group x3, id, name,
	                                     base) of the level's table
	h2_load_tags_count(make)            the table's tags
	h2_load_tags_room / room_used       the room as offsets, for a maker that
	                                     writes it through its own buffer
A maker that fails, or leaves an index of its range with no data, refuses the
level (it is made again).
*/

#ifndef __H2_LOAD_TAGS_H
#define __H2_LOAD_TAGS_H

#include "cseries.h"

enum
{
	H2_LOAD_TAGS_HEADER_OFFSET = 0x780,
	H2_LOAD_TAGS_MAGIC = 0x68326C74,       /* 'h2lt' */
	H2_LOAD_TAGS_MAXIMUM_RANGES = 4,   /* (0x780..0x7C7: the kit's record after, h2_level.c H2_KIT_RECORD_OFFSET) */
	/* (makers' names) */
	H2_LOAD_TAGS_MAKER_TEST = 0x68327473,  /* 'h2ts': HALO_H2_LOAD_TAGS_TEST's string list */
	H2_LOAD_TAGS_MAKER_EFFECTS = 0x68326678, /* 'h2fx': h2_effects.c */
	H2_LOAD_TAGS_MAKER_MODELS = 0x68326D64,  /* 'h2md': h2_models_load.inc (h2_level.c) */
	H2_LOAD_TAGS_MAKER_WEAPON_MODELS = 0x68326D77, /* 'h2mw': the weapons' and vehicles' models, h2_models_load.inc */
};

struct h2_load_tags;

/* (the builder) a range of the level's table made at load, reserve bytes of
room for it: FALSE if the header has room for no more ranges */
boolean h2_load_tags_header_add(unsigned char *header, unsigned long maker, long first, long count, unsigned long reserve);
/* the room the header's ranges reserved */
unsigned long h2_load_tags_header_reserve(unsigned char const *header);

/* (cache_files.c, the tags read and relocated at tags, tag_data_size bytes,
the cache file's header at file_header) every range made, *tag_data_size
grown past what was made; FALSE (why logged): the level is refused. Nothing
for a map whose header names no range */
boolean h2_load_tags_make(unsigned char const *file_header, unsigned char *tags, unsigned long *tag_data_size,
	unsigned long tag_cache_size, char const *map_name);

/* (a maker's) the vertex and index buffer table its models' parts draw
from (a D3DVertexBuffer/D3DIndexBuffer each: Common, Data, Lock): checked
with the map's tags (tag_validate.c) and registered after the check, as the
level's own are (h2_load_tags_buffers_register: cache_files.c). FALSE: no
room for another table */
boolean h2_load_tags_buffers_add(void *vertex_buffers, long vertex_buffer_count, void *index_buffers, long index_buffer_count);
/* (cache_files.c, once the tags are checked) the makers' tables registered */
void h2_load_tags_buffers_register(void);

/* (a maker's) */
void *h2_load_tags_alloc(struct h2_load_tags *make, unsigned long size);
boolean h2_load_tags_place(struct h2_load_tags *make, long index, void *root);
unsigned long h2_load_tags_link(struct h2_load_tags const *make);
unsigned char *h2_load_tags_instance(struct h2_load_tags const *make, long index);
long h2_load_tags_count(struct h2_load_tags const *make);
/* (a maker that writes the room itself - a region of the builder's: its
offsets from the tag cache's base) the room's next byte and its end, and
the next byte once it has written */
void h2_load_tags_room(struct h2_load_tags const *make, unsigned long *used, unsigned long *limit);
void h2_load_tags_room_used(struct h2_load_tags *make, unsigned long used);

#endif
