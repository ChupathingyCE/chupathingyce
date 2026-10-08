/*
CACHE_FILES.C

symbols in this file:
001A9180 00f0:
	_cache_files_map_directory (0000)
001A9270 0030:
	_scenario_tags_unload (0000)
001A92A0 0010:
	_tag_files_open (0000)
001A92B0 0010:
	_tag_files_close (0000)
001A92C0 0040:
	_tag_groups_checksum (0000)
001A9300 0010:
	_cache_files_get_checksum (0000)
001A9310 00b0:
	_tag_loaded (0000)
001A93C0 0020:
	_cache_files_enable_writes (0000)
001A93E0 0090:
	_cache_files_disable_writes (0000)
001A9470 0020:
	_tag_block_resize (0000)
001A9490 0020:
	_tag_data_resize (0000)
001A94B0 0020:
	_tag_block_add_element (0000)
001A94D0 0010:
	_tag_block_delete_element (0000)
001A94E0 0020:
	_tag_load (0000)
001A9500 0010:
	_tag_unload (0000)
001A9510 0020:
	_tag_file_get_path (0000)
001A9530 0010:
	_tag_reference_set (0000)
001A9540 0020:
	_tag_iterator_new (0000)
001A9560 0070:
	_tag_iterator_next (0000)
001A95D0 00f0:
	_cache_get_tag_instance (0000)
001A96C0 0100:
	_cache_file_header_verify (0000)
001A97C0 0090:
	_cache_files_give_time_to_precache (0000)
001A9850 0130:
	_scenario_tags_load (0000)
001A9980 0120:
	_scenario_structure_bsp_load (0000)
001A9AA0 0080:
	_scenario_structure_bsp_unload (0000)
001A9B20 00b0:
	_tag_get (0000)
001A9BD0 0020:
	_tag_get_name (0000)
001A9BF0 0020:
	_tag_get_group_tag (0000)
002A62D8 0009:
	??_C@_08NDLPNBDL@d?3?2maps?2?$AA@ (0000)
002A62E4 000c:
	??_C@_0M@KPLLEAGM@d?3?2maps_it?2?$AA@ (0000)
002A62F0 000c:
	??_C@_0M@OACCOJFB@d?3?2maps_es?2?$AA@ (0000)
002A62FC 000c:
	??_C@_0M@PDFFCMII@d?3?2maps_fr?2?$AA@ (0000)
002A6308 000c:
	??_C@_0M@EADFFBPG@d?3?2maps_de?2?$AA@ (0000)
002A6314 001e:
	??_C@_0BO@JHMCCGLN@no?5valid?5map?5directory?5exists?$AA@ (0000)
002A6334 0023:
	??_C@_0CD@DIDKODIK@c?3?2halo?2SOURCE?2cache?2cache_files@ (0000)
002A6358 001f:
	??_C@_0BP@GJKOKCFG@cache_file_globals?4tags_loaded?$AA@ (0000)
002A6378 0015:
	??_C@_0BF@KAIMLJJI@global_tag_instances?$AA@ (0000)
002A6390 003d:
	??_C@_0DN@EPHHKDMF@tag_block_resize?$CI?$CJ?5is?5not?5suppor@ (0000)
002A63D0 003c:
	??_C@_0DM@FNPDMPEA@tag_data_resize?$CI?$CJ?5is?5not?5support@ (0000)
002A6410 0042:
	??_C@_0EC@MOKCAFPK@tag_block_add_element?$CI?$CJ?5is?5not?5s@ (0000)
002A6458 0045:
	??_C@_0EF@NIGPCFB@tag_block_delete_element?$CI?$CJ?5is?5no@ (0000)
002A64A0 0035:
	??_C@_0DF@IIJGOBIP@tag_load?$CI?$CJ?5is?5not?5supported?5with@ (0000)
002A64D8 0037:
	??_C@_0DH@OJBEKPBJ@tag_unload?$CI?$CJ?5is?5not?5supported?5wi@ (0000)
002A6510 003e:
	??_C@_0DO@CMGIBDCM@tag_file_get_path?$CI?$CJ?5is?5not?5suppo@ (0000)
002A6550 003e:
	??_C@_0DO@PNBFAACM@tag_reference_set?$CI?$CJ?5is?5not?5suppo@ (0000)
002A6590 0022:
	??_C@_0CC@JDEMIPEM@i?5don?8t?5think?5?$CF08x?5is?5a?5tag?5inde@ (0000)
002A65B4 0028:
	??_C@_0CI@MCDCHEHF@?8?$CFs?8?5does?5not?5appear?5to?5be?5a?5cac@ (0000)
002A65DC 0036:
	??_C@_0DG@MBMNLMG@the?5cache?5file?5?8?$CFs?8?5belongs?5to?5a@ (0000)
002A6614 0026:
	??_C@_0CG@GAKJDLAF@the?5cache?5file?5?8?$CFs?8?5is?5an?5old?5ve@ (0000)
002A663C 002e:
	??_C@_0CO@BLPPGPI@signature?5is?5?8?$CFc?$CFc?$CFc?$CFc?8?0?5should?5@ (0000)
002A666C 002b:
	??_C@_0CL@BCHNHKGI@tag_instance?9?$DOgroup_tag?$DN?$DNSTRUCTU@ (0000)
002A6698 001c:
	??_C@_0BM@EFCFDCHK@?$CBtag_instance?9?$DObase_address?$AA@ (0000)
002A66B8 005e:
	??_C@_0FO@FEJCGGNA@cache_file_globals?4structure_bsp@ (0000)
002A6718 001b:
	??_C@_0BL@OICFEJJN@tag_instance?9?$DObase_address?$AA@ (0000)
002A6734 0027:
	??_C@_0CH@HHANGOKG@can?8t?5get?$CI?$CJ?5a?5tag?5with?5a?5base?5ad@ (0000)
002A675C 002e:
	??_C@_0CO@IDEKIPEG@expected?5tag?5group?5?8?$CFs?8?5but?5got?5@ (0000)
00316820 0018:
	_data_00316820 (0000)
004CCB20 080c:
	_bss_004ccb20 (0000)
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries_windows.h"
#include "errors.h"
#include "tag_files/tag_groups.h"
#include "tag_files/files.h"
#include "cache_files.h"
#include "physical_memory_map.h"
#include "sound_cache.h"
#include "texture_cache.h"
#include "interface/ui_widget.h"
#include "scenario/scenario_definitions.h"
#include "sound/sound_manager.h"
#include "tag_schema.h"
#include "halo_map_families.h"
#include "models/models.h"

/* ---------- constants */

enum
{
	/* port: a vertex or index buffer in a cache file (a D3DResource: Common,
	Data, Lock), as cache_files_disable_writes counts them */
	CACHE_FILE_BUFFER_SIZE = 12,
	/* port: what cache_file_read rounds a read's size up to a multiple of
	(cache_files_windows.c) */
	CACHE_FILE_SECTOR_SIZE = 512,
};

/* ---------- macros */

#define STRUCTURE_BSP_TAG 'sbsp'
#define CACHE_FILE_TAG_HEADER_SIGNATURE 'tags'
#define CACHE_FILE_STRUCTURE_BSP_HEADER_SIGNATURE 'sbsp'
#define CACHE_FILE_HEADER_SIGNATURE 'head'
#define CACHE_FILE_FOOTER_SIGNATURE 'foot'

/* ---------- structures */

