/*
SERVER_BADGE_KEY.H

The public keys a server badge's certificate (server_badge.c;
docs/server-badges.md) is accepted from: Ed25519, 32 bytes each. CI signs
each certificate it issues (the command repository's "Issue server badge"
workflow) with the matching private key, which lives only in CI's secrets,
never in this repository. The keys are public: anyone can check a
certificate with them. They are published, with the secrets that hold their
private halves, as keys/badge.pub.json in ChupathingyCE's command
repository.

The primary key signs every certificate; the recovery key, kept apart, is
for a lost or leaked primary: certificates signed with it are accepted the
same, so a new primary can be rolled out without a build that shows no
badge.

A build whose keys are all zero has no key: it accepts no certificate, so it
shows no badge at all (the list's word is not enough without the server's
own proof on joining).

To rotate: add the new key beside the old one, release, sign with the new key
once the builds in use have it, and drop the old key a few releases later.
*/

#ifndef SERVER_BADGE_KEY_H
#define SERVER_BADGE_KEY_H

#ifndef HALO_BADGE_TEST_KEY
static const unsigned char server_badge_public_keys[][32] =
{
	/* primary (none yet: the Keys workflow's set badge) */
	{
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	},
	/* recovery (none yet) */
	{
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	},
};
#else
/* (a test's keys, given by the test's build: HALO_BADGE_TEST_KEY is its
primary's bytes, HALO_BADGE_TEST_RECOVERY_KEY its recovery key's) */
static const unsigned char server_badge_public_keys[][32] = { { HALO_BADGE_TEST_KEY }, { HALO_BADGE_TEST_RECOVERY_KEY } };
#endif

#endif
