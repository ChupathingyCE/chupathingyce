/*
DELTA_KEY.H

The public keys a signed legacy table (delta.c; docs/delta.md, "The legacy
table as config") is accepted from: Ed25519, 32 bytes each. CI signs each
table it publishes (tools/delta_table.py sign) with the matching private key,
which lives only in CI's secrets, never in this repository. The keys are
public: anyone building on Delta can check a table with them. They are
published, with the secrets that hold their private halves, as
keys/delta.pub.json in ChupathingyCE's command repository.

The primary key signs every table; the recovery key, kept apart, is for a
lost or leaked primary: tables signed with it are accepted the same, so a
new primary can be rolled out without a build that cannot take a table.

A build whose keys are all zero has no key: it accepts no signed table, so it
plays with the numbers it was built with (and a local override,
network.legacy_table), and does not fetch tables at all.

To rotate: add the new key beside the old one, release, sign with the new key
once the builds in use have it, and drop the old key a few releases later.
*/

#ifndef DELTA_KEY_H
#define DELTA_KEY_H

#ifndef HALO_DELTA_TEST_KEY
static const unsigned char delta_public_keys[][32] =
{
	/* primary (d51bd85346bf889b...) */
	{
		0xd5, 0x1b, 0xd8, 0x53, 0x46, 0xbf, 0x88, 0x9b, 0x6b, 0xc5, 0xaa, 0x21, 0xdc, 0xc7, 0xa4, 0x34,
		0x88, 0x65, 0x7b, 0x64, 0xf2, 0x43, 0x2e, 0x17, 0xf6, 0xe0, 0xe7, 0xfe, 0x97, 0x69, 0x33, 0xce,
	},
	/* recovery (97cde84d8b9b6ba8...) */
	{
		0x97, 0xcd, 0xe8, 0x4d, 0x8b, 0x9b, 0x6b, 0xa8, 0xa2, 0x75, 0xd4, 0x12, 0xb4, 0x94, 0x22, 0xd4,
		0xc0, 0x79, 0x70, 0x65, 0x3e, 0xe5, 0x3a, 0xe5, 0x42, 0x85, 0xe9, 0x66, 0x40, 0xa5, 0xe4, 0x56,
	},
};
#else
/* (a test's key, given by the test's build: HALO_DELTA_TEST_KEY is its bytes) */
static const unsigned char delta_public_keys[][32] = { { HALO_DELTA_TEST_KEY } };
#endif

#endif
