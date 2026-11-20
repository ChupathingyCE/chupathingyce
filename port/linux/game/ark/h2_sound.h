/*
H2_SOUND.H

Halo 2 (MCC)'s own sound effects in a Halo 2 level played by this engine
(h2_sound.c): its weapons', vehicles', footsteps', materials', grenades',
melee, the players' foley, grunts and deaths, the shields', read from the
player's sounds_neutral.dat through the sound gestalt of the maps beside the
level's file (zanzibar.map and shared.map, h2_gestalt.c).

The level's cache file (h2_level.c) carries this engine's stand-in sounds,
the Custom Edition ones of the globals' pack; nothing of Halo 2's sound is in
it (its 49 MB limit is the tag cache's, and the sound cache is another). As
the level loads, each stand-in sound named in the table (h2_sound_table.inc)
is given Halo 2's: its pitch range's permutations made of the Halo 2 sound's,
their samples the chunks in the dat - no byte of them copied - and its
gain, distances, random pitch and skip fraction Halo 2's where it sets them.
The sound cache makes the Xbox ADPCM the mixer plays as it loads a
permutation, on the sound decoding thread (custom_edition_sounds.c): Opus
decoded, mixed to mono, taken to 22.05 kHz and encoded, and the result kept
in a sound cache file (d:\sound_cache\h2_<map>.sndcache, CRC-32 checked) that
the thread fills in the background, the shortest first; Xbox ADPCM at the
right format is copied as it lies in the dat.

The tags are changed in memory, so nothing about them is in a cache file that
another branch's converter makes, and a stand-in sound with no row is left
alone.
*/

#ifndef __H2_SOUND_H
#define __H2_SOUND_H

#include "cseries.h"
#include "math/real_math.h"

/* ---------- constants */

enum
{
	/* the compression a permutation of Halo 2's has here (a sound cache
	source: custom_edition_sounds.c makes it Xbox ADPCM) */
	H2_SOUND_PERMUTATION_COMPRESSION = 0x4832,
	H2_SOUND_OUTPUT_RATE = 22050,
	/* the most permutations a pitch range gets (the played mask is 32 bits) */
	H2_SOUND_RANGE_PERMUTATIONS = 32,
	/* the events of a machine (h2_sound_machine_event) */
	H2_SOUND_MACHINE_OPEN = 1,
	H2_SOUND_MACHINE_OPENED,
	H2_SOUND_MACHINE_PRESSED,
	H2_SOUND_MACHINE_COLLIDE,
	H2_SOUND_MACHINE_CLOSE,
	H2_SOUND_MACHINE_CLOSED
};

/* ---------- structures */

struct sound_permutation;
struct h2_sound_codec_stream;

/* a source (a permutation's stream) of the sound cache */
struct h2_sound_source_info
{
	unsigned long dat;
	/* the permutation's samples' pad: its dat, channels and rate, which tell
	streams of the same bytes made in two formats apart */
	unsigned long pad;
	int channels;
	long rate;
	unsigned long dat_offset;
	unsigned long raw_bytes;
	unsigned long adpcm_bytes;
	/* nonzero: copied as it lies, needing no entry in the sound cache file */
	int copied;
	struct sound_permutation const *permutation;
};

/* ---------- prototypes/H2_SOUND.C */

/* A level just loaded: if it is a Halo 2 one (its cache file is made by
h2_level.c), its stand-in sounds in the table are given Halo 2's, and the
sound cache file opened. Nothing happens without audio, without the dat or
for another map. */
void h2_sounds_map_loaded(
	char const *map_name);
/* The level goes: the sound decoding stopped, the tables freed. */
void h2_sounds_unloaded(void);

/* ---------- the hooks: Halo 2's ambience, machines and announcer lines */

/* The sound tag (a pool sound of the level, given the Halo 2 sound) of a Halo
2 sound named as the maps name it without "sound\\" (e.g.
"dialog\\multiplayer\\general\\misc\\betrayal"); NONE if the level has none: the
table's H2_SOUND_VESSEL rows and the scenario's scenery and machines are
there. Played with sound_new_impulse and the rest of the engine's calls. */
long h2_sound_vessel(
	char const *h2_name);
/* (h2_scripts.c) the looping sound tag of the level carrying the Halo 2
looping sound of this name (the maps' name, e.g.
"scenarios\\solo\\03a_oldmombasa\\03a_music\\03a_01"): its tracks Halo 2's,
played by this engine's looping sounds (scripted_looping_sound_start...);
NONE if none */
long h2_sound_looping(
	char const *h2_name);
/* (h2_scripts.c, h2_ai.c) the length in ticks of a script sound or a
mission dialogue line, carried or not (the longest permutation, from the
level's gestalt at load - Halo 2's sound_impulse_language_time knows every
sound's); -1 for one not known */
long h2_sound_length_ticks(
	char const *h2_name);
/* (h2_scripts.c) a script's sound, named as above: the pool sound carrying
it, made to play its own permutation next, and that permutation's length;
FALSE if none carries it */
boolean h2_sound_script_sound(
	char const *h2_name,
	long *tag_index,
	real *seconds);
/* a Halo 2 sound (as above) played once at a place; FALSE if there is none or
it is out of hearing */
boolean h2_sound_play_at(
	char const *h2_name,
	real_point3d const *position);
