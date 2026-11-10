/*
RENDER_PARTICLES.C

symbols in this file:
0017BD20 00b0:
	_local_player_is_first_person (0000)
0017BDD0 0030:
	_compare_rendered_particles (0000)
0017BE00 0490:
	_render_particles (0000)
0030E180 05f8:
	_render_particles_section (0000)
*/

/* ---------- headers */

#include <stdlib.h>

#include "camera/director.h"
#include "cseries/cseries.h"
#include "cseries/profile.h"
#include "effects/particles.h"
#include "game/players.h"
#include "interface/first_person_weapons.h"
#include "render.h"
#include "render_cameras_internal.h"
#include "render_particles.h"
#include "render_sprite.h"
#include "tag_files/tag_groups.h"
#include "units/unit_definitions.h"
#include "units/units.h"
#include "rasterizer/rasterizer.h" /* port: (render_particles_cut_run) */
#include "bitmaps/bitmap_group.h" /* port: (render_particles_part_group_bound) */

/* ---------- constants */

enum
{
#ifndef HALO_XBOX_CONSOLE
	/* render_particles lists every visible particle in an array of this size
	without a bound check, so it must stay the size of the native builds'
	particle pool (effects/particles.c, halo_port_capacity.h) */
	MAXIMUM_RENDERED_PARTICLES = HALO_PORT_MAXIMUM_PARTICLES,
#else
	MAXIMUM_RENDERED_PARTICLES = 1024,
#endif
	/* the native builds' (halo_port_capacity.h: the Xbox's 512 on Android
	and the Xbox builds) */
	MAXIMUM_RENDERED_PARTICLE_GROUPS = HALO_PORT_MAXIMUM_RENDERED_PARTICLE_GROUPS,
};

/* ---------- macros */

/* ---------- structures */

struct rendered_particle_datum
{
	short particle_index;
	short definition_index;
	short cluster_index;
	boolean attached_to_first_person_weapon;
	byte pad;
};

typedef char rendered_particle_size_assert[
	sizeof(struct rendered_particle_datum) == 8 ? 1 : -1];

/* ---------- prototypes */

/* ---------- globals */

static struct profile_section render_particles_section =
{
	"render_particles",
	NONE,
	TRUE,
};

/* ---------- public code */

boolean local_player_is_first_person(
	short local_player_index)
{
	boolean result =
		director_get_perspective(local_player_index) ==
		_director_perspective_first_person;

	if (!result)
	{
		long player_index = local_player_get_player_index(local_player_index);
		struct player_datum *player = player_get(player_index);

		if (player->unit_index != NONE)
		{
			struct unit_datum *unit = unit_get(player->unit_index);

			if (unit->object.parent_object_index != NONE &&
				unit->unit.parent_seat_index != NONE)
			{
				struct unit_datum *parent_unit =
					unit_get(unit->object.parent_object_index);
				struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(
					&unit_definition_get(parent_unit->definition_index)->unit.seats,
					unit->unit.parent_seat_index,
					struct unit_seat);

				if (TEST_FLAG(seat->flags, _unit_seat_gunner_bit))
					result = TRUE;
			}
		}
	}

	return result;
}

static int __cdecl compare_rendered_particles(
	void const *left_pointer,
	void const *right_pointer)
{
	struct rendered_particle_datum const *left = left_pointer;
	struct rendered_particle_datum const *right = right_pointer;
	int result = left->definition_index - right->definition_index;

	if (!result)
	{
		result = left->cluster_index - right->cluster_index;
		if (!result)
		{
			result =
				left->attached_to_first_person_weapon -
				right->attached_to_first_person_weapon;
		}
	}

	return result;
}

