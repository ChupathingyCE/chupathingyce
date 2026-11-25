/*
DEVELOPER_FLAMES.C

A blue flame on a verified Chupathingy developer's helmet, as the Halo 2
flaming helmets were for Bungie's staff (display.developer_flames, on by
default; a viewer can turn it off, and a developer can turn their own off on
the site). Who has one is profile_names_developer's: a card this copy
checked from a bound proof, of this build's own network, its player shown by their verified
name, whose device certificate carries the developer mark
(docs/delta/link.md, "Device identity": account marks). The rules are
port/linux/src/developer_flames_rules.h.

Looks only, by construction: nothing ships, and nothing of it is game state.
The flame is built at run time from the map's own flame sprite, found as the
map's tags load (effects\particles\flash\bitmaps\burning flame, else ...\fire
cloud; a map with neither shows none, said once), drawn from a copy of its
bitmaps whose pixels are put on a blue ramp as the texture cache loads them
(developer_flames_bitmap_loaded), so the map's own fire stays orange. A
sprite whose format has no color of its own (a luminance format) is tinted
blue instead. The flame's sprites are this file's own, not the game's
particles (which are game state): moved and drawn from the render with
their own random numbers, after the map's particles, at the biped's head
marker. Never in the wearer's own first-person view, on a camouflaged or
invisible player, on a dead player's body, or far away; MCC maps are left
out. At most DEVELOPER_FLAMES_PER_HELMET sprites a helmet and
DEVELOPER_FLAMES_MOST in all, and none start while the game's particles are
past three quarters of display.particle_limit.

Nothing reaches the simulation, saved games, films or the network: OpenCE
players and older builds never agree to verified names, so there is no card
to draw a flame from. The consoles leave it out (HALO_DEVELOPER_FLAMES,
halo_console_features.h): they don't offer verified names yet.
*/

#include "cseries.h"

#ifdef HALO_DEVELOPER_FLAMES

#include "bitmaps/bitmap_group.h"
#include "cache/texture_cache.h"
#include "effects/particles.h"
#include "game/players.h"
#include "math/real_math.h"
#include "memory/data.h"
#include "objects/objects.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "render/render_sprite.h"
#include "shaders/shader_definitions.h"
#include "tag_files/tag_groups.h"
#include "units/units.h"
#include "../src/developer_flames_rules.h"
#include "halo_profile_screen.h"
#include "mcc_cache.h"

#include <string.h>

#ifdef HALO_IDENTITY_TEST_KEY
#include "../src/identity.h"
#include "../src/identity_key.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
/* (the test's file is the test's, by host path: not one under the data
root, where port/linux/include/stdio.h's fopen puts every path) */
#ifdef fopen
#undef fopen
#endif
#endif

/* the platform layer's */
void platform_log(char const *format, ...);
int config_boolean(char const *name);
long config_integer(char const *name);
unsigned long system_milliseconds(void);
/* render_particles.c's */
boolean local_player_is_first_person(short local_player_index);

void developer_flames_tags_loaded(void);
void developer_flames_tags_unloaded(void);
void developer_flames_render(void);
void developer_flames_bitmap_loaded(struct bitmap_data *bitmap);

/* ---------- constants */

enum
{
	/* a sprite sheet's bitmaps this file copies at most */
	MAXIMUM_FLAME_BITMAPS = 32,
	/* who has a flame is looked at again this often (milliseconds) */
	WEARERS_CHECK_GAP = 250,
	/* the fewest free transparent groups and vertex buffers left to the
	map's effects drawn after these */
	FREE_GROUPS_KEPT = 16,
};

/* each helmet's: sprites started a second, their life (seconds), their
size (world units across), how fast they rise (world units a second), and
how far above the head marker they start */
#define FLAME_RATE 20.0f
#define FLAME_LIFE_SHORTEST 0.4f
#define FLAME_LIFE_LONGEST 0.7f
#define FLAME_SIZE_SMALLEST 0.10f
#define FLAME_SIZE_LARGEST 0.17f
#define FLAME_RISE_SLOWEST 0.12f
#define FLAME_RISE_FASTEST 0.24f
#define FLAME_ABOVE_HEAD 0.025f
#define FLAME_SPREAD 0.025f
/* (the sprite's frames a second, and how far into its life it fades in) */
#define FLAME_FRAMES_PER_SECOND 15.0f
#define FLAME_FADE_IN 0.12f
/* (a bitmap whose pixels the texture cache loads from the map: the
bitmaps' _bitmap_cached_bit, xbox_texture_cache.c) */
#define FLAME_BITMAP_CACHED_BIT 7
/* (camouflage drawn past this hides the flame) */
#define FLAME_CAMOUFLAGE 0.05f

