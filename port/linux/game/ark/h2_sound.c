/*
H2_SOUND.C

Halo 2 (MCC)'s sound effects given to a Halo 2 level's stand-in sounds
(h2_sound.h).

The table (h2_sound_table.inc) says, for a stand-in sound of the level's
tags (its name), the Halo 2 sound tags that play as it: their permutations
(at most H2_SOUND_RANGE_PERMUTATIONS, evenly taken from all of them) become
the stand-in's one pitch range. A permutation's samples are the chunks the
gestalt lists for it, in sounds_neutral.dat or, for dialogue (the players'
grunts and deaths), in the language dats, which hold those with the same bytes:
the dat is the first of them in which the chunk's packets are what its codec's
are, from the first to the last. A permutation's place in the tag is the first
chunk's offset in that dat (its dat and made format in the samples' pad), its
size the chunks' bytes, its buffer size the Xbox ADPCM bytes it is made into:
mono 22.05 kHz, or, for a row asked to keep stereo ("S:" before a name) whose
every Halo 2 sound is, stereo - at 44.1 kHz where the Halo 2 sound is Xbox ADPCM
at that rate already (it is copied as it lies) and 22.05 kHz otherwise. The
sound cache turns it to Xbox ADPCM as it loads it (custom_edition_sounds.c,
h2_sound_codec.c).

Any sound tag of the level named sound\\h2\\<a Halo 2 sound's name> is given that
Halo 2 sound without a row (a branch with a tag for each Halo 2 weapon names the
sounds it gives them so).

The pool: the level's vessel sounds (H2_SOUND_VESSEL_NAME, made for it by
h2_level.c, sounds no tag refers to) carry the
Halo 2 sounds the level has no stand-in for (H2_SOUND_VESSEL rows, and the
sound scenery and machines of the level's scenario): h2_sound_vessel gives
the tag of one by its Halo 2 name, for the code that plays it. The sound
scenery of the scenario (Halo 2's sound_scenery placements, each a looping
sound or a sound whose "details" are played at random times) is played here,
at its place, as positioned impulse sounds that start again as the last one
ends (a looping sound has no object in this port to be attached to), and the
machines' loops (the fan's blades, the wheels) at the machines' places, and
on a multiplayer level a named machine's attached looping sounds on the
machine itself while it exists (Terminal's trains' passing sound). Each is
played at its looping sound's track gain, and at the placement's override
distance bounds where the scenario sets them (Halo 2's sound scenery: a
vessel sound tag for each sound and distances). The level's ambience bed is
the clusters' background sounds (below), as on a campaign level.

Everything this file keeps comes in one block of the Custom Edition memory,
freed as the level goes: the sources (a permutation's stream, sorted by the
offset of its first chunk, which is how the sound cache finds it), their
chunks, and the pitch ranges and permutations of the tags that were changed.
The tags are changed in memory (the pitch range block's address and count, the
definition's few fields) after the engine's tag check, so the cache file has
none of it.

The looping sounds: the scripts' (sound_looping_start and its kin, h2_scripts.c:
the mission music, a cutscene's foley, a horn on its vehicle) and, on a
campaign level, the background sounds its structure BSPs' clusters name, are
given the level's looping sound tags (H2_SOUND_LOOP_NAME, made empty by
h2_level.c): Halo 2's tracks (in, loop, out, alternate loop and out, gains,
fades), each sound a track plays carried by a music sound tag of its own
(H2_SOUND_MUSIC_NAME), kept stereo and as long as it is - the sound cache plays
a permutation longer than it gives one as a chain of links made as they load
(custom_edition_sounds.c), so a two-minute track takes ~1 MB of the cache at a
time, never all of it. This engine's looping sounds play them
(game_sound.c, sound_manager.c): gapless loops, the out on a stop, the
alternate. A cutscene's score (the scripts' sounds of Halo 2's music classes)
is a music sound too. HALO_H2_MUSIC_DUMP=1 logs the looping sounds,
HALO_H2_LOOP_TEST="<seconds>:<name>" starts one, HALO_H2_MUSIC_SOLO=1 silences
all but the music (tests).

HALO_H2_SOUND=0 leaves the stand-ins; HALO_H2_SOUND_DUMP=1 logs every sound
tag of the level (name, class, permutations, rate) and each row's outcome.
*/

/* ---------- headers */

#include "cseries.h"
#ifdef HALO_LINUX
#include "render_epoch.h"
#endif
#include "errors.h"
#include "cseries_windows.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "cache/cache_files.h"
#include "sound/sound_definitions.h"
#include "sound/sound_environment_definitions.h"
#include "sound/sound_classes.h"
#include "sound/game_sound.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "hs/hs_scenario_definitions.h"
#include "structures/structure_bsp_definitions.h"
#include "game/players.h"
#include "objects/objects.h"
#include "sound/sound_manager.h"
#include "camera/observer.h"
#include "h2_sound.h"
#include "h2_gestalt.h"
#include "h2_cache.h"
#include "h2_level.h"
#include "h2_build_heap.h"
#include "custom_edition_cache.h"
#include "cache/physical_memory_map.h"
#include "cache_file_formats.h"
#include "h2_sound_codec.h"
#include "xbox_adpcm_encoder.h"
#include "cache/sound_cache.h"
#include "game/game.h"

boolean sound_cache_sound_in_use(struct sound_permutation *sound);
struct h2_cache *h2_scripts_cache(void);
static void voice_slots_resolve(void);

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void h2_sound_classes_halo2(boolean halo2);

/* (the Vita's and its null build's) the sound loader's budget for the maps'
inflated pieces, KB (HALO_H2_SOUND_SEGMENT_KB; 0: none, as the desktop) */
#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
#define H2_SOUND_SEGMENT_KB_DEFAULT 2048
#else
#define H2_SOUND_SEGMENT_KB_DEFAULT 0
#endif

#undef malloc
#undef free
#undef calloc

/* ---------- constants */

enum
{
	MAXIMUM_ROWS = 640,
	MAXIMUM_NAMES = 8,
	NAME_BYTES = 192,
	SOURCE_MAXIMUM = 6144,
	/* the most Xbox ADPCM a permutation is given (the sound cache's 4 MB:
	OGG_SOUND_MAXIMUM_ADPCM_BYTES), and a long WMA one's links' (as Custom
	Edition's chains': the one playing and the three loaded ahead take 1 MB) */
	H2_SOUND_PERMUTATION_BYTES = 1024 * 1024,
	H2_SOUND_LINK_BYTES = 256 * 1024,
	CHUNK_POOL_MAXIMUM = 32768,
	DAT_NAME_BYTES = 512,
	/* the dats a stream may be in: sounds_neutral.dat (the effects), then the
	languages' */
	DAT_COUNT = 6,
	PLAUSIBLE_BYTES = 8192,
	/* (the codec check's reads at the load: a chunk read in one piece up to
	this - the card's reads, each a seek and a read, were 7 KB apiece and
	Zanzibar's rows 7312 of them, 17 s on the Vita - and each chunk's
	verdict kept, a chunk checked once: piece H) */
	PLAUSIBLE_WINDOW_BYTES = 256 * 1024,
	PLAUSIBLE_MEMO_SLOTS = 4096,
	MAXIMUM_VESSELS = 96 + (24 + 30) * 32 + H2_SOUND_MUSIC_TAGS,
	/* the looping sounds (the scripts' music, foley and horns, the clusters'
	background sounds): their tracks (this engine's most) and the Halo 2
	sounds the tracks play (each a music sound tag) */
	MAXIMUM_LOOPS = H2_SOUND_LOOP_TAGS,
	LOOP_TRACKS = MAXIMUM_TRACKS_PER_LOOPING_SOUND,
	MAXIMUM_MUSIC_SOUNDS = H2_SOUND_MUSIC_TAGS,
	/* the most Xbox ADPCM a permutation is made (the sound cache makes a
	longer one a chain of links: custom_edition_sounds.c) - a music sound's
	or the main menu's; any other's is left out past 1 MB */
	LONG_PERMUTATION_BYTES = 64 * 1024 * 1024,
	SHORT_PERMUTATION_BYTES = 1024 * 1024,
	/* the pool sounds the scripts' sounds may take first (32 a sound): the
	rest stay the scenery's and machines' */
	MAXIMUM_SCRIPT_GROUPS = 24,
	/* the pool sounds the AI's voices take: VOICE_SLOTS reusable permutations
	in two groups (h2_sound_voice_play) */
	VOICE_SLOTS = 2 * H2_SOUND_RANGE_PERMUTATIONS,
	VOICE_SLOT_CHUNKS = 16,
	MAXIMUM_POOL = 160,
	MAXIMUM_EMITTERS = 64,
	SOUND_CLASS_COUNT = 64,
	SOUND_CLASS_BYTES = 120,
	/* the ambience bed's names and the machines' loops, in the table by their names */
	SCENERY_PERIOD_DEFAULT_MINIMUM = 10,
	SCENERY_PERIOD_DEFAULT_MAXIMUM = 30
};

static char const *const dat_names[DAT_COUNT] =
{
	"sounds_neutral.dat", "sounds_en.dat", "sounds_sp.dat", "sounds_de.dat", "sounds_fr.dat", "sounds_it.dat"
};

/* a machine of the scenario (its palette tag's name has this) and the loop it
plays; the object animation code triggers the rest (h2_sound_machine_event) */
static struct
{
	char const *machine;
	char const *loop;
} const machine_loops[] =
{
	{ "blades", "ambience\\device_machines\\zanzibar_big_fan\\fan_blades\\blades\\loop" },
	{ "flywheel", "ambience\\device_machines\\zanzibar_pegs\\zanzibar_pegs\\loop" },
	{ "big_wheel", "ambience\\device_machines\\zanzibar_pegs2\\zanzibar_pegs2\\loop" },
	{ NULL, NULL }
};

/* a machine's events and the one-shot each plays (h2_sound_machine_event) */
static struct
{
	char const *machine;
	int event;
	char const *sound;
} const machine_events[] =
{
	{ "house_gate_control", H2_SOUND_MACHINE_PRESSED, "ambience\\device_machines\\zanzibar_gate_button\\button_push" },
	{ "house_gate", H2_SOUND_MACHINE_CLOSE, "ambience\\device_machines\\zanzibar_house_gate\\house_gate_open" },
	{ "house_gate", H2_SOUND_MACHINE_OPEN, "ambience\\device_machines\\zanzibar_house_gate\\house_gate_open" },
	{ "house_gate", H2_SOUND_MACHINE_OPENED, "ambience\\device_machines\\zanzibar_house_gate\\house_gate_opened" },
	{ "gate_button", H2_SOUND_MACHINE_PRESSED, "ambience\\device_machines\\zanzibar_gate_button\\button_push" },
	{ NULL, 0, NULL }
};

/* ---------- the table */

enum
{
	ROW_PLAIN,
	/* a stand-in sound the game does not use, given to the vessels */
	ROW_POOL,
	/* a Halo 2 sound given to the next free pool sound */
	ROW_VESSEL,
	/* (h2_scripts.c) up to 32 of the level's scripts' sounds given one pool
	sound, the first permutation of each one of its permutations (played by
	forcing it: dialogue lines, one a sound) */
	ROW_SCRIPT_GROUP,
	/* a sound a looping sound's track plays (its in, loop, out...) given the
	next music sound tag, kept stereo, its permutations as long as they are */
	ROW_MUSIC
};

struct row
{
	int kind;
	char const *ce;
	char const *h2;
	float trim_db;
	/* (ROW_SCRIPT_GROUP) which 32 of the scripts' sounds */
	long group;
	/* (an emitter's vessel) its key - the sound's name with its override
	distances, which the vessel's tag is given (0: the sound's own) */
	char const *key;
	float distance_minimum;
	float distance_maximum;
};

/* (h2_scripts.c) the level's scripts' sounds, as the maps name them
without "sound\\" */
long h2_scripts_sound_names(char const **names, long maximum);
static char const *script_sound_names[(24 + 30) * 32];
static long script_sound_count;

/* ---- the AI's voices (h2_dialogue.c): a character's combat dialogue is
the lines of its own dialogue tag - some 300 a voice, 900 permutations -,
too many for the pool (SOURCE_MAXIMUM) and for the Vita; they are played
through VOICE_SLOTS reusable permutations instead (two script groups, seeded
at the load with any dialogue sounds of the maps), each bound to a line's
permutation as it is asked for (h2_sound_voice_play): the line's chunks
read from the gestalt of the level's cache the scripts keep open, a source
of the slot's own, the slot's permutation rewritten (its old samples let
go of the sound cache first) and forced as the one its group plays. A slot
in use (playing or loading) is never taken; the oldest free one is. The
structures follow the sources' (below). */

#define H2_SOUND_ROW(ce, h2, trim) { ROW_PLAIN, ce, h2, trim, 0 },
#define H2_SOUND_POOL(ce) { ROW_POOL, ce, NULL, 0.f },
#define H2_SOUND_VESSEL(h2, trim) { ROW_VESSEL, NULL, h2, trim },
static struct row const static_rows[] =
{
#include "h2_sound_table.inc"
	{ ROW_PLAIN, NULL, NULL, 0.f }
};
#undef H2_SOUND_ROW
#undef H2_SOUND_VESSEL

/* ---------- structures */

struct source
{
	uint32_t dat_offset;
	uint32_t raw_bytes;
	uint32_t frames;
	uint32_t adpcm_bytes;
	uint32_t rate;
	uint32_t out_rate;
	uint16_t first_chunk;
	uint16_t chunk_count;
	uint8_t kind;
	uint8_t channels;
	uint8_t out_channels;
	uint8_t dat;
	struct sound_permutation *permutation;
	/* (a link of a long WMA permutation's chain - the main menu music: its
	first frame in the stream, its index and the chain's links; 0, 0 and 1
	for any other) */
	uint32_t first_frame;
	uint16_t link;
	uint16_t links;
};

struct pending_permutation
{
	uint32_t source;
	char name[32];
	/* (a chain's link after its first: the permutation it follows is the
	pending one before it) and its place in the range */
	uint16_t link;
	uint16_t place;
};

struct pending_sound
{
	long row;
	long tag_index;
	struct h2_snd snd;
	struct h2_snd_playback playback;
	boolean have_playback;
	/* (block 2's: what the sound's scale adds) */
	struct h2_snd_scale scale;
	boolean have_scale;
	int out_channels;
	long out_rate;
	long permutation_first;
	long permutation_count;
	/* (a script group's) the widest of its lines' distances: theirs, not its
	first line's alone (a foley line's 5 units made a group's mission lines
	inaudible past them) */
	float group_minimum, group_maximum;
};

struct h2_caches
{
	struct h2_cache *cache[2];
	struct h2_gestalt *gestalt[2];
};

struct candidate
{
	long which;
	struct h2_snd snd;
	struct h2_snd_permutation permutation;
};

/* a Halo 2 sound carried by a pool sound */
struct vessel
{
	char name[160];
	long tag_index;
	/* (a script sound of a group) its permutation; NONE: any */
	long permutation;
};

/* (the AI's voices, above) a slot and the slots */
struct voice_slot
{
	struct sound_permutation *permutation;    /* NULL: not made */
	long tag_index;
	short index;                              /* in its group's range */
	long sound_tag;                           /* the line bound (NONE) */
	short sound_permutation;
	long bound_time;
};

static struct
{
	char names[VOICE_SLOTS][96];
	long count;
	struct voice_slot slots[VOICE_SLOTS];
	struct source sources[VOICE_SLOTS];
	struct h2_sound_codec_chunk chunks[VOICE_SLOTS * VOICE_SLOT_CHUNKS];
	struct h2_gestalt *gestalt;
	struct h2_cache *gestalt_cache;
	long plays, binds, refused;
} h2_voice;

/* a looping sound of the level (Halo 2's 'lsnd') as this engine's: its
tracks' sounds (indices in the music names), as the maps have them */
enum
{
	_loop_in,
	_loop_loop,
	_loop_out,
	_loop_alternate_loop,
	_loop_alternate_out,
	_loop_alternate_in,
	NUMBER_OF_LOOP_SOUNDS
};

struct loop_plan
{
	char name[160];
	/* (a cluster's background sound) its palette entry's name, and the
	scale the entry plays it at (its interior scale where its flags say so:
	"zanzibar ambience inside" at 0.4; else 1) */
	char palette_name[32];
	boolean palette;
	real scale;
	unsigned long flags;
	long track_count;
	struct
	{
		unsigned long flags;
		float gain_db;
		float fade_in;
		float fade_out;
		short sounds[NUMBER_OF_LOOP_SOUNDS];
	} tracks[LOOP_TRACKS];
};

/* a looping sound given this engine's looping sound tag */
struct loop
{
	char name[160];
	long tag_index;
};

/* a background sound palette entry's name and its looping sound tag; a
sound environment palette entry's name and its sound environment tag */
struct palette_loop
{
	char name[32];
	long tag_index;
};

/* a sound of the level's scenery or machines, played at a place */
struct emitter
{
	long vessel;
	real_point3d position;
	/* a loop starts again as it ends; else a sound that starts at random
	times between period_minimum and period_maximum seconds apart */
	boolean loop;
	boolean unspatialized;
	real period_minimum;
	real period_maximum;
	double next_time;
	/* (a named machine's loop, emitter_plan's) its object name's index + 1,
	the object it plays on now and its sound (NONE: none) */
	short follow_name;
	long object_index;
	long sound_index;
	/* its looping sound's track gain, as a scale of the amplitude */
	real gain;
};

/* ---------- globals */
static long radio_permutation_count;   /* (h2_sound_permutation_radio) */

static double permutation_seconds(struct sound_definition const *d, long k);
void platform_log(const char *format, ...);
unsigned long long vita_host_time_us(void);
void *halo_custom_edition_memory_alloc(unsigned long bytes);
void halo_custom_edition_memory_free(void *address);
int config_boolean(char const *name);
/* (custom_edition_sounds.c) */
void custom_edition_sounds_h2_loaded(void);
void custom_edition_sounds_stop(void);

static struct
{
	boolean loaded;
	char map_name[32];
	unsigned long map_bytes;
	unsigned long map_crc;
	unsigned long dat_bytes;
	unsigned long dat_sizes[DAT_COUNT];
	FILE *dats[DAT_COUNT];
	pthread_mutex_t dat_lock;
	/* (the dats' reads at the load, logged: the card's reads are what a
	load waits on, piece H) */
	unsigned long dat_reads, dat_read_bytes, plausible_checks;
	unsigned long long dat_read_us;
	byte *arena;
	unsigned long arena_bytes;
	struct source *sources;
	long source_count;
	struct h2_sound_codec_chunk *chunks;
	byte *work;
	unsigned long work_bytes;
	struct vessel vessels[MAXIMUM_VESSELS];
	long vessel_count;
	struct emitter emitters[MAXIMUM_EMITTERS];
	long emitter_count;
	struct loop loops[MAXIMUM_LOOPS];
	long loop_count;
	struct palette_loop palette_loops[MAXIMUM_LOOPS];
	long palette_loop_count;
	struct palette_loop palette_environments[MAXIMUM_LOOPS];
	long palette_environment_count;
	double time;
	char ambience_name[40];
	unsigned long random;
	/* a pool sound's sound tags handed out so far */
	long pool_used;
} h2_sound_globals = { FALSE, "", 0, 0, 0, { 0 }, { NULL }, PTHREAD_MUTEX_INITIALIZER };

/* ---------- private code */

static long setting(char const *name, long fallback)
{
	char const *text = getenv(name);

	return text ? atol(text) : fallback;
}

static float db_to_gain(float db)
{
	return (float)pow(10.0, (double)db / 20.0);
}

static float cents_to_ratio(short cents)
{
	return (float)pow(2.0, (double)cents / 1200.0);
}

/* a Halo 2 sound's name without the table's prefixes */
static char const *bare(char const *name)
{
	while ((name[0] == 'S' || name[0] == '1') && name[1] == ':')
		name += 2;
	return name;
}

static double random_unit(void)
{
	h2_sound_globals.random = h2_sound_globals.random * 1664525UL + 1013904223UL;
	return (double)((h2_sound_globals.random >> 8) & 0xFFFFFF) / 16777216.0;
}

/* `size` bytes of dat `dat` at `offset` (the dats are read by the decoding
thread and by the plan: one lock) */
static int dat_read(unsigned long dat, unsigned long offset, unsigned long size, void *buffer)
{
	unsigned long long vita_host_time_us(void);
	int ok = 0;

	if (dat >= DAT_COUNT)
		return 0;
	pthread_mutex_lock(&h2_sound_globals.dat_lock);
	if (h2_sound_globals.dats[dat] && offset <= h2_sound_globals.dat_sizes[dat] && size <= h2_sound_globals.dat_sizes[dat] - offset)
	{
		unsigned long long started = vita_host_time_us();

		ok = fseek(h2_sound_globals.dats[dat], (long)offset, SEEK_SET) == 0 && fread(buffer, 1, size, h2_sound_globals.dats[dat]) == size;
		h2_sound_globals.dat_reads++;
		h2_sound_globals.dat_read_bytes += size;
		h2_sound_globals.dat_read_us += vita_host_time_us() - started;
	}
	pthread_mutex_unlock(&h2_sound_globals.dat_lock);
	return ok;
}

/* whether a whole chunk is what its codec's is in dat `dat`: a run of Opus
packets of a 16-bit length and a CELT TOC byte, the last one's length negated
and ending exactly at the chunk's end; or whole Xbox ADPCM blocks with an
index of 88 at most and a zero pad byte. The dialogue of the language dats is
at offsets where sounds_neutral.dat holds other sounds' chunks, which a look
at a chunk's first packet would take for it. */
struct plausible_memo
{
	uint32_t offset;
	uint32_t bytes;
	uint8_t dat;
	uint8_t opus;
	uint8_t channels;
	uint8_t verdict;   /* (0 an empty slot, 1 not plausible, 2 plausible) */
};

/* (the load's codec checks: their window and verdicts, the loading thread's
alone - source_add's; let go when the rows are found) */
static struct
{
	unsigned char *window;
	struct plausible_memo *memo;
} plausible;

static void plausible_end(void)
{
	free(plausible.window);
	free(plausible.memo);
	plausible.window = NULL;
	plausible.memo = NULL;
}

static boolean chunk_plausible_read(unsigned long dat, boolean opus, int channels, unsigned long offset, unsigned long bytes);

static boolean chunk_plausible(unsigned long dat, boolean opus, int channels, unsigned long offset, unsigned long bytes)
{
	struct plausible_memo *slot = NULL;
	boolean verdict;

	if (!plausible.memo)
		plausible.memo = (struct plausible_memo *)calloc(PLAUSIBLE_MEMO_SLOTS, sizeof(*plausible.memo));
	if (plausible.memo && offset <= 0xFFFFFFFFUL && bytes <= 0xFFFFFFFFUL)
	{
		unsigned long hash = (unsigned long)((offset * 2654435761UL) ^ (bytes * 40503UL) ^ (dat << 3) ^ (opus ? 1 : 0) ^ ((unsigned long)channels << 5));
		long probe;

		for (probe = 0; probe < 64; probe++)
		{
			struct plausible_memo *m = &plausible.memo[(hash + (unsigned long)probe) % PLAUSIBLE_MEMO_SLOTS];

			if (!m->verdict)
			{
				slot = m;
				break;
			}
			if (m->offset == (uint32_t)offset && m->bytes == (uint32_t)bytes && m->dat == (uint8_t)dat &&
				m->opus == (uint8_t)(opus ? 1 : 0) && m->channels == (uint8_t)channels)
			{
				return m->verdict == 2;
			}
		}
	}
	verdict = chunk_plausible_read(dat, opus, channels, offset, bytes);
	if (slot)
	{
		slot->offset = (uint32_t)offset;
		slot->bytes = (uint32_t)bytes;
		slot->dat = (uint8_t)dat;
		slot->opus = (uint8_t)(opus ? 1 : 0);
		slot->channels = (uint8_t)channels;
		slot->verdict = verdict ? 2 : 1;
	}
	return verdict;
}

