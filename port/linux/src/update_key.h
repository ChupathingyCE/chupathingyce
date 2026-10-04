/*
UPDATE_KEY.H

The public keys the self-updater (updater.c, update_signature.c) accepts a
release's signature from: Ed25519, 32 bytes each. The release workflow signs
every update zip with the matching private key (tools/update_sign.py), which
lives only in the release workflow's secrets, never in this repository.

A build whose keys are all zero has no key: it installs updates unsigned, as
builds did before signatures (and says so in its log). Once a key is set
here, the builds from then on install only signed updates, so the releases
from then on must be signed with it.

To rotate: add the new key beside the old one, release, sign with the new key
from the next release on, and drop the old key a few releases later.

The key below is a PLACEHOLDER (all zero): the owner replaces it with the
release key's public half (tools/update_sign.py public <key.pem> prints it).
*/

#ifndef UPDATE_KEY_H
#define UPDATE_KEY_H

#ifndef HALO_UPDATE_TEST_KEY
static const unsigned char update_public_keys[][32] =
{
	/* PLACEHOLDER: the release signing key's public half goes here */
	{
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	},
};
#else
/* (a test's key, given by the test's build: HALO_UPDATE_TEST_KEY is its bytes) */
static const unsigned char update_public_keys[][32] = { { HALO_UPDATE_TEST_KEY } };
#endif

#endif
