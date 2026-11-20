/*
PHYSICS_JOLT.CPP

physics_jolt.h's C face on Jolt Physics 5.5.0 (port/third_party/jolt, MIT,
its LICENSE beside it). Three layers: the level (static), what moves in it
(kinematic: a vehicle's hull, a machine's parts, put where the game has
them), and the ragdolls' parts, which meet all three. Nothing else is in the
world, and nothing in it reaches the game: it is only the picture.

Jolt's memory goes through counting hooks (physics_jolt_get_stats); its jobs
run on the caller's thread (JobSystemSingleThreaded).

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
#include <Jolt/Skeleton/Skeleton.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "physics_jolt.h"

extern "C" void platform_log(const char *format, ...);

using namespace JPH;

namespace
{

/* ---------- memory: counted (the block's size in a header before it) */

struct counters
{
	unsigned int now, peak, allocations;
} memory;

const size_t HEADER = 16;

void note(long delta)
{
	memory.now += (unsigned int)delta;
	if (memory.now > memory.peak)
		memory.peak = memory.now;
	if (delta > 0)
		memory.allocations++;
}

/* whether the C heap has bytes free in one piece now (taken and given back):
a 32-bit process's address space can run short (the game's windows, the
graphics driver's mappings), and Jolt does not check what it is given, so
a world or a ragdoll is made only with room to spare */
bool room_for(size_t bytes)
{
	void *probe = std::malloc(bytes);

	if (!probe)
		return false;
	std::free(probe);
	return true;
}

/* the room asked for before a world (its system, the level's mesh being
made, the step's memory) and before a ragdoll */
const size_t WORLD_ROOM = 32 * 1024 * 1024;
const size_t RAGDOLL_ROOM = 4 * 1024 * 1024;

/* the reserve: what an allocation the C heap refuses all the same is given
(Jolt does not check for a null, so it is never handed one). Taken when a
world is made, given back when nothing in it is used any more. From the
first refusal on, physics_jolt_out_of_memory says so: the ragdolls let every
body go and the world goes, all in the same tick (ragdolls.c). A mesh being
made stops at its next piece; the reserve holds the rest of what a step or
a ragdoll asks. Only an allocation the reserve has no room for either still
stops the game. */
const size_t RESERVE_SIZE = 2 * 1024 * 1024;
const size_t RESERVE_ALIGNMENT = 16;

struct reserve_state
{
	unsigned char *block;
	size_t used;
	/* (the reserve's allocations not yet freed: it starts again empty at 0) */
	unsigned int live;
	bool out_of_memory;
	/* (the tests: the allocation this many from now refused, as the C heap
	would refuse it, and every one after it until the world goes; 0 none) */
	unsigned int refuse_after;
	bool refusing;
} reserve;

bool reserve_owns(void const *block)
{
	return reserve.block && (unsigned char const *)block >= reserve.block &&
		(unsigned char const *)block < reserve.block + RESERVE_SIZE;
}

/* the reserve given back when nothing in it is used */
void reserve_release(void)
{
	if (reserve.block && !reserve.live)
	{
		std::free(reserve.block);
		reserve.block = nullptr;
		reserve.used = 0;
	}
}

void *raw_allocate(size_t bytes)
{
	void *block = nullptr;

	if (reserve.refuse_after && !--reserve.refuse_after)
		reserve.refusing = true;
	if (!reserve.refusing)
		block = std::malloc(bytes);
	if (block)
		return block;
	if (!reserve.out_of_memory)
		platform_log("jolt: no memory for %lu bytes: the ragdolls stop", (unsigned long)bytes);
	reserve.out_of_memory = true;
	size_t start = (reserve.used + RESERVE_ALIGNMENT - 1) & ~(RESERVE_ALIGNMENT - 1);
	if (!reserve.block || bytes > RESERVE_SIZE - start)
	{
		platform_log("jolt: no memory for %lu bytes, and the reserve has no room either: the game stops here",
			(unsigned long)bytes);
		std::abort();
	}
	reserve.used = start + bytes;
	reserve.live++;
	return reserve.block + start;
}

void raw_free(void *block)
{
	if (!reserve_owns(block))
	{
		std::free(block);
		return;
	}
	if (reserve.live && !--reserve.live)
		reserve.used = 0;
}

void *counted_allocate(size_t size)
{
	unsigned char *block = (unsigned char *)raw_allocate(size + HEADER);

	*(size_t *)block = size;
	note((long)size);
	return block + HEADER;
}

void counted_free(void *pointer)
{
	if (!pointer)
		return;
	unsigned char *block = (unsigned char *)pointer - HEADER;
	note(-(long)*(size_t *)block);
	raw_free(block);
}

