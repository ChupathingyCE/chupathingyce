/*
IDENTITY.H

Delta's device identity (docs/delta.md, "Device identity" and "Devices"):
this copy's device key, the site's device certificates (CDEV) for it under an
issuer certificate (CISS) the identity root keys vouch for
(identity_key.h), the revocation list (CREV), the requests a device signs,
and when to enroll, refresh and get a token (browser.c's thread does the
asking).

No SDL and no game here, so its tests build anywhere
(port/linux/tests/identity_test.c, tools/test_identity.py). Every binary
form is read and written byte by byte, little-endian, so big-endian hosts
read them the same. Times are Unix seconds.
*/

#ifndef IDENTITY_H
#define IDENTITY_H

enum
{
	IDENTITY_SEED_SIZE = 32,
	IDENTITY_KEY_SIZE = 32,
	IDENTITY_SIGNATURE_SIZE = 64,
	IDENTITY_ID_SIZE = 16,
	IDENTITY_KEY_ID_SIZE = 8,
	IDENTITY_NONCE_SIZE = 16,
	IDENTITY_ISSUER_SIZE = 112,
	IDENTITY_DEVICE_HEADER = 84,
	IDENTITY_HANDLE_MOST = 20,
	IDENTITY_DEVICE_MOST = IDENTITY_DEVICE_HEADER + IDENTITY_HANDLE_MOST + IDENTITY_SIGNATURE_SIZE,
	IDENTITY_REVOKED_HEADER = 32,
	IDENTITY_REVOKED_MOST = 4096,
	/* a token: opaque, at most 200 characters, and its NUL */
	IDENTITY_TOKEN_SIZE = 201,
	/* the site's host a device signature names (its audience) */
	IDENTITY_AUDIENCE_MOST = 253,
	/* (the longest signed message: the enroll's, with the longest audience) */
	IDENTITY_MESSAGE_MOST = 384,
	/* the time a check allows a clock to be off by */
	IDENTITY_GRACE = 600,
};

/* a certificate's flags and capabilities */
enum
{
	IDENTITY_FLAG_LEGACY = 0x01,
	IDENTITY_FLAG_CONSOLE = 0x02,
	IDENTITY_CAN_PLAY = 0x01,
	IDENTITY_CAN_STATS = 0x02,
	IDENTITY_CAN_PROFILE = 0x04,
	IDENTITY_CAN_MODERATE = 0x08,
	IDENTITY_CAN_SOCIAL = 0x10,
	/* approve another device's code, revoke another device (stage B) */
	IDENTITY_CAN_DEVICES = 0x20,
};

/* sign-in by code (Quick Connect): the code's characters (as a console's
Link Profile code), and the text the device shows, ABCD-EFGH and a NUL */
enum
{
	IDENTITY_CODE_LENGTH = 8,
	IDENTITY_CODE_TEXT = IDENTITY_CODE_LENGTH + 2,
	/* a poll token: 64 hex digits and a NUL */
	IDENTITY_POLL_TOKEN_SIZE = 65,
};

/* why a certificate or list is not one to trust (0: it is) */
enum
{
	IDENTITY_OK = 0,
	IDENTITY_MALFORMED,
	IDENTITY_NOT_ROOT_SIGNED,
	IDENTITY_WEAK_KEY,
	IDENTITY_NOT_YET,
	IDENTITY_EXPIRED,
	IDENTITY_OTHER_ISSUER,
	IDENTITY_NOT_ISSUER_SIGNED,
	IDENTITY_OUTSIDE_ISSUER,
	IDENTITY_REVOKED,
	IDENTITY_NOT_MINE,
};

struct identity_issuer
{
	unsigned long valid_from, valid_until;
	unsigned char public_key[IDENTITY_KEY_SIZE];
	unsigned char key_id[IDENTITY_KEY_ID_SIZE];
};