/* a Halo 2 sound (as above, or the level's own sound\\h2\\<name>) played
once on an object (its root node: it moves with it); the sound (sound_stop_impulse stops it), NONE if there is
none or it cannot be heard; *seconds its length */
/* (h2_sound_play_on_object's object for a sound heard unspatialized) */
#define H2_SOUND_UNSPATIALIZED (-2L)
void h2_sound_radio_mark(char const *h2_name, long tag_index);

/* a Halo 2 sound played unspatialized - a radio line -: the sound, NONE if
it could not be (its length in seconds) */
long h2_sound_play_unspatialized(
	char const *h2_name,
	real *seconds);

long h2_sound_play_on_object(
	char const *h2_name,
	long object_index,
	real *seconds);
/* what a machine of the level (the name of its definition tag, e.g.
"house_gate") does when it opens, has opened, is pressed or collides: its
sound, at `position`. For the code that animates the machines. */
boolean h2_sound_machine_event(
	char const *machine,
	int event,
	real_point3d const *position);
/* a unit's pain or death sound as its voice: a Halo 2 Elite's own (the unit's definition tag has "elite" in its
name), else `sound_definition_index` (unit_dialogue.c) */
/* (h2_dialogue.c) a voice's line - a Halo 2 sound tag of the level's map,
the voice's dialogue tag - played on a unit through a voice slot: the sound
index (NONE: not heard) and the line's seconds; a permutation other than
the last (*last_permutation, the time it played) within repetition_ticks */
long h2_sound_voice_play(
	long voice_tag,
	long sound_tag,
	long unit_index,
	short *last_permutation,
	long last_time,
	long repetition_ticks,
	real *seconds);
/* the length of a line's longest permutation, seconds (0: none) */
real h2_sound_voice_seconds(
	long voice_tag,
	long sound_tag);
long h2_sound_voice(
	char const *unit_definition_name,
	short vocalization_type,
	long sound_definition_index);
/* every frame of the game: the scenery, machines' loops and the ambience bed
started as they end */
void h2_sounds_update(
	real dt);

/* ---------- the level builder's (h2_level.c) */

/* the level's own sound tags for the Halo 2 sounds that have no stand-in
(the announcer's lines, the scenario's sound scenery and machines, the
vessels): H2_SOUND_VESSEL_TAGS of them, named H2_SOUND_VESSEL_NAME with their
number, copies of a sound of the globals' pack that no tag refers to - a
borrowed stand-in sound played as something else wherever its own tag plays
it (the rocket launcher's melee damage, "thirty seconds remaining") */
enum
{
	H2_SOUND_VESSEL_TAGS = 72,
	/* the music's and the scripts' looping sounds' sound tags (made as the
	vessels are), and the looping sounds (no tracks: h2_sound.c gives them
	Halo 2's tracks, each its in, loop and out as these sound tags) the
	scripts start (sound_looping_start) and the clusters' background sound
	palettes name */
	H2_SOUND_MUSIC_TAGS = 96,
	H2_SOUND_LOOP_TAGS = 32,
	/* the sound environments (empty: h2_sound.c gives them Halo 2's reverbs)
	the structure BSPs' sound environment palettes name - this engine plays a
	cluster's background sound only where its sound environment is one */
	H2_SOUND_ENVIRONMENT_TAGS = 16
};
#define H2_SOUND_VESSEL_NAME "h2\\sound\\vessel %ld"
#define H2_SOUND_MUSIC_NAME "h2\\sound\\music %ld"
#define H2_SOUND_LOOP_NAME "h2\\sound\\loop %ld"
#define H2_SOUND_ENVIRONMENT_NAME "h2\\sound\\environment %ld"

/* TRUE if the table gives this stand-in sound (a tag name of the globals'
pack's) Halo 2's sounds in place of its own (a plain row: not the pool's);
*dialogue (if not NULL) TRUE if those are dialogue, read from the language
dats. The builder leaves such a stand-in's samples out of the level when
the dats are there: Halo 2's play as it. */
boolean h2_sound_table_replaces(
	char const *ce_name,
	boolean *dialogue);

/* ---------- the sound cache's (custom_edition_sounds.c) */

boolean h2_sound_permutation_is_h2(
	struct sound_permutation const *permutation);
/* the Halo 2 stream a permutation is: its chunks live as long as the level */
boolean h2_sound_stream(
	struct sound_permutation const *permutation,
	struct h2_sound_codec_stream *stream);
/* the dat (0: sounds_neutral.dat, then the languages') a permutation's
stream is in */
unsigned long h2_sound_permutation_dat(
	struct sound_permutation const *permutation);
/* reads `size` bytes of dat `dat` at `offset`; nonzero on success */
int h2_sound_read(
	unsigned long dat,
	unsigned long offset,
	unsigned long size,
	void *buffer);
/* the CRC-32 of the stream's raw bytes (all its chunks); FALSE if unreadable */
boolean h2_sound_source_crc(
	unsigned long pad,
	unsigned long dat_offset,
	unsigned long raw_bytes,
	unsigned long *crc);
/* the sources, sorted by their dat offset */
long h2_sound_source_count(void);
boolean h2_sound_source_get(
	long index,
	struct h2_sound_source_info *info);
/* what the sound cache file is for: the map's name, its files' sizes */
boolean h2_sound_cache_source(
	char *name,
	long name_size,
	unsigned long *map_bytes,
	unsigned long *map_crc,
	unsigned long *dat_bytes);
/* working memory for the decoding thread (aligned to 16), NULL if none */
void *h2_sound_work(
	unsigned long *bytes);

#endif
