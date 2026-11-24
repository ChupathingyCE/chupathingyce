#include "harness.h"

#include <fenv.h>

/* effect_update's split screen share of an effect's particles (its count
past MAXIMUM_SPLIT_SCREEN_PARTICLE_COUNT divided among the local players):
with no local player (a dedicated server, a network client before its
player is added) nothing may be divided by 0. What the division and the
conversion of its infinity to a byte give differs by CPU (0 on x86, 255 on
ARM and PowerPC), so the case checks the floating point flags instead. */

#include "config.inc"
struct effect_datum { byte particle_counts[8]; };
static short local_players;
static short local_player_count(void) { return local_players; }

static int share(byte count)
{
	struct effect_datum effect_storage = { { 0 } };
	struct effect_datum *effect = &effect_storage;
	long particle_index = 3;

	effect->particle_counts[particle_index] = count;
	feclearexcept(FE_ALL_EXCEPT);
#include "under_test.inc"
	CHECK(!fetestexcept(FE_DIVBYZERO | FE_INVALID), "a division by 0 or an invalid conversion (%d local players)",
		local_players);
	return effect->particle_counts[particle_index];
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	int got;

	CASE("one")
	{
		local_players = 1;
		got = share(20);
		CHECK(got == 20, "one local player has every particle: %d", got);
		return 0;
	}
	CASE("two")
	{
		local_players = 2;
		got = share(20);
		CHECK(got == 13, "two share those past %d: %d", MAXIMUM_SPLIT_SCREEN_PARTICLE_COUNT, got);
		return 0;
	}
	CASE("none")
	{
		local_players = 0;
		got = share(20);
		CHECK(got == 0, "no local player, no particles: %d", got);
		return 0;
	}
	CASE("few")
	{
		local_players = 0;
		got = share(4);
		CHECK(got == 4, "a count under the share is kept: %d", got);
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