struct identity_device
{
	int platform, flags;
	unsigned long issued, expires, capabilities;
	unsigned char serial[8];
	unsigned char issuer[IDENTITY_KEY_ID_SIZE];
	unsigned char account[IDENTITY_ID_SIZE];
	unsigned char public_key[IDENTITY_KEY_SIZE];
	unsigned char device_id[IDENTITY_ID_SIZE];
	char handle[IDENTITY_HANDLE_MOST + 1];
	/* (the signed part's size: the bytes before the signature) */
	int body_size;
};

struct identity_revoked
{
	unsigned long serial, issued, next_update;
	unsigned char issuer[IDENTITY_KEY_ID_SIZE];
	int count;
	/* (the sorted device IDs, inside the parsed data) */
	const unsigned char *ids;
	int body_size;
};

/* ---------- keys and signatures (Ed25519, RFC 8032, with Monocypher) */

void identity_sha256(const void *data, int size, unsigned char *digest);
void identity_public(const unsigned char *seed, unsigned char *public_key);
void identity_sign(const unsigned char *seed, const void *message, int size, unsigned char *signature);
/* the key's signature of the message, and the key not one of small order */
int identity_verify(const unsigned char *public_key, const void *message, int size, const unsigned char *signature);
/* the first 16 bytes of SHA-256("chupathingy device id\n" + key) */
void identity_device_id(const unsigned char *public_key, unsigned char *id);
/* an issuer's: the first 8 bytes of SHA-256(key) */
void identity_key_id(const unsigned char *public_key, unsigned char *id);

/* ---------- the certificates and the list: parsed (1, or 0 for one that
is malformed), then checked (IDENTITY_OK, or why not) */

int identity_parse_issuer(const unsigned char *data, int size, struct identity_issuer *issuer);
/* signed by one of the root keys (count of them; an all-zero one is none)
and in date */
int identity_check_issuer(const unsigned char *data, int size, const unsigned char (*roots)[IDENTITY_KEY_SIZE],
	int root_count, unsigned long now, struct identity_issuer *issuer);
int identity_parse_device(const unsigned char *data, int size, struct identity_device *device);
/* signed by that (checked) issuer, issued in its time and in date */
int identity_check_device(const unsigned char *data, int size, const struct identity_issuer *issuer,
	unsigned long now, struct identity_device *device);
int identity_parse_revoked(const unsigned char *data, int size, struct identity_revoked *revoked);
int identity_check_revoked(const unsigned char *data, int size, const struct identity_issuer *issuer,
	struct identity_revoked *revoked);
int identity_is_revoked(const struct identity_revoked *revoked, const unsigned char *device_id);

/* this copy's own certificate and its issuer's, as the site answered: the
issuer checked against the root keys when any is not all zero (else only
well-formed: the keys aren't made yet), the device certificate signed by
the issuer, naming this device's key, and in date */
int identity_accept(const unsigned char *certificate, int certificate_size, const unsigned char *issuer_data,
	int issuer_size, const unsigned char (*roots)[IDENTITY_KEY_SIZE], int root_count,
	const unsigned char *own_key, unsigned long now, struct identity_device *device);

/* ---------- what a device signs: its label, the audience (a byte of
length, then the site's host), then the fields. The message's size in out
(IDENTITY_MESSAGE_MOST bytes), or -1 for an audience that isn't one */

int identity_enroll_message(const char *audience, const unsigned char *nonce, const unsigned char *copy_id,
	const unsigned char *public_key, unsigned char *out);
int identity_refresh_message(const char *audience, const unsigned char *nonce, const unsigned char *device_id,
	unsigned char *out);
int identity_token_message(const char *audience, const unsigned char *nonce, const unsigned char *device_id,
	unsigned char *out);
/* Quick Connect's: its start (the device's key), its answer to an approval
(yes or no, for the profile it showed), and signing out */
int identity_start_message(const char *audience, const unsigned char *nonce, const unsigned char *public_key,
	unsigned char *out);
int identity_confirm_message(const char *audience, const unsigned char *nonce, const unsigned char *device_id,
	int accept, const char *handle, unsigned char *out);
int identity_signout_message(const char *audience, const unsigned char *nonce, const unsigned char *device_id,
	unsigned char *out);
