/*
NETWORKS_KEY.H

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later

The keys a signed list of networks (networks.c; delta-spec's link.md,
"Networks") is taken from: Ed25519, 32 bytes each, a key set of its own,
used for nothing else: never the signed legacy table's (delta_key.h) nor
the identity root keys (identity_key.h). The private halves live only with
the list's publisher, never in this repository.

The primary key signs every list; the recovery key, kept apart, is for a
lost or leaked primary. Epochs work as the legacy table's (delta_key.h): a
serial's top byte is its epoch, and each key signs lists of epochs up to
its own last one (networks_key_last_epochs): the primary's 0, the
recovery's 255.

While every key is all zero (before the keys are made), a build takes no
signed list and fetches none: it trusts its home network and the local
additions (network.networks_trust) alone.
*/

#ifndef NETWORKS_KEY_H
#define NETWORKS_KEY_H

#ifndef HALO_NETWORKS_TEST_KEY
static const unsigned char networks_list_keys[][32] =
{
	/* primary (not made yet) */
	{ 0 },
	/* recovery (not made yet) */
	{ 0 },
};
static const unsigned char networks_key_last_epochs[] = { 0, 255 };
#else
/* (a test's keys, given by the test's build: HALO_NETWORKS_TEST_KEY is its
primary's bytes, HALO_NETWORKS_TEST_RECOVERY_KEY its recovery key's) */
static const unsigned char networks_list_keys[][32] = { { HALO_NETWORKS_TEST_KEY },
	{ HALO_NETWORKS_TEST_RECOVERY_KEY } };
static const unsigned char networks_key_last_epochs[] = { 0, 255 };
#endif
_Static_assert(sizeof(networks_key_last_epochs) == sizeof(networks_list_keys) / sizeof(*networks_list_keys),
	"a last epoch for each key");

#define NETWORKS_LIST_KEY_COUNT ((int)(sizeof(networks_list_keys) / sizeof(*networks_list_keys)))

#endif
