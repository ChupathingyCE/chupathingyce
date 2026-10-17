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

The keys below are the release workflow's: its primary signing key and its
recovery key (the command repository's keys/update.pub.json lists both).
*/

#ifndef UPDATE_KEY_H
#define UPDATE_KEY_H

#ifndef HALO_UPDATE_TEST_KEY
static const unsigned char update_public_keys[][32] =
{
	/* the release key (primary), 2026-10-06 */
	{
		0xc6, 0x64, 0xb3, 0x81, 0xef, 0x69, 0xc5, 0x56, 0xd4, 0x73, 0x15, 0xb8, 0x39, 0x80, 0x3b, 0xe8,
		0x00, 0x39, 0x77, 0x1f, 0xa8, 0xdd, 0x10, 0x51, 0xdd, 0x6f, 0x95, 0x57, 0x08, 0xc1, 0xc6, 0x62,
	},
	/* the recovery key, for a release signed while the primary is lost or rotated */
	{
		0x32, 0x2c, 0x59, 0x09, 0xe9, 0x77, 0x2f, 0x70, 0x14, 0x8c, 0xf9, 0xac, 0x11, 0x48, 0x83, 0xfc,
		0x2a, 0xd1, 0xa1, 0x50, 0x1c, 0x71, 0xae, 0xd7, 0x10, 0x3e, 0x6e, 0x4b, 0xf7, 0x69, 0xe0, 0x88,
	},
};
#else
/* (a test's key, given by the test's build: HALO_UPDATE_TEST_KEY is its bytes) */
static const unsigned char update_public_keys[][32] = { { HALO_UPDATE_TEST_KEY } };
#endif

#endif
