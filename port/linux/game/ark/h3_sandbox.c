/*
H3_SANDBOX.C

A Halo 3 level's sandbox: its weapons, grenades and vehicles made from the
level's own MCC map and written into the level's tags (h3_sandbox.h, which
says what it does). A part of h2_level.c (included at its end, after
h2_models_load.inc): the writers it drives are h2_level.c's own - the
models' (model_made_write), the shaders' (level_shader_write,
soso_shader_write), the bitmaps' (bitmap_write) - and h2_weapons.c's
(h2_weapons_write, h2_vehicles_write) on a build of its own with no Halo 2
cache, the Halo 3 readers' (h3_weapons.c, h3_vehicles.c) level mode naming
the map and the scenario's palettes.
*/

#ifdef H2_LEVEL_INTERNAL

#include "h3_sandbox.h"

struct h3_sandbox
{
	struct build *build;
	char const **paths;         /* the palettes' paths, the weapons' then the vehicles' (kept for the readers) */
	long path_count;
	/* the layout, from the sandbox's first tag */
	long weapons_first;         /* h2_weapons.c's tags: the weapons', vehicles', grenades', sounds' */
	long shader_first, soso_first, bitmap_first, model_first, tag_count;
	long characters_first;      /* h3_characters.c's tags (bipeds, collision models, graphs, actors, actor variants) */
	struct h3_sandbox_palettes palettes;
	char const *ai_weapons[32]; /* (the squads' weapons' paths, after the palettes') */
	long soso_count;
	short *shader_soso;         /* each shader's model shader twin (NONE: none) */
	short *soso_shader;         /* each twin's shader */
	boolean grenades;
};

/* the readers' state as h2_weapons_gather and h2_vehicles_gather leave it
before their kinds are tried: nothing made, the kit's tags unknown */
H2_ALIGN static void h3_sandbox_begin_state(void)
{
	long k;

	h2w.count = 0;
	h2w.sound_count = 0;
	h2w.loop_count = 0;
	h2w.ammo_count = 0;
	h2w.light_count = 0;
	for (k = 0; k < H2W_GRENADE_COUNT; k++)
	{
		h2w_model_begin(&h2w.grenades[k]);
		h2w.grenade_scale[k] = 0.0f;
		memset(&h2w.grenade_data[k], 0, sizeof(h2w.grenade_data[k]));
	}
	for (k = 0; k < H2W_POWERUP_COUNT; k++)
	{
		h2w_model_begin(&h2w.powerups[k]);
		h2w.powerup_sound[k] = NONE;
		h2w.powerup_scale[k] = 0.0f;
	}
	h2v.count = 0;
	h2v.kit_rocket = h2v.kit_bolt = h2v.kit_smoke = h2v.kit_fire_explosion = NONE;
	/* (the players' collision damage limits: Halo 2's biped's on a Halo 2
	level; none read here - the stand-in's stay) */
	memset(h2v.splatter, 0, sizeof(h2v.splatter));
	h2v.board_melee = NONE;
	h2v.debris_count = 0;
	h2v.extra_debris_count = 0;
	h2v.blast_count = 0;
	h2v.cache_for_names = NULL;
	h2_vehicle_damage_definitions_clear();
}

struct h3_sandbox *h3_sandbox_gather(char const *maps_directory, char const *name, struct h3_cache *cache,
	struct h3_sandbox_palettes const *palettes, unsigned char *kit)
{
	struct h3_sandbox *sb;
	struct build *build;
	long i, k, weapons_before;
	unsigned long long started = vita_host_time_us();