struct cache_file_tag_instance
{
	long group_tag;
	long parent_group_tags[2];
	long tag_index;
	/* read from the cache file: Xbox addresses */
	XPTR(char) name;
	XPTR(void) base_address;
	unsigned long unused[2];
};

struct cache_file_tag_header
{
	/* read from the cache file: Xbox addresses */
	XPTR(struct cache_file_tag_instance) tag_instances;
	long scenario_tag_index;
	unsigned long checksum;
	long tag_count;
	long vertex_buffer_count;
	XPTR(void) vertex_buffers;
	long index_buffer_count;
	XPTR(void) index_buffers;
	unsigned long signature;
};

struct cache_file_structure_bsp_header
{
	/* read from the cache file: Xbox addresses */
	XPTR(void) base_address;
	long vertex_buffer_count;
	XPTR(void) vertex_buffers;
	long index_buffer_count;
	XPTR(void) index_buffers;
	unsigned long signature;
};

#ifdef HALO_CUSTOM_EDITION
/* port: Custom Edition maps (Halo PC's): their version, their tag cache
(platform.h), and their tag header, which has the models' vertices and
indices in one block of the file rather than Direct3D buffers */
#define CACHE_FILE_CE_VERSION 609
/* (and Halo PC retail's, version 7: HaloMD's maps, played as <name>@md and
read as Custom Edition's are, their stock bitmaps' and sounds' data from
Custom Edition's resource maps, port/linux/game/ce_resources.c) */
#define CACHE_FILE_RETAIL_VERSION 7
#define CACHE_FILE_VERSION_IS_PC(version) \
	((version) == CACHE_FILE_CE_VERSION || (version) == CACHE_FILE_RETAIL_VERSION)
#define CE_TAG_CACHE_BASE 0x40440000U
#define CE_TAG_CACHE_SIZE 0x01700000U

struct cache_file_ce_tag_header
{
	XPTR(struct cache_file_tag_instance) tag_instances;
	long scenario_tag_index;
	unsigned long checksum;
	long tag_count;
	long model_part_count;
	long model_data_file_offset;
	long model_part_count_again;
	long vertex_data_size;
	long model_data_size;
	unsigned long signature;
};

/* (whether the map being loaded, or loaded, is one: tag_groups.c) */
boolean cache_file_is_ce;

#endif

struct cache_file_header
{
	unsigned long header_signature;
	long version;
	long file_length;
	byte reservedC[4];
	long tag_data_offset;
	long tag_data_size;
	byte reserved18[8];
	char name[0x20];
	char build[0x20];
	byte reserved60[4];
	unsigned long checksum;
	byte reserved68[0x794];
	unsigned long footer_signature;
};

struct cache_file_globals
{
	boolean tags_loaded;
	byte pad1[3];
	struct cache_file_header header;
	struct cache_file_tag_header *tag_header;
	struct cache_file_structure_bsp_header *structure_bsp_header;
};

typedef char verify_cache_file_tag_instance_size[
	sizeof(struct cache_file_tag_instance) == 0x20 ? 1 : -1];

typedef char verify_cache_file_tag_header_count_offset[
	offsetof(struct cache_file_tag_header, tag_count) == 0xC ? 1 : -1];

#ifndef HALO_64BIT
typedef char verify_cache_file_globals_size[
	sizeof(struct cache_file_globals) == 0x80C ? 1 : -1];
#endif
typedef char verify_cache_file_header_size[
	sizeof(struct cache_file_header) == 0x800 ? 1 : -1];

/* ---------- prototypes */

static struct cache_file_tag_instance *cache_get_tag_instance(
	long tag_index);
static struct cache_file_tag_instance *cache_empty_tag_instance(
	long tag_index);
static boolean cache_file_region_contains(
	void const *region,
	unsigned long region_size,
	void const *address,
	long count,
	long element_size);
static boolean cache_file_tag_header_verify(
	struct cache_file_tag_header *tag_header,
	long tag_data_size,
	char const *scenario_name);
static boolean cache_file_structure_bsp_reference_verify(
	struct scenario_structure_bsp_reference *reference);

/* ---------- globals */

static struct cache_file_globals cache_file_globals = { 0 };
extern struct cache_file_tag_instance *global_tag_instances;
static char const *data_00316820[] =
{
	"d:\\maps_de\\",
	"d:\\maps_fr\\",
	"d:\\maps_es\\",
	"d:\\maps_it\\",
	"d:\\maps\\",
	NULL
};

/* ---------- private code */

static struct cache_file_tag_instance *cache_get_tag_instance(
	long tag_index)
{
	short absolute_index;
	struct cache_file_tag_instance *tag_instance;

	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		518,
		cache_file_globals.tags_loaded);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		519,
		global_tag_instances);

	absolute_index = (short)tag_index;
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		522,
		absolute_index >= 0 && absolute_index < cache_file_globals.tag_header->tag_count,
		csprintf(temporary, "i don't think %08x is a tag index", tag_index));
	/* port: an index that is not a tag (NONE, or one a map's data gave
	that nothing checked) is the empty tag, not whatever lies around the
	tag table */
	if (!cache_file_globals.tags_loaded || !global_tag_instances ||
		absolute_index < 0 || absolute_index >= cache_file_globals.tag_header->tag_count)
	{
		return cache_empty_tag_instance(tag_index);
	}

	tag_instance = &global_tag_instances[absolute_index];
#ifdef HALO_CUSTOM_EDITION
	/* port: a Custom Edition or HaloMD map's references may have the salt
	of another build of its tags (HaloMD's maps, edited by tools that
	renumbered them, do): Halo PC's engine took the index alone, as this
	does for such maps only (tag_get still checks the group). An Xbox map's
	are asserted as they always were */
	if (cache_file_is_ce)
		return tag_instance;
#endif
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		526,
		!(tag_index & 0xFFFF0000) || tag_instance->tag_index == tag_index,
		csprintf(temporary, "i don't think %08x is a tag index", tag_index));
	if ((tag_index & 0xFFFF0000) && tag_instance->tag_index != tag_index)
		return cache_empty_tag_instance(tag_index);

	return tag_instance;
}

/* port: the tag that a tag index that is not one gets (cache_get_tag_instance,
tag_get): no group, no name, and data that is all zeros, which whatever
reads it reads as an empty tag of its group (no elements in its blocks,
no tags referenced), and whatever writes it writes nowhere that matters.
It is zeroed again each time it is given. Logged once */
static struct cache_file_tag_instance *cache_empty_tag_instance(
	long tag_index)
{
	static struct cache_file_tag_instance empty_tag_instance;
	static boolean logged = FALSE;

	if (!logged)
	{
		logged = TRUE;
		error(_error_silent, "%08lx is not a tag index: an empty tag is used", (unsigned long)tag_index);
	}
	csmemset(&empty_tag_instance, 0, sizeof(empty_tag_instance));
	empty_tag_instance.tag_index = NONE;
	empty_tag_instance.group_tag = NONE;
	empty_tag_instance.parent_group_tags[0] = NONE;
	empty_tag_instance.parent_group_tags[1] = NONE;
#ifdef HALO_64BIT
	/* (Xbox addresses: an empty name in the Xbox address space) */
	{
		static char *empty_name;

		if (!empty_name)
			empty_name = malloc(1);
		if (empty_name)
			empty_name[0] = 0;
		empty_tag_instance.name = XBOX_ADDRESS(empty_name);
	}
	empty_tag_instance.base_address = XBOX_ADDRESS(tag_empty_data());
#else
	empty_tag_instance.name = "";
	empty_tag_instance.base_address = tag_empty_data();
#endif

	return &empty_tag_instance;
}

