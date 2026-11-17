/*
SC_INTERNAL.H

The Steam content client's pieces (steam_content.h), shared between its
files: the CM (sc_cm.c) and the depot's content (sc_depot.c). Field
numbers and message ids are Steam's public protocol, as documented by the
community's protobuf collection (SteamDatabase/Protobufs).

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/

#ifndef SC_INTERNAL_H
#define SC_INTERNAL_H

#include "steam_content.h"

#include <stddef.h>
#include <stdint.h>

extern struct steam_content_host sc_host;

long long sc_milliseconds(void);
void sc_log(const char *format, ...);

/* ---------- the CM (sc_cm.c) */

enum
{
	SC_EMSG_MULTI = 1,
	SC_EMSG_SERVICE_RESPONSE = 147,
	SC_EMSG_SERVICE_CALL = 151,
	SC_EMSG_HEARTBEAT = 703,
	SC_EMSG_LOG_OFF = 706,
	SC_EMSG_LOGON_RESPONSE = 751,
	SC_EMSG_LOGGED_OFF = 757,
	SC_EMSG_ACCOUNT_INFO = 768,
	SC_EMSG_DEPOT_KEY = 5438,
	SC_EMSG_DEPOT_KEY_RESPONSE = 5439,
	SC_EMSG_LOGON = 5514,
	SC_EMSG_SERVICE_CALL_NOT_AUTHED = 9804,
	SC_EMSG_HELLO = 9805,
	SC_PROTOCOL_VERSION = 65580,
	/* EResult */
	SC_RESULT_OK = 1,
	SC_RESULT_FAIL = 2,
	SC_RESULT_NO_CONNECTION = 3,
	SC_RESULT_INVALID_PASSWORD = 5,
	SC_RESULT_LOGGED_IN_ELSEWHERE = 6,
	SC_RESULT_ACCESS_DENIED = 15,
	SC_RESULT_BANNED = 17,
	SC_RESULT_ACCOUNT_NOT_FOUND = 18,
	SC_RESULT_REVOKED = 26,
	SC_RESULT_SESSION_REPLACED = 34,
	SC_RESULT_CONNECT_FAILED = 35,
	SC_RESULT_IO_FAILURE = 37,
	SC_RESULT_REMOTE_DISCONNECT = 38,
	SC_RESULT_TIMEOUT = 16,
	SC_RESULT_SERVICE_UNAVAILABLE = 20,
	SC_RESULT_EXPIRED = 27,
	SC_RESULT_INVALID_LOGIN_AUTH_CODE = 65,
	SC_RESULT_ACCOUNT_LOGON_DENIED = 63,
	SC_RESULT_TRY_ANOTHER_CM = 48,
	SC_RESULT_NOT_LOGGED_ON = 21,
};

struct sc_message
{
	unsigned emsg;
	const unsigned char *header;
	size_t header_length;
	const unsigned char *body;
	size_t body_length;
};

struct sc_cm
{
	struct sc_connection *connection;
	const volatile int *cancel;
	uint64_t steamid;
	int32_t session;
	uint64_t next_job;
	uint32_t cell_id;
	int heartbeat_seconds;
	long long last_send;
	int logged_off;
	char persona[STEAM_CONTENT_PERSONA_SIZE];
	/* the message last received, and a Multi's inner messages still to give */
	unsigned char *message;
	unsigned char *multi;
	size_t multi_length;
	size_t multi_at;
};

/* a CM (the Web API's list, websockets) connected and said hello to: STEAM_CONTENT_OK, OFFLINE or FAILED */
int sc_cm_connect(struct sc_cm *cm, const volatile int *cancel);
/* logged off (when logged on) and closed */
void sc_cm_close(struct sc_cm *cm);
int sc_cm_send(struct sc_cm *cm, unsigned emsg, const void *body, size_t length, const char *job_name, uint64_t job);
/* the next message (a Multi's taken apart; heartbeats sent; the persona
name kept): 1, 0 none within timeout_ms, -1 the connection ended. The
message stays valid until the next call */
int sc_cm_receive(struct sc_cm *cm, struct sc_message *message, int timeout_ms);
/* a unified service method ("Authentication.PollAuthSessionStatus#1"):
1 with Steam's EResult and the response body (malloc'ed), 0 no answer */
int sc_cm_call(struct sc_cm *cm, const char *method, const void *body, size_t length, int authed,
	unsigned char **response, size_t *response_length, int *eresult);