/* port: the visible particles in compare_rendered_particles' order, by a
radix sort on the same key (definition, cluster, attached to the
first-person weapon) a byte at a time, each pass skipped when every particle
has the same byte: qsort's (musl's smoothsort, swapping the records by
memcpy) was a third of the particles' drawing in a 64-player match. It is
stable, so a group's particles keep the order they were listed in, which
qsort never promised. */
static void rendered_particles_sort(
	struct rendered_particle_datum *particles,
	long count)
{
	static struct rendered_particle_datum scratch[MAXIMUM_RENDERED_PARTICLES];
	struct rendered_particle_datum *from = particles;
	struct rendered_particle_datum *to = scratch;
	long index;
	short pass;

	/* (least significant first: the flag, the cluster's two bytes, the
	definition's two; the signed values offset so that they sort as numbers) */
	for (pass = 0; pass < 5; pass++)
	{
		long counts[256];
		long total = 0;
		short digit;

		csmemset(counts, 0, sizeof(counts));
		for (index = 0; index < count; index++)
		{
			struct rendered_particle_datum const *particle = &from[index];
			unsigned long key = pass == 0 ? (unsigned long)(particle->attached_to_first_person_weapon != 0) :
				pass < 3 ? (unsigned long)(word)(particle->cluster_index ^ 0x8000) >> (8 * (pass - 1)) :
				(unsigned long)(word)(particle->definition_index ^ 0x8000) >> (8 * (pass - 3));

			counts[key & 0xFF]++;
		}
		for (digit = 0; digit < 256; digit++)
		{
			if (counts[digit] == count)
				break;
		}
		if (digit < 256)
			continue;
		for (digit = 0; digit < 256; digit++)
		{
			long here = counts[digit];

			counts[digit] = total;
			total += here;
		}
		for (index = 0; index < count; index++)
		{
			struct rendered_particle_datum const *particle = &from[index];
			unsigned long key = pass == 0 ? (unsigned long)(particle->attached_to_first_person_weapon != 0) :
				pass < 3 ? (unsigned long)(word)(particle->cluster_index ^ 0x8000) >> (8 * (pass - 1)) :
				(unsigned long)(word)(particle->definition_index ^ 0x8000) >> (8 * (pass - 3));

			to[counts[key & 0xFF]++] = *particle;
		}
		{
			struct rendered_particle_datum *swap = from;

			from = to;
			to = swap;
		}
	}
	if (from != particles)
		csmemcpy(particles, from, (size_t)count * sizeof(*particles));

	return;
}

/* port: the depths the transparent queue's groups sort at, and a run of
particles where they are drawn, the key of the part each is in (NONE when
it is not drawn) and the order they are built in (render_particles_cut_run):
sized for the most groups a frame may have, a console's above its memory
baseline (halo_port_capacity.h) */
static real render_particles_depths[RASTERIZER_MAXIMUM_TRANSPARENT_GEOMETRY_GROUPS_EXTRAS];
static short render_particles_part_counts[RASTERIZER_MAXIMUM_TRANSPARENT_GEOMETRY_GROUPS_EXTRAS + 1];
static short render_particles_part_ends[MAXIMUM_RENDERED_PARTICLES];
static real_point3d render_particles_run_positions[MAXIMUM_RENDERED_PARTICLES];
static real_vector3d render_particles_run_directions[MAXIMUM_RENDERED_PARTICLES];
static short render_particles_run_keys[MAXIMUM_RENDERED_PARTICLES];
static short render_particles_run_order[MAXIMUM_RENDERED_PARTICLES];

/* port: a particle's place and direction, as render_particles found them
as it built the particle's sprite: FALSE, the particle deleted, when the
object it is on is gone */
static boolean render_particle_place(
	struct rendered_particle_datum const *rendered_particle,
	real_point3d *position,
	real_vector3d *direction)
{
	struct particle_datum *particle = particle_get(rendered_particle->particle_index);

	if (particle->object_index != NONE)
	{
		real_matrix4x3 const *node_matrix;

		if (TEST_FLAG(
			particle->flags,
			_particle_datum_attached_to_local_player_bit))
		{
			node_matrix =
				first_person_weapon_get_node_matrix(
					particle->local_player_index,
					particle->node_index);
		}
		else if (object_try_and_get(particle->object_index))
		{
			node_matrix = object_get_node_matrix(
				particle->object_index,
				particle->node_index);
		}
		else
		{
			particle_delete(rendered_particle->particle_index);
			return FALSE;
		}

		matrix4x3_transform_point(
			node_matrix,
			&particle->position,
			position);
		matrix4x3_transform_normal(
			node_matrix,
			&particle->direction,
			direction);
	}
	else
	{
		/* (where it is drawn: a tick behind, as the objects, particles.c) */
		particle_render_position(rendered_particle->particle_index, position);
		*direction = particle->direction;
		/* January explicitly reasserts the detached sentinel here. */
		particle->object_index = NONE;
	}

	return TRUE;
}

