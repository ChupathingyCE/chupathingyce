/*
CONSOLE_PINS.H

The console pin document (docs/console-https.md, "Pin updates"): the site's
key pins, signed with the console pin key (console_pins_key.h), so that a
console takes a new site key from it without a new build. console_https.c
fetches it when the site's key is not one it pins (and at most once a day
otherwise), over plain HTTP from warthog.milenko.org or over HTTPS from
GitHub's raw files, and keeps the newest one beside config.toml.

The document, console-pins.json (at most CONSOLE_PINS_DOCUMENT_SIZE bytes):

    {"console_pins": 1, "serial": 3, "epoch": 0, "issued": 1793232000,
     "hosts": {"stats.chupathingy.org": {"current": "<64 hex digits>",
                                         "next": "<64 hex digits>"}}}

Each pin is the SHA-256 of a certificate's SubjectPublicKeyInfo. "next" is
optional; hosts other than the site are read past. Its signature,
console-pins.json.sig: the Ed25519 signature of the document's exact bytes,
as 128 hex digits and a line feed. Everything from outside is hostile: the
size is checked first, then the signature, then the serial's epoch, and
only then is the document read.

tools/console_pins.py makes, signs and checks it.
*/

#ifndef __CONSOLE_PINS_H
#define __CONSOLE_PINS_H

#define CONSOLE_PINS_FORMAT 1
#define CONSOLE_PINS_DOCUMENT_SIZE 2048
#define CONSOLE_PINS_SIGNATURE_SIZE 64
/* the signed form kept on the console: the signature's hex digits, a line
feed, the document */
#define CONSOLE_PINS_SIGNED_SIZE (CONSOLE_PINS_SIGNATURE_SIZE * 2 + 1 + CONSOLE_PINS_DOCUMENT_SIZE)
/* the site's pins a document gives, at most */
#define CONSOLE_PINS_MAXIMUM 4
#define CONSOLE_PINS_PIN_SIZE 32

struct console_pins
{
	unsigned long serial;
	int count;
	unsigned char pins[CONSOLE_PINS_MAXIMUM][CONSOLE_PINS_PIN_SIZE];
};

/* whether a pin key is built in (none: no document is ever taken) */
int console_pins_have_key(void);

/* document (size bytes) and its signature's hex digits (white space after
them allowed): checked, and the site's pins read into pins (host is the
site's name). 1, or 0 and why */
int console_pins_check(const char *host, const char *document, unsigned long size, const char *signature,
	unsigned long signature_length, struct console_pins *pins, const char **why);

/* the same, of the signed form */
int console_pins_check_signed(const char *host, const char *signed_form, unsigned long size,
	struct console_pins *pins, const char **why);

#endif