void *counted_reallocate(void *pointer, size_t old_size, size_t new_size)
{
	void *result = counted_allocate(new_size);

	if (pointer)
		std::memcpy(result, pointer, old_size < new_size ? old_size : new_size);
	counted_free(pointer);
	return result;
}

/* (aligned: the block's start and size kept just before the aligned
pointer) */
void *counted_aligned_allocate(size_t size, size_t alignment)
{
	if (alignment < sizeof(void *) * 2)
		alignment = sizeof(void *) * 2;
	unsigned char *block = (unsigned char *)raw_allocate(size + alignment + 2 * sizeof(void *));
	uintptr_t aligned = ((uintptr_t)block + 2 * sizeof(void *) + alignment - 1) & ~(uintptr_t)(alignment - 1);

	((void **)aligned)[-1] = block;
	((size_t *)aligned)[-2] = size;
	note((long)size);
	return (void *)aligned;
}

void counted_aligned_free(void *pointer)
{
	if (!pointer)
		return;
	note(-(long)((size_t *)pointer)[-2]);
	raw_free(((void **)pointer)[-1]);
}

/* ---------- layers */

namespace layers
{
	const ObjectLayer STATIC = 0;
	const ObjectLayer MOVING = 1;
	const ObjectLayer PART = 2;
	const uint COUNT = 3;
}

namespace broad
{
	const BroadPhaseLayer STATIC(0);
	const BroadPhaseLayer MOVING(1);
	const uint COUNT = 2;
}

class layer_interface final : public BroadPhaseLayerInterface
{
public:
	uint GetNumBroadPhaseLayers() const override { return broad::COUNT; }
	BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override
	{
		return layer == layers::STATIC ? broad::STATIC : broad::MOVING;
	}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	const char *GetBroadPhaseLayerName(BroadPhaseLayer layer) const override { return "layer"; }
#endif
};

class object_vs_broad final : public ObjectVsBroadPhaseLayerFilter
{
public:
	bool ShouldCollide(ObjectLayer layer, BroadPhaseLayer broad_layer) const override
	{
		/* (a part meets everything; the level and what moves in it, only
		the parts) */
		return layer == layers::PART || broad_layer == broad::MOVING;
	}
};

class object_vs_object final : public ObjectLayerPairFilter
{
public:
	bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override
	{
		return a == layers::PART || b == layers::PART;
	}
};

/* (a part against the level: its own friction with the level's, the square
root of the product, and no bounce) */
const float LEVEL_FRICTION = 0.6f;

class contacts final : public ContactListener
{
	static void level(const Body &a, const Body &b, ContactSettings &settings)
	{
		if (a.GetObjectLayer() == layers::STATIC || b.GetObjectLayer() == layers::STATIC)
		{
			const Body &part = a.GetObjectLayer() == layers::PART ? a : b;

			settings.mCombinedFriction = std::sqrt(part.GetFriction() * LEVEL_FRICTION);
			settings.mCombinedRestitution = 0.f;
		}
	}
public:
	void OnContactAdded(const Body &a, const Body &b, const ContactManifold &, ContactSettings &settings) override
	{
		level(a, b, settings);
	}
	void OnContactPersisted(const Body &a, const Body &b, const ContactManifold &, ContactSettings &settings) override
	{
		level(a, b, settings);
	}
};

/* (physics_jolt_add_mesh) the last level mesh made, and what it was made
from; physics_jolt_forget_meshes lets it go */
ShapeRefC level_kept;
unsigned long long level_kept_key;

unsigned long long mesh_key(float const *vertices, int vertex_count, int const *triangles, int triangle_count)
{
	unsigned long long key = 1469598103934665603ULL ^ ((unsigned long long)vertex_count << 32) ^ (unsigned)triangle_count;
	uint32 word;

	for (int k = 0; k < 3 * vertex_count; k++)
	{
		std::memcpy(&word, &vertices[k], 4);
		key = (key ^ word) * 1099511628211ULL;
	}
	for (int k = 0; k < 3 * triangle_count; k++)
		key = (key ^ (uint32)triangles[k]) * 1099511628211ULL;
	return key;
}

/* a large mesh is made in spatial pieces of at most this many triangles,
under one static compound: Jolt's build of a mesh holds some 250 bytes a
triangle at once, a campaign level's 80,000 triangles 20 MB in one piece */
const int MESH_PIECE_TRIANGLES = 4096;

/* the step's temporary memory: a fixed 1 MB (a dozen ragdolls in a level use
a few hundred KB), and past it, should a step need more, its own blocks */
class step_temporary final : public TempAllocator
{
public:
	JPH_OVERRIDE_NEW_DELETE

