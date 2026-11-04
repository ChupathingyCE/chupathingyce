/*
SERVER_WINDOWS.C

The Windows server's own (server_platform.c calls it; tools/windows_build.py
builds it for the Windows server only): what a POSIX server does with
signals and a service manager's unit file, Windows does with these.

- Ctrl+C, Ctrl+Break and the console window closed stop the server at the
  next frame, as SIGINT and SIGTERM do on Linux; it takes its game off the
  lists on the way out. (Closing the window gives a program a few seconds:
  the handler waits that long for the server to end.)
- As a Windows service (--service, which --install-service puts in the
  service's command line): the service control manager is answered from a
  thread of its own, and Stop (sc stop, the Services panel, a shutdown) is
  the same as Ctrl+C. Without a console, the server's output (its setup
  code and tokens, printed once, and its errors) goes to
  logs\server-output.txt, and its log to logs\ as always.
- --install-service [name] makes the service (ChupathingyServer unless
  named), from an administrator's command prompt in the data folder: it
  starts with Windows, runs as its own virtual account (NT SERVICE\<name>,
  not SYSTEM), which is given the data folder, restarts after a crash, and
  has the HALO_* settings of the prompt it was made from (start-server.bat
  sets them), with HALO_DATA_ROOT and HALO_SAVE_ROOT set to this folder
  unless they are. --uninstall-service [name] stops it and takes it out.

Built with the host's ABI (the Windows SDK, not the Xbox's), and only plain
types cross to server_platform.c.
*/

#include <windows.h>
#include <aclapi.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define DEFAULT_SERVICE_NAME L"ChupathingyServer"
/* a service's name: letters, digits, - and _ (sc and the registry take
more, but a name people type stays plain) */
#define SERVICE_NAME_SIZE 64

/* the platform layer's (xbox_files.c): the logs folder, before the game starts */
int platform_logs_folder_early(char *path, unsigned long size);

static void (*stop_function)(void);
static volatile LONG stop_asked;

/* ---------- Ctrl+C and the console window */

static BOOL WINAPI console_handler(DWORD event)
{
	switch (event)
	{
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
		InterlockedExchange(&stop_asked, 1);
		if (stop_function)
			stop_function();
		return TRUE;
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		/* (the process ends when this returns: the server's own exit, at its
		next frame, comes first) */
		InterlockedExchange(&stop_asked, 1);
		if (stop_function)
			stop_function();
		Sleep(4000);
		return TRUE;
	default:
		return FALSE;
	}
}

void server_windows_console_handlers(void (*stop)(void))
{
	stop_function = stop;
	SetConsoleCtrlHandler(console_handler, TRUE);
}

/* ---------- running as a service */

static SERVICE_STATUS_HANDLE service_status_handle;
static SERVICE_STATUS service_status;
static HANDLE service_ready;
static HANDLE service_finished;

static void report_status(DWORD state, DWORD exit_code, DWORD wait_hint)
{
	static DWORD check_point = 1;

	service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	service_status.dwCurrentState = state;
	service_status.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
	service_status.dwWin32ExitCode = exit_code ? ERROR_SERVICE_SPECIFIC_ERROR : NO_ERROR;
	service_status.dwServiceSpecificExitCode = exit_code;
	service_status.dwWaitHint = wait_hint;
	service_status.dwCheckPoint = state == SERVICE_RUNNING || state == SERVICE_STOPPED ? 0 : check_point++;
	if (service_status_handle)
		SetServiceStatus(service_status_handle, &service_status);
}

static DWORD WINAPI service_control(DWORD control, DWORD event_type, LPVOID event_data, LPVOID context)
{
	(void)event_type;
	(void)event_data;
	(void)context;
	switch (control)
	{
	case SERVICE_CONTROL_STOP:
	case SERVICE_CONTROL_SHUTDOWN:
		report_status(SERVICE_STOP_PENDING, 0, 20000);
		InterlockedExchange(&stop_asked, 1);
		if (stop_function)
			stop_function();
		return NO_ERROR;
	case SERVICE_CONTROL_INTERROGATE:
		return NO_ERROR;
	default:
		return ERROR_CALL_NOT_IMPLEMENTED;
	}
}

static void WINAPI service_main(DWORD count, LPWSTR *arguments)
{
	(void)count;
	(void)arguments;
	service_status_handle = RegisterServiceCtrlHandlerExW(L"", service_control, NULL);
	if (!service_status_handle)
		return;
	report_status(SERVICE_RUNNING, 0, 0);
	SetEvent(service_ready);
	/* (the process ends with the server: server_windows_service_exit) */
	WaitForSingleObject(service_finished, INFINITE);
}