/* the map's flame sprites, the first found */
static char const *const flame_bitmap_names[] =
{
	"effects\\particles\\flash\\bitmaps\\burning flame",
	"effects\\particles\\flash\\bitmaps\\fire cloud",
};

/* ---------- structures */

struct developer_flame
{
	boolean live;
	short wearer;
	short sequence_index;
	short first_sprite;
	real age;
	real life;
	real size;
	real rotation;
	real spin;
	/* from the head marker, as it moves */
	real_vector3d offset;
	real_vector3d velocity;
};

struct developer_flames_wearer_state
{
	/* the player (its datum index), NONE: a free slot */
	long player_index;
	long unit_index;
	boolean has_head;
	boolean alive;
	boolean drawn;
	boolean camouflaged;
	real_point3d head;
	real owed;
	short live;
};

/* ---------- globals */

/* the flame's bitmap group in this map (NONE: none, or an MCC map) */
static long flame_group_index = NONE;
static struct
{
	long bitmap_count;
	struct bitmap_data *bitmaps;
	/* blue: the copies drawn in its place, their pixels on the blue ramp;
	else the map's own, tinted */
	boolean blue;
	struct bitmap_data copies[MAXIMUM_FLAME_BITMAPS];
	/* the sequences with sprites */
	short sequence_count;
	short sequences[8];
} flame_art;

static struct developer_flame flames[DEVELOPER_FLAMES_MOST];
static short flames_live;
static struct developer_flames_wearer_state wearers[DEVELOPER_FLAMES_WEARERS];
static struct shader_effect_definition flame_shader;
static unsigned long flames_random_state = 0x9E3779B9UL;
static long flames_frame = NONE;
static unsigned long flames_time;
static unsigned long wearers_checked;
static boolean wearers_checked_once;

/* ---------- private code */

/* the flames' own random numbers (never the game's: no seed the
simulation reads changes) */
static real flames_random(
	real lower,
	real upper)
{
	flames_random_state ^= flames_random_state << 13;
	flames_random_state ^= flames_random_state >> 17;
	flames_random_state ^= flames_random_state << 5;
	flames_random_state &= 0xFFFFFFFFUL;
	return lower + (upper - lower) * (real)(flames_random_state >> 8) / (real)0x1000000;
}

static boolean flame_format_recolorable(
	short format)
{
	switch (format)
	{
	case DEVELOPER_FLAMES_R5G6B5:
	case DEVELOPER_FLAMES_A1R5G5B5:
	case DEVELOPER_FLAMES_A4R4G4B4:
	case DEVELOPER_FLAMES_X8R8G8B8:
	case DEVELOPER_FLAMES_A8R8G8B8:
	case DEVELOPER_FLAMES_DXT1:
	case DEVELOPER_FLAMES_DXT3:
	case DEVELOPER_FLAMES_DXT5:
		return TRUE;
	default:
		return FALSE;
	}
}

static void flames_clear(
	void)
{
	short index;

	memset(flames, 0, sizeof(flames));
	flames_live = 0;
	for (index = 0; index < DEVELOPER_FLAMES_WEARERS; index++)
	{
		memset(&wearers[index], 0, sizeof(wearers[index]));
		wearers[index].player_index = NONE;
		wearers[index].unit_index = NONE;
	}
	wearers_checked_once = FALSE;
}

