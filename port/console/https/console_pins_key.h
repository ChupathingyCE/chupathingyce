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
	/* primary: not made yet */
	{
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	},
	/* recovery: not made yet */
	{
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
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