	if (getenv("HALO_H3_SANDBOX") && !atoi(getenv("HALO_H3_SANDBOX")))
	{
		platform_log("h3 sandbox: off (HALO_H3_SANDBOX=0): the kit's stand-ins");
		return NULL;
	}
	sb = calloc(1, sizeof(*sb));
	build = calloc(1, sizeof(*build));
	if (!sb || !build)
	{
		free(sb);
		free(build);
		return NULL;
	}
	sb->build = build;
	build->multiplayer = TRUE;
	sb->palettes = *palettes;
	/* (a mission's squads' weapons - the scenario's weapon palette -: made too) */
	{
		long ai_count = h3c_weapon_paths(cache, palettes, sb->ai_weapons, (long)(sizeof(sb->ai_weapons) / sizeof(sb->ai_weapons[0])));

		sb->path_count = palettes->weapon_count + palettes->vehicle_count;
		sb->paths = calloc((size_t)(sb->path_count + ai_count + 1), sizeof(*sb->paths));
		if (!sb->paths)
		{
			h3_sandbox_free(sb);
			return NULL;
		}
		for (i = 0; i < palettes->weapon_count; i++)
			sb->paths[i] = palettes->weapons[i];
		for (i = 0; i < palettes->vehicle_count; i++)
			sb->paths[palettes->weapon_count + i] = palettes->vehicles[i];
		for (i = 0; i < ai_count; i++)
		{
			for (k = 0; k < sb->path_count && strcmp(sb->paths[k], sb->ai_weapons[i]); k++)
				;
			if (k == sb->path_count)
				sb->paths[sb->path_count++] = sb->ai_weapons[i];
		}
	}
	h2_build_heap_unit("the Halo 3 level's weapons and vehicles");
	h3_weapons_level_begin(maps_directory, name, sb->paths, sb->path_count, cache);
	h3_sandbox_begin_state();
	h3_weapons_gather(build);
	weapons_before = h2w.count;
	h3_vehicles_gather(build);
	/* (a mission's characters: their models into this build, h3_characters.c) */
	h3c_gather(build, cache, palettes, kit);
	h3_weapons_level_end();
	sb->grenades = h3_weapons_grenades_h3();
	if (!h2w.count && !h2v.count && !sb->grenades && !h3c_tag_count())
	{
		platform_log("h3 sandbox: %s: nothing made from the map: the kit's stand-ins", name);
		h3_sandbox_free(sb);
		return NULL;
	}
	/* the kit's tags the weapons and vehicles are written on */
	h2_weapons_kit_find(kit);
	/* the layout: h2_weapons.c's tags, the shaders, their model shader twins
	(the models' environment shaders as model shaders, as h2_level.c), the
	bitmaps, the models */
	sb->weapons_first = 0;
	sb->shader_first = h2_weapons_tag_count();
	sb->shader_soso = malloc((size_t)(build->shader_count + 1) * sizeof(short));
	sb->soso_shader = malloc((size_t)(build->shader_count + 1) * sizeof(short));
	if (!sb->shader_soso || !sb->soso_shader)
	{
		h3_sandbox_free(sb);
		return NULL;
	}
	for (i = 0; i < build->shader_count; i++)
		sb->shader_soso[i] = NONE;
	for (i = 0; i < build->model_count; i++)
	{
		for (k = 0; k < build->models[i].part_count; k++)
		{
			short shader = build->models[i].parts[k].shader;

			if (shader >= 0 && shader < build->shader_count && build->shaders[shader].group == 'senv' && sb->shader_soso[shader] == NONE)
			{
				sb->shader_soso[shader] = (short)sb->soso_count;
				sb->soso_shader[sb->soso_count++] = shader;
			}
		}
	}
	sb->soso_first = sb->shader_first + build->shader_count;
	sb->bitmap_first = sb->soso_first + sb->soso_count;
	sb->model_first = sb->bitmap_first + build->bitmap_count;
	/* (the desktop's texture cache holds 96 MB, the level's own maps in view
	beside the sandbox's: a sandbox of more than its budget - Avalanche with
	its seven vehicle kinds Halo 3's came to 107 MB, and the level drew red and
	green as the cache thrashed - is gathered once more with its multipurpose
	maps DXT-encoded, a quarter of their bytes. HALO_H3_SANDBOX_BUDGET_MB=n
	the budget, 0 none: as before) */
	{
		extern int h3_spartan_multipurpose_compact;
		char const *setting = getenv("HALO_H3_SANDBOX_BUDGET_MB");
		unsigned long budget = (setting ? (unsigned long)atol(setting) : 84UL) * 1024UL * 1024UL, total = 0;

		for (i = 0; i < build->bitmap_count; i++)
			total += build->bitmaps[i].pixels_size;
		if (budget && total > budget && !h3_spartan_multipurpose_compact)
		{
			struct h3_sandbox *again;

			platform_log("h3 sandbox: %s: its maps come to %lu KB, more than its budget (%lu KB): gathered again, the multipurpose maps "
				"DXT-encoded", name, total / 1024, budget / 1024);
			h3_sandbox_free(sb);
			h3_spartan_multipurpose_compact = 1;
			again = h3_sandbox_gather(maps_directory, name, cache, palettes, kit);
			h3_spartan_multipurpose_compact = 0;
			return again;
		}
	}
	sb->characters_first = sb->model_first + build->model_count;
	sb->tag_count = sb->characters_first + h3c_tag_count();
	platform_log("h3 sandbox: %s: %ld weapons, %ld vehicles%s made from the map: %ld tags (%ld h2_weapons', %ld shaders, %ld model "
		"shaders, %ld bitmaps, %ld models), %.0f ms", name, weapons_before, h2v.count, sb->grenades ? ", the grenades" : "",
		sb->tag_count, sb->shader_first, build->shader_count, sb->soso_count, build->bitmap_count, build->model_count,
		(double)(vita_host_time_us() - started) / 1000.0);
	return sb;
}

