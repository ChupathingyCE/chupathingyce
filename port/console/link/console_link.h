/*
CONSOLE_LINK.H

Link Profile on a console without TLS (the original Xbox, the Xbox 360, the
Wii U): the console links itself to a Delta profile on
stats.chupathingy.org with a code, over the console list's plain HTTP,
without sending anything secret (the site's console_link.py and
/v1/console/link/ calls; docs/delta.md, "Delta Link").

- The console makes its own Ed25519 key the first time its player asks for
  a code. Its 32-byte seed is kept beside the settings (the port's key_path,
  64 hex digits) and never leaves the console, nor goes in the log.
- Its player ID (the 16 bytes every table of the site and Delta Peer's
  PROFILE carry) is the first 16 bytes of the SHA-256 of
  "chupathingy console id v1\n" and its public key, so no one can claim
  another console's ID.
- The code: the console sends its public key and a commitment to a secret,
  the site answers a nonce, the console sends the secret. Both sides work
  the code out of the key, the secret and the nonce (XXXX-XXXX), so a code
  from a key swapped on the way is not the one on the screen. The screen
  also shows a QR code of stats.chupathingy.org/connect?code=XXXX-XXXX.
- The player signs in on any device and enters the code; the console then
  asks its player to confirm (A or B), and signs the answer over the
  profile it showed.
- Once linked, the console keeps the profile's handle (link_path), shares
  its ID in Delta Peer's PROFILE (console_link_player_id), and asks the
  site for its profile's name and clan tag when Online Games opens
  (console_link_refresh, a signed one-use challenge). A console that never
  linked sends nothing at all.

Plain C, the same on a big- or little-endian console (bytes and text only),
over a port's hooks (struct console_link_port). The requests block, so they
run on a thread the port starts; the main thread reads a copy of the state
(console_link_view). port/console/tests/console_link_test.c builds the pure
parts on the host.
*/

#ifndef __CONSOLE_LINK_H
#define __CONSOLE_LINK_H

#define CONSOLE_LINK_SEED_SIZE 32
#define CONSOLE_LINK_KEY_SIZE 32
#define CONSOLE_LINK_ID_SIZE 16
#define CONSOLE_LINK_SECRET_SIZE 32
#define CONSOLE_LINK_NONCE_SIZE 16
/* the code's characters, without the dash */
#define CONSOLE_LINK_CODE_LENGTH 8
/* the poll token, in hex digits */
#define CONSOLE_LINK_TOKEN_LENGTH 64
/* a profile's handle (3 to 20 characters), display name and clan tag, with
their terminators */
#define CONSOLE_LINK_HANDLE_SIZE 24
#define CONSOLE_LINK_DISPLAY_SIZE 36
#define CONSOLE_LINK_TAG_SIZE 12
#define CONSOLE_LINK_MESSAGE_SIZE 96
/* the QR code's most modules a side (version 6) */
#define CONSOLE_LINK_QR_MAXIMUM 41
/* where the code is entered */
#define CONSOLE_LINK_SITE "stats.chupathingy.org"
#define CONSOLE_LINK_PAGE "stats.chupathingy.org/connect"

/* ---------- the pure parts (console_link_test.c) */

/* the code's alphabet (no 0, O, 1, I or L), the site's */
extern const char console_link_alphabet[];

void console_link_public_key(const unsigned char *seed, unsigned char *public_key);
void console_link_id(const unsigned char *public_key, unsigned char *id);
void console_link_commitment(const unsigned char *secret, unsigned char *commitment);
/* the code: CONSOLE_LINK_CODE_LENGTH characters and a terminator */
void console_link_code(const unsigned char *public_key, const unsigned char *secret, const unsigned char *nonce,
	char *code);
/* the signed messages' text: their length, or 0 if they do not fit */
int console_link_confirm_message(const char *token, const char *handle, int accept, char *text, int size);
int console_link_profile_message(const char *public_hex, const char *challenge, char *text, int size);
/* the seed's signature of a message, as 128 lowercase hex digits and a terminator */
void console_link_sign(const unsigned char *seed, const void *message, int size, char *signature_hex);
/* bytes as lowercase hex (2 * size digits and a terminator) */
void console_link_hex(const unsigned char *data, int size, char *text);
/* size bytes from exactly 2 * size hex digits at text: 1, or 0 */
int console_link_unhex(const char *text, unsigned char *data, int size);
/* the seed in a key file's bytes (64 hex digits, white space around them): 1 and the seed, or 0 */
int console_link_parse_seed(const char *data, int size, unsigned char *seed);

/* the site's answers (NUL-terminated text) */
/* "ok <nonce> <seconds> <token>": 1 with them, or 0 */
int console_link_parse_start(const char *reply, unsigned char *nonce, int *seconds, char *token);