	explicit step_temporary(uint size) : primary(size) {}

	void *Allocate(uint size) override
	{
		if (size == 0)
			return nullptr;
		if (primary.CanAllocate(size))
			return primary.Allocate(size);
		return AlignedAllocate(size, JPH_RVECTOR_ALIGNMENT);
	}

	void Free(void *address, uint size) override
	{
		if (!address)
			return;
		if (primary.OwnsMemory(address))
			primary.Free(address, size);
		else
			AlignedFree(address);
	}

private:
	TempAllocatorImpl primary;
};

struct world
{
	contacts contact_listener;
	Array<Ref<Ragdoll>> ragdolls;
	unsigned int ragdoll_groups = 0;
	layer_interface layer_map;
	object_vs_broad broad_filter;
	object_vs_object pair_filter;
	PhysicsSystem system;
	step_temporary temporary { 1024 * 1024 };
	JobSystemSingleThreaded jobs { cMaxPhysicsJobs };
	Array<BodyID> bodies;
	Array<bool> added;
	unsigned long long step_us = 0;
	unsigned int steps = 0;
};

world *the_world = nullptr;
unsigned int world_generation = 0;
bool registered = false;

inline Vec3 vec(float const *v) { return Vec3(v[0], v[1], v[2]); }
inline Quat quat(float const *q) { return Quat(q[0], q[1], q[2], q[3]).Normalized(); }

BodyID body_id(int body)
{
	if (!the_world || body < 0 || body >= (int)the_world->bodies.size())
		return BodyID();
	return the_world->bodies[body];
}

int keep(BodyID id, bool added)
{
	the_world->bodies.push_back(id);
	the_world->added.push_back(added);
	return (int)the_world->bodies.size() - 1;
}

Ragdoll *ragdoll_get(int ragdoll)
{
	if (!the_world || ragdoll < 0 || ragdoll >= (int)the_world->ragdolls.size())
		return nullptr;
	return the_world->ragdolls[ragdoll];
}

void trace(const char *format, ...)
{
	char line[512];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	platform_log("jolt: %s", line);
}

/* a mesh's triangles ordered into pieces of at most MESH_PIECE_TRIANGLES,
each split at the median of its triangles' centres along its longest side */
void mesh_pieces(float const *vertices, int const *triangles, int triangle_count, Array<int> &order,
	Array<std::pair<int, int>> &pieces)
{
	Array<std::pair<int, int>> stack;

	order.resize((size_t)triangle_count);
	for (int index = 0; index < triangle_count; index++)
		order[(size_t)index] = index;
	stack.push_back(std::make_pair(0, triangle_count));
	while (!stack.empty())
	{
		std::pair<int, int> range = stack.back();
		stack.pop_back();
		if (range.second - range.first <= MESH_PIECE_TRIANGLES)
		{
			pieces.push_back(range);
			continue;
		}
		float low[3] = { 1e30f, 1e30f, 1e30f }, high[3] = { -1e30f, -1e30f, -1e30f };
		for (int k = range.first; k < range.second; k++)
		{
			int const *t = &triangles[3 * order[(size_t)k]];
			for (int axis = 0; axis < 3; axis++)
			{
				float centre = vertices[3 * t[0] + axis] + vertices[3 * t[1] + axis] + vertices[3 * t[2] + axis];
				low[axis] = std::min(low[axis], centre);
				high[axis] = std::max(high[axis], centre);
			}
		}
		int axis = 0;
		for (int a = 1; a < 3; a++)
		{
			if (high[a] - low[a] > high[axis] - low[axis])
				axis = a;
		}
		int middle = range.first + (range.second - range.first) / 2;
		std::nth_element(order.begin() + range.first, order.begin() + middle, order.begin() + range.second,
			[&](int a, int b)
			{
				int const *ta = &triangles[3 * a], *tb = &triangles[3 * b];
				float ca = vertices[3 * ta[0] + axis] + vertices[3 * ta[1] + axis] + vertices[3 * ta[2] + axis];
				float cb = vertices[3 * tb[0] + axis] + vertices[3 * tb[1] + axis] + vertices[3 * tb[2] + axis];
				return ca < cb || (ca == cb && a < b);
			});
		stack.push_back(std::make_pair(middle, range.second));
		stack.push_back(std::make_pair(range.first, middle));
	}
}

