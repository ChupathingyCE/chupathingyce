/*
CONTROL_ACCOUNTS.C

Delta Control's accounts as bytes (control_accounts.h).
*/

#include "control_accounts.h"

#include "monocypher.h"
#include "mbedtls/md.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- names and passwords */

int control_account_name_valid(const char *text)
{
	size_t length = strlen(text);
	size_t index;

	if (!length || length >= CONTROL_ACCOUNT_NAME_SIZE)
		return 0;
	for (index = 0; index < length; index++)
	{
		char character = text[index];
		int alphanumeric = (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');

		if (!alphanumeric && (index == 0 || (character != '_' && character != '.' && character != '-')))
			return 0;
	}
	return 1;
}

int control_password_acceptable(const char *password, const char **reason)
{
	size_t length = strlen(password);
	size_t index;
	int varied = 0;

	if (length < CONTROL_PASSWORD_MINIMUM)
	{
		*reason = "a password is at least 12 characters";
		return 0;
	}
	if (length > CONTROL_PASSWORD_MAXIMUM)
	{
		*reason = "a password is at most 128 characters";
		return 0;
	}
	for (index = 0; index < length; index++)
	{
		unsigned char character = (unsigned char)password[index];

		if (character < 0x20 || character > 0x7E)
		{
			*reason = "a password is printable ASCII (letters, digits, spaces, punctuation)";
			return 0;
		}
		varied |= password[index] != password[0];
	}
	if (!varied)
	{
		*reason = "a password is not one character over and over";
		return 0;
	}
	return 1;
}

int control_account_make(const char *name, const uint8_t id_bytes[CONTROL_ID_LENGTH / 2], int role,
	const char *password, const uint8_t salt[CONTROL_SALT_BYTES], uint32_t kib, uint32_t passes,
	int64_t created, struct control_account *account)
{
	memset(account, 0, sizeof(*account));
	snprintf(account->name, sizeof(account->name), "%s", name);
	control_hex_text(id_bytes, CONTROL_ID_LENGTH / 2, account->id);
	account->role = role;
	account->created = created;
	return control_account_set_password(account, password, salt, kib, passes);
}

int control_account_set_password(struct control_account *account, const char *password,
	const uint8_t salt[CONTROL_SALT_BYTES], uint32_t kib, uint32_t passes)
{
	uint8_t hash[CONTROL_HASH_BYTES];

	if (!control_argon2id(password, salt, kib, passes, hash))
		return 0;
	memcpy(account->salt, salt, CONTROL_SALT_BYTES);
	memcpy(account->hash, hash, CONTROL_HASH_BYTES);
	account->kib = kib;
	account->passes = passes;
	crypto_wipe(hash, sizeof(hash));
	return 1;
}

int control_account_check(const struct control_account *account, const char *password)
{
	uint8_t hash[CONTROL_HASH_BYTES];
	int same;

	if (!control_argon2id(password, account->salt, account->kib, account->passes, hash))
		return -1;
	same = !crypto_verify32(hash, account->hash);
	crypto_wipe(hash, sizeof(hash));
	return same;
}

/* ---------- the file's lines */

static void hex_text(const uint8_t *bytes, size_t count, char *text)
{
	control_hex_text(bytes, count, text);
}

static int hex_digit(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	return -1;
}

/* exactly count bytes of lowercase hex: 1, else 0 */
static int hex_parse(const char *text, uint8_t *bytes, size_t count)
{
	size_t index;

	if (strlen(text) != 2 * count)
		return 0;
	for (index = 0; index < count; index++)
	{
		int high = hex_digit(text[2 * index]);
		int low = hex_digit(text[2 * index + 1]);

		if (high < 0 || low < 0)
			return 0;
		bytes[index] = (uint8_t)(high << 4 | low);
	}
	return 1;
}

/* a whole decimal number from minimum to maximum: 1, else 0 */
static int number_parse(const char *text, int64_t minimum, int64_t maximum, int64_t *value)
{
	int64_t number = 0;
	int digits = 0;
	int negative = 0;

	if (*text == '-' && text[1])
	{
		negative = 1;
		text++;
	}
	for (; *text; text++)
	{
		if (*text < '0' || *text > '9' || ++digits > 18)
			return 0;
		number = number * 10 + (*text - '0');
	}
	if (!digits)
		return 0;
	if (negative)
		number = -number;
	if (number < minimum || number > maximum)
		return 0;
	*value = number;
	return 1;
}

/* a line's words, split at single spaces (its end of line gone): how many,
at most count, each at most size - 1; -1 if there are more, or one is too
long */
static int split_words(const char *line, char words[][80], int count)
{
	int found = 0;

	while (*line && *line != '\n' && *line != '\r')
	{
		size_t length = strcspn(line, " \r\n");

		if (found >= count || !length || length >= 80)
			return -1;
		memcpy(words[found], line, length);
		words[found][length] = 0;
		found++;
		line += length;
		if (*line == ' ')
		{
			line++;
			if (!*line || *line == '\n' || *line == '\r' || *line == ' ')
				return -1;
		}
	}
	return found;
}

int control_account_line(const struct control_account *account, char *out, size_t size)
{
	char salt[2 * CONTROL_SALT_BYTES + 1];
	char hash[2 * CONTROL_HASH_BYTES + 1];
	char totp[2 * CONTROL_TOTP_SECRET_BYTES + 1];
	char key[CONTROL_KEY_TEXT + 1];
	int length;

	hex_text(account->salt, CONTROL_SALT_BYTES, salt);
	hex_text(account->hash, CONTROL_HASH_BYTES, hash);
	if (account->has_totp)
		hex_text(account->totp_secret, CONTROL_TOTP_SECRET_BYTES, totp);
	else
		snprintf(totp, sizeof(totp), "-");
	if (account->has_key)
		control_key_text(account->key, key);
	else
		snprintf(key, sizeof(key), "-");
	length = snprintf(out, size, "v1 account %s %s %s %lu %lu %s %s %s %lld %s %lld", account->name, account->id,
		control_role_name(account->role), (unsigned long)account->kib, (unsigned long)account->passes, salt, hash, totp,
		(long long)account->totp_last_step, key, (long long)account->created);
	crypto_wipe(totp, sizeof(totp));
	return length < 0 || (size_t)length >= size ? -1 : length;
}

int control_account_parse(const char *line, struct control_account *account)
{
	char words[13][80];
	uint8_t id[CONTROL_ID_LENGTH / 2];
	int64_t value;
	int count = split_words(line, words, 13);

	memset(account, 0, sizeof(*account));
	if (count != 13 || strcmp(words[0], "v1") || strcmp(words[1], "account") ||
		!control_account_name_valid(words[2]) || !hex_parse(words[3], id, sizeof(id)))
	{
		goto refused;
	}
	snprintf(account->name, sizeof(account->name), "%s", words[2]);
	snprintf(account->id, sizeof(account->id), "%s", words[3]);
	account->role = control_role_parse(words[4]);
	if (account->role < CONTROL_ROLE_MODERATOR)
		goto refused;
	if (!number_parse(words[5], CONTROL_ARGON2_MINIMUM_KIB, CONTROL_ARGON2_MAXIMUM_KIB, &value))
		goto refused;
	account->kib = (uint32_t)value;
	if (!number_parse(words[6], 1, CONTROL_ARGON2_MAXIMUM_PASSES, &value))
		goto refused;
	account->passes = (uint32_t)value;
	if (!hex_parse(words[7], account->salt, CONTROL_SALT_BYTES) || !hex_parse(words[8], account->hash,
		CONTROL_HASH_BYTES))
	{
		goto refused;
	}
	if (strcmp(words[9], "-"))
	{
		if (!hex_parse(words[9], account->totp_secret, CONTROL_TOTP_SECRET_BYTES))
			goto refused;
		account->has_totp = 1;
	}
	if (!number_parse(words[10], 0, INT64_MAX / 2, &account->totp_last_step))
		goto refused;
	if (strcmp(words[11], "-"))
	{
		if (!control_key_parse(words[11], account->key))
			goto refused;
		account->has_key = 1;
	}
	if (!number_parse(words[12], 0, INT64_MAX / 2, &account->created))
		goto refused;
	crypto_wipe(words, sizeof(words));
	return 1;
refused:
	crypto_wipe(words, sizeof(words));
	crypto_wipe(account, sizeof(*account));
	return 0;
}

int control_invite_line(const struct control_invite *invite, char *out, size_t size)
{
	char hash[65];
	int length;

	hex_text(invite->code_hash, sizeof(invite->code_hash), hash);
	length = snprintf(out, size, "v1 invite %s %s %s %lld %s %s", invite->id, control_role_name(invite->role), hash,
		(long long)invite->expires, invite->account[0] ? invite->account : "-", invite->made_by[0] ?
		invite->made_by : "-");
	return length < 0 || (size_t)length >= size ? -1 : length;
}

int control_invite_parse(const char *line, struct control_invite *invite)
{
	char words[8][80];
	uint8_t id[CONTROL_ID_LENGTH / 2];
	int count = split_words(line, words, 8);

	memset(invite, 0, sizeof(*invite));
	if (count != 8 || strcmp(words[0], "v1") || strcmp(words[1], "invite") || !hex_parse(words[2], id, sizeof(id)))
		return 0;
	snprintf(invite->id, sizeof(invite->id), "%s", words[2]);
	invite->role = control_role_parse(words[3]);
	if (invite->role < CONTROL_ROLE_MODERATOR || !hex_parse(words[4], invite->code_hash, sizeof(invite->code_hash)) ||
		!number_parse(words[5], 0, INT64_MAX / 2, &invite->expires))
	{
		return 0;
	}
	if (strcmp(words[6], "-"))
	{
		if (!control_account_name_valid(words[6]))
			return 0;
		snprintf(invite->account, sizeof(invite->account), "%s", words[6]);
	}
	if (strcmp(words[7], "-"))
	{
		if (strlen(words[7]) >= sizeof(invite->made_by))
			return 0;
		snprintf(invite->made_by, sizeof(invite->made_by), "%s", words[7]);
	}
	return 1;
}

int control_require_2fa_parse(const char *line, int *value)
{
	char words[3][80];

	if (split_words(line, words, 3) != 3 || strcmp(words[0], "v1") || strcmp(words[1], "require_2fa") ||
		(strcmp(words[2], "0") && strcmp(words[2], "1")))
	{
		return 0;
	}
	*value = words[2][0] == '1';
	return 1;
}

/* ---------- codes */

void control_code_text(const char *prefix, const uint8_t bytes[CONTROL_CODE_BYTES], char *text, size_t size)
{
	char hex[2 * CONTROL_CODE_BYTES + 1];

	hex_text(bytes, CONTROL_CODE_BYTES, hex);
	snprintf(text, size, "%s%s", prefix, hex);
	crypto_wipe(hex, sizeof(hex));
}

void control_code_hash(const char *code, uint8_t hash[32])
{
	crypto_blake2b(hash, 32, (const uint8_t *)code, strlen(code));
}

int control_code_valid(const char *prefix, const char *text)
{
	size_t prefix_length = strlen(prefix);
	uint8_t bytes[CONTROL_CODE_BYTES];
	int valid;

	if (strncmp(text, prefix, prefix_length))
		return 0;
	valid = hex_parse(text + prefix_length, bytes, sizeof(bytes));
	crypto_wipe(bytes, sizeof(bytes));
	return valid;
}

/* ---------- TOTP */

int control_totp_code(const uint8_t secret[CONTROL_TOTP_SECRET_BYTES], int64_t step,
	char code[CONTROL_TOTP_DIGITS + 1])
{
	const mbedtls_md_info_t *sha1 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
	uint8_t counter[8];
	uint8_t digest[20];
	uint32_t number;
	int offset;
	int index;

	if (!sha1 || step < 0)
		return 0;
	for (index = 7; index >= 0; index--)
	{
		counter[index] = (uint8_t)(step & 0xFF);
		step >>= 8;
	}
	if (mbedtls_md_hmac(sha1, secret, CONTROL_TOTP_SECRET_BYTES, counter, sizeof(counter), digest))
		return 0;
	/* (RFC 4226's dynamic truncation) */
	offset = digest[19] & 0x0F;
	number = ((uint32_t)(digest[offset] & 0x7F) << 24) | ((uint32_t)digest[offset + 1] << 16) |
		((uint32_t)digest[offset + 2] << 8) | (uint32_t)digest[offset + 3];
	snprintf(code, CONTROL_TOTP_DIGITS + 1, "%06lu", (unsigned long)(number % 1000000UL));
	crypto_wipe(digest, sizeof(digest));
	return 1;
}

int control_totp_check(const uint8_t secret[CONTROL_TOTP_SECRET_BYTES], const char *code, int64_t now,
	int64_t *last_step)
{
	int64_t step = now / CONTROL_TOTP_STEP;
	int64_t candidate;
	int found = 0;
	int64_t found_step = 0;
	size_t index;

	if (strlen(code) != CONTROL_TOTP_DIGITS)
		return 0;
	for (index = 0; index < CONTROL_TOTP_DIGITS; index++)
	{
		if (code[index] < '0' || code[index] > '9')
			return 0;
	}
	/* (every candidate compared, in constant time, so the time taken says
	nothing of which step matched) */
	for (candidate = step - 1; candidate <= step + 1; candidate++)
	{
		char expected[CONTROL_TOTP_DIGITS + 1];
		uint8_t a[16] = { 0 }, b[16] = { 0 };

		if (!control_totp_code(secret, candidate, expected))
			continue;
		memcpy(a, expected, CONTROL_TOTP_DIGITS);
		memcpy(b, code, CONTROL_TOTP_DIGITS);
		if (!crypto_verify16(a, b) && candidate > *last_step && !found)
		{
			found = 1;
			found_step = candidate;
		}
		crypto_wipe(expected, sizeof(expected));
	}
	if (found)
		*last_step = found_step;
	return found;
}

void control_base32(const uint8_t *bytes, size_t count, char *text, size_t size)
{
	static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
	size_t length = 0;
	uint32_t buffer = 0;
	int bits = 0;
	size_t index;

	for (index = 0; index < count; index++)
	{
		buffer = buffer << 8 | bytes[index];
		bits += 8;
		while (bits >= 5)
		{
			if (length + 1 < size)
				text[length++] = alphabet[(buffer >> (bits - 5)) & 31];
			bits -= 5;
		}
	}
	if (bits > 0 && length + 1 < size)
		text[length++] = alphabet[(buffer << (5 - bits)) & 31];
	if (size)
		text[length] = 0;
}

/* text percent-encoded for a URI's path or query: its length, or -1 */
static int uri_encode(const char *text, char *out, size_t size)
{
	static const char digits[] = "0123456789ABCDEF";
	size_t length = 0;

	for (; *text; text++)
	{
		unsigned char character = (unsigned char)*text;
		int plain = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') || character == '-' || character == '.' || character == '_' ||
			character == '~';

		if (length + (plain ? 1 : 3) >= size)
			return -1;
		if (plain)
			out[length++] = (char)character;
		else
		{
			out[length++] = '%';
			out[length++] = digits[character >> 4];
			out[length++] = digits[character & 15];
		}
	}
	out[length] = 0;
	return (int)length;
}

int control_totp_uri(const uint8_t secret[CONTROL_TOTP_SECRET_BYTES], const char *issuer, const char *name,
	char *out, size_t size)
{
	char base32[CONTROL_TOTP_SECRET_TEXT + 1];
	char issuer_text[128];
	char name_text[128];
	int length;

	if (uri_encode(issuer, issuer_text, sizeof(issuer_text)) < 0 || uri_encode(name, name_text, sizeof(name_text)) < 0)
		return -1;
	control_base32(secret, CONTROL_TOTP_SECRET_BYTES, base32, sizeof(base32));
	length = snprintf(out, size, "otpauth://totp/%s:%s?secret=%s&issuer=%s&algorithm=SHA1&digits=%d&period=%d",
		issuer_text, name_text, base32, issuer_text, (int)CONTROL_TOTP_DIGITS, (int)CONTROL_TOTP_STEP);
	crypto_wipe(base32, sizeof(base32));
	return length < 0 || (size_t)length >= size ? -1 : length;
}

/* ---------- one account's wrong passwords */

int control_backoff_allowed(const struct control_account_backoff *backoff, int64_t now, int64_t *retry_after)
{
	if (backoff->locked_until > now)
	{
		if (retry_after)
			*retry_after = backoff->locked_until - now;
		return 0;
	}
	return 1;
}

void control_backoff_failed(struct control_account_backoff *backoff, int64_t now)
{
	if (++backoff->failures >= CONTROL_ACCOUNT_FAILURES)
	{
		int extra = backoff->failures - CONTROL_ACCOUNT_FAILURES;
		int64_t seconds = CONTROL_ACCOUNT_LOCK_SECONDS;

		while (extra-- > 0 && seconds < CONTROL_ACCOUNT_LOCK_MAXIMUM)
			seconds *= 2;
		if (seconds > CONTROL_ACCOUNT_LOCK_MAXIMUM)
			seconds = CONTROL_ACCOUNT_LOCK_MAXIMUM;
		backoff->locked_until = now + seconds;
	}
}

void control_backoff_succeeded(struct control_account_backoff *backoff)
{
	backoff->failures = 0;
	backoff->locked_until = 0;
}