static DWORD WINAPI dispatcher_thread(LPVOID unused)
{
	static SERVICE_TABLE_ENTRYW table[] = { { L"", service_main }, { NULL, NULL } };

	(void)unused;
	if (!StartServiceCtrlDispatcherW(table))
		return GetLastError();
	return 0;
}

/* at exit: the service stopped (a stop asked for, or the server's own end,
which the service manager's recovery restarts when it was not asked for) */
static void service_exit(void)
{
	report_status(SERVICE_STOPPED, stop_asked ? 0 : 1, 0);
}

/* the folder of this program, with no separator after it */
static void program_folder(wchar_t *folder, DWORD size)
{
	DWORD length = GetModuleFileNameW(NULL, folder, size);
	wchar_t *slash;

	if (!length || length >= size)
	{
		folder[0] = 0;
		return;
	}
	slash = wcsrchr(folder, L'\\');
	if (slash)
		*slash = 0;
}

/* started by the service manager: 1, else 0 (and why, on standard error) */
int server_windows_service_start(void (*stop)(void))
{
	HANDLE handles[2];
	HANDLE thread;
	DWORD waited;
	char logs[1024];
	wchar_t folder[MAX_PATH];
	const char *root = getenv("HALO_DATA_ROOT");

	stop_function = stop;
	/* (a service starts in the system's folder: the data folder, else the
	program's, is the current one) */
	if (root && root[0])
		SetCurrentDirectoryA(root);
	else
	{
		program_folder(folder, MAX_PATH);
		if (folder[0])
			SetCurrentDirectoryW(folder);
	}
	service_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
	service_finished = CreateEventW(NULL, TRUE, FALSE, NULL);
	thread = service_ready && service_finished ? CreateThread(NULL, 0, dispatcher_thread, NULL, 0, NULL) : NULL;
	if (!thread)
	{
		fprintf(stderr, "chupathingyce-server: --service: cannot start (error %lu)\n", GetLastError());
		return 0;
	}
	handles[0] = service_ready;
	handles[1] = thread;
	waited = WaitForMultipleObjects(2, handles, FALSE, 30000);
	if (waited != WAIT_OBJECT_0)
	{
		DWORD error = 0;

		GetExitCodeThread(thread, &error);
		fprintf(stderr, "chupathingyce-server: --service is for the service manager (--install-service makes the service)%s\n",
			error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT ? "" : ": it did not answer");
		return 0;
	}
	CloseHandle(thread);
	atexit(service_exit);
	/* the server's output (its setup code and new tokens, printed once) and
	its errors (as "no maps"), to a file in the logs folder: a service has no
	console */
	if ((_fileno(stdout) < 0 || _fileno(stderr) < 0) && platform_logs_folder_early(logs, sizeof(logs)) &&
		strlen(logs) + sizeof("\\server-output.txt") <= sizeof(logs))
	{
		CreateDirectoryA(logs, NULL);
		strcat(logs, "\\server-output.txt");
		if (_fileno(stdout) < 0 && freopen(logs, "a", stdout))
			setvbuf(stdout, NULL, _IONBF, 0);
		if (_fileno(stderr) < 0 && freopen(logs, "a", stderr))
			setvbuf(stderr, NULL, _IONBF, 0);
	}
	return 1;
}

/* ---------- installing the service */

static int valid_service_name(const wchar_t *name)
{
	size_t index, length = wcslen(name);

	if (!length || length >= SERVICE_NAME_SIZE)
		return 0;
	for (index = 0; index < length; index++)
	{
		wchar_t character = name[index];

		if (!((character >= L'a' && character <= L'z') || (character >= L'A' && character <= L'Z') ||
			(character >= L'0' && character <= L'9') || character == L'-' || character == L'_'))
			return 0;
	}
	return 1;
}

static int service_name(const char *given, wchar_t *name)
{
	if (!given || !given[0])
	{
		wcscpy(name, DEFAULT_SERVICE_NAME);
		return 1;
	}
	if (!MultiByteToWideChar(CP_UTF8, 0, given, -1, name, SERVICE_NAME_SIZE) || !valid_service_name(name))
	{
		fprintf(stderr, "chupathingyce-server: \"%s\" cannot be a service's name (letters, digits, - and _, "
			"63 at most)\n", given);
		return 0;
	}
	return 1;
}

static void print_error(const char *what, DWORD error)
{
	if (error == ERROR_ACCESS_DENIED)
		fprintf(stderr, "chupathingyce-server: %s: access denied. Run it from a command prompt opened with "
			"\"Run as administrator\".\n", what);
	else
		fprintf(stderr, "chupathingyce-server: %s: error %lu\n", what, error);
}