long h3_sandbox_tag_count(struct h3_sandbox const *sandbox)
{
	return sandbox ? sandbox->tag_count : 0;
}

long h3_sandbox_buffer_count(struct h3_sandbox const *sandbox)
{
	long i, total = 0;

	if (!sandbox)
		return 0;
	for (i = 0; i < sandbox->build->model_count; i++)
		total += sandbox->build->models[i].part_count;
	return total;
}

void h3_sandbox_tag_name(struct h3_sandbox const *sandbox, long index, unsigned long *group, char *text, long size)
{
	struct build const *build = sandbox->build;

	if (index < sandbox->shader_first)
		h2_weapons_tag_name(index, group, text, size);
	else if (index < sandbox->soso_first)
		*group = build->shaders[index - sandbox->shader_first].group, snprintf(text, (size_t)size, "%s", build->shaders[index - sandbox->shader_first].path);
	else if (index < sandbox->bitmap_first)
		*group = 'soso', snprintf(text, (size_t)size, "%s model", build->shaders[sandbox->soso_shader[index - sandbox->soso_first]].path);
	else if (index < sandbox->model_first)
		*group = 'bitm', snprintf(text, (size_t)size, "%s", build->bitmaps[index - sandbox->bitmap_first].path);
	else if (index < sandbox->characters_first)
		*group = 'mode', snprintf(text, (size_t)size, "h3\\models\\%ld", index - sandbox->model_first);
	else
		h3c_tag_name(index - sandbox->characters_first, group, text, size);
}

void h3_sandbox_tag_parents(unsigned long group, unsigned long *parent, unsigned long *grandparent)
{
	*parent = *grandparent = 0xFFFFFFFFUL;
	switch (group)
	{
	case 'weap':
	case 'eqip':
	case 'garb':
		*parent = 'item';
		*grandparent = 'obje';
		break;
	case 'vehi':
	case 'bipd':
		*parent = 'unit';
		*grandparent = 'obje';
		break;
	case 'mach':
	case 'lifi':
	case 'ctrl':
		*parent = 'devi';
		*grandparent = 'obje';
		break;
	case 'proj':
	case 'scen':
	case 'plac':
	case 'ssce':
		*parent = 'obje';
		break;
	case 'senv':
	case 'soso':
	case 'schi':
	case 'scex':
	case 'swat':
	case 'smet':
	case 'sgla':
	case 'spla':
	case 'sotr':
		*parent = 'shdr';
		break;
	default:
		break;
	}
}

