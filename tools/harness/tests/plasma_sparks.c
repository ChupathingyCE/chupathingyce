/* A fake particle and its bitmap; the production frame-time update of a
particle (particles.c: its first update's time, display.plasma_sparks, then
particle_update_frame_time's frames and sequences) below. The case is the
plasma grenade's bolts: one sequence of one sprite at 30 frames a second. */
#include "harness.h"
#include <float.h>

typedef unsigned short word;
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define PIN(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define TICKS_PER_SECOND 30
#include "types.inc"

struct tag_block { long count; void *address; };
struct bitmap_group_sequence { long sprite_count; struct tag_block sprites; };
struct bitmap_group { struct tag_block sequences; };
struct reference { long index; };
struct particle_definition
{
	unsigned long flags;
	struct reference bitmap;
	short first_sequence_index, initial_sequence_count, looping_sequence_count, final_sequence_count;
};
struct particle_datum
{
	word flags;
	long definition_index;
	short state, sequence_index, frame_index;
	real age, lifespan, frame_span, frame_time;
};

static struct bitmap_group_sequence sequences[4];
static struct bitmap_group the_bitmap = { { 4, sequences } };
static struct particle_definition the_definition;
static struct particle_datum the_particle;
static boolean dead;

#define particle_get(index) (&the_particle)
#define particle_definition_get(index) (&the_definition)
#define bitmap_group_get(index) (&the_bitmap)
#define local_random_range(lower, upper) (lower)
#define TAG_BLOCK_GET_ELEMENT(block, index, type) (&((type *)(block)->address)[index])
#define particle_die(index) (dead = TRUE)

static boolean particles_plasma_sparks;

#include "under_test.inc"

/* a bolt made in a tick, as particle_new leaves it (frame -1, forward) */
static void make(void)
{
	int index;

	for (index = 0; index < 4; index++)
		sequences[index].sprites.count = 1;
	memset(&the_definition, 0, sizeof(the_definition));
	the_definition.initial_sequence_count = 1;
	memset(&the_particle, 0, sizeof(the_particle));
	the_particle.lifespan = 1.0f;
	the_particle.frame_span = 1.0f / 30.0f;
	the_particle.frame_time = -1.0f;
	the_particle.state = _particle_state_next_sequence_initial;
	dead = FALSE;
	particle_next_sequence(0);
	the_particle.frame_index = NONE;
}

/* how many frames (updates at the frame rate) it is drawn: as particles_update
updates it, then the frame draws what is left */
static int frames_drawn(real frame_rate)
{
	real dt = 1.0f / frame_rate;
	int frames = 0;
	boolean first = TRUE;

	make();
	while (!dead && frames < 1000)
	{
		the_particle.age += dt;
		particle_update_frame_time(0, first ? particle_first_frame_time(&the_definition, dt) : dt);
		first = FALSE;
		if (!dead)
			frames++;
	}
	return frames;
}

int main(int argc, char **argv)
{
	const char *case_name = argv[1];

	CASE("xbox")
	{
		static const real rates[] = { 30.0f, 60.0f, 120.0f, 144.0f, 240.0f };
		int index;

		particles_plasma_sparks = FALSE;
		for (index = 0; index < 5; index++)
			CHECK(frames_drawn(rates[index]) == 0, "off: drawn %d frames at %g a second", frames_drawn(rates[index]),
				(double)rates[index]);
	}
	CASE("sparks")
	{
		particles_plasma_sparks = TRUE;
		CHECK(frames_drawn(30.0f) == 1, "on: drawn %d frames at 30 a second", frames_drawn(30.0f));
		CHECK(frames_drawn(60.0f) >= 2, "on: drawn %d frames at 60 a second", frames_drawn(60.0f));
		CHECK(frames_drawn(144.0f) >= 4, "on: drawn %d frames at 144 a second", frames_drawn(144.0f));
	}
	CASE("looping")
	{
		/* a particle that loops is left as it was, either way */
		particles_plasma_sparks = FALSE;
		make();
		the_definition.looping_sequence_count = 1;
		CHECK(particle_first_frame_time(&the_definition, 1.0f / 144.0f) == 1.0f / 144.0f, "a looping particle's time changed");
		particles_plasma_sparks = TRUE;
		CHECK(particle_first_frame_time(&the_definition, 1.0f / 144.0f) == 1.0f / 144.0f, "a looping particle's time changed");
	}
	return 0;
}