/* a triangle mesh's shape, in pieces when it is large; null: none */
ShapeRefC mesh_shape(float const *vertices, int vertex_count, int const *triangles, int triangle_count)
{
	Array<int> order;
	Array<std::pair<int, int>> pieces;
	Array<int> remap((size_t)vertex_count, -1);
	StaticCompoundShapeSettings compound;
	ShapeRefC whole;

	mesh_pieces(vertices, triangles, triangle_count, order, pieces);
	for (std::pair<int, int> const &piece : pieces)
	{
		/* (out of memory: no more pieces, the reserve kept for the rest) */
		if (reserve.out_of_memory)
			return nullptr;
		VertexList points;
		IndexedTriangleList list;

		list.reserve((size_t)(piece.second - piece.first));
		for (int k = piece.first; k < piece.second; k++)
		{
			int const *t = &triangles[3 * order[(size_t)k]];
			int corner[3];
			for (int c = 0; c < 3; c++)
			{
				int v = t[c] >= 0 && t[c] < vertex_count ? t[c] : 0;
				if (remap[(size_t)v] < 0)
				{
					remap[(size_t)v] = (int)points.size();
					points.push_back(Float3(vertices[3 * v], vertices[3 * v + 1], vertices[3 * v + 2]));
				}
				corner[c] = remap[(size_t)v];
			}
			list.push_back(IndexedTriangle((uint32)corner[0], (uint32)corner[1], (uint32)corner[2], 0));
		}
		/* (the remap made ready for the next piece) */
		for (int k = piece.first; k < piece.second; k++)
		{
			int const *t = &triangles[3 * order[(size_t)k]];
			for (int c = 0; c < 3; c++)
				remap[(size_t)(t[c] >= 0 && t[c] < vertex_count ? t[c] : 0)] = -1;
		}
		MeshShapeSettings settings(std::move(points), std::move(list));
		Shape::ShapeResult shape = settings.Create();
		if (shape.HasError())
		{
			/* (a piece all of degenerate triangles has none to keep) */
			trace("mesh: %s", shape.GetError().c_str());
			continue;
		}
		if (pieces.size() == 1)
			whole = shape.Get();
		else
			compound.AddShape(Vec3::sZero(), Quat::sIdentity(), shape.Get());
	}
	if (reserve.out_of_memory)
		return nullptr;
	if (pieces.size() > 1)
	{
		if (compound.mSubShapes.empty())
			return nullptr;
		if (compound.mSubShapes.size() == 1)
			return compound.mSubShapes[0].mShapePtr;
		Shape::ShapeResult shape = compound.Create();
		if (shape.HasError())
		{
			trace("mesh: %s", shape.GetError().c_str());
			return nullptr;
		}
		whole = shape.Get();
	}
	return whole;
}

int add_moving(RefConst<Shape> const &shape, float const position[3], float const rotation[4])
{
	BodyCreationSettings body(shape, RVec3(vec(position)), quat(rotation), EMotionType::Kinematic, layers::MOVING);

	/* (a mesh has no mass of its own) */
	body.mOverrideMassProperties = EOverrideMassProperties::MassAndInertiaProvided;
	body.mMassPropertiesOverride.mMass = 1.f;
	body.mMassPropertiesOverride.mInertia = Mat44::sIdentity();
	body.mCollideKinematicVsNonDynamic = false;
	BodyID id = the_world->system.GetBodyInterfaceNoLock().CreateAndAddBody(body, EActivation::DontActivate);
	if (id.IsInvalid())
	{
		trace("no body (%d of at most %d)", (int)the_world->system.GetNumBodies(), (int)the_world->system.GetMaxBodies());
		return -1;
	}
	return keep(id, true);
}

inline float pin_angle(float value)
{
	const float most = 3.05f;
	return value > most ? most : (value < -most ? -most : value);
}

}

