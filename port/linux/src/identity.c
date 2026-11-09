/*
IDENTITY.C

Delta's device identity, stage A (identity.h; docs/delta.md, "Device
identity"). No SDL and no game: the certificates, the signed messages, the
requests' text and the schedule. browser.c's thread holds the files and
does the asking.
*/

#include "identity.h"
#include "halo_console_features.h"

/* (consoles leave it out until their own endpoints come) */
#ifndef HALO_CONSOLE_NO_DELTA_IDENTITY

#include "monocypher.h"
#include "monocypher-ed25519.h"

#include <stdio.h>
#include <string.h>

static const char issuer_label[] = "chupathingy identity issuer v1\n";
static const char device_label[] = "chupathingy device cert v1\n";
static const char revoked_label[] = "chupathingy device revocations v1\n";
static const char device_id_label[] = "chupathingy device id\n";
static const char enroll_label[] = "chupathingy legacy enroll v1\n";
static const char refresh_label[] = "chupathingy device refresh v1\n";
static const char token_label[] = "chupathingy device token v1\n";
static const char start_label[] = "chupathingy device start v1\n";
static const char confirm_label[] = "chupathingy device confirm v1\n";
static const char signout_label[] = "chupathingy device signout v1\n";
static const char code_label[] = "chupathingy device code v1\n";
/* (a code's characters: no 0, 1, I, L or O, as a console's Link Profile code) */
static const char code_alphabet[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";

/* ---------- SHA-256 (FIPS 180-4), for the IDs */

static const unsigned long sha256_k[64] =
{
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROTATE(x, n) ((((x) >> (n)) | ((x) << (32 - (n)))) & 0xFFFFFFFFUL)

static void sha256_block(unsigned long *state, const unsigned char *block)
{
	unsigned long w[64], a, b, c, d, e, f, g, h, t1, t2;
	int index;

	for (index = 0; index < 16; index++)
	{
		w[index] = (unsigned long)block[index * 4] << 24 | (unsigned long)block[index * 4 + 1] << 16 |
			(unsigned long)block[index * 4 + 2] << 8 | block[index * 4 + 3];
	}
	for (; index < 64; index++)
	{
		unsigned long s0 = ROTATE(w[index - 15], 7) ^ ROTATE(w[index - 15], 18) ^ (w[index - 15] >> 3);
		unsigned long s1 = ROTATE(w[index - 2], 17) ^ ROTATE(w[index - 2], 19) ^ (w[index - 2] >> 10);

		w[index] = (w[index - 16] + s0 + w[index - 7] + s1) & 0xFFFFFFFFUL;
	}
	a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6], h = state[7];
	for (index = 0; index < 64; index++)
	{
		t1 = (h + (ROTATE(e, 6) ^ ROTATE(e, 11) ^ ROTATE(e, 25)) + ((e & f) ^ (~e & g)) + sha256_k[index] +
			w[index]) & 0xFFFFFFFFUL;
		t2 = ((ROTATE(a, 2) ^ ROTATE(a, 13) ^ ROTATE(a, 22)) + ((a & b) ^ (a & c) ^ (b & c))) & 0xFFFFFFFFUL;
		h = g, g = f, f = e, e = (d + t1) & 0xFFFFFFFFUL, d = c, c = b, b = a, a = (t1 + t2) & 0xFFFFFFFFUL;
	}
	state[0] = (state[0] + a) & 0xFFFFFFFFUL, state[1] = (state[1] + b) & 0xFFFFFFFFUL;
	state[2] = (state[2] + c) & 0xFFFFFFFFUL, state[3] = (state[3] + d) & 0xFFFFFFFFUL;
	state[4] = (state[4] + e) & 0xFFFFFFFFUL, state[5] = (state[5] + f) & 0xFFFFFFFFUL;
	state[6] = (state[6] + g) & 0xFFFFFFFFUL, state[7] = (state[7] + h) & 0xFFFFFFFFUL;
}

void identity_sha256(const void *data, int size, unsigned char *digest)
{
	unsigned long state[8] =
	{
		0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
	};
	const unsigned char *bytes = (const unsigned char *)data;
	unsigned char last[128];
	int done = 0, rest, padded, index;
	unsigned long long bits = (unsigned long long)size * 8;

	for (; size - done >= 64; done += 64)
		sha256_block(state, bytes + done);
	rest = size - done;
	memset(last, 0, sizeof(last));
	memcpy(last, bytes + done, (size_t)rest);
	last[rest] = 0x80;
	padded = rest < 56 ? 64 : 128;
	for (index = 0; index < 8; index++)
		last[padded - 1 - index] = (unsigned char)(bits >> (8 * index));
	sha256_block(state, last);
	if (padded == 128)
		sha256_block(state, last + 64);
	for (index = 0; index < 32; index++)
		digest[index] = (unsigned char)(state[index / 4] >> (24 - 8 * (index % 4)));
}

/* ---------- keys and signatures */

void identity_public(const unsigned char *seed, unsigned char *public_key)
{
	unsigned char copy[IDENTITY_SEED_SIZE];
	unsigned char secret_key[64];

	/* (the key pair's making wipes the seed it is given) */
	memcpy(copy, seed, sizeof(copy));
	crypto_ed25519_key_pair(secret_key, public_key, copy);
	crypto_wipe(secret_key, sizeof(secret_key));
}

void identity_sign(const unsigned char *seed, const void *message, int size, unsigned char *signature)
{
	unsigned char secret_key[64];

	/* (Monocypher's secret key: the seed, then the public key) */
	memcpy(secret_key, seed, IDENTITY_SEED_SIZE);
	identity_public(seed, secret_key + IDENTITY_SEED_SIZE);
	crypto_ed25519_sign(signature, secret_key, (const unsigned char *)message, (size_t)size);
	crypto_wipe(secret_key, sizeof(secret_key));
}

/* a key of small order: anyone can sign for it */
static int small_order(const unsigned char *public_key)
{
	static const unsigned char zero[32];
	static const unsigned char scalar[32] = { 1 };
	unsigned char x25519_public[32], product[32];

	crypto_eddsa_to_x25519(x25519_public, public_key);
	crypto_x25519(product, scalar, x25519_public);
	return !memcmp(product, zero, sizeof(zero));
}

int identity_verify(const unsigned char *public_key, const void *message, int size, const unsigned char *signature)
{
	/* (Monocypher's check turns away an S past the group's order) */
	return !small_order(public_key) &&
		crypto_ed25519_check(signature, public_key, (const unsigned char *)message, (size_t)size) == 0;
}

static void labeled_hash(const char *label, const unsigned char *data, int size, unsigned char *digest)
{
	unsigned char buffer[64 + IDENTITY_KEY_SIZE];
	int length = (int)strlen(label);

	memcpy(buffer, label, (size_t)length);
	memcpy(buffer + length, data, (size_t)size);
	identity_sha256(buffer, length + size, digest);
}

void identity_device_id(const unsigned char *public_key, unsigned char *id)
{
	unsigned char digest[32];

	labeled_hash(device_id_label, public_key, IDENTITY_KEY_SIZE, digest);
	memcpy(id, digest, IDENTITY_ID_SIZE);
}

void identity_key_id(const unsigned char *public_key, unsigned char *id)
{
	unsigned char digest[32];

	identity_sha256(public_key, IDENTITY_KEY_SIZE, digest);
	memcpy(id, digest, IDENTITY_KEY_ID_SIZE);
}

/* (a body's signature, over its label and the body) */
static int signed_by(const unsigned char *public_key, const char *label, const unsigned char *body, int size,
	const unsigned char *signature)
{
	unsigned char message[64 + IDENTITY_REVOKED_HEADER + 16 * IDENTITY_REVOKED_MOST];
	int length = (int)strlen(label);

	if (size < 0 || length + size > (int)sizeof(message))
		return 0;
	memcpy(message, label, (size_t)length);
	memcpy(message + length, body, (size_t)size);
	return identity_verify(public_key, message, length + size, signature);
}

/* ---------- the certificates and the list */

static unsigned long read32(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] | (unsigned long)bytes[1] << 8 | (unsigned long)bytes[2] << 16 |
		(unsigned long)bytes[3] << 24;
}