#ifdef HALO_IDENTITY_TEST_KEY
/* a test's build alone (identity_key.h's root keys a test's: never the real
keys): HALO_DEVELOPER_FLAMES_TEST names, for each of this machine's own
players in turn (split screen), split by ";", a file of a test-signed issuer
certificate and device certificate, in hex, a line each, or nothing for
none. A player whose card checks against this build's root keys and carries
the developer mark has a flame, in any game, by the same rules as a card
from a game (developer_flames_card_shows), so its drawing can be seen; one
that doesn't check, or has no mark, has none. Read once */
static boolean flames_test_card(
	short local_player_index)
{
	static int known[MAXIMUM_LOCAL_PLAYERS] = { -1, -1, -1, -1 };
	char const *list = getenv("HALO_DEVELOPER_FLAMES_TEST");
	char path[512], line[2][1024];
	unsigned char issuer_data[IDENTITY_ISSUER_SIZE], device_data[IDENTITY_DEVICE_MOST];
	struct identity_issuer issuer;
	struct identity_device device;
	struct developer_flames_card card;
	FILE *file;
	int length[2], index;
	size_t path_length;
	unsigned long now = (unsigned long)time(NULL);

	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS)
		return FALSE;
	if (known[local_player_index] >= 0)
		return known[local_player_index];
	known[local_player_index] = 0;
	for (index = 0; list && index < local_player_index; index++)
	{
		list = strchr(list, ';');
		if (list)
			list++;
	}
	if (!list)
		return FALSE;
	path_length = strcspn(list, ";");
	if (!path_length || path_length >= sizeof(path))
		return FALSE;
	memcpy(path, list, path_length);
	path[path_length] = 0;
	if (!(file = fopen(path, "r")))
		return FALSE;
	for (index = 0; index < 2; index++)
	{
		if (!fgets(line[index], sizeof(line[index]), file))
			line[index][0] = 0;
		length[index] = (int)strcspn(line[index], "\r\n");
	}
	fclose(file);
	if (length[0] != 2 * IDENTITY_ISSUER_SIZE || length[1] % 2 || length[1] / 2 > IDENTITY_DEVICE_MOST ||
		!identity_unhex(line[0], length[0], issuer_data, IDENTITY_ISSUER_SIZE) ||
		!identity_unhex(line[1], length[1], device_data, length[1] / 2) ||
		identity_check_issuer(issuer_data, IDENTITY_ISSUER_SIZE,
			(const unsigned char (*)[IDENTITY_KEY_SIZE])identity_root_keys, IDENTITY_ROOT_KEY_COUNT, now, &issuer) !=
			IDENTITY_OK ||
		identity_check_device(device_data, length[1] / 2, &issuer, now, &device) != IDENTITY_OK)
	{
		platform_log("developer flames: local player %d's test card doesn't check against this build's root keys: "
			"no flame", local_player_index);
		return FALSE;
	}
	memset(&card, 0, sizeof(card));
	card.option_on = config_boolean("display.developer_flames");
	card.names_on = config_boolean("display.profile_names");
	card.on_delta = card.checked = card.name_shown = card.bound = 1;
	card.capabilities = device.capabilities;
	known[local_player_index] = developer_flames_card_shows(&card);
	platform_log("developer flames: local player %d's test card checks (capabilities 0x%08lx): %s",
		local_player_index, device.capabilities, known[local_player_index] ? "a flame" : "no flame");
	return known[local_player_index];
}
#endif

/* the wearers: the players profile_names_developer gives a flame, looked at
again every WEARERS_CHECK_GAP; one no longer a wearer loses their flames */
static void flames_check_wearers(
	void)
{
	long found[DEVELOPER_FLAMES_WEARERS];
	short found_count = 0, index;
	long player_index;

	for (player_index = data_next_index(player_data, NONE);
		player_index != NONE && found_count < DEVELOPER_FLAMES_WEARERS;
		player_index = data_next_index(player_data, player_index))
	{
		if (profile_names_developer(DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index)))
			found[found_count++] = player_index;
#ifdef HALO_IDENTITY_TEST_KEY
		else if (flames_test_card(player_get(player_index)->local_player_index))
			found[found_count++] = player_index;
#endif
	}
	for (index = 0; index < DEVELOPER_FLAMES_WEARERS; index++)
	{
		short other;
		boolean kept = FALSE;

		for (other = 0; other < found_count; other++)
		{
			if (found[other] == wearers[index].player_index)
			{
				kept = TRUE;
				found[other] = NONE;
			}
		}
		if (!kept && wearers[index].player_index != NONE)
		{
			short flame;

			for (flame = 0; flame < DEVELOPER_FLAMES_MOST; flame++)
			{
				if (flames[flame].live && flames[flame].wearer == index)
				{
					flames[flame].live = FALSE;
					flames_live--;
				}
			}
			memset(&wearers[index], 0, sizeof(wearers[index]));
			wearers[index].player_index = NONE;
			wearers[index].unit_index = NONE;
		}
	}
	for (index = 0; index < found_count; index++)
	{
		short slot;

		if (found[index] == NONE)
			continue;
		for (slot = 0; slot < DEVELOPER_FLAMES_WEARERS && wearers[slot].player_index != NONE; slot++)
			;
		if (slot < DEVELOPER_FLAMES_WEARERS)
		{
			memset(&wearers[slot], 0, sizeof(wearers[slot]));
			wearers[slot].player_index = found[index];
			wearers[slot].unit_index = NONE;
		}
	}
}

