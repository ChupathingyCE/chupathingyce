/*
WIN32_STEAM_WORKSHOP.C

Registered Steam installations for the MCC Workshop catalog. This Windows-facing
unit keeps the Windows SDK separate from the Xbox-facing platform headers.
*/

#include <windows.h>
#include "steam_workshop.h"

static int registry_steam_root(HKEY key, const char *value, REGSAM view, char *path)
{
	HKEY steam_key;
	DWORD size = STEAM_WORKSHOP_PATH_SIZE;
	LONG result;

	/* RegGetValue checks the string type, bounds and termination. Use the same
	 * narrow encoding as win32_files.c and the runtime's file functions. */
	if (RegOpenKeyExA(key, "Software\\Valve\\Steam", 0, KEY_QUERY_VALUE | view, &steam_key) != ERROR_SUCCESS)
		return 0;
	result = RegGetValueA(steam_key, NULL, value, RRF_RT_REG_SZ, NULL, path, &size);
	RegCloseKey(steam_key);
	return result == ERROR_SUCCESS && path[0] != 0;
}

int steam_workshop_windows_roots(char (*paths)[STEAM_WORKSHOP_PATH_SIZE], int maximum)
{
	int count = 0;

	/* The game is 32-bit, but Steam may register its installation in either
	 * machine-wide registry view. Missing registrations leave the fallbacks. */
	if (count < maximum && registry_steam_root(HKEY_CURRENT_USER, "SteamPath", 0, paths[count]))
		count++;
	if (count < maximum && registry_steam_root(HKEY_LOCAL_MACHINE, "InstallPath", KEY_WOW64_32KEY, paths[count]))
		count++;
	if (count < maximum && registry_steam_root(HKEY_LOCAL_MACHINE, "InstallPath", KEY_WOW64_64KEY, paths[count]))
		count++;
	return count;
}
