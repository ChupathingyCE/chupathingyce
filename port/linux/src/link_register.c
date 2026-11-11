/*
LINK_REGISTER.C

The handler of halo:// links registered for this game, without taking
another program's over: link_register.h.
*/

#include "link_register.h"

#include <stdio.h>
#include <string.h>

enum
{
	PATH_SIZE = 1200,
	ENTRY_SIZE = 4096,
	VALUE_SIZE = 1200,
};

static int lower(int character)
{
	return character >= 'A' && character <= 'Z' ? character - 'A' + 'a' : character;
}

static int same_text(const char *a, const char *b, size_t length, int ignore_case)
{
	size_t index;

	for (index = 0; index < length; index++)
	{
		if (ignore_case ? lower((unsigned char)a[index]) != lower((unsigned char)b[index]) : a[index] != b[index])
			return 0;
	}
	return 1;
}

/* the length of path's folder (up to its last slash or backslash), 0 for
none */
static size_t folder_length(const char *path)
{
	const char *slash = strrchr(path, '/');
	const char *backslash = strrchr(path, '\\');

	if (backslash && (!slash || backslash > slash))
		slash = backslash;
	return slash ? (size_t)(slash - path) : 0;
}

int link_register_desktop_entry(char *entry, int size, const char *executable)
{
	char argument[PATH_SIZE * 2];
	int written;

	if (!executable || !executable[0] || discord_register_exec_argument(argument, sizeof(argument), executable) < 0)
		return -1;
	written = snprintf(entry, (size_t)size,
		"[Desktop Entry]\n"
		"Name=Chupathingy\n"
		"Comment=Opens Chupathingy's party and game links\n"
		"Exec=%s %%u\n"
		"Type=Application\n"
		"NoDisplay=true\n"
		"Categories=Game;\n"
		"MimeType=" LINK_REGISTER_MIME_TYPE ";\n"
		DISCORD_REGISTER_MARK "=true\n",
		argument);
	return written < 0 || written >= size ? -1 : written;
}

/* the value of a desktop entry's line key= (the first), or NULL; its length
in length */
static const char *entry_value(const char *text, const char *key, size_t *length)
{
	const char *line;
	size_t key_length = strlen(key);

	for (line = text; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL)
	{
		if (!strncmp(line, key, key_length) && line[key_length] == '=')
		{
			const char *value = line + key_length + 1;

			*length = strcspn(value, "\r\n");
			return value;
		}
	}
	return NULL;
}

int link_register_desktop_exec(const char *text, char *executable, int size)
{
	size_t length, index;
	int used = 0;
	const char *value = text ? entry_value(text, "Exec", &length) : NULL;

	if (size <= 0)
		return 0;
	executable[0] = 0;
	if (!value)
		return 0;
	if (length && value[0] == '"')
	{
		/* (a quoted argument, as earlier versions wrote it: a backslash takes
		the next character as it is) */
		for (index = 1; index < length && value[index] != '"'; index++)
		{
			if (value[index] == '\\' && index + 1 < length)
				index++;
			if (used + 1 >= size)
				return 0;
			executable[used++] = value[index];
		}
		if (index >= length)
			return 0;
	}
	else
	{
		for (index = 0; index < length && value[index] != ' ' && value[index] != '\t'; index++)
		{
			if (used + 1 >= size)
				return 0;
			executable[used++] = value[index];
		}
	}
	executable[used] = 0;
	return used > 0;
}