static boolean chunk_plausible_read(unsigned long dat, boolean opus, int channels, unsigned long offset, unsigned long bytes)
{
	unsigned char small[PLAUSIBLE_BYTES];
	unsigned char *buffer = small;
	unsigned long buffer_bytes = sizeof(small);
	unsigned long p = 0;
	unsigned long window = 0, window_bytes = 0;
	unsigned long block_bytes = (unsigned long)XBOX_ADPCM_BLOCK_BYTES * (unsigned long)channels;

	if (!bytes)
		return FALSE;
	if (!opus && bytes % block_bytes)
		return FALSE;
	h2_sound_globals.plausible_checks++;
	/* (the chunk read in one piece when it fits the window; no memory for
	the window: the small buffer's pieces, as before) */
	if (!plausible.window)
		plausible.window = (unsigned char *)malloc(PLAUSIBLE_WINDOW_BYTES);
	if (plausible.window)
	{
		buffer = plausible.window;
		buffer_bytes = PLAUSIBLE_WINDOW_BYTES;
	}
	while (p < bytes)
	{
		unsigned long need = opus ? 2 + 4096 : block_bytes;
		unsigned char const *at;

		if (need > bytes - p)
			need = bytes - p;
		if (!window_bytes || p < window || p + need > window + window_bytes)
		{
			window = p;
			window_bytes = bytes - p < buffer_bytes ? bytes - p : buffer_bytes;
			if (!dat_read(dat, offset + p, window_bytes, buffer))
				return FALSE;
		}
		at = buffer + (p - window);
		if (opus)
		{
			unsigned long length;
			int last;

			if (need < 3)
				return FALSE;
			length = (unsigned long)at[0] | (unsigned long)at[1] << 8;
			last = length >= 0x8000;
			if (last)
				length = 65536 - length;
			if (!length || length > 4096 || p + 2 + length > bytes || (at[2] & 0xF8) != 0xE8)
				return FALSE;
			p += 2 + length;
			if (last)
				return p == bytes;
		}
		else
		{
			int c;

			for (c = 0; c < channels; c++)
			{
				if (at[4 * c + 2] > 88 || at[4 * c + 3] != 0)
					return FALSE;
			}
			p += block_bytes;
		}
	}
	return !opus;
}

/* the sound tags of the maps by name: each map's own (a map lists the
shared map's tags too, with their data in the shared map: the gestalt that
goes with a tag is its own map's), the level's first */
struct sound_name
{
	char const *name;
	long which;
	struct h2_cache_tag tag;
};

static int sound_name_compare(void const *a, void const *b)
{
	struct sound_name const *x = (struct sound_name const *)a;
	struct sound_name const *y = (struct sound_name const *)b;
	int order = strcmp(x->name, y->name);

	return order ? order : (x->which < y->which ? -1 : x->which > y->which ? 1 : 0);
}

static struct sound_name *sound_names_make(struct h2_caches const *caches, long *count_out)
{
	long capacity = 0, count = 0, c;
	struct sound_name *names;

	for (c = 0; c < 2; c++)
		capacity += caches->cache[c] ? h2_cache_tag_count(caches->cache[c]) : 0;
	names = (struct sound_name *)calloc((size_t)capacity + 1, sizeof(*names));
	if (!names)
		return NULL;
	for (c = 0; c < 2; c++)
	{
		long n, i;

		if (!caches->cache[c])
			continue;
		n = h2_cache_tag_count(caches->cache[c]);
		for (i = 0; i < n; i++)
		{
			struct h2_cache_tag t;

			if (h2_cache_tag_get(caches->cache[c], i, &t) && t.group_tag == 'snd!' && t.address && t.size &&
				h2_cache_address_source(caches->cache[c], t.address) == 0)
			{
				names[count].name = h2_cache_tag_name(caches->cache[c], i);
				names[count].which = c;
				names[count].tag = t;
				count++;
			}
		}
	}
	qsort(names, (size_t)count, sizeof(*names), sound_name_compare);
	*count_out = count;
	return names;
}

static boolean h2_sound_find(
	struct sound_name const *names,
	long count,
	char const *name,
	long *which,
	struct h2_cache_tag *tag)
{
	long low = 0, high = count;

	while (low < high)
	{
		long middle = low + (high - low) / 2;

		if (strcmp(names[middle].name, name) < 0)
			low = middle + 1;
		else
			high = middle;
	}
	if (low < count && !strcmp(names[low].name, name))
	{
		*which = names[low].which;
		*tag = names[low].tag;
		return TRUE;
	}
	return FALSE;
}

static long cap_index(long i, long count)
{
	/* the i-th of H2_SOUND_RANGE_PERMUTATIONS taken evenly from count */
	if (count <= H2_SOUND_RANGE_PERMUTATIONS)
		return i;
	return (long)(((long long)i * count) / H2_SOUND_RANGE_PERMUTATIONS);
}

static char const *source_why;

/* the made format a sound row keeps its candidates in: stereo when asked for
and every candidate is, at 44.1 kHz when every one is Xbox ADPCM at that rate
(copied as it lies), else 22.05 kHz */
static void row_format(
	struct candidate const *candidates,
	long count,
	boolean want_stereo,
	int *channels,
	long *rate)
{
	boolean all_stereo = count > 0, all_copy = count > 0;
	long i;

	for (i = 0; i < count; i++)
	{
		int codec = candidates[i].snd.codec;

		all_stereo = all_stereo && h2_gestalt_codec_channels(codec) == 2;
		all_copy = all_copy && !h2_gestalt_codec_is_opus(codec) && codec != H2_SOUND_CODEC_WMA && h2_gestalt_codec_channels(codec) == 2 &&
			h2_gestalt_codec_rate(codec) == 44100;
	}
	*channels = want_stereo && all_stereo ? 2 : 1;
	*rate = *channels == 2 && all_copy ? 44100 : H2_SOUND_OUTPUT_RATE;
}

/* a source for the permutation (its chunks added to the pool when new);
the index in `sources`, NONE if it cannot be */
static long source_add(
	struct source *sources,
	long *source_count,
	struct h2_sound_codec_chunk *pool,
	long *pool_count,
	struct h2_gestalt const *gestalt,
	struct h2_snd const *snd,
	struct h2_snd_permutation const *permutation,
	int out_channels,
	long out_rate,
	unsigned long maximum_bytes)
{
	int channels = h2_gestalt_codec_channels(snd->codec);
	long rate = h2_gestalt_codec_rate(snd->codec);
	boolean opus = h2_gestalt_codec_is_opus(snd->codec);
	struct h2_snd_chunk chunk;
	struct source s;
	long k, i;
	unsigned long raw = 0;
	unsigned long dat;
	unsigned long first_offset = 0, first_bytes = 0;
	uint32_t wma_frames = 0;

	source_why = "ok";
	if (!channels || !rate || permutation->chunk_count < 1 || permutation->chunk_count > H2_SOUND_MAXIMUM_CHUNKS ||
		permutation->first_chunk < 0 || !permutation->decoded_bytes)
	{
		source_why = "codec, chunk count or decoded bytes";
		return NONE;
	}
	memset(&s, 0, sizeof(s));
	s.first_chunk = (uint16_t)*pool_count;
	s.chunk_count = (uint16_t)permutation->chunk_count;
	if (*pool_count + permutation->chunk_count > CHUNK_POOL_MAXIMUM)
	{
		source_why = "the chunk pool full";
		return NONE;
	}
	for (k = 0; k < permutation->chunk_count; k++)
	{
		if (!h2_gestalt_chunk(gestalt, 0, permutation->first_chunk + k, &chunk) || !chunk.bytes)
		{
			source_why = "chunk unreadable";
			return NONE;
		}
		pool[*pool_count + k].offset = (uint32_t)chunk.offset;
		pool[*pool_count + k].bytes = (uint32_t)chunk.bytes;
		raw += chunk.bytes;
		if (k == 0)
		{
			s.dat_offset = (uint32_t)chunk.offset;
			first_offset = chunk.offset;
			first_bytes = chunk.bytes;
		}
	}
	/* (the dat whose chunk looks like the codec's) */
	for (dat = 0; dat < DAT_COUNT; dat++)
	{
		boolean fits = h2_sound_globals.dats[dat] != NULL;

		for (k = 0; fits && k < permutation->chunk_count; k++)
		{
			fits = pool[*pool_count + k].offset <= h2_sound_globals.dat_sizes[dat] &&
				pool[*pool_count + k].bytes <= h2_sound_globals.dat_sizes[dat] - pool[*pool_count + k].offset;
		}
		/* (WMA - the main menu music: its chunk an AM_MEDIA_TYPE, "auds" first,
		its WAVEFORMATEX at 64; its length the packets' bytes over its average
		bytes a second - the gestalt's decoded bytes are its coded size) */
		if (fits && snd->codec == H2_SOUND_CODEC_WMA)
		{
			unsigned char header[64 + 18];

			if (permutation->chunk_count == 1 && first_bytes > sizeof(header) && dat_read(dat, first_offset, sizeof(header), header) &&
				!memcmp(header, "auds", 4))
			{
				unsigned long format_bytes = header[60] | header[61] << 8;
				unsigned long average = header[72] | header[73] << 8 | (unsigned long)header[74] << 16;

				if (average && format_bytes >= 18 && 64 + format_bytes < first_bytes)
					wma_frames = (uint32_t)((double)(first_bytes - 64 - format_bytes) * (double)rate / (double)average);
				break;
			}
			continue;
		}
		if (fits && chunk_plausible(dat, opus, channels, first_offset, first_bytes))
			break;
	}
	if (dat >= DAT_COUNT)
	{
		source_why = "no dat holds it";
		return NONE;
	}
	s.dat = (uint8_t)dat;
	s.raw_bytes = (uint32_t)raw;
	s.kind = (uint8_t)(opus ? H2_SOUND_CODEC_KIND_OPUS : snd->codec == H2_SOUND_CODEC_WMA ? H2_SOUND_CODEC_KIND_WMA :
		H2_SOUND_CODEC_KIND_XBOX_ADPCM);
	s.channels = (uint8_t)channels;
	s.out_channels = (uint8_t)out_channels;
	s.rate = (uint32_t)rate;
	s.out_rate = (uint32_t)out_rate;
	s.frames = snd->codec == H2_SOUND_CODEC_WMA ? wma_frames : (uint32_t)(permutation->decoded_bytes / (2UL * (unsigned long)channels));
	if (!s.frames)
	{
		source_why = "no frames";
		return NONE;
	}
	{
		uint32_t out = h2_sound_codec_output_frames(s.frames, rate, out_rate);

		if (!out)
		{
			source_why = "no output frames";
			return NONE;
		}
		s.adpcm_bytes = (uint32_t)((out + XBOX_ADPCM_BLOCK_SAMPLES - 1) / XBOX_ADPCM_BLOCK_SAMPLES * XBOX_ADPCM_BLOCK_BYTES *
			(unsigned long)out_channels);
	}
	s.links = 1;
	/* (a permutation longer than the sound cache gives one - 1 MB - is played
	as a chain of links Custom Edition's chains make as they load
	(custom_edition_sounds.c chains_map_loaded): a music track's, the main
	menu music's and any WMA one up to the long maximum; another row's past
	its maximum is left out) */
	if (s.kind == H2_SOUND_CODEC_KIND_WMA && maximum_bytes < LONG_PERMUTATION_BYTES)
		maximum_bytes = LONG_PERMUTATION_BYTES;
	if (s.adpcm_bytes > maximum_bytes)
	{
		source_why = "too long";
		return NONE;
	}
	/* (the same stream already: shared) */
	for (i = 0; i < *source_count; i++)
	{
		if (sources[i].dat_offset == s.dat_offset && sources[i].raw_bytes == s.raw_bytes && sources[i].dat == s.dat &&
			sources[i].out_channels == s.out_channels && sources[i].out_rate == s.out_rate)
		{
			return i;
		}
	}
	if (*source_count >= SOURCE_MAXIMUM)
	{
		source_why = "the sources full";
		return NONE;
	}
	*pool_count += permutation->chunk_count;
	sources[*source_count] = s;
	return (*source_count)++;
}

/* a source's format word: the sound cache's entry key (channels, rate, dat) */
static uint32_t source_format(struct source const *s)
{
	return (uint32_t)s->out_channels | s->out_rate << 8 | (uint32_t)s->dat << 24;
}

/* the sound cache's order: what custom_edition_sounds.c's entries are
sorted by (offset, size, ADPCM bytes, and the format) */
static int source_compare(void const *a, void const *b)
{
	struct source const *x = (struct source const *)a;
	struct source const *y = (struct source const *)b;

	if (x->dat_offset != y->dat_offset)
		return x->dat_offset < y->dat_offset ? -1 : 1;
	if (x->raw_bytes != y->raw_bytes)
		return x->raw_bytes < y->raw_bytes ? -1 : 1;
	if (x->adpcm_bytes != y->adpcm_bytes)
		return x->adpcm_bytes < y->adpcm_bytes ? -1 : 1;
	if (source_format(x) != source_format(y))
		return source_format(x) < source_format(y) ? -1 : 1;
	/* (a chain's links in order) */
	return x->first_frame < y->first_frame ? -1 : x->first_frame > y->first_frame ? 1 : 0;
}

/* a permutation's pad: its dat, channels and whether its rate is 44.1 kHz */
static unsigned long permutation_pad(struct source const *s)
{
	return (unsigned long)s->dat | (unsigned long)s->out_channels << 8 | (s->out_rate == 44100 ? 1UL << 12 : 0) |
		(unsigned long)s->link << 16;
}

static long source_find(unsigned long pad, uint32_t dat_offset, uint32_t raw_bytes)
{
	long low = 0, high = h2_sound_globals.source_count;

	while (low < high)
	{
		long middle = low + (high - low) / 2;
		struct source const *s = &h2_sound_globals.sources[middle];

		if (s->dat_offset < dat_offset || (s->dat_offset == dat_offset && s->raw_bytes < raw_bytes))
			low = middle + 1;
		else
			high = middle;
	}
	for (; low < h2_sound_globals.source_count; low++)
	{
		struct source const *s = &h2_sound_globals.sources[low];

		if (s->dat_offset != dat_offset || s->raw_bytes != raw_bytes)
			break;
		if (permutation_pad(s) == pad)
			return low;
	}
	return NONE;
}

static void close_files(void)
{
	long dat;

	pthread_mutex_lock(&h2_sound_globals.dat_lock);
	for (dat = 0; dat < DAT_COUNT; dat++)
	{
		if (h2_sound_globals.dats[dat])
		{
			fclose(h2_sound_globals.dats[dat]);
			h2_sound_globals.dats[dat] = NULL;
		}
		h2_sound_globals.dat_sizes[dat] = 0;
	}
	pthread_mutex_unlock(&h2_sound_globals.dat_lock);
}

/* the tag whose data holds address (the loaded tag with the nearest start
at or below it), for the log */
static long tag_holding(unsigned long address)
{
	struct tag_iterator iterator;
	long tag_index, best = NONE;
	unsigned long best_start = 0;

	tag_iterator_new(&iterator, NONE);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		unsigned long start = (unsigned long)tag_get(tag_get_group_tag(tag_index), tag_index);

		if (start && start <= address && start >= best_start)
		{
			best_start = start;
			best = tag_index;
		}
	}
	return best;
}

/* the pool's stand-ins (count of them in tags) that no tag of the level
refers to, kept in their order (the count returned): a stand-in some tag
plays (a melee's damage effect's sound, an animation's sound) would play the
Halo 2 sound its pool place gave it there - the announcer's "thirty seconds
remaining" on a melee hit. A sound reference is its group 'snd!', the name
and its length, then the tag's index (the tag instances' own entries have
no parent groups, 0xFFFFFFFF, where a reference has the name's length) */
static long pool_unreferenced(long *tags, long count)
{
	unsigned long base = (unsigned long)physical_memory_get_tag_cache_base_address();
	unsigned long size = TAG_CACHE_SIZE;
	uint32_t const *words;
	unsigned long n, w;
	unsigned char referenced[MAXIMUM_POOL];
	long k, kept = 0;

	if (custom_edition_cache_tags_loaded())
		base = (unsigned long)custom_edition_cache_tag_cache(&size);
	if (!base || count <= 0)
		return count;
	memset(referenced, 0, sizeof(referenced));
	words = (uint32_t const *)base;
	n = size / 4;
	for (w = 0; w + 3 < n; w++)
	{
		if (words[w] != 'snd!' || words[w + 2] == 0xFFFFFFFFUL)
			continue;
		for (k = 0; k < count; k++)
		{
			if (!referenced[k] && words[w + 3] == (uint32_t)tags[k])
			{
				long holder = tag_holding(base + 4 * w);

				referenced[k] = TRUE;
				platform_log("h2 sound: %s is not free for the pool: %s refers to it", tag_get_name(tags[k]),
					holder != NONE ? tag_get_name(holder) : "a tag");
			}
		}
	}
	for (k = 0; k < count; k++)
	{
		if (!referenced[k])
			tags[kept++] = tags[k];
	}
	return kept;
}

static void dump_stand_ins(void)
{
	struct tag_iterator iterator;
	long tag_index;

	tag_iterator_new(&iterator, SOUND_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct sound_definition *d = (struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, tag_index);
		long permutations = 0;
		long r;

		for (r = 0; r < d->pitch_ranges.count; r++)
			permutations += TAG_BLOCK_GET_ELEMENT(&d->pitch_ranges, r, struct sound_pitch_range)->permutations.count;
		platform_log("h2 sound: stand-in %s: class %d rate %d enc %d comp %d ranges %d perms %ld dist %.2f-%.2f gain %.3f "
			"pitch %.3f-%.3f skip %.3f flags %lX",
			tag_get_name(tag_index), (int)d->sound_class, (int)d->sample_rate, (int)d->encoding, (int)d->compression,
			(int)d->pitch_ranges.count, permutations, d->minimum_distance, d->maximum_distance, d->gain_modifier,
			d->random_pitch_bounds.lower, d->random_pitch_bounds.upper, d->skip_fraction, (unsigned long)d->flags);
	}
}

/* Halo 2's sound class table (sound_classes, 120 bytes a class, the same
fields as this engine's from +24: the distances): the class a Halo 2 sound is
of gives the distances its playback record leaves out. `minimum` and
`maximum` (SOUND_CLASS_COUNT each) are 0 where there is none. */
/* Halo 2's sound classes (shared.map's sound\sound_classes, 0x78 a class):
each one's distance bounds at +24 and gain bounds (dB) at +32 - the gain a
sound of the class plays at, its lower bound at scale 0 and its upper at
scale 1 (Halo 2's sound_definition_gain_lower/upper add them to the sound's
own gain modifiers: halo2-upstream-work src/unknown_218c60.cpp; the sound's
scale picks between them, src/unknown_124f90.cpp function_1251e0) */
struct h2_sound_classes
{
	float minimum[SOUND_CLASS_COUNT];
	float maximum[SOUND_CLASS_COUNT];
	float gain_lower_db[SOUND_CLASS_COUNT];
	float gain_upper_db[SOUND_CLASS_COUNT];
	/* its sounds at once of one tag (+0) and of one tag from one object
	(+2), its preemption time (ms, +4), its priority (+12) */
	short per_tag[SOUND_CLASS_COUNT];
	short per_object[SOUND_CLASS_COUNT];
	long preemption[SOUND_CLASS_COUNT];
	short priority[SOUND_CLASS_COUNT];
	boolean read;
};

/* (the level's: h2_sound_classes_halo2 sets this engine's classes from it) */
static struct h2_sound_classes classes;

static void sound_classes_read(struct h2_caches const *caches, struct h2_sound_classes *classes)
{
	long c;
	float *minimum = classes->minimum, *maximum = classes->maximum;

	memset(classes, 0, sizeof(*classes));
	for (c = 0; c < 2; c++)
	{
		long count, i;

		if (!caches->cache[c])
			continue;
		count = h2_cache_tag_count(caches->cache[c]);
		for (i = 0; i < count; i++)
		{
			struct h2_cache_tag t;

			if (h2_cache_tag_get(caches->cache[c], i, &t) && t.group_tag == 'sncl' && t.address && t.size &&
				!strcmp(h2_cache_tag_name(caches->cache[c], i), "sound\\sound_classes"))
			{
				long entries;
				unsigned long elements;

				if (h2_cache_block(caches->cache[c], t.address, SOUND_CLASS_BYTES, &entries, &elements))
				{
					long k;

					for (k = 0; k < entries && k < SOUND_CLASS_COUNT; k++)
					{
						unsigned char const *p = (unsigned char const *)h2_cache_at(caches->cache[c], elements + (unsigned long)k *
							SOUND_CLASS_BYTES, SOUND_CLASS_BYTES);
						union { unsigned long bits; float value; } u;

						if (!p)
							break;
						u.bits = (unsigned long)p[24] | (unsigned long)p[25] << 8 | (unsigned long)p[26] << 16 | (unsigned long)p[27] << 24;
						minimum[k] = u.value;
						u.bits = (unsigned long)p[28] | (unsigned long)p[29] << 8 | (unsigned long)p[30] << 16 | (unsigned long)p[31] << 24;
						maximum[k] = u.value;
						u.bits = (unsigned long)p[32] | (unsigned long)p[33] << 8 | (unsigned long)p[34] << 16 | (unsigned long)p[35] << 24;
						classes->gain_lower_db[k] = u.value >= -64.f && u.value <= 12.f ? u.value : 0.f;
						u.bits = (unsigned long)p[36] | (unsigned long)p[37] << 8 | (unsigned long)p[38] << 16 | (unsigned long)p[39] << 24;
						classes->gain_upper_db[k] = u.value >= -64.f && u.value <= 12.f ? u.value : 0.f;
						classes->per_tag[k] = (short)(p[0] | p[1] << 8);
						classes->per_object[k] = (short)(p[2] | p[3] << 8);
						classes->preemption[k] = (long)((unsigned long)p[4] | (unsigned long)p[5] << 8 | (unsigned long)p[6] << 16 |
							(unsigned long)p[7] << 24);
						classes->priority[k] = (short)(p[12] | p[13] << 8);
					}
					classes->read = TRUE;
					return;
				}
			}
		}
	}
}

/* the scenery and machines of the level's scenario made emitters: each
sound scenery placement's looping sound tag (a Halo 2 'lsnd': its tracks'
loop sounds at the tracks' gains, or its detail sounds, found by their tag
references, a detail's period the two floats after it), each machine's loop;
the emitters' sounds become vessel rows (they need pool sounds) */
struct emitter_plan
{
	/* the Halo 2 sound (the maps' name without "sound\\"), where, how */
	char name[160];
	real_point3d position;
	boolean loop;
	boolean unspatialized;
	real period_minimum;
	real period_maximum;
	/* (a named machine's attached loop) its Halo 2 object name's index + 1:
	played on that object while it exists, not at a place; 0 none */
	short follow_name;
	/* its looping sound's track gain (dB; a detail's 0: Halo 2's details have
	none) */
	real gain_db;
	/* the placement's override distance bounds (0: the sound's own) */
	real distance_minimum;
	real distance_maximum;
	/* its vessel's key (plan_vessel_key) */
	char key[200];
};

/* the vessel key of a plan's sound: its name, and its override distances
when it has them (a vessel sound tag for each sound and distances, as the
distances are the tag's) */
static void plan_vessel_key(struct emitter_plan const *plan, char *out, size_t size)
{
	if (plan->distance_minimum > 0.f && plan->distance_maximum > plan->distance_minimum)
		snprintf(out, size, "%s@%.1f-%.1f", bare(plan->name), plan->distance_minimum, plan->distance_maximum);
	else
		snprintf(out, size, "%s", bare(plan->name));
}

/* a looping sound's tracks' loop sounds and gains (dB): the tracks at 0x24,
0x58 each, the gain at +8, the loop's reference at +0x1C */
struct lsnd_track
{
	long sound;
	float gain_db;
};

static long lsnd_tracks_read(struct h2_cache const *cache, long lsnd, struct lsnd_track *out, long maximum)
{
	struct h2_cache_tag l;
	long tracks, count = 0;
	unsigned long track;

	if (lsnd == NONE || !h2_cache_tag_get(cache, lsnd, &l) || l.group_tag != 'lsnd' || !l.address ||
		!h2_cache_block(cache, l.address + 0x24, 0x58, &tracks, &track))
	{
		return 0;
	}
	for (; tracks > 0 && count < maximum; tracks--, track += 0x58)
	{
		union { unsigned long bits; float value; } gain;
		long loop = h2_cache_reference(cache, track + 0x1C);

		if (loop == NONE)
			continue;
		gain.bits = h2_cache_read32(cache, track + 8);
		out[count].sound = loop;
		out[count].gain_db = gain.value > -64.f && gain.value < 12.f ? gain.value : 0.f;
		count++;
	}
	return count;
}

