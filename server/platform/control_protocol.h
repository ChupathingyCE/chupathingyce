/*
CONTROL_PROTOCOL.H

The dedicated server's control API as bytes (server_control.c, which does
the sockets and threads): its HTTP requests, their JSON, its credentials
and their checking, the failed attempts it limits, and its log's lines as
it hands them out. No sockets, files or threads here, so the tests
(server/tests/control_test.c) run all of it.

Every byte of a request is a stranger's until its token is checked, and
the parsing is strict: anything not plainly what the API takes is refused.
*/

#ifndef CONTROL_PROTOCOL_H
#define CONTROL_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

enum
{
	/* a request's line and headers, at most */
	CONTROL_MAXIMUM_HEAD = 8192,
	/* its body, at most (a command is at most 255 characters) */
	CONTROL_MAXIMUM_BODY = 4096,
	/* its target (path and query), at most */
	CONTROL_MAXIMUM_TARGET = 256,
	CONTROL_MAXIMUM_HEADERS = 32,
	/* a bearer token, at most, with its end */
	CONTROL_MAXIMUM_TOKEN = 128,
	/* a command, at most, with its end (command_line.h's) */
	CONTROL_MAXIMUM_COMMAND = 256,

	/* a token: "chce_" and 32 random bytes in hexadecimal */
	CONTROL_TOKEN_BYTES = 32,
	CONTROL_TOKEN_LENGTH = 5 + 2 * CONTROL_TOKEN_BYTES,
	CONTROL_SALT_BYTES = 16,
	CONTROL_HASH_BYTES = 32,
	CONTROL_ID_LENGTH = 8,
	/* Argon2id's cost for a new credential: memory (KiB) and passes, as
	OWASP's password storage advice has it */
	CONTROL_ARGON2_KIB = 19 * 1024,
	CONTROL_ARGON2_PASSES = 2,
	/* the costs a credentials file may give, at most and at least */
	CONTROL_ARGON2_MINIMUM_KIB = 8,
	CONTROL_ARGON2_MAXIMUM_KIB = 256 * 1024,
	CONTROL_ARGON2_MAXIMUM_PASSES = 16,
	/* a credential's line, at most, with its end */
	CONTROL_CREDENTIAL_LINE = 256,
	/* a credential's name, at most, with its end */
	CONTROL_NAME_SIZE = 32,

	/* the web page's session id and CSRF token: random bytes, in
	hexadecimal (control_web.h) */
	CONTROL_SESSION_BYTES = 32,
	CONTROL_SESSION_LENGTH = 2 * CONTROL_SESSION_BYTES,
	/* a Host's (or X-Forwarded-Host's) value kept, at most, with its end */
	CONTROL_MAXIMUM_HOST = 128,
};

/* ---------- requests */

enum
{
	CONTROL_METHOD_GET = 1,
	CONTROL_METHOD_POST,
};

enum
{
	/* more bytes needed */
	CONTROL_PARSE_INCOMPLETE,
	/* a whole request */
	CONTROL_PARSE_DONE,
	/* not one: answer with the status and reason it gives, and close */
	CONTROL_PARSE_ERROR,
};

struct control_request
{
	int method;
	char path[CONTROL_MAXIMUM_TARGET];
	/* (after the ?, empty if none) */
	char query[CONTROL_MAXIMUM_TARGET];
	/* the Authorization header: none, a bearer token (in token), or something
	else */
	int authorization;
	char token[CONTROL_MAXIMUM_TOKEN];
	int json_body;
	size_t content_length;
	/* (in the bytes parsed) */
	const char *body;

	/* what a browser sends (the web page's, control_web.h): the session
	cookie's id (chce_session, 64 lowercase hex digits; empty if none, if
	not that, or if there were two) and X-CSRF-Token's (the same form) */
	char session[CONTROL_SESSION_LENGTH + 1];
	char csrf[CONTROL_SESSION_LENGTH + 1];
	/* Host's and X-Forwarded-Host's values (empty if none, or too long), and
	Origin's, whether there was one */
	char host[CONTROL_MAXIMUM_HOST];
	char forwarded_host[CONTROL_MAXIMUM_HOST];
	int has_origin;
	char origin[CONTROL_MAXIMUM_HOST + 16];
	/* Sec-Fetch-Site said cross-site or same-site (another site's page) */
	int cross_site;
	/* a reverse proxy's headers came with it (Forwarded, X-Forwarded-For,
	X-Forwarded-Proto, X-Forwarded-Host, X-Real-IP), and one said HTTPS */
	int proxied;
	int https;
	/* X-Background: 1, the page's own polling (a session it does not keep
	from going idle) */
	int background;
};

enum
{
	CONTROL_AUTHORIZATION_NONE,
	CONTROL_AUTHORIZATION_BEARER,
	CONTROL_AUTHORIZATION_OTHER,
};

