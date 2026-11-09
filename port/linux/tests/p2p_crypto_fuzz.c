/*
P2P_CRYPTO_FUZZ.C

libFuzzer's entry for what internet play opens from the network
(port/linux/src/p2p_crypto.c; tools/test_host_fuzz.py builds it): sealed
packets (both the random-nonce form and the tunnel's, with additional data),
a sealed token, a signature and a key, and an invite's text (as it arrives
from a link, the clipboard or Discord: ended, as the game's strings are).
Each in an allocation of exactly its size.
*/

#include "../src/p2p_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void posix_random_bytes(void *buffer, unsigned long size)
{
	memset(buffer, 0x5A, size);
}

void platform_log(const char *format, ...)
{
	(void)format;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static const unsigned char key[32] = { 1, 2, 3 };
	unsigned char plain[2048], token[P2P_TOKEN_SIZE], hash[P2P_KEY_HASH_SIZE], check[P2P_INVITE_CHECK_SIZE];
	unsigned char x25519[32];
	unsigned char *bytes;
	char *text;
	int cut;

	if (size < 2)
		return 0;
	cut = data[0] % (int)(size - 1);
	bytes = malloc(size);
	if (!bytes)
		return 0;
	memcpy(bytes, data + 1, size - 1);
	size--;

	/* (the tunnel's packets, and the sealed messages of signalling) */
	if (size <= sizeof(plain) + P2P_SEAL_OVERHEAD)
	{
		p2p_open(key, bytes, (int)size, plain);
		p2p_aead_open(key, bytes, bytes, cut, bytes + cut, (int)(size - (size_t)cut), plain);
	}
	if (size >= 32 + 64)
	{
		/* (a listing's key and signature over the rest) */
		p2p_ed25519_verify(bytes, bytes + 96 > bytes + size ? bytes : bytes + 96, (int)(size - 96), bytes + 32);
		p2p_ed25519_to_x25519(bytes, x25519);
	}
	if (size >= P2P_SEALED_TOKEN_SIZE + 32)
		p2p_unseal_token(key, bytes + P2P_SEALED_TOKEN_SIZE, bytes, token);
	free(bytes);

	/* (invite text: ended, and any bytes) */
	text = malloc(size + 1);
	if (!text)
		return 0;
	memcpy(text, data + 1, size);
	text[size] = 0;
	p2p_parse_invite(text, hash, token, check);
	p2p_parse_invite(text, hash, token, NULL);
	free(text);
	return 0;
}
