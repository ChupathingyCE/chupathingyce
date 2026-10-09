/*
SERVER_ROLES.H

Delta Control's roles as the server keeps them (server/docs/moderation.md):
whose moderator key has which role, from three places (the moderators file,
moderators.txt in the data folder; the control panel's accounts bound to a
game; the halo.milenko.org link's role list, when the server is linked),
the audit file every change is written to, and how often each person may
act. Shared by the game's main thread (server_commands.c, the Delta Peer
moderation hooks) and the control thread (server_control.c), under a lock
of its own. Plain types only: the game's units call it, and it is built
with the host's ABI (control_roles.h has the bytes).
*/

#ifndef SERVER_ROLES_H
#define SERVER_ROLES_H

/* (control_roles.h's numbers, for the game's units, which do not include it:
server_roles.c checks they are the same) */
enum
{
	SERVER_ROLE_NONE,
	SERVER_ROLE_MODERATOR,
	SERVER_ROLE_ADMIN,
	SERVER_ROLE_OWNER,

	SERVER_PERMISSION_VIEW = 0x001,
	SERVER_PERMISSION_WARN = 0x002,
	SERVER_PERMISSION_KICK = 0x004,
	SERVER_PERMISSION_BAN_TIMED = 0x008,
	SERVER_PERMISSION_BAN = 0x010,
	SERVER_PERMISSION_UNBAN = 0x020,
	SERVER_PERMISSION_MAP = 0x040,
	SERVER_PERMISSION_SETTINGS = 0x080,
	SERVER_PERMISSION_ROLES = 0x100,
	SERVER_PERMISSION_INVITE = 0x200,
	SERVER_PERMISSION_CONSOLE = 0x400,
	SERVER_PERMISSION_ALL = 0x7FF,
};

/* control_roles.h's, with plain types: a role's name and permissions, a
role by its name (-1 none), a key from 64 hex digits (1, else 0) and its
short form (8 hex digits and an end), and the permission a command line
needs (and whether it changes something) */
const char *server_role_name(int role);
unsigned int server_role_permissions(int role);
int server_role_parse(const char *text);
int server_key_parse(const char *text, unsigned char *key);
void server_key_short(const unsigned char *key, char *text);
unsigned int server_command_permission(const char *line, int *changes);

/* the moderators file read (and kept up with: an edit by hand takes effect
within a second); the audit file's place. Once, at start */
void server_roles_start(void);

/* the role a moderator key has (CONTROL_ROLE_*: the highest of the three
places), its permissions and, in who (who_size bytes, may be NULL), the
name it goes by there ("Odb718", or "alice (account)"); 0 none */
int server_roles_key_role(const unsigned char *key, unsigned int *permissions, char *who, int who_size);

/* ---------- the moderators file (sv_mod_add, sv_mod_remove, sv_mod_list) */

/* a key given a role (a new line, or the role of its line changed), with a
name for display (may be empty): 1, else 0 and why in problem */
int server_roles_moderator_set(int role, const unsigned char *key, const char *name, char *problem, int problem_size);
/* a moderator's line taken out, by its key, its key's beginning (8 hex
digits or more) or its name: 1 (and its name and key in found_name,
found_key: may be NULL), else 0 and why */
int server_roles_moderator_remove(const char *which, char *found_name, int found_name_size, char *problem,
	int problem_size);
/* the file's moderators: how many; the one at index (role, key as 64 hex
digits, name) */
int server_roles_moderator_count(void);
int server_roles_moderator_get(int index, int *role, char *key_text, int key_text_size, char *name, int name_size);

/* ---------- the other places (their whole lists, replaced) */

/* the accounts bound to a game (server_control.c's, whenever they change):
count keys (32 bytes each, one after another), their roles and names */
void server_roles_set_accounts(int count, const unsigned char *keys, const int *roles, const char *const *names);
/* the site's role list (control_link.c's): the same, the names the site's
handles; version, the list's (0: none) */
void server_roles_set_site(int count, const unsigned char *keys, const int *roles, const char *const *names,
	unsigned int version);
/* the site's role of a handle (for a command the site relays), 0 none */
int server_roles_site_handle_role(const char *handle);
void server_roles_set_site_handles(int count, const char *const *handles, const int *roles);

/* ---------- audit */

/* a change, written to the audit file (control_audit.log, a JSON object a
line; the caller logs it as it logs everything else) and handed to the
recorder (below): via (console,
startup, api, web, game, site), who, their role, the action (sv_kick, or
the control's own: login, invite, ...), its target and reason (either may
be NULL), whether it was done, and what it answered (NULL: nothing) */
void server_audit(const char *via, const char *actor, int role, const char *action, const char *target,
	const char *reason, int ok, const char *detail);

/* a recorder of moderation and control events beside the audit file (the
server's event log, Delta Stats: struct control_audit_event in
control_roles.h): called for each, on the thread that made the change,
under the audit's lock; NULL none */
struct control_audit_event;
void server_roles_set_recorder(void (*recorder)(const struct control_audit_event *event));

/* ---------- binding an account to a game

The control panel (its thread) asks for an account to be bound to the game
of a player in sv_players; the main thread asks that player's game over
Delta Peer, and its player answers; the panel reads the result. */

enum
{
	SERVER_BIND_PENDING,
	/* asked of the game; waiting for its player */
	SERVER_BIND_SENT,
	SERVER_BIND_ACCEPTED,
	SERVER_BIND_DECLINED,
	/* no such player now */
	SERVER_BIND_NO_PLAYER,
	/* the player's game does not speak Delta Peer's moderation (an OpenCE
	game, or an older ChupathingyCE): it cannot be bound */
	SERVER_BIND_NOT_DELTA,
	SERVER_BIND_EXPIRED,
};

/* a request (the panel's): its id, or 0 if too many are waiting */
unsigned int server_roles_bind_request(const char *account, int player_number);
/* the next request not yet asked of a game (the main thread's): 1, else 0 */
int server_roles_bind_take(unsigned int *request_id, int *player_number, char *account, int account_size);
/* what became of it (the main thread's; key, 32 bytes, with ACCEPTED) */
void server_roles_bind_answer(unsigned int request_id, int state, const unsigned char *key);
/* a request's state now, and its account and key once accepted (the
panel's); -1 if there is no such request (or it was forgotten: they are
kept 5 minutes) */
int server_roles_bind_state(unsigned int request_id, char *account, int account_size, unsigned char *key);

/* the roles changed somewhere the main thread does not see (an account's
role, or its game bound): the moderators' games are told again; and the
main thread's taking of that (1 if it changed since) */
void server_roles_moderation_changed(void);
int server_roles_take_changed(void);

/* ---------- how often a person acts */

/* whether an actor ("web alice", "game Odb718 1a2b3c4d") may make a change
now: 1, else 0 and the seconds until it may */
int server_roles_actor_allowed(const char *actor, int *retry_after);

#endif