extern "C" {

int physics_jolt_create(int maximum_bodies, float const gravity[3])
{
	physics_jolt_destroy();
	if (!room_for(WORLD_ROOM))
	{
		trace("no world: the C heap has no %u MB free in one piece", (unsigned)(WORLD_ROOM >> 20));
		return 0;
	}
	reserve.out_of_memory = false;
	if (!reserve.block && (reserve.block = (unsigned char *)std::malloc(RESERVE_SIZE)) == nullptr)
	{
		trace("no world: no memory for its reserve");
		return 0;
	}
	if (!registered)
	{
		Allocate = counted_allocate;
		Reallocate = counted_reallocate;
		Free = counted_free;
		AlignedAllocate = counted_aligned_allocate;
		AlignedFree = counted_aligned_free;
		Trace = trace;
		Factory::sInstance = new Factory();
		RegisterTypes();
		registered = true;
	}
	the_world = new world();
	world_generation++;
	/* (pairs and contacts: what a few awake ragdolls need, their room taken
	from the temporary allocator each step) */
	the_world->system.Init((uint)maximum_bodies, 0, 4096, 1024, the_world->layer_map, the_world->broad_filter,
		the_world->pair_filter);
	the_world->system.SetGravity(vec(gravity));
	the_world->system.SetContactListener(&the_world->contact_listener);
	{
		PhysicsSettings settings = the_world->system.GetPhysicsSettings();

		/* (a body asleep when its points move under 6 cm/s for half a second) */
		settings.mPointVelocitySleepThreshold = 0.06f;
		settings.mTimeBeforeSleep = 0.5f;
		the_world->system.SetPhysicsSettings(settings);
	}
	return 1;
}

void physics_jolt_destroy(void)
{
	if (!the_world)
		return;
	for (Ref<Ragdoll> &ragdoll : the_world->ragdolls)
	{
		if (ragdoll)
			ragdoll->RemoveFromPhysicsSystem();
	}
	the_world->ragdolls.clear();
	BodyInterface &bodies = the_world->system.GetBodyInterfaceNoLock();
	for (size_t index = 0; index < the_world->bodies.size(); index++)
	{
		if (the_world->bodies[index].IsInvalid())
			continue;
		if (the_world->added[index])
			bodies.RemoveBody(the_world->bodies[index]);
		bodies.DestroyBody(the_world->bodies[index]);
	}
	delete the_world;
	the_world = nullptr;
	reserve.refusing = false;
	reserve_release();
}

int physics_jolt_exists(void)
{
	return the_world != nullptr;
}

unsigned int physics_jolt_generation(void)
{
	return world_generation;
}

void physics_jolt_forget_meshes(void)
{
	level_kept = nullptr;
	level_kept_key = 0;
	if (!the_world)
		reserve_release();
}

int physics_jolt_out_of_memory(void)
{
	return reserve.out_of_memory;
}

void physics_jolt_refuse_allocations_after(unsigned int count)
{
	reserve.refuse_after = count;
}

int physics_jolt_add_mesh(float const *vertices, int vertex_count, int const *triangles, int triangle_count)
{
	if (!the_world || triangle_count <= 0 || reserve.out_of_memory)
		return -1;
	/* (the same level as the last world's - made again after a checkpoint
	revert -: its mesh kept, not made again) */
	unsigned long long key = mesh_key(vertices, vertex_count, triangles, triangle_count);
	ShapeRefC shape;
	if (level_kept && key == level_kept_key)
		shape = level_kept;
	else
	{
		shape = mesh_shape(vertices, vertex_count, triangles, triangle_count);
		if (!shape)
			return -1;
		if (triangle_count > MESH_PIECE_TRIANGLES)
		{
			level_kept = shape;
			level_kept_key = key;
		}
	}
	BodyCreationSettings body(shape, RVec3::sZero(), Quat::sIdentity(), EMotionType::Static, layers::STATIC);
	BodyID id = the_world->system.GetBodyInterfaceNoLock().CreateAndAddBody(body, EActivation::DontActivate);
	if (id.IsInvalid())
		return -1;
	return keep(id, true);
}

int physics_jolt_add_moving_mesh(float const *vertices, int vertex_count, int const *triangles, int triangle_count,
	float const position[3], float const rotation[4])
{
	if (!the_world || triangle_count <= 0 || reserve.out_of_memory)
		return -1;
	ShapeRefC shape = mesh_shape(vertices, vertex_count, triangles, triangle_count);
	if (!shape)
		return -1;
	return add_moving(shape, position, rotation);
}

int physics_jolt_add_moving_hull(float const *points, int point_count, float convex_radius,
	float const position[3], float const rotation[4])
{
	if (!the_world || point_count < 4 || reserve.out_of_memory)
		return -1;
	Array<Vec3> hull_points;
	for (int index = 0; index < point_count; index++)
		hull_points.push_back(vec(points + 3 * index));
	ConvexHullShapeSettings settings(hull_points, convex_radius);
	Shape::ShapeResult shape = settings.Create();
	if (shape.HasError())
	{
		trace("hull: %s", shape.GetError().c_str());
		return -1;
	}
	return add_moving(shape.Get(), position, rotation);
}

void physics_jolt_remove_body(int body)
{
	BodyID id = body_id(body);
	if (id.IsInvalid())
		return;
	BodyInterface &bodies = the_world->system.GetBodyInterfaceNoLock();
	if (the_world->added[body])
		bodies.RemoveBody(id);
	bodies.DestroyBody(id);
	the_world->bodies[body] = BodyID();
	the_world->added[body] = false;
}

void physics_jolt_set_enabled(int body, int enabled)
{
	BodyID id = body_id(body);
	if (id.IsInvalid() || (bool)enabled == the_world->added[body])
		return;
	if (enabled)
		the_world->system.GetBodyInterfaceNoLock().AddBody(id, EActivation::DontActivate);
	else
		the_world->system.GetBodyInterfaceNoLock().RemoveBody(id);
	the_world->added[body] = enabled != 0;
}

void physics_jolt_move(int body, float const position[3], float const rotation[4], float dt)
{
	BodyID id = body_id(body);
	if (id.IsInvalid() || !the_world->added[body])
		return;
	BodyInterface &bodies = the_world->system.GetBodyInterfaceNoLock();
	RVec3 to(vec(position)), at;
	Quat turn = quat(rotation), was;

	/* (one that has not moved - a parked vehicle, a closed door: within 2 mm
	and about a tenth of a degree - is left where it is, so it sleeps: a body
	moved each step stays awake, and one awake body costs a whole step) */
	bodies.GetPositionAndRotation(id, at, was);
	if (Vec3(at - to).LengthSq() < 0.002f * 0.002f && was.IsClose(turn, 1e-6f))
	{
		if (bodies.IsActive(id))
			bodies.SetLinearAndAngularVelocity(id, Vec3::sZero(), Vec3::sZero());
		return;
	}
	bodies.MoveKinematic(id, to, turn, dt);
}

void physics_jolt_optimize(void)
{
	if (the_world)
		the_world->system.OptimizeBroadPhase();
}

void physics_jolt_step(float dt, int collision_steps)
{
	if (!the_world || reserve.out_of_memory)
		return;
	auto started = std::chrono::steady_clock::now();
	the_world->system.Update(dt, collision_steps > 0 ? collision_steps : 1, &the_world->temporary, &the_world->jobs);
	the_world->step_us += (unsigned long long)std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now() - started).count();
	the_world->steps++;
}

