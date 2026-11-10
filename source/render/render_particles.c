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
particles where they are drawn, the depth each sorts at alone and the order
they are built in (render_particles_cut_run) */
static real render_particles_depths[RASTERIZER_MAXIMUM_TRANSPARENT_GEOMETRY_GROUPS];
static short render_particles_part_counts[RASTERIZER_MAXIMUM_TRANSPARENT_GEOMETRY_GROUPS + 1];
static short render_particles_part_ends[MAXIMUM_RENDERED_PARTICLES];
static real_point3d render_particles_run_positions[MAXIMUM_RENDERED_PARTICLES];
static real_vector3d render_particles_run_directions[MAXIMUM_RENDERED_PARTICLES];
static real render_particles_run_depths[MAXIMUM_RENDERED_PARTICLES];
static boolean render_particles_run_valid[MAXIMUM_RENDERED_PARTICLES];
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

/* port: a run of particles (one definition's in one cluster) is one group
of the transparent queue, sorted at its particles' centroid, so an object
amid the run was drawn over all of it or under all of it: a pickup behind
a waterfall's mist showed through the mist in front of it. The run
is built in parts instead, cut at the depths (ascending: z_sort's, the
farthest first) the queue's other groups sort at: each particle in the
part of the depths below its own, the parts from the farthest, each in the
run's order. A part's centroid, the average of its particles' depths, sorts
among the queue's groups where each of its particles would alone. Returns
the parts' count and where each ends in order; a particle not valid is in
none. With no depth among its particles', or more parts than
maximum_part_count, the run is one part in its order, as the Xbox built it.
keys holds one key a particle; counts one a depth, and one more. */
static short render_particles_cut_run(
	real const *particle_depths,
	boolean const *valid,
	short particle_count,
	real const *depths,
	long depth_count,
	short maximum_part_count,
	short *keys,
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
		long low = 0;
		long high = depth_count;

		if (!valid[particle_index])
		{
			continue;
		}
		/* (the depths below the particle's: a depth it equals is not) */
		while (low < high)
		{
			long middle = (low + high) / 2;

			if (depths[middle] < particle_depths[particle_index])
			{
				low = middle + 1;
			}
			else
			{
				high = middle;
			}
		}
		keys[particle_index] = (short)low;
		if (!counts[low]++)
		{
			part_count++;
		}
	}

	if (part_count > maximum_part_count)
	{
		for (particle_index = 0; particle_index < particle_count; particle_index++)
		{
			if (valid[particle_index])
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
		if (valid[particle_index])
		{
			order[counts[keys[particle_index]]++] = particle_index;
		}
	}

	return part_count;
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
				(render_particles_cut_run), and as many more groups as the cuts
				may add this frame: a quarter of the queue's room, so that the
				effects after these still have theirs */
				long depth_count = rasterizer_transparent_geometry_sort_depths(
					render_particles_depths, NUMBEROF(render_particles_depths));
				long cut_budget = rasterizer_transparent_geometry_free_group_count() / 4;

				for (group_index = 0; group_index < group_count; group_index++)
				{
					struct particle_definition *definition =
						particle_definition_get(
							rendered_particle->definition_index);
					short particle_count = group_particle_counts[group_index];
					short part_count;
					short part_index;
					short run_index;
					short order_index = 0;

					/* port: each of the run's particles where it is drawn, first,
					and the depth it would sort at alone */
					for (run_index = 0; run_index < particle_count; run_index++)
					{
						render_particles_run_valid[run_index] = render_particle_place(
							&rendered_particle[run_index],
							&render_particles_run_positions[run_index],
							&render_particles_run_directions[run_index]);
						if (render_particles_run_valid[run_index])
						{
							render_particles_run_depths[run_index] = rasterizer_dynamic_unlit_geometry_z_sort(
								(struct shader const *)&definition->shader,
								&render_particles_run_positions[run_index]);
						}
					}
					/* port: and the run built in parts, cut where the queue's groups
					sort among its particles (the first-person weapon's runs, which
					sort after the world, whole) */
					part_count = render_particles_cut_run(
						render_particles_run_depths,
						render_particles_run_valid,
						particle_count,
						render_particles_depths,
						rendered_particle->attached_to_first_person_weapon ? 0 : depth_count,
						(short)MIN(cut_budget + 1, SHORT_MAX),
						render_particles_run_keys,
						render_particles_part_counts,
						render_particles_run_order,
						render_particles_part_ends);
					if (part_count > 1)
					{
						cut_budget -= part_count - 1;
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
