#include "harness.h"

/* structure_render.c's surface lists: the sort of a light's or shadow's
list into ascending order (structure_render_sort_surface_indices, which
replaced qsort_4byte with compare_surface_indices) and the walk of the
visible surfaces' bit vector by its set bits
(structure_render_dynamic_triangles_from_bitvector). Both must give the
surfaces, and so the triangles, in the order the old code did: checked
against a plain sort and a walk of every bit, on random lists. */

#include "config.inc"
#define csmemset memset
#define csmemcpy memcpy
struct structure_surface { short vertex_indices[3]; };
struct tag_block { long count; struct structure_surface *elements; };
struct structure_bsp { struct tag_block surfaces; };
#define TAG_BLOCK_GET_ELEMENT(block, index, type) (&((type *)(block)->elements)[(index)])
static struct structure_bsp bsp;
static struct structure_bsp *global_structure_bsp_get(void) { return &bsp; }
#include "under_test.inc"

static unsigned long seed = 12345;
static unsigned long next_random(void) { seed = seed * 1103515245UL + 12345UL; return (seed >> 8) & 0xFFFFFF; }
static int ascending(const void *a, const void *b)
{
	long x = *(const long *)a, y = *(const long *)b;
	return (x > y) - (x < y);
}

static long list[MAXIMUM_LOCALLY_RENDERED_SURFACES], expected[MAXIMUM_LOCALLY_RENDERED_SURFACES];

static void check_sort(long count, long range, int shape)
{
	long index;

	for (index = 0; index < count; index++)
	{
		list[index] = shape == 1 ? index * range / (count ? count : 1) :
			shape == 2 ? (count - index) * range / (count ? count : 1) : (long)(next_random() % (unsigned long)range);
	}
	memcpy(expected, list, count * sizeof(long));
	qsort(expected, (size_t)count, sizeof(long), ascending);
	structure_render_sort_surface_indices(list, count);
	CHECK(!memcmp(list, expected, count * sizeof(long)), "a list of %ld (indices below %ld, shape %d) sorted as before",
		count, range, shape);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	CASE("sort")
	{
		long counts[] = { 0, 1, 2, 3, 15, 16, 17, 63, 64, 65, 200, 1000, 2047, 4096 };
		long ranges[] = { 1, 7, 300, 2048, 2049, 30000, 0x7FFFFFF };
		unsigned long c, r;
		int shape, round;

		for (round = 0; round < 20; round++)
			for (c = 0; c < NUMBEROF(counts); c++)
				for (r = 0; r < NUMBEROF(ranges); r++)
					for (shape = 0; shape < 3; shape++)
						check_sort(counts[c], ranges[r], shape);
		return 0;
	}
	CASE("walk")
	{
		static struct structure_surface surfaces[3000];
		static unsigned long flags[3000 / 32 + 2];
		static long indices[3000], expected_indices[3000];
		static short triangles[3000 * 3], expected_triangles[3000 * 3];
		long counts[] = { 0, 1, 31, 32, 33, 64, 100, 2999 };
		unsigned long c;
		int round;

		for (c = 0; c < 3000; c++)
		{
			surfaces[c].vertex_indices[0] = (short)c;
			surfaces[c].vertex_indices[1] = (short)(c + 1);
			surfaces[c].vertex_indices[2] = (short)(c * 7);
		}
		bsp.surfaces.elements = surfaces;
		for (round = 0; round < 200; round++)
		{
			for (c = 0; c < NUMBEROF(counts); c++)
			{
				long count = counts[c], surface, found = 0, word;
				unsigned long density = next_random() % 4;

				bsp.surfaces.count = count;
				/* (bits past the surfaces too, which the old walk left alone) */
				for (word = 0; word < (long)NUMBEROF(flags); word++)
				{
					flags[word] = density == 0 ? 0 : density == 3 ? 0xFFFFFFFFUL :
						(unsigned long)next_random() << 8 ^ (unsigned long)next_random();
					if (density == 1)
						flags[word] &= (unsigned long)next_random() << 8 ^ (unsigned long)next_random();
				}
				for (surface = 0; surface < count; surface++)
				{
					if (TEST_FLAG(flags[surface / 32], surface % 32))
					{
						expected_indices[found] = surface;
						memcpy(&expected_triangles[found * 3], surfaces[surface].vertex_indices, 3 * sizeof(short));
						found++;
					}
				}
				memset(indices, 0xFF, sizeof(indices));
				structure_render_dynamic_triangles_from_bitvector((short)found, indices, flags, triangles);
				CHECK(!memcmp(indices, expected_indices, found * sizeof(long)) &&
					!memcmp(triangles, expected_triangles, found * 3 * sizeof(short)) && indices[found] == NONE,
					"%ld set bits of %ld surfaces walked as before", found, count);
			}
		}
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