/* port: whether count elements of element_size bytes at address all lie in
the region_size bytes at region (no elements always do): a map's pointers
and counts are checked so before anything follows them */
static boolean cache_file_region_contains(
	void const *region,
	unsigned long region_size,
	void const *address,
	long count,
	long element_size)
{
	unsigned long offset = (unsigned long)(POINTER_BITS(address) - POINTER_BITS(region));

	if (count == 0)
		return TRUE;

	return count > 0 &&
		POINTER_BITS(address) >= POINTER_BITS(region) &&
		offset <= region_size &&
		(unsigned long)count <= (region_size - offset) / (unsigned long)element_size;
}

/* port: whether the tag header of the tags just read (tag_data_size bytes
at the tag cache's base) can be trusted, as everything after trusts it:
its tag table and vertex and index buffers lie in the tag data, its tags'
count fits a tag index's absolute index, and it names a scenario tag */
static boolean cache_file_tag_header_verify(
	struct cache_file_tag_header *tag_header,
	long tag_data_size,
	char const *scenario_name)
{
	char const *problem = NULL;

	if (tag_header->signature != CACHE_FILE_TAG_HEADER_SIGNATURE)
	{
		problem = "signature";
	}
	else if (tag_header->tag_count <= 0 || tag_header->tag_count > UNSIGNED_SHORT_MAX)
	{
		problem = "tag count";
	}
	else if (!cache_file_region_contains(
		tag_header,
		tag_data_size,
		xbox_pointer(tag_header->tag_instances),
		tag_header->tag_count,
		sizeof(struct cache_file_tag_instance)))
	{
		problem = "tag table";
	}
	else if (!cache_file_region_contains(
		tag_header,
		tag_data_size,
		xbox_pointer(tag_header->vertex_buffers),
		tag_header->vertex_buffer_count,
		CACHE_FILE_BUFFER_SIZE))
	{
		problem = "vertex buffers";
	}
	else if (!cache_file_region_contains(
		tag_header,
		tag_data_size,
		xbox_pointer(tag_header->index_buffers),
		tag_header->index_buffer_count,
		CACHE_FILE_BUFFER_SIZE))
	{
		problem = "index buffers";
	}
	else
	{
		struct cache_file_tag_instance *tag_instances = xbox_pointer(tag_header->tag_instances);
		long scenario_absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(tag_header->scenario_tag_index);
		long absolute_index;

		if (scenario_absolute_index >= tag_header->tag_count ||
			tag_instances[scenario_absolute_index].tag_index != tag_header->scenario_tag_index ||
			tag_instances[scenario_absolute_index].group_tag != SCENARIO_TAG)
		{
			problem = "scenario tag";
		}

		/* port: each tag's data lies in the tag cache, or there is none yet
		(a structure bsp's, set as it loads). Every tag_get goes by these.
		The port's own tags (menu_tags.c) are added after this, and may lie
		elsewhere */
		for (absolute_index = 0;
			!problem && absolute_index < tag_header->tag_count;
			absolute_index++)
		{
			void const *base_address = tag_instances[absolute_index].base_address ?
				xbox_pointer(tag_instances[absolute_index].base_address) : NULL;

			if (base_address &&
				!cache_file_region_contains(tag_header, TAG_CACHE_SIZE, base_address, 1, 1))
			{
				problem = "tag data address";
			}
		}
	}

	if (problem)
	{
		error(_error_silent, "the cache file '%s' is damaged: its tag header's %s is wrong", scenario_name, problem);

		return FALSE;
	}

	return TRUE;
}

/* port: whether a structure bsp reference (the scenario's) may be loaded:
its bytes lie in the map and fit the tag cache after the tag data, where
they are read to (rounded up to whole sectors, as the read is), and it
names a structure bsp tag */
static boolean cache_file_structure_bsp_reference_verify(
	struct scenario_structure_bsp_reference *reference)
{
	byte *tag_cache_base_address = physical_memory_get_tag_cache_base_address();
	long tag_data_size = cache_file_globals.header.tag_data_size;
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(reference->structure_bsp.index);
	long read_size;

	if (reference->file_offset < 0 ||
		reference->file_size < (long)sizeof(struct cache_file_structure_bsp_header) ||
		reference->file_size > TAG_CACHE_SIZE ||
		reference->file_offset > cache_file_globals.header.file_length - reference->file_size)
	{
		error(
			_error_silent,
			"a structure bsp is damaged: %08x bytes at %08x, in %08x bytes",
			reference->file_size,
			reference->file_offset,
			cache_file_globals.header.file_length);

		return FALSE;
	}

	read_size = (reference->file_size + CACHE_FILE_SECTOR_SIZE - 1) & ~(CACHE_FILE_SECTOR_SIZE - 1);
	if (!cache_file_region_contains(
		tag_cache_base_address + tag_data_size,
		TAG_CACHE_SIZE - tag_data_size,
		xbox_pointer(reference->base_address),
		read_size,
		1))
	{
		error(
			_error_silent,
			"a structure bsp is damaged: its %08x bytes at %08x would load to %08lx, outside the tag cache",
			reference->file_size,
			reference->file_offset,
			(unsigned long)reference->base_address);

		return FALSE;
	}

	if (reference->structure_bsp.index == NONE ||
		absolute_index >= cache_file_globals.tag_header->tag_count ||
		global_tag_instances[absolute_index].tag_index != reference->structure_bsp.index ||
		global_tag_instances[absolute_index].group_tag != STRUCTURE_BSP_TAG)
	{
		error(
			_error_silent,
			"a structure bsp is damaged: %08x is not a structure bsp tag",
			reference->structure_bsp.index);

		return FALSE;
	}

	return TRUE;
}

/* ---------- public code */

#ifdef HALO_CUSTOM_EDITION
/* whether the loaded map is a Custom Edition map: its bitmaps' pixels are laid
out as Halo PC's (xbox_texture_cache.c) */
boolean cache_file_tags_are_ce(
	void)
{
	return cache_file_globals.tags_loaded && cache_file_is_ce;
}
#endif

/* port: the tag cache the map's tags are read into, and its size: a Custom
Edition map's is its own, larger (CE_TAG_CACHE_BASE), the others' the
Xbox's (physical_memory_map.c) */
void *cache_files_tag_cache(
	unsigned long *size)
{
#ifdef HALO_CUSTOM_EDITION
	if (cache_file_is_ce)
	{
		*size = CE_TAG_CACHE_SIZE;

		return xbox_pointer(CE_TAG_CACHE_BASE);
	}
#endif
	*size = TAG_CACHE_SIZE;

	return physical_memory_get_tag_cache_base_address();
}

