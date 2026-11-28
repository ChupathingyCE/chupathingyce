/*
SPV3_INSTALLER.C

SPV3 (the SPV3 team's campaign for Custom Edition, on OpenSauce) put in place
from the player's own copy of its release, for SPV3 mode
(port/linux/game/spv3_mode.c), as the setup screen takes the Xbox disc image
and the Custom Edition installer (sdl_platform.c's import, launcher.c). The
game never downloads SPV3 and no SPV3 file is part of it: the player brings
the release.

SPV3 3.3.0's release is a zip (or the folder it was unpacked to) holding its
installer programs and a data folder: manifest.bin, a raw-deflated XML list
of its packages, and each file of SPV3 as a package of its own, <uuid>.bin, a
zip of one member. The release this build knows is listed below by its
manifest's size and CRC-32 and, for each file the game uses, its package,
size and CRC-32 (facts about the release, read from the player's copy of
it). Only those files are written, and only where the game reads them:

  maps\*.yelo, bitmaps.map, sounds.map, loc.map  ->  maps_spv3\
  attract.bik, credits.bik, credits_xbox.bik     ->  maps_spv3\bink\
  chimera\lua\global\init.lua                    ->  lua\spv3\scripts\global\init.lua
  lua\CMT, lua\halo_classes, lua\class, JSON.lua ->  lua\spv3\modules\

No program or library of the release is written or run, and no name from
the archive is ever a path: every target is this file's own. A release
whose manifest is not the one known is refused with one line.

Nothing in the archive is trusted. Its central directory (zip64 too) is
read with every offset and size checked against the file; a package is
streamed (stored or deflated, inside an outer entry stored or deflated)
straight to <target>.part, its size and CRC-32 checked against the list as
it is written, then renamed over the target, so an install that stops never
leaves a bad file under a real name. A target already there with the right
size and CRC-32 is kept, so running it again resumes or repairs. A target,
or a folder on the way to it, that is a link is refused: on POSIX systems each
folder is opened from the one before it without following a link, and every
file is opened, made and renamed in its folder from there, so a folder swapped
for a link after a check isn't followed. A .part already there (a stopped
run's, or a link of any kind someone put there) is removed, never written
through: the new one is made fresh. The free space is checked first.
*/

/* (the desktop ports only: the Android app imports the game data itself) */
#ifndef HALO_ANDROID

#include "spv3_installer.h"
#ifdef HALO_SPV3_INSTALLER_STANDALONE
/* (the host fuzz target, tools/tests/host_fuzz/spv3_release: the game's
platform layer left out, its log the one thing used of it) */
void platform_log(const char *format, ...);
#else
#include "platform.h"
#endif
#include "posix.h"
#include "zlib_prefixed.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../game/content_bytes.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif

/* (POSIX: the folders are opened one from the other, openat and the rest;
Windows has no such calls, see target_folder_open) */
#ifndef _WIN32
#define SPV3_FOLDER_DESCRIPTORS 1
#endif

/* ---------- constants */

enum
{
	ZIP_LOCAL_SIGNATURE = 0x04034b50,
	ZIP_CENTRAL_SIGNATURE = 0x02014b50,
	ZIP_END_SIGNATURE = 0x06054b50,
	ZIP64_LOCATOR_SIGNATURE = 0x07064b50,
	ZIP64_END_SIGNATURE = 0x06064b50,
	ZIP_LOCAL_SIZE = 30,
	ZIP_CENTRAL_SIZE = 46,
	ZIP_END_SIZE = 22,
	ZIP64_LOCATOR_SIZE = 20,
	ZIP64_END_SIZE = 56,
	ZIP_MAXIMUM_COMMENT = 0xffff,
	ZIP_FLAG_ENCRYPTED = 0x0001,
	ZIP_FLAG_DESCRIPTOR = 0x0008,
	ZIP_METHOD_STORED = 0,
	ZIP_METHOD_DEFLATED = 8,
	ZIP_EXTRA_ZIP64 = 0x0001,
	/* (a release holds a few hundred entries; its central directory a few
	hundred KB) */
	MAXIMUM_ENTRIES = 4096,
	MAXIMUM_CENTRAL_DIRECTORY = 16 << 20,
	MAXIMUM_NAME = 512,
	/* a package's name, <uuid>.bin, and its end */
	PACKAGE_NAME_SIZE = 48,
	CHUNK_SIZE = 1 << 20,
	PATH_SIZE = 1200,
};

#ifndef HALO_SPV3_INSTALLER_STANDALONE
/* SPV3 3.3.0's data\manifest.bin */
#define RELEASE_MANIFEST_SIZE 9477ULL
#define RELEASE_MANIFEST_CRC 0x85e0aa9bUL
#define RELEASE_NAME "3.3.0"
#else
/* (the fuzz target's own test release, tools/tests/host_fuzz/spv3_release's
seeds: none of SPV3's, so the install's whole path is fuzzed too) */
#define RELEASE_MANIFEST_SIZE 328ULL
#define RELEASE_MANIFEST_CRC 0xa92bc0d5UL
#define RELEASE_NAME "test"
#endif
/* room kept free beside SPV3's files */
#define FREE_SPACE_MARGIN (256ULL << 20)

/* ---------- structures */

struct spv3_file
{
	const char *package;
	const char *target;
	unsigned long long size;
	unsigned long crc;
};

/* an entry of the release's data folder */
struct release_entry
{
	char name[PACKAGE_NAME_SIZE];
	unsigned long long data_offset;
	unsigned long long compressed_size;
	unsigned long long size;
	int method;
};

struct release
{
	/* a zip, or the data folder of one unpacked */
	int zip;
	int descriptor;
	unsigned long long file_size;
	char folder[PATH_SIZE];
	struct release_entry *entries;
	int entry_count;
	char *error;
	int error_size;
};

/* bytes of an entry: a range of a file, or of another stream, inflated or
as they are */
struct stream
{
	int descriptor;
	unsigned long long position;
	struct stream *parent;
	unsigned long long remaining;
	int method;
	z_stream inflater;
	int inflating;
	int finished;
	unsigned char *input;
};

/* ---------- globals */