static unsigned read16(const unsigned char *bytes)
{
	return (unsigned)bytes[0] | (unsigned)bytes[1] << 8;
}

static int all_zero(const unsigned char *bytes, int size)
{
	int index;

	for (index = 0; index < size; index++)
	{
		if (bytes[index])
			return 0;
	}
	return 1;
}

static int in_date(unsigned long from, unsigned long until, unsigned long now)
{
	if (now + IDENTITY_GRACE < from)
		return IDENTITY_NOT_YET;
	if (now >= until + IDENTITY_GRACE)
		return IDENTITY_EXPIRED;
	return IDENTITY_OK;
}

int identity_parse_issuer(const unsigned char *data, int size, struct identity_issuer *issuer)
{
	if (!data || size != IDENTITY_ISSUER_SIZE || memcmp(data, "CISS", 4) || data[4] != 1 || data[5] || data[6] ||
		data[7])
	{
		return 0;
	}
	issuer->valid_from = read32(data + 8);
	issuer->valid_until = read32(data + 12);
	if (issuer->valid_from >= issuer->valid_until)
		return 0;
	memcpy(issuer->public_key, data + 16, IDENTITY_KEY_SIZE);
	identity_key_id(issuer->public_key, issuer->key_id);
	return 1;
}

int identity_check_issuer(const unsigned char *data, int size, const unsigned char (*roots)[IDENTITY_KEY_SIZE],
	int root_count, unsigned long now, struct identity_issuer *issuer)
{
	int index, signed_ok = 0;

	if (!identity_parse_issuer(data, size, issuer))
		return IDENTITY_MALFORMED;
	for (index = 0; index < root_count && !signed_ok; index++)
	{
		signed_ok = !all_zero(roots[index], IDENTITY_KEY_SIZE) &&
			signed_by(roots[index], issuer_label, data, 48, data + 48);
	}
	if (!signed_ok)
		return IDENTITY_NOT_ROOT_SIGNED;
	if (small_order(issuer->public_key))
		return IDENTITY_WEAK_KEY;
	return in_date(issuer->valid_from, issuer->valid_until, now);
}

