/*
LINK_REGISTER.H

How the desktop opens halo:// links (party links, halo://party/CODE; game
invites, halo://join/...; a player key's halo://key/...) with this game
(port/linux/src/link_register.c), as carefully as discord_register.h
registers Discord's launch command: an entry that is not this game's is
never replaced.

- Windows: HKEY_CURRENT_USER\Software\Classes\halo, a URL protocol whose
  shell\open\command runs the game with the link (link_register_windows,
  through the registry interface port/windows/src/win32_p2p.c gives it).
- Linux and FreeBSD: a desktop entry, chupathingy-links.desktop in the
  user's applications folder, the handler of x-scheme-handler/halo
  (link_register_desktop_file; posix_net.c then runs xdg-mime default when
  the default is not already this one). An AppImage's entry runs the
  AppImage, not the executable in its mount.
- macOS: the application bundle declares the scheme (port/macos/Info.plist).

Earlier versions wrote their entries without a mark (Name "Halo: Combat
Evolved invite", halo-ce-universal-halo.desktop; Windows' label
"URL:Halo: Combat Evolved invite"). Other programs that came from the same
code write the same, so such an entry counts as this game's only when the
program it runs is this one, another copy of this game (in the same folder,
or named chupathingy...), or gone.

A link opened while the game runs goes to the running copy (p2p.c's
p2p_hand_off_invite). No SDL or game dependency, so its tests build anywhere
(port/linux/tests/link_register_test.c).
*/

#ifndef LINK_REGISTER_H
#define LINK_REGISTER_H

#include "discord_register.h"

#define LINK_REGISTER_SCHEME "halo"
#define LINK_REGISTER_MIME_TYPE "x-scheme-handler/halo"
/* this game's desktop entry */
#define LINK_REGISTER_DESKTOP_NAME "chupathingy-links.desktop"
/* the one earlier versions wrote (with no mark) */
#define LINK_REGISTER_OLD_DESKTOP_NAME "halo-ce-universal-halo.desktop"
/* the name (Linux) and label (Windows, after "URL:") earlier versions gave
it */
#define LINK_REGISTER_OLD_NAME "Halo: Combat Evolved invite"
/* the label of this game's own Windows key */
#define LINK_REGISTER_WINDOWS_LABEL "URL:Chupathingy link"

/* whether a file is there (the real file system's in the game, a fake in the
tests) */
typedef int (*link_register_exists)(const char *path);

/* the desktop entry for halo:// that runs executable with the link into entry
(size bytes): its length, or -1 if it does not fit */
int link_register_desktop_entry(char *entry, int size, const char *executable);

/* the program a desktop entry's Exec line runs (its first argument, with its
quotes and escapes taken off) into executable (size bytes): 1, or 0 if
there is none */
int link_register_desktop_exec(const char *text, char *executable, int size);

/* whether path is a whole path (/..., C:\..., \\server\...), not a name
found on PATH */
int link_register_path_absolute(const char *path);

/* whether named (the program an entry runs) is this game: executable itself,
the earlier name (halo, halo.exe) in its folder, a program named
chupathingy..., or a whole path to a file that is gone (exists says so;
NULL: never gone). ignore_case: Windows' paths */
int link_register_program_ours(const char *named, const char *executable, link_register_exists exists,
	int ignore_case);

/* whether a desktop entry's text is this game's: its mark, or an earlier
version's entry whose program is this game (link_register_program_ours) */
int link_register_desktop_entry_ours(const char *text, const char *executable, link_register_exists exists);

/* the desktop entry's name in xdg-mime query default's answer (its first
word, a .desktop file's name) into name (size bytes): 1, or 0 if it names
none */
int link_register_query_answer(const char *text, char *name, int size);

/* writes chupathingy-links.desktop into directory (which exists), unless the
scheme's handler is another program's: current_default is the desktop
entry xdg-mime query default names ("" for none) and current_text its
text (NULL if no applications folder has it: another program's, unless it
is the name earlier versions gave this game's). An earlier version's entry
of this game's in directory goes. *make_default: whether xdg-mime default
should make this game's entry the handler. DISCORD_REGISTER_* */
int link_register_desktop_file(const char *directory, const char *current_default, const char *current_text,
	const char *executable, link_register_exists exists, int *make_default);

/* Software\Classes\halo run by executable with the link (its label, URL
Protocol, DefaultIcon, shell\open\command, and this game's mark), unless
one there is another program's. DISCORD_REGISTER_* */
int link_register_windows(const struct discord_registry *registry, const char *executable,
	link_register_exists exists);

#endif