/* the gain (dB) of the track of a looping sound of the map that plays
`sound` as its loop (the machines' loops, named here without their looping
sounds); 0 if none does */
static float loop_gain_of_sound(struct h2_cache const *cache, long sound)
{
	long count = h2_cache_tag_count(cache), i;

	for (i = 0; i < count; i++)
	{
		struct h2_cache_tag t;
		struct lsnd_track tracks[4];
		long n, k;

		if (!h2_cache_tag_get(cache, i, &t) || t.group_tag != 'lsnd')
			continue;
		n = lsnd_tracks_read(cache, i, tracks, 4);
		for (k = 0; k < n; k++)
		{
			if (tracks[k].sound == sound)
				return tracks[k].gain_db;
		}
	}
	return 0.f;
}

/* the tags of `group` a tag refers to (references are a group and a handle, found by scanning its bytes) */
static long tag_references(struct h2_cache const *cache, long tag_index, unsigned long group, long *out, long maximum)
{
	struct h2_cache_tag t;
	unsigned char const *bytes;
	unsigned long o;
	long count = 0;

	if (!h2_cache_tag_get(cache, tag_index, &t) || !t.address || !t.size || t.size > 4096 ||
		!(bytes = (unsigned char const *)h2_cache_at(cache, t.address, t.size)))
	{
		return 0;
	}
	for (o = 0; o + 8 <= t.size && count < maximum; o += 4)
	{
		unsigned long g = (unsigned long)bytes[o] | (unsigned long)bytes[o + 1] << 8 | (unsigned long)bytes[o + 2] << 16 |
			(unsigned long)bytes[o + 3] << 24;

		if (g == group)
		{
			unsigned long handle = (unsigned long)bytes[o + 4] | (unsigned long)bytes[o + 5] << 8 | (unsigned long)bytes[o + 6] << 16 |
				(unsigned long)bytes[o + 7] << 24;
			long found = h2_cache_tag_from_handle(cache, handle);

			if (found != NONE)
				out[count++] = found;
		}
	}
	return count;
}

static void plan_clear(struct emitter_plan *plan)
{
	memset(plan, 0, sizeof(*plan));
}

static long emitter_plans_read(struct h2_cache const *cache, struct emitter_plan *plans, long maximum, boolean dump)
{
	struct h2_cache_tag scenario;
	long count = 0, n, i;
	unsigned long elements, palette;
	long palette_count;

	if (!h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario))
		return 0;
	/* the sound scenery: placements (0x50 each: the object placement's 0x34 -
	position at +8 -, then the volume type at 0x34, the height at 0x38, the
	override distance bounds at 0x3C, the override cone angles at 0x44 and
	outer cone gain at 0x4C) of palette entries (a tag reference at +0) */
	if (h2_cache_block(cache, scenario.address + 0xE0, 0x28, &palette_count, &palette) &&
		h2_cache_block(cache, scenario.address + 0xD8, 0x50, &n, &elements))
	{
		for (i = 0; i < n && count < maximum - 8; i++)
		{
			unsigned char const *p = (unsigned char const *)h2_cache_at(cache, elements + 0x50UL * (unsigned long)i, 0x50);
			short palette_index;
			long lsnd;
			struct h2_cache_tag t;
			unsigned char const *bytes;
			unsigned long o;
			union { unsigned long bits; float value; } minimum, maximum_distance;
			struct lsnd_track tracks[4];
			long track_count;

			if (!p)
				continue;
			palette_index = (short)(p[0] | p[1] << 8);
			if (palette_index < 0 || palette_index >= palette_count)
				continue;
			lsnd = h2_cache_reference(cache, palette + 0x28UL * (unsigned long)palette_index);
			/* (the palette names a sound scenery object, whose sound is a looping sound) */
			if (lsnd != NONE && h2_cache_tag_get(cache, lsnd, &t) && t.group_tag == 'ssce')
			{
				long found[2];

				if (tag_references(cache, lsnd, 'lsnd', found, 1) < 1)
					continue;
				lsnd = found[0];
			}
			if (lsnd == NONE || !h2_cache_tag_get(cache, lsnd, &t) || !t.address || !t.size || t.size > 4096 ||
				!(bytes = (unsigned char const *)h2_cache_at(cache, t.address, t.size)))
			{
				continue;
			}
			minimum.bits = (unsigned long)p[0x3C] | (unsigned long)p[0x3D] << 8 | (unsigned long)p[0x3E] << 16 | (unsigned long)p[0x3F] << 24;
			maximum_distance.bits = (unsigned long)p[0x40] | (unsigned long)p[0x41] << 8 | (unsigned long)p[0x42] << 16 |
				(unsigned long)p[0x43] << 24;
			if (!(minimum.value > 0.f && maximum_distance.value > minimum.value && maximum_distance.value < 1000.f))
				minimum.value = maximum_distance.value = 0.f;
			track_count = lsnd_tracks_read(cache, lsnd, tracks, 4);
			for (o = 0; o + 16 <= t.size; o += 4)
			{
				unsigned long group = (unsigned long)bytes[o] | (unsigned long)bytes[o + 1] << 8 | (unsigned long)bytes[o + 2] << 16 |
					(unsigned long)bytes[o + 3] << 24;

				if (group == 'snd!')
				{
					unsigned long handle = (unsigned long)bytes[o + 4] | (unsigned long)bytes[o + 5] << 8 |
						(unsigned long)bytes[o + 6] << 16 | (unsigned long)bytes[o + 7] << 24;
					long snd = h2_cache_tag_from_handle(cache, handle);
					char const *name = snd != NONE ? h2_cache_tag_name(cache, snd) : "";
					union { unsigned long bits; float value; } lo, hi;
					long k;
					boolean track = FALSE;

					if (strncmp(name, "sound\\", 6))
						continue;
					lo.bits = (unsigned long)bytes[o + 8] | (unsigned long)bytes[o + 9] << 8 | (unsigned long)bytes[o + 10] << 16 |
						(unsigned long)bytes[o + 11] << 24;
					hi.bits = (unsigned long)bytes[o + 12] | (unsigned long)bytes[o + 13] << 8 | (unsigned long)bytes[o + 14] << 16 |
						(unsigned long)bytes[o + 15] << 24;
					plan_clear(&plans[count]);
					/* (a track's loop, at the track's gain; else a detail, played at
					random times - a track's in, out and alternates are not played) */
					for (k = 0; k < track_count; k++)
					{
						if (tracks[k].sound == snd)
						{
							plans[count].gain_db = tracks[k].gain_db;
							track = TRUE;
						}
					}
					if (!track && strstr(name, "\\loop"))
						continue;
					snprintf(plans[count].name, sizeof(plans[count].name), "%s", name + 6);
					memcpy(&plans[count].position, p + 8, 12);
					plans[count].unspatialized = FALSE;
					plans[count].loop = track;
					plans[count].distance_minimum = minimum.value;
					plans[count].distance_maximum = maximum_distance.value;
					if (track)
					{
						plans[count].period_minimum = plans[count].period_maximum = 0;
					}
					else if (lo.value > 0.f && hi.value >= lo.value && hi.value < 600.f)
					{
						plans[count].period_minimum = lo.value;
						plans[count].period_maximum = hi.value;
					}
					else
					{
						plans[count].period_minimum = SCENERY_PERIOD_DEFAULT_MINIMUM;
						plans[count].period_maximum = SCENERY_PERIOD_DEFAULT_MAXIMUM;
					}
					count++;
					if (count >= maximum)
						return count;
				}
			}
		}
	}
	/* the scenery's and crates' attached looping sounds (an object's
	attachments of a looping sound: a gravity lift's hum, Coagulation's
	base) at its place: each one's tracks' loops at their gains */
	{
		static struct { unsigned long placements, size, palette; } const kinds[] =
		{
			{ 0x50, 0x5C, 0x58 },
			{ 0x328, 0x4C, 0x330 },
		};
		int k;

		for (k = 0; k < (int)(sizeof(kinds) / sizeof(kinds[0])); k++)
		{
			long kind_palette_count, kind_count;
			unsigned long kind_palette, placements;

			if (!h2_cache_block(cache, scenario.address + kinds[k].palette, 0x28, &kind_palette_count, &kind_palette) ||
				!h2_cache_block(cache, scenario.address + kinds[k].placements, kinds[k].size, &kind_count, &placements))
			{
				continue;
			}
			for (i = 0; i < kind_count && count < maximum; i++)
			{
				unsigned char const *p = (unsigned char const *)h2_cache_at(cache, placements + kinds[k].size * (unsigned long)i, 0x14);
				short palette_index;
				long object, loops[4], loop_count, l;

				if (!p)
					continue;
				palette_index = (short)(p[0] | p[1] << 8);
				if (palette_index < 0 || palette_index >= kind_palette_count ||
					(object = h2_cache_reference(cache, kind_palette + 0x28UL * (unsigned long)palette_index)) == NONE)
				{
					continue;
				}
				loop_count = tag_references(cache, object, 'lsnd', loops, 4);
				for (l = 0; l < loop_count && count < maximum; l++)
				{
					struct lsnd_track tracks[4];
					long track_count = lsnd_tracks_read(cache, loops[l], tracks, 4), t;

					for (t = 0; t < track_count && count < maximum; t++)
					{
						char const *name = h2_cache_tag_name(cache, tracks[t].sound);

						if (strncmp(name, "sound\\", 6))
							continue;
						plan_clear(&plans[count]);
						snprintf(plans[count].name, sizeof(plans[count].name), "%s", name + 6);
						memcpy(&plans[count].position, p + 8, 12);
						plans[count].loop = TRUE;
						plans[count].unspatialized = FALSE;
						plans[count].gain_db = tracks[t].gain_db;
						count++;
					}
				}
			}
		}
	}
	/* the machines (position at +8, palette index at +0; the palette's tag name says what it is) */
	{
		long machine_palette_count, machine_count;
		unsigned long machine_palette, machines;

		if (h2_cache_block(cache, scenario.address + 0xB0, 0x28, &machine_palette_count, &machine_palette) &&
			h2_cache_block(cache, scenario.address + 0xA8, 0x48, &machine_count, &machines))
		{
			for (i = 0; i < machine_count && count < maximum; i++)
			{
				unsigned char const *p = (unsigned char const *)h2_cache_at(cache, machines + 0x48UL * (unsigned long)i, 0x48);
				short palette_index;
				long object;
				long m;

				if (!p)
					continue;
				palette_index = (short)(p[0] | p[1] << 8);
				if (palette_index < 0 || palette_index >= machine_palette_count)
					continue;
				object = h2_cache_reference(cache, machine_palette + 0x28UL * (unsigned long)palette_index);
				if (object == NONE)
					continue;
				/* (a named machine's attached looping sounds that no function
				scales - Terminal's trains' passing sound, on the train the
				script makes and destroys: Halo 2 plays an object's attachments
				while it exists. Its tracks' loops, each with the track's gain;
				played on the object, h2_sounds_update. A multiplayer level's
				alone - its netgame flags -: the campaign's machines are left
				as they were) */
				{
					short name_index = (short)(p[2] | p[3] << 8);
					struct h2_cache_tag t;
					long attachment_count = 0, a, netgame_flags = 0;
					unsigned long attachments, flags;

					if (name_index >= 0 && h2_cache_block(cache, scenario.address + 0x118, 0x20, &netgame_flags, &flags) &&
						netgame_flags > 0 && h2_cache_tag_get(cache, object, &t) &&
						h2_cache_block(cache, t.address + 0x94, 0x18, &attachment_count, &attachments))
					{
						for (a = 0; a < attachment_count && count < maximum; a++)
						{
							unsigned long q = attachments + 0x18UL * (unsigned long)a;
							long lsnd = h2_cache_reference(cache, q);
							struct h2_cache_tag l;
							struct lsnd_track tracks[4];
							long track_count, t_index;

							if (lsnd == NONE || !h2_cache_tag_get(cache, lsnd, &l) || l.group_tag != 'lsnd' ||
								h2_cache_read32(cache, q + 0xC) || h2_cache_read32(cache, q + 0x10) || h2_cache_read32(cache, q + 0x14))
							{
								continue;
							}
							track_count = lsnd_tracks_read(cache, lsnd, tracks, 4);
							for (t_index = 0; t_index < track_count && count < maximum; t_index++)
							{
								char const *name = h2_cache_tag_name(cache, tracks[t_index].sound);

								if (strncmp(name, "sound\\", 6))
									continue;
								plan_clear(&plans[count]);
								snprintf(plans[count].name, sizeof(plans[count].name), "%s", name + 6);
								memcpy(&plans[count].position, p + 8, 12);
								plans[count].loop = TRUE;
								plans[count].unspatialized = FALSE;
								plans[count].follow_name = (short)(name_index + 1);
								plans[count].gain_db = tracks[t_index].gain_db;
								count++;
							}
						}
					}
				}
				for (m = 0; machine_loops[m].machine; m++)
				{
					if (strstr(h2_cache_tag_name(cache, object), machine_loops[m].machine))
					{
						char name[192];
						long snd;

						plan_clear(&plans[count]);
						snprintf(plans[count].name, sizeof(plans[count].name), "%s", machine_loops[m].loop);
						memcpy(&plans[count].position, p + 8, 12);
						plans[count].loop = TRUE;
						plans[count].unspatialized = FALSE;
						/* (at the gain of the looping sound of the map that plays it) */
						snprintf(name, sizeof(name), "sound\\%s", machine_loops[m].loop);
						snd = h2_cache_tag_find(cache, 'snd!', name);
						plans[count].gain_db = snd != NONE ? loop_gain_of_sound(cache, snd) : 0.f;
						count++;
						break;
					}
				}
			}
		}
	}
	if (dump)
	{
		for (i = 0; i < count; i++)
		{
			platform_log("h2 sound: emitter plan %ld: %s at %.1f %.1f %.1f %s, %.1f dB, distances %.1f-%.1f%s", i, plans[i].name,
				plans[i].position.x, plans[i].position.y, plans[i].position.z, plans[i].loop ? "loop" : "details", plans[i].gain_db,
				plans[i].distance_minimum, plans[i].distance_maximum, plans[i].distance_minimum > 0.f ? " (the placement's)" : " (the sound's)");
		}
	}
	return count;
}

/* the looping sounds of the level: the scripts' (h2_scripts.c: the music,
a cutscene's foley, a horn) and, on a campaign level, the background sounds
its structure BSPs' clusters name (their palettes: an entry's name, then its
looping sound). A Halo 2 'lsnd' (MCC): its flags at 8, its tracks at 0x24
(0x58 each: flags at 4, gain (dB) at 8, fades in and out at 0xC and 0x10, then
its sounds' references - in 0x14, loop 0x1C, out 0x24, alt loop 0x2C, alt out
0x34, alt trans in 0x40, alt trans out 0x48). The sounds the tracks play go
to `music_names` (the maps' names, each once). HALO_H2_MUSIC_DUMP=1 logs
them. */
long h2_scripts_looping_sound_names(char const **names, long maximum);

static long music_name_index(char (*music_names)[160], long *music_count, char const *name)
{
	long i;

	for (i = 0; i < *music_count; i++)
	{
		if (!strcmp(music_names[i], name))
			return i;
	}
	if (*music_count >= MAXIMUM_MUSIC_SOUNDS)
		return NONE;
	snprintf(music_names[*music_count], 160, "%s", name);
	return (*music_count)++;
}

static boolean loop_plan_read(
	struct h2_caches const *caches,
	long which,
	long tag_index,
	struct loop_plan *plan,
	char (*music_names)[160],
	long *music_count,
	boolean dump)
{
	static unsigned long const sound_offsets[NUMBER_OF_LOOP_SOUNDS] = { 0x14, 0x1C, 0x24, 0x2C, 0x34, 0x40 };
	struct h2_cache *cache = caches->cache[which];
	struct h2_cache_tag t;
	long count, k, sounds = 0;
	unsigned long tracks;

	if (!h2_cache_tag_get(cache, tag_index, &t) || t.group_tag != 'lsnd' || t.size < 0x2C ||
		!h2_cache_block(cache, t.address + 0x24, 0x58, &count, &tracks))
	{
		return FALSE;
	}
	plan->flags = h2_cache_read32(cache, t.address + 8);
	plan->track_count = 0;
	for (k = 0; k < count && plan->track_count < LOOP_TRACKS; k++)
	{
		unsigned long e = tracks + 0x58UL * (unsigned long)k;
		union { unsigned long bits; float value; } gain, fade_in, fade_out;
		long q;

		gain.bits = h2_cache_read32(cache, e + 8);
		fade_in.bits = h2_cache_read32(cache, e + 0xC);
		fade_out.bits = h2_cache_read32(cache, e + 0x10);
		plan->tracks[plan->track_count].flags = h2_cache_read32(cache, e + 4);
		plan->tracks[plan->track_count].gain_db = gain.value > -60.f && gain.value < 12.f ? gain.value : 0.f;
		plan->tracks[plan->track_count].fade_in = fade_in.value >= 0.f && fade_in.value < 60.f ? fade_in.value : 0.f;
		plan->tracks[plan->track_count].fade_out = fade_out.value >= 0.f && fade_out.value < 60.f ? fade_out.value : 0.f;
		for (q = 0; q < NUMBER_OF_LOOP_SOUNDS; q++)
		{
			long snd = h2_cache_reference(cache, e + sound_offsets[q]);
			char const *name = snd != NONE ? h2_cache_tag_name(cache, snd) : "";

			plan->tracks[plan->track_count].sounds[q] = NONE;
			if (!strncmp(name, "sound\\", 6))
			{
				plan->tracks[plan->track_count].sounds[q] = (short)music_name_index(music_names, music_count, name + 6);
				sounds += plan->tracks[plan->track_count].sounds[q] != NONE;
			}
		}
		if (dump)
		{
			platform_log("h2 music: %s track %ld: flags %lx, gain %.1f dB, fades %.1f %.1f s: in %s, loop %s, out %s, alt loop %s, "
				"alt out %s, alt in %s", plan->name, k, plan->tracks[plan->track_count].flags, plan->tracks[plan->track_count].gain_db,
				plan->tracks[plan->track_count].fade_in, plan->tracks[plan->track_count].fade_out,
				plan->tracks[plan->track_count].sounds[0] != NONE ? music_names[plan->tracks[plan->track_count].sounds[0]] : "-",
				plan->tracks[plan->track_count].sounds[1] != NONE ? music_names[plan->tracks[plan->track_count].sounds[1]] : "-",
				plan->tracks[plan->track_count].sounds[2] != NONE ? music_names[plan->tracks[plan->track_count].sounds[2]] : "-",
				plan->tracks[plan->track_count].sounds[3] != NONE ? music_names[plan->tracks[plan->track_count].sounds[3]] : "-",
				plan->tracks[plan->track_count].sounds[4] != NONE ? music_names[plan->tracks[plan->track_count].sounds[4]] : "-",
				plan->tracks[plan->track_count].sounds[5] != NONE ? music_names[plan->tracks[plan->track_count].sounds[5]] : "-");
		}
		plan->track_count++;
	}
	return sounds > 0;
}

static long loop_plans_read(
	struct h2_caches const *caches,
	struct loop_plan *plans,
	char (*music_names)[160],
	long *music_count,
	boolean dump)
{
	char const *names[MAXIMUM_LOOPS];
	long n = h2_scripts_looping_sound_names(names, MAXIMUM_LOOPS), i, count = 0;
	struct scenario const *scenario = NULL;

	/* (the level's scenario tag: the scenario is not the game's yet) */
	{
		struct tag_iterator iterator;
		long scenario_index;

		tag_iterator_new(&iterator, SCENARIO_TAG);
		scenario_index = tag_iterator_next(&iterator);
		if (scenario_index != NONE)
			scenario = (struct scenario const *)tag_get(SCENARIO_TAG, scenario_index);
	}

	*music_count = 0;
	/* (the dump: the scripts named for the music, to wake one in a test) */
	if (dump && scenario)
	{
		for (i = 0; i < scenario->hs_scripts.count; i++)
		{
			struct hs_script const *script = TAG_BLOCK_GET_ELEMENT(&scenario->hs_scripts, i, struct hs_script);

			if (strstr(script->name, "music") || strstr(script->name, "score") || strstr(script->name, "foley"))
				platform_log("h2 music: script %.32s", script->name);
		}
	}
	for (i = 0; i < n && count < MAXIMUM_LOOPS; i++)
	{
		long which, tag_index = NONE;

		for (which = 0; which < 2 && tag_index == NONE; which++)
			tag_index = caches->cache[which] ? h2_cache_tag_find(caches->cache[which], 'lsnd', names[i]) : NONE;
		memset(&plans[count], 0, sizeof(plans[count]));
		snprintf(plans[count].name, sizeof(plans[count].name), "%s", names[i]);
		if (tag_index != NONE && loop_plan_read(caches, which - 1, tag_index, &plans[count], music_names, music_count, dump))
			count++;
		else if (dump)
			platform_log("h2 music: the scripts' looping sound %s is not in the maps", names[i]);
	}
	/* the clusters' background sounds (the level's ambience bed: Zanzibar's
	"zanzibar ambience" and "ocean waves", each the looping sound its palette
	entry names, at its tracks' gains - Halo 2 plays the listener's cluster's,
	as this engine does) */
	if (scenario && caches->cache[0] && !setting("HALO_H2_NO_BACKGROUND_SOUNDS", 0))
	{
		struct h2_cache *cache = caches->cache[0];
		long bsp_count = h2_cache_structure_count(cache), b;

		for (b = 0; b < bsp_count; b++)
		{
			long sbsp = h2_cache_structure_tag(cache, b), palette_count, k;
			struct h2_cache_tag t;
			unsigned long palette;

			if (sbsp == NONE || !h2_cache_tag_get(cache, sbsp, &t) || !t.address ||
				!h2_cache_block(cache, t.address + 0xD4, 0x64, &palette_count, &palette))
			{
				if (dump)
					platform_log("h2 music: structure BSP %ld: no background sound palette read", b);
				continue;
			}
			for (k = 0; k < palette_count && count < MAXIMUM_LOOPS; k++)
			{
				unsigned long e = palette + 0x64UL * (unsigned long)k;
				char const *entry_name = (char const *)h2_cache_at(cache, e, 32);
				long lsnd = h2_cache_reference(cache, e + 0x20), j;
				char const *lsnd_name = lsnd != NONE ? h2_cache_tag_name(cache, lsnd) : "";
				char palette_name[32];

				if (!entry_name || lsnd == NONE)
					continue;
				memcpy(palette_name, entry_name, 32);
				palette_name[31] = 0;
				/* (one looping sound a name: the BSPs share their entries) */
				for (j = 0; j < count && !(plans[j].palette && !strcmp(plans[j].palette_name, palette_name)); j++)
					;
				if (j < count)
					continue;
				memset(&plans[count], 0, sizeof(plans[count]));
				/* (the entry's scale: its interior scale, where its scale flags
				(0x48) say so (bit 0), as "zanzibar ambience inside" plays
				the outside's sound at 0.4; one looping sound tag a scale) */
				{
					unsigned long flags = h2_cache_read32(cache, e + 0x48);
					union { unsigned long bits; float value; } interior;

					interior.bits = h2_cache_read32(cache, e + 0x4C);
					plans[count].scale = (flags & 1) && interior.value >= 0.f && interior.value < 1.f ? interior.value : 1.f;
				}
				if (plans[count].scale != 1.f)
					snprintf(plans[count].name, sizeof(plans[count].name), "%s@%.2f", lsnd_name, plans[count].scale);
				else
					snprintf(plans[count].name, sizeof(plans[count].name), "%s", lsnd_name);
				memcpy(plans[count].palette_name, palette_name, 32);
				plans[count].palette = TRUE;
				if (loop_plan_read(caches, 0, lsnd, &plans[count], music_names, music_count, dump))
				{
					if (dump)
					{
						platform_log("h2 music: structure BSP %ld background sound \"%s\": %s at scale %.2f", b, palette_name, lsnd_name,
							plans[count].scale);
					}
					count++;
				}
			}
		}
	}
	return count;
}

/* Halo 2's sound class as this engine's (the same numbers but for the
classes Halo 1 did not have: a cutscene's music is music, the rest the
scripts' effects) */
static short sound_class_from_halo2(int h2_class)
{
	if (h2_class == 38)
		return _sound_class_music;
	if (h2_class <= 35 || h2_class == 39 || (h2_class >= 44 && h2_class <= 47))
		return (short)h2_class;
	return _sound_class_scripted_other;
}