int identity_parse_device(const unsigned char *data, int size, struct identity_device *device)
{
	int length, index;

	if (!data || size < IDENTITY_DEVICE_HEADER + IDENTITY_SIGNATURE_SIZE || memcmp(data, "CDEV", 4) || data[4] != 1)
		return 0;
	length = data[6];
	if ((length && (length < 3 || length > IDENTITY_HANDLE_MOST)) ||
		size != IDENTITY_DEVICE_HEADER + length + IDENTITY_SIGNATURE_SIZE)
	{
		return 0;
	}
	for (index = 0; index < length; index++)
	{
		int c = data[IDENTITY_DEVICE_HEADER + index];

		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
			return 0;
		device->handle[index] = (char)c;
	}
	device->handle[length] = 0;
	device->platform = data[5];
	device->flags = data[7];
	device->issued = read32(data + 8);
	device->expires = read32(data + 12);
	if (device->issued >= device->expires)
		return 0;
	memcpy(device->serial, data + 16, 8);
	memcpy(device->issuer, data + 24, IDENTITY_KEY_ID_SIZE);
	memcpy(device->account, data + 32, IDENTITY_ID_SIZE);
	memcpy(device->public_key, data + 48, IDENTITY_KEY_SIZE);
	identity_device_id(device->public_key, device->device_id);
	device->capabilities = read32(data + 80);
	device->body_size = IDENTITY_DEVICE_HEADER + length;
	return 1;
}

int identity_check_device(const unsigned char *data, int size, const struct identity_issuer *issuer,
	unsigned long now, struct identity_device *device)
{
	if (!identity_parse_device(data, size, device))
		return IDENTITY_MALFORMED;
	if (!issuer || memcmp(device->issuer, issuer->key_id, IDENTITY_KEY_ID_SIZE))
		return IDENTITY_OTHER_ISSUER;
	if (!signed_by(issuer->public_key, device_label, data, device->body_size, data + device->body_size))
		return IDENTITY_NOT_ISSUER_SIGNED;
	if (device->issued < issuer->valid_from || device->issued > issuer->valid_until)
		return IDENTITY_OUTSIDE_ISSUER;
	if (small_order(device->public_key))
		return IDENTITY_WEAK_KEY;
	return in_date(device->issued, device->expires, now);
}

int identity_parse_revoked(const unsigned char *data, int size, struct identity_revoked *revoked)
{
	int count, index;

	if (!data || size < IDENTITY_REVOKED_HEADER + IDENTITY_SIGNATURE_SIZE || memcmp(data, "CREV", 4) ||
		data[4] != 1 || data[5] || data[6] || data[7] || data[30] || data[31])
	{
		return 0;
	}
	count = (int)read16(data + 28);
	if (count > IDENTITY_REVOKED_MOST || size != IDENTITY_REVOKED_HEADER + 16 * count + IDENTITY_SIGNATURE_SIZE)
		return 0;
	/* (sorted, no repeats: what identity_is_revoked's search needs) */
	for (index = 1; index < count; index++)
	{
		if (memcmp(data + IDENTITY_REVOKED_HEADER + 16 * (index - 1), data + IDENTITY_REVOKED_HEADER + 16 * index,
			16) >= 0)
		{
			return 0;
		}
	}
	revoked->serial = read32(data + 8);
	revoked->issued = read32(data + 12);
	revoked->next_update = read32(data + 16);
	memcpy(revoked->issuer, data + 20, IDENTITY_KEY_ID_SIZE);
	revoked->count = count;
	revoked->ids = data + IDENTITY_REVOKED_HEADER;
	revoked->body_size = IDENTITY_REVOKED_HEADER + 16 * count;
	return 1;
}

int identity_check_revoked(const unsigned char *data, int size, const struct identity_issuer *issuer,
	struct identity_revoked *revoked)
{
	if (!identity_parse_revoked(data, size, revoked))
		return IDENTITY_MALFORMED;
	if (!issuer || memcmp(revoked->issuer, issuer->key_id, IDENTITY_KEY_ID_SIZE))
		return IDENTITY_OTHER_ISSUER;
	if (!signed_by(issuer->public_key, revoked_label, data, revoked->body_size, data + revoked->body_size))
		return IDENTITY_NOT_ISSUER_SIGNED;
	return IDENTITY_OK;
}

int identity_is_revoked(const struct identity_revoked *revoked, const unsigned char *device_id)
{
	int low = 0, high = revoked ? revoked->count - 1 : -1;

	while (low <= high)
	{
		int middle = (low + high) / 2;
		int order = memcmp(revoked->ids + 16 * middle, device_id, 16);

		if (!order)
			return 1;
		if (order < 0)
			low = middle + 1;
		else
			high = middle - 1;
	}
	return 0;
}

int identity_accept(const unsigned char *certificate, int certificate_size, const unsigned char *issuer_data,
	int issuer_size, const unsigned char (*roots)[IDENTITY_KEY_SIZE], int root_count,
	const unsigned char *own_key, unsigned long now, struct identity_device *device)
{
	struct identity_issuer issuer;
	int index, any_root = 0, result;

	for (index = 0; index < root_count; index++)
		any_root |= !all_zero(roots[index], IDENTITY_KEY_SIZE);
	if (any_root)
	{
		result = identity_check_issuer(issuer_data, issuer_size, roots, root_count, now, &issuer);
		if (result != IDENTITY_OK)
			return result;
	}
	else if (!identity_parse_issuer(issuer_data, issuer_size, &issuer))
	{
		return IDENTITY_MALFORMED;
	}
	result = identity_check_device(certificate, certificate_size, &issuer, now, device);
	if (result != IDENTITY_OK)
		return result;
	if (memcmp(device->public_key, own_key, IDENTITY_KEY_SIZE))
		return IDENTITY_NOT_MINE;
	return IDENTITY_OK;
}

