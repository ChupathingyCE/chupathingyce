/*
WIN32_STEAM_BRIDGE.C

The Steam bridge's platform functions on Windows (steam_client_bridge.h):
MCC's steam_api64.dll loaded from its own folder (so its neighbors load
with it), and SteamAppId set in this process's environment, both the
Windows one and the C runtime's; and the folders of the game's own
downloads (the built-in Steam download's, steam_bridge_pipe.c), looked at
and removed without following links. steamcmd isn't used here.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h): a console builds this to nothing */

#include <windows.h>
#include <stdio.h>
#include <string.h>
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

int steam_bridge_platform_is_folder(const char *path)
{
	DWORD attributes = GetFileAttributesA(path);

	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
		!(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

int steam_bridge_platform_remove_folder(const char *path)
{
	char pattern[MAX_PATH + 4], child[MAX_PATH + 1];
	WIN32_FIND_DATAA found;
	HANDLE search;
	DWORD attributes = GetFileAttributesA(path);

	if (attributes == INVALID_FILE_ATTRIBUTES)
		return 1;
	/* (a link is removed itself, never what it points at) */
	if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
		return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryA(path) != 0 : DeleteFileA(path) != 0;
	if (snprintf(pattern, sizeof(pattern), "%s\\*", path) >= (int)sizeof(pattern))
		return 0;
	search = FindFirstFileA(pattern, &found);
	if (search != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (!strcmp(found.cFileName, ".") || !strcmp(found.cFileName, ".."))
				continue;
			if (snprintf(child, sizeof(child), "%s\\%s", path, found.cFileName) >= (int)sizeof(child))
				continue;
			if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
				!(found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
			{
				steam_bridge_platform_remove_folder(child);
			}
			else if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				RemoveDirectoryA(child);
			else
			{
				SetFileAttributesA(child, FILE_ATTRIBUTE_NORMAL);
				DeleteFileA(child);
			}
		} while (FindNextFileA(search, &found));
		FindClose(search);
	}
	return RemoveDirectoryA(path) != 0;
}
#endif /* HALO_MCC */
