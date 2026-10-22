/*
SERVER_BADGE.C

Server badges' certificates and proofs (server_badge.h;
docs/server-badges.md).
*/

#include "server_badge.h"
#include "server_badge_key.h"

#include "monocypher.h"
#include "monocypher-ed25519.h"

#include <string.h>

/* (a certificate's magic, and the labels before what each key signs) */
static const char certificate_label[] = "chupathingy server badge v1\n";
static const char proof_label[] = "chupathingy server badge proof v1\n";
static const char announce_label[] = "chupathingy server badge announce v1\n";

static const char *const tier_names[] = { "", "official", "developer", "community" };
typedef char tier_names_check[sizeof(tier_names) / sizeof(*tier_names) == NUMBER_OF_SERVER_BADGE_TIERS ? 1 : -1];

static unsigned int read_u32(const unsigned char *data)
{
	return (unsigned int)data[0] | (unsigned int)data[1] << 8 | (unsigned int)data[2] << 16 |
		(unsigned int)data[3] << 24;
}

/* whether a signature is key's, of message: never for a key of small order
(anyone can sign for one) or all zeros (no key) */
static int verify(const unsigned char *key, const unsigned char *message, int size, const unsigned char *signature)
{
	static const unsigned char zero[32];
	static const unsigned char scalar[32] = { 1 };
	unsigned char x25519[32];
	unsigned char product[32];

	if (!memcmp(key, zero, sizeof(zero)))
		return 0;
	crypto_eddsa_to_x25519(x25519, key);
	crypto_x25519(product, scalar, x25519);
	if (!crypto_verify32(product, zero))
		return 0;
	return crypto_ed25519_check(signature, key, message, (size_t)size) == 0;
}

int server_badge_parse(const unsigned char *certificate, int size, struct server_badge *badge)
{
	int length, index;

	memset(badge, 0, sizeof(*badge));
	if (!certificate || size < SERVER_BADGE_MINIMUM_SIZE || size > SERVER_BADGE_MAXIMUM_SIZE ||
		memcmp(certificate, "CBDG", 4) || certificate[4] != SERVER_BADGE_FORMAT)
	{
		return 0;
	}
	length = certificate[6];
	if (length > SERVER_BADGE_LABEL_SIZE || size != SERVER_BADGE_MINIMUM_SIZE + length)
		return 0;
	for (index = 0; index < length; index++)
	{
		unsigned char character = certificate[SERVER_BADGE_HEADER_SIZE + index];

		if (character < 0x20 || character > 0x7E)
			return 0;
		badge->label[index] = (char)character;
	}
	badge->tier = certificate[5];
	badge->issued = read_u32(certificate + 8);
	badge->expires = read_u32(certificate + 12);
	if (badge->expires <= badge->issued)
		return 0;
	memcpy(badge->server_key, certificate + 16, SERVER_BADGE_KEY_SIZE);
	return 1;
}

int server_badge_signed(const unsigned char *certificate, int size, const unsigned char (*keys)[32], int count)
{
	unsigned char message[sizeof(certificate_label) - 1 + SERVER_BADGE_MAXIMUM_SIZE];
	int body, index;

	if (!certificate || size < SERVER_BADGE_MINIMUM_SIZE || size > SERVER_BADGE_MAXIMUM_SIZE)
		return 0;
	body = size - SERVER_BADGE_SIGNATURE_SIZE;
	memcpy(message, certificate_label, sizeof(certificate_label) - 1);
	memcpy(message + sizeof(certificate_label) - 1, certificate, (size_t)body);
	for (index = 0; index < count; index++)
	{
		if (verify(keys[index], message, (int)(sizeof(certificate_label) - 1) + body, certificate + body))
			return 1;
	}
	return 0;
}

int server_badge_has_keys(void)
{
	static const unsigned char zero[32];
	int index;

	for (index = 0; index < (int)(sizeof(server_badge_public_keys) / sizeof(*server_badge_public_keys)); index++)
	{
		if (memcmp(server_badge_public_keys[index], zero, sizeof(zero)))
			return 1;
	}
	return 0;
}

int server_badge_check(const unsigned char *certificate, int size, unsigned long long now, struct server_badge *badge)
{
	if (!server_badge_parse(certificate, size, badge) || !server_badge_tier_name(badge->tier)[0] ||
		!server_badge_signed(certificate, size, server_badge_public_keys,
			(int)(sizeof(server_badge_public_keys) / sizeof(*server_badge_public_keys))))
	{
		return 0;
	}
	/* (a day's leeway for a clock behind on a certificate just issued) */
	if (now && (now >= badge->expires || now + 24 * 60 * 60 < badge->issued))
		return 0;
	return 1;
}

int server_badge_proof_message(unsigned char *message, const unsigned char *nonce)
{
	memcpy(message, proof_label, sizeof(proof_label) - 1);
	memcpy(message + sizeof(proof_label) - 1, nonce, SERVER_BADGE_NONCE_SIZE);
	return (int)(sizeof(proof_label) - 1) + SERVER_BADGE_NONCE_SIZE;
}

int server_badge_announce_message(unsigned char *message, const char *invite, unsigned long long time)
{
	char digits[24];
	int length = 0, count = 0;
	size_t invite_length = strlen(invite);

	/* (the label, the invite, the time in decimal, each line ended) */
	do
	{
		digits[count++] = (char)('0' + time % 10);
		time /= 10;
	} while (time);
	if (sizeof(announce_label) - 1 + invite_length + 1 + (size_t)count + 1 > SERVER_BADGE_MESSAGE_SIZE)
		return 0;
	memcpy(message, announce_label, sizeof(announce_label) - 1);
	length = (int)(sizeof(announce_label) - 1);
	memcpy(message + length, invite, invite_length);
	length += (int)invite_length;
	message[length++] = '\n';
	while (count)
		message[length++] = (unsigned char)digits[--count];
	message[length++] = '\n';
	return length;
}

int server_badge_signature_valid(const struct server_badge *badge, const unsigned char *message, int size,
	const unsigned char *signature)
{
	return verify(badge->server_key, message, size, signature);
}

const char *server_badge_tier_name(int tier)
{
	return tier > 0 && tier < NUMBER_OF_SERVER_BADGE_TIERS ? tier_names[tier] : "";
}

int server_badge_tier_number(const char *name)
{
	int tier;

	for (tier = 1; name && name[0] && tier < NUMBER_OF_SERVER_BADGE_TIERS; tier++)
	{
		if (!strcmp(name, tier_names[tier]))
			return tier;
	}
	return _server_badge_tier_none;
}

static int hex_value(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return -1;
}

int server_badge_from_text(const char *text, unsigned char *certificate, int size)
{
	while (text && *text)
	{
		const char *end = text + strcspn(text, "\r\n");
		const char *cursor = text;
		int length = 0;

		while (cursor < end && (*cursor == ' ' || *cursor == '\t'))
			cursor++;
		if (cursor < end && *cursor != '#')
		{
			while (cursor + 1 < end && hex_value(cursor[0]) >= 0 && hex_value(cursor[1]) >= 0 && length < size)
			{
				certificate[length++] = (unsigned char)(hex_value(cursor[0]) << 4 | hex_value(cursor[1]));
				cursor += 2;
			}
			while (cursor < end && (*cursor == ' ' || *cursor == '\t'))
				cursor++;
			/* (the first line of digits is the certificate: all of it, or none) */
			return cursor == end ? length : 0;
		}
		text = *end ? end + 1 : end;
	}
	return 0;
}