/* ---------- what a device signs */

void identity_wipe(void *data, int size)
{
	if (data && size > 0)
		crypto_wipe(data, (size_t)size);
}

static int audience_ok(const char *audience)
{
	int length = audience ? (int)strlen(audience) : 0, index;

	if (length < 1 || length > IDENTITY_AUDIENCE_MOST)
		return 0;
	for (index = 0; index < length; index++)
	{
		char c = audience[index];

		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-'))
			return 0;
	}
	return 1;
}

int identity_audience(const char *url, char *audience, int size)
{
	const char *start = url ? strstr(url, "://") : NULL;
	int length = 0;

	if (size <= 0)
		return 0;
	audience[0] = 0;
	if (!start)
		return 0;
	for (start += 3; start[length] && !strchr(":/?#", start[length]); length++)
	{
		if (length >= size - 1)
			return audience[0] = 0, 0;
		audience[length] = (char)(start[length] >= 'A' && start[length] <= 'Z' ? start[length] - 'A' + 'a' :
			start[length]);
	}
	audience[length] = 0;
	if (!audience_ok(audience))
		return audience[0] = 0, 0;
	return length;
}

/* label, audience (its length, then it), then up to three fields */
static int message(const char *label, const char *audience, const unsigned char *a, int a_size,
	const unsigned char *b, int b_size, const unsigned char *c, int c_size, unsigned char *out)
{
	int length = (int)strlen(label), audience_length;

	if (!audience_ok(audience))
		return -1;
	audience_length = (int)strlen(audience);
	memcpy(out, label, (size_t)length);
	out[length++] = (unsigned char)audience_length;
	memcpy(out + length, audience, (size_t)audience_length);
	length += audience_length;
	memcpy(out + length, a, (size_t)a_size);
	memcpy(out + length + a_size, b, (size_t)b_size);
	if (c_size)
		memcpy(out + length + a_size + b_size, c, (size_t)c_size);
	return length + a_size + b_size + c_size;
}

int identity_enroll_message(const char *audience, const unsigned char *nonce, const unsigned char *copy_id,
	const unsigned char *public_key, unsigned char *out)
{
	return message(enroll_label, audience, nonce, IDENTITY_NONCE_SIZE, copy_id, IDENTITY_ID_SIZE, public_key,
		IDENTITY_KEY_SIZE, out);
}

int identity_refresh_message(const char *audience, const unsigned char *nonce, const unsigned char *device_id,
	unsigned char *out)
{
	return message(refresh_label, audience, nonce, IDENTITY_NONCE_SIZE, device_id, IDENTITY_ID_SIZE, NULL, 0, out);
}

int identity_token_message(const char *audience, const unsigned char *nonce, const unsigned char *device_id,
	unsigned char *out)
{
	return message(token_label, audience, nonce, IDENTITY_NONCE_SIZE, device_id, IDENTITY_ID_SIZE, NULL, 0, out);
}

int identity_start_message(const char *audience, const unsigned char *nonce, const unsigned char *public_key,
	unsigned char *out)
{
	return message(start_label, audience, nonce, IDENTITY_NONCE_SIZE, public_key, IDENTITY_KEY_SIZE, NULL, 0, out);
}

int identity_confirm_message(const char *audience, const unsigned char *nonce, const unsigned char *device_id,
	int accept, const char *handle, unsigned char *out)
{
	unsigned char tail[2 + IDENTITY_HANDLE_MOST];
	int length = handle ? (int)strlen(handle) : 0;

	if (length > IDENTITY_HANDLE_MOST)
		return -1;
	tail[0] = (unsigned char)(accept ? 1 : 0);
	tail[1] = (unsigned char)length;
	if (length)
		memcpy(tail + 2, handle, (size_t)length);
	return message(confirm_label, audience, nonce, IDENTITY_NONCE_SIZE, device_id, IDENTITY_ID_SIZE, tail,
		2 + length, out);
}

int identity_signout_message(const char *audience, const unsigned char *nonce, const unsigned char *device_id,
	unsigned char *out)
{
	return message(signout_label, audience, nonce, IDENTITY_NONCE_SIZE, device_id, IDENTITY_ID_SIZE, NULL, 0, out);
}

void identity_device_code(const unsigned char *public_key, const unsigned char *code_nonce, char *code)
{
	unsigned char data[sizeof(code_label) - 1 + IDENTITY_KEY_SIZE + IDENTITY_NONCE_SIZE], digest[32];
	int index, at = 0;

	memcpy(data, code_label, sizeof(code_label) - 1);
	memcpy(data + sizeof(code_label) - 1, public_key, IDENTITY_KEY_SIZE);
	memcpy(data + sizeof(code_label) - 1 + IDENTITY_KEY_SIZE, code_nonce, IDENTITY_NONCE_SIZE);
	identity_sha256(data, (int)sizeof(data), digest);
	for (index = 0; index < IDENTITY_CODE_LENGTH; index++)
	{
		if (index == IDENTITY_CODE_LENGTH / 2)
			code[at++] = '-';
		code[at++] = code_alphabet[digest[index] % (sizeof(code_alphabet) - 1)];
	}
	code[at] = 0;
}