char const *cache_files_map_directory(
	void)
{
	char const *map_directory;
	struct file_reference reference;
	long directory_index;

	switch (XGetLanguage())
	{
	case XC_LANGUAGE_GERMAN:
		map_directory = "d:\\maps_de\\";
		break;
	case XC_LANGUAGE_FRENCH:
		map_directory = "d:\\maps_fr\\";
		break;
	case XC_LANGUAGE_SPANISH:
		map_directory = "d:\\maps_es\\";
		break;
	case XC_LANGUAGE_ITALIAN:
		map_directory = "d:\\maps_it\\";
		break;
	default:
		map_directory = "d:\\maps\\";
		break;
	}

	if (!file_exists(file_reference_create_from_path(&reference, map_directory, TRUE)))
	{
		for (directory_index = 0; data_00316820[directory_index]; directory_index++)
		{
			if (file_exists(file_reference_create_from_path(
				&reference,
				data_00316820[directory_index],
				TRUE)))
			{
				map_directory = data_00316820[directory_index];
				break;
			}
		}

		match_vassert(
			"c:\\halo\\SOURCE\\cache\\cache_files.c",
			60,
			data_00316820[directory_index],
			"no valid map directory exists");
	}

	return map_directory;
}

void scenario_tags_unload(
	void)
{
	/* port: the high-res HUD forgets this map's bitmaps (port/linux/game/hud_hires_tags.c) */
	{
		extern void hud_hires_tags_unloaded(void);

		hud_hires_tags_unloaded();
	}
	sound_cache_close();
	texture_cache_close();
	/* port: the menus' tags go, and the map's own table comes back
	(port/linux/game/menu_tags.c): after the texture cache, which writes to
	the bitmaps it has loaded as it closes, theirs among them */
	{
		extern void menu_tags_unloaded(void);

		menu_tags_unloaded();
	}
	cache_file_close();
#ifdef HALO_CUSTOM_EDITION
	/* (a Custom Edition map registered no Direct3D buffers) */
	if (cache_file_is_ce)
	{
		extern void ce_resources_tags_unloaded(void);
		extern void ce_models_tags_unloaded(void);
		extern void ce_hud_tags_unloaded(void);

		ce_resources_tags_unloaded();
		ce_models_tags_unloaded();
		ce_hud_tags_unloaded();
	}
	else
#endif
	tags_header_deregister_vertex_and_index_buffers(cache_file_globals.tag_header);
	cache_file_globals.tags_loaded = FALSE;
	global_tag_instances = NULL;

	return;
}

/* port: the loaded tags' table, and its tags' count, for the menus' tags
(port/linux/game/menu_tags.c), which a copy with theirs added replaces */
void *cache_files_tag_instances(
	long *count)
{
	*count = cache_file_globals.tags_loaded ? cache_file_globals.tag_header->tag_count : 0;
	return cache_file_globals.tags_loaded ? xbox_pointer(cache_file_globals.tag_header->tag_instances) : NULL;
}

void cache_files_set_tag_instances(
	void *instances,
	long count)
{
	cache_file_globals.tag_header->tag_instances = XBOX_ADDRESS(instances);
	cache_file_globals.tag_header->tag_count = count;
	global_tag_instances = instances;
}

void tag_files_open(
	void)
{
	cache_files_initialize();

	return;
}

void tag_files_close(
	void)
{
	cache_files_dispose();

	return;
}

unsigned long cache_files_get_checksum(
	void)
{
	return cache_file_globals.header.checksum;
}

unsigned long tag_groups_checksum(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		327,
		cache_file_globals.tags_loaded);

	return cache_file_globals.tag_header->checksum;
}

long tag_loaded(
	long group_tag,
	char const *name)
{
	/* port: a long, as the tags' count is (a short wrapped on a count past
	0x7FFF, and the walk never ended) */
	long absolute_index;
	long result = NONE;

	if (cache_file_globals.tags_loaded)
	{
		match_assert(
			"c:\\halo\\SOURCE\\cache\\cache_files.c",
			346,
			global_tag_instances);

		for (absolute_index = 0;
			absolute_index < cache_file_globals.tag_header->tag_count;
			absolute_index++)
		{
			if (group_tag == global_tag_instances[absolute_index].group_tag &&
				!_stricmp(name, xbox_pointer(global_tag_instances[absolute_index].name)))
			{
				result = global_tag_instances[absolute_index].tag_index;
				break;
			}
		}
	}

	return result;
}

void cache_files_enable_writes(
	void)
{
	XPhysicalProtect((void *)0x803A6000, 0x01600000, PAGE_READWRITE);

	return;
}

void cache_files_disable_writes(
	void)
{
	XPhysicalProtect((void *)0x803A6000, 0x01600000, PAGE_READONLY);
	XPhysicalProtect(
		xbox_pointer(cache_file_globals.tag_header->vertex_buffers),
		cache_file_globals.tag_header->vertex_buffer_count * 12,
		PAGE_READWRITE);
	XPhysicalProtect(
		xbox_pointer(cache_file_globals.tag_header->index_buffers),
		cache_file_globals.tag_header->index_buffer_count * 12,
		PAGE_READWRITE);

	if (cache_file_globals.structure_bsp_header)
	{
		XPhysicalProtect(
			xbox_pointer(cache_file_globals.structure_bsp_header->vertex_buffers),
			cache_file_globals.structure_bsp_header->vertex_buffer_count * 12,
			PAGE_READWRITE);
		XPhysicalProtect(
			xbox_pointer(cache_file_globals.structure_bsp_header->index_buffers),
			cache_file_globals.structure_bsp_header->index_buffer_count * 12,
			PAGE_READWRITE);
	}

	return;
}

boolean tag_block_resize(
	struct tag_block *block,
	long count)
{
	error(_error_silent, "tag_block_resize() is not supported with a cache file active");

	return FALSE;
}

boolean tag_data_resize(
	struct tag_data *data,
	long size)
{
	error(_error_silent, "tag_data_resize() is not supported with a cache file active");

	return FALSE;
}

long tag_block_add_element(
	struct tag_block *block)
{
	error(_error_silent, "tag_block_add_element() is not supported with a cache file active");

	return NONE;
}

void tag_block_delete_element(
	struct tag_block *block,
	long element_index)
{
	error(_error_silent, "tag_block_delete_element() is not supported with a cache file active");

	return;
}

long tag_load(
	long group_tag,
	char const *name,
	unsigned long flags)
{
	error(_error_silent, "tag_load() is not supported with a cache file active");

	return NONE;
}

void tag_unload(
	long tag_index)
{
	error(_error_silent, "tag_unload() is not supported with a cache file active");

	return;
}

void tag_file_get_path(
	long group_tag,
	char const *name,
	char *path)
{
	error(_error_silent, "tag_file_get_path() is not supported with a cache file active");
	path[0] = 0;

	return;
}

void tag_reference_set(
	struct tag_reference *reference,
	unsigned long group_tag,
	char const *name)
{
	error(_error_silent, "tag_reference_set() is not supported with a cache file active");

	return;
}

void tag_iterator_new(
	struct tag_iterator *iterator,
	long group_tag)
{
	iterator->absolute_index = 0;
	iterator->group_tag = group_tag;

	return;
}

long tag_iterator_next(
	struct tag_iterator *iterator)
{
	long result = NONE;

	while (iterator->absolute_index < cache_file_globals.tag_header->tag_count)
	{
		struct cache_file_tag_instance *tag_instance =
			&global_tag_instances[iterator->absolute_index++];

		if (tag_instance &&
			(iterator->group_tag == NONE ||
			iterator->group_tag == tag_instance->group_tag ||
			iterator->group_tag == tag_instance->parent_group_tags[0] ||
			iterator->group_tag == tag_instance->parent_group_tags[1]))
		{
			result = tag_instance->tag_index;
			break;
		}
	}

	return result;
}

