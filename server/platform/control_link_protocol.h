/*
CONTROL_LINK_PROTOCOL.H

Delta Control's link to halo.milenko.org as bytes (docs/delta.md, Delta
Control; control_link.c does the requests and the thread): the server's
signed requests (Ed25519, Monocypher's crypto_ed25519_sign), the site's
answers' MACs (BLAKE2b keyed with the link's secret), a small strict JSON
reader for the site's answers, and what they say: a link's code and state,
the commands the site relays and its role list. No files, sockets or
threads, so the tests (server/tests/control_test.c) run all of it.

Everything the site says is checked as a stranger's would be: sizes
bounded, strings printable ASCII, a command one of those the link takes
(control_link_command_line), and the whole answer refused if its MAC is
not right.
*/

#ifndef CONTROL_LINK_PROTOCOL_H
#define CONTROL_LINK_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

enum
{
	CONTROL_LINK_SEED_BYTES = 32,
	CONTROL_LINK_SECRET_BYTES = 32,
	CONTROL_LINK_NONCE_BYTES = 16,
	CONTROL_LINK_SERVER_ID_SIZE = 64,
	CONTROL_LINK_HANDLE_SIZE = 32,
	CONTROL_LINK_CODE_SIZE = 16,
	CONTROL_LINK_TOKEN_SIZE = 72,
	/* a command's text and reason as the site sends them, at most */
	CONTROL_LINK_COMMAND_SIZE = 201,
	CONTROL_LINK_REASON_SIZE = 64,
	/* what one answer carries, at most */
	CONTROL_LINK_COMMANDS = 16,
	CONTROL_LINK_ROLE_ENTRIES = 128,
	CONTROL_LINK_ENTRY_KEYS = 8,
	/* a JSON answer's tokens, at most */
	CONTROL_JSON_TOKENS = 2048,
};

/* ---------- JSON */

enum
{
	CONTROL_JSON_OBJECT = 1,
	CONTROL_JSON_ARRAY,
	CONTROL_JSON_STRING,
	CONTROL_JSON_PRIMITIVE,
};

struct control_json_token
{
	int type;
	/* its bytes in the text: [start, end) (a string's without its quotes) */
	int start;
	int end;
	/* an object's members (key and value each a token) or an array's items */
	int size;
	/* the token after this one and all it holds */
	int next;
};

/* text (length bytes) read into tokens (at most count): how many, or -1 if
it is not one whole JSON value (or has more than count tokens, or nests
deeper than 16) */
int control_json_parse(const char *text, size_t length, struct control_json_token *tokens, int count);
/* an object's member's value (its token's index), by key; -1 none */
int control_json_member(const char *text, const struct control_json_token *tokens, int object, const char *key);
/* a string token's text, its escapes undone, into out: printable ASCII only
(\u escapes of it too). 1, else 0 */
int control_json_text(const char *text, const struct control_json_token *token, char *out, size_t size);
/* a primitive token as a whole number (no sign, at most 18 digits), or
true/false: 1, else 0 */
int control_json_number(const char *text, const struct control_json_token *token, uint64_t *value);
int control_json_boolean(const char *text, const struct control_json_token *token, int *value);

/* ---------- requests and answers */

/* the signed request's body, for a path ("/v1/control/poll"), the server's
id and key's seed, a time (unix seconds), a nonce (random bytes) and a
payload (JSON text): its length, or -1 if it does not fit */
int control_link_signed_body(const char *path, const char *server_id, const uint8_t seed[CONTROL_LINK_SEED_BYTES],
	int64_t time, const uint8_t nonce[CONTROL_LINK_NONCE_BYTES], const char *payload, char *out, size_t size);
/* the public key of a seed (to tell the site at the link's start) */
void control_link_public_key(const uint8_t seed[CONTROL_LINK_SEED_BYTES], uint8_t public_key[32]);

/* an answer to a signed request ({"payload": "...", "mac": "..."}) checked
with the link's secret and the request's nonce: 1 and the payload's text
in payload (its escapes undone), else 0 */
int control_link_open_answer(const char *answer, size_t length, const uint8_t secret[CONTROL_LINK_SECRET_BYTES],
	const uint8_t nonce[CONTROL_LINK_NONCE_BYTES], char *payload, size_t payload_size);

struct control_link_command
{
	uint64_t id;
	char actor[CONTROL_LINK_HANDLE_SIZE];
	char command[CONTROL_LINK_COMMAND_SIZE];
	char reason[CONTROL_LINK_REASON_SIZE];
};

struct control_link_role
{
	char handle[CONTROL_LINK_HANDLE_SIZE];
	/* control_roles.h's */
	int role;
	int key_count;
	uint8_t keys[CONTROL_LINK_ENTRY_KEYS][32];
};

struct control_link_poll
{
	int command_count;
	struct control_link_command commands[CONTROL_LINK_COMMANDS];
	int has_roles;
	uint64_t roles_version;
	int role_count;
	struct control_link_role roles[CONTROL_LINK_ROLE_ENTRIES];
	int unlinked;
	int poll_seconds;
};

/* a poll's payload: 1, else 0 (not one: nothing of it is taken) */
int control_link_parse_poll(const char *payload, size_t length, struct control_link_poll *poll);

/* the link's start's answer ({"code", "expires", "token"}) and its state's
({"state", "server_id", "owner"}): 1, else 0. state: "pending", "linked",
"expired", "declined" */
int control_link_parse_start(const char *answer, size_t length, char *code, size_t code_size, char *token,
	size_t token_size, int *expires);
int control_link_parse_status(const char *answer, size_t length, char *state, size_t state_size, char *server_id,
	size_t server_id_size, char *owner, size_t owner_size);

/* a command the site relays as the server's command line: one of those the
link takes (sv_warn, sv_kick, sv_ban, sv_unban, sv_map, sv_mapcycle_next,
sv_end_game, sv_name, sv_maxplayers), and its reason, if any, its last word
in quotes (sv_warn, sv_kick and sv_ban only). 1, else 0 and why */
int control_link_command_line(const char *command, const char *reason, char *line, size_t size, const char **why);

#endif