/* port: the key of the part of a run of particles a particle that sorts
at particle_depth is in: the count of the depths (ascending: z_sort's, the
farthest first) the queue's other groups sort at below its own (a depth it
equals is not) */
static short render_particles_run_key(
	real particle_depth,
	real const *depths,
	long depth_count)
{
	long low = 0;
	long high = depth_count;

	while (low < high)
	{
		long middle = (low + high) / 2;

		if (depths[middle] < particle_depth)
		{
			low = middle + 1;
		}
		else
		{
			high = middle;
		}
	}

	return (short)low;
}

/* port: a run of particles (one definition's in one cluster) is one group
of the transparent queue, sorted at its particles' centroid, so an object
amid the run was drawn over all of it or under all of it: a pickup behind
a waterfall's mist showed through the mist in front of it. The run
is built in parts instead, cut at the depths the queue's other groups sort
at: each particle in the part of its key (render_particles_run_key), the
parts from the farthest, each in the run's order. A part's centroid, the
average of its particles' depths, sorts among the queue's groups where each
of its particles would alone. Returns the parts' count and where each ends
in order; a particle whose key is NONE is in none. With one key among the
particles', or more parts than maximum_part_count, the run is one part in
its order, as the Xbox built it. counts holds one a depth, and one more. */
static short render_particles_cut_run(
	short const *keys,
	short particle_count,
	long depth_count,
	short maximum_part_count,
	short *counts,
	short *order,
	short *part_ends)
{
	short particle_index;
	short part_count = 0;
	short order_count = 0;
	long key;

	for (key = 0; key <= depth_count; key++)
	{
		counts[key] = 0;
	}
	for (particle_index = 0; particle_index < particle_count; particle_index++)
	{
		if (keys[particle_index] == NONE)
		{
			continue;
		}
		if (!counts[keys[particle_index]]++)
		{
			part_count++;
		}
	}

	if (part_count > maximum_part_count)
	{
		for (particle_index = 0; particle_index < particle_count; particle_index++)
		{
			if (keys[particle_index] != NONE)
			{
				order[order_count++] = particle_index;
			}
		}
		part_ends[0] = order_count;
		return order_count ? 1 : 0;
	}

	/* (each key's count become where its part starts) */
	part_count = 0;
	for (key = 0; key <= depth_count; key++)
	{
		short count = counts[key];

		if (count)
		{
			counts[key] = order_count;
			order_count += count;
			part_ends[part_count++] = order_count;
		}
	}
	for (particle_index = 0; particle_index < particle_count; particle_index++)
	{
		if (keys[particle_index] != NONE)
		{
			order[counts[keys[particle_index]]++] = particle_index;
		}
	}

	return part_count;
}

/* port: the most parts a run may be cut into now: as many as the room left
allows (the queue's free groups or the free dynamic vertex buffers, the
fewer, less this run built whole and the later runs built whole, and no
more than the frame's cut_budget), each charged the most groups a part of
it makes (part_group_bound); one, the run whole, when there is none */
static short render_particles_maximum_part_count(
	long free_group_count,
	long free_vertex_buffer_count,
	long part_group_bound,
	long later_group_count,
	long cut_budget)
{
	long room = MIN(free_group_count, free_vertex_buffer_count) - part_group_bound - later_group_count;
	short maximum_part_count = 1;

	room = MIN(room, cut_budget);
	if (room > 0 && part_group_bound > 0)
	{
		maximum_part_count = (short)MIN(room / part_group_bound + 1, SHORT_MAX);
	}

	return maximum_part_count;
}

/* port: the most transparent groups, and dynamic vertex buffers, one part
of a run of particles of this definition makes: one a bitmap its sprites
are on, at most a build_sprites_begin call's (render_sprite.c) */
static long render_particles_part_group_bound(
	struct particle_definition const *definition)
{
	long bound = 1;

	if (definition->bitmap.index != NONE)
	{
		struct bitmap_group const *bitmap_group = bitmap_group_get(definition->bitmap.index);

		bound = PIN((long)bitmap_group->bitmaps.count, 1,
			(long)NUMBEROF(((struct build_sprite_data *)0)->groups));
	}

	return bound;
}