/* a request in data (length bytes so far); CONTROL_PARSE_ERROR sets status
(400, 405, 411, 413, 414, 417, 431, 501, 505) and reason */
int control_parse_request(const char *data, size_t length, struct control_request *request, int *status,
	const char **reason);

/* a command's request body, {"command": "..."}: 1 and the command (printable
ASCII) in command, else 0 and why in reason */
int control_parse_command_body(const char *body, size_t length, char *command, size_t command_size,
	const char **reason);

/* a login's request body, {"token": "..."}: 1 and the token in token,
else 0 and why in reason */
int control_parse_login_body(const char *body, size_t length, char *token, size_t token_size, const char **reason);

/* whether text is a session id's or CSRF token's form (64 lowercase hex
digits) */
int control_session_text_valid(const char *text);

/* the log's query, "since=<number>" or nothing (0): 1, else 0 */
int control_parse_log_query(const char *query, uint64_t *since);

/* text as a JSON string with its quotes, into out (size bytes); its length,
or -1 if it does not fit */
int control_json_string(const char *text, char *out, size_t size);

/* an HTTP status's reason phrase */
const char *control_status_text(int status);

/* ---------- credentials */

struct control_credential
{
	char name[CONTROL_NAME_SIZE];
	char id[CONTROL_ID_LENGTH + 1];
	uint32_t kib;
	uint32_t passes;
	uint8_t salt[CONTROL_SALT_BYTES];
	uint8_t hash[CONTROL_HASH_BYTES];
};

/* a token from random bytes */
void control_token_text(const uint8_t bytes[CONTROL_TOKEN_BYTES], char token[CONTROL_TOKEN_LENGTH + 1]);

/* whether text is a token's form (it may still be the wrong one) */
int control_token_valid(const char *text);

/* whether text may name a credential: 1 to 31 of RFC 9110's token
characters (letters, digits, !#$%&'*+-.^_`|~) */
int control_credential_name_valid(const char *text);

/* a credential for a token: its name, its id (random bytes, in hex), salt,
and the token's Argon2id hash at that cost. 1, else 0 (no memory) */
int control_credential_make(const char *token, const char *name, const uint8_t id_bytes[CONTROL_ID_LENGTH / 2],
	const uint8_t salt[CONTROL_SALT_BYTES], uint32_t kib, uint32_t passes, struct control_credential *credential);

/* a credential as a line of the credentials file, and back:
  v1 argon2id <name> <id> <kib> <passes> <salt, hex> <hash, hex>
parse: 1 if the line is one, else 0 */
void control_credential_line(const struct control_credential *credential, char line[CONTROL_CREDENTIAL_LINE]);
int control_credential_parse(const char *line, struct control_credential *credential);

/* whether a token is a credential's (Argon2id, its hash compared in constant
time): 1, 0 if not, -1 if there was no memory to check */
int control_credential_check(const struct control_credential *credential, const char *token);

/* ---------- failed attempts */

enum
{
	CONTROL_LIMITER_ADDRESSES = 64,
	/* an address that gave a wrong token this many times in the window ... */
	CONTROL_LIMITER_FAILURES = 5,
	CONTROL_LIMITER_WINDOW_SECONDS = 60,
	/* ... is refused this long */
	CONTROL_LIMITER_BLOCK_SECONDS = 300,
	/* and tokens are checked (Argon2id, which takes a while) no more than
	this many a minute for everyone, a burst of up to this many at once */
	CONTROL_LIMITER_CHECKS_PER_MINUTE = 30,
	CONTROL_LIMITER_CHECK_BURST = 10,
};

struct control_limiter_entry
{
	uint8_t address[16];
	int used;
	int failures;
	int64_t window_start;
	int64_t blocked_until;
};

struct control_limiter
{
	struct control_limiter_entry entries[CONTROL_LIMITER_ADDRESSES];
	/* (the checks' bucket, in thousandths of a check) */
	int64_t check_tokens;
	int64_t refilled;
	int started;
};

void control_limiter_initialize(struct control_limiter *limiter);
/* whether an address (16 bytes: IPv4 as ::ffff:a.b.c.d) may try now
(seconds, any clock that does not go back); if not, how long until it may */
int control_limiter_allowed(struct control_limiter *limiter, const uint8_t address[16], int64_t now,
	int64_t *retry_after);
/* whether a token may be checked now (the bucket for everyone) */
int control_limiter_take_check(struct control_limiter *limiter, int64_t now);
void control_limiter_failed(struct control_limiter *limiter, const uint8_t address[16], int64_t now);
void control_limiter_succeeded(struct control_limiter *limiter, const uint8_t address[16]);

/* ---------- the log */

/* a line of the server's log as the API hands it out: its end of line gone,
anything not printable ASCII a ?, and any public IP address (one that is
no one's on the internet is not; log_address.h) that is in it whole, as the
log writes it with debug.log_addresses, as "addr#hidden" */
void control_log_scrub(const char *line, char *out, size_t size);

#endif
