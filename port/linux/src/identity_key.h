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

While both are all zero (the keys aren't made yet), this copy takes its own
certificate when it is well formed, signed by the issuer the site named,
names this device's key and is in date: nothing else trusts it until the
keys are here.
*/

#ifndef IDENTITY_KEY_H
#define IDENTITY_KEY_H

#ifndef HALO_IDENTITY_TEST_KEY
static const unsigned char identity_root_keys[][32] =
{
	/* primary (not made yet) */
	{
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	},
	/* recovery (not made yet) */
	{
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	},
};
#else
/* (a test's key, given by the test's build: HALO_IDENTITY_TEST_KEY is its
bytes) */
static const unsigned char identity_root_keys[][32] = { { HALO_IDENTITY_TEST_KEY } };
#endif

#define IDENTITY_ROOT_KEY_COUNT ((int)(sizeof(identity_root_keys) / sizeof(*identity_root_keys)))

#endif