/* the logon message's body: its length, 0 if it couldn't be made (no
LoginID: one is always needed, so the player's Steam isn't replaced) */
size_t sc_cm_logon_body(void *buffer, size_t capacity, const char *account, const char *refresh_token,
	uint32_t login_id);
/* logged on with a refresh token: Steam's EResult (SC_RESULT_OK), or 0 no answer */
int sc_cm_logon(struct sc_cm *cm, const char *account, const char *refresh_token, uint32_t login_id);
/* the depot's key: Steam's EResult, the 32-byte key when SC_RESULT_OK; 0 no answer */
int sc_cm_depot_key(struct sc_cm *cm, uint32_t depot, uint32_t app, unsigned char key[32]);

/* ---------- the system's secret store (sc_secret.c) */

/* its name for the player ("the macOS Keychain"), NULL where there is none */
const char *sc_secret_store_name(void);
int sc_secret_put(const char *account, const char *token);
int sc_secret_get(const char *account, char *token, size_t size);
void sc_secret_delete(const char *account);

/* ---------- the depot's content (sc_depot.c) */

enum
{
	SC_FILE_DIRECTORY = 0x40,
	SC_MAXIMUM_CHUNK = 64 * 1024 * 1024,
	SC_MAXIMUM_NAME = 260,
};

struct sc_chunk
{
	unsigned char sha[20];
	uint64_t offset;
	uint32_t size;
	uint32_t compressed_size;
};

struct sc_file
{
	char name[SC_MAXIMUM_NAME];
	uint64_t size;
	uint32_t flags;
	unsigned char sha[20];
	int has_sha;
	int first_chunk;
	int chunk_count;
};

struct sc_manifest
{
	struct sc_file *files;
	int file_count;
	struct sc_chunk *chunks;
	int chunk_count;
	uint64_t total;
};

/* Steam's symmetric decryption: AES-256, the IV the first block's ECB
decryption, the rest CBC with PKCS#7. out has room for length bytes: the
plain length, or -1 */
long sc_symmetric_decrypt(const unsigned char key[32], const unsigned char *data, size_t length, unsigned char *out);
/* a zip's first file, unpacked (stored or deflated): malloc'ed, or NULL */
unsigned char *sc_unzip_first(const unsigned char *data, size_t length, size_t *out_length, size_t maximum);
/* a chunk from the content servers into its plain bytes (decrypted,
unpacked, size and SHA-1 checked): 1, else 0 with why */
int sc_chunk_decode(const unsigned char key[32], const unsigned char *data, size_t length,
	const struct sc_chunk *chunk, unsigned char *out, char *why, int why_size);
/* a decrypted chunk unpacked ("VZa", "VSZa" or a zip) into exactly size bytes: 1, else 0 with why */
int sc_chunk_unpack(const unsigned char *data, size_t length, unsigned char *out, size_t size, char *why, int why_size);
/* a manifest as the content servers give it (a zip): 1, else 0 with why */
int sc_manifest_parse(const unsigned char *data, size_t length, const unsigned char key[32],
	struct sc_manifest *manifest, char *why, int why_size);
void sc_manifest_free(struct sc_manifest *manifest);
/* an update's leftovers: what folder/relative has that the manifest doesn't, deleted (links removed, never
followed; nothing outside folder) */
void sc_prune(const char *folder, const char *relative, const struct sc_manifest *manifest, int depth);
/* whether a name from a manifest is safe to write under the item's folder
(relative, no "..", no drive or control characters) */
int sc_name_safe(const char *name);

#endif
