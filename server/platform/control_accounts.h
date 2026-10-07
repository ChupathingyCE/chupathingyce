/*
CONTROL_ACCOUNTS.H

Delta Control's accounts as bytes (server/docs/moderation.md): one login for
each person who runs a server with its owner, a name and a password (its
Argon2id hash kept, as the control API's tokens are), a role, an optional
TOTP second factor (RFC 6238: SHA-1, 30 seconds, 6 digits, as every
authenticator app reads an otpauth:// link), and an optional moderator key
(the person's game, bound to the account over Delta Peer). Invitations: a
code good once, for a while, that makes an account of a role (or sets a
new password for one). And the backoff that slows guessing one account's
password. No files, sockets or threads here, so the tests run all of it;
server_control.c keeps control_accounts.txt and serves the pages.

The file's lines (control_accounts.txt in the data folder, readable by its
owner alone):
  v1 account <name> <id> <role> <kib> <passes> <salt> <hash> <totp secret|->
     <totp last step> <moderator key|-> <created>
  v1 invite <id> <role> <code hash> <expires> <account|-> <made by>
  v1 require_2fa <0|1>
(each on one line; hex for the bytes, seconds since 1970 for the times).
*/

#ifndef CONTROL_ACCOUNTS_H
#define CONTROL_ACCOUNTS_H

#include "control_protocol.h"
#include "control_roles.h"

#include <stddef.h>
#include <stdint.h>

enum
{
	CONTROL_ACCOUNT_NAME_SIZE = 32,
	CONTROL_PASSWORD_MINIMUM = 12,
	CONTROL_PASSWORD_MAXIMUM = 128,
	CONTROL_TOTP_SECRET_BYTES = 20,
	/* base32, unpadded: 32 characters for 20 bytes */
	CONTROL_TOTP_SECRET_TEXT = 32,
	CONTROL_TOTP_STEP = 30,
	CONTROL_TOTP_DIGITS = 6,
	/* an invitation's and the first owner's setup code: random bytes, in
	hex after a prefix of four characters ("inv_", "set_") */
	CONTROL_CODE_BYTES = 16,
	CONTROL_CODE_TEXT = 4 + 2 * CONTROL_CODE_BYTES,
	/* an invitation is good this long */
	CONTROL_INVITE_SECONDS = 48 * 60 * 60,
	/* a line of the file, at most, with its end */
	CONTROL_ACCOUNT_LINE = 512,

	/* one account's wrong passwords or codes: this many in a row ... */
	CONTROL_ACCOUNT_FAILURES = 5,
	/* ... lock it this long, doubled for each further one, up to the most */
	CONTROL_ACCOUNT_LOCK_SECONDS = 60,
	CONTROL_ACCOUNT_LOCK_MAXIMUM = 60 * 60,
};

struct control_account
{
	char name[CONTROL_ACCOUNT_NAME_SIZE];
	char id[CONTROL_ID_LENGTH + 1];
	int role;
	uint32_t kib;
	uint32_t passes;
	uint8_t salt[CONTROL_SALT_BYTES];
	uint8_t hash[CONTROL_HASH_BYTES];
	int has_totp;
	uint8_t totp_secret[CONTROL_TOTP_SECRET_BYTES];
	/* the last TOTP step taken (a code is good once) */
	int64_t totp_last_step;
	int has_key;
	uint8_t key[CONTROL_KEY_BYTES];
	int64_t created;
};

struct control_invite
{
	char id[CONTROL_ID_LENGTH + 1];
	int role;
	uint8_t code_hash[32];
	int64_t expires;
	/* a new password for this account (a reset), or empty: a new account */
	char account[CONTROL_ACCOUNT_NAME_SIZE];
	char made_by[CONTROL_ACCOUNT_NAME_SIZE];
};

/* whether text may name an account: 1 to 31 of a-z, 0-9, _ . - (lowercase),
beginning with a letter or digit */
int control_account_name_valid(const char *text);
/* whether a password is one the server takes: CONTROL_PASSWORD_MINIMUM to
_MAXIMUM printable ASCII characters (spaces too), not all one character.
1, else 0 and why */
int control_password_acceptable(const char *password, const char **reason);

/* an account for a password: its name, id (random bytes, in hex), role,
salt, and the password's Argon2id hash. 1, else 0 (no memory) */
int control_account_make(const char *name, const uint8_t id_bytes[CONTROL_ID_LENGTH / 2], int role,
	const char *password, const uint8_t salt[CONTROL_SALT_BYTES], uint32_t kib, uint32_t passes,
	int64_t created, struct control_account *account);
/* a new password for an account (a new salt) */
int control_account_set_password(struct control_account *account, const char *password,
	const uint8_t salt[CONTROL_SALT_BYTES], uint32_t kib, uint32_t passes);
/* whether a password is the account's (constant time): 1, 0, -1 no memory */
int control_account_check(const struct control_account *account, const char *password);

/* the file's lines: an account, an invitation, the setting; parse: 1 if the
line is one, else 0 */
int control_account_line(const struct control_account *account, char *out, size_t size);
int control_account_parse(const char *line, struct control_account *account);
int control_invite_line(const struct control_invite *invite, char *out, size_t size);
int control_invite_parse(const char *line, struct control_invite *invite);
/* "v1 require_2fa 1": 1 and the value, else 0 */
int control_require_2fa_parse(const char *line, int *value);

/* a code from random bytes: prefix ("inv_", "set_") and hex; its hash (the
file keeps only that); whether text is a code's form */
void control_code_text(const char *prefix, const uint8_t bytes[CONTROL_CODE_BYTES], char *text, size_t size);
void control_code_hash(const char *code, uint8_t hash[32]);
int control_code_valid(const char *prefix, const char *text);

/* ---------- TOTP (RFC 6238 over RFC 4226's HOTP, HMAC-SHA-1 from Mbed TLS) */

/* the code for a step (time / CONTROL_TOTP_STEP) as text: 1, else 0 */
int control_totp_code(const uint8_t secret[CONTROL_TOTP_SECRET_BYTES], int64_t step,
	char code[CONTROL_TOTP_DIGITS + 1]);
/* whether a code is right at time now (unix seconds), a step either side,
and newer than *last_step (each code once): 1 and *last_step moved on,
else 0 */
int control_totp_check(const uint8_t secret[CONTROL_TOTP_SECRET_BYTES], const char *code, int64_t now,
	int64_t *last_step);
/* a secret as base32 (RFC 4648, no padding) */
void control_base32(const uint8_t *bytes, size_t count, char *text, size_t size);
/* the otpauth:// link an authenticator app takes: label "<issuer>:<name>" */
int control_totp_uri(const uint8_t secret[CONTROL_TOTP_SECRET_BYTES], const char *issuer, const char *name,
	char *out, size_t size);

/* ---------- one account's wrong passwords */

struct control_account_backoff
{
	int failures;
	int64_t locked_until;
};

/* whether the account may try now (seconds); if not, how long until */
int control_backoff_allowed(const struct control_account_backoff *backoff, int64_t now, int64_t *retry_after);
void control_backoff_failed(struct control_account_backoff *backoff, int64_t now);
void control_backoff_succeeded(struct control_account_backoff *backoff);

#endif
