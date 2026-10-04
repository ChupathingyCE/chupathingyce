/*
UPDATE_SIGNATURE.C

The self-updater's check of a release's Ed25519 signature
(update_signature.h), with Monocypher (port/third_party/monocypher) and the
build's keys (update_key.h).
*/

#include "update_signature.h"
#include "update_key.h"

#include "monocypher.h"
#include "monocypher-ed25519.h"

#include <stdio.h>
#include <string.h>

#define UPDATE_SIGNATURE_SIZE 64
#define UPDATE_KEY_COUNT (sizeof(update_public_keys) / sizeof(*update_public_keys))

static int update_key_set(const unsigned char *key)
{
	unsigned char any = 0;
	int index;

	for (index = 0; index < 32; index++)
		any |= key[index];
	return any != 0;
}

int update_signature_required(void)
{
	size_t index;

	for (index = 0; index < UPDATE_KEY_COUNT; index++)
	{
		if (update_key_set(update_public_keys[index]))
			return 1;
	}
	return 0;
}

/* (a release's asset name or version: letters, digits and . _ + -, as the
release workflow names them; nothing that would change the text's lines) */
static int update_signature_word(const char *text)
{
	size_t length = 0;

	for (; text[length]; length++)
	{
		char c = text[length];

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
			c == '+' || c == '-'))
		{
			return 0;
		}
	}
	return length > 0 && length < 128;
}

int update_signature_message(char *message, size_t message_size, const char *asset, const char *version,
	const unsigned char *zip, size_t zip_size)
{
	static const char digits[] = "0123456789abcdef";
	unsigned char digest[64];
	char hex[sizeof(digest) * 2 + 1];
	size_t index;
	int length;

	if (!update_signature_word(asset) || !update_signature_word(version))
		return 0;
	crypto_sha512(digest, zip, zip_size);
	for (index = 0; index < sizeof(digest); index++)
	{
		hex[index * 2] = digits[digest[index] >> 4];
		hex[index * 2 + 1] = digits[digest[index] & 15];
	}
	hex[sizeof(hex) - 1] = 0;
	length = snprintf(message, message_size,
		"chupathingyce-update-signature 1\nasset %s\nversion %s\nsize %llu\nsha512 %s\n",
		asset, version, (unsigned long long)zip_size, hex);
	return length > 0 && (size_t)length < message_size ? length : 0;
}

static int update_hex_digit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* the signature's 128 hex digits, with white space around them, into bytes */
static int update_signature_parse(const char *text, size_t length, unsigned char *signature)
{
	size_t start = 0, end = length, index;

	while (start < end && (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' || text[start] == '\n'))
		start++;
	while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' ||
		text[end - 1] == '\n'))
	{
		end--;
	}
	if (end - start != UPDATE_SIGNATURE_SIZE * 2)
		return 0;
	for (index = 0; index < UPDATE_SIGNATURE_SIZE; index++)
	{
		int high = update_hex_digit(text[start + index * 2]);
		int low = update_hex_digit(text[start + index * 2 + 1]);

		if (high < 0 || low < 0)
			return 0;
		signature[index] = (unsigned char)(high << 4 | low);
	}
	return 1;
}

int update_signature_check(const unsigned char *zip, size_t zip_size, const char *asset, const char *version,
	const char *signature_text, size_t signature_length, char *error, size_t error_size)
{
	unsigned char signature[UPDATE_SIGNATURE_SIZE];
	char message[512];
	int message_length;
	size_t index;

	if (!signature_text || !update_signature_parse(signature_text, signature_length, signature))
	{
		snprintf(error, error_size, "its signature is not one");
		return 0;
	}
	message_length = update_signature_message(message, sizeof(message), asset, version, zip, zip_size);
	if (!message_length)
	{
		snprintf(error, error_size, "its name or version is not one");
		return 0;
	}
	for (index = 0; index < UPDATE_KEY_COUNT; index++)
	{
		/* (Monocypher's check turns away an S past the group's order) */
		if (update_key_set(update_public_keys[index]) &&
			crypto_ed25519_check(signature, update_public_keys[index], (const unsigned char *)message,
				(size_t)message_length) == 0)
		{
			return 1;
		}
	}
	snprintf(error, error_size, "its signature does not match ChupathingyCE's release key");
	return 0;
}
