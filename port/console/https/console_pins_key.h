/*
CONSOLE_PINS_KEY.H

The public keys a console pin document (console_pins.c; docs/console-https.md,
"Pin updates") is accepted from: Ed25519, 32 bytes each. The document carries
the site's key pins, so that a console learns a new site key without a new
build. These keys sign that document and nothing else: they are not Delta's
table keys (port/linux/src/delta_key.h), nor the update keys.

The primary key signs every document; the recovery key, kept apart and
offline, is for a lost or leaked primary. Epochs work as Delta's table's do:
a serial's top byte is its epoch, and each key signs documents of epochs up
to its own last one (console_pins_last_epochs). The primary's is 0 and the
recovery key's 255, so only the recovery key opens a new epoch, and a leaked
primary can never sign past it.

A build whose keys are all zero has none: it takes no pin document, and
keeps to its built-in pins and the Let's Encrypt roots (console_https_trust.c).
The keys below are zero until the pair is made (docs/console-https.md, "Making
the pin keys"): tools/console_pins.py keygen prints each public half as the C
array to paste here.
*/

#ifndef __CONSOLE_PINS_KEY_H
#define __CONSOLE_PINS_KEY_H

#ifndef CONSOLE_PINS_TEST_KEY
static const unsigned char console_pins_keys[][32] =
{
	/* primary */
	{
		0x50, 0x09, 0x8d, 0x7a, 0x36, 0xcc, 0xbe, 0xb9, 0x7c, 0x0f, 0xab, 0x21, 0x85, 0x55, 0x94, 0x1b,
		0x07, 0x5e, 0xc1, 0x3e, 0x25, 0x52, 0xf7, 0x40, 0x6c, 0xfc, 0x09, 0xce, 0x3e, 0xf1, 0x5e, 0x2a,
	},
	/* recovery (kept offline) */
	{
		0x2f, 0xf4, 0xe2, 0x26, 0xbe, 0x76, 0xe3, 0x08, 0x90, 0x6c, 0x28, 0x8a, 0x14, 0x50, 0x1a, 0x8a,
		0x9d, 0x04, 0x69, 0x85, 0x28, 0xb3, 0x0f, 0x06, 0xa5, 0x6e, 0xd4, 0x21, 0x48, 0xcb, 0xb4, 0xd1,
	},
};
#else
/* (a test's keys, given by its build: CONSOLE_PINS_TEST_KEY is the primary's
bytes, CONSOLE_PINS_TEST_RECOVERY_KEY the recovery key's) */
static const unsigned char console_pins_keys[][32] = { { CONSOLE_PINS_TEST_KEY }, { CONSOLE_PINS_TEST_RECOVERY_KEY } };
#endif
/* each key's last epoch, in the keys' order */
static const unsigned char console_pins_last_epochs[] = { 0, 255 };

#endif