/* the vessel (a music sound tag) carrying a Halo 2 sound, NONE if none */
static long music_vessel(char const *name)
{
	long v;

	for (v = 0; v < h2_sound_globals.vessel_count; v++)
	{
		if (!strcmp(h2_sound_globals.vessels[v].name, name))
			return h2_sound_globals.vessels[v].tag_index;
	}
	return NONE;
}

/* a cluster background sound's palette entry name given its looping sound:
the structure BSP loaded is given it as it loads (palette_update) */
static long loop_palette_give(char const *palette_name, long tag_index)
{
	struct palette_loop *p;

	if (h2_sound_globals.palette_loop_count >= MAXIMUM_LOOPS)
		return 0;
	p = &h2_sound_globals.palette_loops[h2_sound_globals.palette_loop_count++];
	memcpy(p->name, palette_name, sizeof(p->name));
	p->name[sizeof(p->name) - 1] = 0;
	p->tag_index = tag_index;
	return 1;
}

/* (every frame) the structure BSP loaded: its background sound palette's
entries given the looping sounds of their names, which this engine plays as
the listener's cluster names them (game_sound.c, scenario.c) - a BSP's tag
data is read again as it is switched to, so it is looked at each time */
static void palette_update(void)
{
	struct structure_bsp *bsp;
	long e;

	if ((!h2_sound_globals.palette_loop_count && !h2_sound_globals.palette_environment_count) ||
		global_structure_bsp_tag_index_get() == NONE || !(bsp = global_structure_bsp_get()))
	{
		return;
	}
	for (e = 0; e < bsp->sound_environment_palette.count; e++)
	{
		struct structure_sound_environment_palette_entry *entry =
			TAG_BLOCK_GET_ELEMENT(&bsp->sound_environment_palette, e, struct structure_sound_environment_palette_entry);
		long k;

		if (entry->sound_environment.index != NONE)
			continue;
		for (k = 0; k < h2_sound_globals.palette_environment_count; k++)
		{
			if (!strncmp((char const *)entry->unused0, h2_sound_globals.palette_environments[k].name, 31))
			{
				entry->sound_environment.group_tag = SOUND_ENVIRONMENT_TAG;
				entry->sound_environment.name = (char *)tag_get_name(h2_sound_globals.palette_environments[k].tag_index);
				entry->sound_environment.name_length = (long)strlen(entry->sound_environment.name);
				entry->sound_environment.index = h2_sound_globals.palette_environments[k].tag_index;
				if (setting("HALO_H2_MUSIC_DUMP", 0))
					platform_log("h2 sound: sound environment \"%.31s\" of %s is %s", h2_sound_globals.palette_environments[k].name,
						tag_get_name(global_structure_bsp_tag_index_get()), tag_get_name(entry->sound_environment.index));
				break;
			}
		}
	}
	for (e = 0; e < bsp->background_sound_palette.count; e++)
	{
		struct structure_background_sound_palette_entry *entry =
			TAG_BLOCK_GET_ELEMENT(&bsp->background_sound_palette, e, struct structure_background_sound_palette_entry);
		long k;

		if (entry->background_sound.index != NONE)
			continue;
		for (k = 0; k < h2_sound_globals.palette_loop_count; k++)
		{
			if (!strncmp((char const *)entry->unused0, h2_sound_globals.palette_loops[k].name, 31))
			{
				entry->background_sound.group_tag = LOOPING_SOUND_DEFINITION_TAG;
				entry->background_sound.name = (char *)tag_get_name(h2_sound_globals.palette_loops[k].tag_index);
				entry->background_sound.name_length = (long)strlen(entry->background_sound.name);
				entry->background_sound.index = h2_sound_globals.palette_loops[k].tag_index;
				if (setting("HALO_H2_MUSIC_DUMP", 0))
					platform_log("h2 sound: background sound \"%.31s\" of %s is %s", h2_sound_globals.palette_loops[k].name,
						tag_get_name(global_structure_bsp_tag_index_get()), tag_get_name(entry->background_sound.index));
				break;
			}
		}
	}
}

static void loop_reference(struct tag_reference *reference, long tag_index)
{
	reference->group_tag = tag_index != NONE ? SOUND_DEFINITION_TAG : 0;
	reference->name = tag_index != NONE ? (char *)tag_get_name(tag_index) : NULL;
	reference->name_length = reference->name ? (long)strlen(reference->name) : 0;
	reference->index = tag_index;
}

/* the structure BSPs' sound environment palettes (0xDC of a Halo 2 sbsp,
0x48 an entry: its name, its reverb's reference at 0x20): each entry's name
given one of the level's sound environment tags (H2_SOUND_ENVIRONMENT_NAME),
which is given Halo 2's reverb - the same I3DL2 fields as this engine's
(priority, room intensity and its HF, rolloff, decay time and HF ratio,
reflections and reverb intensities and delays, diffusion, density, HF
reference). This engine plays a cluster's background sound only where its
sound environment is one (scenario_get_sound_environment), and sets the
reverb for the platform (the PC's SDL mixer renders none). Returns the
entries given */
static long environments_make(struct h2_caches const *caches, boolean dump)
{
	struct h2_cache *cache = caches->cache[0];
	long bsp_count, b, given = 0, made = 0;
	long sources[H2_SOUND_ENVIRONMENT_TAGS];
	long tags[H2_SOUND_ENVIRONMENT_TAGS];

	if (!cache)
		return 0;
	bsp_count = h2_cache_structure_count(cache);
	for (b = 0; b < bsp_count; b++)
	{
		long sbsp = h2_cache_structure_tag(cache, b), palette_count, k;
		struct h2_cache_tag t;
		unsigned long palette;

		if (sbsp == NONE || !h2_cache_tag_get(cache, sbsp, &t) || !t.address ||
			!h2_cache_block(cache, t.address + 0xDC, 0x48, &palette_count, &palette))
		{
			continue;
		}
		for (k = 0; k < palette_count && h2_sound_globals.palette_environment_count < MAXIMUM_LOOPS; k++)
		{
			unsigned long e = palette + 0x48UL * (unsigned long)k;
			char const *entry_name = (char const *)h2_cache_at(cache, e, 32);
			long snde = h2_cache_reference(cache, e + 0x20), j, m;
			struct h2_cache_tag source;
			char palette_name[32];

			if (!entry_name || snde == NONE || !h2_cache_tag_get(cache, snde, &source) || source.group_tag != 'snde' || !source.address)
			{
				if (dump)
				{
					platform_log("h2 sound: sound environment palette entry \"%.31s\": its reverb (tag %ld, group %08lx, address %08lx) not read",
						entry_name ? entry_name : "", snde, snde != NONE && h2_cache_tag_get(cache, snde, &source) ? source.group_tag : 0UL,
						snde != NONE && h2_cache_tag_get(cache, snde, &source) ? source.address : 0UL);
				}
				continue;
			}
			memcpy(palette_name, entry_name, 32);
			palette_name[31] = 0;
			for (j = 0; j < h2_sound_globals.palette_environment_count && strcmp(h2_sound_globals.palette_environments[j].name, palette_name); j++)
				;
			if (j < h2_sound_globals.palette_environment_count)
				continue;
			/* (one tag a reverb) */
			for (m = 0; m < made && sources[m] != snde; m++)
				;
			if (m >= made)
			{
				char name[64];
				long tag_index;
				struct sound_environment_definition *definition;
				unsigned char const *p;
				long at;

				if (made >= H2_SOUND_ENVIRONMENT_TAGS)
					continue;
				snprintf(name, sizeof(name), H2_SOUND_ENVIRONMENT_NAME, made);
				tag_index = tag_loaded(SOUND_ENVIRONMENT_TAG, name);
				if (tag_index == NONE || !(definition = (struct sound_environment_definition *)tag_get(SOUND_ENVIRONMENT_TAG, tag_index)))
					break;
				/* (Halo 2's fields from its priority on - its first four
				bytes this engine's unknown, or none: the layout whose decay
				time and room intensity are in their ranges) */
				p = (unsigned char const *)h2_cache_at(cache, source.address, 0x40);
				for (at = 4; p && at >= 0; at -= 4)
				{
					union { unsigned long bits; float value; } room, decay;

					room.bits = (unsigned long)p[at + 4] | (unsigned long)p[at + 5] << 8 | (unsigned long)p[at + 6] << 16 | (unsigned long)p[at + 7] << 24;
					decay.bits = (unsigned long)p[at + 16] | (unsigned long)p[at + 17] << 8 | (unsigned long)p[at + 18] << 16 |
						(unsigned long)p[at + 19] << 24;
					if (room.value <= 0.f && room.value >= -100.f && decay.value >= 0.1f && decay.value <= 20.f)
					{
						definition->priority = (short)(p[at] | p[at + 1] << 8);
						memcpy(&definition->room_intensity, p + at + 4, 12 * sizeof(real));
						/* (Halo 2's intensities are decibels; this engine's are
						gains, none above 1 - its DirectSound layer asserts it) */
						definition->room_intensity = db_to_gain(definition->room_intensity > 0.f ? 0.f : definition->room_intensity);
						definition->room_intensity_hf = db_to_gain(definition->room_intensity_hf > 0.f ? 0.f : definition->room_intensity_hf);
						definition->reflections_intensity =
							db_to_gain(definition->reflections_intensity > 0.f ? 0.f : definition->reflections_intensity);
						definition->reverb_intensity = db_to_gain(definition->reverb_intensity > 0.f ? 0.f : definition->reverb_intensity);
						break;
					}
				}
				if (dump)
				{
					platform_log("h2 sound: sound environment %s is %s: priority %d, room %.3f/%.3f, rolloff %.2f, decay %.2f s (HF %.2f), "
						"reflections %.4f at %.3f s, reverb %.3f at %.3f s, diffusion %.2f, density %.2f, HF %.0f Hz%s (gains of Halo 2's decibels)", name,
						h2_cache_tag_name(cache, snde), (int)definition->priority, definition->room_intensity, definition->room_intensity_hf,
						definition->room_rolloff, definition->decay_time, definition->decay_hf_ratio, definition->reflections_intensity,
						definition->reflections_delay, definition->reverb_intensity, definition->reverb_delay, definition->diffusion,
						definition->density, definition->hf_reference, at < 0 ? " (no layout fit: left empty)" : "");
				}
				sources[made] = snde;
				tags[made] = tag_index;
				made++;
			}
			memcpy(h2_sound_globals.palette_environments[h2_sound_globals.palette_environment_count].name, palette_name, 32);
			h2_sound_globals.palette_environments[h2_sound_globals.palette_environment_count].tag_index = tags[m];
			h2_sound_globals.palette_environment_count++;
			given++;
		}
	}
	if (given)
		platform_log("h2 sound: %ld sound environment palette entries given %ld reverbs of Halo 2's", given, made);
	return given;
}

/* the plans made the level's looping sound tags (H2_SOUND_LOOP_NAME): Halo
2's flags (deafening, not a loop, stops the music: this engine's three), each
track's in, loop, out, alternate loop and out, gain, fades and their flags
(Halo 2's fade in at start, fade out at stop and crossfade to the alternate
loop, this engine's three); its alternate in is not played (this engine's tracks have none). A
cluster's background sound is given to the palette entries of that name in
every structure BSP of the level, which this engine then plays as the
listener's cluster names it (game_sound.c) */
static void loops_make(
	struct loop_plan const *plans,
	long plan_count,
	char (*music_names)[160],
	struct looping_sound_track *tracks,
	boolean dump)
{
	long p, palettes = 0;

	for (p = 0; p < plan_count && h2_sound_globals.loop_count < MAXIMUM_LOOPS; p++)
	{
		struct loop_plan const *plan = &plans[p];
		char name[64];
		long tag_index, k, carried = 0;
		struct looping_sound_definition *definition;
		struct looping_sound_track *own = tracks + h2_sound_globals.loop_count * LOOP_TRACKS;
		real distance = 0.f;

		/* (the same looping sound again - palette entries of several names
		sharing one: the tag made for it) */
		for (k = 0; k < h2_sound_globals.loop_count && strcmp(h2_sound_globals.loops[k].name, plan->name); k++)
			;
		if (k < h2_sound_globals.loop_count)
		{
			if (plan->palette)
				palettes += loop_palette_give(plan->palette_name, h2_sound_globals.loops[k].tag_index);
			continue;
		}
		snprintf(name, sizeof(name), H2_SOUND_LOOP_NAME, h2_sound_globals.loop_count);
		tag_index = tag_loaded(LOOPING_SOUND_DEFINITION_TAG, name);
		if (tag_index == NONE || !(definition = (struct looping_sound_definition *)tag_get(LOOPING_SOUND_DEFINITION_TAG, tag_index)))
			break;
		memset(own, 0, LOOP_TRACKS * sizeof(*own));
		for (k = 0; k < plan->track_count; k++)
		{
			struct looping_sound_track *track = &own[k];
			long sounds[NUMBER_OF_LOOP_SOUNDS], q;

			for (q = 0; q < NUMBER_OF_LOOP_SOUNDS; q++)
			{
				sounds[q] = plan->tracks[k].sounds[q] != NONE ? music_vessel(music_names[plan->tracks[k].sounds[q]]) : NONE;
				carried += sounds[q] != NONE;
				if (sounds[q] != NONE && sound_definition_get(sounds[q])->maximum_distance > distance)
					distance = sound_definition_get(sounds[q])->maximum_distance;
			}
			/* (Halo 2's bits as this engine's: 0 fade in at start, 1 fade out at stop, 2 crossfade to the alternate loop) */
			track->flags = plan->tracks[k].flags & 7;
			track->gain = db_to_gain(plan->tracks[k].gain_db);
			/* (a palette entry's scale, which this engine's background sound
			has not (it starts at 1): the loop sound's gain at it over its
			gain at 1, in the track's gain - Halo 2's function_1251e0 picks
			between the sound's gain bounds by the scale) */
			if (plan->palette && plan->scale != 1.f && sounds[_loop_loop] != NONE)
			{
				struct sound_definition const *loop_sound = sound_definition_get(sounds[_loop_loop]);
				real at_scale = (loop_sound->one_gain_modifier - loop_sound->zero_gain_modifier) * plan->scale + loop_sound->zero_gain_modifier;

				if (loop_sound->one_gain_modifier > 0.f)
					track->gain *= at_scale / loop_sound->one_gain_modifier;
			}
			if (track->gain > 1.f)
				track->gain = 1.f;
			track->fade_in_duration = plan->tracks[k].fade_in;
			track->fade_out_duration = plan->tracks[k].fade_out;
			loop_reference(&track->start_sound, sounds[_loop_in]);
			loop_reference(&track->loop_sound, sounds[_loop_loop]);
			loop_reference(&track->stop_sound, sounds[_loop_out]);
			loop_reference(&track->alternate_loop_sound, sounds[_loop_alternate_loop]);
			loop_reference(&track->alternate_stop_sound, sounds[_loop_alternate_out]);
		}
		if (!carried)
			continue;
		definition->flags = plan->flags & 7;
		definition->runtime_scripting_sound_index = NONE;
		definition->runtime_maximum_distance = distance > 0.f ? distance : 40.f;
		definition->tracks.count = plan->track_count;
		definition->tracks.address = own;
		definition->details.count = 0;
		definition->details.address = NULL;
		snprintf(h2_sound_globals.loops[h2_sound_globals.loop_count].name, sizeof(h2_sound_globals.loops[0].name), "%s", plan->name);
		h2_sound_globals.loops[h2_sound_globals.loop_count].tag_index = tag_index;
		h2_sound_globals.loop_count++;
		if (dump)
			platform_log("h2 sound: looping sound %s is %s (%ld tracks, %ld sounds)", plan->name, name, plan->track_count, carried);
		if (plan->palette)
			palettes += loop_palette_give(plan->palette_name, tag_index);
	}
	platform_log("h2 sound: %ld looping sounds (the scripts' and the clusters' background sounds, %ld palette entries)",
		h2_sound_globals.loop_count, palettes);
}

/* ---------- public code */

/* (HALO_H2_SOUND_TABLES_DUMP) the loaded tables to a file, appended: the
arena (its words pointing into it written as offsets, marked), the vessels,
the emitters (their start times and objects left out), the loops and the
palettes, each struct's words pointing into the arena made offsets too */
static void sound_words_write(FILE *f, void const *data, unsigned long bytes)
{
	unsigned char const *p = (unsigned char const *)data;
	unsigned long base = (unsigned long)h2_sound_globals.arena, end = base + h2_sound_globals.arena_bytes, i;

	for (i = 0; i + 4 <= bytes; i += 4)
	{
		uint32_t word;

		memcpy(&word, p + i, 4);
		if (base && word >= base && word < end)
			word = 0xA0000000UL | (uint32_t)(word - base);
		fwrite(&word, 4, 1, f);
	}
	if (i < bytes)
		fwrite(p + i, 1, bytes - i, f);
}

static void sound_tables_dump(char const *path, char const *map_name)
{
	FILE *f = fopen(path, "ab");
	long i;

	if (!f)
		return;
	fprintf(f, "\n== %s: arena %lu bytes, %ld sources, %ld vessels, %ld emitters, %ld loops\n", map_name, h2_sound_globals.arena_bytes,
		h2_sound_globals.source_count, h2_sound_globals.vessel_count, h2_sound_globals.emitter_count, h2_sound_globals.loop_count);
	sound_words_write(f, h2_sound_globals.arena, h2_sound_globals.arena_bytes);
	sound_words_write(f, h2_sound_globals.vessels, sizeof(h2_sound_globals.vessels[0]) * (unsigned long)h2_sound_globals.vessel_count);
	for (i = 0; i < h2_sound_globals.emitter_count; i++)
	{
		struct emitter e = h2_sound_globals.emitters[i];

		e.next_time = 0.0;
		e.object_index = e.sound_index = NONE;
		sound_words_write(f, &e, sizeof(e));
	}
	sound_words_write(f, h2_sound_globals.loops, sizeof(h2_sound_globals.loops[0]) * (unsigned long)h2_sound_globals.loop_count);
	sound_words_write(f, h2_sound_globals.palette_loops, sizeof(h2_sound_globals.palette_loops[0]) * (unsigned long)h2_sound_globals.palette_loop_count);
	sound_words_write(f, h2_sound_globals.palette_environments,
		sizeof(h2_sound_globals.palette_environments[0]) * (unsigned long)h2_sound_globals.palette_environment_count);
	/* the level's sound tags as they are now: each's root, pitch ranges and
	permutations (their addresses the tag data's, alike two loads) */
	{
		struct tag_iterator iterator;
		long tag_index;

		tag_iterator_new(&iterator, SOUND_DEFINITION_TAG);
		while ((tag_index = tag_iterator_next(&iterator)) != NONE)
		{
			struct sound_definition d = *sound_definition_get(tag_index);
			long r, k;

			fprintf(f, "\n%s\n", tag_get_name(tag_index));
			sound_words_write(f, &d, sizeof(d));
			for (r = 0; r < sound_definition_get(tag_index)->pitch_ranges.count; r++)
			{
				struct sound_pitch_range pr = *TAG_BLOCK_GET_ELEMENT(&sound_definition_get(tag_index)->pitch_ranges, r, struct sound_pitch_range);
				struct sound_pitch_range const *live = TAG_BLOCK_GET_ELEMENT(&sound_definition_get(tag_index)->pitch_ranges, r,
					struct sound_pitch_range);

				sound_words_write(f, &pr, sizeof(pr));
				for (k = 0; k < live->permutations.count; k++)
				{
					struct sound_permutation pm = *TAG_BLOCK_GET_ELEMENT(&live->permutations, k, struct sound_permutation);

					sound_words_write(f, &pm, sizeof(pm));
				}
			}
		}
	}
	fclose(f);
	platform_log("h2 sound: %s: the tables written to %s", map_name, path);
}

/* ---------- the lengths of the scripts' sounds and the mission dialogue's lines

Halo 2's scripts time themselves on their sounds' lengths -
(sleep (sound_impulse_language_time line)), (sleep (ai_play_line ...)), a
command script's cs_play_line -, and Halo 2 knows every sound's length from
its tag (function_18a1c0: the duration, in ticks). Here a sound no pool sound
carries has no tag of its own, so its length is kept from the load, while the
gestalt is open: each script sound's and each mission dialogue line's longest
permutation, in ticks (decoded bytes over 2 bytes a sample, the codec's
channels and rate). h2_sound_length_ticks gives it, -1 for a sound not
known */
enum { SOUND_LENGTHS_MAXIMUM = 4096 };
static struct
{
	unsigned long hash;
	short ticks;
} sound_lengths[SOUND_LENGTHS_MAXIMUM];
static long sound_length_count;

static unsigned long sound_name_hash(char const *name)
{
	unsigned long h = 2166136261UL;

	for (; *name; name++)
	{
		unsigned char c = (unsigned char)*name;

		if (c >= 'A' && c <= 'Z')
			c = (unsigned char)(c - 'A' + 'a');
		h = (h ^ c) * 16777619UL;
	}
	return h;
}

static void sound_length_add(struct h2_caches const *caches, struct sound_name const *sound_names, long sound_name_count, char const *h2_name)
{
	char name[NAME_BYTES];
	long which, r, i;
	struct h2_cache_tag tag;
	struct h2_snd snd;
	unsigned long hash;
	double longest = 0.0;
	int channels;
	long rate;

	if (!h2_name || !*h2_name || sound_length_count >= SOUND_LENGTHS_MAXIMUM)
		return;
	hash = sound_name_hash(bare(h2_name));
	for (i = 0; i < sound_length_count; i++)
	{
		if (sound_lengths[i].hash == hash)
			return;
	}
	snprintf(name, sizeof(name), "sound\\%s", bare(h2_name));
	if (!h2_sound_find(sound_names, sound_name_count, name, &which, &tag) || !caches->gestalt[which] ||
		!h2_gestalt_sound(caches->gestalt[which], tag.address, &snd) || snd.range_count < 1)
	{
		return;
	}
	channels = h2_gestalt_codec_channels(snd.codec);
	rate = h2_gestalt_codec_rate(snd.codec);
	if (!channels || !rate)
		return;
	for (r = 0; r < snd.range_count; r++)
	{
		struct h2_snd_range range;
		long k;

		if (!h2_gestalt_range(caches->gestalt[which], snd.first_range + r, &range))
			continue;
		for (k = 0; k < range.permutation_count; k++)
		{
			struct h2_snd_permutation permutation;
			double seconds;

			if (!h2_gestalt_permutation(caches->gestalt[which], range.first_permutation + k, 0, &permutation))
				continue;
			seconds = (double)permutation.decoded_bytes / (2.0 * channels * rate);
			if (seconds > longest)
				longest = seconds;
		}
	}
	if (longest <= 0.0)
		return;
	sound_lengths[sound_length_count].hash = hash;
	sound_lengths[sound_length_count].ticks = (short)MIN(longest * 30.0 + 0.5, 32000.0);
	sound_length_count++;
}

/* the mission dialogue's lines ('mdlg' tags of the level: each line's
variants' sounds) given their lengths */
static void sound_lengths_add_dialogue(struct h2_caches const *caches, struct sound_name const *sound_names, long sound_name_count)
{
	long tag, count;

	if (!caches->cache[0])
		return;
	count = h2_cache_tag_count(caches->cache[0]);
	for (tag = 0; tag < count; tag++)
	{
		struct h2_cache_tag t;
		long line_count, i;
		unsigned long lines;

		if (!h2_cache_tag_get(caches->cache[0], tag, &t) || t.group_tag != 'mdlg' ||
			!h2_cache_block(caches->cache[0], t.address, 0x10, &line_count, &lines) || line_count <= 0)
		{
			continue;
		}
		for (i = 0; i < line_count; i++)
		{
			long variant_count, v;
			unsigned long variants;

			if (!h2_cache_block(caches->cache[0], lines + 0x10 * i + 4, 0x10, &variant_count, &variants))
				continue;
			for (v = 0; v < variant_count; v++)
			{
				long sound = h2_cache_reference(caches->cache[0], variants + 0x10 * v + 4);

				if (sound != NONE)
					sound_length_add(caches, sound_names, sound_name_count, h2_cache_tag_name(caches->cache[0], sound));
			}
		}
	}
}

long h2_sound_length_ticks(
	char const *h2_name)
{
	unsigned long hash;
	long i;

	if (!h2_name || !*h2_name)
		return -1;
	hash = sound_name_hash(bare(h2_name));
	for (i = 0; i < sound_length_count; i++)
	{
		if (sound_lengths[i].hash == hash)
			return sound_lengths[i].ticks;
	}
	return -1;
}