/* where a wearer's head is now, and whether their flame may show at all
(alive, drawn, not camouflaged) */
static void flames_place_wearer(
	struct developer_flames_wearer_state *wearer)
{
	struct player_datum *player = player_try_and_get(wearer->player_index);
	long unit_index = player ? player->unit_index : NONE;
	struct object_marker marker;

	wearer->unit_index = unit_index;
	wearer->has_head = FALSE;
	wearer->alive = FALSE;
	wearer->drawn = FALSE;
	wearer->camouflaged = FALSE;
	if (unit_index == NONE || !object_try_and_get(unit_index))
		return;
	{
		struct unit_datum *unit = unit_get(unit_index);
		real super_amount = 0.0f;

		wearer->alive = !TEST_FLAG(unit->object.damage_flags, _object_dead_bit);
		wearer->drawn = !TEST_FLAG(unit->object.flags, _object_invisible_bit);
		wearer->camouflaged = unit_get_drawn_camouflage(unit_index, &super_amount) > FLAME_CAMOUFLAGE;
	}
	/* (a custom biped without a head marker: no flame) */
	if (object_get_marker_by_name(unit_index, "head", &marker, 1) > 0)
	{
		wearer->has_head = TRUE;
		wearer->head = marker.matrix.position;
	}
}

static void flames_spawn(
	short wearer_index)
{
	short index;

	if (!flame_art.sequence_count)
		return;
	for (index = 0; index < DEVELOPER_FLAMES_MOST && flames[index].live; index++)
		;
	if (index == DEVELOPER_FLAMES_MOST)
		return;
	{
		struct developer_flame *flame = &flames[index];
		struct bitmap_group *group = bitmap_group_get(flame_group_index);
		struct bitmap_group_sequence *sequence;

		flame->live = TRUE;
		flame->wearer = wearer_index;
		flame->sequence_index = flame_art.sequences[(short)flames_random(0.0f, (real)flame_art.sequence_count - 0.001f)];
		sequence = TAG_BLOCK_GET_ELEMENT(&group->sequences, flame->sequence_index, struct bitmap_group_sequence);
		flame->first_sprite = (short)flames_random(0.0f, (real)sequence->sprites.count - 0.001f);
		flame->age = 0.0f;
		flame->life = flames_random(FLAME_LIFE_SHORTEST, FLAME_LIFE_LONGEST);
		flame->size = flames_random(FLAME_SIZE_SMALLEST, FLAME_SIZE_LARGEST);
		flame->rotation = flames_random(-0.4f, 0.4f);
		flame->spin = flames_random(-0.8f, 0.8f);
		flame->offset.i = flames_random(-FLAME_SPREAD, FLAME_SPREAD);
		flame->offset.j = flames_random(-FLAME_SPREAD, FLAME_SPREAD);
		flame->offset.k = FLAME_ABOVE_HEAD + flames_random(0.0f, FLAME_SPREAD);
		flame->velocity.i = flames_random(-0.03f, 0.03f);
		flame->velocity.j = flames_random(-0.03f, 0.03f);
		flame->velocity.k = flames_random(FLAME_RISE_SLOWEST, FLAME_RISE_FASTEST);
		flames_live++;
		wearers[wearer_index].live++;
	}
}