/* the service's settings: the HALO_* variables set here (start-server.bat's),
and the data and save folders, as a REG_MULTI_SZ; its length in wchar_ts */
static size_t service_environment(const wchar_t *data_folder, wchar_t *block, size_t size)
{
	wchar_t *strings = GetEnvironmentStringsW();
	const wchar_t *entry;
	size_t used = 0;
	int has_data = 0, has_save = 0;
	wchar_t line[MAX_PATH + 32];

	block[0] = 0;
	for (entry = strings; entry && *entry; entry += wcslen(entry) + 1)
	{
		size_t length = wcslen(entry);

		if (_wcsnicmp(entry, L"HALO_", 5) || used + length + 2 > size)
			continue;
		has_data |= !_wcsnicmp(entry, L"HALO_DATA_ROOT=", 15);
		has_save |= !_wcsnicmp(entry, L"HALO_SAVE_ROOT=", 15);
		memcpy(block + used, entry, (length + 1) * sizeof(wchar_t));
		used += length + 1;
	}
	if (strings)
		FreeEnvironmentStringsW(strings);
	if (!has_data)
	{
		_snwprintf(line, sizeof(line) / sizeof(line[0]), L"HALO_DATA_ROOT=%ls", data_folder);
		line[sizeof(line) / sizeof(line[0]) - 1] = 0;
		if (used + wcslen(line) + 2 <= size)
		{
			wcscpy(block + used, line);
			used += wcslen(line) + 1;
		}
	}
	if (!has_save)
	{
		_snwprintf(line, sizeof(line) / sizeof(line[0]), L"HALO_SAVE_ROOT=%ls\\saves", data_folder);
		line[sizeof(line) / sizeof(line[0]) - 1] = 0;
		if (used + wcslen(line) + 2 <= size)
		{
			wcscpy(block + used, line);
			used += wcslen(line) + 1;
		}
	}
	block[used++] = 0;
	return used;
}

/* the service's own account given the data folder (read, write, make and
delete, for its logs, saves and settings), inherited by what is in it */
static DWORD grant_folder(const wchar_t *folder, const wchar_t *account)
{
	PACL old_list = NULL, new_list = NULL;
	PSECURITY_DESCRIPTOR descriptor = NULL;
	EXPLICIT_ACCESSW access;
	DWORD error;

	error = GetNamedSecurityInfoW(folder, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &old_list, NULL,
		&descriptor);
	if (error != ERROR_SUCCESS)
		return error;
	memset(&access, 0, sizeof(access));
	access.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE | DELETE;
	access.grfAccessMode = GRANT_ACCESS;
	access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
	access.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
	access.Trustee.TrusteeType = TRUSTEE_IS_USER;
	access.Trustee.ptstrName = (LPWSTR)account;
	error = SetEntriesInAclW(1, &access, old_list, &new_list);
	if (error == ERROR_SUCCESS)
		error = SetNamedSecurityInfoW((LPWSTR)folder, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL,
			new_list, NULL);
	if (new_list)
		LocalFree(new_list);
	if (descriptor)
		LocalFree(descriptor);
	return error;
}

