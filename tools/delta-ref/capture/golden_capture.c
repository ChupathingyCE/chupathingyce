/*
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later

GOLDEN_CAPTURE.C

Writes tools/delta-ref/golden.h: bytes the game's own Delta code makes
(port/linux/src/delta_wire.c and delta_compat.c, and browser.c's form
encoding), which the reference client's test compares its own with. Built
and run by tools/delta-ref/capture/capture.py; never by a port.
*/

#include "delta.h"
#include "delta_wire.h"
#include "delta_compat.h"

#include <stdio.h>
#include <string.h>

/* browser.c's form_add, as capture.py takes it from the file */
#include "form_add.inc"

static void array(const char *name, const unsigned char *data, int size)
{
	int index;

	printf("static const uint8_t %s[%d] = {", name, size);
	for (index = 0; index < size; index++)
		printf("%s0x%02x", !index ? "\n\t" : index % 12 ? ", " : ",\n\t", data[index]);
	printf("\n};\n\n");
}

static void key_of(struct delta_platform_key *key, int platform, int flags, int host, int join, int opt_in, int memory)
{
	memset(key, 0, sizeof(*key));
	key->platform = (unsigned char)platform;
	key->version = DELTA_PLATFORM_KEY_VERSION;
	key->flags = (unsigned char)flags;
	key->host_players = (unsigned char)host;
	key->join_players = (unsigned char)join;
	key->join_players_opt_in = (unsigned char)opt_in;
	key->memory_class = (unsigned char)memory;
}

