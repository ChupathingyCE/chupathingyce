/*
DISCORD_REGISTER_TEST.C

Tests of the launch command registered for Discord (port/linux/src/
discord_register.c): the desktop entry written into a scratch folder (as
~/.local/share/applications), and the Windows registry's keys through a
fake registry. Built and run by tools/test_discord_register.py with a
scratch folder as its argument; exits nonzero on a failure.
*/

#include "../src/discord_register.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

static const char application[] = "1560397272166957136";

static int read_file(const char *path, char *text, int size)
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

static void write_file(const char *path, const char *text)
{
	FILE *file = fopen(path, "w");

	if (file)
	{
		fputs(text, file);
		fclose(file);
	}
}

static void test_desktop_entry(void)
{
	char entry[4096];

	CHECK(discord_register_desktop_entry(entry, sizeof(entry), application, "/opt/Chupathingy/chupathingy") > 0);
	CHECK(strstr(entry, "[Desktop Entry]\n") == entry);
	CHECK(strstr(entry, "\nExec=\"/opt/Chupathingy/chupathingy\" %u\n"));
	CHECK(strstr(entry, "\nMimeType=x-scheme-handler/discord-1560397272166957136;\n"));
	CHECK(strstr(entry, "\nNoDisplay=true\n"));
	CHECK(strstr(entry, "\nX-Chupathingy=true\n"));
	CHECK(discord_register_desktop_entry_ours(entry));

	/* a path's spaces, quotes, dollars, backslashes and percents, quoted as
	the desktop entry spec says */
	CHECK(discord_register_desktop_entry(entry, sizeof(entry), application, "/home/a b/$x\"y\\z%1/chupathingy") > 0);
	CHECK(strstr(entry, "\nExec=\"/home/a b/\\\\$x\\\\\"y\\\\\\\\z%%1/chupathingy\" %u\n"));
	/* a line break would start a line of its own: refused */
	CHECK(discord_register_desktop_entry(entry, sizeof(entry), application, "/tmp/a\nExec=evil") < 0);

	/* an application id is digits only */
	CHECK(discord_register_desktop_entry(entry, sizeof(entry), "12/../x", "/bin/game") < 0);
	CHECK(discord_register_desktop_entry(entry, sizeof(entry), "", "/bin/game") < 0);
	CHECK(!discord_register_application_valid(NULL));
	CHECK(!discord_register_application_valid("123456789012345678901234567890123"));
	CHECK(discord_register_application_valid(application));
	/* too small */
	CHECK(discord_register_desktop_entry(entry, 40, application, "/bin/game") < 0);

	CHECK(!discord_register_desktop_entry_ours("[Desktop Entry]\nName=Game 1560397272166957136\n"));
	CHECK(!discord_register_desktop_entry_ours("[Desktop Entry]\nComment=X-Chupathingy=true\n"));
	CHECK(!discord_register_desktop_entry_ours(""));
}

static void test_desktop_file(const char *directory)
{
	char path[1024], text[4096], name[128];
	static const char other[] = "[Desktop Entry]\nName=Game 1560397272166957136\nExec=/usr/bin/other %u\n";

	snprintf(path, sizeof(path), "%s/discord-%s.desktop", directory, application);
	remove(path);

	/* none there: written, then unchanged */
	CHECK(discord_register_desktop_file(directory, application, "/opt/game/chupathingy", name, sizeof(name)) ==
		DISCORD_REGISTER_WRITTEN);
	CHECK(!strcmp(name, "discord-1560397272166957136.desktop"));
	CHECK(read_file(path, text, sizeof(text)));
	CHECK(strstr(text, "Exec=\"/opt/game/chupathingy\" %u"));
	CHECK(discord_register_desktop_file(directory, application, "/opt/game/chupathingy", name, sizeof(name)) ==
		DISCORD_REGISTER_UNCHANGED);

	/* the game moved (an AppImage elsewhere): its own entry rewritten */
	CHECK(discord_register_desktop_file(directory, application, "/home/me/Chupathingy.AppImage", name, sizeof(name)) ==
		DISCORD_REGISTER_WRITTEN);
	CHECK(read_file(path, text, sizeof(text)));
	CHECK(strstr(text, "Exec=\"/home/me/Chupathingy.AppImage\" %u"));
	CHECK(!strstr(text, "/opt/game"));

	/* another program's entry: left as it is */
	write_file(path, other);
	CHECK(discord_register_desktop_file(directory, application, "/opt/game/chupathingy", name, sizeof(name)) ==
		DISCORD_REGISTER_LEFT);
	CHECK(read_file(path, text, sizeof(text)));
	CHECK(!strcmp(text, other));

	/* no folder */
	CHECK(discord_register_desktop_file("", application, "/opt/game/chupathingy", name, sizeof(name)) ==
		DISCORD_REGISTER_FAILED);
	snprintf(text, sizeof(text), "%s/no-such-folder", directory);
	CHECK(discord_register_desktop_file(text, application, "/opt/game/chupathingy", name, sizeof(name)) ==
		DISCORD_REGISTER_FAILED);
	remove(path);
}

/* ---------- a fake registry */

enum
{
	MAXIMUM_VALUES = 16,
};

static struct
{
	int count;
	int sets;
	int fail_sets;
	struct
	{
		char key[192];
		char name[64];
		char value[1200];
	} values[MAXIMUM_VALUES];
} registry_values;

static int value_index(const char *key, const char *name)
{
	int index;

	for (index = 0; index < registry_values.count; index++)
	{
		if (!strcmp(registry_values.values[index].key, key) &&
			!strcmp(registry_values.values[index].name, name ? name : ""))
		{
			return index;
		}
	}
	return -1;
}