void render_particles(
	void)
{
	struct rendered_particle_datum rendered_particles[MAXIMUM_RENDERED_PARTICLES];
	short group_particle_counts[MAXIMUM_RENDERED_PARTICLE_GROUPS];
	short rendered_particle_count;
	struct build_sprite_data sprite_data;
	real_point3d position;
	real_vector3d direction;

	rendered_particle_count = 0;
	profile_enter(render_particles_section);

	if (render_particles_enabled)
	{
		short local_player_index = render.local_player_index;
		long particle_index;

		if (local_player_index == NONE ||
			!local_player_is_first_person(local_player_index))
		{
			local_player_index = MAXIMUM_LOCAL_PLAYERS;
		}

		for (particle_index = data_next_index(particle_data, NONE);
			particle_index != NONE;
			particle_index = data_next_index(particle_data, particle_index))
		{
			struct particle_datum *particle = particle_get(particle_index);
			boolean owned_by_local_player =
				particle->local_player_index == local_player_index;

			if (render_location_visible(&particle->location) &&
				(!TEST_FLAG(
					particle->flags,
					_particle_datum_dont_draw_first_person_bit) ||
					!owned_by_local_player) &&
				(!TEST_FLAG(
					particle->flags,
					_particle_datum_dont_draw_third_person_bit) ||
					owned_by_local_player))
			{
				struct rendered_particle_datum *rendered_particle =
					&rendered_particles[rendered_particle_count++];

				rendered_particle->particle_index = (short)particle_index;
				rendered_particle->definition_index =
					(short)particle->definition_index;
				rendered_particle->cluster_index =
					particle->location.cluster_index;
				rendered_particle->attached_to_first_person_weapon =
					owned_by_local_player &&
					TEST_FLAG(
						particle->flags,
						_particle_datum_dont_draw_third_person_bit);
			}
		}

		if (rendered_particle_count > 0)
		{
			short group_count = 0;

			rendered_particles_sort(rendered_particles, rendered_particle_count);

			{
				struct rendered_particle_datum *rendered_particle =
					rendered_particles;
				short *current_sprite_group = NULL;
				short previous_definition_index = NONE;
				short previous_cluster_index = NONE;
				boolean previous_attached_to_first_person_weapon = FALSE;

				while (rendered_particle_count-- > 0)
				{
					if (rendered_particle->definition_index ==
							previous_definition_index &&
						rendered_particle->cluster_index ==
							previous_cluster_index &&
						rendered_particle->attached_to_first_person_weapon ==
							previous_attached_to_first_person_weapon)
					{
						(*current_sprite_group)++;
					}
					else
					{
						if (group_count >= MAXIMUM_RENDERED_PARTICLE_GROUPS)
							break;

						current_sprite_group =
							&group_particle_counts[group_count++];
						*current_sprite_group = 1;
						previous_definition_index =
							rendered_particle->definition_index;
						previous_cluster_index =
							rendered_particle->cluster_index;
						previous_attached_to_first_person_weapon =
							rendered_particle->attached_to_first_person_weapon;
					}

					rendered_particle++;
				}
			}

			{
				struct rendered_particle_datum *rendered_particle =
					rendered_particles;
				short group_index;
				/* port: the depths the queue's groups sort at, where a run is cut
				(render_particles_cut_run), and how many more groups the cuts may
				add this frame: a quarter of the room the runs built whole leave
				(the queue's, and the dynamic vertex buffers', one a group), so
				that the effects after these (plasma, contrails, particle
				systems) still have theirs. A part is charged the most groups it
				can make (render_particles_part_group_bound). */
				long depth_count = rasterizer_transparent_geometry_sort_depths(
					render_particles_depths, NUMBEROF(render_particles_depths));
				long whole_group_count = 0;
				long cut_budget;

				for (group_index = 0; group_index < group_count; group_index++)
				{
					whole_group_count += render_particles_part_group_bound(
						particle_definition_get(rendered_particle->definition_index));
					rendered_particle += group_particle_counts[group_index];
				}
				rendered_particle = rendered_particles;
				cut_budget = MAX(0, MIN(rasterizer_transparent_geometry_free_group_count(),
					rasterizer_dynamic_vertex_buffer_free_count()) - whole_group_count) / 4;

				for (group_index = 0; group_index < group_count; group_index++)
				{
					struct particle_definition *definition =
						particle_definition_get(
							rendered_particle->definition_index);
					short particle_count = group_particle_counts[group_index];
					long part_group_bound = render_particles_part_group_bound(definition);
					short part_count;
					short part_index;
					short run_index;
					short order_index = 0;

					/* port: each of the run's particles where it is drawn, first,
					and the part it is in by the depth it would sort at alone (the
					first-person weapon's runs, which sort after the world, one
					part) */
					for (run_index = 0; run_index < particle_count; run_index++)
					{
						render_particles_run_keys[run_index] = NONE;
						if (render_particle_place(
							&rendered_particle[run_index],
							&render_particles_run_positions[run_index],
							&render_particles_run_directions[run_index]))
						{
							render_particles_run_keys[run_index] = render_particles_run_key(
								rasterizer_dynamic_unlit_geometry_z_sort(
									(struct shader const *)&definition->shader,
									&render_particles_run_positions[run_index]),
								render_particles_depths,
								rendered_particle->attached_to_first_person_weapon ? 0 : depth_count);
						}
					}
					/* port: and the run built in parts, cut where the queue's groups
					sort among its particles, in as many as the room now allows */
					whole_group_count -= part_group_bound;
					part_count = render_particles_cut_run(
						render_particles_run_keys,
						particle_count,
						rendered_particle->attached_to_first_person_weapon ? 0 : depth_count,
						render_particles_maximum_part_count(
							rasterizer_transparent_geometry_free_group_count(),
							rasterizer_dynamic_vertex_buffer_free_count(),
							part_group_bound,
							whole_group_count,
							cut_budget),
						render_particles_part_counts,
						render_particles_run_order,
						render_particles_part_ends);
					if (part_count > 1)
					{
						cut_budget -= (part_count - 1) * part_group_bound;
					}

					for (part_index = 0; part_index < part_count; part_index++)
					{
						real total_radius = 0.0f;
						short built_particle_count = 0;

						build_sprites_begin(
							&sprite_data,
							render_particles_part_ends[part_index] - order_index,
							definition->bitmap.index,
							&definition->shader,
							rendered_particle->attached_to_first_person_weapon
								? FLAG(_build_sprites_first_person_bit)
								: 0);

						for (; order_index < render_particles_part_ends[part_index]; order_index++)
						{
							short particle_run_index = render_particles_run_order[order_index];
							struct particle_datum *particle =
								particle_get(rendered_particle[particle_run_index].particle_index);
							real radius =
								particle_get_radius(rendered_particle[particle_run_index].particle_index);

							position = render_particles_run_positions[particle_run_index];
							direction = render_particles_run_directions[particle_run_index];

							{
								real diameter =
									render_frustum_sphere_diameter_in_pixels(
										&render.frustum,
										&position,
										radius);

								if (diameter > definition->lod_cutoff)
								{
									real fade;
									real scale;
									real remaining_life;
									unsigned long flags;
									word particle_flags;

									fade = 1.0f;
									built_particle_count++;
									scale =
										(radius + radius) *
										definition->runtime_oo_width;
									remaining_life =
										particle->lifespan - particle->age;
									total_radius += radius;

									if (diameter < definition->minimum_pixels)
									{
										scale =
											(definition->minimum_pixels / diameter) * scale;
									}

									if (definition->fade_in_time > 0.0f &&
										particle->age < definition->fade_in_time)
									{
										fade = particle->age / definition->fade_in_time;
									}

									if (definition->fade_out_time > 0.0f &&
										remaining_life < definition->fade_out_time)
									{
										fade *=
											remaining_life / definition->fade_out_time;
									}

									flags = 0;
									particle_flags = particle->flags;
									SET_FLAG(
										flags,
										_build_sprite_u_mirror_bit,
										TEST_FLAG(
											particle_flags,
											_particle_datum_u_mirror_bit));
									SET_FLAG(
										flags,
										_build_sprite_v_mirror_bit,
										TEST_FLAG(
											particle_flags,
											_particle_datum_v_mirror_bit));

									build_sprite(
										&sprite_data,
										definition->sprite_orientation,
										particle->sequence_index,
										particle->frame_index,
										&position,
										&direction,
										particle->rotation,
										scale,
										&particle->color,
										fade,
										flags);

									particle->last_rendered_frame_index =
										render.frame_index;
								}
							}
						}

						((struct shader_effect_definition *)sprite_data.shader)->
							secondary_map_radius = built_particle_count ?
							total_radius / built_particle_count : 0.0f;
						build_sprites_end(&sprite_data);
					}

					rendered_particle += particle_count;
				}
			}
		}
	}

	profile_exit(render_particles_section);
	return;
}

/* ---------- private code */
