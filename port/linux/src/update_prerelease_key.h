/*
UPDATE_PRERELEASE_KEY.H

The public keys the self-updater (updater.c, update_signature.c) accepts a
test build's or a nightly's list of update files from (the testing and
nightly channels' feeds: update_manifest.h), Ed25519, 32 bytes each, beside
the release keys (update_key.h), which may sign those too.

These keys are trusted for nothing else: never for a release's list, a
release's zip, or anything on the release channel. So a leaked prerelease
key reaches only the players who chose Testing or Nightly, and never puts a
"release" in front of everyone else. The command repository's
keys/update.pub.json names it as the update set's "prerelease" role (secret
UPDATE_PRERELEASE_KEY, environment signing-prerelease), and its
tools/signing.py check-header --set update --roles prerelease checks this
file (and refuses update_key.h if it ever holds this key).

All zero: no prerelease key yet. The test builds and nightlies are then
signed with the release key, which the feeds accept.
*/

#ifndef UPDATE_PRERELEASE_KEY_H
#define UPDATE_PRERELEASE_KEY_H

#if defined(HALO_PORT_UPDATE_API) && !defined(HALO_UPDATE_TEST_KEY)
/* (a console port's own releases have no channels: no prerelease key) */
static const unsigned char update_prerelease_public_keys[][32] = { { 0 } };
#elif !defined(HALO_UPDATE_TEST_KEY)
static const unsigned char update_prerelease_public_keys[][32] =
{
	/* the prerelease key (not made yet) */
	{
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	},
};
#elif defined(HALO_UPDATE_TEST_PRERELEASE_KEY)
/* (a test's, given by the test's build: HALO_UPDATE_TEST_PRERELEASE_KEY is its bytes) */
static const unsigned char update_prerelease_public_keys[][32] = { { HALO_UPDATE_TEST_PRERELEASE_KEY } };
#else
static const unsigned char update_prerelease_public_keys[][32] = { { 0 } };
#endif

#endif
