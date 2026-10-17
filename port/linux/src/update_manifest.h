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

/* the SHA-256 of size bytes of data, as 64 lowercase hex digits, into hex */
void update_sha256(const unsigned char *data, size_t size, char hex[UPDATE_SHA256_TEXT_SIZE]);

#endif