void h2_sounds_map_loaded(
	char const *map_name)
{
	char const *directory = map_name ? h2_maps_directory_of(map_name) : cache_files_map_directory();
	char path[DAT_NAME_BYTES];
	struct h2_caches caches;
	struct pending_sound *pending = NULL;
	struct pending_permutation *pending_permutations = NULL;
	struct source *sources = NULL;
	struct h2_sound_codec_chunk *pool = NULL;
	struct candidate *candidates = NULL;
	struct sound_name *sound_names = NULL;
	struct row *rows = NULL;
	struct emitter_plan *plans = NULL;
	static char extra_rows[2048];
	long sound_name_count = 0;
	unsigned long long opened_at, named_at;
	long pending_count = 0, pending_permutation_count = 0, source_count = 0, pool_count = 0;
	unsigned long long started = vita_host_time_us();
	long row_index, row_count = 0, dat;
	long applied = 0, missing_stand_in = 0, missing_h2 = 0, skipped_permutations = 0, auto_rows = 0;
	unsigned long arena_bytes, offset;
	char why[160];
	boolean dump = setting("HALO_H2_SOUND_DUMP", 0) != 0;
	float const *class_minimum = classes.minimum, *class_maximum = classes.maximum;
	long pool_tags[MAXIMUM_POOL];
	long pool_count_tags = 0, pool_next = 0, script_sounds_carried = 0;
	long plan_count = 0;
	struct loop_plan *loop_plans = NULL;
	char (*music_names)[160] = NULL;
	static char music_rows[MAXIMUM_MUSIC_SOUNDS][168];
	long music_tags[MAXIMUM_MUSIC_SOUNDS];
	long loop_plan_count = 0, music_count = 0, music_tag_count = 0, music_next = 0, music_carried = 0;
	struct looping_sound_track *loop_tracks = NULL;
	boolean sound_heap_begun = FALSE;

	if (h2_sound_globals.loaded || h2_sound_globals.arena)
		h2_sounds_unloaded();
	memset(&caches, 0, sizeof(caches));
	if (!map_name || !directory || !setting("HALO_H2_SOUND", 1) || !config_boolean("audio.enabled") ||
		!h2_level_is_halo2_map(directory, map_name))
	{
		return;
	}
	if (dump)
		dump_stand_ins();
	h2_sound_globals.dat_reads = h2_sound_globals.dat_read_bytes = h2_sound_globals.plausible_checks = 0;
	h2_sound_globals.dat_read_us = 0;
	for (dat = 0; dat < DAT_COUNT; dat++)
	{
		FILE *file;

		snprintf(path, sizeof(path), "%s%s", h2_resource_directory(directory, dat_names[dat]), dat_names[dat]);
		file = fopen(path, "rb");
		{
			void h2_file_unbuffered(FILE *file);

			h2_file_unbuffered(file);
		}
		if (file)
		{
			fseek(file, 0, SEEK_END);
			h2_sound_globals.dat_sizes[dat] = (unsigned long)ftell(file);
			h2_sound_globals.dats[dat] = file;
			h2_sound_globals.dat_bytes += h2_sound_globals.dat_sizes[dat];
		}
	}
	if (!h2_sound_globals.dats[0])
	{
		platform_log("h2 sound: %s%s is not beside the map; the level keeps its stand-in sounds", directory, dat_names[0]);
		goto done;
	}
	snprintf(h2_sound_globals.map_name, sizeof(h2_sound_globals.map_name), "%s", map_name);

	caches.cache[0] = h2_cache_open(directory, map_name, why, sizeof(why));
	if (!caches.cache[0])
	{
		platform_log("h2 sound: %s.map cannot be read: %s", map_name, why);
		goto done;
	}
	/* (the map's pieces in the memory window now, before shared.map opens:
	the load's kept open still held what the AI's step inflated - some 9 MB
	of 04a's, in the C heap when the window had no room - and the switch
	below let them go only after shared.map's open, which then found the C
	heap at its peak: the Vita's 04a after 03b, "out of memory", its sounds
	not found. The same pieces let go, earlier) */
	if (setting("HALO_H2_SOUND_WINDOW", H2_SOUND_SEGMENT_KB_DEFAULT != 0))
		h2_cache_segment_memory(caches.cache[0], h2_level_window_alloc, h2_level_window_free);
	caches.cache[1] = strcmp(map_name, "shared") ? h2_cache_open(directory, "shared", why, sizeof(why)) : NULL;
	if (!caches.cache[1] && strcmp(map_name, "shared"))
	{
		void platform_heap_usage(unsigned long *in_use, unsigned long *capacity);
		void platform_heap_census_log(const char *when);
		unsigned long in_use = 0, capacity = 0;

		platform_heap_usage(&in_use, &capacity);
		platform_log("h2 sound: shared.map cannot be read (its sounds, the weapons' and vehicles', are not found): %s "
			"(C heap %lu KB in use of %lu KB)", why, in_use >> 10, capacity >> 10);
		platform_heap_census_log("shared.map not opened");
	}
	/* (the Vita: the maps' inflated pieces kept to a budget, in the memory
	window - kept whole, shared.map's sound tags and gestalt came to more than
	the 48 MB C heap had free with a level loaded (some 13 MB): past it each
	read failed and its sounds were "not found", 83 of them, the battle
	rifle's among them; HALO_H2_SOUND_SEGMENT_KB, HALO_H2_SOUND_WINDOW=0) */
	{
		long budget_kb = setting("HALO_H2_SOUND_SEGMENT_KB", H2_SOUND_SEGMENT_KB_DEFAULT);

		/* (the chunks the reads inflate kept whole for this load, in user RAM
		outside the C heap - the build's heap, opened for it -, and copied
		rather than inflated again when the budget's pieces were let go:
		Zanzibar's rows read 61 chunks, inflated 170 times in the 2 MB, each a
		long inflate on the Vita; h2_cache.c's make's keep) */
		if (budget_kb > 0 && setting("HALO_H2_SOUND_CHUNKS_KEPT", 1))
		{
			/* (the kept open's names' index made now, in the C heap: it
			outlives this load) */
			h2_cache_tag_find(caches.cache[0], 'snd!', "");
			/* (shared.map may not have opened - out of memory: the Vita's
			04a after 03b's whole finale - its sounds then "not found", no
			index made of no cache: h2_cache_tag_find read through NULL) */
			if (caches.cache[1])
				h2_cache_tag_find(caches.cache[1], 'snd!', "");
			if (!h2_build_heap_active())
			{
				h2_build_heap_begin();
				sound_heap_begun = TRUE;
				h2_cache_chunks_begin();
			}
		}
		/* (their pieces in the memory window when it has room, not the C heap) */
		if (setting("HALO_H2_SOUND_WINDOW", H2_SOUND_SEGMENT_KB_DEFAULT != 0))
		{
			h2_cache_segment_memory(caches.cache[0], h2_level_window_alloc, h2_level_window_free);
			h2_cache_segment_memory(caches.cache[1], h2_level_window_alloc, h2_level_window_free);
		}
		if (budget_kb > 0)
		{
			h2_cache_segment_budget(caches.cache[0], (unsigned long)budget_kb * 1024UL);
			h2_cache_segment_budget(caches.cache[1], (unsigned long)budget_kb * 1024UL);
		}
	}
	caches.gestalt[0] = h2_gestalt_open(caches.cache[0]);
	if (caches.cache[1])
		caches.gestalt[1] = h2_gestalt_open(caches.cache[1]);
	if (!caches.gestalt[0] && !caches.gestalt[1])
	{
		platform_log("h2 sound: no sound gestalt in %s.map or shared.map", map_name);
		goto done;
	}
	/* the identity the sound cache file is of: the map's size and its header's CRC-32 */
	{
		char map_path[DAT_NAME_BYTES];
		FILE *map;

		snprintf(map_path, sizeof(map_path), "%s%s.map", directory, map_name);
		map = fopen(map_path, "rb");
		{
			void h2_file_unbuffered(FILE *file);

			h2_file_unbuffered(map);
		}
		if (map)
		{
			unsigned char header[0x800];
			size_t got = fread(header, 1, sizeof(header), map);

			fseek(map, 0, SEEK_END);
			h2_sound_globals.map_bytes = (unsigned long)ftell(map);
			h2_sound_globals.map_crc = cache_file_crc32_update(0, header, (uint32_t)got);
			fclose(map);
		}
	}

	opened_at = vita_host_time_us();
	sound_names = sound_names_make(&caches, &sound_name_count);
	h2_load_step("sounds: the maps opened");
	sound_classes_read(&caches, &classes);
	named_at = vita_host_time_us();
	pending = (struct pending_sound *)calloc(MAXIMUM_ROWS, sizeof(*pending));
	pending_permutations = (struct pending_permutation *)calloc(MAXIMUM_ROWS * H2_SOUND_RANGE_PERMUTATIONS, sizeof(*pending_permutations));
	sources = (struct source *)calloc(SOURCE_MAXIMUM, sizeof(*sources));
	pool = (struct h2_sound_codec_chunk *)calloc(CHUNK_POOL_MAXIMUM, sizeof(*pool));
	candidates = (struct candidate *)calloc(H2_SOUND_MAXIMUM_PERMUTATIONS * MAXIMUM_NAMES, sizeof(*candidates));
	rows = (struct row *)calloc(MAXIMUM_ROWS + 1, sizeof(*rows));
	plans = (struct emitter_plan *)calloc(MAXIMUM_EMITTERS, sizeof(*plans));
	if (!sound_names || !pending || !pending_permutations || !sources || !pool || !candidates || !rows || !plans)
		goto done;

	h2_load_step("sounds: the classes");
	/* the rows: the table's; the level's own sound\\h2\\ tags; the scenery's and
	machines' sounds as vessels */
	for (row_index = 0; static_rows[row_index].ce || static_rows[row_index].h2; row_index++)
	{
		if (row_count < MAXIMUM_ROWS - 1)
			rows[row_count++] = static_rows[row_index];
	}
	{
		struct tag_iterator iterator;
		long tag_index;

		tag_iterator_new(&iterator, SOUND_DEFINITION_TAG);
		while ((tag_index = tag_iterator_next(&iterator)) != NONE)
		{
			char const *name = tag_get_name(tag_index);

			if (!strncmp(name, "sound\\h2\\", 9) && row_count < MAXIMUM_ROWS - 1)
			{
				rows[row_count].kind = ROW_PLAIN;
				rows[row_count].ce = name;
				rows[row_count].h2 = name + 9;
				rows[row_count].trim_db = 0.f;
				row_count++;
				auto_rows++;
			}
		}
	}
	/* (HALO_H2_SOUND_ROWS="<stand-in tag>=<Halo 2 sound>;...": rows of the player's own, for trying one) */
	if (HALO_ENV_ONCE("HALO_H2_SOUND_ROWS"))
	{
		char *cursor;

		snprintf(extra_rows, sizeof(extra_rows), "%s", HALO_ENV_ONCE("HALO_H2_SOUND_ROWS"));
		for (cursor = extra_rows; cursor && *cursor && row_count < MAXIMUM_ROWS - 1;)
		{
			char *end = strchr(cursor, ';');
			char *equals = strchr(cursor, '=');

			if (end)
				*end = 0;
			if (equals && equals < cursor + strlen(cursor))
			{
				*equals = 0;
				rows[row_count].kind = ROW_PLAIN;
				rows[row_count].ce = cursor;
				rows[row_count].h2 = equals + 1;
				rows[row_count].trim_db = 0.f;
				row_count++;
			}
			cursor = end ? end + 1 : NULL;
		}
	}
	plan_count = emitter_plans_read(caches.cache[0], plans, MAXIMUM_EMITTERS, dump);
	h2_load_step("sounds: the rows");
	/* the looping sounds (the scripts', the clusters') and the sounds their
	tracks play, each a row of its own (ROW_MUSIC: the music sound tags) */
	loop_plans = (struct loop_plan *)calloc(MAXIMUM_LOOPS, sizeof(*loop_plans));
	music_names = (char (*)[160])calloc(MAXIMUM_MUSIC_SOUNDS, 160);
	if (loop_plans && music_names)
	{
		long q;

		loop_plan_count = loop_plans_read(&caches, loop_plans, music_names, &music_count,
			setting("HALO_H2_MUSIC_DUMP", 0) != 0 || dump);
		for (q = 0; q < music_count && row_count < MAXIMUM_ROWS - 1; q++)
		{
			snprintf(music_rows[q], sizeof(music_rows[q]), "S:%s", music_names[q]);
			memset(&rows[row_count], 0, sizeof(rows[row_count]));
			rows[row_count].kind = ROW_MUSIC;
			rows[row_count].h2 = music_rows[q];
			row_count++;
		}
		for (q = 0; q < MAXIMUM_MUSIC_SOUNDS; q++)
		{
			char name[64];
			long tag_index;

			snprintf(name, sizeof(name), H2_SOUND_MUSIC_NAME, q);
			tag_index = tag_loaded(SOUND_DEFINITION_TAG, name);
			if (tag_index != NONE && tag_get(SOUND_DEFINITION_TAG, tag_index))
				music_tags[music_tag_count++] = tag_index;
		}
	}
	{
		long p, r;

		for (p = 0; p < plan_count; p++)
		{
			boolean known = FALSE;

			plan_vessel_key(&plans[p], plans[p].key, sizeof(plans[p].key));
			for (r = 0; r < row_count && !known; r++)
			{
				known = rows[r].kind == ROW_VESSEL && rows[r].h2 &&
					!strcmp(rows[r].key ? rows[r].key : bare(rows[r].h2), plans[p].key);
			}
			if (!known && row_count < MAXIMUM_ROWS - 1)
			{
				rows[row_count].kind = ROW_VESSEL;
				rows[row_count].ce = NULL;
				rows[row_count].h2 = plans[p].name;
				rows[row_count].trim_db = 0.f;
				rows[row_count].key = plans[p].key;
				rows[row_count].distance_minimum = plans[p].distance_minimum;
				rows[row_count].distance_maximum = plans[p].distance_maximum;
				row_count++;
			}
		}
	}
	h2_load_step("sounds: the looping sounds");
	/* the level's scripts' sounds (h2_scripts.c), in the order the scripts
	name them, 32 to a pool sound (ROW_SCRIPT_GROUP): put first, they take the
	pool before the scenery and machines, at most MAXIMUM_SCRIPT_GROUPS pool
	sounds (HALO_H2_NO_SCRIPT_SOUNDS=1: none) */
	{
		long n = h2_scripts_sound_names(script_sound_names, MAXIMUM_SCRIPT_GROUPS * H2_SOUND_RANGE_PERMUTATIONS), q, groups;

		script_sound_count = 0;
		sound_length_count = 0;
		for (q = 0; q < n; q++)
			sound_length_add(&caches, sound_names, sound_name_count, script_sound_names[q]);
		sound_lengths_add_dialogue(&caches, sound_names, sound_name_count);
		if (dump)
			platform_log("h2 sound: %ld script sounds and dialogue lines with their lengths", sound_length_count);
		for (q = 0; q < n; q++)
		{
			long r;
			boolean known = FALSE;

			for (r = 0; r < row_count && !known; r++)
				known = (rows[r].kind == ROW_VESSEL || rows[r].kind == ROW_MUSIC) && rows[r].h2 && !strcmp(bare(rows[r].h2), script_sound_names[q]);
			/* (a cutscene's score - Halo 2's music classes: a music sound of
			its own, stereo, as long as it is; the scripts play it by its name) */
			if (!known && music_names && music_count < MAXIMUM_MUSIC_SOUNDS && row_count < MAXIMUM_ROWS - 1 &&
				!setting("HALO_H2_NO_SCRIPT_MUSIC", 0))
			{
				char name[NAME_BYTES];
				long which;
				struct h2_cache_tag tag;
				struct h2_snd snd;

				snprintf(name, sizeof(name), "sound\\%s", script_sound_names[q]);
				if (h2_sound_find(sound_names, sound_name_count, name, &which, &tag) && caches.gestalt[which] &&
					h2_gestalt_sound(caches.gestalt[which], tag.address, &snd) && (snd.sound_class == 32 || snd.sound_class == 38))
				{
					snprintf(music_names[music_count], 160, "%s", script_sound_names[q]);
					snprintf(music_rows[music_count], sizeof(music_rows[0]), "S:%s", script_sound_names[q]);
					memset(&rows[row_count], 0, sizeof(rows[row_count]));
					rows[row_count].kind = ROW_MUSIC;
					rows[row_count].h2 = music_rows[music_count];
					row_count++;
					music_count++;
					known = TRUE;
				}
			}
			if (!known)
				script_sound_names[script_sound_count++] = script_sound_names[q];
		}
		/* (the AI's voices: VOICE_SLOTS permutations in two groups of their
		own, seeded with the maps' first dialogue sounds; h2_dialogue.c binds
		the characters' lines to them as they speak - a campaign level's only;
		HALO_H2_NO_VOICES=1: none) */
		memset(&h2_voice, 0, sizeof(h2_voice));
		if (!setting("HALO_H2_NO_VOICES", 0) && caches.cache[0] && strstr(h2_cache_scenario_path(caches.cache[0]), "\\solo\\"))
		{
			long n;

			while (script_sound_count % H2_SOUND_RANGE_PERMUTATIONS && script_sound_count < (long)NUMBEROF(script_sound_names))
				script_sound_names[script_sound_count++] = NULL;
			for (n = 0; n < sound_name_count && h2_voice.count < VOICE_SLOTS && script_sound_count < (long)NUMBEROF(script_sound_names); n++)
			{
				struct h2_snd snd;
				struct h2_snd_range range;

				if (strncmp(sound_names[n].name, "sound\\dialog\\combat\\", 20) || !caches.gestalt[sound_names[n].which] ||
					!h2_gestalt_sound(caches.gestalt[sound_names[n].which], sound_names[n].tag.address, &snd) || snd.range_count < 1 ||
					!h2_gestalt_range(caches.gestalt[sound_names[n].which], snd.first_range, &range) || range.permutation_count < 1)
				{
					continue;
				}
				snprintf(h2_voice.names[h2_voice.count], 96, "%s#0", sound_names[n].name + 6);
				script_sound_names[script_sound_count++] = h2_voice.names[h2_voice.count++];
			}
			while (script_sound_count % H2_SOUND_RANGE_PERMUTATIONS && script_sound_count < (long)NUMBEROF(script_sound_names))
				script_sound_names[script_sound_count++] = NULL;
			if (h2_voice.count)
				platform_log("h2 sound: %ld voice slots", h2_voice.count);
		}
		groups = (script_sound_count + H2_SOUND_RANGE_PERMUTATIONS - 1) / H2_SOUND_RANGE_PERMUTATIONS;
		if (groups && row_count + groups < MAXIMUM_ROWS - 1)
		{
			memmove(rows + groups, rows, (size_t)row_count * sizeof(*rows));
			for (q = 0; q < groups; q++)
			{
				rows[q].kind = ROW_SCRIPT_GROUP;
				rows[q].ce = NULL;
				rows[q].h2 = "(the scripts' sounds)";
				rows[q].trim_db = 0.f;
				rows[q].group = q;
			}
			row_count += groups;
		}
	}
	h2_load_step("sounds: the scripts' sounds");
	/* (Halo 2's main menu, h2_menu.c - the main menu's level only: its list's
	sounds and its music, put first: the pool's first sounds) */
	{
		extern long h2_menu_sound_names(char const **names, long maximum);
		static char const *menu_sounds[8];
		long n = h2_menu_sound_names(menu_sounds, 8), q;

		if (n > 0 && row_count + n < MAXIMUM_ROWS - 1)
		{
			memmove(rows + n, rows, (size_t)row_count * sizeof(*rows));
			for (q = 0; q < n; q++)
			{
				memset(&rows[q], 0, sizeof(rows[q]));
				rows[q].kind = ROW_VESSEL;
				rows[q].h2 = menu_sounds[q];
			}
			row_count += n;
		}
	}
	h2_load_step("sounds: the main menu's");
	/* (the pool: the level's vessel sounds, h2_level.c's) */
	for (row_index = 0; row_index < H2_SOUND_VESSEL_TAGS && pool_count_tags < MAXIMUM_POOL; row_index++)
	{
		char name[64];
		long tag_index;

		snprintf(name, sizeof(name), H2_SOUND_VESSEL_NAME, row_index);
		tag_index = tag_loaded(SOUND_DEFINITION_TAG, name);
		if (tag_index != NONE && tag_get(SOUND_DEFINITION_TAG, tag_index))
			pool_tags[pool_count_tags++] = tag_index;
	}
	pool_count_tags = pool_unreferenced(pool_tags, pool_count_tags);

	for (row_index = 0; row_index < row_count && pending_count < MAXIMUM_ROWS; row_index++)
	{
		/* (the loading screen drawn between rows: h2_level.h) */
		h2_load_draw_coarse();
		struct row const *row = &rows[row_index];
		long tag_index;
		struct pending_sound *p;
		char names[MAXIMUM_NAMES][NAME_BYTES];
		boolean first_only[MAXIMUM_NAMES];
		boolean want_stereo = FALSE;
		long name_count = 0;
		long n;
		long candidate_count = 0;
		long first_candidate_cache = NONE;
		long permutation_cap;
		long chosen;
		int out_channels;
		long out_rate;

		if (row->kind == ROW_POOL)
			continue;
		if (row->kind == ROW_MUSIC)
		{
			if (music_next >= music_tag_count || h2_sound_globals.vessel_count >= MAXIMUM_VESSELS)
			{
				if (dump)
					platform_log("h2 sound: no free music sound for %s", row->h2);
				continue;
			}
			tag_index = music_tags[music_next++];
		}
		else if (row->kind == ROW_VESSEL || row->kind == ROW_SCRIPT_GROUP)
		{
			if (pool_next >= pool_count_tags || h2_sound_globals.vessel_count >= MAXIMUM_VESSELS)
			{
				if (dump)
					platform_log("h2 sound: no free pool sound for %s", row->h2);
				continue;
			}
			tag_index = pool_tags[pool_next++];
		}
		else
		{
			tag_index = tag_loaded(SOUND_DEFINITION_TAG, row->ce);
		}
		if (tag_index == NONE)
		{
			missing_stand_in++;
			if (dump)
				platform_log("h2 sound: row %s: no such stand-in in the level", row->ce);
			continue;
		}
		/* (32 of the scripts' sounds: the first permutation of each, one of
		the pool sound's permutations, a vessel each) */
		if (row->kind == ROW_SCRIPT_GROUP)
		{
			long first = row->group * H2_SOUND_RANGE_PERMUTATIONS, q;
			long candidate_name[H2_SOUND_RANGE_PERMUTATIONS];

			p = &pending[pending_count];
			memset(p, 0, sizeof(*p));
			p->row = row_index;
			p->tag_index = tag_index;
			p->permutation_first = pending_permutation_count;
			for (q = first; q < script_sound_count && q < first + H2_SOUND_RANGE_PERMUTATIONS; q++)
			{
				long which;
				struct h2_cache_tag tag;
				struct h2_snd snd;
				struct h2_snd_range range;
				char name[NAME_BYTES];

				long permutation_index = 0;
				char const *mark;

				/* (a group's end before the next voice's) */
				if (!script_sound_names[q])
					continue;
				/* (a voice's line's variant: "<name>#<permutation>") */
				snprintf(name, sizeof(name), "sound\\%s", script_sound_names[q]);
				if ((mark = strrchr(name, '#')) != NULL)
				{
					permutation_index = atol(mark + 1);
					name[mark - name] = 0;
				}
				if (!h2_sound_find(sound_names, sound_name_count, name, &which, &tag) || !caches.gestalt[which] ||
					!h2_gestalt_sound(caches.gestalt[which], tag.address, &snd) || snd.range_count < 1 ||
					!h2_gestalt_range(caches.gestalt[which], snd.first_range, &range) || range.permutation_count <= permutation_index ||
					!h2_gestalt_permutation(caches.gestalt[which], range.first_permutation + permutation_index, 0, &candidates[candidate_count].permutation))
				{
					missing_h2++;
					if (dump)
						platform_log("h2 sound: script sound %s is not in the maps", name);
					continue;
				}
				if (first_candidate_cache == NONE)
				{
					first_candidate_cache = which;
					p->snd = snd;
					p->have_playback = h2_gestalt_playback(caches.gestalt[which], snd.playback_index, &p->playback);
					p->have_scale = h2_gestalt_scale(caches.gestalt[which], snd.scale_index, &p->scale);
				}
				/* (the group's distances: the widest of its lines') */
				{
					struct h2_snd_playback playback;
					float minimum = 0.f, maximum = 0.f;

					if (h2_gestalt_playback(caches.gestalt[which], snd.playback_index, &playback) && playback.minimum_distance > 0.f &&
						playback.maximum_distance > playback.minimum_distance)
					{
						minimum = playback.minimum_distance;
						maximum = playback.maximum_distance;
					}
					else if (snd.sound_class < SOUND_CLASS_COUNT && class_minimum[snd.sound_class] > 0.f &&
						class_maximum[snd.sound_class] > class_minimum[snd.sound_class] && class_maximum[snd.sound_class] < 1000.f)
					{
						minimum = class_minimum[snd.sound_class];
						maximum = class_maximum[snd.sound_class];
					}
					if (maximum > p->group_maximum)
						p->group_maximum = maximum;
					if (minimum > p->group_minimum)
						p->group_minimum = minimum;
				}
				candidates[candidate_count].which = which;
				candidates[candidate_count].snd = snd;
				candidate_name[candidate_count++] = q;
			}
			row_format(candidates, candidate_count, FALSE, &out_channels, &out_rate);
			p->out_channels = out_channels;
			p->out_rate = out_rate;
			for (chosen = 0; chosen < candidate_count && h2_sound_globals.vessel_count < MAXIMUM_VESSELS; chosen++)
			{
				long source = source_add(sources, &source_count, pool, &pool_count, caches.gestalt[candidates[chosen].which],
					&candidates[chosen].snd, &candidates[chosen].permutation, out_channels, out_rate, SHORT_PERMUTATION_BYTES);
				char const *bare_name = script_sound_names[candidate_name[chosen]];
				size_t length = strlen(bare_name);
				struct vessel *v;

				if (source == NONE)
				{
					skipped_permutations++;
					platform_log("h2 sound: script sound %s: permutation skipped: %s (codec %d, %d chunks, %lu bytes decoded)",
						bare_name, source_why, (int)candidates[chosen].snd.codec, (int)candidates[chosen].permutation.chunk_count,
						(unsigned long)candidates[chosen].permutation.decoded_bytes);
					continue;
				}
				pending_permutations[pending_permutation_count].source = (uint32_t)source;
				snprintf(pending_permutations[pending_permutation_count].name, sizeof(pending_permutations[0].name), "%s",
					length > 28 ? bare_name + length - 28 : bare_name);
				pending_permutation_count++;
				v = &h2_sound_globals.vessels[h2_sound_globals.vessel_count++];
				snprintf(v->name, sizeof(v->name), "%s", bare_name);
				v->tag_index = tag_index;
				v->permutation = p->permutation_count++;
			}
			if (!p->permutation_count)
			{
				pool_next--;
				continue;
			}
			script_sounds_carried += p->permutation_count;
			pending_count++;
			continue;
		}
		/* the names, '|' apart, each in the maps' "sound" folder ("S:" before the
		first: kept stereo; "1:" before one: the first pitch range's only) */
		{
			char const *text = row->h2;

			while (*text && name_count < MAXIMUM_NAMES)
			{
				long length = 0;
				boolean first = FALSE;

				if (text[0] == 'S' && text[1] == ':')
				{
					want_stereo = TRUE;
					text += 2;
				}
				if (text[0] == '1' && text[1] == ':')
				{
					first = TRUE;
					text += 2;
				}
				while (text[length] && text[length] != '|')
					length++;
				if (length >= NAME_BYTES - 8)
					length = NAME_BYTES - 8;
				snprintf(names[name_count], NAME_BYTES, "sound\\%.*s", (int)length, text);
				first_only[name_count] = first;
				name_count++;
				text += length;
				if (*text == '|')
					text++;
			}
		}
		p = &pending[pending_count];
		memset(p, 0, sizeof(*p));
		p->row = row_index;
		p->tag_index = tag_index;
		p->permutation_first = pending_permutation_count;
		for (n = 0; n < name_count; n++)
		{
			long which;
			struct h2_cache_tag tag;
			struct h2_snd snd;
			long r;

			if (!h2_sound_find(sound_names, sound_name_count, names[n], &which, &tag) || !caches.gestalt[which] ||
				!h2_gestalt_sound(caches.gestalt[which], tag.address, &snd))
			{
				if (dump)
					platform_log("h2 sound: row %s: %s is not in the maps", row->ce ? row->ce : "(vessel)", names[n]);
				continue;
			}
			if (first_candidate_cache == NONE)
			{
				first_candidate_cache = which;
				p->snd = snd;
				p->have_playback = h2_gestalt_playback(caches.gestalt[which], snd.playback_index, &p->playback);
				p->have_scale = h2_gestalt_scale(caches.gestalt[which], snd.scale_index, &p->scale);
			}
			for (r = 0; r < snd.range_count && !(first_only[n] && r > 0); r++)
			{
				struct h2_snd_range range;
				long k;

				if (!h2_gestalt_range(caches.gestalt[which], snd.first_range + r, &range))
					continue;
				for (k = 0; k < range.permutation_count && candidate_count < H2_SOUND_MAXIMUM_PERMUTATIONS * MAXIMUM_NAMES; k++)
				{
					if (h2_gestalt_permutation(caches.gestalt[which], range.first_permutation + k, 0,
						&candidates[candidate_count].permutation))
					{
						candidates[candidate_count].which = which;
						candidates[candidate_count].snd = snd;
						candidate_count++;
					}
				}
			}
		}
		if (!candidate_count)
		{
			missing_h2++;
			if (row->kind == ROW_VESSEL)
				pool_next--;
			if (row->kind == ROW_MUSIC)
				music_next--;
			if (dump)
				platform_log("h2 sound: row %s: no permutations in %s (%ld names)", row->ce ? row->ce : "(vessel)", row->h2, name_count);
			continue;
		}
		row_format(candidates, candidate_count, want_stereo, &out_channels, &out_rate);
		p->out_channels = out_channels;
		p->out_rate = out_rate;
		permutation_cap = candidate_count < H2_SOUND_RANGE_PERMUTATIONS ? candidate_count : H2_SOUND_RANGE_PERMUTATIONS;
		for (chosen = 0; chosen < permutation_cap; chosen++)
		{
			long c = cap_index(chosen, candidate_count);
			long links = 1, link;   /* (one: a long permutation is chained by custom_edition_sounds.c) */
			long source = source_add(sources, &source_count, pool, &pool_count, caches.gestalt[candidates[c].which],
				&candidates[c].snd, &candidates[c].permutation, out_channels, out_rate,
				row->kind == ROW_MUSIC || (row->h2 && strstr(row->h2, "main_menu_music")) ? LONG_PERMUTATION_BYTES : SHORT_PERMUTATION_BYTES);

			if (source == NONE)
			{
				skipped_permutations++;
				if (dump)
					platform_log("h2 sound: row %s: permutation %ld of %s skipped: %s (codec %d, %d chunks)",
						row->ce ? row->ce : "(vessel)", c, row->h2, source_why, (int)candidates[c].snd.codec,
						(int)candidates[c].permutation.chunk_count);
				continue;
			}
			for (link = 0; link < links && pending_permutation_count < MAXIMUM_ROWS * H2_SOUND_RANGE_PERMUTATIONS; link++)
			{
				pending_permutations[pending_permutation_count].source = (uint32_t)(source + link);
				pending_permutations[pending_permutation_count].link = (uint16_t)link;
				if (row->kind == ROW_VESSEL || row->kind == ROW_MUSIC)
				{
					char const *bare_name = bare(row->h2);
					size_t length = strlen(bare_name);

					snprintf(pending_permutations[pending_permutation_count].name, sizeof(pending_permutations[0].name), "%s",
						length > 28 ? bare_name + length - 28 : bare_name);
				}
				else
				{
					snprintf(pending_permutations[pending_permutation_count].name, sizeof(pending_permutations[0].name), "h2 %ld", c);
				}
				pending_permutation_count++;
				p->permutation_count++;
			}
		}
		if (!p->permutation_count)
		{
			missing_h2++;
			if (row->kind == ROW_VESSEL)
				pool_next--;
			if (row->kind == ROW_MUSIC)
				music_next--;
			continue;
		}
		if (row->kind == ROW_MUSIC)
		{
			struct vessel *v = &h2_sound_globals.vessels[h2_sound_globals.vessel_count++];

			snprintf(v->name, sizeof(v->name), "%s", bare(row->h2));
			v->tag_index = tag_index;
			v->permutation = NONE;
			((struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, tag_index))->sound_class = sound_class_from_halo2(p->snd.sound_class);
			music_carried++;
			if (dump)
				platform_log("h2 sound: %s is carried by %s", v->name, tag_get_name(tag_index));
		}
		if (row->kind == ROW_VESSEL)
		{
			struct vessel *v = &h2_sound_globals.vessels[h2_sound_globals.vessel_count++];

			snprintf(v->name, sizeof(v->name), "%s", row->key ? row->key : bare(row->h2));
			v->tag_index = tag_index;
			/* (its class: the vessels are copies of the announcer's sound;
			an Elite's pain and death are a unit's dialogue) */
			if (!strncmp(v->name, "dialog\\combat\\", 14))
				((struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, tag_index))->sound_class = _sound_class_unit_dialog;
			v->permutation = NONE;
			if (dump)
				platform_log("h2 sound: %s is carried by %s", v->name, tag_get_name(tag_index));
		}
		pending_count++;
	}
	h2_load_step("sounds: the rows' Halo 2 sounds found");
	plausible_end();
	platform_log("h2 sound: the dats read %lu times by the rows' end, %lu KB, %llu ms (%lu chunks checked for their codec)", h2_sound_globals.dat_reads,
		h2_sound_globals.dat_read_bytes / 1024, h2_sound_globals.dat_read_us / 1000ULL, h2_sound_globals.plausible_checks);
	if (!pending_count)
	{
		platform_log("h2 sound: no row of the table applies to this level (%ld stand-ins missing, %ld Halo 2 sounds missing)",
			missing_stand_in, missing_h2);
		goto done;
	}

	/* the block: the chunks, the sources (sorted), the ranges and permutations, the work */
	arena_bytes = (unsigned long)pool_count * sizeof(struct h2_sound_codec_chunk) + (unsigned long)source_count * sizeof(struct source) +
		(unsigned long)pending_count * sizeof(struct sound_pitch_range) +
		(unsigned long)pending_permutation_count * sizeof(struct sound_permutation) +
		(unsigned long)loop_plan_count * LOOP_TRACKS * sizeof(struct looping_sound_track) + 64;
	h2_sound_globals.arena = (byte *)halo_custom_edition_memory_alloc(arena_bytes + H2_SOUND_CODEC_WORK_BYTES + 16);
	if (!h2_sound_globals.arena)
	{
		platform_log("h2 sound: no room for the sound tables; the level keeps its stand-in sounds");
		goto done;
	}
	h2_sound_globals.arena_bytes = arena_bytes + H2_SOUND_CODEC_WORK_BYTES + 16;
	memset(h2_sound_globals.arena, 0, h2_sound_globals.arena_bytes);
	offset = 0;
	h2_sound_globals.chunks = (struct h2_sound_codec_chunk *)(h2_sound_globals.arena + offset);
	memcpy(h2_sound_globals.chunks, pool, (size_t)pool_count * sizeof(*pool));
	offset += (unsigned long)pool_count * sizeof(*pool);
	h2_sound_globals.sources = (struct source *)(h2_sound_globals.arena + offset);
	offset += (unsigned long)source_count * sizeof(struct source);
	{
		struct sound_pitch_range *ranges = (struct sound_pitch_range *)(h2_sound_globals.arena + offset);
		struct sound_permutation *permutations;
		long i, j;
		uint32_t *remap = (uint32_t *)calloc((size_t)source_count + 1, sizeof(uint32_t));
		struct sorted_source
		{
			struct source source;
			uint32_t original;
		} *sorted = (struct sorted_source *)calloc((size_t)source_count + 1, sizeof(struct sorted_source));

		offset += (unsigned long)pending_count * sizeof(struct sound_pitch_range);
		permutations = (struct sound_permutation *)(h2_sound_globals.arena + offset);
		offset += (unsigned long)pending_permutation_count * sizeof(struct sound_permutation);
		loop_tracks = (struct looping_sound_track *)(h2_sound_globals.arena + offset);
		offset += (unsigned long)loop_plan_count * LOOP_TRACKS * sizeof(struct looping_sound_track);
		h2_sound_globals.work_bytes = H2_SOUND_CODEC_WORK_BYTES;
		h2_sound_globals.work = (byte *)(((unsigned long)(h2_sound_globals.arena + offset) + 15) & ~15UL);

		/* (sorted: the permutations name sources by their index before the sort) */
		if (!remap || !sorted)
		{
			free(remap);
			free(sorted);
			goto done;
		}
		for (i = 0; i < source_count; i++)
		{
			sorted[i].source = sources[i];
			sorted[i].original = (uint32_t)i;
		}
		/* (compared by the source alone: the struct's first member) */
		qsort(sorted, (size_t)source_count, sizeof(*sorted), source_compare);
		for (i = 0; i < source_count; i++)
		{
			remap[sorted[i].original] = (uint32_t)i;
			h2_sound_globals.sources[i] = sorted[i].source;
		}
		h2_sound_globals.source_count = source_count;
		for (i = 0; i < pending_permutation_count; i++)
			pending_permutations[i].source = remap[pending_permutations[i].source];
		free(remap);
		free(sorted);

		for (i = 0; i < pending_count; i++)
		{
			struct pending_sound *p = &pending[i];
			struct sound_definition *d = (struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, p->tag_index);
			struct sound_pitch_range *template_range;
			struct sound_permutation template_permutation;
			struct sound_pitch_range *range = &ranges[i];
			uint32_t longest = 0, chain_bytes = 0;
			float trim = rows[p->row].trim_db;
			boolean chained = FALSE;

			if (d->pitch_ranges.count < 1)
				continue;
			template_range = TAG_BLOCK_GET_ELEMENT(&d->pitch_ranges, 0, struct sound_pitch_range);
			if (template_range->permutations.count < 1)
				continue;
			template_permutation = *TAG_BLOCK_GET_ELEMENT(&template_range->permutations, 0, struct sound_permutation);
			*range = *template_range;
			/* (a chain's links after the permutations: actual_permutation_count
			the permutations the sound manager picks among) */
			{
				long actual = 0;

				for (j = 0; j < p->permutation_count; j++)
					actual += !pending_permutations[p->permutation_first + j].link;
				range->actual_permutation_count = (short)actual;
			}
			range->played_permutation_mask = 0;
			range->previous_permutation_index = NONE;
			range->forced_permutation_index = NONE;
			/* (pending j's place: the permutations in order, then the links) */
			{
				long actual = range->actual_permutation_count, next_actual = 0, next_link = range->actual_permutation_count;

				for (j = 0; j < p->permutation_count; j++)
					pending_permutations[p->permutation_first + j].place =
						(uint16_t)(pending_permutations[p->permutation_first + j].link ? next_link++ : next_actual++);
				(void)actual;
			}
			for (j = 0; j < p->permutation_count; j++)
			{
				struct pending_permutation const *q = &pending_permutations[p->permutation_first + j];
				struct sound_permutation *permutation = &permutations[p->permutation_first + q->place];
				struct source *source = &h2_sound_globals.sources[q->source];

				*permutation = template_permutation;
				memset(permutation->name, 0, sizeof(permutation->name));
				strncpy(permutation->name, q->name, sizeof(permutation->name) - 1);
				permutation->skip_fraction = 0.f;
				permutation->gain = 1.f;
				permutation->compression = H2_SOUND_PERMUTATION_COMPRESSION;
				/* (a chain's: its next link) */
				permutation->next_permutation_index = j + 1 < p->permutation_count && q[1].link ? (short)q[1].place : NONE;
				if (q->link || permutation->next_permutation_index != NONE)
					chained = TRUE;
				permutation->sample_buffer_size = source->adpcm_bytes;
				permutation->unknown3 = (unsigned long)p->tag_index;
				memset(&permutation->samples, 0, sizeof(permutation->samples));
				permutation->samples.size = (long)source->raw_bytes;
				permutation->samples.pad = permutation_pad(source);
				permutation->samples.file_offset = (long)source->dat_offset;
				memset(&permutation->mouth_data, 0, sizeof(permutation->mouth_data));
				memset(&permutation->subtitle_data, 0, sizeof(permutation->subtitle_data));
				if (!source->permutation)
					source->permutation = permutation;
				/* (a chain's length: its links' together) */
				if (q->link)
					chain_bytes += source->adpcm_bytes;
				else
					chain_bytes = source->adpcm_bytes;
				if (chain_bytes > longest)
					longest = chain_bytes;
			}
			range->permutations.count = p->permutation_count;
			range->permutations.address = &permutations[p->permutation_first];
			d->pitch_ranges.count = 1;
			d->pitch_ranges.address = range;
			d->sample_rate = p->out_rate == 44100 ? 1 : 0;
			d->encoding = p->out_channels == 2 ? 1 : 0;
			d->compression = 1;
			SET_FLAG(d->flags, _sound_definition_linked_permutations_bit, chained);
			/* the distances: the playback record's, else the Halo 2 class's, else the stand-in's */
			{
				float minimum = 0.f, maximum = 0.f;

				if (p->have_playback && p->playback.minimum_distance > 0.f && p->playback.maximum_distance > p->playback.minimum_distance)
				{
					minimum = p->playback.minimum_distance;
					maximum = p->playback.maximum_distance;
				}
				else if (p->snd.sound_class < SOUND_CLASS_COUNT && class_minimum[p->snd.sound_class] > 0.f &&
					class_maximum[p->snd.sound_class] > class_minimum[p->snd.sound_class] && class_maximum[p->snd.sound_class] < 1000.f)
				{
					minimum = class_minimum[p->snd.sound_class];
					maximum = class_maximum[p->snd.sound_class];
				}
				if (p->group_maximum > p->group_minimum && p->group_minimum > 0.f)
				{
					minimum = p->group_minimum;
					maximum = p->group_maximum;
				}
				/* (a sound scenery placement's override distance bounds: Halo 2's
				sound_get_minimum_distance takes a sound's override first,
				halo2-upstream-work src/unknown_124f90.cpp) */
				if (rows[p->row].distance_minimum > 0.f && rows[p->row].distance_maximum > rows[p->row].distance_minimum)
				{
					minimum = rows[p->row].distance_minimum;
					maximum = rows[p->row].distance_maximum;
				}
				if (minimum > 0.f && setting("HALO_H2_SOUND_DISTANCES", 1))
				{
					d->minimum_distance = minimum;
					d->maximum_distance = maximum;
				}
			}
			/* Halo 2's gain for the sound (halo2-upstream-work src/unknown_124f90.cpp
			function_126000 and function_1251e0, src/unknown_218c60.cpp): its
			playback parameters' gain base, plus, picked by its scale (0 to 1)
			between the lower and upper bound, its class's gain bounds added to
			its own gain modifier bounds - in decibels, all of them. Here: the
			gain base is the definition's gain modifier, the bounds its scale
			modifiers (this engine's sound_scale_value picks between them the same
			way); its class's gain bounds come from shared.map's sound_classes
			(HALO_H2_SOUND_MIX=0 leaves them out: a test's). Its pitch modifiers
			the same (cents added, ratios here); its skip fraction the playback
			parameters' */
			{
				struct h2_snd_playback const *pb = p->have_playback ? &p->playback : NULL;
				float gain_db = pb && pb->gain_db >= -64.f && pb->gain_db <= 12.f ? pb->gain_db : 0.f;
				float lower_db = 0.f, upper_db = 0.f;
				boolean mix = setting("HALO_H2_SOUND_MIX", 1) != 0;

				if (mix && classes.read && p->snd.sound_class < SOUND_CLASS_COUNT)
				{
					lower_db = classes.gain_lower_db[p->snd.sound_class];
					upper_db = classes.gain_upper_db[p->snd.sound_class];
				}
				if (mix && p->have_scale && p->scale.gain_lower_db >= -64.f && p->scale.gain_lower_db <= 12.f &&
					p->scale.gain_upper_db >= -64.f && p->scale.gain_upper_db <= 12.f)
				{
					lower_db += p->scale.gain_lower_db;
					upper_db += p->scale.gain_upper_db;
				}
				/* (none above 1: this engine's channels take no more - a Halo 2
				sound above 0 dB plays at 0) */
				d->gain_modifier = db_to_gain(gain_db + trim > 0.f ? 0.f : gain_db + trim);
				d->zero_gain_modifier = db_to_gain(lower_db > 0.f ? 0.f : lower_db);
				d->one_gain_modifier = db_to_gain(upper_db > 0.f ? 0.f : upper_db);
				d->zero_pitch_modifier = 1.f;
				d->one_pitch_modifier = 1.f;
				if (p->have_scale && (p->scale.pitch_lower_cents || p->scale.pitch_upper_cents))
				{
					d->zero_pitch_modifier = cents_to_ratio(p->scale.pitch_lower_cents);
					d->one_pitch_modifier = cents_to_ratio(p->scale.pitch_upper_cents);
				}
				if (pb)
				{
					d->skip_fraction = pb->skip_fraction > 0.f && pb->skip_fraction < 1.f ? pb->skip_fraction : 0.f;
					d->zero_skip_fraction_modifier = 1.f;
					d->one_skip_fraction_modifier = 1.f;
					if (pb->pitch_lower_cents || pb->pitch_upper_cents)
					{
						d->random_pitch_bounds.lower = cents_to_ratio(pb->pitch_lower_cents);
						d->random_pitch_bounds.upper = cents_to_ratio(pb->pitch_upper_cents);
					}
				}
			}
			/* (milliseconds: the multiplayer announcer queue waits the longest permutation's length) */
			d->longest_permutation_length = (long)((unsigned long long)(longest / ((uint32_t)XBOX_ADPCM_BLOCK_BYTES * (uint32_t)p->out_channels)) *
				XBOX_ADPCM_BLOCK_SAMPLES * 1000ull / (unsigned long long)p->out_rate);
			applied++;
			if (dump)
			{
					platform_log("h2 sound: %s <- %s: %ld permutations, %d ch %ld Hz, class %d, codec %d, dist %.2f-%.2f (the engine's class: "
						"%.2f-%.2f) gain %.3f (scale 0 %.3f, 1 %.3f: class %.1f..%.1f dB, own %.1f..%.1f dB, skip %.2f x %.2f..%.2f) pitch %.3f-%.3f, "
						"longest %ld ms (Halo 2's own frames)",
						tag_get_name(p->tag_index), rows[p->row].h2, p->permutation_count, p->out_channels, p->out_rate, (int)p->snd.sound_class,
						(int)p->snd.codec, d->minimum_distance, d->maximum_distance,
						sound_class_get(d->sound_class)->minimum_distance, sound_class_get(d->sound_class)->maximum_distance,
						d->gain_modifier, d->zero_gain_modifier, d->one_gain_modifier,
						p->snd.sound_class < SOUND_CLASS_COUNT ? classes.gain_lower_db[p->snd.sound_class] : 0.f,
						p->snd.sound_class < SOUND_CLASS_COUNT ? classes.gain_upper_db[p->snd.sound_class] : 0.f,
						p->have_scale ? p->scale.gain_lower_db : 0.f, p->have_scale ? p->scale.gain_upper_db : 0.f,
						d->skip_fraction, p->have_scale ? p->scale.skip_lower : 0.f, p->have_scale ? p->scale.skip_upper : 0.f,
						d->random_pitch_bounds.lower, d->random_pitch_bounds.upper, d->longest_permutation_length);
			}
		}
	}
	h2_load_step("sounds: the block made");
	/* the emitters: each scenery sound and machine loop whose sound is a vessel */
	{
		long p;

		for (p = 0; p < plan_count && h2_sound_globals.emitter_count < MAXIMUM_EMITTERS; p++)
		{
			long v;

			/* (its vessel: the one of its key - the sound at its distances -,
			else, the pool short, the sound's own) */
			for (v = 0; v < h2_sound_globals.vessel_count &&
				strcmp(h2_sound_globals.vessels[v].name, plans[p].key); v++)
				;
			if (v >= h2_sound_globals.vessel_count)
			{
				for (v = 0; v < h2_sound_globals.vessel_count &&
					strcmp(h2_sound_globals.vessels[v].name, bare(plans[p].name)); v++)
					;
			}
			if (v < h2_sound_globals.vessel_count)
			{
				struct emitter *e = &h2_sound_globals.emitters[h2_sound_globals.emitter_count++];

				e->vessel = v;
				e->position = plans[p].position;
				e->loop = plans[p].loop;
				e->unspatialized = plans[p].unspatialized;
				e->period_minimum = plans[p].period_minimum;
				e->period_maximum = plans[p].period_maximum;
				/* (spread: the scenery does not all start at once) */
				e->next_time = random_unit() * (e->loop ? 0.5 : (double)e->period_minimum);
				e->follow_name = plans[p].follow_name;
				e->object_index = e->sound_index = NONE;
				e->gain = db_to_gain(plans[p].gain_db);
				if (e->gain > 1.f)
					e->gain = 1.f;
				if (dump)
				{
					struct sound_definition const *d = sound_definition_get(h2_sound_globals.vessels[v].tag_index);

					platform_log("h2 sound: emitter %ld: %s (%s) at %.1f %.1f %.1f, %s, gain %.1f dB x scale 1 %.1f dB, distances %.1f-%.1f",
						h2_sound_globals.emitter_count - 1, plans[p].name, tag_get_name(h2_sound_globals.vessels[v].tag_index),
						e->position.x, e->position.y, e->position.z, e->loop ? "loop" : "details", plans[p].gain_db,
						20.0 * log10((double)(d->gain_modifier * d->one_gain_modifier) + 1e-9), d->minimum_distance, d->maximum_distance);
				}
			}
		}
	}
	/* the looping sounds: each plan given a looping sound tag of the level,
	its tracks the music sound tags carrying their sounds */
	h2_sound_globals.loop_count = 0;
	h2_sound_globals.palette_loop_count = 0;
	h2_sound_globals.palette_environment_count = 0;
	if (loop_tracks)
		loops_make(loop_plans, loop_plan_count, music_names, loop_tracks, dump);
	environments_make(&caches, dump);
	if (loop_plan_count)
	{
		platform_log("h2 sound: the looping sounds' tracks play %ld Halo 2 sounds, %ld of them carried (%ld music sound tags)%s", music_count,
			music_carried, music_tag_count, music_count >= MAXIMUM_MUSIC_SOUNDS || loop_plan_count >= MAXIMUM_LOOPS ? " - more than this level has room for" : "");
	}
	h2_sound_globals.random = 0x2468ACE1UL;
	h2_sound_globals.time = 0.0;
	h2_sound_globals.loaded = TRUE;
	h2_sound_classes_halo2(TRUE);
	{
		unsigned long adpcm = 0;
		long i, copied = 0;

		for (i = 0; i < h2_sound_globals.source_count; i++)
		{
			adpcm += h2_sound_globals.sources[i].adpcm_bytes;
			copied += h2_sound_codec_is_copy(h2_sound_globals.sources[i].kind, h2_sound_globals.sources[i].channels,
				(long)h2_sound_globals.sources[i].rate, (long)h2_sound_globals.sources[i].out_rate,
				h2_sound_globals.sources[i].out_channels);
		}
		platform_log("h2 sound: %s: %ld stand-in sounds given Halo 2's (%ld by their own names; %ld of the table missing from the level, "
			"%ld Halo 2 sounds not found, %ld permutations skipped); %ld vessels (%ld of %ld script sounds), %ld emitters; %ld streams (%ld copied as they lie), "
			"%lu KB of Xbox ADPCM, tables %lu KB, %.1f ms (maps and gestalts opened %.1f, names %.1f)",
			map_name, applied, auto_rows, missing_stand_in, missing_h2, skipped_permutations, h2_sound_globals.vessel_count,
			script_sounds_carried, script_sound_count,
			h2_sound_globals.emitter_count, h2_sound_globals.source_count, copied, adpcm / 1024, h2_sound_globals.arena_bytes / 1024,
			(double)(vita_host_time_us() - started) / 1000.0, (double)(opened_at - started) / 1000.0, (double)(named_at - opened_at) / 1000.0);
	}
	h2_load_step("sounds: the emitters and looping sounds");
	/* (a check: HALO_H2_SOUND_TABLES_DUMP=<file>: the loaded tables written,
	the arena's words that point into it made offsets - two loads compared) */
	if (HALO_ENV_ONCE("HALO_H2_SOUND_TABLES_DUMP"))
		sound_tables_dump(HALO_ENV_ONCE("HALO_H2_SOUND_TABLES_DUMP"), map_name);
	/* (test: HALO_H2_MUSIC_SOLO=1 - every sound but the music silent, to
	hear and record it alone) */
	if (setting("HALO_H2_MUSIC_SOLO", 0))
	{
		struct tag_iterator iterator;
		long tag_index;

		tag_iterator_new(&iterator, SOUND_DEFINITION_TAG);
		while ((tag_index = tag_iterator_next(&iterator)) != NONE)
		{
			struct sound_definition *d = (struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, tag_index);

			if (d->sound_class != _sound_class_music)
				d->gain_modifier = 0.f;
		}
	}
	/* (test: HALO_H2_SOUND_SOLO_CLASS="<class>,<class>,..." - every sound
	not of those classes (this engine's numbers, Halo 2's for the first 36)
	silent, to measure a class's part of the mix) */
	if (HALO_ENV_ONCE("HALO_H2_SOUND_SOLO_CLASS"))
	{
		struct tag_iterator iterator;
		long tag_index;
		char const *list = HALO_ENV_ONCE("HALO_H2_SOUND_SOLO_CLASS");

		tag_iterator_new(&iterator, SOUND_DEFINITION_TAG);
		while ((tag_index = tag_iterator_next(&iterator)) != NONE)
		{
			struct sound_definition *d = (struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, tag_index);
			char const *cursor = list;
			boolean kept = FALSE;

			while (*cursor && !kept)
			{
				kept = atoi(cursor) == d->sound_class;
				while (*cursor && *cursor != ',')
					cursor++;
				if (*cursor == ',')
					cursor++;
			}
			if (!kept)
				d->gain_modifier = 0.f;
		}
	}
	voice_slots_resolve();
	custom_edition_sounds_h2_loaded();
	{
		unsigned long inflated = 0, evicted = 0;

		h2_cache_segment_counts(caches.cache[0], &inflated, &evicted);
		h2_cache_segment_counts(caches.cache[1], &inflated, &evicted);
		platform_log("h2 sound: the maps' chunks inflated %lu times (%lu pieces let go, budget %ld KB)", inflated, evicted,
			setting("HALO_H2_SOUND_SEGMENT_KB", H2_SOUND_SEGMENT_KB_DEFAULT));
	}

done:
	plausible_end();
	for (row_index = 0; row_index < 2; row_index++)
	{
		if (caches.gestalt[row_index])
			h2_gestalt_close(caches.gestalt[row_index]);
		if (caches.cache[row_index])
			h2_cache_close(caches.cache[row_index]);
	}
	/* (the chunks kept and the build's heap this load opened: let go, its
	blocks given back) */
	if (sound_heap_begun)
	{
		h2_cache_chunks_end();
		h2_build_heap_end();
	}
	free(pending);
	free(pending_permutations);
	free(sources);
	free(pool);
	free(candidates);
	free(sound_names);
	free(rows);
	free(plans);
	free(loop_plans);
	free(music_names);
	if (!h2_sound_globals.loaded)
	{
		if (h2_sound_globals.arena)
		{
			halo_custom_edition_memory_free(h2_sound_globals.arena);
			h2_sound_globals.arena = NULL;
		}
		h2_sound_globals.sources = NULL;
		h2_sound_globals.source_count = 0;
		h2_sound_globals.vessel_count = 0;
		h2_sound_globals.emitter_count = 0;
		h2_sound_globals.loop_count = 0;
		h2_sound_globals.palette_loop_count = 0;
		h2_sound_globals.palette_environment_count = 0;
	h2_sound_globals.palette_environment_count = 0;
		close_files();
	}
}