static int fake_get(void *context, const char *key, const char *name, char *value, int size)
{
	int index = value_index(key, name);

	CHECK(context == &registry_values);
	if (index < 0)
		return 0;
	snprintf(value, (size_t)size, "%s", registry_values.values[index].value);
	return 1;
}

static int fake_set(void *context, const char *key, const char *name, const char *value)
{
	int index = value_index(key, name);

	CHECK(context == &registry_values);
	registry_values.sets++;
	if (registry_values.fail_sets)
		return 0;
	if (index < 0)
	{
		if (registry_values.count == MAXIMUM_VALUES)
			return 0;
		index = registry_values.count++;
		snprintf(registry_values.values[index].key, sizeof(registry_values.values[index].key), "%s", key);
		snprintf(registry_values.values[index].name, sizeof(registry_values.values[index].name), "%s",
			name ? name : "");
	}
	snprintf(registry_values.values[index].value, sizeof(registry_values.values[index].value), "%s", value);
	return 1;
}

static const char *fake_value(const char *key, const char *name)
{
	int index = value_index(key, name);

	return index < 0 ? NULL : registry_values.values[index].value;
}

static void test_windows(void)
{
	static const struct discord_registry registry = { &registry_values, fake_get, fake_set };
	static const char key[] = "Software\\Classes\\discord-1560397272166957136";
	static const char command_key[] = "Software\\Classes\\discord-1560397272166957136\\shell\\open\\command";
	const char *value;

	memset(&registry_values, 0, sizeof(registry_values));
	/* none there: the SDK's keys, with the link as the argument */
	CHECK(discord_register_windows(&registry, application, "C:\\Games\\Chupathingy\\chupathingy.exe") ==
		DISCORD_REGISTER_WRITTEN);
	value = fake_value(key, NULL);
	CHECK(value && !strcmp(value, "URL:Run game 1560397272166957136 protocol"));
	value = fake_value(key, "URL Protocol");
	CHECK(value && !strcmp(value, ""));
	value = fake_value(key, "Chupathingy");
	CHECK(value != NULL);
	value = fake_value("Software\\Classes\\discord-1560397272166957136\\DefaultIcon", NULL);
	CHECK(value && !strcmp(value, "C:\\Games\\Chupathingy\\chupathingy.exe"));
	value = fake_value(command_key, NULL);
	CHECK(value && !strcmp(value, "\"C:\\Games\\Chupathingy\\chupathingy.exe\" \"%1\""));

	/* again: unchanged, nothing written */
	registry_values.sets = 0;
	CHECK(discord_register_windows(&registry, application, "C:\\Games\\Chupathingy\\chupathingy.exe") ==
		DISCORD_REGISTER_UNCHANGED);
	CHECK(registry_values.sets == 0);

	/* the game moved: its own keys rewritten */
	CHECK(discord_register_windows(&registry, application, "D:\\Chupathingy\\chupathingy.exe") ==
		DISCORD_REGISTER_WRITTEN);
	value = fake_value(command_key, NULL);
	CHECK(value && !strcmp(value, "\"D:\\Chupathingy\\chupathingy.exe\" \"%1\""));

	/* another program's (no mark): left as it is */
	memset(&registry_values, 0, sizeof(registry_values));
	fake_set(&registry_values, command_key, NULL, "\"C:\\Other\\game.exe\"");
	registry_values.sets = 0;
	CHECK(discord_register_windows(&registry, application, "C:\\Games\\Chupathingy\\chupathingy.exe") ==
		DISCORD_REGISTER_LEFT);
	CHECK(registry_values.sets == 0);
	value = fake_value(command_key, NULL);
	CHECK(value && !strcmp(value, "\"C:\\Other\\game.exe\""));

	/* a key with no command (an empty one): taken */
	memset(&registry_values, 0, sizeof(registry_values));
	fake_set(&registry_values, key, NULL, "URL:something");
	CHECK(discord_register_windows(&registry, application, "C:\\Games\\chupathingy.exe") == DISCORD_REGISTER_WRITTEN);

	/* failures, and what is refused */
	memset(&registry_values, 0, sizeof(registry_values));
	registry_values.fail_sets = 1;
	CHECK(discord_register_windows(&registry, application, "C:\\Games\\chupathingy.exe") == DISCORD_REGISTER_FAILED);
	registry_values.fail_sets = 0;
	CHECK(discord_register_windows(&registry, "12\\..\\x", "C:\\Games\\chupathingy.exe") == DISCORD_REGISTER_FAILED);
	CHECK(discord_register_windows(&registry, application, "") == DISCORD_REGISTER_FAILED);
	CHECK(discord_register_windows(&registry, application, "C:\\a\"b.exe") == DISCORD_REGISTER_FAILED);
	CHECK(discord_register_windows(NULL, application, "C:\\a.exe") == DISCORD_REGISTER_FAILED);
	CHECK(registry_values.count == 0);
}

static void test_old_json(void)
{
	/* the one an earlier version wrote goes; the SDK's own forms stay */
	CHECK(discord_register_old_json_ours("{\"command\": \"open -b io.github.mrmilenko.halo-ce-macos\"}\n"));
	CHECK(!discord_register_old_json_ours("{\"command\": \"steam://rungameid/1234\"}"));
	CHECK(!discord_register_old_json_ours("{\"command\": \"discord-1560397272166957136://\"}"));
	CHECK(!discord_register_old_json_ours(""));
	CHECK(!discord_register_old_json_ours(NULL));
}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		printf("usage: %s <scratch folder>\n", argv[0]);
		return 2;
	}
	test_desktop_entry();
	test_desktop_file(argv[1]);
	test_windows();
	test_old_json();
	if (failures)
		printf("%d failed\n", failures);
	else
		printf("ok\n");
	return failures ? 1 : 0;
}