void physics_jolt_get_stats(struct physics_jolt_stats *stats)
{
	std::memset(stats, 0, sizeof(*stats));
	stats->bytes_now = memory.now;
	stats->bytes_peak = memory.peak;
	stats->allocations = memory.allocations;
	if (!the_world)
		return;
	stats->bodies = (int)the_world->system.GetNumBodies();
	stats->active_bodies = (int)the_world->system.GetNumActiveBodies(EBodyType::RigidBody);
	stats->step_ms_total = (float)(the_world->step_us / 1000.0);
	stats->steps = the_world->steps;
}

unsigned int physics_jolt_clock_us(void)
{
	return (unsigned int)std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

/* ---------- ragdolls */

int physics_jolt_rig_create(struct physics_jolt_rig_body const *in, int count, struct physics_jolt_rig_options const *options)
{
	static const struct physics_jolt_rig_options none = { 0, 0 };

	if (!options)
		options = &none;
	if (!the_world || count <= 0 || reserve.out_of_memory)
		return -1;
	if (!room_for(RAGDOLL_ROOM))
	{
		trace("no ragdoll: the C heap has no %u MB free in one piece", (unsigned)(RAGDOLL_ROOM >> 20));
		return -1;
	}
	Ref<Skeleton> skeleton = new Skeleton;
	Ref<RagdollSettings> settings = new RagdollSettings;
	char name[16];

	for (int body = 0; body < count; body++)
	{
		if (in[body].parent >= body)
			return -1;
		snprintf(name, sizeof(name), "p%d", body);
		skeleton->AddJoint(name, in[body].parent);
	}
	settings->mSkeleton = skeleton;
	settings->mParts.resize(count);
	for (int body = 0; body < count; body++)
	{
		struct physics_jolt_rig_body const *b = &in[body];
		RagdollSettings::Part &part = settings->mParts[body];
		RefConst<Shape> shape;

		if (b->points && b->point_count >= 4)
		{
			Array<Vec3> points;
			for (int k = 0; k < b->point_count; k++)
				points.push_back(Vec3(b->points[3 * k], b->points[3 * k + 1], b->points[3 * k + 2]));
			ConvexHullShapeSettings hull(points, 0.01f);
			Shape::ShapeResult result = hull.Create();
			if (result.HasError())
			{
				trace("rig hull: %s", result.GetError().c_str());
				return -1;
			}
			shape = result.Get();
		}
		else
		{
			float small = std::min(b->half[0], std::min(b->half[1], b->half[2]));
			shape = new RotatedTranslatedShape(vec(b->box_centre), quat(b->box_rotation),
				new BoxShape(vec(b->half), small < 0.04f ? small * 0.5f : 0.02f));
		}
		part.SetShape(shape);
		part.mPosition = RVec3(vec(b->position));
		part.mRotation = quat(b->rotation);
		part.mMotionType = EMotionType::Dynamic;
		part.mObjectLayer = layers::PART;
		part.mFriction = b->friction;
		part.mRestitution = b->restitution;
		part.mLinearDamping = b->linear_damping;
		part.mAngularDamping = b->angular_damping;
		part.mMaxLinearVelocity = 60.f;
		part.mMotionQuality = b->linear_cast ? EMotionQuality::LinearCast : EMotionQuality::Discrete;
		/* (no ghost contacts on the level mesh's inner edges, which kicked a
		part lying still on uneven ground) */
		part.mEnhancedInternalEdgeRemoval = true;
		if (options->velocity_steps > 0)
			part.mNumVelocityStepsOverride = (uint)options->velocity_steps;
		if (options->position_steps > 0)
			part.mNumPositionStepsOverride = (uint)options->position_steps;
		/* (the inertia its shape's at the mass given) */
		if (b->mass > 0.f)
		{
			part.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
			part.mMassPropertiesOverride.mMass = b->mass;
		}
		else
			part.mOverrideMassProperties = EOverrideMassProperties::CalculateMassAndInertia;
		if (b->parent < 0 || b->joint == PHYSICS_JOLT_RIG_JOINT_NONE)
			continue;
		Quat turn_parent = quat(in[b->parent].rotation), turn = quat(b->rotation);
		Vec3 pivot = vec(b->position);
		if (b->joint == PHYSICS_JOLT_RIG_JOINT_HINGE)
		{
			Ref<HingeConstraintSettings> hinge = new HingeConstraintSettings;
			Vec3 axis1 = (turn_parent * vec(&b->parent_axes[0])).Normalized();
			Vec3 axis2 = (turn * vec(&b->axes[0])).Normalized();
			Vec3 normal1 = turn_parent * vec(&b->parent_axes[3]);
			Vec3 normal2 = turn * vec(&b->axes[3]);

			normal1 = (normal1 - axis1 * axis1.Dot(normal1)).Normalized();
			normal2 = (normal2 - axis2 * axis2.Dot(normal2)).Normalized();
			hinge->mPoint1 = hinge->mPoint2 = RVec3(pivot);
			hinge->mHingeAxis1 = axis1;
			hinge->mHingeAxis2 = axis2;
			hinge->mNormalAxis1 = normal1;
			hinge->mNormalAxis2 = normal2;
			hinge->mLimitsMin = std::min(0.f, pin_angle(b->limits[0]));
			hinge->mLimitsMax = std::max(0.f, pin_angle(b->limits[1]));
			hinge->mMaxFrictionTorque = b->joint_friction;
			hinge->mNumVelocityStepsOverride = (uint)std::max(0, options->velocity_steps);
			hinge->mNumPositionStepsOverride = (uint)std::max(0, options->position_steps);
			part.mToParent = hinge;
		}
		else
		{
			Ref<SixDOFConstraintSettings> joint = new SixDOFConstraintSettings;
			Vec3 x1 = (turn_parent * vec(&b->parent_axes[0])).Normalized();
			Vec3 x2 = (turn * vec(&b->axes[0])).Normalized();
			Vec3 y1 = turn_parent * vec(&b->parent_axes[3]);
			Vec3 y2 = turn * vec(&b->axes[3]);

			y1 = (y1 - x1 * x1.Dot(y1)).Normalized();
			y2 = (y2 - x2 * x2.Dot(y2)).Normalized();
			joint->mNumVelocityStepsOverride = (uint)std::max(0, options->velocity_steps);
			joint->mNumPositionStepsOverride = (uint)std::max(0, options->position_steps);
			joint->mPosition1 = joint->mPosition2 = RVec3(pivot);
			joint->mAxisX1 = x1;
			joint->mAxisY1 = y1;
			joint->mAxisX2 = x2;
			joint->mAxisY2 = y2;
			joint->mSwingType = ESwingType::Pyramid;
			joint->MakeFixedAxis(SixDOFConstraintSettings::TranslationX);
			joint->MakeFixedAxis(SixDOFConstraintSettings::TranslationY);
			joint->MakeFixedAxis(SixDOFConstraintSettings::TranslationZ);
			for (int axis = 0; axis < 3; axis++)
			{
				SixDOFConstraintSettings::EAxis which = (SixDOFConstraintSettings::EAxis)(SixDOFConstraintSettings::RotationX + axis);
				joint->SetLimitedAxis(which, pin_angle(b->limits[2 * axis]), pin_angle(b->limits[2 * axis + 1]));
				joint->mMaxFriction[which] = b->joint_friction;
			}
			part.mToParent = joint;
		}
	}
	/* (the masses and inertia made steady for the joints - a part no more
	than 1:8 its parent, a parent's inertia at least its children's: Jolt's,
	from Havok's "Stop my constraints from blowing up") */
	settings->Stabilize();
	{
		Array<Mat44> joints(count);
		for (int body = 0; body < count; body++)
			joints[body] = Mat44::sRotationTranslation(quat(in[body].rotation), vec(in[body].position));
		/* (parts joined, or overlapping as it died, never meet) */
		settings->DisableParentChildCollisions(joints.data(), 0.f);
		/* (nor parts two joints or fewer apart - a bone and the one above it,
		two on one joint -: only the limbs meet the trunk and each other, and
		the parts that pinched each other at a joint no longer fight it) */
		GroupFilterTable *table = const_cast<GroupFilterTable *>(
			static_cast<GroupFilterTable const *>(settings->mParts[0].mCollisionGroup.GetGroupFilter()));
		if (table)
		{
			for (int a = 0; a < count; a++)
				for (int b = a + 1; b < count; b++)
				{
					int pa = in[a].parent, pb = in[b].parent;
					if (pa == b || pb == a || (pa >= 0 && pa == pb) || (pb >= 0 && in[pb].parent == a) ||
						(pa >= 0 && in[pa].parent == b))
						table->DisableCollision(a, b);
				}
		}
	}
	settings->CalculateBodyIndexToConstraintIndex();
	settings->CalculateConstraintIndexToBodyIdxPair();
	Ref<Ragdoll> ragdoll = settings->CreateRagdoll(++the_world->ragdoll_groups, 0, &the_world->system);
	if (!ragdoll)
		return -1;
	ragdoll->AddToPhysicsSystem(EActivation::Activate);
	BodyInterface &bodies = the_world->system.GetBodyInterfaceNoLock();
	for (int body = 0; body < count; body++)
		bodies.SetLinearVelocity(ragdoll->GetBodyID(body), vec(in[body].velocity));
	for (size_t slot = 0; slot < the_world->ragdolls.size(); slot++)
	{
		if (!the_world->ragdolls[slot])
		{
			the_world->ragdolls[slot] = ragdoll;
			return (int)slot;
		}
	}
	the_world->ragdolls.push_back(ragdoll);
	return (int)the_world->ragdolls.size() - 1;
}

void physics_jolt_rig_destroy(int ragdoll)
{
	Ragdoll *r = ragdoll_get(ragdoll);
	if (!r)
		return;
	r->RemoveFromPhysicsSystem();
	the_world->ragdolls[ragdoll] = nullptr;
}

void physics_jolt_rig_get_body(int ragdoll, int body, float position[3], float rotation[4])
{
	Ragdoll *r = ragdoll_get(ragdoll);
	if (!r || body < 0 || body >= (int)r->GetBodyCount())
		return;
	RVec3 p;
	Quat q;
	the_world->system.GetBodyInterfaceNoLock().GetPositionAndRotation(r->GetBodyID(body), p, q);
	position[0] = (float)p.GetX();
	position[1] = (float)p.GetY();
	position[2] = (float)p.GetZ();
	rotation[0] = q.GetX();
	rotation[1] = q.GetY();
	rotation[2] = q.GetZ();
	rotation[3] = q.GetW();
}

void physics_jolt_rig_add_velocity(int ragdoll, int body, float const velocity[3])
{
	Ragdoll *r = ragdoll_get(ragdoll);
	if (!r || body < 0 || body >= (int)r->GetBodyCount())
		return;
	BodyInterface &bodies = the_world->system.GetBodyInterfaceNoLock();
	BodyID id = r->GetBodyID(body);
	bodies.SetLinearVelocity(id, bodies.GetLinearVelocity(id) + vec(velocity));
	bodies.ActivateBody(id);
}

int physics_jolt_rig_active(int ragdoll)
{
	Ragdoll *r = ragdoll_get(ragdoll);
	return r && r->IsActive();
}

void physics_jolt_rig_set_active(int ragdoll, int active)
{
	Ragdoll *r = ragdoll_get(ragdoll);
	if (!r)
		return;
	if (active)
		r->Activate();
	else
	{
		BodyInterface &bodies = the_world->system.GetBodyInterfaceNoLock();
		for (BodyID id : r->GetBodyIDs())
			bodies.DeactivateBody(id);
	}
}

}
