/*
HALO_STUB.C

halo.exe for the Windows downloads after the rename (port/linux/src/
rename_stub.h): starts chupathingy.exe from its own folder with the same
arguments and working folder, waits for it, and exits with its exit code,
so a shortcut or a Steam entry for halo.exe sees the game run as before.
Nothing but kernel32 and user32 (tools/windows_build.py links it on its
own).

TODO(after 0.9.x): remove, with the other stubs.
*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "rename_stub.h"

/* (found by the renamed game's clean-up, which keeps this file) */
static const char rename_stub_mark[] = RENAME_STUB_MARK;

#define RENAME_STUB_PATH 32768

static wchar_t program[RENAME_STUB_PATH];
static wchar_t command[RENAME_STUB_PATH + 64];

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR arguments_given, int show)
{
	const volatile char *mark = rename_stub_mark;
	const wchar_t *arguments;
	wchar_t *slash;
	DWORD length, code = 1;
	STARTUPINFOW startup;
	PROCESS_INFORMATION process;

	(void)instance;
	(void)previous;
	(void)arguments_given;
	(void)show;
	if (!mark[0])
		return 1;
	length = GetModuleFileNameW(NULL, program, RENAME_STUB_PATH);
	if (!length || length >= RENAME_STUB_PATH)
		return 1;
	slash = wcsrchr(program, L'\\');
	if (!slash || (size_t)(slash + 1 - program) + 16 >= RENAME_STUB_PATH)
		return 1;
	wcscpy(slash + 1, L"chupathingy.exe");
	arguments = rename_stub_arguments(GetCommandLineW());
	if (wcslen(program) + wcslen(arguments) + 4 >= sizeof(command) / sizeof(*command))
		return 1;
	wcscpy(command, L"\"");
	wcscat(command, program);
	wcscat(command, L"\"");
	if (*arguments)
	{
		wcscat(command, L" ");
		wcscat(command, arguments);
	}
	GetStartupInfoW(&startup);
	if (!CreateProcessW(program, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process))
	{
		MessageBoxW(NULL, L"Could not start chupathingy.exe. It should be in the same folder as halo.exe.",
			L"Chupathingy", MB_OK | MB_ICONERROR);
		return 1;
	}
	CloseHandle(process.hThread);
	WaitForSingleObject(process.hProcess, INFINITE);
	if (!GetExitCodeProcess(process.hProcess, &code))
		code = 1;
	CloseHandle(process.hProcess);
	return (int)code;
}