boolean h3_sandbox_write(struct h3_sandbox *sandbox, struct region *r, unsigned long instances, long tag_count, long first,
	unsigned long scenario_root, unsigned long vertex_table, unsigned long index_table, long buffer_first, h3_sandbox_pixels_put put,
	void *context)
{
	struct build *build = sandbox->build;
	long i;
	unsigned long long started = vita_host_time_us();

	/* the names h2_level.c's region_reference gives a reference: this level's
	table's (its own static table is another level's, or none: the tag checker
	then corrects every reference written here to none - the first run's
	vehicles never spawned, the grenades lost their models) */
	free(reference_names);
	reference_names = calloc((size_t)(tag_count + 1), sizeof(*reference_names));
	reference_name_count = reference_names ? tag_count : 0;
	for (i = 0; i < reference_name_count; i++)
		reference_names[i] = read32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * i + 16));

	/* the models: their tags, their parts' buffers at the tables' entries
	after the level's own */
	{
		struct model_made_writer writer;

		memset(&writer, 0, sizeof(writer));
		writer.build = build;
		writer.tags = r;
		writer.vertex_table = vertex_table;
		writer.index_table = index_table;
		writer.vertex_first = buffer_first;
		writer.index_first = buffer_first;
		writer.shader_first = first + sandbox->shader_first;
		writer.soso_first = first + sandbox->soso_first;
		writer.shader_soso = sandbox->shader_soso;
		/* (h3vita) a buffer written once: the Spartan's arms are in every
		first-person model (HALO_H3_SHARE_BUFFERS=0: each its own as before) */
		writer.share_buffers = !getenv("HALO_H3_SHARE_BUFFERS") || atoi(getenv("HALO_H3_SHARE_BUFFERS")) != 0;
		for (i = 0; i < build->model_count && !r->b.failed; i++)
		{
			unsigned long root = model_made_write(&writer, i);

			if (!root)
			{
				platform_log("h3 sandbox: model %ld could not be written", i);
				model_made_writer_end(&writer);
				return FALSE;
			}
			write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (first + sandbox->model_first + i) + 20), r->link + root);
		}
		model_made_writer_end(&writer);
	}
	/* the weapons, grenades and vehicles on their kit stand-ins (and the
	globals' weapon list, the players' start) */
	h3_weapons_level_begin("", "", sandbox->paths, sandbox->path_count, NULL);
	h2_weapons_write(build, r, instances, first + sandbox->weapons_first, first + sandbox->model_first, scenario_root);
	h3_weapons_level_end();
	/* a mission's characters: bipeds, actors, the scenario's encounters */
	if (!h3c_write(build, r, instances, first + sandbox->characters_first, first + sandbox->model_first, first + sandbox->weapons_first,
		scenario_root, &sandbox->palettes))
	{
		platform_log("h3 sandbox: the characters could not be written");
		return FALSE;
	}
	/* the shaders, their model shader twins */
	for (i = 0; i < build->shader_count && !r->b.failed; i++)
		level_shader_write(build, r, instances, first + sandbox->shader_first, i, first + sandbox->bitmap_first);
	for (i = 0; i < sandbox->soso_count && !r->b.failed; i++)
	{
		soso_shader_write(build, r, instances, first + sandbox->soso_first + i, &build->shaders[sandbox->soso_shader[i]],
			first + sandbox->bitmap_first);
	}
	/* the bitmaps: their pixels into the level's file, their tags */
	for (i = 0; i < build->bitmap_count && !r->b.failed; i++)
	{
		struct bitmap *b = &build->bitmaps[i];
		unsigned long root = region_alloc(r, 108);
		unsigned short flags = b->ce_flags ? b->ce_flags : b->format >= 14 && b->format <= 16 ? 0x83 : 0x89;

		if (r->b.failed)
			break;
		if (b->pixels && b->pixels_size)
		{
			b->file_offset = put(context, b->pixels, b->pixels_size);
			b->size = b->pixels_size;
		}
		else if (b->pixels_put && b->size)
		{
			/* (h3_sandbox_pixels_flush put them) */
		}
		else
		{
			platform_log("h3 sandbox: bitmap %s has no pixels (%s): left empty", b->path, b->described ? "described" : "none made");
			b->file_offset = 0;
			b->size = 0;
		}
		if (getenv("HALO_H3_DEBUG"))
		{
			platform_log("h3 sandbox: bitmap %ld: %s: format %d %dx%d mips %d, %lu KB", i, b->path, (int)b->format, (int)b->width, (int)b->height,
				(int)b->mipmaps, b->size / 1024);
		}
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (first + sandbox->bitmap_first + i) + 20), r->link + root);
		bitmap_write(r, root, tag_id(first + sandbox->bitmap_first + i), b->width, b->height, b->format, flags, b->mipmaps, b->file_offset,
			b->size, (unsigned short)((b->type << 8) | b->usage));
	}
	if (r->b.failed)
	{
		platform_log("h3 sandbox: out of room for its tags");
		return FALSE;
	}
	platform_log("h3 sandbox: written: %ld tags from %ld, %ld models on buffers from %ld, %.0f ms", sandbox->tag_count, first,
		build->model_count, buffer_first, (double)(vita_host_time_us() - started) / 1000.0);
	return TRUE;
}