/* once a frame (not each view of a split screen): the wearers looked at,
the sprites aged and moved, and new ones started */
static void flames_update(
	void)
{
	unsigned long now = system_milliseconds();
	real dt;
	long pool_limit, pool_live;
	short index;

	if (render.frame_index == flames_frame)
		return;
	flames_frame = render.frame_index;
	dt = flames_time ? (real)(now - flames_time) / 1000.0f : 0.0f;
	flames_time = now;
	if (!(dt >= 0.0f) || dt > 0.1f)
		dt = 0.1f;
	if (!wearers_checked_once || now - wearers_checked >= WEARERS_CHECK_GAP)
	{
		wearers_checked = now;
		wearers_checked_once = TRUE;
		flames_check_wearers();
	}
	for (index = 0; index < DEVELOPER_FLAMES_WEARERS; index++)
	{
		wearers[index].live = 0;
		if (wearers[index].player_index != NONE)
			flames_place_wearer(&wearers[index]);
	}
	for (index = 0; index < DEVELOPER_FLAMES_MOST; index++)
	{
		struct developer_flame *flame = &flames[index];

		if (!flame->live)
			continue;
		flame->age += dt;
		if (flame->age >= flame->life || wearers[flame->wearer].player_index == NONE)
		{
			flame->live = FALSE;
			flames_live--;
			continue;
		}
		flame->offset.i += flame->velocity.i * dt;
		flame->offset.j += flame->velocity.j * dt;
		flame->offset.k += flame->velocity.k * dt;
		flame->rotation += flame->spin * dt;
		wearers[flame->wearer].live++;
	}
	/* (the game's particles, and their limit: 0 is the pool's) */
	pool_limit = config_integer("display.particle_limit");
	if (pool_limit <= 0)
		pool_limit = particle_data ? particle_data->maximum_count : 0;
	pool_live = particle_data ? particle_data->actual_count : 0;
	for (index = 0; index < DEVELOPER_FLAMES_WEARERS; index++)
	{
		struct developer_flames_wearer_state *wearer = &wearers[index];

		if (wearer->player_index == NONE || !wearer->has_head || !wearer->alive || !wearer->drawn ||
			wearer->camouflaged)
		{
			wearer->owed = 0.0f;
			continue;
		}
		wearer->owed += dt * FLAME_RATE;
		while (wearer->owed >= 1.0f)
		{
			wearer->owed -= 1.0f;
			if (!developer_flames_may_spawn(wearer->live, flames_live, pool_live, pool_limit))
			{
				wearer->owed = 0.0f;
				break;
			}
			flames_spawn(index);
		}
	}
}

/* the unit this view's player looks out of in first person (NONE: none) */
static long flames_view_unit(
	void)
{
	short local_player_index = render.local_player_index;
	long player_index;
	struct player_datum *player;

	if (local_player_index == NONE || !local_player_is_first_person(local_player_index))
		return NONE;
	player_index = local_player_get_player_index(local_player_index);
	player = player_index == NONE ? NULL : player_try_and_get(player_index);
	return player ? player->unit_index : NONE;
}

/* a sprite's scale for a flame size across (build_sprite multiplies the
sprite's share of its bitmap by the bitmap's width) */
static real flames_sprite_scale(
	struct bitmap_group *group,
	short sequence_index,
	short sprite_index,
	real size)
{
	struct bitmap_group_sequence *sequence = TAG_BLOCK_GET_ELEMENT(&group->sequences, sequence_index,
		struct bitmap_group_sequence);
	struct bitmap_group_sprite *sprite = TAG_BLOCK_GET_ELEMENT(&sequence->sprites, sprite_index,
		struct bitmap_group_sprite);
	struct bitmap_data *bitmap;
	real across;

	if (sprite->bitmap_index < 0 || sprite->bitmap_index >= group->bitmaps.count)
		return 0.0f;
	bitmap = TAG_BLOCK_GET_ELEMENT(&group->bitmaps, sprite->bitmap_index, struct bitmap_data);
	across = (sprite->bounds.x1 - sprite->bounds.x0) * (real)bitmap->width;
	return across > 0.0f ? size / across : 0.0f;
}

/* ---------- public code */

