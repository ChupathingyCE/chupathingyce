/*
CONTENT_SHA256.H

The game's one SHA-256 (FIPS 180-4): map identity and content checks, Delta's
device IDs, internet play's keys, playlist and update manifests. Streaming, or
whole with content_sha256. content_sha256.c.
*/

#ifndef CONTENT_SHA256_H
#define CONTENT_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define CONTENT_SHA256_SIZE 32

struct content_sha256
{
	uint32_t state[8];
	unsigned char block[64];
	uint64_t length;
	int used;
};

void content_sha256_begin(struct content_sha256 *context);
void content_sha256_add(struct content_sha256 *context, const void *data, size_t size);
void content_sha256_end(struct content_sha256 *context, unsigned char digest[CONTENT_SHA256_SIZE]);
/* data's digest, whole */
void content_sha256(const void *data, size_t size, unsigned char digest[CONTENT_SHA256_SIZE]);
/* which code hashes: the processor's SHA-256 instructions or the portable code
(MCC_SHA256_PORTABLE set in the environment: the portable code) */
const char *content_sha256_implementation(void);

#endif
