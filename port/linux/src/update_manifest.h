/*
UPDATE_MANIFEST.H

A release's list of its update files (updates.json, an asset of the release
like its zips), which the self-updater (updater.c) reads first: for each
platform and configuration, which file to download, from which release, and
its size and SHA-256. The release workflow writes it and signs it
(updates.json.sig: the Ed25519 signature of its exact bytes, by the release
key; update_signature.h), so its one signature covers every file it names:

	{
	  "format": "chupathingyce-updates 1",
	  "version": "0.8.0",
	  "commit": "<the commit the release was built from>",
	  "files": [
	    {"platform": "linux64", "configuration": "release", "kind": "zip",
	     "tag": "v0.8.0", "name": "chupathingyce-linux64-release.zip",
	     "size": 32373110, "sha256": "<64 lowercase hex digits>",
	     "main": "chupathingy"},
	    ...
	  ]
	}

"main", which a list may leave out, is the zip's file to start once it is
installed (the game's executable, by the name the release gives it); without
one, the updater starts the file it was started from, as before.

"tag" is the release the file is on (the debug builds are on a pre-release
of their own, v<version>-debug). "kind" is what the file is: "zip" (the
files of the game, which the desktop updater installs) or one a later build
installs (a Linux AppImage, "appimage"). A build passes over the kinds and
the fields it does not know, so a later release can add to the list without
breaking the builds before it.

A release from before the list has none: the updater then downloads
chupathingyce-<platform>-<configuration>.zip and its own signature
(<zip>.sig), as it did before.

The update channels (update.channel: release, testing or nightly). From
0.9.1 a list also says whose it is, "channel": "release", "testing" or
"nightly" (a list from before has none, and is a release's). A release's
list is found as above (the latest release, v<version>/updates.json). The
testing and nightly channels each have one list at a fixed address, their
feed (the "testing" and "nightly" pre-releases' updates.json), whose version
is <version>-testing.<serial> or <version>-nightly.<serial>, the serial being
the UTC time the build was made, exactly 12 digits (YYYYMMDDHHMM, 2026 to
2099), and whose files are on the test build's own pre-release
(v<version>-testing, or v<version>-testing<digits>, with or without -debug)
or the nightly's ("nightly"). One grammar, the same in the command
repository's tools/signing.py, UpdateManifest.java and build.gradle:

	^v?(\d{1,9})\.(\d{1,9})\.(\d{1,9})([a-z]{0,15})(?:-(testing|nightly)\.(\d{12}))?$ A feed's list is
signed by the prerelease key or a release key (update_signature.h), and is
used only when its channel and its version's label are the feed's: a release
is never offered from a feed.

Versions are in this order (update_version_compare): by their numbers, then
their hotfix letters (none first, then a, b), then a release before none of
its test builds and nightlies, which are in the order they were built (by
the serial, whichever channel they are on):

	0.9.0d < 0.9.1-nightly.202611100400 < 0.9.1-testing.202611101200
	       < 0.9.1-nightly.202611110400 < 0.9.1 < 0.9.2-testing.202611200900

Only bytes, sizes and strings cross here.
*/

#ifndef UPDATE_MANIFEST_H
#define UPDATE_MANIFEST_H

#include <stddef.h>

/* a SHA-256 as hex digits, with its terminator */
#define UPDATE_SHA256_TEXT_SIZE 65

enum
{
	/* the list is not one, or not the release's: nothing of it is used */
	_update_manifest_refused = -1,
	/* the list has no file of that kind for the platform and configuration */
	_update_manifest_missing = 0,
	_update_manifest_found = 1,
};

/* in the list (text, length bytes; its signature checked first) of the
release of version, the file of kind for platform and configuration: its
release's tag, its name, size and SHA-256 (lowercase hex) into tag, name,
size and sha256, and its "main" into main_name (empty without one); one of
the above, and why in error when it is not found */
int update_manifest_find(const char *text, size_t length, const char *version, const char *platform,
	const char *configuration, const char *kind, char *tag, size_t tag_size, char *name, size_t name_size,
	unsigned long long *size, char *sha256, size_t sha256_size, char *main_name, size_t main_size, char *error,
	size_t error_size);

/* in a channel's feed list (testing or nightly; its signature checked
first): its version, when the list is that channel's and its version a build
of that channel's (0.9.1-testing.202611101530); one of the above, and why in
error when it is not */
int update_manifest_about(const char *text, size_t length, const char *channel, char *version, size_t version_size,
	char *error, size_t error_size);

/* as update_manifest_find, in a channel's feed list (update_manifest_about's
checks, and its version version): the file's tag is then the channel's
pre-release (testing: v<version>-testing..., nightly: nightly...) */
int update_manifest_find_channel(const char *text, size_t length, const char *channel, const char *version,
	const char *platform, const char *configuration, const char *kind, char *tag, size_t tag_size, char *name,
	size_t name_size, unsigned long long *size, char *sha256, size_t sha256_size, char *main_name, size_t main_size,
	char *error, size_t error_size);

/* the order of two versions (above): 1 when a is newer than b, -1 when it is
older, 0 when they are the same; *comparable 0 (and 0) when either is not a
version of that shape */
int update_version_compare(const char *a, const char *b, int *comparable);

/* whether version is newer than current (both of the shape above) */
int update_version_newer(const char *version, const char *current);

/* whether version can be a release's: not a test build's or a nightly's (no
-testing or -nightly in it at all, of the shape above or not). The release
path (the latest release, its list, its zip's .sig) takes only these */
int update_version_release(const char *version);

/* whether version is a test build's or a nightly's: its label (testing,
nightly) into label */
int update_version_prerelease(const char *version, char *label, size_t label_size);

/* the SHA-256 of size bytes of data, as 64 lowercase hex digits, into hex */
void update_sha256(const unsigned char *data, size_t size, char hex[UPDATE_SHA256_TEXT_SIZE]);

#endif