/* the map's flame sprite, as its tags load (cache_files.c) */
void developer_flames_tags_loaded(
	void)
{
	short name_index;
	long group_index = NONE;

	flames_clear();
	flame_group_index = NONE;
	flame_art.bitmap_count = 0;
	flame_art.bitmaps = NULL;
	flame_art.blue = FALSE;
	flame_art.sequence_count = 0;
	flame_shader = global_shader_effect_additive;
	/* (an MCC map's particles are its own: mcc_particles.c) */
	if (mcc_cache_tags_loaded())
		return;
	for (name_index = 0; name_index < (short)NUMBEROF(flame_bitmap_names) && group_index == NONE; name_index++)
		group_index = tag_loaded(BITMAP_GROUP_TAG, flame_bitmap_names[name_index]);
	if (group_index == NONE)
	{
		platform_log("developer flames: this map has no flame sprite; no flames");
		return;
	}
	{
		struct bitmap_group *group = bitmap_group_get(group_index);
		long index;
		boolean recolorable = group->bitmaps.count > 0 && group->bitmaps.count <= MAXIMUM_FLAME_BITMAPS;

		for (index = 0; index < group->sequences.count && flame_art.sequence_count < (short)NUMBEROF(flame_art.sequences);
			index++)
		{
			struct bitmap_group_sequence *sequence = TAG_BLOCK_GET_ELEMENT(&group->sequences, index,
				struct bitmap_group_sequence);

			if (sequence->first_bitmap_index != NONE && sequence->sprites.count > 0)
				flame_art.sequences[flame_art.sequence_count++] = (short)index;
		}
		if (!flame_art.sequence_count)
		{
			platform_log("developer flames: %s has no sprites; no flames", flame_bitmap_names[name_index - 1]);
			return;
		}
		flame_art.bitmaps = TAG_BLOCK_GET_ELEMENT(&group->bitmaps, 0, struct bitmap_data);
		flame_art.bitmap_count = group->bitmaps.count;
		for (index = 0; recolorable && index < group->bitmaps.count; index++)
		{
			struct bitmap_data const *bitmap = &flame_art.bitmaps[index];

			/* (a copy the texture cache loads on its own, from the map: the
			bitmap's pixels must be the cache's, not the tag's) */
			recolorable = TEST_FLAG(bitmap->flags, FLAME_BITMAP_CACHED_BIT) && flame_format_recolorable(bitmap->format);
		}
		if (recolorable)
		{
			for (index = 0; index < group->bitmaps.count; index++)
			{
				flame_art.copies[index] = flame_art.bitmaps[index];
				flame_art.copies[index].cache_block_index = NONE;
				flame_art.copies[index].base_address = XBOX_NULL;
			}
		}
		flame_art.blue = recolorable;
		flame_group_index = group_index;
		platform_log("developer flames: %s, %ld bitmaps, %s", flame_bitmap_names[name_index - 1],
			(long)group->bitmaps.count, recolorable ? "a blue copy" : "tinted blue");
	}
}

void developer_flames_tags_unloaded(
	void)
{
	flames_clear();
	flame_group_index = NONE;
	flame_art.bitmap_count = 0;
	flame_art.bitmaps = NULL;
	flame_art.blue = FALSE;
	flame_art.sequence_count = 0;
}

/* the texture cache's, once each time a bitmap's pixels are loaded and
before they are drawn: the blue copies' put on the blue ramp */
void developer_flames_bitmap_loaded(
	struct bitmap_data *bitmap)
{
	if (!flame_art.blue || bitmap < flame_art.copies || bitmap >= flame_art.copies + flame_art.bitmap_count)
		return;
	if (!developer_flames_recolor(bitmap->format, (unsigned char *)xbox_pointer(bitmap->base_address),
		bitmap->pixels_size))
	{
		platform_log("developer flames: bitmap %ld's pixels (format %d, %ld bytes) can't be made blue",
			(long)(bitmap - flame_art.copies), (int)bitmap->format, (long)bitmap->pixels_size);
	}
}