/* Halo 2's own sound classes' limits (shared.map's sound\sound_classes,
0x78 a class: its sounds at once of one tag and of one tag from one object,
its preemption time and priority) set on this engine's classes of the same
numbers (0 to 35, and 44 to 47, the dialogue's) while a Halo 2 level's
sounds are loaded; Halo 1's again after. A weapon's fire 8 and 2 (Halo 1's 4
and 1) - a battle rifle's burst sound, a second long, plays on as the next
burst starts (with one an object, each burst cut the last off: choppy), as
an SMG's and plasma rifle's shots overlap -, a vehicle's engine 8 and 3,
footsteps 4 and 2. Without the table (an old map), the two classes below
(HALO_SOUND_TRACE=1 logs a sound cutting its own off) */
static struct
{
	short sound_class, per_definition, per_object;
} const h2_sound_class_limits[] =
{
	{ 4, 8, 2 },    /* weapon_fire */
	{ 6, 8, 2 },    /* weapon_reload */
};
#define H2_SOUND_CLASS_LIMIT_COUNT ((long)(sizeof(h2_sound_class_limits) / sizeof(h2_sound_class_limits[0])))
static struct sound_class_definition h2_sound_class_saved[SOUND_CLASS_COUNT];
static boolean h2_sound_class_limits_set = FALSE;