/* the code a device shows (ABCD-EFGH) for its key and the site's code
nonce: IDENTITY_CODE_TEXT bytes into code */
void identity_device_code(const unsigned char *public_key, const unsigned char *code_nonce, char *code);
/* a code as typed (any case, a dash or spaces or not): its 8 characters in
the code alphabet into code (IDENTITY_CODE_TEXT, shown with the dash): 1,
or 0 for one that isn't */
int identity_clean_code(const char *typed, char *code);
/* a code another device approves (Link a device): a device's or console's
(8 characters) or an older copy's Link Profile code (6), as typed; its
characters without the dash into code (IDENTITY_CODE_LENGTH + 1): 1, or 0
for one that isn't */
int identity_approval_code(const char *typed, char *code);
/* the audience of a site's address (network.browser_url): its host,
lowercase, without the scheme, port or path; 1 to 253 of a-z 0-9 . -.
Its length, or 0 when the address has none */
int identity_audience(const char *url, char *audience, int size);
/* bytes wiped so the compiler can't leave them (seeds, tokens, signatures) */
void identity_wipe(void *data, int size);

/* ---------- text */

void identity_hex(const unsigned char *bytes, int size, char *text);
/* text's 2 * size lowercase or uppercase hex digits into bytes: 1, or 0 */
int identity_unhex(const char *text, int length, unsigned char *bytes, int size);
/* a JSON object's top-level member name: its string value into out (cut
to size - 1), or a literal's text (true, 3600); its length, or -1 when it
has none */
int identity_json_member(const char *json, const char *name, char *out, int size);
int identity_json_true(const char *json, const char *name);

/* the requests' bodies: their length, or -1 when out is too small */
int identity_enroll_body(char *out, int size, const char *player_key_hex, const unsigned char *public_key,
	const char *nonce_hex, const unsigned char *signature, const char *platform, const char *build);
int identity_signed_body(char *out, int size, const unsigned char *device_id, const char *nonce_hex,
	const unsigned char *signature);
int identity_start_body(char *out, int size, const unsigned char *public_key, const char *nonce_hex,
	const unsigned char *signature, const char *platform, const char *build, const char *name);
int identity_confirm_body(char *out, int size, const char *poll_token, const unsigned char *device_id,
	const char *nonce_hex, const unsigned char *signature, int accept);
/* an answer's parts: 1, or 0 for an answer without them */
int identity_nonce_answer(const char *json, char *nonce_hex, unsigned char *nonce);
int identity_certificate_answer(const char *json, unsigned char *certificate, int *certificate_size,
	unsigned char *issuer, int *issuer_size);
int identity_token_answer(const char *json, char *token, int size, unsigned long *lifetime);
/* /v1/identity/device/start's: the code nonce (16 bytes), the poll token,
how often to poll and how long the code lives (seconds) */
int identity_start_answer(const char *json, unsigned char *code_nonce, char *poll_token, unsigned long *interval,
	unsigned long *lifetime);

/* ---------- when to ask (browser.c's thread keeps the state) */

enum
{
	IDENTITY_STEP_NONE = 0,
	IDENTITY_STEP_ENROLL,
	IDENTITY_STEP_REFRESH,
};

/* what an answer from the identity paths means */
enum
{
	IDENTITY_ANSWER_OK = 0,
	/* no answer, or refused for now: tried again after a growing wait (a
	minute, to an hour) */
	IDENTITY_ANSWER_RETRY,
	/* an older site, which doesn't speak identity: an hour */
	IDENTITY_ANSWER_LATER,
	/* busy, or not issuing yet (503): ten minutes, to an hour */
	IDENTITY_ANSWER_BUSY,
	/* a locked account, too many devices: a day, and given up for this run
	after IDENTITY_DAY_TRIES */
	IDENTITY_ANSWER_DAY,
	/* a copy the site hasn't seen yet: again after the player's next
	confirmed game or report (identity_confirmed), or within an hour */
	IDENTITY_ANSWER_UNKNOWN_COPY,
	/* the device key is another copy's: a new device key, once */
	IDENTITY_ANSWER_NEW_KEY,
	/* the site doesn't know the device: enroll again */
	IDENTITY_ANSWER_ENROLL,
	/* revoked or signed out: the certificate dropped, and signed out (no key
	either) until it signs in again by code */
	IDENTITY_ANSWER_OFF,
};

