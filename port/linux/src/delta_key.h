/*
DELTA_KEY.H

The public keys a signed legacy table (delta.c; docs/delta.md, "The legacy
table as config") is accepted from: Ed25519, 32 bytes each. CI signs each
table it publishes (tools/delta_table.py sign) with the matching private key,
which lives only in CI's secrets, never in this repository. The keys are
public: anyone building on Delta can check a table with them.

A build whose keys are all zero has no key: it accepts no signed table, so it
plays with the numbers it was built with (and a local override,
network.legacy_table), and does not fetch tables at all.

To rotate: add the new key beside the old one, release, sign with the new key
once the builds in use have it, and drop the old key a few releases later.

The key below is a PLACEHOLDER (all zero): the owner makes the real key pair
(tools/delta_table.py keygen), stores the private key as a CI secret, and
puts the public half printed by keygen here.
*/

#ifndef DELTA_KEY_H
#define DELTA_KEY_H

#ifndef HALO_DELTA_TEST_KEY
static const unsigned char delta_public_keys[][32] =
{
	/* PLACEHOLDER: the legacy table signing key's public half goes here */
	{
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	},
};
#else
/* (a test's key, given by the test's build: HALO_DELTA_TEST_KEY is its bytes) */
static const unsigned char delta_public_keys[][32] = { { HALO_DELTA_TEST_KEY } };
#endif

#endif
