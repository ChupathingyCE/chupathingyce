/*
PACKAGED_INSTALL.H

A packaged install: the game as a package manager put it on the computer
(the AUR's chupathingy-bin, a distribution's package), which updates it,
in a folder the player cannot write. The build marks one (configure.py
--packaged: HALO_PACKAGED), or, for a package of a release's own
executable, which is signed and not rebuilt, the package does at run time:
HALO_PACKAGED=1 in the environment, or a file named "packaged" (any
contents) in the executable's folder. Then the game never updates itself
(updater.c: a new version says to update with the package manager),
update.auto is off and stays off (port_config.c), and on Linux its data,
settings and logs go in the save root's usual place,
~/.local/share/chupathingyce, as an AppImage's do (platform_app_folder).

No SDL, system or game dependency (the caller looks for the file): port/linux/tests/packaged_install_test.c builds
it on its own (tools/test_packaged_install.py).
*/

#ifndef __HALO_PACKAGED_INSTALL_H
#define __HALO_PACKAGED_INSTALL_H

#include <stdio.h>
#include <string.h>

/* the file in the executable's folder that marks a packaged install */
#define PACKAGED_INSTALL_FILE "packaged"

/* whether text (HALO_PACKAGED's value) says no: "", "0", "false", "no" or "off" */
static inline int packaged_install_text_is_no(const char *text)
{
	static const char *const no[] = { "", "0", "false", "no", "off" };
	unsigned long index;

	for (index = 0; index < sizeof(no) / sizeof(no[0]); index++)
	{
		const char *a = text, *b = no[index];

		while (*a && *b && (*a | 0x20) == *b)
		{
			a++;
			b++;
		}
		if (!*a && !*b)
			return 1;
	}
	return 0;
}

/* the marking file's path in folder (the executable's, with or without its
separator) into path: 0 when folder is NULL or "" */
static inline int packaged_install_file_path(char *path, size_t size, const char *folder)
{
	size_t length;

	if (!folder || !*folder)
		return 0;
	length = strlen(folder);
	snprintf(path, size, "%s%s" PACKAGED_INSTALL_FILE, folder,
		folder[length - 1] == '/' || folder[length - 1] == '\\' ? "" : "/");
	return 1;
}

/* whether this is a packaged install: built as one, or environment
(HALO_PACKAGED's value, NULL when it is not set) says so, or the file is
there (file_present: a file, not a folder, at packaged_install_file_path) */
static inline int packaged_install_marked(const char *environment, int file_present)
{
#ifdef HALO_PACKAGED
	(void)environment;
	(void)file_present;
	return 1;
#else
	return file_present || (environment && !packaged_install_text_is_no(environment));
#endif
}

/* whether the game's own files (its data, config.toml and logs) go in the
save root's usual place, not the executable's folder (Linux): an AppImage
(mounted read-only), a packaged install, or an executable's folder the game
cannot write (folder_writable 0) that holds no setup of its own (no maps/
and no config.toml: folder_has_setup 0), which is kept where it is */
static inline int packaged_install_files_in_home(int appimage, int packaged, int folder_writable,
	int folder_has_setup)
{
	return appimage || packaged || (!folder_writable && !folder_has_setup);
}

#endif
