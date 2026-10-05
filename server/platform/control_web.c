/*
CONTROL_WEB.C

The dedicated server's web admin page as bytes (control_web.h). Built with
the host's ABI, as server_control.c, which uses it.
*/

#include "control_web.h"

#include "monocypher.h"

#include <stdio.h>
#include <string.h>

/* ---------- private code */

static void hex_text(const uint8_t *bytes, size_t count, char *text)
{
	static const char digits[] = "0123456789abcdef";
	size_t index;

	for (index = 0; index < count; index++)
	{
		text[2 * index] = digits[bytes[index] >> 4];
		text[2 * index + 1] = digits[bytes[index] & 15];
	}
	text[2 * count] = 0;
}

static void id_hash(const struct control_web_sessions *sessions, const char *id_text, uint8_t hash[32])
{
	crypto_blake2b_keyed(hash, 32, sessions->key, sizeof(sessions->key), (const uint8_t *)id_text,
		strlen(id_text));
}

static int session_ended(const struct control_web_session *session, int64_t now)
{
	return now - session->last_used >= CONTROL_WEB_IDLE_SECONDS ||
		now - session->created >= CONTROL_WEB_LIFETIME_SECONDS;
}

/* whether an Origin ("http://host:port") is a host's, by either scheme */
static int origin_is(const char *origin, const char *host)
{
	size_t skip;

	if (!host[0])
		return 0;
	if (!strncmp(origin, "http://", 7))
		skip = 7;
	else if (!strncmp(origin, "https://", 8))
		skip = 8;
	else
		return 0;
	return !strcmp(origin + skip, host);
}

/* ---------- files */

const struct control_web_asset *control_web_find_asset(const char *path)
{
	int index;

	if (!strcmp(path, "/"))
		path = "/index.html";
	for (index = 0; index < control_web_asset_count; index++)
	{
		if (!strcmp(control_web_assets[index].path, path))
			return &control_web_assets[index];
	}
	return NULL;
}

/* ---------- sessions */

void control_web_sessions_initialize(struct control_web_sessions *sessions, const uint8_t key[32])
{
	memset(sessions, 0, sizeof(*sessions));
	memcpy(sessions->key, key, sizeof(sessions->key));
}

int control_web_session_create(struct control_web_sessions *sessions, const uint8_t id_bytes[CONTROL_WEB_SECRET_BYTES],
	const uint8_t csrf_bytes[CONTROL_WEB_SECRET_BYTES], const char *name, const char *credential_id, int64_t now,
	char id_text[CONTROL_WEB_SECRET_LENGTH + 1])
{
	int chosen = -1;
	int index;
	struct control_web_session *session;

	/* a free one (an ended one is), else the least recently used */
	for (index = 0; index < CONTROL_WEB_SESSIONS; index++)
	{
		session = &sessions->entries[index];
		if (!session->used || session_ended(session, now))
		{
			chosen = index;
			break;
		}
		if (chosen < 0 || session->last_used < sessions->entries[chosen].last_used)
			chosen = index;
	}
	session = &sessions->entries[chosen];
	crypto_wipe(session, sizeof(*session));
	session->used = 1;
	hex_text(id_bytes, CONTROL_WEB_SECRET_BYTES, id_text);
	id_hash(sessions, id_text, session->id_hash);
	hex_text(csrf_bytes, CONTROL_WEB_SECRET_BYTES, session->csrf);
	snprintf(session->name, sizeof(session->name), "%s", name);
	snprintf(session->credential_id, sizeof(session->credential_id), "%s", credential_id);
	session->created = now;
	session->last_used = now;
	return chosen;
}

int control_web_session_find(struct control_web_sessions *sessions, const char *id_text, int64_t now, int background)
{
	uint8_t hash[32];
	int found = -1;
	int index;

	if (!control_session_text_valid(id_text))
		return -1;
	id_hash(sessions, id_text, hash);
	/* (every session compared, whichever matches) */
	for (index = 0; index < CONTROL_WEB_SESSIONS; index++)
	{
		int same = !crypto_verify32(hash, sessions->entries[index].id_hash);

		if (same && sessions->entries[index].used)
			found = index;
	}
	crypto_wipe(hash, sizeof(hash));
	if (found < 0)
		return -1;
	if (session_ended(&sessions->entries[found], now))
	{
		control_web_session_end(sessions, found);
		return -1;
	}
	if (!background)
		sessions->entries[found].last_used = now;
	return found;
}

void control_web_session_end(struct control_web_sessions *sessions, int index)
{
	if (index >= 0 && index < CONTROL_WEB_SESSIONS)
		crypto_wipe(&sessions->entries[index], sizeof(sessions->entries[index]));
}

int control_web_sessions_end_credential(struct control_web_sessions *sessions, const char *credential_id)
{
	int count = 0;
	int index;

	for (index = 0; index < CONTROL_WEB_SESSIONS; index++)
	{
		if (sessions->entries[index].used && !strcmp(sessions->entries[index].credential_id, credential_id))
		{
			control_web_session_end(sessions, index);
			count++;
		}
	}
	return count;
}

/* ---------- requests and responses */

int control_web_check_change(const struct control_web_session *session, const struct control_request *request,
	const char **reason)
{
	*reason = "";
	if (request->cross_site)
	{
		*reason = "a request from another site's page";
		return 403;
	}
	if (request->has_origin && !origin_is(request->origin, request->host) &&
		!origin_is(request->origin, request->forwarded_host))
	{
		*reason = "a request from another origin (a reverse proxy must pass Host or X-Forwarded-Host)";
		return 403;
	}
	if (session)
	{
		uint8_t given[CONTROL_WEB_SECRET_LENGTH];
		uint8_t expected[CONTROL_WEB_SECRET_LENGTH];
		size_t length = strlen(request->csrf);
		int same;

		/* (both are 64 characters: the request's was checked to be, and an
		empty one compares as zeros, which no session's is) */
		memset(given, 0, sizeof(given));
		memcpy(given, request->csrf, length < sizeof(given) ? length : sizeof(given));
		memcpy(expected, session->csrf, sizeof(expected));
		same = !crypto_verify64(given, expected);
		crypto_wipe(expected, sizeof(expected));
		if (!same || !request->csrf[0])
		{
			*reason = "the CSRF token is missing or wrong: reload the page";
			return 403;
		}
	}
	return 0;
}

void control_web_session_cookie(char *out, size_t size, const char *id_text, int secure)
{
	snprintf(out, size, "Set-Cookie: chce_session=%s; Path=/; Max-Age=%d; HttpOnly; SameSite=Strict%s\r\n",
		id_text ? id_text : "", id_text ? (int)CONTROL_WEB_LIFETIME_SECONDS : 0, secure ? "; Secure" : "");
}

const char *control_web_security_headers(void)
{
	return "Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self'; "
		"connect-src 'self'; object-src 'none'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'\r\n"
		"X-Frame-Options: DENY\r\n"
		"Referrer-Policy: no-referrer\r\n"
		"X-Content-Type-Options: nosniff\r\n"
		"Cross-Origin-Opener-Policy: same-origin\r\n"
		"Cross-Origin-Resource-Policy: same-origin\r\n"
		"Permissions-Policy: camera=(), microphone=(), geolocation=()\r\n"
		"Cache-Control: no-store\r\n";
}