/* what of SPV3 3.3.0 the game uses: each file's package in data\, where it
goes under the data folder, its size and CRC-32 */
static const struct spv3_file spv3_files[] =
{
#ifndef HALO_SPV3_INSTALLER_STANDALONE
	{ "1da11bd2-6292-47e7-9b3d-efd1266d890c", "lua/spv3/modules/CMT/DEPRECATED/armor_abilities.lua", 6741UL, 0xe8e23f1eUL },
	{ "89c7f4ad-61b1-4daa-91f8-0e174a6cdc2a", "lua/spv3/modules/CMT/DEPRECATED/armor_ability/camo.lua", 1033UL, 0xecf8d1a8UL },
	{ "16e51780-c39e-43a1-9d33-0dc49f9c37b1", "lua/spv3/modules/CMT/DEPRECATED/armor_ability/visr.lua", 1490UL, 0xcd823820UL },
	{ "91e9e27a-b891-4587-9d32-de0e1b4471e7", "lua/spv3/modules/CMT/hud.lua", 458UL, 0x7b0e062aUL },
	{ "9b87b381-328f-4438-a5f0-4045bd45a6db", "lua/spv3/modules/CMT/hud/motion_tracker.lua", 288UL, 0xaca5dedbUL },
	{ "9fa1b958-280a-4f7a-b4c9-7929799da1e8", "lua/spv3/modules/CMT/init.lua", 425UL, 0xb0d5bcaeUL },
	{ "02560dd9-a64c-45d8-812e-507ee5c98106", "lua/spv3/modules/CMT/level.lua", 1245UL, 0x7ad78a84UL },
	{ "a23a454a-c9fe-4b46-91b1-4cbe530726d0", "lua/spv3/modules/CMT/players.lua", 94UL, 0xeb48b6aaUL },
	{ "1bfe376a-1bdc-40af-a755-518313019a8f", "lua/spv3/modules/CMT/players/fp_legs.lua", 14131UL, 0x352df7bcUL },
	{ "23aed372-8db9-4a8f-bb32-a2eb4e65cca9", "lua/spv3/modules/CMT/post_processing.lua", 15674UL, 0xb136bafbUL },
	{ "166c63f3-308e-43a9-9217-69ffa4feef61", "lua/spv3/modules/CMT/skulls.lua", 453UL, 0xe4d0af81UL },
	{ "1ee4e480-60a0-4e7a-be25-3fe8737a8eaa", "lua/spv3/modules/CMT/skulls/mythic.lua", 635UL, 0x1301d34bUL },
	{ "54302b0b-b9a3-40b7-8679-ef81ee4c68b6", "lua/spv3/modules/CMT/skulls/tilt.lua", 631UL, 0x01adfde6UL },
	{ "d3f5fc1b-c53e-4ef7-a5ad-e327946bea8b", "lua/spv3/modules/CMT/vehicles.lua", 0UL, 0x00000000UL },
	{ "642f2bf6-087b-425d-8751-f5a3bb19a0df", "lua/spv3/modules/CMT/weapons.lua", 41UL, 0x96f53972UL },
	{ "3aa86e7d-00b9-4f13-a227-0da86a333495", "lua/spv3/modules/CMT/weapons/rocket_launcher.lua", 4347UL, 0xb1f39c07UL },
	{ "eb73fc6d-b24d-4fa6-850c-1826a9471e56", "lua/spv3/modules/JSON.lua", 63243UL, 0xed41ceeeUL },
	{ "ca07d2db-d670-465a-9828-9d652b39ea1f", "lua/spv3/modules/class/init.lua", 1128UL, 0x03807aa6UL },
	{ "7ee2a6c1-cdcd-445b-b3f6-b1baf065f61d", "lua/spv3/modules/halo_classes/init.lua", 7987UL, 0xc6d0861cUL },
	{ "937aeab1-1d54-41b3-8114-23efcbed4115", "lua/spv3/modules/halo_classes/objects.lua", 1985UL, 0x1a14326eUL },
	{ "5e94b6da-8551-47e4-86f0-f624724002bb", "lua/spv3/modules/halo_classes/objects/biped.lua", 6900UL, 0x5ef7ef29UL },
	{ "4609bd5c-cd32-4d5e-ac7f-4261bccfa41f", "lua/spv3/modules/halo_classes/objects/device.lua", 1790UL, 0xac9bbfcbUL },
	{ "4e1c16d5-afdb-4a69-b949-e9b6f845da9a", "lua/spv3/modules/halo_classes/objects/device_control.lua", 280UL, 0x340d36a6UL },
	{ "d483d371-19b6-4344-be64-ab11f2bcdf41", "lua/spv3/modules/halo_classes/objects/device_machine.lua", 477UL, 0x57317bd8UL },
	{ "290522c5-ae73-47dd-b9f0-11ae5c609381", "lua/spv3/modules/halo_classes/objects/equipment.lua", 1919UL, 0x24a2185bUL },
	{ "9b6cf07f-2095-404f-9581-843d9cb07dd1", "lua/spv3/modules/halo_classes/objects/garbage.lua", 265UL, 0x919f9ea8UL },
	{ "5f6a2085-cf2d-47ab-9648-48391f997d26", "lua/spv3/modules/halo_classes/objects/item.lua", 1729UL, 0x9fc47947UL },
	{ "34aed48d-0765-4ee9-a350-2714353f7a99", "lua/spv3/modules/halo_classes/objects/light_fixture.lua", 476UL, 0x31c9a8d1UL },
	{ "ae6e8f12-1d53-432b-91d1-937578f62464", "lua/spv3/modules/halo_classes/objects/object.lua", 28878UL, 0xd4ad2b76UL },
	{ "97840c21-fe81-4aa1-b978-c1c2cc03eb4b", "lua/spv3/modules/halo_classes/objects/projectile.lua", 3468UL, 0xd2a56178UL },
	{ "6e35e632-5f08-4e34-8e93-5dcd1bb822b3", "lua/spv3/modules/halo_classes/objects/unit.lua", 33685UL, 0xe27fe6edUL },
	{ "f9efe176-a7f6-4e92-890c-af6d2cefe2bf", "lua/spv3/modules/halo_classes/objects/vehicle.lua", 4926UL, 0x00d4d457UL },
	{ "9be7b85b-f1aa-4697-b60e-de84cc516139", "lua/spv3/modules/halo_classes/objects/weapon.lua", 7602UL, 0x057f0be5UL },
	{ "d57bd71d-a287-4a90-998c-4a7a92515a5e", "lua/spv3/modules/halo_classes/players.lua", 11227UL, 0x8a50730dUL },
	{ "9d163535-bf3a-4565-b059-a0a1dc433e08", "lua/spv3/modules/halo_classes/scripts.lua", 8551UL, 0xde917f29UL },
	{ "5af0e456-d9f1-4e8d-94dc-4616ac9391cf", "lua/spv3/modules/halo_classes/tags.lua", 2549UL, 0x0f1db28eUL },
	{ "74942728-784a-4824-806b-843bd1d4a45b", "lua/spv3/modules/halo_classes/tags/animation.lua", 86UL, 0x2e670822UL },
	{ "47ea389a-0d59-4fb8-af19-599b3dfc1368", "lua/spv3/modules/halo_classes/tags/collision.lua", 16435UL, 0xe1c28f70UL },
	{ "65d6bb99-1945-4dc3-bf8b-3d2521b39f3e", "lua/spv3/modules/halo_classes/tags/damage_effect.lua", 16171UL, 0x6b2b8750UL },
	{ "926f7da6-8230-44cb-b8b8-b13f0a39fff0", "lua/spv3/modules/halo_classes/tags/effect.lua", 83UL, 0xaccce6cfUL },
	{ "0e9efef4-c3b9-425d-a2b3-58161bd13fe7", "lua/spv3/modules/halo_classes/tags/hud_globals.lua", 13563UL, 0xcc1c391cUL },
	{ "b9fe398a-e456-45ce-96dc-56a5f89687b4", "lua/spv3/modules/halo_classes/tags/item.lua", 6232UL, 0x9db74e02UL },
	{ "f0ed845a-fc26-438d-ba90-32351410ac65", "lua/spv3/modules/halo_classes/tags/model.lua", 82UL, 0x611a708bUL },
	{ "7e949e10-1274-424a-ba43-35f8f3a89a9c", "lua/spv3/modules/halo_classes/tags/object.lua", 18301UL, 0xb9799f56UL },
	{ "d9f97fad-5dba-4348-97d0-c87ae0720f09", "lua/spv3/modules/halo_classes/tags/physics.lua", 84UL, 0x95db52deUL },
	{ "2aa2d32b-10dd-4b2a-b199-d4cd7ceaa8b0", "lua/spv3/modules/halo_classes/tags/shader.lua", 203UL, 0xb07eec8eUL },
	{ "62f0dfe5-addf-4e44-84f5-ca3092af3469", "lua/spv3/modules/halo_classes/tags/unit_hud_interface.lua", 86736UL, 0x0b0ecbd0UL },
	{ "1dee7e5d-30d0-40d3-9464-218b675b258e", "lua/spv3/modules/halo_classes/tags/weapon.lua", 27845UL, 0xe6b629e4UL },
	{ "09f093df-762c-4c90-85d8-e100efd9dbc7", "lua/spv3/scripts/global/init.lua", 3282UL, 0xb97ad33eUL },
	{ "9f2a5611-a559-47e3-bb9c-b64e38cd79f9", "maps_spv3/bink/attract.bik", 7383548UL, 0xb2b14d3dUL },
	{ "d8096b06-7088-4cd6-b0c3-e55d2dadb692", "maps_spv3/bink/credits.bik", 110617348UL, 0x67f2c12fUL },
	{ "710456fb-f49c-4945-b03f-7936e40964b8", "maps_spv3/bink/credits_xbox.bik", 200614260UL, 0xf102124eUL },
	{ "180dc0b9-2089-442e-a12c-8595099adaa9", "maps_spv3/bitmaps.map", 2061280753UL, 0x07236f48UL },
	{ "ccd0cc64-9d05-422c-b887-0d38689fe2ad", "maps_spv3/loc.map", 923240UL, 0x88201ed7UL },
	{ "424e27ae-8a3a-4b9c-91f9-14811964f2d0", "maps_spv3/sounds.map", 628546085UL, 0x17cd2b7fUL },
	{ "b9d69ff1-47f7-492f-8c51-888ef33d6816", "maps_spv3/spv3_engine_flood.yelo", 490048803UL, 0x814262b1UL },
	{ "24165bea-0b06-4805-9d8b-36a92986b88a", "maps_spv3/spv3_engine_spartan.yelo", 455158528UL, 0xe69c3400UL },
	{ "14c353db-20e9-44f1-a9cb-1f20d50cedaf", "maps_spv3/spv3_lumoria_a.yelo", 489565132UL, 0x44f7c6f8UL },
	{ "a61cb084-a013-4e60-aa60-746afd68efb1", "maps_spv3/spv3_lumoria_b.yelo", 501726836UL, 0x6e6de08bUL },
	{ "31bee259-03c5-42af-988a-1c2d2aa59eb7", "maps_spv3/spv3_lumoria_cd.yelo", 513364829UL, 0xb2ba8051UL },
	{ "52bd58bc-29cb-463d-be88-3ee54554a3b6", "maps_spv3/spv3_rockslide_flood.yelo", 401786079UL, 0x51a42d39UL },
	{ "f8613750-ad5e-4c5c-9ef9-9270d50c3c04", "maps_spv3/spv3_rockslide_spartan.yelo", 409825137UL, 0x769d6bafUL },
	{ "1b1b3242-04fb-41ae-8ac1-72860ecc5e7a", "maps_spv3/spv3a05.yelo", 364389821UL, 0xf1ba4fe9UL },
	{ "2849e811-7511-4670-a799-aeaba294a647", "maps_spv3/spv3a10.yelo", 476380365UL, 0x6e4b8cd8UL },
	{ "06b64279-9232-445d-9ecd-e1f6e54ceb5a", "maps_spv3/spv3a30.yelo", 320939220UL, 0x0c42e72fUL },
	{ "5470804f-6f18-4757-81a3-fc3e95fd4581", "maps_spv3/spv3a50.yelo", 412403649UL, 0x57dfdee9UL },
	{ "27255f6e-559a-4ce0-b953-96d019d25fa1", "maps_spv3/spv3b30.yelo", 299452265UL, 0x3b953e54UL },
	{ "d9995d68-0c91-4eda-85d3-b17f27e3060c", "maps_spv3/spv3b30_evolved.yelo", 406807556UL, 0x614169f9UL },
	{ "e319d002-573c-4f07-b67c-f77344385580", "maps_spv3/spv3b40.yelo", 364075039UL, 0x69c44262UL },
	{ "f3735005-e224-4288-8520-e8e4e61829c8", "maps_spv3/spv3c10.yelo", 332156681UL, 0x8a1b267aUL },
	{ "b6f3aad3-881e-4fbe-ade6-fe6dd4794126", "maps_spv3/spv3c20.yelo", 235716919UL, 0x3961814eUL },
	{ "b347b750-0550-4f1b-b216-567c07a4893e", "maps_spv3/spv3c40.yelo", 362472380UL, 0x64425c77UL },
	{ "27f4e6d2-c3f9-4f39-87d0-e7b56c205c8b", "maps_spv3/spv3d20.yelo", 272422863UL, 0x22cca854UL },
	{ "2bf9016d-b99f-4efc-bc60-97349b85249b", "maps_spv3/spv3d25.yelo", 361379245UL, 0x072190a7UL },
	{ "e488856c-df7b-4eb6-908d-34c121870632", "maps_spv3/spv3d30.yelo", 301743978UL, 0x3021e1f6UL },
	{ "bdaa3768-c960-4116-a738-80f77a0e37eb", "maps_spv3/spv3d30_evolved.yelo", 389208131UL, 0x403ae826UL },
	{ "52af0e5b-c176-4e9d-84ad-c803ca443a76", "maps_spv3/spv3d40.yelo", 525160917UL, 0x7ee6574bUL },
	{ "a8a4fec5-8be4-4f4e-819f-9df62e051f6e", "maps_spv3/ui.yelo", 574822415UL, 0x00605c90UL },
#else
	{ "00000000-0000-4000-8000-000000000001", "lua/spv3/modules/test.lua", 50UL, 0xb9eddbfeUL },
	{ "00000000-0000-4000-8000-000000000002", "maps_spv3/test.yelo", 3000UL, 0xb26b8940UL },
#endif
};

