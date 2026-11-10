/*
IDENTITY_KEY.H

The identity root keys (docs/delta.md, "Device identity"): Ed25519, 32 bytes
each. They sign only the site's issuer certificate (CISS), and the issuer
signs the device certificates (identity.c checks the chain). The private
halves live only in CI's secrets (command's keys.yml, set identity), never
in this repository; the public halves are published as
keys/identity.pub.json in ChupathingyCE's command repository, and anyone
building on Delta can check a certificate with them.

The primary key signs every issuer certificate; the recovery key, kept apart,
is for a lost or leaked primary, and is trusted the same.

(While both were all zero, before the keys were made, a copy took its own
certificate on the issuer the site named; with the keys here, the issuer
certificate must chain to one of them.)
*/

#ifndef IDENTITY_KEY_H
#define IDENTITY_KEY_H

#ifndef HALO_IDENTITY_TEST_KEY
static const unsigned char identity_root_keys[][32] =
{
	/* primary (keys/identity.pub.json, 2026-11-09) */
	{
		0x9c, 0x1c, 0x44, 0x54, 0x40, 0x0f, 0xc0, 0x13, 0x86, 0x30, 0xd6, 0x3b, 0x8e, 0x2b, 0xbd, 0xeb,
		0x0f, 0x31, 0xc9, 0x94, 0x09, 0x76, 0x3b, 0xec, 0xd0, 0x76, 0xb7, 0x0f, 0x31, 0x89, 0x46, 0x8e,
	},
	/* recovery (keys/identity.pub.json, 2026-11-09) */
	{
		0xa3, 0x12, 0x45, 0x02, 0x70, 0x2c, 0xd3, 0xb2, 0xf6, 0x10, 0x15, 0x5c, 0x28, 0xc6, 0x22, 0xcb,
		0xab, 0x72, 0xc6, 0xf0, 0xdf, 0x48, 0x3b, 0x33, 0x94, 0x39, 0xa1, 0x49, 0x64, 0xf1, 0xd2, 0x9f,
	},
};
#else
/* (a test's key, given by the test's build: HALO_IDENTITY_TEST_KEY is its
bytes) */
static const unsigned char identity_root_keys[][32] = { { HALO_IDENTITY_TEST_KEY } };
#endif

#define IDENTITY_ROOT_KEY_COUNT ((int)(sizeof(identity_root_keys) / sizeof(*identity_root_keys)))

#endif