int identity_clean_code(const char *typed, char *code)
{
	int count = 0, at = 0;

	for (; typed && *typed; typed++)
	{
		char c = *typed;

		if (c == '-' || c == ' ')
			continue;
		if (c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 'A');
		if (!strchr(code_alphabet, c) || !c || count == IDENTITY_CODE_LENGTH)
			return code[0] = 0, 0;
		if (count == IDENTITY_CODE_LENGTH / 2)
			code[at++] = '-';
		code[at++] = c;
		count++;
	}
	code[at] = 0;
	return count == IDENTITY_CODE_LENGTH || (code[0] = 0, 0);
}

int identity_approval_code(const char *typed, char *code)
{
	int length = 0;

	for (; typed && *typed; typed++)
	{
		char c = *typed >= 'a' && *typed <= 'z' ? (char)(*typed - 'a' + 'A') : *typed;

		if (c == '-' || c == ' ')
			continue;
		if (!c || !strchr(code_alphabet, c) || length == IDENTITY_CODE_LENGTH)
			return code[0] = 0, 0;
		code[length++] = c;
	}
	code[length] = 0;
	return length == 6 || length == IDENTITY_CODE_LENGTH || (code[0] = 0, 0);
}

/* ---------- text */

void identity_hex(const unsigned char *bytes, int size, char *text)
{
	static const char digits[] = "0123456789abcdef";
	int index;

	for (index = 0; index < size; index++)
	{
		text[index * 2] = digits[bytes[index] >> 4];
		text[index * 2 + 1] = digits[bytes[index] & 15];
	}
	text[size * 2] = 0;
}

