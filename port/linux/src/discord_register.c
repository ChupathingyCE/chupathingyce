/*
DISCORD_REGISTER.C

The launch command registered for the Discord application, so that Discord
shows a friend Join and can start the game for it: discord_register.h.
*/

#include "discord_register.h"

#include <stdio.h>
#include <string.h>

enum
{
	PATH_SIZE = 1200,
	ENTRY_SIZE = 4096,
	VALUE_SIZE = 1200,
};

int discord_register_application_valid(const char *application)
{
	size_t length = application ? strlen(application) : 0;

	return length > 0 && length < 32 && strspn(application, "0123456789") == length;
}

/* executable as a desktop entry's quoted argument, into text (size bytes):
its length, or -1 (a line break, or too long). The quoted argument's
backslash, quote, backquote and dollar take a backslash, and the value's
own escapes double the backslashes; a percent is a field code's, doubled */
static int quoted_argument(char *text, int size, const char *executable)
{
	int used = 0;

	if (size < 3)
		return -1;
	text[used++] = '"';
	for (; *executable; executable++)
	{
		char character = *executable;
		const char *add;
		char single[2] = { character, 0 };
		char escaped[4] = { '\\', '\\', character, 0 };

		if (character == '\n' || character == '\r')
			return -1;
		if (character == '\\')
			add = "\\\\\\\\";
		else if (character == '"' || character == '`' || character == '$')
			add = escaped;
		else if (character == '%')
			add = "%%";
		else
			add = single;
		if (used + (int)strlen(add) + 2 > size)
			return -1;
		memcpy(text + used, add, strlen(add));
		used += (int)strlen(add);
	}
	text[used++] = '"';
	text[used] = 0;
	return used;
}

int discord_register_desktop_entry(char *entry, int size, const char *application, const char *executable)
{
	char argument[PATH_SIZE * 2];
	int written;

	if (!discord_register_application_valid(application) || !executable || !executable[0] ||
		quoted_argument(argument, sizeof(argument), executable) < 0)
	{
		return -1;
	}
	/* (the SDK's entry, with the game's name and its mark) */
	written = snprintf(entry, (size_t)size,
		"[Desktop Entry]\n"
		"Name=Chupathingy\n"
		"Exec=%s %%u\n"
		"Type=Application\n"
		"NoDisplay=true\n"
		"Categories=Discord;Games;\n"
		"MimeType=x-scheme-handler/discord-%s;\n"
		DISCORD_REGISTER_MARK "=true\n",
		argument, application);
	return written < 0 || written >= size ? -1 : written;
}

int discord_register_desktop_entry_ours(const char *text)
{
	const char *line;

	for (line = text; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL)
	{
		if (!strncmp(line, DISCORD_REGISTER_MARK "=", strlen(DISCORD_REGISTER_MARK) + 1))
			return 1;
	}
	return 0;
}

int discord_register_desktop_file(const char *directory, const char *application, const char *executable,
	char *name, int name_size)
{
	char path[PATH_SIZE], entry[ENTRY_SIZE], existing[ENTRY_SIZE];
	FILE *file;
	int length = discord_register_desktop_entry(entry, sizeof(entry), application, executable);

	if (length < 0 || !directory || !directory[0])
		return DISCORD_REGISTER_FAILED;
	if (snprintf(name, (size_t)name_size, "discord-%s.desktop", application) >= name_size ||
		snprintf(path, sizeof(path), "%s/%s", directory, name) >= (int)sizeof(path))
	{
		return DISCORD_REGISTER_FAILED;
	}
	file = fopen(path, "r");
	if (file)
	{
		size_t read = fread(existing, 1, sizeof(existing) - 1, file);

		fclose(file);
		existing[read] = 0;
		if (!strcmp(existing, entry))
			return DISCORD_REGISTER_UNCHANGED;
		/* (another program's: the game written by the SDK, a launcher's) */
		if (!discord_register_desktop_entry_ours(existing))
			return DISCORD_REGISTER_LEFT;
	}
	file = fopen(path, "w");
	if (!file)
		return DISCORD_REGISTER_FAILED;
	if (fputs(entry, file) < 0)
	{
		fclose(file);
		return DISCORD_REGISTER_FAILED;
	}
	return fclose(file) == 0 ? DISCORD_REGISTER_WRITTEN : DISCORD_REGISTER_FAILED;
}

int discord_register_windows(const struct discord_registry *registry, const char *application,
	const char *executable)
{
	char key[128], subkey[192], label[128], command[VALUE_SIZE], existing[VALUE_SIZE], mark[32];
	int has_command, has_mark, ok = 1;

	if (!registry || !discord_register_application_valid(application) || !executable || !executable[0] ||
		strchr(executable, '"'))
	{
		return DISCORD_REGISTER_FAILED;
	}
	snprintf(key, sizeof(key), "Software\\Classes\\discord-%s", application);
	snprintf(label, sizeof(label), "URL:Run game %s protocol", application);
	if (snprintf(command, sizeof(command), "\"%s\" \"%%1\"", executable) >= (int)sizeof(command))
		return DISCORD_REGISTER_FAILED;
	snprintf(subkey, sizeof(subkey), "%s\\shell\\open\\command", key);
	has_command = registry->get(registry->context, subkey, NULL, existing, sizeof(existing)) && existing[0];
	has_mark = registry->get(registry->context, key, DISCORD_REGISTER_REGISTRY_MARK, mark, sizeof(mark));
	/* (another program's: left) */
	if (has_command && !has_mark)
		return DISCORD_REGISTER_LEFT;
	if (has_command && !strcmp(existing, command))
		return DISCORD_REGISTER_UNCHANGED;
	ok &= registry->set(registry->context, key, NULL, label);
	ok &= registry->set(registry->context, key, "URL Protocol", "");
	ok &= registry->set(registry->context, key, DISCORD_REGISTER_REGISTRY_MARK, "launch");
	snprintf(subkey, sizeof(subkey), "%s\\DefaultIcon", key);
	ok &= registry->set(registry->context, subkey, NULL, executable);
	snprintf(subkey, sizeof(subkey), "%s\\shell\\open\\command", key);
	ok &= registry->set(registry->context, subkey, NULL, command);
	return ok ? DISCORD_REGISTER_WRITTEN : DISCORD_REGISTER_FAILED;
}

int discord_register_old_json_ours(const char *text)
{
	/* ({"command": "open -b io.github.mrmilenko.halo-ce-macos"}) */
	return text && strstr(text, "\"command\"") && strstr(text, "open -b io.github.");
}