static boolean h2_sound_class_shared(long c)
{
	/* (sound_classes.c: a class this engine has not is unnamed - its
	sound_class_get asserts) */
	extern char const *sound_class_names[];

	return (c <= 35 || (c >= 44 && c <= 47)) && sound_class_names[c] && sound_class_names[c][0];
}

static void h2_sound_classes_halo2(boolean halo2)
{
	long i;

	/* (HALO_H2_SOUND_CLASSES=0 keeps Halo 1's: a test's) */
	if (halo2 == h2_sound_class_limits_set || (halo2 && !setting("HALO_H2_SOUND_CLASSES", 1)))
		return;
	if (halo2)
	{
		for (i = 0; i < SOUND_CLASS_COUNT; i++)
		{
			if (h2_sound_class_shared(i))
				h2_sound_class_saved[i] = *sound_class_get((short)i);
		}
		if (classes.read)
		{
			for (i = 0; i < SOUND_CLASS_COUNT; i++)
			{
				struct sound_class_definition *definition;

				if (!h2_sound_class_shared(i) || classes.per_tag[i] < 1 || classes.per_tag[i] > 16 || classes.per_object[i] < 1 ||
					classes.per_object[i] > 16)
				{
					continue;
				}
				definition = sound_class_get((short)i);
				definition->maximum_number_per_definition = classes.per_tag[i];
				definition->maximum_number_per_object = classes.per_object[i];
				if (classes.preemption[i] >= 0 && classes.preemption[i] < 10000)
					definition->preemption_time = classes.preemption[i];
				definition->priority = classes.priority[i];
			}
		}
		else
		{
			for (i = 0; i < H2_SOUND_CLASS_LIMIT_COUNT; i++)
			{
				struct sound_class_definition *definition = sound_class_get(h2_sound_class_limits[i].sound_class);

				definition->maximum_number_per_definition = h2_sound_class_limits[i].per_definition;
				definition->maximum_number_per_object = h2_sound_class_limits[i].per_object;
			}
		}
	}
	else
	{
		for (i = 0; i < SOUND_CLASS_COUNT; i++)
		{
			if (h2_sound_class_shared(i))
				*sound_class_get((short)i) = h2_sound_class_saved[i];
		}
	}
	h2_sound_class_limits_set = halo2;
	/* (and a sound that takes a playing one's channel fades it out, not
	cuts it: a battle rifle's burst's tail ends smoothly under the next;
	HALO_SOUND_STEAL_FADE_MS another length, 0 cut - a test's) */
	{
		extern long sound_steal_fade_milliseconds;

		sound_steal_fade_milliseconds = halo2 ? setting("HALO_SOUND_STEAL_FADE_MS", 80) : 0;
	}
	/* (and Halo 2's distance falloff - inverse distance to nothing at the
	maximum distance, its sound_get_distance_gain - in the sound manager and
	the SDL mixer; HALO_H2_SOUND_FALLOFF=0 keeps DirectSound's, held beyond
	the maximum: a test's) */
	{
		extern boolean sound_distance_falloff_halo2;
		extern int dsound_sdl_falloff_halo2;

		sound_distance_falloff_halo2 = halo2 && setting("HALO_H2_SOUND_FALLOFF", 1);
		dsound_sdl_falloff_halo2 = sound_distance_falloff_halo2;
	}
}

void h2_sounds_unloaded(void)
{
	if (!h2_sound_globals.loaded && !h2_sound_globals.arena)
		return;
	/* (the decoding thread first: it reads these) */
	custom_edition_sounds_stop();
	close_files();
	if (h2_voice.gestalt)
		h2_gestalt_close(h2_voice.gestalt);
	memset(&h2_voice, 0, sizeof(h2_voice));
	if (h2_sound_globals.arena)
		halo_custom_edition_memory_free(h2_sound_globals.arena);
	h2_sound_globals.arena = NULL;
	h2_sound_globals.arena_bytes = 0;
	h2_sound_globals.sources = NULL;
	h2_sound_globals.chunks = NULL;
	h2_sound_globals.source_count = 0;
	h2_sound_globals.work = NULL;
	h2_sound_globals.dat_bytes = 0;
	h2_sound_globals.vessel_count = 0;
	h2_sound_globals.emitter_count = 0;
	h2_sound_globals.loop_count = 0;
	h2_sound_globals.palette_loop_count = 0;
	h2_sound_globals.palette_environment_count = 0;
	h2_sound_globals.loaded = FALSE;
	radio_permutation_count = 0;
	h2_sound_classes_halo2(FALSE);
}

/* ---------- the world: ambience, the announcer and the hooks */

boolean h2_sound_script_sound(
	char const *h2_name,
	long *tag_index,
	real *seconds)
{
	long v;

	*tag_index = NONE;
	*seconds = 0.f;
	if (!h2_sound_globals.loaded || !h2_name)
		return FALSE;
	for (v = 0; v < h2_sound_globals.vessel_count; v++)
	{
		struct vessel const *vessel = &h2_sound_globals.vessels[v];
		struct sound_definition *d;

		if (strcmp(vessel->name, bare(h2_name)))
			continue;
		d = (struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, vessel->tag_index);
		if (d->pitch_ranges.count < 1)
			return FALSE;
		{
			struct sound_pitch_range *range = TAG_BLOCK_GET_ELEMENT(&d->pitch_ranges, 0, struct sound_pitch_range);
			long k = vessel->permutation;

			if (k == NONE || k >= range->permutations.count)
				k = 0;
			if (range->permutations.count < 1)
				return FALSE;
			range->forced_permutation_index = vessel->permutation != NONE ? (short)k : NONE;
			*seconds = (real)permutation_seconds(d, k);
		}
		*tag_index = vessel->tag_index;
		return TRUE;
	}
	return FALSE;
}

long h2_sound_looping(
	char const *h2_name)
{
	long i;

	if (!h2_sound_globals.loaded || !h2_name)
		return NONE;
	for (i = 0; i < h2_sound_globals.loop_count; i++)
	{
		if (!strcmp(h2_sound_globals.loops[i].name, h2_name))
			return h2_sound_globals.loops[i].tag_index;
	}
	return NONE;
}

long h2_sound_vessel(
	char const *h2_name)
{
	long v;

	if (!h2_sound_globals.loaded || !h2_name)
		return NONE;
	for (v = 0; v < h2_sound_globals.vessel_count; v++)
	{
		if (!strcmp(h2_sound_globals.vessels[v].name, bare(h2_name)))
			return h2_sound_globals.vessels[v].tag_index;
	}
	return NONE;
}

/* the length in seconds of permutation `k` of a sound tag's one range */
static double permutation_seconds(struct sound_definition const *d, long k)
{
	struct sound_pitch_range const *range = TAG_BLOCK_GET_ELEMENT(&d->pitch_ranges, 0, struct sound_pitch_range);
	double frames = 0.0;
	long links = 0;

	/* (a long one made a chain, custom_edition_sounds.c: all its links) */
	while (k >= 0 && k < range->permutations.count && links++ < range->permutations.count)
	{
		struct sound_permutation const *permutation = TAG_BLOCK_GET_ELEMENT(&range->permutations, k, struct sound_permutation);

		frames += (double)(permutation->sample_buffer_size / ((uint32_t)XBOX_ADPCM_BLOCK_BYTES * (uint32_t)(d->encoding == 1 ? 2 : 1))) *
			XBOX_ADPCM_BLOCK_SAMPLES;
		if (!TEST_FLAG(d->flags, _sound_definition_linked_permutations_bit))
			break;
		k = permutation->next_permutation_index;
	}
	return frames / (d->sample_rate == 1 ? 44100.0 : 22050.0);
}

/* one of a vessel's permutations started: at a place, or unspatialized; the
seconds it lasts, 0 if it could not be heard (too far) */
/* a vessel's sound played on an object (at its origin) at a gain: its
length in seconds, 0 when not played; its sound index */
static double object_fire(long tag_index, long object_index, real gain, long *sound_index)
{
	struct sound_definition *d;
	struct sound_pitch_range *range;
	real_point3d origin = { 0.0f, 0.0f, 0.0f };
	real_vector3d forward = { 1.0f, 0.0f, 0.0f };
	long k;

	*sound_index = NONE;
	if (tag_index == NONE)
		return 0.0;
	d = (struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, tag_index);
	if (d->pitch_ranges.count < 1)
		return 0.0;
	range = TAG_BLOCK_GET_ELEMENT(&d->pitch_ranges, 0, struct sound_pitch_range);
	if (range->permutations.count < 1)
		return 0.0;
	k = (long)(random_unit() * (double)range->permutations.count);
	if (k >= range->permutations.count)
		k = range->permutations.count - 1;
	range->forced_permutation_index = (short)k;
	*sound_index = object_impulse_sound_new_gain(object_index, tag_index, 0, &origin, &forward, 1.f, gain);
	if (*sound_index == NONE)
	{
		range->forced_permutation_index = NONE;
		return 0.0;
	}
	return permutation_seconds(d, k);
}

static double vessel_fire(long tag_index, real_point3d const *position, boolean unspatialized, real gain)
{
	struct sound_definition *d;
	struct sound_pitch_range *range;
	long k, sound;

	if (tag_index == NONE)
		return 0.0;
	d = (struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, tag_index);
	if (d->pitch_ranges.count < 1)
		return 0.0;
	range = TAG_BLOCK_GET_ELEMENT(&d->pitch_ranges, 0, struct sound_pitch_range);
	if (range->permutations.count < 1)
		return 0.0;
	/* (the one picked here, so that its length is known) */
	k = (long)(random_unit() * (double)range->permutations.count);
	if (k >= range->permutations.count)
		k = range->permutations.count - 1;
	range->forced_permutation_index = (short)k;
	if (unspatialized)
	{
		sound = unspatialized_impulse_sound_new_gain(tag_index, 1.f, gain);
	}
	else
	{
		struct sound_location location;

		memset(&location, 0, sizeof(location));
		location.position = *position;
		/* (a direction that is a direction: the sound manager asserts it) */
		location.forward.i = 1.f;
		scenario_location_from_point(&location.game_location, position);
		/* (a place outside the structure has no cluster to be heard from) */
		if (location.game_location.cluster_index == NONE)
		{
			range->forced_permutation_index = NONE;
			return 0.0;
		}
		sound = unattached_impulse_sound_new_gain(tag_index, &location, 1.f, gain);
	}
	if (sound == NONE)
	{
		range->forced_permutation_index = NONE;
		return 0.0;
	}
	return permutation_seconds(d, k);
}

/* the name of the background sound palette entry of the first local player's cluster ("" if none) */
static char const *listener_ambience(void)
{
	struct structure_bsp *bsp = global_structure_bsp_get();
	long player = local_player_get_player_index(0);
	struct observer_result const *camera;
	struct structure_cluster *cluster;

	if (player == NONE || !bsp)
		return "";
	camera = observer_get_camera(0);
	if (camera->location.cluster_index == NONE || camera->location.cluster_index >= bsp->clusters.count)
		return "";
	cluster = TAG_BLOCK_GET_ELEMENT(&bsp->clusters, camera->location.cluster_index, struct structure_cluster);
	if (cluster->background_sound_palette_index < 0 || cluster->background_sound_palette_index >= bsp->background_sound_palette.count)
		return "";
	return (char const *)TAG_BLOCK_GET_ELEMENT(&bsp->background_sound_palette, cluster->background_sound_palette_index,
		struct structure_background_sound_palette_entry);
}

void h2_sounds_update(
	real dt)
{
	long i;

	if (!h2_sound_globals.loaded || global_structure_bsp_tag_index_get() == NONE)
		return;
	palette_update();
	/* (test: HALO_H2_LOOP_TEST="<seconds>:<looping sound>[;<seconds>:[stop=|alt=|noalt=|scale=<x>=]<looping sound>...]"
	starts each then, as sound_looping_start would - a cue the level's play
	does not reach headless -, or stops it / sets its alternate / its scale
	as the scripts' functions would) */
	{
		static char const *test;
		static long test_cue;
		char const *setting_text = HALO_ENV_ONCE("HALO_H2_LOOP_TEST");

		if (setting_text && test != setting_text)
		{
			test = setting_text;
			test_cue = 0;
		}
		while (test && test_cue >= 0)
		{
			char const *cue = test;
			char const *colon, *end, *name;
			char what[16] = "start", loop_name[160];
			double at;
			long tag_index, k;

			for (k = 0; k < test_cue && cue; k++)
				cue = strchr(cue, ';') ? strchr(cue, ';') + 1 : NULL;
			if (!cue || !*cue || !(colon = strchr(cue, ':')))
			{
				test_cue = -1;
				break;
			}
			at = atof(cue);
			if (h2_sound_globals.time + (double)dt < at)
				break;
			name = colon + 1;
			/* (the kind before the name, '=' between - no spaces: the harness
			passes the setting through a word list) */
			if (!strncmp(name, "stop=", 5) || !strncmp(name, "alt=", 4) || !strncmp(name, "noalt=", 6) || !strncmp(name, "scale=", 6))
			{
				snprintf(what, sizeof(what), "%.*s", (int)(strchr(name, '=') - name), name);
				name = strchr(name, '=') + 1;
				if (!strcmp(what, "scale"))
					name = strchr(name, '=') ? strchr(name, '=') + 1 : name;
			}
			end = strchr(name, ';');
			snprintf(loop_name, sizeof(loop_name), "%.*s", end ? (int)(end - name) : (int)strlen(name), name);
			tag_index = h2_sound_looping(loop_name);
			platform_log("h2 sound: test: %s %s at %.1f s (%s)", what, loop_name, h2_sound_globals.time,
				tag_index != NONE ? tag_get_name(tag_index) : "not carried");
			if (tag_index != NONE)
			{
				if (!strcmp(what, "stop"))
					scripted_looping_sound_stop(tag_index);
				else if (!strcmp(what, "alt"))
					scripted_looping_sound_set_alternate(tag_index, TRUE);
				else if (!strcmp(what, "noalt"))
					scripted_looping_sound_set_alternate(tag_index, FALSE);
				else if (!strcmp(what, "scale"))
					scripted_looping_sound_set_scale(tag_index, (real)atof(colon + 7));
				else
					scripted_looping_sound_start(tag_index, NONE, 1.f);
			}
			test_cue++;
		}
	}
	if (!h2_sound_globals.emitter_count)
	{
		h2_sound_globals.time += (double)dt;
		return;
	}
	h2_sound_globals.time += (double)dt;
	/* (the dump: the listener's cluster's background sound, which this
	engine plays - the bed; game_sound.c) */
	if (setting("HALO_H2_SOUND_DUMP", 0))
	{
		extern long game_sound_background_looping_sound_index(void);
		extern short game_looping_sound_state(long looping_sound_index);
		char const *name = listener_ambience();
		static double said_at = -10.0;

		if (strncmp(name, h2_sound_globals.ambience_name, sizeof(h2_sound_globals.ambience_name) - 1) ||
			h2_sound_globals.time - said_at >= 10.0)
		{
			long index = game_sound_background_looping_sound_index();

			snprintf(h2_sound_globals.ambience_name, sizeof(h2_sound_globals.ambience_name), "%s", name);
			platform_log("h2 sound: the listener's background sound is \"%s\" at %.1f s (looping sound %08lx, state %d)", name,
				h2_sound_globals.time, (unsigned long)index, (int)game_looping_sound_state(index));
			said_at = h2_sound_globals.time;
		}
	}
	for (i = 0; i < h2_sound_globals.emitter_count; i++)
	{
		struct emitter *e = &h2_sound_globals.emitters[i];
		double length;

		/* (a named machine's loop: on its object while it exists, from the
		tick it is made; stopped as it goes) */
		if (e->follow_name)
		{
			long object_index = object_index_from_name_index((short)(e->follow_name - 1));

			if (object_index != e->object_index)
			{
				if (e->sound_index != NONE)
					sound_stop_impulse(e->sound_index);
				e->sound_index = NONE;
				e->object_index = object_index;
				e->next_time = h2_sound_globals.time;
			}
			if (object_index == NONE || h2_sound_globals.time < e->next_time)
				continue;
			length = object_fire(h2_sound_globals.vessels[e->vessel].tag_index, object_index, e->gain, &e->sound_index);
			if (setting("HALO_H2_SOUND_DUMP", 0))
			{
				platform_log("h2 sound: %s on %08lx (name %d) at %.1f s: %.1f s long, gain %.2f", h2_sound_globals.vessels[e->vessel].name,
					(unsigned long)object_index, (int)e->follow_name - 1, h2_sound_globals.time, length, e->gain);
			}
			e->next_time = h2_sound_globals.time + (length > 0.0 ? length - 0.04 : 0.3);
			continue;
		}
		if (h2_sound_globals.time < e->next_time)
			continue;
		length = vessel_fire(h2_sound_globals.vessels[e->vessel].tag_index, &e->position, e->unspatialized, e->gain);
		/* (HALO_H2_SOUND_DUMP=2: each start, with the camera's distance) */
		if (setting("HALO_H2_SOUND_DUMP", 0) >= 2)
		{
			struct observer_result const *camera = observer_get_camera(0);
			real x = camera->position.x - e->position.x, y = camera->position.y - e->position.y, z = camera->position.z - e->position.z;

			platform_log("h2 sound: emitter %ld %s at %.1f s: %s, %.1f s long, gain %.2f, %.1f away", i,
				h2_sound_globals.vessels[e->vessel].name, h2_sound_globals.time, length > 0.0 ? "started" : "not heard", length, e->gain,
				sqrt(x * x + y * y + z * z));
		}
		if (length <= 0.0)
		{
			/* (out of hearing: looked at again soon) */
			e->next_time = h2_sound_globals.time + 0.3;
		}
		else if (e->loop)
		{
			e->next_time = h2_sound_globals.time + length - 0.04;
		}
		else
		{
			double span = (double)e->period_maximum - (double)e->period_minimum;

			e->next_time = h2_sound_globals.time + (double)e->period_minimum + random_unit() * (span > 0.0 ? span : 0.0);
		}
	}
}