enum
{
	CONSOLE_LINK_STATUS_BAD = 0,
	CONSOLE_LINK_STATUS_PENDING,
	CONSOLE_LINK_STATUS_CONFIRM,
	CONSOLE_LINK_STATUS_CONNECTED,
	CONSOLE_LINK_STATUS_DECLINED,
	CONSOLE_LINK_STATUS_EXPIRED
};

/* a status or confirm answer's first line: CONSOLE_LINK_STATUS_*, with the
profile's handle (and the one its games count for now, else empty) for
confirm and connected */
int console_link_parse_status(const char *reply, char *handle, char *previous);
/* {"challenge": "<32 hex>"}: 1 and it in challenge (33 bytes), or 0 */
int console_link_parse_challenge(const char *reply, char *challenge);
/* /v1/console/profile's lines: 1 (linked: handle, display, tag, each maybe
empty but the handle), 0 (unlinked), -1 (not an answer) */
int console_link_parse_profile(const char *reply, char *handle, char *display, char *tag);

/* ---------- on the console */

struct console_link_port
{
	/* the Delta platform name ("xbox", "xbox360", "wiiu") and the build's
	name ("Warthog 0.8.1b"), for the site's page */
	const char *platform;
	const char *build;
	/* the device key's file, and the linked profile's */
	const char *key_path;
	const char *link_path;
	/* one plain HTTP request to the console list's host: path, a JSON body
	(NULL for a GET); the answer's status and body (NUL terminated, cut to
	reply_size - 1). 1 if an answer came, else 0. Blocks: called on the
	thread start_thread started */
	int (*request)(const char *path, const char *body, int *status, char *reply, int reply_size);
	/* the same over HTTPS to CONSOLE_LINK_SITE, once the console has TLS
	(Mbed TLS over its own sockets, the site's key pinned so no clock is
	needed): when not NULL it is used in place of request, the same calls
	and answers. The exchange needs no TLS to be safe; TLS keeps what it
	says (the handle, the poll token) from onlookers too */
	int (*https_request)(const char *path, const char *body, int *status, char *reply, int reply_size);
	/* runs function(context) on a thread of its own, which ends when it
	returns: 1, or 0 if it could not */
	int (*start_thread)(void (*function)(void *context), void *context);
	void (*lock)(void);
	void (*unlock)(void);
	/* random bytes for keys (the port's own source of them) */
	void (*random)(void *buffer, int size);
	unsigned long (*milliseconds)(void);
	/* one line in the log (never a key, code, token or address is given) */
	void (*write_log)(const char *text);
};

/* where a link stands, as the panel shows it */
enum
{
	CONSOLE_LINK_IDLE = 0,
	CONSOLE_LINK_GETTING_CODE,
	CONSOLE_LINK_CODE_SHOWN,
	CONSOLE_LINK_CONFIRM,
	CONSOLE_LINK_CONFIRMING,
	CONSOLE_LINK_DONE,
	CONSOLE_LINK_FAILED
};

struct console_link_view
{
	int phase;
	/* the console is linked (to handle; display and tag once the site said) */
	int linked;
	char handle[CONSOLE_LINK_HANDLE_SIZE];
	char display[CONSOLE_LINK_DISPLAY_SIZE];
	char tag[CONSOLE_LINK_TAG_SIZE];
	/* CONSOLE_LINK_CODE_SHOWN: "XXXX-XXXX", and how long it has left */
	char code[CONSOLE_LINK_CODE_LENGTH + 2];
	unsigned long code_left;
	/* CONSOLE_LINK_CONFIRM: who entered it, and the profile the console
	counts for now (empty if none) */
	char asker[CONSOLE_LINK_HANDLE_SIZE];
	char previous[CONSOLE_LINK_HANDLE_SIZE];
	/* CONSOLE_LINK_DONE and CONSOLE_LINK_FAILED: what happened */
	char message[CONSOLE_LINK_MESSAGE_SIZE];
	/* a request under way */
	int busy;
};

/* the port's hooks (kept, not copied), at the start; reads link_path */
void console_link_initialize(const struct console_link_port *port);
void console_link_view(struct console_link_view *view);
/* the main thread's, every frame the panel is up: the code's poll, when due */
void console_link_update(void);
/* a new code (makes the device key the first time) */
void console_link_begin(void);
/* the player's answer to CONSOLE_LINK_CONFIRM */
void console_link_answer(int accept);
/* back to CONSOLE_LINK_IDLE (a code shown is let lapse) */
void console_link_cancel(void);
/* the console's profile asked of the site again (when Online Games opens),
at most once a minute, and only by a console that has a device key */
void console_link_refresh(void);
/* the QR code of the code's page: its size (0 for none) and its modules */
int console_link_qr_size(void);
int console_link_qr_dark(int x, int y);
/* this console's player ID while it is linked: 1, or 0 */
int console_link_player_id(unsigned char *id);

#endif