int link_register_program_ours(const char *named, const char *executable, link_register_exists exists,
	int ignore_case)
{
	static const char prefix[] = "chupathingy";
	size_t folder, named_folder, named_length;
	const char *base;

	if (!named || !named[0] || !executable || !executable[0])
		return 0;
	named_length = strlen(named);
	/* this executable */
	if (named_length == strlen(executable) && same_text(named, executable, named_length, ignore_case))
		return 1;
	folder = folder_length(executable);
	named_folder = folder_length(named);
	base = named + named_folder + (named_folder ? 1 : 0);
	/* the earlier name in this install's folder (port/rename_stub: halo.exe
	or halo, which now starts chupathingy) */
	if (folder && folder == named_folder && same_text(named, executable, folder, ignore_case) &&
		(same_text(base, "halo", 5, 1) || same_text(base, "halo.exe", 9, 1) || same_text(base, "halo.sh", 8, 1)))
	{
		return 1;
	}
	/* another copy of this game, by its name */
	if (strlen(base) >= sizeof(prefix) - 1 && same_text(base, prefix, sizeof(prefix) - 1, 1))
		return 1;
	/* a program that is gone: nothing runs it */
	return exists && !exists(named);
}

int link_register_desktop_entry_ours(const char *text, const char *executable, link_register_exists exists)
{
	char named[PATH_SIZE];
	size_t length;
	const char *name;

	if (!text)
		return 0;
	if (discord_register_desktop_entry_ours(text))
		return 1;
	name = entry_value(text, "Name", &length);
	if (!name || length != strlen(LINK_REGISTER_OLD_NAME) || strncmp(name, LINK_REGISTER_OLD_NAME, length))
		return 0;
	return link_register_desktop_exec(text, named, sizeof(named)) &&
		link_register_program_ours(named, executable, exists, 0);
}

int link_register_mimeapps_default(const char *text, char *name, int size)
{
	static const char key[] = LINK_REGISTER_MIME_TYPE "=";
	const char *line;
	int in_defaults = 0;

	if (size <= 0)
		return 0;
	name[0] = 0;
	for (line = text; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL)
	{
		size_t length = strcspn(line, "\r\n");

		if (line[0] == '[')
			in_defaults = length == strlen("[Default Applications]") &&
				!strncmp(line, "[Default Applications]", length);
		else if (in_defaults && !strncmp(line, key, sizeof(key) - 1))
		{
			const char *value = line + sizeof(key) - 1;
			size_t value_length = strcspn(value, ";\r\n");

			if (!value_length || (int)value_length >= size)
				return 0;
			memcpy(name, value, value_length);
			name[value_length] = 0;
			/* (a file's name, never a path) */
			if (strchr(name, '/'))
			{
				name[0] = 0;
				return 0;
			}
			return 1;
		}
	}
	return 0;
}

static int read_text(const char *path, char *text, int size)
{
	FILE *file = fopen(path, "r");
	size_t length;

	if (!file)
		return 0;
	length = fread(text, 1, (size_t)size - 1, file);
	fclose(file);
	text[length] = 0;
	return 1;
}

int link_register_desktop_file(const char *directory, const char *current_default, const char *current_text,
	const char *executable, link_register_exists exists, int *make_default)
{
	char path[PATH_SIZE], old_path[PATH_SIZE], entry[ENTRY_SIZE], existing[ENTRY_SIZE];
	FILE *file;
	int length = link_register_desktop_entry(entry, sizeof(entry), executable);
	int has_old, old_ours, result;

	*make_default = 0;
	if (length < 0 || !directory || !directory[0])
		return DISCORD_REGISTER_FAILED;
	if (!current_default)
		current_default = "";
	if (snprintf(path, sizeof(path), "%s/%s", directory, LINK_REGISTER_DESKTOP_NAME) >= (int)sizeof(path) ||
		snprintf(old_path, sizeof(old_path), "%s/%s", directory, LINK_REGISTER_OLD_DESKTOP_NAME) >= (int)sizeof(old_path))
	{
		return DISCORD_REGISTER_FAILED;
	}
	/* the handler the user has: another program's is left (one that no
	applications folder has any more runs nothing) */
	if (current_default[0] && strcmp(current_default, LINK_REGISTER_DESKTOP_NAME) && current_text &&
		!link_register_desktop_entry_ours(current_text, executable, exists))
	{
		return DISCORD_REGISTER_LEFT;
	}
	/* an earlier version's entry: another program's, with no default named,
	is the handler as it is; this game's goes */
	has_old = read_text(old_path, existing, sizeof(existing));
	old_ours = has_old && link_register_desktop_entry_ours(existing, executable, exists);
	if (has_old && !old_ours && !current_default[0])
		return DISCORD_REGISTER_LEFT;
	/* this game's entry, unless its name is another program's */
	result = DISCORD_REGISTER_WRITTEN;
	if (read_text(path, existing, sizeof(existing)))
	{
		if (!discord_register_desktop_entry_ours(existing))
			return DISCORD_REGISTER_LEFT;
		if (!strcmp(existing, entry))
			result = DISCORD_REGISTER_UNCHANGED;
	}
	if (result == DISCORD_REGISTER_WRITTEN)
	{
		file = fopen(path, "w");
		if (!file)
			return DISCORD_REGISTER_FAILED;
		if (fputs(entry, file) < 0)
		{
			fclose(file);
			return DISCORD_REGISTER_FAILED;
		}
		if (fclose(file) != 0)
			return DISCORD_REGISTER_FAILED;
	}
	if (old_ours)
		remove(old_path);
	if (strcmp(current_default, LINK_REGISTER_DESKTOP_NAME))
	{
		*make_default = 1;
		result = DISCORD_REGISTER_WRITTEN;
	}
	return result;
}

