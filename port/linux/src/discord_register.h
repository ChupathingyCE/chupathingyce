/*
DISCORD_REGISTER.H

How Discord starts the game for a friend's Join when the game is closed
(port/linux/src/discord_register.c): the launch command registered for the
Discord application, as Discord's own SDK registers it (discord-rpc's
discord_register_*.c), under the scheme discord-<application>. Discord shows
Join only to players whose client knows how to start the application; once
started, the game connects to Discord and gets the join as an ACTIVITY_JOIN
(p2p_discord.c).

- Windows: HKEY_CURRENT_USER\Software\Classes\discord-<application>, a URL
  protocol whose shell\open\command runs the game with the link
  (discord_register_windows, through the registry interface
  port/windows/src/win32_p2p.c gives it).
- Linux and FreeBSD: a desktop entry, discord-<application>.desktop in the
  user's applications folder, the handler of x-scheme-handler/
  discord-<application> (discord_register_desktop_file; posix_net.c then runs
  xdg-mime default).
- macOS: the application bundle declares the scheme (port/macos/Info.plist)
  and posix_net.c makes it the scheme's handler (Launch Services), as the
  SDK does for a bundle. The SDK's other form, a games/<application>.json
  in Discord's folder, holds a command Discord opens as a link (a page, for
  anything that is not one): the one an earlier version wrote, with a shell
  command, goes (discord_register_old_json_ours).

An entry that is not this game's (none of its marks) is never replaced. No
SDL or game dependency, so its tests build anywhere (port/linux/tests/
discord_register_test.c).
*/

#ifndef DISCORD_REGISTER_H
#define DISCORD_REGISTER_H

/* what a registration did */
enum
{
	DISCORD_REGISTER_FAILED = -1,
	/* an entry of another program's was there: left as it was */
	DISCORD_REGISTER_LEFT = 0,
	DISCORD_REGISTER_WRITTEN = 1,
	/* this game's own, already as it would be written */
	DISCORD_REGISTER_UNCHANGED = 2,
	/* nothing to register here (Android, iOS, the Mac game outside its
	bundle) */
	DISCORD_REGISTER_NONE = 3,
};

/* the mark of this game's own entries (a desktop entry's key, a registry
key's value) */
#define DISCORD_REGISTER_MARK "X-Chupathingy"
#define DISCORD_REGISTER_REGISTRY_MARK "Chupathingy"

/* whether application (discord.application_id) can be registered: digits
only, as it goes into a path and a registry key */
int discord_register_application_valid(const char *application);

/* the desktop entry for discord-<application> that runs executable with the
link into entry (size bytes): its length, or -1 if it does not fit */
int discord_register_desktop_entry(char *entry, int size, const char *application, const char *executable);

/* whether a desktop entry's text is this game's own (its mark) */
int discord_register_desktop_entry_ours(const char *text);

/* writes discord-<application>.desktop into directory (which exists),
unless one there is another program's. name gets the file's name (for
xdg-mime; name_size bytes). DISCORD_REGISTER_* */
int discord_register_desktop_file(const char *directory, const char *application, const char *executable,
	char *name, int name_size);

/* the registry, as discord_register_windows uses it (the real one on
Windows, a fake in the tests): keys under HKEY_CURRENT_USER */
struct discord_registry
{
	void *context;
	/* a string value of key (name NULL: its default value) into value
	(size bytes): 1, or 0 if there is none */
	int (*get)(void *context, const char *key, const char *name, char *value, int size);
	/* sets a string value of key (made if need be): 1, or 0 if it fails */
	int (*set)(void *context, const char *key, const char *name, const char *value);
};

/* the protocol discord-<application> run by executable with the link, as
the SDK writes it (Software\Classes\discord-<application>: its
"URL:Run game <application> protocol", URL Protocol, DefaultIcon and
shell\open\command), and this game's mark, unless one there is another
program's. DISCORD_REGISTER_* */
int discord_register_windows(const struct discord_registry *registry, const char *application,
	const char *executable);

/* whether a games/<application>.json's text is the one an earlier version
of this game wrote (a shell command naming its bundle), which goes */
int discord_register_old_json_ours(const char *text);

#endif
