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

A build whose keys are all zero has no key: it never asks Chupathingy maps
for anything (no index, no mirror list, no download from it), and the
community map repositories are asked as before.

TODO(maps keys): the keys aren't made yet. They will be made inside GitHub
Actions by ChupathingyCE's command repository (keys.yml, set maps), into the
environment secrets MAP_INDEX_SIGNING_KEY (environment signing) and
MAP_INDEX_RECOVERY_KEY (environment signing-recovery), and published as
keys/maps.pub.json in that repository; the index workflow (map-index.yml,
done like playlist-maps.yml) signs with them. Paste the two public halves
below then, as identity_key.h has its keys.
*/

#ifndef MAPS_KEY_H
#define MAPS_KEY_H

#ifndef HALO_MAPS_TEST_KEY
static const unsigned char maps_public_keys[][32] =
{
	/* primary (keys/maps.pub.json: not made yet) */
	{
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	},
	/* recovery (keys/maps.pub.json: not made yet) */
	{
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
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