/* the program a Windows command runs (its first argument) into named */
static int command_program(const char *command, char *named, int size)
{
	const char *end;
	size_t length;

	if (command[0] == '"')
	{
		command++;
		end = strchr(command, '"');
		if (!end)
			return 0;
	}
	else
	{
		end = strchr(command, ' ');
		if (!end)
			end = command + strlen(command);
	}
	length = (size_t)(end - command);
	if (!length || (int)length >= size)
		return 0;
	memcpy(named, command, length);
	named[length] = 0;
	return 1;
}

int link_register_windows(const struct discord_registry *registry, const char *executable,
	link_register_exists exists)
{
	static const char key[] = "Software\\Classes\\" LINK_REGISTER_SCHEME;
	static const char command_key[] = "Software\\Classes\\" LINK_REGISTER_SCHEME "\\shell\\open\\command";
	static const char icon_key[] = "Software\\Classes\\" LINK_REGISTER_SCHEME "\\DefaultIcon";
	char command[VALUE_SIZE], existing[VALUE_SIZE], label[VALUE_SIZE], named[VALUE_SIZE], mark[32];
	int has_command, has_mark, ok = 1;

	if (!registry || !executable || !executable[0] || strchr(executable, '"'))
		return DISCORD_REGISTER_FAILED;
	if (snprintf(command, sizeof(command), "\"%s\" \"%%1\"", executable) >= (int)sizeof(command))
		return DISCORD_REGISTER_FAILED;
	has_command = registry->get(registry->context, command_key, NULL, existing, sizeof(existing)) && existing[0];
	has_mark = registry->get(registry->context, key, DISCORD_REGISTER_REGISTRY_MARK, mark, sizeof(mark));
	if (has_command && !has_mark)
	{
		/* an earlier version's (its label, and a program of this game's or
		one that is gone), or another program's, which is left */
		if (!registry->get(registry->context, key, NULL, label, sizeof(label)) ||
			strcmp(label, "URL:" LINK_REGISTER_OLD_NAME) || !command_program(existing, named, sizeof(named)) ||
			!link_register_program_ours(named, executable, exists, 1))
		{
			return DISCORD_REGISTER_LEFT;
		}
	}
	if (has_command && has_mark && !strcmp(existing, command))
		return DISCORD_REGISTER_UNCHANGED;
	ok &= registry->set(registry->context, key, NULL, LINK_REGISTER_WINDOWS_LABEL);
	ok &= registry->set(registry->context, key, "URL Protocol", "");
	ok &= registry->set(registry->context, key, DISCORD_REGISTER_REGISTRY_MARK, "links");
	ok &= registry->set(registry->context, icon_key, NULL, executable);
	ok &= registry->set(registry->context, command_key, NULL, command);
	return ok ? DISCORD_REGISTER_WRITTEN : DISCORD_REGISTER_FAILED;
}
