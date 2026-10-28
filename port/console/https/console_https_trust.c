/*
CONSOLE_HTTPS_TRUST.C

The hosts the consoles ask over HTTPS (console_https.h) and what each one's
certificate must show. docs/console-https.md, "Rotating keys", says how to
change them without leaving a console behind: a new key is pinned here,
and released, before the site starts using it.

stats.chupathingy.org: its key, pinned (the SHA-256 of the certificate's
SubjectPublicKeyInfo). Two keys are taken: the one the site uses now, and
a backup the site has not used yet, kept offline, for the day the first one
has to go. The site's certificate must renew with the same key (certbot's
reuse_key), so that a renewal changes nothing here.

When the site's key is not one of these, the console asks for the signed pin
document (console_pins.h) and tries again with its pins; when that brings
none, a chain to Let's Encrypt's ISRG Root X1 or X2 naming the host is taken
as a backstop (dates unchecked), so that a console that missed an update is
never stranded. Pins win: the backstop is asked only when no pin matches.

raw.githubusercontent.com: GitHub's, whose keys are not ours to pin: its
chain must lead to one of console_https_roots.c's roots (Let's Encrypt's
ISRG roots, which it uses now, and the Sectigo and DigiCert roots GitHub
has used before).
*/

#include "console_https.h"
#include "console_https_trust.h"

#include <stddef.h>

static const unsigned char site_pins[][CONSOLE_HTTPS_PIN_SIZE] =
{
	/* the site's key in use (ECDSA P-256), since 2026-10-13 */
	{
		0x9a, 0xb7, 0x24, 0xe9, 0x12, 0x79, 0xa8, 0x49,
		0xc7, 0x2f, 0x87, 0xe1, 0xab, 0x61, 0xf2, 0x6f,
		0x98, 0x70, 0xc5, 0xcd, 0xa3, 0x22, 0x07, 0x98,
		0xa7, 0x32, 0xc2, 0x2b, 0x90, 0xa9, 0xa5, 0x0f
	},
	/* the backup key (ECDSA P-256), not in use yet */
	{
		0x08, 0x26, 0xe1, 0x70, 0x92, 0xc5, 0x7c, 0x72,
		0x54, 0xae, 0xdf, 0x17, 0x86, 0xed, 0xba, 0x19,
		0xe5, 0x43, 0x30, 0x9e, 0x12, 0xee, 0x47, 0xb7,
		0x3c, 0xe0, 0x59, 0x24, 0x67, 0xcc, 0xc7, 0x5a
	},
};

const struct console_https_trust console_https_hosts[] =
{
	/* (its backstop: console_https_roots.c's first two, ISRG Root X1 and X2) */
	{ CONSOLE_HTTPS_SITE, site_pins, sizeof(site_pins) / sizeof(*site_pins), console_https_roots, 2 },
	/* (roots NULL: all of console_https_roots.c's) */
	{ CONSOLE_HTTPS_GITHUB, NULL, 0, NULL, 0 },
};
const int console_https_host_count = sizeof(console_https_hosts) / sizeof(*console_https_hosts);