boolean cache_file_header_verify(
	struct cache_file_header *header,
	char const *scenario_name,
	boolean fatal)
{
	if (header->header_signature != CACHE_FILE_HEADER_SIGNATURE ||
		header->footer_signature != CACHE_FILE_FOOTER_SIGNATURE ||
		header->file_length < 0 ||
		(header->file_length > 0x11600000
#ifdef HALO_CUSTOM_EDITION
			/* port: but a Halo PC map, which is read where it is, every
			offset in it checked against its file (ce_map_checks.c): it may
			be larger than the Xbox's largest (the_Cage's is 346 MB) */
			&& !CACHE_FILE_VERSION_IS_PC(header->version)
#endif
		) ||
		csstrlen(header->name) > 31)
	{
		if (fatal)
		{
			match_vassert(
				"c:\\halo\\SOURCE\\cache\\cache_files.c",
				544,
				FALSE,
				csprintf(temporary, "'%s' does not appear to be a cache file", scenario_name));
		}

		return FALSE;
	}

	/* port: a Custom Edition map (Halo PC's, version 609: cache_files_windows.c) */
	if (header->version != 5
#ifdef HALO_CUSTOM_EDITION
		&& !CACHE_FILE_VERSION_IS_PC(header->version)
#endif
		)
	{
		if (fatal)
		{
			match_vassert(
				"c:\\halo\\SOURCE\\cache\\cache_files.c",
				548,
				FALSE,
				csprintf(temporary, "the cache file '%s' is an old version", scenario_name));
		}

		return FALSE;
	}

	/* port: the map holds at least its header, and its tag data lies in it
	and fits the tag cache, which it is read into whole (a size rounded up
	to whole sectors still fits, the cache being whole sectors). Checked
	without overflow: the offset and size are each checked first. A
	Custom Edition map's goes in a larger tag cache of its own, and is
	checked further against its file before it is opened: ce_map_checks.c */
	if (header->file_length < (long)sizeof(struct cache_file_header) ||
		header->tag_data_offset < 0 ||
		header->tag_data_size < 0 ||
#ifdef HALO_CUSTOM_EDITION
		header->tag_data_size > (CACHE_FILE_VERSION_IS_PC(header->version) ? (long)CE_TAG_CACHE_SIZE : TAG_CACHE_SIZE) ||
#else
		header->tag_data_size > TAG_CACHE_SIZE ||
#endif
		header->tag_data_offset > header->file_length - header->tag_data_size)
	{
		error(
			_error_silent,
			"the cache file '%s' is damaged: %08x bytes of tag data at %08x, in %08x bytes",
			scenario_name,
			header->tag_data_size,
			header->tag_data_offset,
			header->file_length);

		return FALSE;
	}

	return TRUE;
}

/* port: the builds of the released maps, by region. Any build here plays
multiplayer with the others; a map of another build may differ in what
machines send each other, so its players cannot open the multiplayer menu
(ui_widget.c, ui_widget_launch_widget). A PAL build's maps are played as the
NTSC maps are (port/linux/game/pal_tags.c) */
static struct
{
	char const *build;
	char const *region;
} const cache_file_builds[] =
{
	{ "01.01.14.2342", "PAL" },
	{ "01.10.12.2276", "NTSC" },
	{ "01.08.15.1749", "NTSC" },
};

/* the region of a build's maps ("PAL" or "NTSC") if it is listed above, else
NULL; build is a cache file header's (which need not end it) */
char const *cache_files_build_region(
	char const *build)
{
	short index;

	for (index = 0; index < NUMBEROF(cache_file_builds); index++)
	{
		if (!csstrncmp(build, cache_file_builds[index].build, sizeof(cache_file_globals.header.build)))
			return cache_file_builds[index].region;
	}

	return NULL;
}

/* the region of the loaded map's build if it plays multiplayer, else NULL;
build gets the build */
char const *cache_files_multiplayer_region(
	char build[0x20])
{
	csstrncpy(build, cache_file_globals.header.build, 0x20);
	build[0x1F] = 0;

	return cache_files_build_region(cache_file_globals.header.build);
}

/* whether the named map plays multiplayer with the others: FALSE only for
a map whose header is of a build not listed above (a map whose header cannot
be read is left to precaching, which tells of a missing map); build gets the
map's build, empty if unread. The multiplayer menus check the loaded map's
(ui.map's) build; this checks a multiplayer map's own, which may be of
another build: the object and damage messages name definitions by tag
index, which differs between builds */
boolean cache_files_map_plays_multiplayer(
	char const *map_name,
	char build[0x20])
{
	struct cache_file_header header;
	char path[256];
	HANDLE file;
	boolean result = TRUE;

	build[0] = 0;
	if (!map_name || !map_name[0])
		return TRUE;
	snprintf(path, sizeof(path), "%s%s.map", cache_files_map_directory(), tag_name_strip_path(map_name));
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		unsigned long bytes_read;

		if (ReadFile(file, &header, sizeof(header), &bytes_read, NULL) &&
			bytes_read == sizeof(header) &&
			cache_file_header_verify(&header, path, FALSE))
		{
			csstrncpy(build, header.build, 0x20);
			build[0x1F] = 0;
			result = cache_files_build_region(header.build) != NULL;
		}
		CloseHandle(file);
	}

	return result;
}

/* tells the player that maps of a build (a cache file header's) do not play
multiplayer: map_name the map's, or NULL for the player's maps */
void cache_files_show_multiplayer_unavailable(
	char const *map_name,
	char const *build)
{
	void platform_log(char const *format, ...);
	void platform_show_message(char const *title, char const *message);
	char message[320];

	if (map_name)
	{
		platform_log("multiplayer is unavailable: the map %s is of build %s, which is not supported", map_name, build);
		snprintf(
			message,
			sizeof(message),
			"The map %s (build %s) isn't supported for multiplayer yet.\n\nAsk in the Discord to get it added.",
			tag_name_strip_path(map_name),
			build);
	}
	else
	{
		platform_log("multiplayer is unavailable: maps of build %s are not supported", build);
		snprintf(
			message,
			sizeof(message),
			"Your maps (build %s) aren't supported for multiplayer yet.\n\nAsk in the Discord to get them added.",
			build);
	}
	platform_show_message("Halo: multiplayer unavailable", message);

	return;
}

/* port: the version of the map a network game is on, which the host sends
in its game record's map version (which the Xbox game left 0): a Halo PC
map's header checksum, which differs between versions of a map, as OpenCE's
build-147 sends a Custom Edition map's. 0, which a client checks nothing
for, for the Xbox's maps, whose builds of other regions play together. */
unsigned long cache_files_map_version(
	char const *map_name)
{
#ifdef HALO_CUSTOM_EDITION
	char file[64];
	char path[256];
	short family = map_family_parse(map_name, file, sizeof(file));
	struct cache_file_header header;
	unsigned long bytes_read = 0;
	HANDLE handle;

	if (family == _map_family_xbox || !map_family_find(family, file, path, sizeof(path)))
		return 0;
	handle = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (handle == INVALID_HANDLE_VALUE)
		return 0;
	if (!ReadFile(handle, &header, sizeof(header), &bytes_read, NULL) || bytes_read != sizeof(header))
		header.checksum = 0;
	CloseHandle(handle);
	return header.checksum;
#else
	(void)map_name;
	return 0;
#endif
}

