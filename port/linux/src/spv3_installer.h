/*
SPV3_INSTALLER.H

SPV3 put in place from the player's own copy of its release (spv3_installer.c):
its maps into maps_spv3 and its Lua scripts into lua/spv3, for SPV3 mode
(port/linux/game/spv3_mode.c). The player brings the release; the game never
downloads it.
*/

#ifndef __HALO_LINUX_SPV3_INSTALLER_H
#define __HALO_LINUX_SPV3_INSTALLER_H

#include "cabinet.h"

/* whether the path is SPV3's release: its zip, the folder it was unpacked to,
or that folder's data folder or data/manifest.bin (any version: which one is
told when it is installed) */
int spv3_installer_is_release(const char *path);

/* SPV3's maps and scripts written under destination (the data folder: its
maps_spv3 and lua/spv3), each checked against the release this build knows;
what is there already and right is kept, so a second run resumes or repairs.
As pc_disc_extract_maps (pc_disc.h): 1 on success, else 0 with a line for the
player in error */
int spv3_installer_install(const char *release_path, const char *destination, cabinet_progress_proc progress,
	void *context, char *error, int error_size);

/* the install under way stopped at its next chunk, its .part removed
(the setup screen's Cancel, on the game's thread) */
void spv3_installer_cancel(void);

/* whether data (size bytes) is one of the Lua files the installer writes for
the release this build knows: relative is its path under the data folder
(lua/spv3/..., case ignored, either slash), and its size and CRC-32 must be
that file's. SPV3 mode's scripts run without lua.scripts only from these
(port/linux/game/lua_scripting.c) */
int spv3_installer_known_lua(const char *relative, const void *data, unsigned long long size);

#endif