static int hex_value(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

int identity_unhex(const char *text, int length, unsigned char *bytes, int size)
{
	int index;

	if (!text || length != 2 * size)
		return 0;
	for (index = 0; index < size; index++)
	{
		int high = hex_value((unsigned char)text[index * 2]), low = hex_value((unsigned char)text[index * 2 + 1]);

		if (high < 0 || low < 0)
			return 0;
		bytes[index] = (unsigned char)(high << 4 | low);
	}
	return 1;
}

static const char *skip_space(const char *at)
{
	while (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n')
		at++;
	return at;
}

/* past a JSON string starting at its quote (NULL if it does not end) */
static const char *skip_string(const char *at)
{
	for (at++; *at && *at != '"'; at++)
	{
		if (*at == '\\' && at[1])
			at++;
	}
	return *at ? at + 1 : NULL;
}

/* to the comma or bracket after a JSON value (NULL if it is cut off) */
static const char *skip_value(const char *at)
{
	int depth = 0;

	for (;;)
	{
		if (!*at)
			return NULL;
		if (*at == '"')
		{
			at = skip_string(at);
			if (!at)
				return NULL;
			continue;
		}
		if (!depth && (*at == ',' || *at == '}' || *at == ']'))
			return at;
		if (*at == '{' || *at == '[')
			depth++;
		else if (*at == '}' || *at == ']')
			depth--;
		at++;
	}
}

int identity_json_member(const char *json, const char *name, char *out, int size)
{
	const char *at = json ? skip_space(json) : NULL;
	int name_length = (int)strlen(name);

	if (!at || *at != '{' || size <= 0)
		return -1;
	at = skip_space(at + 1);
	while (*at == '"')
	{
		const char *key = at + 1, *end = skip_string(at), *value, *value_end;

		if (!end)
			return -1;
		at = skip_space(end);
		if (*at != ':')
			return -1;
		value = skip_space(at + 1);
		value_end = skip_value(value);
		if (!value_end)
			return -1;
		if (end - 1 - key == name_length && !memcmp(key, name, (size_t)name_length))
		{
			int length;

			if (*value == '"')
			{
				value++;
				length = (int)(value_end - value) - 1;
				while (length > 0 && value[length] != '"')
					length--;
			}
			else
			{
				length = (int)(value_end - value);
				while (length > 0 && (value[length - 1] == ' ' || value[length - 1] == '\n' ||
					value[length - 1] == '\r' || value[length - 1] == '\t'))
				{
					length--;
				}
			}
			if (length > size - 1)
				length = size - 1;
			memcpy(out, value, (size_t)length);
			out[length] = 0;
			return length;
		}
		at = skip_space(value_end);
		if (*at != ',')
			return -1;
		at = skip_space(at + 1);
	}
	return -1;
}

int identity_json_true(const char *json, const char *name)
{
	char value[8];

	return identity_json_member(json, name, value, sizeof(value)) == 4 && !strcmp(value, "true");
}

/* (a JSON string's characters: printable ASCII, no quote or backslash) */
static void clean_text(char *out, int size, const char *text)
{
	int used = 0;

	for (; text && *text && used < size - 1; text++)
	{
		if (*text >= ' ' && *text <= '~' && *text != '"' && *text != '\\')
			out[used++] = *text;
	}
	out[used] = 0;
}

int identity_enroll_body(char *out, int size, const char *player_key_hex, const unsigned char *public_key,
	const char *nonce_hex, const unsigned char *signature, const char *platform, const char *build)
{
	char key_text[2 * IDENTITY_KEY_SIZE + 1], signature_text[2 * IDENTITY_SIGNATURE_SIZE + 1];
	char platform_text[24], build_text[48];
	int length;

	identity_hex(public_key, IDENTITY_KEY_SIZE, key_text);
	identity_hex(signature, IDENTITY_SIGNATURE_SIZE, signature_text);
	clean_text(platform_text, sizeof(platform_text), platform);
	clean_text(build_text, sizeof(build_text), build);
	length = snprintf(out, (size_t)size, "{\"key\": \"%s\", \"device_key\": \"%s\", \"nonce\": \"%s\", "
		"\"signature\": \"%s\", \"platform\": \"%s\", \"build\": \"%s\"}", player_key_hex, key_text, nonce_hex,
		signature_text, platform_text, build_text);
	return length < 0 || length >= size ? -1 : length;
}

int identity_signed_body(char *out, int size, const unsigned char *device_id, const char *nonce_hex,
	const unsigned char *signature)
{
	char id_text[2 * IDENTITY_ID_SIZE + 1], signature_text[2 * IDENTITY_SIGNATURE_SIZE + 1];
	int length;

	identity_hex(device_id, IDENTITY_ID_SIZE, id_text);
	identity_hex(signature, IDENTITY_SIGNATURE_SIZE, signature_text);
	length = snprintf(out, (size_t)size, "{\"device_id\": \"%s\", \"nonce\": \"%s\", \"signature\": \"%s\"}",
		id_text, nonce_hex, signature_text);
	return length < 0 || length >= size ? -1 : length;
}

int identity_start_body(char *out, int size, const unsigned char *public_key, const char *nonce_hex,
	const unsigned char *signature, const char *platform, const char *build, const char *name)
{
	char key_text[2 * IDENTITY_KEY_SIZE + 1], signature_text[2 * IDENTITY_SIGNATURE_SIZE + 1];
	char platform_text[24], build_text[48], name_text[40];
	int length;

	identity_hex(public_key, IDENTITY_KEY_SIZE, key_text);
	identity_hex(signature, IDENTITY_SIGNATURE_SIZE, signature_text);
	clean_text(platform_text, sizeof(platform_text), platform);
	clean_text(build_text, sizeof(build_text), build);
	clean_text(name_text, 33, name);
	length = snprintf(out, (size_t)size, "{\"device_key\": \"%s\", \"nonce\": \"%s\", \"signature\": \"%s\", "
		"\"platform\": \"%s\", \"build\": \"%s\", \"name\": \"%s\"}", key_text, nonce_hex, signature_text,
		platform_text, build_text, name_text);
	return length < 0 || length >= size ? -1 : length;
}

int identity_confirm_body(char *out, int size, const char *poll_token, const unsigned char *device_id,
	const char *nonce_hex, const unsigned char *signature, int accept)
{
	char id_text[2 * IDENTITY_ID_SIZE + 1], signature_text[2 * IDENTITY_SIGNATURE_SIZE + 1], token_text[72];
	int length;

	identity_hex(device_id, IDENTITY_ID_SIZE, id_text);
	identity_hex(signature, IDENTITY_SIGNATURE_SIZE, signature_text);
	clean_text(token_text, IDENTITY_POLL_TOKEN_SIZE, poll_token);
	length = snprintf(out, (size_t)size, "{\"poll_token\": \"%s\", \"device_id\": \"%s\", \"nonce\": \"%s\", "
		"\"accept\": %s, \"signature\": \"%s\"}", token_text, id_text, nonce_hex, accept ? "true" : "false",
		signature_text);
	return length < 0 || length >= size ? -1 : length;
}

/* a member of decimal digits: 1 and its value, or 0 */
static int json_number(const char *json, const char *name, unsigned long *value)
{
	char number[16];
	int length, index;
	unsigned long seconds = 0;

	length = identity_json_member(json, name, number, sizeof(number));
	for (index = 0; index < length && number[index] >= '0' && number[index] <= '9'; index++)
		seconds = seconds * 10 + (unsigned long)(number[index] - '0');
	if (length <= 0 || index != length)
		return 0;
	*value = seconds;
	return 1;
}

int identity_start_answer(const char *json, unsigned char *code_nonce, char *poll_token, unsigned long *interval,
	unsigned long *lifetime)
{
	char text[2 * IDENTITY_NONCE_SIZE + 1];
	int index;

	if (identity_json_member(json, "code_nonce", text, sizeof(text)) != 2 * IDENTITY_NONCE_SIZE ||
		!identity_unhex(text, 2 * IDENTITY_NONCE_SIZE, code_nonce, IDENTITY_NONCE_SIZE) ||
		identity_json_member(json, "poll_token", poll_token, IDENTITY_POLL_TOKEN_SIZE) != IDENTITY_POLL_TOKEN_SIZE - 1 ||
		!json_number(json, "interval", interval) || !json_number(json, "expires_in", lifetime) || !*lifetime)
	{
		poll_token[0] = 0;
		return 0;
	}
	for (index = 0; index < IDENTITY_POLL_TOKEN_SIZE - 1; index++)
	{
		if (!((poll_token[index] >= '0' && poll_token[index] <= '9') || (poll_token[index] >= 'a' && poll_token[index] <= 'f')))
			return poll_token[0] = 0, 0;
	}
	/* (asked no more often than every 2 seconds, nor less than every 30) */
	if (*interval < 2)
		*interval = 2;
	if (*interval > 30)
		*interval = 30;
	return 1;
}

int identity_nonce_answer(const char *json, char *nonce_hex, unsigned char *nonce)
{
	return identity_json_member(json, "nonce", nonce_hex, 2 * IDENTITY_NONCE_SIZE + 1) == 2 * IDENTITY_NONCE_SIZE &&
		identity_unhex(nonce_hex, 2 * IDENTITY_NONCE_SIZE, nonce, IDENTITY_NONCE_SIZE);
}

int identity_certificate_answer(const char *json, unsigned char *certificate, int *certificate_size,
	unsigned char *issuer, int *issuer_size)
{
	char text[2 * IDENTITY_DEVICE_MOST + 2];
	int length;

	length = identity_json_member(json, "certificate", text, sizeof(text));
	if (length <= 0 || length > 2 * IDENTITY_DEVICE_MOST || length % 2 ||
		!identity_unhex(text, length, certificate, length / 2))
	{
		return 0;
	}
	*certificate_size = length / 2;
	length = identity_json_member(json, "issuer", text, sizeof(text));
	if (length != 2 * IDENTITY_ISSUER_SIZE || !identity_unhex(text, length, issuer, IDENTITY_ISSUER_SIZE))
		return 0;
	*issuer_size = IDENTITY_ISSUER_SIZE;
	return 1;
}

int identity_token_answer(const char *json, char *token, int size, unsigned long *lifetime)
{
	char number[16];
	int length, index;
	unsigned long seconds = 0;

	length = identity_json_member(json, "token", token, size);
	if (length <= 0 || length >= IDENTITY_TOKEN_SIZE || length >= size)
		return 0;
	for (index = 0; index < length; index++)
	{
		char c = token[index];

		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
			c == '-'))
		{
			token[0] = 0;
			return 0;
		}
	}
	length = identity_json_member(json, "expires_in", number, sizeof(number));
	for (index = 0; index < length && number[index] >= '0' && number[index] <= '9'; index++)
		seconds = seconds * 10 + (unsigned long)(number[index] - '0');
	if (length <= 0 || index != length || !seconds)
	{
		token[0] = 0;
		return 0;
	}
	*lifetime = seconds;
	return 1;
}