/* port: whether this machine has the map a network game is on (a client
joining it: network_client_manager.c); when not, tells the player which map
is missing and the folder to copy it into, in the error the main menu shows
next, rather than the damaged disc error that precaching a map that is not
there gives (cache_files_give_time_to_precache). A Halo PC map (<file>@ce,
@pc or @md) is looked for in its family's folders (halo_map_families.h), an
Xbox map in maps. (After OpenCE's build-145, which tells its players so.) A Halo PC map must be the host's version
(version, cache_files_map_version's on the host; 0 for any). */
boolean cache_files_map_present(
	char const *map_name,
	unsigned long version)
{
	void platform_log(char const *format, ...);
	char const *cache_files_map_directory(void);
	wchar_t error_text[512];
	char file[64];
	char message[512];
	short family;
	short index;

	if (!map_name || !map_name[0])
		return TRUE;
	family = map_family_parse(map_name, file, sizeof(file));
#ifdef HALO_CUSTOM_EDITION
	if (family != _map_family_xbox)
	{
		char path[256];

		if (map_family_find(family, file, path, sizeof(path)))
		{
			unsigned long own = cache_files_map_version(map_name);

			/* (the host's version of it, when the host says which) */
			if (!version || own == version)
				return TRUE;
			platform_log("map version: %s is %08lx here, %08lx on the host", map_name, own, version);
			snprintf(message, sizeof(message),
				"Your %s map %.63s is another version than the host's. Copy the host's %.63s.map into %s.",
				map_family_badge(family), file, file, map_family_folder(family));
		}
		else
		{
			snprintf(message, sizeof(message), "You don't have the %s map %.63s. If you have it, copy %.63s.map into %s.",
				map_family_badge(family), file, file, map_family_folder(family));
		}
	}
	else
#endif
	{
		char path[256];
		HANDLE handle;

		if (family != _map_family_xbox)
		{
			snprintf(message, sizeof(message), "The map %.63s is a %s map, which this build doesn't play.", file,
				map_family_badge(family));
		}
		else
		{
			if (cache_files_precache_map_loaded(map_name))
				return TRUE;
			snprintf(path, sizeof(path), "%s%s.map", cache_files_map_directory(), file);
			handle = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
			if (handle != INVALID_HANDLE_VALUE)
			{
				CloseHandle(handle);
				return TRUE;
			}
#ifdef HALO_CUSTOM_EDITION
			/* (a host that names a Halo PC map as the game's own maps are
			named: OpenCE's builds before build-145) */
			if (map_family_find(_map_family_custom_edition, file, path, sizeof(path)))
			{
				snprintf(message, sizeof(message),
					"The host's map %.63s is a Halo PC map named for an older version of the game.", file);
			}
			else
#endif
			{
				snprintf(message, sizeof(message), "You don't have the map %.63s. If you have it, copy %.63s.map into maps.",
					file, file);
			}
		}
	}
	platform_log("map missing: %s", message);
	for (index = 0; message[index] && index < NUMBEROF(error_text) - 1; index++)
		error_text[index] = (wchar_t)(unsigned char)message[index];
	error_text[index] = 0;
	display_error_text_when_main_menu_loaded(error_text);

	return FALSE;
}

boolean cache_files_give_time_to_precache(
	char const *map_name)
{
	boolean result = FALSE;

	/* port: no map named yet is nothing to precache. A client joining over
	the internet asks for its multiplayer map (network_game_client_update_precache_status)
	before the host's settings name it: an empty name, which matched a cache
	file slot not yet used, and once all six hold maps (two campaign levels,
	the main menu and three multiplayer maps played) matched none, so was
	taken for a map missing from the disc (the damaged disc error) */
	if (!map_name || !map_name[0])
		return FALSE;
	if (cache_files_precache_map_loaded(map_name))
	{
		result = TRUE;
	}
	else
	{
		if (cache_files_precache_in_progress() &&
			!cache_files_precache_is_copying_map(map_name))
		{
			cache_files_precache_map_end();
		}

		if (cache_files_precache_in_progress())
		{
			real progress;
			short status = cache_files_precache_map_status(&progress);

			if (status == 2)
				display_error_damaged_media();
			else if (status == 1)
				cache_files_precache_map_end();
		}
		else
		{
			cache_files_precache_set_priority(0);
			if (!cache_files_precache_map_begin(map_name, FALSE))
				display_error_damaged_media();
		}
	}

	return result;
}