enum
{
	IDENTITY_LATER = 3600,
	IDENTITY_RETRY_FIRST = 60,
	IDENTITY_BUSY_FIRST = 600,
	IDENTITY_DAY = 86400,
	IDENTITY_DAY_TRIES = 3,
	/* a token is asked for again this long before it runs out */
	IDENTITY_TOKEN_MARGIN = 300,
};

struct identity_state
{
	/* (a certificate, maybe expired: it refreshes, never enrolls again) */
	int has_certificate;
	unsigned long issued, expires, refresh_at;
	int off;
	int must_enroll;
	/* nothing asked of the identity paths before this */
	unsigned long wait_until;
	int failures, busy_failures, day_tries;
	/* (a new device key made once this run for another copy's) */
	int new_key_made;
	/* (an unknown copy, waiting for a confirmed game or report) */
	int waiting_for_game;
	char token[IDENTITY_TOKEN_SIZE];
	unsigned long token_until;
	/* tokens not sent before this (a site that wants the key) */
	unsigned long tokens_off_until;
	/* signed out by its player (stage B): nothing goes to the site as the
	account, the key neither, until it signs in again by code */
	int signed_out;
};

/* the step due now (the player key's file there or not) */
int identity_due(const struct identity_state *state, int has_player_key, unsigned long now);
/* a certificate taken (identity_accept's device; an expired saved one too),
refreshed refresh_after seconds from now (the site's answer; 0: at half
its life) */
void identity_have_certificate(struct identity_state *state, const struct identity_device *device,
	unsigned long refresh_after, unsigned long now);
/* how an identity path answered (status 0: none), and the state updated */
int identity_answered(struct identity_state *state, int status, const char *response, unsigned long now);
/* a token to send now, copied to token: 1; 0 to send the key */
int identity_token(const struct identity_state *state, unsigned long now, char *token, int size);
/* whether to ask for a token now (a certificate, and no token in date) */
int identity_token_due(const struct identity_state *state, unsigned long now);
void identity_set_token(struct identity_state *state, const char *token, unsigned long lifetime, unsigned long now);
/* what to do after a request sent with the token was answered status */
enum
{
	/* the answer stands: never sent again (the site may have acted on it) */
	IDENTITY_SENT_DONE = 0,
	/* a 401 for the token, which is dropped: a new token, and again with it
	once (the answer returned when there is none) */
	IDENTITY_SENT_NEW_TOKEN,
	/* the same for a token the site calls "unknown": with no new token to
	be had, the site may not speak identity, and the key goes */
	IDENTITY_SENT_UNKNOWN_TOKEN,
};
int identity_token_sent(struct identity_state *state, int status, const char *response, unsigned long now);
/* more than one account on a copy (docs/delta.md, Devices): each Halo
profile that signs in to its own account has a device key of its own,
delta_device_<n>.key and .cert (n from 1); delta_accounts.txt in the save
root maps a Halo profile (its save folder's name) to n, one "name n" a
line. The n of a Halo profile in that text, 0 for the copy's own key
(delta_device.key). Builds of 0.9.1 read it and show one account a copy */
int identity_account_slot(const char *text, const char *profile);
/* a slot's file names (n 0: the copy's own): its key's and certificate's */
void identity_slot_files(int slot, char *key_name, char *cert_name, int size);

/* whether a request may go with the player key now (stage C): only with no
certificate and not signed out, or to a site that doesn't speak identity */
int identity_key_allowed(const struct identity_state *state, unsigned long now);
/* a game or report of the player's confirmed: an unknown copy asks again soon */
void identity_confirmed(struct identity_state *state, unsigned long now);

#endif
