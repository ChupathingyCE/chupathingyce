/*
CONTROL_ROLES.H

Delta Control's roles (server/docs/moderation.md): who may do what on a
dedicated server, as bytes. The roles and their permissions, the
permission each command needs, the moderators file's lines, a person's
moderator key as text, the audit file's lines and the limit on how often
one person acts. No files, sockets or threads here, so the tests
(server/tests/control_test.c) run all of it; server_roles.c keeps the
files and the shared state, server_control.c and server_commands.c ask it.

Roles, lowest first: none, moderator, admin, owner. The server's console,
its startup commands and the control API's bearer tokens act as owner.
*/

#ifndef CONTROL_ROLES_H
#define CONTROL_ROLES_H

#include <stddef.h>
#include <stdint.h>

enum
{
	CONTROL_ROLE_NONE,
	CONTROL_ROLE_MODERATOR,
	CONTROL_ROLE_ADMIN,
	CONTROL_ROLE_OWNER,
	CONTROL_ROLE_COUNT,
};

/* permission bits (the same numbers on Delta Peer's MOD_STATE and the
site's role lists: docs/delta.md, Delta Control) */
enum
{
	/* status, players, bans, the log, playlists, settings: reads */
	CONTROL_PERMISSION_VIEW = 0x001,
	CONTROL_PERMISSION_WARN = 0x002,
	CONTROL_PERMISSION_KICK = 0x004,
	/* a ban of CONTROL_ROLES_TIMED_BAN_MINUTES at most */
	CONTROL_PERMISSION_BAN_TIMED = 0x008,
	/* a ban of any length, or for ever */
	CONTROL_PERMISSION_BAN = 0x010,
	CONTROL_PERMISSION_UNBAN = 0x020,
	/* the map, the playlist and the game: sv_map, sv_mapcycle_next,
	sv_end_game, playlists */
	CONTROL_PERMISSION_MAP = 0x040,
	/* the server's settings and game types */
	CONTROL_PERMISSION_SETTINGS = 0x080,
	/* roles: moderators, accounts, the site link */
	CONTROL_PERMISSION_ROLES = 0x100,
	/* invitations for roles below one's own */
	CONTROL_PERMISSION_INVITE = 0x200,
	/* what only the server's console does (sv_admin_*: tokens) */
	CONTROL_PERMISSION_CONSOLE = 0x400,

	CONTROL_PERMISSION_ALL = 0x7FF,

	/* the longest ban BAN_TIMED allows */
	CONTROL_ROLES_TIMED_BAN_MINUTES = 7 * 24 * 60,
};

/* a role's permissions (the console's own is not any role's) */
unsigned int control_role_permissions(int role);
/* "moderator", "admin", "owner" ("none" for none) */
const char *control_role_name(int role);
/* a role by its name, or -1 */
int control_role_parse(const char *text);

/* the permission a command line needs (its first word, and for sv_ban its
duration: BAN_TIMED for one of CONTROL_ROLES_TIMED_BAN_MINUTES at most,
else BAN), and whether it changes something (audited, rate limited); 0 if
the line is no command the table knows (the server answers it as it
would: "no command") */
unsigned int control_command_permission(const char *line, int *changes);

/* ---------- moderator keys */

enum
{
	/* an Ed25519 public key */
	CONTROL_KEY_BYTES = 32,
	CONTROL_KEY_TEXT = 2 * CONTROL_KEY_BYTES,
	/* a person's name for display, with its end */
	CONTROL_DISPLAY_NAME_SIZE = 32,
};

/* a key as 64 lowercase hex digits, and back (either case): 1, else 0 */
void control_key_text(const uint8_t key[CONTROL_KEY_BYTES], char text[CONTROL_KEY_TEXT + 1]);
int control_key_parse(const char *text, uint8_t key[CONTROL_KEY_BYTES]);
/* a key's short form for logs and lists: its first 8 hex digits */
void control_key_short(const uint8_t key[CONTROL_KEY_BYTES], char text[9]);

/* whether text may be a display name: 1 to 31 printable ASCII characters,
not all spaces, no quotes or backslashes */
int control_display_name_valid(const char *text);

/* ---------- the moderators file

moderators.txt in the data folder: a line a person,

    <role> <moderator key> [name]

role moderator, admin or owner; the key 64 hex digits (sv_players shows a
player's once their game has proved it); the name, if any, the rest of the
line, for display only. # starts a comment line. */

struct control_moderator
{
	int role;
	uint8_t key[CONTROL_KEY_BYTES];
	char name[CONTROL_DISPLAY_NAME_SIZE];
};

/* a line of the file: 1 a moderator, 0 a blank or comment line, -1 not a
line the file takes (why in reason) */
int control_moderator_parse(const char *line, struct control_moderator *moderator, const char **reason);
/* a moderator as its line (with no end of line): its length, or -1 */
int control_moderator_line(const struct control_moderator *moderator, char *out, size_t size);

/* ---------- how often a person acts */

enum
{
	CONTROL_ACTOR_SLOTS = 64,
	/* changes a person makes: so many a minute, a burst of so many */
	CONTROL_ACTOR_PER_MINUTE = 20,
	CONTROL_ACTOR_BURST = 10,
	CONTROL_ACTOR_NAME_SIZE = 80,
};

struct control_actor_limiter
{
	struct
	{
		int used;
		char actor[CONTROL_ACTOR_NAME_SIZE];
		/* (a change is 60000 of them: control_roles.c) */
		int64_t tokens;
		int64_t refilled;
		int64_t last;
	} slots[CONTROL_ACTOR_SLOTS];
};

void control_actor_limiter_initialize(struct control_actor_limiter *limiter);
/* whether an actor may make a change now (now: milliseconds, any clock that
does not go back): 1 (and it is counted), else 0 and how many seconds until
it may */
int control_actor_allowed(struct control_actor_limiter *limiter, const char *actor, int64_t now, int64_t *retry_after);

/* ---------- the audit file

control_audit.log in the data folder: a JSON object a line, never an
address:
  {"time": 1791168674, "via": "web", "actor": "alice", "role": "moderator",
   "action": "sv_kick", "target": "Odb718", "reason": "...", "ok": true,
   "detail": "kicked Odb718"} */

struct control_audit_event
{
	int64_t time;
	/* console, startup, api, web, game, site */
	const char *via;
	/* who: an account, a credential, a player's name and key, a site handle */
	const char *actor;
	int role;
	/* the command's name (sv_kick), or the control's own (login, invite, ...) */
	const char *action;
	/* whom or what it was done to, and why (either may be NULL) */
	const char *target;
	const char *reason;
	int ok;
	/* what it answered, its first line (NULL: none) */
	const char *detail;
};

/* an event as its line (with its end of line): its length, or -1 */
int control_audit_line(const struct control_audit_event *event, char *out, size_t size);

#endif
