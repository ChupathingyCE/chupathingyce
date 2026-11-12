/*
WIN32_STEAM_BRIDGE.C

The Steam bridge's platform functions on Windows (steam_client_bridge.h):
MCC's steam_api64.dll loaded from its own folder (so its neighbors load
with it), and SteamAppId set in this process's environment, both the
Windows one and the C runtime's. steamcmd isn't used here: Windows players
have the Steam client.
*/

#include <windows.h>
#include <stdlib.h>
#include "steam_client_bridge.h"

void *steam_bridge_platform_library_open(const char *path)
{
	wchar_t wide[MAX_PATH];
	char copy[MAX_PATH];
	int index;

	/* (forward slashes from our joins: LOAD_WITH_ALTERED_SEARCH_PATH wants backslashes) */
	for (index = 0; path[index] && index < MAX_PATH - 1; index++)
		copy[index] = path[index] == '/' ? '\\' : path[index];
	copy[index] = 0;
	if (!MultiByteToWideChar(CP_UTF8, 0, copy, -1, wide, MAX_PATH))
		return NULL;
	return (void *)LoadLibraryExW(wide, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
}

void *steam_bridge_platform_library_symbol(void *library, const char *name)
{
	return library ? (void *)GetProcAddress((HMODULE)library, name) : NULL;
}

void steam_bridge_platform_set_app_id(const char *app_id)
{
	SetEnvironmentVariableA("SteamAppId", app_id);
	SetEnvironmentVariableA("SteamGameId", app_id);
	_putenv_s("SteamAppId", app_id);
	_putenv_s("SteamGameId", app_id);
}

void steam_bridge_platform_sleep(int milliseconds)
{
	Sleep((DWORD)milliseconds);
}