/* whether the level has this Halo 2 sound (tests) */
boolean h2_sound_exists(char const *h2_name)
{
	return h2_sound_vessel(h2_name) != NONE;
}

boolean h2_sound_play_at(
	char const *h2_name,
	real_point3d const *position)
{
	long tag_index = h2_sound_vessel(h2_name);

	return tag_index != NONE && position && vessel_fire(tag_index, position, FALSE, 1.f) > 0.0;
}

/* the permutations radio lines have played as (dsound_sdl.c filters them) */
static void const *radio_permutations[1024];
static long radio_permutation_count;

boolean h2_sound_permutation_radio(void const *permutation)
{
	long k;

	for (k = 0; permutation && k < radio_permutation_count; k++)
	{
		if (radio_permutations[k] == permutation)
			return TRUE;
	}
	return FALSE;
}

static void radio_permutation_mark(void const *permutation)
{
	if (HALO_ENV_ONCE("HALO_H2_CINEMATIC_LOG"))
		platform_log("h2 sound: radio permutation %p marked", permutation);
	if (!h2_sound_permutation_radio(permutation) && radio_permutation_count < (long)NUMBEROF(radio_permutations))
		radio_permutations[radio_permutation_count++] = permutation;
}

/* a script's sound_impulse_start_effect radio_*: every permutation of its tag
that its name's line may be, as h2_sound_play_on_object picks it */
void h2_sound_radio_mark(char const *h2_name, long tag_index)
{
	struct sound_definition *d = tag_index != NONE ? (struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, tag_index) : NULL;
	struct sound_pitch_range *range;
	long v, k;

	if (!d || d->pitch_ranges.count < 1)
		return;
	range = TAG_BLOCK_GET_ELEMENT(&d->pitch_ranges, 0, struct sound_pitch_range);
	for (v = 0; v < h2_sound_globals.vessel_count; v++)
	{
		if (h2_sound_globals.vessels[v].tag_index == tag_index && !strcmp(h2_sound_globals.vessels[v].name, bare(h2_name)) &&
			h2_sound_globals.vessels[v].permutation != NONE && h2_sound_globals.vessels[v].permutation < range->permutations.count)
		{
			radio_permutation_mark(TAG_BLOCK_GET_ELEMENT(&range->permutations, h2_sound_globals.vessels[v].permutation,
				struct sound_permutation));
			return;
		}
	}
	for (k = 0; k < range->permutations.count; k++)
		radio_permutation_mark(TAG_BLOCK_GET_ELEMENT(&range->permutations, k, struct sound_permutation));
}

/* a Halo 2 sound played unspatialized (a radio line: heard wherever its
speaker is), its permutation as h2_sound_play_on_object picks it */
long h2_sound_play_unspatialized(
	char const *h2_name,
	real *seconds)
{
	h2_sound_radio_mark(h2_name, h2_sound_vessel(h2_name));
	return h2_sound_play_on_object(h2_name, H2_SOUND_UNSPATIALIZED, seconds);
}

long h2_sound_play_on_object(
	char const *h2_name,
	long object_index,
	real *seconds)
{
	long tag_index = h2_sound_vessel(h2_name), k, sound;
	struct sound_definition *d;
	struct sound_pitch_range *range;
	real_point3d origin = { 0.0f, 0.0f, 0.0f };
	real_vector3d forward = { 1.0f, 0.0f, 0.0f };

	if (seconds)
		*seconds = 0.0f;
	/* (else a sound of the level's own named for it: sound\\h2\\<name>) */
	if (tag_index == NONE && h2_sound_globals.loaded && h2_name)
	{
		char name[256];

		snprintf(name, sizeof(name), "sound\\h2\\%s", bare(h2_name));
		tag_index = tag_loaded(SOUND_DEFINITION_TAG, name);
	}
	if (tag_index == NONE || object_index == NONE)
		return NONE;
	d = (struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, tag_index);
	if (d->pitch_ranges.count < 1)
		return NONE;
	range = TAG_BLOCK_GET_ELEMENT(&d->pitch_ranges, 0, struct sound_pitch_range);
	if (range->permutations.count < 1)
		return NONE;
	/* (the one picked here, so that its length is known: a script group's
	line its own permutation - a line of a group is one permutation of a pool
	sound, the others other lines -, any other's at random) */
	k = (long)(random_unit() * (double)range->permutations.count);
	if (k >= range->permutations.count)
		k = range->permutations.count - 1;
	{
		long v;

		for (v = 0; v < h2_sound_globals.vessel_count; v++)
		{
			if (h2_sound_globals.vessels[v].tag_index == tag_index && !strcmp(h2_sound_globals.vessels[v].name, bare(h2_name)))
			{
				if (h2_sound_globals.vessels[v].permutation != NONE && h2_sound_globals.vessels[v].permutation < range->permutations.count)
					k = h2_sound_globals.vessels[v].permutation;
				break;
			}
		}
	}
	range->forced_permutation_index = (short)k;
	sound = object_index == H2_SOUND_UNSPATIALIZED ? unspatialized_impulse_sound_new(tag_index, 1.f) :
		object_impulse_sound_new(object_index, tag_index, 0, &origin, &forward, 1.f);
	if (sound == NONE)
	{
		range->forced_permutation_index = NONE;
		return NONE;
	}
	if (seconds)
		*seconds = (real)permutation_seconds(d, k);
	return sound;
}

boolean h2_sound_machine_event(
	char const *machine,
	int event,
	real_point3d const *position)
{
	long m;

	if (!machine)
		return FALSE;
	for (m = 0; machine_events[m].machine; m++)
	{
		if (machine_events[m].event == event && strstr(machine, machine_events[m].machine))
			return h2_sound_play_at(machine_events[m].sound, position);
	}
	return FALSE;
}

/* ---- the AI's voices' slots (h2_dialogue.c) */

/* the gestalt of the level's cache the scripts keep open, opened as a voice
first speaks */
static struct h2_gestalt *voice_gestalt(struct h2_cache **cache_out)
{
	struct h2_cache *cache = h2_scripts_cache();

	*cache_out = cache;
	if (!cache)
		return NULL;
	if (h2_voice.gestalt && h2_voice.gestalt_cache == cache)
		return h2_voice.gestalt;
	if (h2_voice.gestalt)
		h2_gestalt_close(h2_voice.gestalt);
	h2_voice.gestalt = h2_gestalt_open(cache);
	h2_voice.gestalt_cache = cache;
	return h2_voice.gestalt;
}

/* the slots made: each seed's vessel (its group's tag and permutation) */
static void voice_slots_resolve(void)
{
	long i, v, made = 0;

	for (i = 0; i < h2_voice.count; i++)
	{
		h2_voice.slots[i].permutation = NULL;
		h2_voice.slots[i].sound_tag = NONE;
		for (v = 0; v < h2_sound_globals.vessel_count; v++)
		{
			struct vessel const *vessel = &h2_sound_globals.vessels[v];
			struct sound_definition *d;
			struct sound_pitch_range *range;

			if (strcmp(vessel->name, h2_voice.names[i]) || vessel->permutation == NONE)
				continue;
			d = (struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, vessel->tag_index);
			if (d->pitch_ranges.count < 1)
				break;
			range = TAG_BLOCK_GET_ELEMENT(&d->pitch_ranges, 0, struct sound_pitch_range);
			if (vessel->permutation >= range->permutations.count)
				break;
			h2_voice.slots[i].permutation = TAG_BLOCK_GET_ELEMENT(&range->permutations, vessel->permutation, struct sound_permutation);
			h2_voice.slots[i].tag_index = vessel->tag_index;
			h2_voice.slots[i].index = (short)vessel->permutation;
			made++;
			break;
		}
	}
	if (h2_voice.count)
		platform_log("h2 sound: %ld of %ld voice slots made", made, h2_voice.count);
}

/* (h2_sound_stream) a slot's source for a permutation's key, and its chunks */
static struct source const *voice_source_find(unsigned long pad, uint32_t dat_offset, uint32_t raw_bytes)
{
	long i;

	for (i = 0; i < h2_voice.count; i++)
	{
		struct source const *s = &h2_voice.sources[i];

		if (h2_voice.slots[i].sound_tag != NONE && s->dat_offset == dat_offset && s->raw_bytes == raw_bytes && permutation_pad(s) == pad)
			return s;
	}
	return NULL;
}

real h2_sound_voice_seconds(
	long voice_tag,
	long sound_tag)
{
	struct h2_cache *cache;
	struct h2_gestalt *gestalt = voice_gestalt(&cache);
	struct h2_cache_tag t;
	struct h2_snd snd;
	struct h2_snd_range range;
	int channels;
	long rate, k;
	real best = 0.0f;

	(void)voice_tag;
	if (!gestalt || sound_tag == NONE || !h2_cache_tag_get(cache, sound_tag, &t) || t.group_tag != 'snd!' ||
		!h2_gestalt_sound(gestalt, t.address, &snd) || snd.range_count < 1 || !h2_gestalt_range(gestalt, snd.first_range, &range))
	{
		return 0.0f;
	}
	channels = h2_gestalt_codec_channels(snd.codec);
	rate = h2_gestalt_codec_rate(snd.codec);
	if (!channels || !rate)
		return 0.0f;
	for (k = 0; k < range.permutation_count; k++)
	{
		struct h2_snd_permutation permutation;
		real seconds;

		if (!h2_gestalt_permutation(gestalt, range.first_permutation + k, 0, &permutation))
			continue;
		seconds = (real)((double)permutation.decoded_bytes / (2.0 * channels) / (double)rate);
		if (seconds > best)
			best = seconds;
	}
	return best;
}

long h2_sound_voice_play(
	long voice_tag,
	long sound_tag,
	long unit_index,
	short *last_permutation,
	long last_time,
	long repetition_ticks,
	real *seconds)
{
	struct h2_cache *cache;
	struct h2_gestalt *gestalt = voice_gestalt(&cache);
	struct h2_cache_tag t;
	struct h2_snd snd;
	struct h2_snd_range range;
	struct h2_snd_permutation permutation;
	struct voice_slot *slot = NULL;
	long k, i, now = game_time_get();
	real_point3d origin = { 0.0f, 0.0f, 0.0f };
	real_vector3d forward = { 1.0f, 0.0f, 0.0f };
	struct sound_definition *d;
	struct sound_pitch_range *group_range;
	long sound;

	(void)voice_tag;
	if (seconds)
		*seconds = 0.0f;
	if (!h2_sound_globals.loaded || !gestalt || sound_tag == NONE || unit_index == NONE || !h2_voice.count)
		return NONE;
	if (!h2_cache_tag_get(cache, sound_tag, &t) || t.group_tag != 'snd!' || !h2_gestalt_sound(gestalt, t.address, &snd) || snd.range_count < 1 ||
		!h2_gestalt_range(gestalt, snd.first_range, &range) || range.permutation_count < 1)
	{
		return NONE;
	}
	/* the permutation: another than the last within the sound's repetition
	delay (Halo 2's, minutes) */
	k = (long)(random_unit() * (double)range.permutation_count);
	if (k >= range.permutation_count)
		k = range.permutation_count - 1;
	if (range.permutation_count > 1 && last_permutation && *last_permutation == k && (last_time == NONE || now - last_time < repetition_ticks))
		k = (k + 1 + (long)(random_unit() * (double)(range.permutation_count - 1))) % range.permutation_count;
	/* the slot: the one bound to this already, else the oldest free one */
	for (i = 0; i < h2_voice.count; i++)
	{
		if (h2_voice.slots[i].permutation && h2_voice.slots[i].sound_tag == sound_tag && h2_voice.slots[i].sound_permutation == k)
		{
			slot = &h2_voice.slots[i];
			break;
		}
	}
	if (!slot)
	{
		for (i = 0; i < h2_voice.count; i++)
		{
			struct voice_slot *candidate = &h2_voice.slots[i];

			if (!candidate->permutation)
				continue;
			if (candidate->permutation->unknown0 != NONE && sound_cache_sound_in_use(candidate->permutation))
				continue;
			if (!slot || candidate->bound_time < slot->bound_time)
				slot = candidate;
		}
		if (!slot)
		{
			h2_voice.refused++;
			return NONE;
		}
		{
			struct source made[1];
			long made_count = 0, pool_count = 0, source;
			struct h2_sound_codec_chunk pool[H2_SOUND_MAXIMUM_CHUNKS];
			struct sound_permutation *p = slot->permutation;
			size_t length;
			char const *bare_name;

			if (!h2_gestalt_permutation(gestalt, range.first_permutation + k, 0, &permutation) || permutation.chunk_count > VOICE_SLOT_CHUNKS)
			{
				h2_voice.refused++;
				return NONE;
			}
			source = source_add(made, &made_count, pool, &pool_count, gestalt, &snd, &permutation, 1, H2_SOUND_OUTPUT_RATE, SHORT_PERMUTATION_BYTES);
			if (source == NONE)
			{
				h2_voice.refused++;
				if (HALO_ENV_ONCE("HALO_H2_VOICE_LOG"))
					platform_log("h2 sound: voice line %s: no source: %s", h2_cache_tag_name(cache, sound_tag), source_why);
				return NONE;
			}
			/* (its old samples let go of the sound cache) */
			if (p->unknown0 != NONE)
				sound_cache_sound_delete(p);
			i = slot - h2_voice.slots;
			slot->sound_tag = NONE;
			memcpy(&h2_voice.chunks[i * VOICE_SLOT_CHUNKS], pool, (size_t)pool_count * sizeof(pool[0]));
			made[0].first_chunk = (uint16_t)(i * VOICE_SLOT_CHUNKS);
			made[0].permutation = p;
			h2_voice.sources[i] = made[0];
			bare_name = h2_cache_tag_name(cache, sound_tag);
			length = strlen(bare_name);
			memset(p->name, 0, sizeof(p->name));
			strncpy(p->name, length > 28 ? bare_name + length - 28 : bare_name, sizeof(p->name) - 1);
			p->skip_fraction = 0.f;
			p->gain = 1.f;
			p->compression = H2_SOUND_PERMUTATION_COMPRESSION;
			p->next_permutation_index = NONE;
			p->sample_buffer_size = made[0].adpcm_bytes;
			memset(&p->samples, 0, sizeof(p->samples));
			p->samples.size = (long)made[0].raw_bytes;
			p->samples.pad = permutation_pad(&made[0]);
			p->samples.file_offset = (long)made[0].dat_offset;
			slot->sound_tag = sound_tag;
			slot->sound_permutation = (short)k;
			h2_voice.binds++;
		}
	}
	slot->bound_time = now;
	d = (struct sound_definition *)tag_get(SOUND_DEFINITION_TAG, slot->tag_index);
	if (d->pitch_ranges.count < 1)
		return NONE;
	group_range = TAG_BLOCK_GET_ELEMENT(&d->pitch_ranges, 0, struct sound_pitch_range);
	group_range->forced_permutation_index = slot->index;
	sound = object_impulse_sound_new(unit_index, slot->tag_index, 0, &origin, &forward, 1.f);
	if (sound == NONE)
	{
		group_range->forced_permutation_index = NONE;
		return NONE;
	}
	if (seconds)
	{
		struct source const *made = &h2_voice.sources[slot - h2_voice.slots];

		*seconds = made->rate ? (real)((double)made->frames / (double)made->rate) : 0.0f;
	}
	if (last_permutation)
		*last_permutation = (short)k;
	h2_voice.plays++;
	if (HALO_ENV_ONCE("HALO_H2_VOICE_LOG"))
		platform_log("h2 voice: %s permutation %ld in slot %ld (%s)", h2_cache_tag_name(cache, sound_tag), k, (long)(slot - h2_voice.slots),
			sound != NONE ? "playing" : "not heard");
	return sound;
}

/* A unit's pain or death sound: a Halo 2 Elite's (the unit's definition tag has "elite" in its name) is
the Elite's own voice, the sound the unit would have otherwise for all else. */
long h2_sound_voice(
	char const *unit_definition_name,
	short vocalization_type,
	long sound_definition_index)
{
	char const *name;
	long tag_index;

	if (!h2_sound_globals.loaded || !unit_definition_name || !strstr(unit_definition_name, "elite"))
		return sound_definition_index;
	/* (the vocalization types: 6-13 pains and screams, 14-19 deaths, 16 falling) */
	if (vocalization_type >= 6 && vocalization_type <= 13)
		name = "dialog\\combat\\elite_mp\\elite_pain";
	else if (vocalization_type == 16)
		name = "dialog\\combat\\elite_mp\\elite_deathfall";
	else if (vocalization_type >= 14 && vocalization_type <= 19)
		name = "dialog\\combat\\elite_mp\\elite_death";
	else
		return sound_definition_index;
	tag_index = h2_sound_vessel(name);
	return tag_index != NONE ? tag_index : sound_definition_index;
}

boolean h2_sound_table_replaces(
	char const *ce_name,
	boolean *dialogue)
{
	long r;

	if (dialogue)
		*dialogue = FALSE;
	if (!ce_name)
		return FALSE;
	for (r = 0; static_rows[r].ce || static_rows[r].h2; r++)
	{
		if (static_rows[r].kind == ROW_PLAIN && static_rows[r].ce && static_rows[r].h2 && !strcmp(static_rows[r].ce, ce_name))
		{
			if (dialogue)
				*dialogue = strstr(static_rows[r].h2, "dialog\\") != NULL;
			return TRUE;
		}
	}
	return FALSE;
}

boolean h2_sound_permutation_is_h2(
	struct sound_permutation const *permutation)
{
	return h2_sound_globals.loaded && permutation->compression == H2_SOUND_PERMUTATION_COMPRESSION;
}

unsigned long h2_sound_permutation_dat(
	struct sound_permutation const *permutation)
{
	return (unsigned long)permutation->samples.pad & 0xFF;
}

boolean h2_sound_stream(
	struct sound_permutation const *permutation,
	struct h2_sound_codec_stream *stream)
{
	long index;
	struct source const *s;

	if (!h2_sound_globals.loaded || permutation->samples.file_offset < 0 || permutation->samples.size < 0)
		return FALSE;
	index = source_find((unsigned long)permutation->samples.pad, (uint32_t)permutation->samples.file_offset,
		(uint32_t)permutation->samples.size);
	if (index == NONE)
	{
		/* (a voice slot's line) */
		s = voice_source_find((unsigned long)permutation->samples.pad, (uint32_t)permutation->samples.file_offset,
			(uint32_t)permutation->samples.size);
		if (!s)
			return FALSE;
		stream->chunks = h2_voice.chunks + s->first_chunk;
	}
	else
	{
		s = &h2_sound_globals.sources[index];
		stream->chunks = h2_sound_globals.chunks + s->first_chunk;
	}
	stream->kind = s->kind;
	stream->channels = s->channels;
	stream->rate = (long)s->rate;
	stream->frames = s->frames;
	stream->chunk_count = s->chunk_count;
	stream->first_frame = s->first_frame;
	return TRUE;
}

int h2_sound_read(
	unsigned long dat,
	unsigned long offset,
	unsigned long size,
	void *buffer)
{
	return dat_read(dat, offset, size, buffer);
}

boolean h2_sound_source_crc(
	unsigned long pad,
	unsigned long dat_offset,
	unsigned long raw_bytes,
	unsigned long *crc)
{
	long index = source_find(pad, (uint32_t)dat_offset, (uint32_t)raw_bytes);
	struct source const *s;
	unsigned char buffer[4096];
	uint32_t value = 0;
	long k;

	if (index == NONE)
		return FALSE;
	s = &h2_sound_globals.sources[index];
	for (k = 0; k < s->chunk_count; k++)
	{
		struct h2_sound_codec_chunk const *chunk = &h2_sound_globals.chunks[s->first_chunk + k];
		unsigned long done = 0;

		while (done < chunk->bytes)
		{
			unsigned long size = chunk->bytes - done < sizeof(buffer) ? chunk->bytes - done : sizeof(buffer);

			if (!dat_read(s->dat, chunk->offset + done, size, buffer))
				return FALSE;
			value = cache_file_crc32_update(value, buffer, (uint32_t)size);
			done += size;
		}
	}
	*crc = value;
	return TRUE;
}

long h2_sound_source_count(void)
{
	return h2_sound_globals.source_count;
}

boolean h2_sound_source_get(
	long index,
	struct h2_sound_source_info *info)
{
	struct source const *s;

	if (index < 0 || index >= h2_sound_globals.source_count)
		return FALSE;
	s = &h2_sound_globals.sources[index];
	info->dat = s->dat;
	info->pad = permutation_pad(s);
	info->dat_offset = s->dat_offset;
	info->raw_bytes = s->raw_bytes;
	info->adpcm_bytes = s->adpcm_bytes;
	info->channels = s->out_channels;
	info->rate = (long)s->out_rate;
	info->copied = h2_sound_codec_is_copy(s->kind, s->channels, (long)s->rate, (long)s->out_rate, s->out_channels);
	/* (a chain's links have no entry in the sound cache file: made as they
	load, as Custom Edition's are) */
	info->permutation = s->permutation;
	return TRUE;
}

boolean h2_sound_cache_source(
	char *name,
	long name_size,
	unsigned long *map_bytes,
	unsigned long *map_crc,
	unsigned long *dat_bytes)
{
	if (!h2_sound_globals.loaded || name_size < 1)
		return FALSE;
	snprintf(name, (size_t)name_size, "h2_%s", h2_sound_globals.map_name);
	*map_bytes = h2_sound_globals.map_bytes;
	*map_crc = h2_sound_globals.map_crc;
	*dat_bytes = h2_sound_globals.dat_bytes;
	return TRUE;
}

void *h2_sound_work(
	unsigned long *bytes)
{
	*bytes = h2_sound_globals.work_bytes;
	return h2_sound_globals.work;
}