long scenario_tags_load(
	char const *scenario_name)
{
	long result;
	char const *stripped_scenario_name;
	void *tag_cache_base_address;
	boolean read_complete;

	stripped_scenario_name = tag_name_strip_path(scenario_name);
	result = NONE;
	texture_cache_open();
	sound_cache_open();
	if (cache_file_open(stripped_scenario_name, &cache_file_globals.header))
	{
		tag_cache_base_address = physical_memory_get_tag_cache_base_address();
#ifdef HALO_CUSTOM_EDITION
		/* port: a Custom Edition map's tags, in their own tag cache */
		cache_file_is_ce = CACHE_FILE_VERSION_IS_PC(cache_file_globals.header.version);
		if (cache_file_is_ce && cache_file_header_verify(&cache_file_globals.header, scenario_name, TRUE))
		{
			struct cache_file_ce_tag_header *ce_header;

			tag_cache_base_address = xbox_pointer(CE_TAG_CACHE_BASE);
			csmemset(tag_cache_base_address, 0xCD, CE_TAG_CACHE_SIZE);
			cache_file_read(NONE, cache_file_globals.header.tag_data_offset, cache_file_globals.header.tag_data_size,
				tag_cache_base_address, &read_complete, TRUE);
			while (!read_complete)
				SwitchToThread();
			ce_header = tag_cache_base_address;
			match_vassert("c:\\halo\\SOURCE\\cache\\cache_files.c", 0x94,
				ce_header->signature == CACHE_FILE_TAG_HEADER_SIGNATURE, "a Custom Edition map's tag header");
			cache_file_globals.tag_header = tag_cache_base_address;
			global_tag_instances = xbox_pointer(ce_header->tag_instances);
			cache_file_globals.tags_loaded = TRUE;
			error(_error_silent, "Custom Edition map %s: %ld tags, %ld model parts (%ld bytes of model data)",
				scenario_name, ce_header->tag_count, ce_header->model_part_count, ce_header->model_data_size);
			/* its indexed tags, from the resource maps, into the space between
			its tags and its structure BSPs (the lowest of them: the scenario's
			references, port/linux/game/ce_resources.c) */
			{
				extern boolean ce_resources_tags_loaded(void *tag_instances, long tag_count, unsigned long first_free,
					unsigned long end_free);
				struct cache_file_tag_instance *scenario = &global_tag_instances[ce_header->scenario_tag_index & 0xffff];
				byte *scenario_data = xbox_pointer(scenario->base_address);
				long bsp_count = *(long *)(scenario_data + 0x5A4);
				struct scenario_structure_bsp_reference *bsps =
					xbox_pointer(*(unsigned long *)(scenario_data + 0x5A8));
				unsigned long end_free = CE_TAG_CACHE_BASE + CE_TAG_CACHE_SIZE;
				long bsp_index;

				for (bsp_index = 0; bsp_index < bsp_count; bsp_index++)
				{
					unsigned long bsp_base = (unsigned long)xbox_address(xbox_pointer(bsps[bsp_index].base_address));

					/* (each above the tags, within the tag cache, checked
					before the map was opened: ce_map_checks.c) */
					if (bsp_base > CE_TAG_CACHE_BASE && bsp_base < end_free)
						end_free = bsp_base;
				}
				{
					extern long ce_map_cache_version;

					ce_map_cache_version = cache_file_globals.header.version;
				}
				if (!ce_resources_tags_loaded(global_tag_instances, ce_header->tag_count,
					CE_TAG_CACHE_BASE + cache_file_globals.header.tag_data_size, end_free))
				{
					error(_error_silent, "Custom Edition map %s: its resources could not be loaded", scenario_name);
				}
				/* what Halo PC's engine let be that this one would not,
				repaired as the map was when it was checked
				(port/linux/game/ce_repairs.c) */
				{
					extern void ce_repairs_tags_loaded(void *tag_instances, long tag_count,
						unsigned long scenario_tag_index);

					ce_repairs_tags_loaded(global_tag_instances, ce_header->tag_count,
						ce_header->scenario_tag_index);
				}
			}
			/* its gbxmodels made models, from its model data block
			(port/linux/game/ce_models.c) */
			{
				extern boolean ce_models_tags_loaded(void *tag_instances, long tag_count, byte const *model_data,
					unsigned long vertex_data_size);
				byte *model_data = system_malloc(ce_header->model_data_size);

				if (model_data)
				{
					cache_file_read(NONE, ce_header->model_data_file_offset, ce_header->model_data_size, model_data,
						&read_complete, TRUE);
					while (!read_complete)
						SwitchToThread();
					if (!ce_models_tags_loaded(global_tag_instances, ce_header->tag_count, model_data,
						ce_header->vertex_data_size))
					{
						error(_error_silent, "Custom Edition map %s: its models could not all be made",
							scenario_name);
					}
					/* (its function fields naming functions there are none
					of made valid ones first: port/linux/game/ce_functions.c) */
					{
						extern void ce_functions_tags_loaded(void *tag_instances, long tag_count);

						ce_functions_tags_loaded(global_tag_instances, ce_header->tag_count);
					}
					{
						extern void ce_shaders_tags_loaded(void *tag_instances, long tag_count);

						ce_shaders_tags_loaded(global_tag_instances, ce_header->tag_count);
					}
					system_free(model_data);
				}
				else
				{
					error(_error_silent, "Custom Edition map %s: no memory for its model data (%ld bytes): its models "
						"could not be made", scenario_name, ce_header->model_data_size);
				}
			}

			/* (none of the high-res HUD: Halo PC's HUD bitmaps are laid out
			otherwise than the Xbox's it was drawn for, so a Custom Edition map
			draws its own) */
			{
				extern void hud_hires_tags_unloaded(void);

				hud_hires_tags_unloaded();
			}
			/* its HUD interfaces: placements at Halo PC's high resolution
			scale halved, meters' bitmaps sampled in Halo PC's channels
			(port/linux/game/ce_hud.c) */
			{
				extern void ce_hud_tags_loaded(void *tag_instances, long tag_count);

				ce_hud_tags_loaded(global_tag_instances, ce_header->tag_count);
			}
			return ce_header->scenario_tag_index;
		}
#endif
		if (cache_file_header_verify(&cache_file_globals.header, scenario_name, TRUE))
		{
			csmemset(tag_cache_base_address, 0xCD, 0x01600000);
			cache_file_read(
				NONE,
				cache_file_globals.header.tag_data_offset,
				cache_file_globals.header.tag_data_size,
				tag_cache_base_address,
				&read_complete,
				TRUE);
			while (!read_complete)
			{
				SwitchToThread();
			}

			/* port: tags that did not all read, or whose header cannot be
			trusted, are not loaded: the map is refused, as one whose header
			is wrong is, and closed for the next to open */
			if (read_complete != TRUE)
			{
				error(_error_silent, "the cache file '%s' could not be read", scenario_name);
				cache_file_close();

				return NONE;
			}
			if (!cache_file_tag_header_verify(
				tag_cache_base_address,
				cache_file_globals.header.tag_data_size,
				scenario_name))
			{
				cache_file_close();

				return NONE;
			}
			/* port: and every tag checked against its group's schema before
			anything reads it (port/linux/game/tag_validate.c): a map whose
			tags' pointers cannot be trusted is refused; what can be
			corrected is */
			if (!tag_validate_tags(
				tag_cache_base_address,
				cache_file_globals.header.tag_data_size,
				cache_file_globals.header.file_length,
				scenario_name))
			{
				cache_file_close();

				return NONE;
			}

			cache_file_globals.tag_header = tag_cache_base_address;
			match_vassert(
				"c:\\halo\\SOURCE\\cache\\cache_files.c",
				0x94,
				cache_file_globals.tag_header->signature == CACHE_FILE_TAG_HEADER_SIGNATURE,
				csprintf(
					temporary,
					"signature is '%c%c%c%c', should be '%c%c%c%c'",
					((char *)&cache_file_globals.tag_header->signature)[3],
					((char *)&cache_file_globals.tag_header->signature)[2],
					((char *)&cache_file_globals.tag_header->signature)[1],
					((char *)&cache_file_globals.tag_header->signature)[0],
					't',
					'a',
					'g',
					's'));
			global_tag_instances = xbox_pointer(cache_file_globals.tag_header->tag_instances);
			tags_header_register_vertex_and_index_buffers(cache_file_globals.tag_header);
			cache_file_globals.tags_loaded = TRUE;
			/* port: a PAL map played as the NTSC maps are (port/linux/game/pal_tags.c) */
			{
				extern void pal_tags_loaded(char const *build);

				pal_tags_loaded(cache_file_globals.header.build);
			}
			/* The two powerups' authored render spheres can be smaller than
			 * their rigid meshes. Derive their bounds while tags are writable. */
			models_fix_powerup_render_bounds();
			/* port: the menus' tags, added to the map's (port/linux/game/menu_tags.c) */
			{
				extern void menu_tags_loaded(char const *map_name);

				menu_tags_loaded(cache_file_globals.header.name);
			}
			/* port: the bitmaps the high-res HUD stands for (port/linux/game/hud_hires_tags.c) */
			{
				extern void hud_hires_tags_loaded(long scenario_index);

				hud_hires_tags_loaded(cache_file_globals.tag_header->scenario_tag_index);
			}
#ifdef HALO_GAME_BROWSER
			/* the Multiplayer menu's ONLINE GAMES (interface/ui_widget.c) */
			{
				extern void ui_widget_online_games_tags_loaded(void);

				ui_widget_online_games_tags_loaded();
			}
#endif
			result = cache_file_globals.tag_header->scenario_tag_index;
		}
		/* port: a map refused is closed for the next to open */
		else
		{
			cache_file_close();
		}
	}

	return result;
}

boolean scenario_structure_bsp_load(
	struct scenario_structure_bsp_reference *reference)
{
	struct cache_file_tag_instance *tag_instance;
	byte *tag_cache_base_address;
	/* port: the bsp's header, once read and checked */
	struct cache_file_structure_bsp_header *structure_bsp_header;

	/* port: the tag data's size was checked as the map loaded
	(cache_file_header_verify); the bsp's reference is the map's, and is
	checked before anything is read where it says */
	if (
#ifdef HALO_CUSTOM_EDITION
		/* (a Custom Edition map's bsps are in its own tag cache, checked
		against its file before it was opened: ce_map_checks.c) */
		!cache_file_is_ce &&
#endif
		(cache_file_globals.header.tag_data_size < 0 ||
		cache_file_globals.header.tag_data_size > TAG_CACHE_SIZE ||
		!cache_file_structure_bsp_reference_verify(reference)))
	{
		return FALSE;
	}

