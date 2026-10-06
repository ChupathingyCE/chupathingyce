/*
HARDWARE_ID.C

This machine's hardware id: what a joining machine tells the host
(network_client_manager.c, in the join request after the Xbox's fields), and
what a host keeps of it to log players and refuse one it banned
(bans.txt; the dedicated server's sv_ban). It is 16 bytes, as 32 hex digits,
of an HMAC-SHA256 keyed for this game, so what is told is no raw serial, is
this game's alone, and tells nothing of what it was made from.

What it is made from, in order:

- What the machine is known by, where the system has a stable id any
  program may read without a permission: Windows' SMBIOS system UUID, else
  its MachineGuid (win32_p2p.c); Linux's /etc/machine-id; macOS's platform
  UUID (gethostuuid, IOKit's IOPlatformUUID). The game has always hashed
  Windows' and Linux's this way, so their ids are what they were.
- Otherwise a random id of this install's: hardware_id.key in the save root,
  made the first time it is needed and kept from then on (an update leaves
  the save root alone). Android always has this one: its only id of the
  device is ANDROID_ID, which the game does not read, so a phone tells a
  host nothing that would follow it to other apps. So do Linux containers
  and sandboxes without a machine-id, and any machine whose system id
  cannot be read.

Why the system's id comes first, and alone, rather than mixed with the
install's: a host bans a hardware id so that the player stays out. With the
system's id, a ban holds through a reinstall, a new save folder, or a new
user on the machine; mixed with the install's, deleting a file would lift
it. Mixing would not make the id more private either: the system's id never
leaves the machine (only the keyed hash does), and it would change every
Windows and Linux player's id, so the bans already written would stop
holding. Where there is no system id the install's is the best there is:
a reinstall, or a new save root, makes a new one.

Either way it is a stable id, not a proof: anyone with administrator or root
access can change what their machine is known by, and anyone can delete
hardware_id.key. Hosts ban by address as well.
*/

#include "platform.h"
#include "posix.h"
#include "p2p_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <unistd.h>
#include <uuid/uuid.h>
#endif

/* this install's random id: 32 bytes, as hex, in the file */
#define HARDWARE_ID_INSTALL_BYTES 32
#define HARDWARE_ID_INSTALL_FILE "hardware_id.key"

/* the HMAC's key; changing it changes every machine's id (and so lifts
every ban), so it stays as it is */
static const char hardware_id_key[] = "halo-ce-universal hardware id v1";

#ifdef _WIN32
/* win32_p2p.c's: the SMBIOS system UUID, else the registry's MachineGuid */
int posix_hardware_id_source(char *text, int size);
#endif

/* what this machine is known by, as text (none: 0) */
int hardware_id_machine_source(char *text, int size)
{
#if defined(_WIN32)
	return posix_hardware_id_source(text, size);
#elif defined(HALO_ANDROID)
	(void)text;
	(void)size;
	return 0;
#elif defined(__APPLE__)
	/* (the kernel's copy of IOKit's IOPlatformUUID, from the Mac's firmware:
	the same for every user, kept through a reinstall of macOS) */
	uuid_t uuid;
	struct timespec wait = { 1, 0 };
	char string[37];
	int index;
	int zeros = 1;

	if (size < (int)sizeof(string) || gethostuuid(uuid, &wait) != 0)
		return 0;
	for (index = 0; index < (int)sizeof(uuid); index++)
		zeros &= uuid[index] == 0;
	if (zeros)
		return 0;
	uuid_unparse_upper(uuid, string);
	snprintf(text, (size_t)size, "%s", string);
	return 1;
#else
	static const char *const paths[] = { "/etc/machine-id", "/var/lib/dbus/machine-id" };
	int index;

	for (index = 0; index < (int)(sizeof(paths) / sizeof(paths[0])); index++)
	{
		FILE *file = fopen(paths[index], "rb");
		size_t length;

		if (!file)
			continue;
		length = fread(text, 1, (size_t)size - 1, file);
		fclose(file);
		text[length] = 0;
		/* (the line, without its end) */
		text[strcspn(text, "\r\n")] = 0;
		if (text[0])
			return 1;
	}
	return 0;
#endif
}

static void hardware_id_hex(const unsigned char *bytes, int size, char *text)
{
	static const char digits[] = "0123456789abcdef";
	int index;

	for (index = 0; index < size; index++)
	{
		text[2 * index] = digits[bytes[index] >> 4];
		text[2 * index + 1] = digits[bytes[index] & 15];
	}
	text[2 * size] = 0;
}

