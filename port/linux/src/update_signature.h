/*
UPDATE_SIGNATURE.H

The self-updater's check of a release's signature (update_signature.c): the
release workflow signs each update zip (tools/update_sign.py) with a key
whose public half the game is built with (update_key.h), and the updater
(updater.c) installs a download only when its signature checks.

What is signed is a short text naming the zip, the release's version and the
zip's size and SHA-512, so a signature fits only that zip, as that version,
for that platform:

	chupathingyce-update-signature 1
	asset chupathingyce-linux-release.zip
	version 0.6.3b
	size 32373110
	sha512 <the zip's SHA-512, 128 lowercase hex digits>

each line ending in a line feed. The signature (<zip>.sig, beside the zip in
the release) is the Ed25519 signature of that text, as 128 hex digits.

Only bytes, sizes and strings cross here.
*/

#ifndef UPDATE_SIGNATURE_H
#define UPDATE_SIGNATURE_H

#include <stddef.h>

/* whether this build has a key to check updates with (one not all zero) */
int update_signature_required(void);

/* the text signed for a zip (above) into message; its length, or 0 if it
does not fit or the asset or version is not one */
int update_signature_message(char *message, size_t message_size, const char *asset, const char *version,
	const unsigned char *zip, size_t zip_size);

/* whether signature_text (the .sig's contents, signature_length bytes) is a
signature, by one of the build's keys, of the zip's text (above); if not,
why in error */
int update_signature_check(const unsigned char *zip, size_t zip_size, const char *asset, const char *version,
	const char *signature_text, size_t signature_length, char *error, size_t error_size);

#endif