/* ---------- when to ask */

int identity_due(const struct identity_state *state, int has_player_key, unsigned long now)
{
	if (state->off || state->signed_out || now < state->wait_until)
		return IDENTITY_STEP_NONE;
	if (!state->has_certificate || state->must_enroll)
		return has_player_key ? IDENTITY_STEP_ENROLL : IDENTITY_STEP_NONE;
	/* (an expired one too: a refresh, with the device key alone) */
	if (now >= state->refresh_at)
		return IDENTITY_STEP_REFRESH;
	return IDENTITY_STEP_NONE;
}

void identity_have_certificate(struct identity_state *state, const struct identity_device *device,
	unsigned long refresh_after, unsigned long now)
{
	state->has_certificate = 1;
	state->must_enroll = 0;
	state->issued = device->issued;
	state->expires = device->expires;
	/* (a duration: seconds from now; at half the life when not given, and
	never past the end) */
	state->refresh_at = refresh_after ? now + refresh_after :
		device->issued + (device->expires - device->issued) / 2;
	if (state->refresh_at > device->expires)
		state->refresh_at = device->expires;
	state->failures = 0;
	state->busy_failures = 0;
	state->day_tries = 0;
}

/* a wait that doubles from first to IDENTITY_LATER with each count */
static unsigned long growing(int first, int *count)
{
	unsigned long wait = (unsigned long)first << (*count < 8 ? *count : 8);

	if (wait > IDENTITY_LATER)
		wait = IDENTITY_LATER;
	(*count)++;
	return wait;
}