/* 0, else the exit status */
int server_windows_install_service(const char *given_name)
{
	wchar_t name[SERVICE_NAME_SIZE];
	wchar_t program[MAX_PATH];
	wchar_t folder[MAX_PATH];
	wchar_t command[MAX_PATH + 32];
	wchar_t account[SERVICE_NAME_SIZE + 16];
	wchar_t display[SERVICE_NAME_SIZE + 64];
	wchar_t key[SERVICE_NAME_SIZE + 64];
	static wchar_t environment[16384];
	size_t environment_length;
	SERVICE_DESCRIPTIONW description;
	SC_ACTION actions[3];
	SERVICE_FAILURE_ACTIONSW failure;
	SERVICE_FAILURE_ACTIONS_FLAG failure_flag;
	SC_HANDLE manager, service;
	DWORD error;
	const char *playlist = getenv("HALO_DEDICATED");

	if (!service_name(given_name, name))
		return 2;
	if (!playlist || !playlist[0])
	{
		fprintf(stderr, "chupathingyce-server: --install-service: set HALO_DEDICATED to a playlist first "
			"(\"start-server.bat install\" sets it, with the rest of your settings)\n");
		return 2;
	}
	if (!GetModuleFileNameW(NULL, program, MAX_PATH) || !GetCurrentDirectoryW(MAX_PATH, folder))
	{
		print_error("--install-service", GetLastError());
		return 1;
	}
	_snwprintf(command, sizeof(command) / sizeof(command[0]), L"\"%ls\" --service", program);
	command[sizeof(command) / sizeof(command[0]) - 1] = 0;
	_snwprintf(account, sizeof(account) / sizeof(account[0]), L"NT SERVICE\\%ls", name);
	account[sizeof(account) / sizeof(account[0]) - 1] = 0;
	_snwprintf(display, sizeof(display) / sizeof(display[0]), L"Chupathingy Dedicated Server (%ls)", name);
	display[sizeof(display) / sizeof(display[0]) - 1] = 0;

	manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
	if (!manager)
	{
		print_error("--install-service", GetLastError());
		return 1;
	}
	service = CreateServiceW(manager, name, display, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
		SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, command, NULL, NULL, NULL, account, NULL);
	if (!service)
	{
		error = GetLastError();
		CloseServiceHandle(manager);
		if (error == ERROR_SERVICE_EXISTS)
			fprintf(stderr, "chupathingyce-server: there is a service named %ls already (--uninstall-service %ls "
				"takes it out, or give this one another name)\n", name, name);
		else
			print_error("--install-service", error);
		return 1;
	}
	description.lpDescription = L"Hosts Halo games from a playlist (Chupathingy). Its settings are the service's "
		L"HALO_* environment; its log is in the data folder's logs folder.";
	ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &description);
	/* (a crash, or an end it was not asked for, restarts it after 10 seconds;
	the count resets after a day) */
	actions[0].Type = actions[1].Type = actions[2].Type = SC_ACTION_RESTART;
	actions[0].Delay = actions[1].Delay = actions[2].Delay = 10000;
	memset(&failure, 0, sizeof(failure));
	failure.dwResetPeriod = 86400;
	failure.cActions = 3;
	failure.lpsaActions = actions;
	ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, &failure);
	failure_flag.fFailureActionsOnNonCrashFailures = TRUE;
	ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &failure_flag);

	environment_length = service_environment(folder, environment, sizeof(environment) / sizeof(environment[0]));
	_snwprintf(key, sizeof(key) / sizeof(key[0]), L"SYSTEM\\CurrentControlSet\\Services\\%ls", name);
	key[sizeof(key) / sizeof(key[0]) - 1] = 0;
	error = RegSetKeyValueW(HKEY_LOCAL_MACHINE, key, L"Environment", REG_MULTI_SZ, environment,
		(DWORD)(environment_length * sizeof(wchar_t)));
	if (error != ERROR_SUCCESS)
		print_error("the service's settings", error);
	error = grant_folder(folder, account);
	if (error != ERROR_SUCCESS)
		print_error("giving the service the data folder", error);

	printf("Made the service %ls (%ls), which starts with Windows, with these settings:\n", name, display);
	{
		const wchar_t *entry;

		for (entry = environment; *entry; entry += wcslen(entry) + 1)
			printf("  %ls\n", entry);
	}
	if (StartServiceW(service, 0, NULL))
		printf("It is starting. Its log: %ls\\logs\\debug.txt; its output (the control panel's setup code): "
			"%ls\\logs\\server-output.txt.\n", folder, folder);
	else
		print_error("starting the service (sc start, or the Services panel, starts it)", GetLastError());
	printf("To change a setting, run the install again after --uninstall-service %ls.\n", name);
	CloseServiceHandle(service);
	CloseServiceHandle(manager);
	return 0;
}

/* 0, else the exit status */
int server_windows_uninstall_service(const char *given_name)
{
	wchar_t name[SERVICE_NAME_SIZE];
	SC_HANDLE manager, service;
	SERVICE_STATUS status;
	int waited;

	if (!service_name(given_name, name))
		return 2;
	manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
	if (!manager)
	{
		print_error("--uninstall-service", GetLastError());
		return 1;
	}
	service = OpenServiceW(manager, name, SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
	if (!service)
	{
		DWORD error = GetLastError();

		CloseServiceHandle(manager);
		if (error == ERROR_SERVICE_DOES_NOT_EXIST)
			fprintf(stderr, "chupathingyce-server: there is no service named %ls\n", name);
		else
			print_error("--uninstall-service", error);
		return 1;
	}
	if (ControlService(service, SERVICE_CONTROL_STOP, &status))
	{
		printf("Stopping %ls...\n", name);
		for (waited = 0; waited < 40 && QueryServiceStatus(service, &status) &&
			status.dwCurrentState != SERVICE_STOPPED; waited++)
			Sleep(500);
	}
	if (!DeleteService(service))
	{
		print_error("--uninstall-service", GetLastError());
		CloseServiceHandle(service);
		CloseServiceHandle(manager);
		return 1;
	}
	printf("Took out the service %ls. The data folder is as it was.\n", name);
	CloseServiceHandle(service);
	CloseServiceHandle(manager);
	return 0;
}