void h3_sandbox_pixels_flush(struct h3_sandbox *sandbox, h3_sandbox_pixels_put put, void *context)
{
	struct build *build = sandbox->build;
	unsigned long bytes = 0;
	long i, count = 0;

	for (i = 0; i < build->bitmap_count; i++)
	{
		struct bitmap *b = &build->bitmaps[i];

		if (!b->pixels || !b->pixels_size)
			continue;
		b->file_offset = put(context, b->pixels, b->pixels_size);
		b->size = b->pixels_size;
		b->pixels_put = TRUE;
		free(b->pixels);
		b->pixels = NULL;
		bytes += b->pixels_size;
		b->pixels_size = 0;
		count++;
	}
	platform_log("h3 sandbox: %ld bitmaps' pixels (%lu KB) into the level's file before its tags", count, bytes / 1024);
}

long h3_sandbox_weapon(struct h3_sandbox const *sandbox, char const *h3_path)
{
	long made;

	if (!sandbox)
		return NONE;
	made = h3_weapons_for(h3_path);
	return made != NONE ? sandbox->weapons_first + made : NONE;
}

long h3_sandbox_vehicle(struct h3_sandbox const *sandbox, char const *h3_path)
{
	long made;

	if (!sandbox)
		return NONE;
	made = h3_vehicles_for(h3_path);
	return made != NONE ? sandbox->weapons_first + h2w.count * H2W_TAGS_PER_WEAPON + made : NONE;
}

boolean h3_sandbox_friendly_start(struct h3_sandbox const *sandbox, float *start)
{
	return sandbox != NULL && h3c_friendly_start(start);
}

boolean h3_sandbox_grenades_made(struct h3_sandbox const *sandbox)
{
	return sandbox && sandbox->grenades;
}

void h3_sandbox_free(struct h3_sandbox *sandbox)
{
	if (!sandbox)
		return;
	h2_weapons_dispose();
	h3c_free();
	models_load_weapons_build_free(sandbox->build);
	free(sandbox->build);
	free(sandbox->paths);
	free(sandbox->shader_soso);
	free(sandbox->soso_shader);
	free(sandbox);
}

#else

/* (built alone: nothing - h2_level.c compiles it) */
typedef int h3_sandbox_compiled_in_h2_level;

#endif