/* (spv3_installer_cancel: set from the game's thread, read by the import's) */
static volatile int spv3_installer_cancelled;

#define STOPPED_MESSAGE "Stopped. Add SPV3 again to carry on where it stopped."

/* ---------- private code */

/* the player's line, and 0 */
static int refuse(char *error, int error_size, const char *format, ...)
{
	va_list arguments;

	if (error && error_size > 0)
	{
		va_start(arguments, format);
		vsnprintf(error, (size_t)error_size, format, arguments);
		va_end(arguments);
	}
	return 0;
}

/* the last part of a path, after its last / or \ (a Windows path has
either) */
static const char *base_name(const char *path)
{
	const char *slash = strrchr(path, '/'), *backslash = strrchr(path, '\\');

	if (backslash && (!slash || backslash > slash))
		slash = backslash;
	return slash ? slash + 1 : path;
}

static int read_at(int descriptor, unsigned long long offset, void *buffer, size_t size)
{
	posix_ulong low, high;
	unsigned char *at = buffer;

	if (posix_seek(descriptor, (posix_long)(offset & 0xffffffffull), (posix_long)(offset >> 32), SEEK_SET, &low,
		&high) != 0)
	{
		return 0;
	}
	while (size > 0)
	{
		ssize_t count = read(descriptor, at, size);

		if (count <= 0)
			return 0;
		at += count;
		size -= (size_t)count;
	}
	return 1;
}