int identity_answered(struct identity_state *state, int status, const char *response, unsigned long now)
{
	int answer;

	if (status == 200)
	{
		state->failures = 0;
		state->busy_failures = 0;
		state->waiting_for_game = 0;
		return IDENTITY_ANSWER_OK;
	}
	if (status == 404 && identity_json_true(response, "enroll"))
		answer = IDENTITY_ANSWER_ENROLL;
	else if (status == 403 && (identity_json_true(response, "revoked") || identity_json_true(response, "signed_out")))
		answer = IDENTITY_ANSWER_OFF;
	else if (status == 409 && identity_json_true(response, "other_copy") && !state->new_key_made)
		answer = IDENTITY_ANSWER_NEW_KEY;
	else if (status == 403 && identity_json_true(response, "unknown_copy"))
		answer = IDENTITY_ANSWER_UNKNOWN_COPY;
	else if (status == 403 || status == 409)
		answer = IDENTITY_ANSWER_DAY;
	else if (status == 404 || status == 405 || status == 501)
		answer = IDENTITY_ANSWER_LATER;
	else if (status == 503)
		answer = IDENTITY_ANSWER_BUSY;
	else
		answer = IDENTITY_ANSWER_RETRY;
	switch (answer)
	{
	case IDENTITY_ANSWER_ENROLL:
		state->must_enroll = 1;
		identity_wipe(state->token, sizeof(state->token));
		break;
	case IDENTITY_ANSWER_OFF:
		/* (revoked or signed out on the site: signed out here too, so the
		player key never goes in its place; browser.c keeps it on disk) */
		state->off = 1;
		state->signed_out = 1;
		state->has_certificate = 0;
		identity_wipe(state->token, sizeof(state->token));
		break;
	case IDENTITY_ANSWER_NEW_KEY:
		/* (the caller makes the new key; enrolled with it right away) */
		state->new_key_made = 1;
		state->has_certificate = 0;
		state->must_enroll = 1;
		identity_wipe(state->token, sizeof(state->token));
		break;
	case IDENTITY_ANSWER_DAY:
		if (++state->day_tries >= IDENTITY_DAY_TRIES)
			state->off = 1;
		state->wait_until = now + IDENTITY_DAY;
		break;
	case IDENTITY_ANSWER_UNKNOWN_COPY:
		/* (not counted: the copy becomes known with its first game) */
		state->waiting_for_game = 1;
		state->wait_until = now + IDENTITY_LATER;
		break;
	case IDENTITY_ANSWER_LATER:
		/* (an older site: no tokens to it either) */
		state->wait_until = now + IDENTITY_LATER;
		state->tokens_off_until = now + IDENTITY_LATER;
		break;
	case IDENTITY_ANSWER_BUSY:
		state->wait_until = now + growing(IDENTITY_BUSY_FIRST, &state->busy_failures);
		break;
	default:
		state->wait_until = now + growing(IDENTITY_RETRY_FIRST, &state->failures);
		break;
	}
	return answer;
}

int identity_token(const struct identity_state *state, unsigned long now, char *token, int size)
{
	if (state->off || !state->has_certificate || !state->token[0] || now + 60 >= state->token_until ||
		now < state->tokens_off_until || (int)strlen(state->token) >= size)
	{
		return 0;
	}
	strcpy(token, state->token);
	return 1;
}

int identity_token_due(const struct identity_state *state, unsigned long now)
{
	return !state->off && state->has_certificate && !state->must_enroll && now >= state->wait_until &&
		now >= state->tokens_off_until && (!state->token[0] || now + IDENTITY_TOKEN_MARGIN >= state->token_until);
}

void identity_set_token(struct identity_state *state, const char *token, unsigned long lifetime, unsigned long now)
{
	snprintf(state->token, sizeof(state->token), "%s", token);
	state->token_until = now + lifetime;
}

int identity_token_sent(struct identity_state *state, int status, const char *response, unsigned long now)
{
	char reason[16];

	(void)now;
	if (status == 401 && identity_json_member(response, "token", reason, sizeof(reason)) > 0)
	{
		/* (a site restart ends every token) */
		identity_wipe(state->token, sizeof(state->token));
		return strcmp(reason, "unknown") ? IDENTITY_SENT_NEW_TOKEN : IDENTITY_SENT_UNKNOWN_TOKEN;
	}
	/* (anything else the site answered stands, a 400 too) */
	return IDENTITY_SENT_DONE;
}

int identity_account_slot(const char *text, const char *profile)
{
	size_t length = profile ? strlen(profile) : 0;

	while (text && *text && length)
	{
		const char *end = strchr(text, '\n');
		size_t line = end ? (size_t)(end - text) : strlen(text);

		/* ("name n": the name up to the last space, n 1 to 15) */
		if (line > length + 1 && !strncmp(text, profile, length) && text[length] == ' ')
		{
			int slot = 0;
			size_t at;

			for (at = length + 1; at < line && text[at] >= '0' && text[at] <= '9' && slot < 100; at++)
				slot = slot * 10 + (text[at] - '0');
			while (at < line && (text[at] == '\r' || text[at] == ' '))
				at++;
			if (at == line && slot >= 1 && slot <= 15)
				return slot;
		}
		text = end ? end + 1 : NULL;
	}
	return 0;
}

void identity_slot_files(int slot, char *key_name, char *cert_name, int size)
{
	if (slot <= 0)
	{
		snprintf(key_name, (size_t)size, "delta_device.key");
		snprintf(cert_name, (size_t)size, "delta_device.cert");
	}
	else
	{
		snprintf(key_name, (size_t)size, "delta_device_%d.key", slot);
		snprintf(cert_name, (size_t)size, "delta_device_%d.cert", slot);
	}
}

int identity_key_allowed(const struct identity_state *state, unsigned long now)
{
	/* (a site that answered 404 for identity: it knows only the key) */
	if (now < state->tokens_off_until)
		return 1;
	return !state->has_certificate && !state->signed_out;
}

void identity_confirmed(struct identity_state *state, unsigned long now)
{
	if (state->waiting_for_game)
	{
		state->waiting_for_game = 0;
		/* (soon: the site records the game first) */
		if (state->wait_until > now + IDENTITY_RETRY_FIRST)
			state->wait_until = now + IDENTITY_RETRY_FIRST;
	}
}

#endif
