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

/* whether the program at path can be started: an executable file, in a
folder whose file system runs programs (not mounted noexec); 1 if so, else
0 and why in reason ("its folder doesn't run programs") */
int update_can_start(const char *path, char *reason, int reason_size);

/* this process to become the program at path as it exits: the last of its
exit's handlers execve's it, with update_launch's environment and none of
this process's files, so the new game is the same process (its parent and
group too, which Steam and gamescope follow to keep the game's window, in
the Steam Deck's Game Mode). 1 when arranged, and the caller then exits;
0 where a system cannot (Windows, tvOS: update_launch then) */
int update_start_in_place(const char *path);
/* the same, with one argument for it (NULL: none) in place of this
process's own, as "--setup": the launcher's restarts (launcher.c) */
int update_start_in_place_with(const char *path, const char *argument);

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

/* ---------- an install from the game's own pages (the setup screen's
UPDATES, the menus' Settings > Online > Updates), with no window or dialog:
what is shown is the page's, so it works under gamescope (the Steam Deck's
Game Mode), where a dialog crashes the game */

enum
{
	UPDATER_INSTALL_IDLE,
	UPDATER_INSTALL_DOWNLOADING,
	UPDATER_INSTALL_INSTALLING,
	UPDATER_INSTALL_FAILED,
	/* (ready, put in place when the game in progress ends) */
	UPDATER_INSTALL_WAITING,
};

/* whether a newer version can be installed now (latest: its version),
changing nothing (the menus ask it every frame) */
int updater_ready(char *latest, int size);
/* the install begun on a thread of its own (its download page on macOS):
1 when it began or one is running, 0 when there is nothing to install */
int updater_install_begin(void);
/* where it is (UPDATER_INSTALL_*), and line, for the page: how much is
downloaded, or why it failed. Called on the game's thread every frame the
page is shown; the new version starts once it is in place, the game then
restarting in the same process (update_start_in_place) */
int updater_install_status(char *line, int size);
/* how much is downloaded, short, for a row's value: "3 / 27 MB" ("Starting"
before the server says the size); text */
const char *updater_install_amount(char *text, int size);
/* the setup screen's, every frame: an install that is ready is put in place
and the game restarts (no game runs there). In the game updater_poll does
it, once no game is in progress (updater_restart_allowed, menu_functions.c) */
void updater_install_restart(void);
/* whether the game runs under gamescope (updater.c) */
int updater_gamescope(void);

/* where the main menu's version line says to update under gamescope ("0.9.1
is out: update in Settings > Online > Updates"), in place of the question
a dialog would ask */
#define UPDATER_GAMESCOPE_WHERE "in Settings > Online > Updates"
/* the version line while an update waits for the game in progress to end */
#define UPDATER_WAITING_NOTE "the update restarts Chupathingy when you leave the game"

/* update.channel's help line on the setup screen (the menus' settings show
the same: tools/port_settings.py's UPDATE_CHANNEL_HELP) */
#define UPDATE_CHANNEL_HELP "Testing gets new features first and may have bugs. Going back waits for the next release."

#endif