/* the install's id in the file at path, if it has one (lowercase hex,
HARDWARE_ID_INSTALL_BYTES' worth, then at most a line's end) */
static int hardware_id_read_install(const char *path, char *text)
{
	char line[2 * HARDWARE_ID_INSTALL_BYTES + 8];
	FILE *file = fopen(path, "rb");
	size_t length;
	int index;

	if (!file)
		return 0;
	length = fread(line, 1, sizeof(line) - 1, file);
	fclose(file);
	line[length] = 0;
	line[strcspn(line, "\r\n")] = 0;
	if (strlen(line) != 2 * HARDWARE_ID_INSTALL_BYTES)
		return 0;
	for (index = 0; line[index]; index++)
	{
		if (!((line[index] >= '0' && line[index] <= '9') || (line[index] >= 'a' && line[index] <= 'f')))
			return 0;
	}
	memcpy(text, line, 2 * HARDWARE_ID_INSTALL_BYTES + 1);
	return 1;
}

/* this install's random id, as hex (text: 2 * HARDWARE_ID_INSTALL_BYTES + 1
characters), from hardware_id.key in root, which is made (random) if it is
missing or unreadable as one: written beside it, then renamed over it, so a
crash never leaves half of one. Two copies of the game making it at once
keep whichever was renamed last, as read back (0: no file could be made) */
int hardware_id_install_source(const char *root, char *text, int size)
{
	char path[1024];
	char temporary[1100];
	unsigned char bytes[HARDWARE_ID_INSTALL_BYTES];
	unsigned char suffix[4];
	char made[2 * HARDWARE_ID_INSTALL_BYTES + 1];
	char suffix_text[2 * sizeof(suffix) + 1];
	FILE *file;
	int written;

	if (size < (int)sizeof(made) || !root || !root[0])
		return 0;
	snprintf(path, sizeof(path), "%s/%s", root, HARDWARE_ID_INSTALL_FILE);
	if (hardware_id_read_install(path, text))
		return 1;
	posix_random_bytes(bytes, sizeof(bytes));
	posix_random_bytes(suffix, sizeof(suffix));
	hardware_id_hex(bytes, (int)sizeof(bytes), made);
	hardware_id_hex(suffix, (int)sizeof(suffix), suffix_text);
	snprintf(temporary, sizeof(temporary), "%s.%s.new", path, suffix_text);
	file = fopen(temporary, "wb");
	if (!file)
		return 0;
	written = fprintf(file, "%s\n", made) == (int)strlen(made) + 1;
	written = fclose(file) == 0 && written;
	if (!written)
	{
		remove(temporary);
		return 0;
	}
	/* (Windows renames over no file: one there is another copy's, read below) */
	if (rename(temporary, path) != 0)
		remove(temporary);
	return hardware_id_read_install(path, text);
}

/* the hardware id (hex: P2P_HARDWARE_ID_SIZE characters) of a machine known
by machine (NULL: none; its first 255 characters), whose save root is root; empty if it has neither.
Returns where it came from: 2 the machine's id, 1 the install's, 0 none */
int hardware_id_of(const char *machine, const char *root, char *hex)
{
	char install[2 * HARDWARE_ID_INSTALL_BYTES + 1];
	char source[256];
	unsigned char digest[P2P_SHA256_SIZE];
	int from = 0;

	hex[0] = 0;
	if (machine && machine[0])
	{
		/* (the text alone, as the game has always hashed it) */
		snprintf(source, sizeof(source), "%s", machine);
		from = 2;
	}
	else if (hardware_id_install_source(root, install, sizeof(install)))
	{
		/* (labelled, so no machine's id hashes to an install's) */
		snprintf(source, sizeof(source), "install %s", install);
		from = 1;
	}
	if (from)
	{
		p2p_hmac_sha256((const unsigned char *)hardware_id_key, (int)sizeof(hardware_id_key) - 1, source,
			(int)strlen(source), digest);
		hardware_id_hex(digest, P2P_HARDWARE_ID_BYTES, hex);
	}
	return from;
}

void p2p_hardware_id(char *hex, int size)
{
	static char cached[P2P_HARDWARE_ID_SIZE];
	static int computed;

	if (!computed)
	{
		char machine[256];
		int from;

		computed = 1;
		from = hardware_id_of(hardware_id_machine_source(machine, sizeof(machine)) ? machine : NULL,
			platform_save_root(), cached);
		platform_log("Hardware id: %s", from == 2 ? "this machine's" :
			from == 1 ? "this install's (" HARDWARE_ID_INSTALL_FILE " in the save root)" : "none");
	}
	snprintf(hex, (size_t)size, "%s", cached);
}

void p2p_hardware_id_sanitize(char *destination, int size, const char *source)
{
	int length = 0;

	for (; source && *source && length < size - 1 && length < 2 * P2P_HARDWARE_ID_BYTES; source++)
	{
		char character = *source >= 'A' && *source <= 'F' ? *source - 'A' + 'a' : *source;

		if ((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))
			destination[length++] = character;
	}
	if (size > 0)
		destination[length] = 0;
}
