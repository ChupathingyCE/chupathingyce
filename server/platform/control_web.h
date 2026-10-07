/*
CONTROL_WEB.H

The dedicated server's web admin page as bytes (server/docs/admin.md): its
files (server/webui, embedded in the program at build time by
tools/embed_webui.py), the sessions a browser logs in to, their cookie,
the checks a state-changing request of a browser must pass, and the
headers every response carries. No sockets, files or threads here, so the
tests (server/tests/control_test.c) run all of it; server_control.c serves
it on the control API's listener.
*/

#ifndef CONTROL_WEB_H
#define CONTROL_WEB_H

#include "control_protocol.h"

#include <stddef.h>
#include <stdint.h>

enum
{
	/* the sessions kept at once, at most (the least recently used one goes
	for a new one) */
	CONTROL_WEB_SESSIONS = 32,
	/* a session's id and its CSRF token: random bytes, in hexadecimal */
	CONTROL_WEB_SECRET_BYTES = CONTROL_SESSION_BYTES,
	CONTROL_WEB_SECRET_LENGTH = CONTROL_SESSION_LENGTH,
	/* a session ends after this long unused, or this long after its login */
	CONTROL_WEB_IDLE_SECONDS = 30 * 60,
	CONTROL_WEB_LIFETIME_SECONDS = 12 * 60 * 60,
};

/* ---------- files */

struct control_web_asset
{
	/* its path ("/index.html") */
	const char *path;
	/* its Content-Type */
	const char *type;
	const unsigned char *data;
	size_t size;
};

/* (tools/embed_webui.py's, generated from server/webui) */
extern const struct control_web_asset control_web_assets[];
extern const int control_web_asset_count;

/* the file a GET's path is, by its whole path ("/" is "/index.html"): the
files are the program's own, nothing on the disk is ever looked up, so no
path can reach anything else. NULL if none */
const struct control_web_asset *control_web_find_asset(const char *path);

/* ---------- sessions */

struct control_web_session
{
	int used;
	/* a keyed hash of its id (the id itself is not kept) */
	uint8_t id_hash[32];
	char csrf[CONTROL_WEB_SECRET_LENGTH + 1];
	/* the credential that logged in (its name and id): a control token's, or
	an account's (account set) */
	char name[32];
	char credential_id[CONTROL_ID_LENGTH + 1];
	int account;
	/* an account's second factor being set up: the secret offered, until a
	code from it is given */
	int totp_pending;
	uint8_t totp_secret[20];
	int64_t created;
	int64_t last_used;
};

struct control_web_sessions
{
	struct control_web_session entries[CONTROL_WEB_SESSIONS];
	uint8_t key[32];
};

/* no sessions, and the key their ids are hashed with (random, the run's) */
void control_web_sessions_initialize(struct control_web_sessions *sessions, const uint8_t key[32]);

/* a new session for a credential (now: seconds, any clock that does not
go back), from random bytes for its id and its CSRF token: its index, and
its id in id_text (for the cookie) */
int control_web_session_create(struct control_web_sessions *sessions, const uint8_t id_bytes[CONTROL_WEB_SECRET_BYTES],
	const uint8_t csrf_bytes[CONTROL_WEB_SECRET_BYTES], const char *name, const char *credential_id, int64_t now,
	char id_text[CONTROL_WEB_SECRET_LENGTH + 1]);

/* the session a cookie's id is (its index; every session compared, in
constant time), used now unless the page only polled (background: it does
not keep a session from going idle); -1 if none, or it has ended (idle too
long, or too old: it is forgotten) */
int control_web_session_find(struct control_web_sessions *sessions, const char *id_text, int64_t now, int background);

void control_web_session_end(struct control_web_sessions *sessions, int index);

/* every session of a credential ended (its token rotated or removed): how
many */
int control_web_sessions_end_credential(struct control_web_sessions *sessions, const char *credential_id);

/* ---------- requests and responses */

/* whether a browser's request may change something: not from another site
(Sec-Fetch-Site), its Origin, if any, this server's (Host, or a reverse
proxy's X-Forwarded-Host), and, with a session (NULL for a login), its
CSRF token the session's (in constant time). 0 if so, else 403 and why */
int control_web_check_change(const struct control_web_session *session, const struct control_request *request,
	const char **reason);

/* the session cookie's header (with its CRLF), for an id, or to clear it
(id_text NULL): HttpOnly, SameSite=Strict, Path=/, and Secure when the
request came over HTTPS (a reverse proxy said so) */
void control_web_session_cookie(char *out, size_t size, const char *id_text, int secure);

/* the headers every response carries (each with its CRLF): a strict
Content-Security-Policy (nothing but this server's own files, no inline
script or style), no framing, no referrer, no sniffing, no caching */
const char *control_web_security_headers(void);

#endif
