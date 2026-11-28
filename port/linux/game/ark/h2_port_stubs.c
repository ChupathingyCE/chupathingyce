/* H2_PORT_STUBS.C
	(port) Halo 2 mode's stand-ins for what its units call that is held back
	from this first step (PLAN-next lists each), and the counters h2_port.h
	declares. */

#include "cseries.h"
#include "h2_cache.h"
#include "h2_territories.h"

unsigned long halo_map_generation = 1;
volatile unsigned long halo_settings_generation = 1;

/* ---------- Halo 2's Territories (h2_territories.c, held back: it needs the
King variant's Territories fields, game_engine.h) */

unsigned char territories_active(void)
{
	return FALSE;
}

void h2_territories_map_loaded(struct h2_cache *cache, char const *map_name)
{
	(void)cache;
	(void)map_name;
}

void h2_territories_map_cleared(void)
{
}

long h2_territories_hud(long player_index, long *holders, long *contenders, real *fractions, long maximum, real *charge)
{
	(void)player_index; (void)holders; (void)contenders; (void)fractions; (void)maximum;
	if (charge)
		*charge = 0.0f;
	return 0;
}

long h2_territories_held(long player_index, real_point3d *centers, long maximum)
{
	(void)player_index; (void)centers; (void)maximum;
	return 0;
}

/* (render_epoch.h) this engine's one game thread */
int halo_thread_index(void)
{
	return 0;
}

/* (Ark's tools/map_validate.c and vita_main.c) a monotonic clock in
microseconds, for the level builder's timings */
struct h2_timespec
{
	long seconds;
	long nanoseconds;
};
int clock_gettime(int clock, struct h2_timespec *now);
unsigned long long vita_host_time_us(void)
{
	struct h2_timespec now;

	/* (CLOCK_MONOTONIC: 1 on Linux; the game's units see no POSIX names) */
	if (clock_gettime(1, &now))
		return 0;
	return (unsigned long long)now.seconds * 1000000ULL + (unsigned long long)(now.nanoseconds / 1000);
}

/* ---------- (Ark's engine) held back from this first step: no answer */

/* (Ark's Forge canvas, forge_canvas.inc: no mode layout here) */
boolean forge_canvas_item_removed(long equipment_index)
{
	(void)equipment_index;
	return FALSE;
}

boolean forge_canvas_item_due(long equipment_index)
{
	(void)equipment_index;
	return FALSE;
}

void forge_canvas_item_made(long equipment_index, long object_index)
{
	(void)equipment_index;
	(void)object_index;
}

/* (Ark's game_state_xbox.c: Halo 2 campaign saves in their own folder - the
campaign is a later step) */
boolean game_state_port_profile_directory(char *path)
{
	(void)path;
	return FALSE;
}

void game_state_port_discard_h2_save(void)
{
}

boolean game_state_set_persistent_storage_custom(boolean custom)
{
	(void)custom;
	return FALSE;
}

boolean game_state_persistent_storage_exists(void)
{
	return FALSE;
}

/* (Ark's player profiles: the Halo 2 emblem a profile keeps - not kept yet) */
boolean player_profile_emblem_get(long player_profile_index, byte *values)
{
	(void)player_profile_index;
	(void)values;
	return FALSE;
}

boolean player_profile_emblem_set(long player_profile_index, byte const *values)
{
	(void)player_profile_index;
	(void)values;
	return FALSE;
}

/* (Ark's scene_pack_ce.c: Custom Edition maps as scene packs - this mode builds
its kit from an Xbox map) */
boolean (*scene_pack_ce_mcc_sound_open)(unsigned long combined_offset, unsigned long bank_bytes, char *reason,
	long reason_size) = NULL;
void (*scene_pack_ce_mcc_sound_close)(void) = NULL;

char const *scene_pack_ce_tag_name(struct scene_pack_ce *ce, long source_index, char const *name)
{
	(void)ce;
	(void)source_index;
	return name;
}

/* (Ark's physical_memory_map.c) the large tag cache's relocation bits: this
engine has no large tag cache */
unsigned char *physical_memory_tag_cache_relocation_bits(void)
{
	return NULL;
}

/* (Ark's matrix_math.c) the parent's matrix times an orientation's */
#include "math/matrix_math.h"
void matrix4x3_multiply_orientation(real_matrix4x3 const *parent, real_orientation const *orientation,
	real_matrix4x3 *result)
{
	matrix4x3_from_orientation(result, orientation);
	matrix4x3_multiply(parent, result, result);
}

/* (glibc 2.33's mallinfo2, which the level builder's memory log asks for: the
portable build's glibc 2.31 has only mallinfo, whose counts are int) */
struct h2_port_mallinfo2 { size_t arena, ordblks, smblks, hblks, hblkhd, usmblks, fsmblks, uordblks, fordblks, keepcost; };
struct h2_port_mallinfo { int arena, ordblks, smblks, hblks, hblkhd, usmblks, fsmblks, uordblks, fordblks, keepcost; };
extern struct h2_port_mallinfo h2_port_mallinfo(void) __asm__("mallinfo");
struct h2_port_mallinfo2 h2_port_mallinfo2(void) __asm__("mallinfo2");
struct h2_port_mallinfo2 h2_port_mallinfo2(void)
{
	struct h2_port_mallinfo old = h2_port_mallinfo();
	struct h2_port_mallinfo2 info;

	info.arena = (unsigned)old.arena;
	info.ordblks = (unsigned)old.ordblks;
	info.smblks = (unsigned)old.smblks;
	info.hblks = (unsigned)old.hblks;
	info.hblkhd = (unsigned)old.hblkhd;
	info.usmblks = (unsigned)old.usmblks;
	info.fsmblks = (unsigned)old.fsmblks;
	info.uordblks = (unsigned)old.uordblks;
	info.fordblks = (unsigned)old.fordblks;
	info.keepcost = (unsigned)old.keepcost;
	return info;
}

/* (Ark's Vita host) pins a builder thread to a core: nothing to do here */
void vita_host_pin_current_thread(int core)
{
	(void)core;
}