	tag_cache_base_address = physical_memory_get_tag_cache_base_address();
#ifdef HALO_CUSTOM_EDITION
	/* port: (not a Custom Edition map's: its tags are in a tag cache of their
	own, and may be more than the Xbox's holds) */
	if (!cache_file_is_ce)
#endif
	csmemset(
		tag_cache_base_address + cache_file_globals.header.tag_data_size,
		0xCD,
		0x01600000 - cache_file_globals.header.tag_data_size);
	{
		boolean read_complete;

		cache_file_read(
			NONE,
			reference->file_offset,
			reference->file_size,
			xbox_pointer(reference->base_address),
			&read_complete,
			TRUE);
		while (!read_complete)
		{
			SwitchToThread();
			if (system_milliseconds() - sound_render_time() > 33)
			{
				sound_idle();
			}
		}

		/* port: a bsp that did not all read, or whose header's pointers
		leave what was read, is not loaded */
		structure_bsp_header = xbox_pointer(reference->base_address);
		if (read_complete != TRUE ||
#ifdef HALO_CUSTOM_EDITION
			(!cache_file_is_ce && (
#endif
			structure_bsp_header->signature != CACHE_FILE_STRUCTURE_BSP_HEADER_SIGNATURE ||
			!cache_file_region_contains(
				structure_bsp_header,
				reference->file_size,
				xbox_pointer(structure_bsp_header->base_address),
				1,
				1) ||
			!cache_file_region_contains(
				structure_bsp_header,
				reference->file_size,
				xbox_pointer(structure_bsp_header->vertex_buffers),
				structure_bsp_header->vertex_buffer_count,
				CACHE_FILE_BUFFER_SIZE) ||
			!cache_file_region_contains(
				structure_bsp_header,
				reference->file_size,
				xbox_pointer(structure_bsp_header->index_buffers),
				structure_bsp_header->index_buffer_count,
				CACHE_FILE_BUFFER_SIZE)
#ifdef HALO_CUSTOM_EDITION
			))
#endif
			)
		{
			error(
				_error_silent,
				"a structure bsp is damaged: its %08x bytes at %08x %s",
				reference->file_size,
				reference->file_offset,
				read_complete != TRUE ? "could not be read" : "have a wrong header");

			return FALSE;
		}
	}

	/* port: and checked against its schema, as the map's tags were
	(port/linux/game/tag_validate.c); a Custom Edition or HaloMD map's
	tags are Halo PC's layout, checked as they load (ce_map_checks.c) */
	if (
#ifdef HALO_CUSTOM_EDITION
		!cache_file_is_ce &&
#endif
		!tag_validate_structure_bsp(
		reference->structure_bsp.index,
		xbox_pointer(reference->base_address),
		reference->file_size))
	{
		return FALSE;
	}

	cache_file_globals.structure_bsp_header = structure_bsp_header;
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		0xE0,
		cache_file_globals.structure_bsp_header->signature==CACHE_FILE_STRUCTURE_BSP_HEADER_SIGNATURE);
	structure_bsp_header_register_vertex_buffers(cache_file_globals.structure_bsp_header);
#ifdef HALO_CUSTOM_EDITION
	/* port: a Custom Edition or HaloMD map's BSP: its vertices compressed,
	as the Xbox's are (port/linux/game/ce_bsp.c), and its clusters'
	predicted resources repaired (port/linux/game/ce_repairs.c) */
	if (cache_file_is_ce)
	{
		extern void ce_bsp_loaded(struct structure_bsp *structure);
		extern void ce_repairs_bsp_loaded(struct structure_bsp *structure);

		ce_bsp_loaded(xbox_pointer(cache_file_globals.structure_bsp_header->base_address));
		ce_repairs_bsp_loaded(xbox_pointer(cache_file_globals.structure_bsp_header->base_address));
	}
#endif
	tag_instance = cache_get_tag_instance(reference->structure_bsp.index);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		0xEA,
		!tag_instance->base_address);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		0xEB,
		tag_instance->group_tag==STRUCTURE_BSP_TAG);
	tag_instance->base_address = cache_file_globals.structure_bsp_header->base_address;

	return TRUE;
}

void scenario_structure_bsp_unload(
	struct scenario_structure_bsp_reference *reference)
{
	struct cache_file_tag_instance *tag_instance;

	structure_bsp_header_deregister_vertex_buffers(cache_file_globals.structure_bsp_header);
#ifdef HALO_CUSTOM_EDITION
	if (cache_file_is_ce)
	{
		extern void ce_bsp_unloaded(void);

		ce_bsp_unloaded();
	}
#endif
	tag_instance = cache_get_tag_instance(reference->structure_bsp.index);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		256,
		tag_instance->base_address);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		257,
		tag_instance->group_tag==STRUCTURE_BSP_TAG);
	tag_instance->base_address = XBOX_NULL;
	cache_file_globals.structure_bsp_header = NULL;

	return;
}

void *tag_get(
	long group_tag,
	long tag_index)
{
	char expected_group[16];
	char returned_group[16];

	struct cache_file_tag_instance *tag_instance = cache_get_tag_instance(tag_index);
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		298,
		tag_instance->group_tag == group_tag ||
		tag_instance->parent_group_tags[0] == group_tag ||
		tag_instance->parent_group_tags[1] == group_tag,
		csprintf(
			temporary,
			"expected tag group '%s' but got '%s' for %08x",
			tag_to_string(group_tag, expected_group),
			tag_to_string(tag_instance->group_tag, returned_group),
			tag_index)
	);
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		302,
		tag_instance->base_address,
		csprintf(temporary, "can't get() a tag with a base address!")
	);
	/* port: a tag of another group (one a map's data named, which nothing
	checked) is not read as this one: the empty tag's data is given, as
	for an index that is not a tag; nor is a tag with no data (a structure
	bsp not loaded) */
	if ((tag_instance->group_tag != group_tag &&
			tag_instance->parent_group_tags[0] != group_tag &&
			tag_instance->parent_group_tags[1] != group_tag) ||
		!tag_instance->base_address)
	{
		return xbox_pointer(cache_empty_tag_instance(tag_index)->base_address);
	}
	
	return xbox_pointer(tag_instance->base_address);
}

/* whether the index is a loaded tag of the group (or a group it inherits
from): the distributed netcode names tags another machine sent
(port/linux/game/network_damage.c), which tag_get would only assert on */
boolean tag_index_is_group(
	long tag_index,
	long group_tag)
{
	short absolute_index = (short)tag_index;
	struct cache_file_tag_instance *tag_instance;

	if (tag_index == NONE || !cache_file_globals.tags_loaded || !global_tag_instances ||
		absolute_index < 0 || absolute_index >= cache_file_globals.tag_header->tag_count)
	{
		return FALSE;
	}
	tag_instance = &global_tag_instances[absolute_index];
	return tag_instance->tag_index == tag_index && tag_instance->base_address &&
		(tag_instance->group_tag == group_tag || tag_instance->parent_group_tags[0] == group_tag ||
			tag_instance->parent_group_tags[1] == group_tag);
}

char *tag_get_name(
	long tag_index)
{
	return xbox_pointer(cache_get_tag_instance(tag_index)->name);
}

unsigned long tag_get_group_tag(
	long tag_index)
{
	return cache_get_tag_instance(tag_index)->group_tag;
}