/* each view's, after the map's particles (render.c) */
void developer_flames_render(
	void)
{
	struct build_sprite_data sprite_data;
	struct bitmap_group *group;
	real view_fade[DEVELOPER_FLAMES_WEARERS];
	long view_unit;
	short index, shown = 0;
	real_vector3d up = { 0.0f, 0.0f, 1.0f };

	if (flame_group_index == NONE)
		return;
	flames_update();
	if (!flames_live)
		return;
	view_unit = flames_view_unit();
	for (index = 0; index < DEVELOPER_FLAMES_WEARERS; index++)
	{
		struct developer_flames_wearer_state const *state = &wearers[index];
		struct developer_flames_wearer wearer;

		wearer.has_head = state->player_index != NONE && state->has_head;
		wearer.alive = state->alive;
		wearer.drawn = state->drawn;
		wearer.camouflaged = state->camouflaged;
		wearer.own_first_person = state->unit_index != NONE && state->unit_index == view_unit;
		wearer.distance = wearer.has_head ? (float)distance3d(&render.camera.position, &state->head) : -1.0f;
		view_fade[index] = developer_flames_wearer_fade(&wearer);
		/* (behind the camera: nothing to draw) */
		if (view_fade[index] > 0.0f)
		{
			real_vector3d toward;

			vector_from_points3d(&render.camera.position, &state->head, &toward);
			if (dot_product3d(&toward, &render.camera.forward) < -0.5f)
				view_fade[index] = 0.0f;
		}
	}
	for (index = 0; index < DEVELOPER_FLAMES_MOST; index++)
	{
		if (flames[index].live && view_fade[flames[index].wearer] > 0.0f)
			shown++;
	}
	if (!shown || rasterizer_transparent_geometry_free_group_count() < FREE_GROUPS_KEPT ||
		rasterizer_dynamic_vertex_buffer_free_count() < FREE_GROUPS_KEPT)
	{
		return;
	}
	/* (the blue copies loaded, and made blue, before they are drawn) */
	if (flame_art.blue)
	{
		for (index = 0; index < flame_art.bitmap_count; index++)
			_texture_cache_bitmap_get_hardware_format(&flame_art.copies[index], TRUE, TRUE);
	}
	group = bitmap_group_get(flame_group_index);
	build_sprites_begin(&sprite_data, shown, flame_group_index, &flame_shader, 0);
	for (index = 0; index < DEVELOPER_FLAMES_MOST; index++)
	{
		struct developer_flame const *flame = &flames[index];
		struct bitmap_group_sequence *sequence;
		real fade, scale, life_left;
		real_argb_color color;
		real_point3d position;
		short sprite_index;

		if (!flame->live || view_fade[flame->wearer] <= 0.0f)
			continue;
		sequence = TAG_BLOCK_GET_ELEMENT(&group->sequences, flame->sequence_index, struct bitmap_group_sequence);
		sprite_index = (short)((flame->first_sprite + (long)(flame->age * FLAME_FRAMES_PER_SECOND)) %
			sequence->sprites.count);
		fade = view_fade[flame->wearer];
		if (flame->age < FLAME_FADE_IN)
			fade *= flame->age / FLAME_FADE_IN;
		life_left = (flame->life - flame->age) / flame->life;
		if (life_left < 0.5f)
			fade *= life_left / 0.5f;
		/* (it shrinks a little as it rises) */
		scale = flames_sprite_scale(group, flame->sequence_index, sprite_index,
			flame->size * (0.7f + 0.3f * life_left));
		if (scale <= 0.0f || fade <= 0.0f)
			continue;
		position.x = wearers[flame->wearer].head.x + flame->offset.i;
		position.y = wearers[flame->wearer].head.y + flame->offset.j;
		position.z = wearers[flame->wearer].head.z + flame->offset.k;
		color.alpha = 1.0f;
		if (flame_art.blue)
		{
			color.red = color.green = color.blue = fade;
		}
		else
		{
			color.red = DEVELOPER_FLAMES_TINT_R * fade;
			color.green = DEVELOPER_FLAMES_TINT_G * fade;
			color.blue = DEVELOPER_FLAMES_TINT_B * fade;
		}
		build_sprite(&sprite_data, 0, flame->sequence_index, sprite_index, &position, &up, flame->rotation, scale,
			&color, fade, 0);
	}
	/* (nothing built: nothing to draw, and the builder holds nothing) */
	if (!sprite_data.sprite_count && !sprite_data.group_count)
		return;
	/* (drawn from the blue copies, not the map's own bitmaps) */
	if (flame_art.blue)
	{
		for (index = 0; index < sprite_data.group_count; index++)
		{
			long bitmap_index = (long)(sprite_data.groups[index].bitmap - flame_art.bitmaps);

			if (bitmap_index >= 0 && bitmap_index < flame_art.bitmap_count)
				sprite_data.groups[index].bitmap = &flame_art.copies[bitmap_index];
		}
	}
	build_sprites_end(&sprite_data);
}

#endif /* HALO_DEVELOPER_FLAMES */
