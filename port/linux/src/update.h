/*
UPDATE.H

What the desktop self-updater (updater.c) needs of the system: a download
over HTTPS, and putting files in place of the running game's and starting
it again. posix_update.c does it on Linux (Mbed TLS, the system's
certificate authorities), win32_update.c on Windows (WinHTTP). The Android
app updates itself in Java (port/android).

Only integers and strings cross here: the two sides are compiled with
different structure layouts.
*/

#ifndef UPDATE_H
#define UPDATE_H

/* bytes received so far, of total (0 when the size is not known) */
typedef void (*update_progress_proc)(void *context, unsigned long long received, unsigned long long total);

/* fetches the https:// url (following redirects) into the file at path,
with the certificate and host name checked, and no more than maximum bytes
of it; 1 on success, else 0 and why in error */
int update_download(const char *url, const char *path, unsigned long long maximum, update_progress_proc progress,
	void *context, char *error, int error_size);

/* the full path of this executable; 1 on success (0 for an AppImage, whose
executable is inside its read-only mount: update_appimage_path) */
int update_executable_path(char *path, int size);

/* the full path of the AppImage file this runs from, when it runs from one
(Linux: the APPIMAGE its runtime sets), which an update replaces whole; 1
when so */
int update_appimage_path(char *path, int size);

/* puts the file at new_path in place of the one at path, which is renamed
to old_path first (a running executable can be renamed, not overwritten),
keeping its permissions; 1 on success */
int update_replace_file(const char *path, const char *new_path, const char *old_path);

/* makes the file at path executable, where the system has such a thing (a
file new to the game's folder is not, from update_replace_file); 1 on
success */
int update_make_executable(const char *path);

/* deletes the file (or empty folder) at path, if there is one */
void update_delete_file(const char *path);

/* makes the directory at path, if there is none; 1 when it is there */
int update_make_directory(const char *path);

/* starts the executable at path as a process of its own, with no arguments,
this process's environment and none of its files (from an AppImage, with the
library path its AppRun was given, not the one it set for its own mount); 1
on success */
int update_launch(const char *path);
/* the same, with one argument for it (NULL: none), as "--setup" */
int update_launch_with(const char *path, const char *argument);

#ifdef _WIN32
/* whether this program runs on 64-bit Windows on an x64 processor, where
the 64-bit build runs (win32_update.c; the 32-bit build offers it) */
int update_windows_runs_64bit(void);
#endif

/* ---------- updater.c's, for the setup screen's Updates (launcher.c) */

enum
{
	UPDATER_NOT_CHECKED,
	UPDATER_CHECKING,
	UPDATER_NEWER,
	UPDATER_LATEST,
	UPDATER_UNREACHABLE,
};

/* a check started now, whatever update.auto says: 0 when this build never
looks (not a release's) or one is running */
int updater_check_now(void);
/* where the check is (UPDATER_*); latest: the newer version's */
int updater_check_state(char *latest, int size);
/* the newer version installed (its download page on macOS); 0 if not */
int updater_install(void);
/* "release", "nightly" or "dev" */
const char *updater_channel(void);

/* update.channel's help line on the setup screen (the menus' settings show
the same: tools/port_settings.py's UPDATE_CHANNEL_HELP) */
#define UPDATE_CHANNEL_HELP "Testing gets new features first and may have bugs. Going back waits for the next release."

#endif