int main(void)
{
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	unsigned char reserved[DELTA_ADVERTISED_RESERVED_SIZE];
	struct delta_wire_hello hello;
	struct delta_wire_welcome welcome;
	static struct delta_wire_roster roster;
	int size;

	printf("/*\nSPDX-FileCopyrightText: 2026 The Chupathingy contributors\n"
		"SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev\nSPDX-License-Identifier: GPL-3.0-or-later\n\n"
		"GOLDEN.H\n\nMade by tools/delta-ref/capture/capture.py from the game's own Delta code\n"
		"(delta_wire.c, delta_compat.c, browser.c's form encoding) at wire %s.\n"
		"Do not edit: run capture.py again.\n*/\n\n#include <stdint.h>\n\n", DELTA_WIRE);
	printf("#define GOLDEN_WIRE \"%s\"\n#define GOLDEN_MAJOR %d\n#define GOLDEN_PEER_PORT %d\n\n", DELTA_WIRE,
		DELTA_MAJOR, DELTA_PEER_PORT);
	printf("#define GOLDEN_CAP_PLATFORM %d\n#define GOLDEN_CAP_PING %d\n#define GOLDEN_CAP_HOST_CHEATS %d\n"
		"#define GOLDEN_CAP_GAME_MODES %d\n#define GOLDEN_CAPABILITIES %d\n\n", _delta_capability_platform,
		_delta_capability_ping, _delta_capability_host_cheats, _delta_capability_game_modes,
		NUMBER_OF_DELTA_CAPABILITIES);
	printf("#define GOLDEN_PLATFORM_N64 %d\n#define GOLDEN_PLATFORM_PC_LEGACY %d\n\n", _delta_platform_n64,
		_delta_platform_pc_legacy);

	/* a plain port's HELLO: the platform alone, takes no tables */
	memset(&hello, 0, sizeof(hello));
	hello.capabilities = 1u << _delta_capability_platform;
	hello.legacy_version = 44;
	hello.machine_index = 2;
	hello.legacy_table_serial = 0xFFFFFFFFu;
	key_of(&hello.key, _delta_platform_n64, 0, 0, 8, 16, 3);
	strcpy(hello.build, "n64-port 0.1");
	size = delta_wire_write_hello(data, 0x1234ABCDu, &hello);
	array("golden_hello_port", data, size);

	/* one like our own builds': many capabilities, modes, a table */
	memset(&hello, 0, sizeof(hello));
	hello.capabilities = 0x03FBFDDFu & ~(1u << _delta_capability_ping) & ~(1u << _delta_capability_host_cheats);
	hello.legacy_version = DELTA_LEGACY_NEWEST;
	hello.machine_index = 1;
	hello.legacy_table_serial = 12;
	key_of(&hello.key, _delta_platform_pc_linux, 0, 128, 128, 128, 12);
	strcpy(hello.build, "0.9.2");
	hello.modes = 3;
	size = delta_wire_write_hello(data, 0x89ABCDEFu, &hello);
	array("golden_hello_pc", data, size);

	/* a dedicated server's WELCOME to the port */
	memset(&welcome, 0, sizeof(welcome));
	welcome.capabilities = 0x03FBFDDFu & ~(1u << _delta_capability_ping) & ~(1u << _delta_capability_host_cheats);
	welcome.agreed = 1u << _delta_capability_platform;
	welcome.legacy_version = DELTA_LEGACY_NEWEST;
	welcome.host_machine_index = DELTA_WIRE_NO_MACHINE;
	welcome.legacy_table_serial = 12;
	key_of(&welcome.key, _delta_platform_pc_linux, 0x02, 128, 128, 128, 12);
	strcpy(welcome.build, "0.9.2 server");
	welcome.modes = 3;
	size = delta_wire_write_welcome(data, 0x1234ABCDu, &welcome);
	array("golden_welcome", data, size);

	/* LEGACY from a host of another major, and a BYE */
	size = delta_wire_write_empty(data, 2, _delta_message_legacy, 0x1234ABCDu);
	array("golden_legacy_major2", data, size);
	size = delta_wire_write_empty(data, DELTA_MAJOR, _delta_message_bye, 0x1234ABCDu);
	array("golden_bye", data, size);

	/* a roster of two machines: the port and a PC */
	memset(&roster, 0, sizeof(roster));
	roster.room_capabilities = 1u << _delta_capability_platform;
	roster.room_players = 8;
	roster.count = 2;
	roster.entries[0].machine_index = 1;
	roster.entries[0].flags = DELTA_ROSTER_DELTA | DELTA_ROSTER_PLATFORM;
	roster.entries[0].capabilities = 1u << _delta_capability_platform;
	key_of(&roster.entries[0].key, _delta_platform_pc_linux, 0, 128, 128, 128, 12);
	roster.entries[1].machine_index = 2;
	roster.entries[1].flags = DELTA_ROSTER_DELTA | DELTA_ROSTER_PLATFORM;
	roster.entries[1].capabilities = 1u << _delta_capability_platform;
	key_of(&roster.entries[1].key, _delta_platform_n64, 0, 0, 8, 16, 3);
	size = delta_wire_write_roster(data, 0x1234ABCDu, &roster);
	array("golden_roster", data, size);

	/* advertisements: this build's tag on a dedicated server announcing 44 */
	memset(reserved, 0, sizeof(reserved));
	reserved[0] = 44;
	reserved[2] = 0x01 | 0x04;
	delta_compat_advertise(reserved, (int)sizeof(reserved), DELTA_MAJOR, DELTA_WIRE);
	array("golden_advert_tag", reserved, (int)sizeof(reserved));
	printf("#define GOLDEN_ADVERT_TAG_SPEAKS %d\n\n", delta_compat_speaks_delta(reserved, (int)sizeof(reserved)));
	/* ... with the old layout too, on a game OpenCE names no kind for */
	reserved[0] = 32;
	delta_compat_advertise_old(reserved, (int)sizeof(reserved), DELTA_MAJOR, DELTA_WIRE);
	array("golden_advert_both", reserved, (int)sizeof(reserved));
	printf("#define GOLDEN_ADVERT_BOTH_SPEAKS %d\n\n", delta_compat_speaks_delta(reserved, (int)sizeof(reserved)));
	/* ... a host before the tag (the old layout alone, announcing 32) */
	memset(reserved, 0, sizeof(reserved));
	reserved[0] = 32;
	reserved[2] = 0x01 | 0x04;
	delta_compat_advertise_old(reserved, (int)sizeof(reserved), DELTA_MAJOR, "chupa-32a");
	array("golden_advert_old", reserved, (int)sizeof(reserved));
	printf("#define GOLDEN_ADVERT_OLD_SPEAKS %d\n\n", delta_compat_speaks_delta(reserved, (int)sizeof(reserved)));
	/* ... an OpenCE dedicated server of 44 (the flag, no Delta) */
	memset(reserved, 0, sizeof(reserved));
	reserved[0] = 44;
	reserved[2] = 0x01 | 0x04;
	array("golden_advert_opence", reserved, (int)sizeof(reserved));
	printf("#define GOLDEN_ADVERT_OPENCE_SPEAKS %d\n\n", delta_compat_speaks_delta(reserved, (int)sizeof(reserved)));

	/* the join mark on a machine name of 32 characters */
	{
		unsigned short name[32];
		unsigned char bytes[64];
		int index;

		memset(name, 0xEE, sizeof(name));
		name[0] = 'N';
		name[1] = '6';
		name[2] = '4';
		name[3] = 0;
		delta_compat_join_mark(name, 32);
		for (index = 0; index < 32; index++)
		{
			bytes[index * 2] = (unsigned char)(name[index] & 0xFF);
			bytes[index * 2 + 1] = (unsigned char)(name[index] >> 8);
		}
		array("golden_join_mark_le", bytes, 64);
	}

	/* the announcement's Delta fields, as browser.c's host_fields writes
	them for a host without Delta Peer (delta 0) */
	{
		static char form[1024];
		unsigned char key[8];
		char hex[17], text[16];
		struct delta_platform_key platform_key;
		int index;

		form[0] = 0;
		form_add(form, (int)sizeof(form), "delta", "0");
		key_of(&platform_key, _delta_platform_n64, 0, 0, 8, 16, 3);
		delta_wire_write_key(key, &platform_key);
		for (index = 0; index < 8; index++)
			snprintf(hex + index * 2, 3, "%02x", key[index]);
		form_add(form, (int)sizeof(form), "platform_key", hex);
		form_add(form, (int)sizeof(form), "arch", "mips");
		form_add(form, (int)sizeof(form), "build", "n64-port 0.1+test");
		snprintf(text, sizeof(text), "%08x", 1u << _delta_capability_platform);
		form_add(form, (int)sizeof(form), "capabilities", text);
		printf("static const char golden_announce[] = \"%s\";\n", form);
	}
	return 0;
}