static unsigned long long file_size(int descriptor)
{
	posix_ulong low = 0, high = 0;

	if (posix_seek(descriptor, 0, 0, SEEK_END, &low, &high) != 0)
		return 0;
	return ((unsigned long long)high << 32) | low;
}

static unsigned long long u64le(const unsigned char *at)
{
	return (unsigned long long)content_u32le(at) | ((unsigned long long)content_u32le(at + 4) << 32);
}

/* whether name is a package's: 36 characters of a uuid and ".bin" */
static int is_package_name(const char *name)
{
	size_t index;

	if (strlen(name) != 40 || strcmp(name + 36, ".bin"))
		return 0;
	for (index = 0; index < 36; index++)
	{
		char character = name[index];

		if (index == 8 || index == 13 || index == 18 || index == 23 ? character != '-' :
			!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')))
		{
			return 0;
		}
	}
	return 1;
}

static struct release_entry *release_find(struct release *release, const char *name)
{
	int index;

	for (index = 0; index < release->entry_count; index++)
	{
		if (!strcmp(release->entries[index].name, name))
			return &release->entries[index];
	}
	return NULL;
}

/* the zip's data folder's entries (zip_listed), read from its central
directory; 0 if it is no zip, or not one with a data\manifest.bin */
static int zip_list(struct release *release)
{
	unsigned char tail[ZIP_END_SIZE + ZIP_MAXIMUM_COMMENT];
	unsigned long long size = release->file_size, tail_size, tail_offset, end = 0, directory_offset,
		directory_size, entry_total;
	unsigned char *directory = NULL, *at;
	char prefix[MAXIMUM_NAME] = "";
	int found_end = 0, prefix_known = 0;
	unsigned long long index;
	long position;

	if (size < ZIP_END_SIZE)
		return 0;
	tail_size = size < sizeof(tail) ? size : sizeof(tail);
	tail_offset = size - tail_size;
	if (!read_at(release->descriptor, tail_offset, tail, (size_t)tail_size))
		return 0;
	for (position = (long)tail_size - ZIP_END_SIZE; position >= 0; position--)
	{
		if (content_u32le(tail + position) == ZIP_END_SIGNATURE &&
			(unsigned long long)position + ZIP_END_SIZE + content_u16le(tail + position + 20) <= tail_size)
		{
			end = tail_offset + (unsigned long long)position;
			found_end = 1;
			break;
		}
	}
	if (!found_end)
		return 0;
	at = tail + (end - tail_offset);
	entry_total = content_u16le(at + 10);
	directory_size = content_u32le(at + 12);
	directory_offset = content_u32le(at + 16);
	if (entry_total == 0xffff || directory_size == 0xffffffffull || directory_offset == 0xffffffffull)
	{
		unsigned char locator[ZIP64_LOCATOR_SIZE], record[ZIP64_END_SIZE];
		unsigned long long record_offset;

		if (end < ZIP64_LOCATOR_SIZE || !read_at(release->descriptor, end - ZIP64_LOCATOR_SIZE, locator,
			sizeof(locator)) || content_u32le(locator) != ZIP64_LOCATOR_SIGNATURE)
		{
			return 0;
		}
		record_offset = u64le(locator + 8);
		if (record_offset > size - ZIP64_END_SIZE ||
			!read_at(release->descriptor, record_offset, record, sizeof(record)) ||
			content_u32le(record) != ZIP64_END_SIGNATURE)
		{
			return 0;
		}
		entry_total = u64le(record + 32);
		directory_size = u64le(record + 40);
		directory_offset = u64le(record + 48);
	}
	if (entry_total == 0 || entry_total > MAXIMUM_ENTRIES || directory_size > MAXIMUM_CENTRAL_DIRECTORY ||
		directory_offset > size || directory_size > size - directory_offset)
	{
		return 0;
	}
	directory = malloc((size_t)directory_size + 1);
	release->entries = calloc((size_t)entry_total, sizeof(*release->entries));
	if (!directory || !release->entries || !read_at(release->descriptor, directory_offset, directory,
		(size_t)directory_size))
	{
		free(directory);
		return 0;
	}
	at = directory;
	for (index = 0; index < entry_total; index++)
	{
		unsigned long long left = directory_size - (unsigned long long)(at - directory), compressed, uncompressed,
			local_offset;
		unsigned int name_length, extra_length, comment_length, flags, method;
		char name[MAXIMUM_NAME];
		const char *leaf;
		unsigned char local[ZIP_LOCAL_SIZE];
		struct release_entry *entry;
		unsigned int extra_at;

		if (left < ZIP_CENTRAL_SIZE || content_u32le(at) != ZIP_CENTRAL_SIGNATURE)
			break;
		flags = content_u16le(at + 8);
		method = content_u16le(at + 10);
		compressed = content_u32le(at + 20);
		uncompressed = content_u32le(at + 24);
		name_length = content_u16le(at + 28);
		extra_length = content_u16le(at + 30);
		comment_length = content_u16le(at + 32);
		local_offset = content_u32le(at + 42);
		if ((unsigned long long)ZIP_CENTRAL_SIZE + name_length + extra_length + comment_length > left ||
			name_length >= sizeof(name))
		{
			break;
		}
		memcpy(name, at + ZIP_CENTRAL_SIZE, name_length);
		name[name_length] = '\0';
		/* (zip64's sizes and offset, those the record leaves at their most) */
		for (extra_at = 0; extra_at + 4 <= extra_length;)
		{
			const unsigned char *field = at + ZIP_CENTRAL_SIZE + name_length + extra_at;
			unsigned int id = content_u16le(field), length = content_u16le(field + 2), used = 0;

			if (extra_at + 4 + length > extra_length)
				break;
			if (id == ZIP_EXTRA_ZIP64)
			{
				if (uncompressed == 0xffffffffull && used + 8 <= length)
					uncompressed = u64le(field + 4 + used), used += 8;
				if (compressed == 0xffffffffull && used + 8 <= length)
					compressed = u64le(field + 4 + used), used += 8;
				if (local_offset == 0xffffffffull && used + 8 <= length)
					local_offset = u64le(field + 4 + used), used += 8;
			}
			extra_at += 4 + length;
		}
		at += ZIP_CENTRAL_SIZE + name_length + extra_length + comment_length;

		/* (only the data folder's packages and its manifest, all under one
		folder, at most one folder deep; folders and the rest passed over) */
		{
			char *character;

			for (character = name; *character; character++)
			{
				if (*character == '\\')
					*character = '/';
			}
		}
		leaf = strrchr(name, '/');
		if (!leaf || leaf - name < 4 || strncmp(leaf - 4, "data/", 5) ||
			(strcmp(leaf + 1, "manifest.bin") && !is_package_name(leaf + 1)))
		{
			continue;
		}
		{
			size_t prefix_length = (size_t)(leaf - 4 - name);
			char this_prefix[MAXIMUM_NAME];
			const char *slash;

			snprintf(this_prefix, sizeof(this_prefix), "%.*s", (int)prefix_length, name);
			slash = strchr(this_prefix, '/');
			if (slash && slash[1])
				continue;
			if (!prefix_known)
			{
				snprintf(prefix, sizeof(prefix), "%s", this_prefix);
				prefix_known = 1;
			}
			else if (strcmp(prefix, this_prefix))
			{
				continue;
			}
		}
		if ((flags & ZIP_FLAG_ENCRYPTED) || (method != ZIP_METHOD_STORED && method != ZIP_METHOD_DEFLATED) ||
			local_offset > size - ZIP_LOCAL_SIZE ||
			!read_at(release->descriptor, local_offset, local, sizeof(local)) ||
			content_u32le(local) != ZIP_LOCAL_SIGNATURE)
		{
			continue;
		}
		entry = &release->entries[release->entry_count];
		snprintf(entry->name, sizeof(entry->name), "%s", leaf + 1);
		entry->data_offset = local_offset + ZIP_LOCAL_SIZE + content_u16le(local + 26) + content_u16le(local + 28);
		entry->compressed_size = compressed;
		entry->size = uncompressed;
		entry->method = (int)method;
		/* (within the file; a name the directory gives twice, its first) */
		if (entry->data_offset > size || compressed > size - entry->data_offset ||
			release_find(release, entry->name))
		{
			continue;
		}
		release->entry_count++;
	}
	free(directory);
	return release_find(release, "manifest.bin") != NULL;
}

/* the data folder of an unpacked release: path itself, its data\, or the
folder of a manifest.bin picked */
static int folder_find(const char *path, char *folder, size_t folder_size)
{
	struct posix_file_information information;
	char candidate[PATH_SIZE], on_disk[256];

	if (posix_stat(path, &information) != 0)
		return 0;
	if (!(information.flags & _posix_file_is_directory))
	{
		const char *leaf = base_name(path);
		size_t length = (size_t)(leaf - path);

		if (strcmp(leaf, "manifest.bin") || length == 0)
			return 0;
		snprintf(folder, folder_size, "%.*s", (int)(length - 1), path);
		return 1;
	}
	if (posix_find_entry_case_insensitive(path, "manifest.bin", on_disk, sizeof(on_disk)) &&
		!strcmp(on_disk, "manifest.bin"))
	{
		snprintf(folder, folder_size, "%s", path);
		return 1;
	}
	snprintf(candidate, sizeof(candidate), "%s/data", path);
	if (posix_find_entry_case_insensitive(candidate, "manifest.bin", on_disk, sizeof(on_disk)) &&
		!strcmp(on_disk, "manifest.bin"))
	{
		snprintf(folder, folder_size, "%s", candidate);
		return 1;
	}
	return 0;
}

static void release_close(struct release *release)
{
	if (release->descriptor >= 0)
		close(release->descriptor);
	release->descriptor = -1;
	free(release->entries);
	release->entries = NULL;
}

/* opens the release at path: a zip or a folder; 0 if it is neither */
static int release_open(struct release *release, const char *path)
{
	memset(release, 0, sizeof(*release));
	release->descriptor = -1;
	if (folder_find(path, release->folder, sizeof(release->folder)))
		return 1;
	release->descriptor = open(path, O_RDONLY | O_BINARY | O_LARGEFILE | O_CLOEXEC);
	if (release->descriptor < 0)
		return 0;
	release->file_size = file_size(release->descriptor);
	release->zip = 1;
	if (!zip_list(release))
	{
		release_close(release);
		return 0;
	}
	return 1;
}

/* ---------- private code: streams */

static void stream_close(struct stream *stream)
{
	if (stream->inflating)
		inflateEnd(&stream->inflater);
	stream->inflating = 0;
	free(stream->input);
	stream->input = NULL;
	if (!stream->parent && stream->descriptor >= 0)
		close(stream->descriptor);
	stream->descriptor = -1;
}

static int stream_start(struct stream *stream, int method)
{
	stream->method = method;
	if (method == ZIP_METHOD_DEFLATED)
	{
		stream->input = malloc(CHUNK_SIZE);
		memset(&stream->inflater, 0, sizeof(stream->inflater));
		if (!stream->input || inflateInit2(&stream->inflater, -MAX_WBITS) != Z_OK)
			return 0;
		stream->inflating = 1;
	}
	return method == ZIP_METHOD_STORED || method == ZIP_METHOD_DEFLATED;
}

static long stream_read(struct stream *stream, unsigned char *output, size_t size);

/* up to size of the bytes the stream holds as they are (compressed or not) */
static long stream_raw(struct stream *stream, unsigned char *output, size_t size)
{
	long count;

	if (size > stream->remaining)
		size = (size_t)stream->remaining;
	if (!size)
		return 0;
	if (stream->parent)
		count = stream_read(stream->parent, output, size);
	else
		count = read_at(stream->descriptor, stream->position, output, size) ? (long)size : -1;
	if (count > 0)
	{
		stream->position += (unsigned long long)count;
		stream->remaining -= (unsigned long long)count;
	}
	return count;
}

/* up to size of the entry's bytes; 0 at its end, -1 if it is damaged */
static long stream_read(struct stream *stream, unsigned char *output, size_t size)
{
	if (stream->method == ZIP_METHOD_STORED)
		return stream_raw(stream, output, size);
	if (stream->finished)
		return 0;
	stream->inflater.next_out = output;
	stream->inflater.avail_out = (uInt)size;
	while (stream->inflater.avail_out == (uInt)size)
	{
		int result;

		if (!stream->inflater.avail_in)
		{
			long count = stream_raw(stream, stream->input, CHUNK_SIZE);

			if (count < 0)
				return -1;
			stream->inflater.next_in = stream->input;
			stream->inflater.avail_in = (uInt)count;
		}
		result = inflate(&stream->inflater, Z_NO_FLUSH);
		if (result == Z_STREAM_END)
		{
			stream->finished = 1;
			break;
		}
		if (result != Z_OK || (!stream->inflater.avail_in && !stream->remaining &&
			stream->inflater.avail_out == (uInt)size))
		{
			return -1;
		}
	}
	return (long)(size - stream->inflater.avail_out);
}

/* exactly size bytes */
static int stream_read_all(struct stream *stream, unsigned char *output, size_t size)
{
	while (size)
	{
		long count = stream_read(stream, output, size);

		if (count <= 0)
			return 0;
		output += count;
		size -= (size_t)count;
	}
	return 1;
}

/* an entry of the release's data folder, as stored */
static int release_stream(struct release *release, const char *name, struct stream *stream)
{
	memset(stream, 0, sizeof(*stream));
	stream->descriptor = -1;
	if (release->zip)
	{
		struct release_entry *entry = release_find(release, name);
		int descriptor;

		if (!entry)
			return 0;
		/* (a stream of its own on the zip, so one may be open beside another) */
		descriptor = dup(release->descriptor);
		if (descriptor < 0)
			return 0;
		stream->descriptor = descriptor;
		stream->position = entry->data_offset;
		stream->remaining = entry->compressed_size;
		return stream_start(stream, entry->method);
	}
	else
	{
		char path[PATH_SIZE * 2];

		snprintf(path, sizeof(path), "%s/%s", release->folder, name);
		stream->descriptor = open(path, O_RDONLY | O_BINARY | O_LARGEFILE | O_CLOEXEC);
		if (stream->descriptor < 0)
			return 0;
		stream->remaining = file_size(stream->descriptor);
		return stream_start(stream, ZIP_METHOD_STORED);
	}
}

/* a package's one member: its local header read from the package's stream,
then a stream of the member itself; 0 if the package isn't the one listed */
static int package_member(struct stream *package, const struct spv3_file *file, struct stream *member)
{
	unsigned char header[ZIP_LOCAL_SIZE], skip[512];
	unsigned int flags, method, name_length, extra_length;
	unsigned long long compressed;

	memset(member, 0, sizeof(*member));
	member->descriptor = -1;
	if (!stream_read_all(package, header, sizeof(header)) || content_u32le(header) != ZIP_LOCAL_SIGNATURE)
		return 0;
	flags = content_u16le(header + 6);
	method = content_u16le(header + 8);
	compressed = content_u32le(header + 18);
	name_length = content_u16le(header + 26);
	extra_length = content_u16le(header + 28);
	if ((flags & (ZIP_FLAG_ENCRYPTED | ZIP_FLAG_DESCRIPTOR)) || content_u32le(header + 14) != file->crc ||
		content_u32le(header + 22) != file->size || name_length + extra_length > sizeof(skip) ||
		!stream_read_all(package, skip, name_length + extra_length))
	{
		return 0;
	}
	member->parent = package;
	member->remaining = compressed;
	return stream_start(member, (int)method);
}

/* ---------- private code: writing */

/* the folder a target is written in, and the target's own name there */
struct target_folder
{
	/* (POSIX: the folder, opened from destination one folder at a time;
	otherwise NONE) */
	int descriptor;
	/* (its path: Windows' file calls take paths) */
	char path[PATH_SIZE * 2];
	const char *name;
};

/* the folders on the way to target made under destination and the last one
opened, none of them a link (but destination itself, and the folder at the
target's top) */
static int target_folder_open(const char *destination, const char *target, struct target_folder *folder,
	char *error, int error_size)
{
	const char *start = target, *slash;
	int top = 1;

	/* (the data folder, and the folder a target is in at its top, maps_spv3
	or lua, may be links: a player moves 11 GB to another drive so; a link
	further down is refused) */
	folder->descriptor = -1;
	snprintf(folder->path, sizeof(folder->path), "%s", destination);
#ifdef SPV3_FOLDER_DESCRIPTORS
	folder->descriptor = open(destination, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (folder->descriptor < 0)
		return refuse(error, error_size, "Couldn't open the data folder: %s.", strerror(errno));
#endif
	for (slash = strchr(start, '/'); slash; start = slash + 1, slash = strchr(start, '/'), top = 0)
	{
		char name[MAXIMUM_NAME];
		size_t length = (size_t)(slash - start);

		if (length >= sizeof(name))
			return refuse(error, error_size, "Couldn't make the folders for %s.", target);
		memcpy(name, start, length);
		name[length] = 0;
		snprintf(folder->path, sizeof(folder->path), "%s/%.*s", destination, (int)(slash - target), target);
#ifdef SPV3_FOLDER_DESCRIPTORS
		{
			int next;

			posix_make_directory_at(folder->descriptor, name);
			next = openat(folder->descriptor, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | (top ? 0 : O_NOFOLLOW));
			if (next < 0)
			{
				int reason = errno;

				close(folder->descriptor);
				folder->descriptor = -1;
				if (reason == ELOOP || reason == ENOTDIR)
					return refuse(error, error_size, "%.*s is a link; SPV3 isn't written through one.",
						(int)(slash - target), target);
				return refuse(error, error_size, "Couldn't make %.*s: %s.", (int)(slash - target), target,
					strerror(reason));
			}
			close(folder->descriptor);
			folder->descriptor = next;
		}
#else
		/* (Windows: checked by path, then made; a folder swapped for a link
		between the check and the write is followed. Only someone already
		able to write in the player's data folder, as the player, can do
		that; there is no openat to close it) */
		if (!top && posix_is_link(folder->path))
			return refuse(error, error_size, "%.*s is a link; SPV3 isn't written through one.", (int)(slash - target),
				target);
		posix_make_directory(folder->path);
#endif
	}
	folder->name = start;
	return 1;
}

static void target_folder_close(struct target_folder *folder)
{
#ifdef SPV3_FOLDER_DESCRIPTORS
	if (folder->descriptor >= 0)
		close(folder->descriptor);
#endif
	folder->descriptor = -1;
}

/* a file of the folder opened, never through a link (POSIX) */
static int target_folder_file(const struct target_folder *folder, const char *name, int flags)
{
#ifdef SPV3_FOLDER_DESCRIPTORS
	return openat(folder->descriptor, name, flags | O_BINARY | O_CLOEXEC | O_NOFOLLOW, 0644);
#else
	char path[PATH_SIZE * 2 + MAXIMUM_NAME];

	snprintf(path, sizeof(path), "%s/%s", folder->path, name);
	return open(path, flags | O_BINARY | O_CLOEXEC | O_NOFOLLOW, 0644);
#endif
}

/* (a link's own name removed, never what it points at) */
static void target_folder_remove(const struct target_folder *folder, const char *name)
{
#ifdef SPV3_FOLDER_DESCRIPTORS
	unlinkat(folder->descriptor, name, 0);
#else
	char path[PATH_SIZE * 2 + MAXIMUM_NAME];

	snprintf(path, sizeof(path), "%s/%s", folder->path, name);
	remove(path);
#endif
}

static int target_folder_is_link(const struct target_folder *folder, const char *name)
{
#ifdef SPV3_FOLDER_DESCRIPTORS
	return posix_is_link_at(folder->descriptor, name);
#else
	char path[PATH_SIZE * 2 + MAXIMUM_NAME];

	snprintf(path, sizeof(path), "%s/%s", folder->path, name);
	return posix_is_link(path);
#endif
}

static int target_folder_rename(const struct target_folder *folder, const char *from, const char *to)
{
#ifdef SPV3_FOLDER_DESCRIPTORS
	return renameat(folder->descriptor, from, folder->descriptor, to);
#else
	char from_path[PATH_SIZE * 2 + MAXIMUM_NAME], to_path[PATH_SIZE * 2 + MAXIMUM_NAME];

	snprintf(from_path, sizeof(from_path), "%s/%s", folder->path, from);
	snprintf(to_path, sizeof(to_path), "%s/%s", folder->path, to);
	return posix_replace_file(from_path, to_path);
#endif
}

/* whether the target is there with the size and CRC-32 listed (not when
the player stopped it while it was read) */
static int file_is_right(const struct target_folder *folder, const struct spv3_file *file, unsigned char *buffer,
	cabinet_progress_proc progress, void *context, unsigned long long done, unsigned long long total)
{
	struct posix_file_information information;
	unsigned long long size, read_so_far = 0;
	uLong check = z_crc32(0L, Z_NULL, 0);
	int descriptor;

	if (target_folder_is_link(folder, folder->name))
		return 0;
	descriptor = target_folder_file(folder, folder->name, O_RDONLY | O_LARGEFILE);
	if (descriptor < 0)
		return 0;
	if (posix_fstat(descriptor, &information) != 0 || (information.flags & _posix_file_is_directory))
	{
		close(descriptor);
		return 0;
	}
	size = ((unsigned long long)information.size_high << 32) | information.size_low;
	if (size != file->size)
	{
		close(descriptor);
		return 0;
	}
	while (read_so_far < size && !spv3_installer_cancelled)
	{
		ssize_t count = read(descriptor, buffer, CHUNK_SIZE);

		if (count <= 0)
			break;
		check = z_crc32(check, buffer, (uInt)count);
		read_so_far += (unsigned long long)count;
		if (progress)
			progress(context, base_name(file->target), done + read_so_far, total);
	}
	close(descriptor);
	return read_so_far == size && check == file->crc;
}

/* one file: its package streamed to <target>.part, checked, renamed */
static int install_file(struct release *release, const char *destination, const struct target_folder *folder,
	const struct spv3_file *file, unsigned char *buffer, cabinet_progress_proc progress, void *context,
	unsigned long long done, unsigned long long total, char *error, int error_size)
{
	char package_name[PACKAGE_NAME_SIZE + 8], part[MAXIMUM_NAME + 8];
	struct stream package, member;
	unsigned long long written = 0;
	uLong check = z_crc32(0L, Z_NULL, 0);
	int output, ok = 1;

	snprintf(package_name, sizeof(package_name), "%s.bin", file->package);
	if (!release_stream(release, package_name, &package))
	{
		stream_close(&package);
		platform_log("spv3 installer: no data/%s (%s) in the release", package_name, file->target);
		return refuse(error, error_size, "That SPV3 %s is missing %s.", release->zip ? "zip" : "folder",
			base_name(file->target));
	}
	if (!package_member(&package, file, &member))
	{
		stream_close(&member);
		stream_close(&package);
		return refuse(error, error_size, "That copy of SPV3 is damaged: %s isn't what SPV3 " RELEASE_NAME " has.",
			base_name(file->target));
	}
	snprintf(part, sizeof(part), "%s.part", folder->name);
	if (target_folder_is_link(folder, folder->name))
		ok = refuse(error, error_size, "%s is a link; SPV3 isn't written through one.", file->target);
	/* (whatever is at the .part's name goes first, a stopped run's or a link
	of either kind, so nothing is written through it and an old one doesn't
	count against the free space; the new one is made, never opened) */
	target_folder_remove(folder, part);
	/* (room for its .part beside what is there: the first check counted only
	the files not there at their size) */
	{
		posix_ulong free_low = 0, free_high = 0, total_low = 0, total_high = 0;

		if (ok && posix_disk_space(destination, &free_low, &free_high, &total_low, &total_high) == 0 &&
			(((unsigned long long)free_high << 32) | free_low) < file->size + FREE_SPACE_MARGIN)
		{
			ok = refuse(error, error_size, "SPV3 needs %.1f GB more free here for %s.",
				(double)(file->size + FREE_SPACE_MARGIN) / 1e9, base_name(file->target));
		}
	}
	output = ok ? target_folder_file(folder, part, O_WRONLY | O_CREAT | O_EXCL) : -1;
	if (ok && output < 0)
		ok = refuse(error, error_size, "Couldn't write %s: %s.", file->target, strerror(errno));
	while (ok)
	{
		long count = stream_read(&member, buffer, CHUNK_SIZE);
		long written_now = 0;

		if (spv3_installer_cancelled)
		{
			ok = refuse(error, error_size, STOPPED_MESSAGE);
			break;
		}

		if (count < 0 || written + (unsigned long long)(count > 0 ? count : 0) > file->size)
		{
			ok = refuse(error, error_size, "That copy of SPV3 is damaged: %s doesn't unpack.",
				base_name(file->target));
			break;
		}
		if (!count)
			break;
		check = z_crc32(check, buffer, (uInt)count);
		while (written_now < count)
		{
			ssize_t result = write(output, buffer + written_now, (size_t)(count - written_now));

			if (result <= 0)
			{
				ok = refuse(error, error_size, "Couldn't write %s: %s.", file->target,
					result < 0 ? strerror(errno) : "the disk is full");
				break;
			}
			written_now += result;
		}
		written += (unsigned long long)count;
		if (progress)
			progress(context, base_name(file->target), done + written, total);
	}
	if (ok && (written != file->size || check != file->crc))
	{
		ok = refuse(error, error_size, "That copy of SPV3 is damaged: %s doesn't match SPV3 " RELEASE_NAME ".",
			base_name(file->target));
	}
	if (output >= 0)
	{
		if (ok && posix_sync(output) != 0)
			ok = refuse(error, error_size, "Couldn't write %s: %s.", file->target, strerror(errno));
		close(output);
	}
	if (ok && target_folder_rename(folder, part, folder->name) != 0)
		ok = refuse(error, error_size, "Couldn't put %s in place: %s.", file->target, strerror(errno));
	if (!ok && output >= 0)
		target_folder_remove(folder, part);
	stream_close(&member);
	stream_close(&package);
	return ok;
}

/* the release's manifest: the one SPV3 3.3.0 has */
static int manifest_known(struct release *release, unsigned char *buffer)
{
	struct stream stream;
	unsigned long long size = 0;
	uLong check = z_crc32(0L, Z_NULL, 0);
	int ok = release_stream(release, "manifest.bin", &stream);

	while (ok)
	{
		long count = stream_read(&stream, buffer, CHUNK_SIZE);

		if (count < 0 || size + (unsigned long long)(count > 0 ? count : 0) > RELEASE_MANIFEST_SIZE)
			ok = 0;
		if (count <= 0)
			break;
		check = z_crc32(check, buffer, (uInt)count);
		size += (unsigned long long)count;
	}
	stream_close(&stream);
	return ok && size == RELEASE_MANIFEST_SIZE && check == RELEASE_MANIFEST_CRC;
}

/* ---------- public code */

int spv3_installer_known_lua(const char *relative, const void *data, unsigned long long size)
{
	size_t index;

	if (!relative || !data)
		return 0;
	for (index = 0; index < sizeof(spv3_files) / sizeof(*spv3_files); index++)
	{
		const struct spv3_file *file = &spv3_files[index];
		const char *a = file->target, *b = relative;

		if (strncmp(file->target, "lua/", 4) || file->size != size)
			continue;
		/* (the names as Windows' are, case ignored, either slash) */
		while (*a && *b)
		{
			char x = *a == '\\' ? '/' : *a, y = *b == '\\' ? '/' : *b;

			if (x >= 'A' && x <= 'Z')
				x = (char)(x - 'A' + 'a');
			if (y >= 'A' && y <= 'Z')
				y = (char)(y - 'A' + 'a');
			if (x != y)
				break;
			a++;
			b++;
		}
		if (*a || *b)
			continue;
		return z_crc32(z_crc32(0L, Z_NULL, 0), (const Bytef *)data, (uInt)size) == file->crc;
	}
	return 0;
}

void spv3_installer_cancel(void)
{
	spv3_installer_cancelled = 1;
}

int spv3_installer_is_release(const char *path)
{
	struct release release;

	if (!path || !*path || !release_open(&release, path))
		return 0;
	release_close(&release);
	return 1;
}

int spv3_installer_install(const char *release_path, const char *destination, cabinet_progress_proc progress,
	void *context, char *error, int error_size)
{
	struct release release;
	unsigned char *buffer;
	unsigned long long total = 0, needed = 0, done = 0;
	posix_ulong free_low = 0, free_high = 0, total_low = 0, total_high = 0;
	size_t index;
	int ok = 1;

	spv3_installer_cancelled = 0;
	if (!release_open(&release, release_path))
		return refuse(error, error_size, "That isn't SPV3's release zip or the folder it was unpacked to.");
	buffer = malloc(CHUNK_SIZE);
	if (!buffer)
	{
		release_close(&release);
		return refuse(error, error_size, "Not enough memory to install SPV3.");
	}
	if (progress)
		progress(context, "manifest.bin", 0, 0);
	if (!manifest_known(&release, buffer))
	{
		free(buffer);
		release_close(&release);
		return refuse(error, error_size, "That's SPV3, but not version " RELEASE_NAME ", the one this build knows.");
	}
	/* (the room it needs: what isn't there at its size yet) */
	for (index = 0; index < sizeof(spv3_files) / sizeof(*spv3_files); index++)
	{
		char path[PATH_SIZE * 2];
		struct posix_file_information information;

		total += spv3_files[index].size;
		snprintf(path, sizeof(path), "%s/%s", destination, spv3_files[index].target);
		if (posix_stat(path, &information) != 0 ||
			(((unsigned long long)information.size_high << 32) | information.size_low) != spv3_files[index].size)
		{
			needed += spv3_files[index].size;
		}
	}
	posix_make_directory(destination);
	if (needed && posix_disk_space(destination, &free_low, &free_high, &total_low, &total_high) == 0)
	{
		unsigned long long available = ((unsigned long long)free_high << 32) | free_low;

		if (available < needed + FREE_SPACE_MARGIN)
		{
			free(buffer);
			release_close(&release);
			return refuse(error, error_size, "SPV3 needs %.1f GB free here; there's %.1f GB.",
				(double)(needed + FREE_SPACE_MARGIN) / 1e9, (double)available / 1e9);
		}
	}
	platform_log("spv3 installer: SPV3 " RELEASE_NAME " from %s %s, %u files, %llu MB, %llu MB to write",
		release.zip ? "a zip" : "a folder", release_path, (unsigned)(sizeof(spv3_files) / sizeof(*spv3_files)),
		total >> 20, needed >> 20);
	for (index = 0; ok && index < sizeof(spv3_files) / sizeof(*spv3_files); index++)
	{
		const struct spv3_file *file = &spv3_files[index];
		struct target_folder folder;

		/* (Cancel counts while the files there are checked too, not only
		while one is written) */
		if (spv3_installer_cancelled)
		{
			ok = refuse(error, error_size, STOPPED_MESSAGE);
			break;
		}
		ok = target_folder_open(destination, file->target, &folder, error, error_size);
		if (!ok)
		{
			target_folder_close(&folder);
			break;
		}
		if (file_is_right(&folder, file, buffer, progress, context, done, total))
		{
			char part[MAXIMUM_NAME + 8];

			/* (a .part a run that stopped left beside a file now right: its
			name removed, a link's too) */
			snprintf(part, sizeof(part), "%s.part", folder.name);
			target_folder_remove(&folder, part);
		}
		else if (spv3_installer_cancelled)
		{
			ok = refuse(error, error_size, STOPPED_MESSAGE);
		}
		else
		{
			ok = install_file(&release, destination, &folder, file, buffer, progress, context, done, total, error,
				error_size);
			if (ok)
				platform_log("spv3 installer: wrote %s (%llu bytes)", file->target, file->size);
		}
		target_folder_close(&folder);
		done += file->size;
		if (progress)
			progress(context, base_name(file->target), done, total);
	}
	free(buffer);
	release_close(&release);
	if (ok)
		platform_log("spv3 installer: SPV3 " RELEASE_NAME " is in maps_spv3 and lua/spv3");
	else
		platform_log("spv3 installer: stopped: %s", error);
	return ok;
}

#endif
