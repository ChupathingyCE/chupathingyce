/*
MAPS_KEY.H

The public keys Chupathingy maps' signed documents are accepted from (the
index and the mirror list of maps.chupathingy.org: map_index.c, map_repo.c):
Ed25519, 32 bytes each. A key set of their own, apart from Delta's
(delta_key.h), so a leak of one never reaches the other.

The primary key signs every document; the recovery key, kept apart, is for a
lost or leaked primary. Epochs as for Delta's legacy table: a serial's top
byte is its epoch, the primary key signs epoch 0 and the recovery key every
one, so a leaked primary is followed by a recovery-signed document of epoch 1
that it can never follow.

The keys were made inside GitHub Actions by ChupathingyCE's command
repository (keys.yml, set maps) on 2026-11-10; their secret halves live only
in its environment secrets MAP_INDEX_SIGNING_KEY (environment signing) and
MAP_INDEX_RECOVERY_KEY (environment signing-recovery), and the public halves
below are keys/maps.pub.json's there. The index workflow signs with them.

A build whose keys are all zero (none now; a fork may blank them) has no
key: it never asks Chupathingy maps for anything (no index, no mirror list,
no download from it), and the community map repositories are asked as
before.
*/

#ifndef MAPS_KEY_H
#define MAPS_KEY_H

#ifndef HALO_MAPS_TEST_KEY
static const unsigned char maps_public_keys[][32] =
{
	/* primary (keys/maps.pub.json: cf69b2e5ac2fea2d2418013bb35d1c04ee0352bc6bcc348e567e3592823246ad) */
	{
		0xcf, 0x69, 0xb2, 0xe5, 0xac, 0x2f, 0xea, 0x2d, 0x24, 0x18, 0x01, 0x3b, 0xb3, 0x5d, 0x1c, 0x04,
		0xee, 0x03, 0x52, 0xbc, 0x6b, 0xcc, 0x34, 0x8e, 0x56, 0x7e, 0x35, 0x92, 0x82, 0x32, 0x46, 0xad,
	},
	/* recovery (keys/maps.pub.json: 5ab67b8db3b60211b29fac31408bc08cf924dd4e95ab2be78d308a4aeae07d05) */
	{
		0x5a, 0xb6, 0x7b, 0x8d, 0xb3, 0xb6, 0x02, 0x11, 0xb2, 0x9f, 0xac, 0x31, 0x40, 0x8b, 0xc0, 0x8c,
		0xf9, 0x24, 0xdd, 0x4e, 0x95, 0xab, 0x2b, 0xe7, 0x8d, 0x30, 0x8a, 0x4a, 0xea, 0xe0, 0x7d, 0x05,
	},
};
#else
/* (a test's keys, given by the test's build: HALO_MAPS_TEST_KEY is its
primary's bytes, HALO_MAPS_TEST_RECOVERY_KEY its recovery key's) */
static const unsigned char maps_public_keys[][32] = { { HALO_MAPS_TEST_KEY }, { HALO_MAPS_TEST_RECOVERY_KEY } };
#endif
/* each key's last epoch, in the keys' order */
static const unsigned char maps_key_last_epochs[] = { 0, 255 };
_Static_assert(sizeof(maps_key_last_epochs) == sizeof(maps_public_keys) / sizeof(*maps_public_keys),
	"a last epoch for each key");

#define MAPS_KEY_COUNT ((int)(sizeof(maps_public_keys) / sizeof(*maps_public_keys)))

#endif
