/*
SERVER_BANS.H

Shared ban lists (docs/delta.md, "Shared ban lists";
server/docs/moderation.md): a linked server's bans that name a Delta
account or device, sent to its owner's list on the site while ban_share is
on, and the entries of the lists its owner subscribed it to, kept in
shared_bans.cache and checked against every machine whose players prove an
account or device. Never an address or hardware ID; a ban's ref is keyed
with this server's own key (shared_bans.key). Shared by the link's thread
(control_link.c), the control thread and the game's main thread
(server_commands.c, server_moderation.c), under a lock of its own. Plain
types only: built with the host's ABI (server/platform/server_bans.c).
*/

#ifndef SERVER_BANS_H
#define SERVER_BANS_H

enum
{
	/* a shared entry's accounts, at most */
	SERVER_BAN_ACCOUNTS = 4,
	/* the entries a server takes from the site, at most */
	SERVER_BAN_SHARED_MOST = 500,
	/* the bans it shares, at most */
	SERVER_BAN_SHARE_MOST = 500,
	/* an account or device ID: 16 bytes, 32 hex digits */
	SERVER_BAN_ID_BYTES = 16,
	SERVER_BAN_REF_SIZE = 33,
	SERVER_BAN_LIST_SIZE = 24,
	SERVER_BAN_LIST_NAME_SIZE = 48,
	SERVER_BAN_HANDLES_SIZE = 64,
	SERVER_BAN_REASON_SIZE = 128,
	SERVER_BAN_SOURCE_SIZE = 32,
	/* the ref's first hex digits sv_banlist shows (S<them>) and sv_unban
	takes */
	SERVER_BAN_REF_SHOWN = 8,
};

/* an entry of a list the server takes */
struct server_shared_ban
{
	char list[SERVER_BAN_LIST_SIZE];
	char list_name[SERVER_BAN_LIST_NAME_SIZE];
	char ref[SERVER_BAN_REF_SIZE];
	int account_count;
	unsigned char accounts[SERVER_BAN_ACCOUNTS][SERVER_BAN_ID_BYTES];
	int has_device;
	unsigned char device[SERVER_BAN_ID_BYTES];
	/* the handles the banned player proved, for display: "alice, bob" */
	char handles[SERVER_BAN_HANDLES_SIZE];
	char reason[SERVER_BAN_REASON_SIZE];
	/* when it was made, until when (0: for ever), unix seconds */
	long long time;
	long long until;
	/* the name of the server that banned */
	char source[SERVER_BAN_SOURCE_SIZE];
};

/* ---------- sharing this server's bans (ban_share) */

int server_bans_sharing(void);
void server_bans_set_sharing(int sharing);

/* (the link's thread, each poll) the poll's "bans" member (with its key:
"\"bans\": {...}") into out when what this server shares changed since the
site last said it has it (or sharing was turned off: an empty set), else
nothing: its length, 0 for nothing, -1 if it does not fit */
int server_bans_share_payload(char *out, int size);
/* (the link's thread) the site's answer's bans_digest: what it has now */
void server_bans_share_answered(const char *digest);

/* bans.txt's line (one, with or without its end) as a shared ban, when it
names an account or a device (and is not over at now): 1 and the entry
(its ref worked out from the line), else 0 */
int server_bans_line_entry(const char *line, long long now, struct server_shared_ban *ban);

/* ---------- the lists this server takes */

/* (the link's thread) the site's entries (count at most
SERVER_BAN_SHARED_MOST), their version, and whether they came from the
cache (not the site, at start) */
void server_bans_set_shared(const struct server_shared_ban *bans, int count, unsigned long long version, int cached);
unsigned long long server_bans_shared_version(void);
/* (the main thread, each frame) whether the entries or the exceptions
changed since it last asked: every machine is checked again */
int server_bans_take_changed(void);

/* whether a machine's identity (as a bans.txt line's fields:
"account=<hex>[,<hex>]\tdevice=<hex>...") matches an entry not over at now
and not excepted here: 1, its list's name and its ref, else 0 */
int server_bans_shared_match(const char *identity, long long now, char *list_name, int list_name_size, char *ref,
	int ref_size);
/* whether an entry matches an identity (no expiry or exception checked) */
int server_bans_entry_matches(const struct server_shared_ban *ban, const char *identity);

/* the entries now (sv_banlist's, each S and its ref's first digits): the
count; one by its index (from 0), and whether it is excepted here: 1, else
0 */
int server_bans_shared_count(void);
int server_bans_shared_entry(int index, struct server_shared_ban *ban, int *excepted);
/* an entry by its ref, or the ref's first digits (at least
SERVER_BAN_REF_SHOWN, lowercase hex; sv_unban S<them>): 1 when one entry
alone has them, and whether it is excepted here; else 0. A ref, unlike a
place in the list, never moves when the site sends the lists again */
int server_bans_shared_find(const char *prefix, struct server_shared_ban *ban, int *excepted);
/* an exception here for an entry (sv_unban S<ref>), or taken back:
shared_unbans.txt in the data folder. 1, else 0 (it could not be written,
or MAXIMUM_EXCEPTIONS are kept even after those of entries no list has now
were let go) */
int server_bans_except(const char *ref, int excepted);

/* sv_ban_lists: the lists taken, their entries, the version and when the
site last sent them; whether sharing is on (JSON when json) */
void server_bans_lists_text(int json, char *out, int size);

#endif
