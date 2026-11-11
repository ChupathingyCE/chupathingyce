/*
LINK_REGISTER_TEST.C

Tests of the handler of halo:// links (port/linux/src/link_register.c): the
desktop entry written into a scratch folder (as
~/.local/share/applications), the user's default read from a mimeapps.list,
and the Windows registry's keys through a fake registry. Another program's
handler is never replaced. Built and run by tools/test_link_register.py
with a scratch folder as its argument; exits nonzero on a failure.
*/

#include "../src/link_register.h"

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

static const char game[] = "/opt/Chupathingy/chupathingy";

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

/* a fake file system: the programs that are there */
static const char *const present[] = {
	"/opt/Chupathingy/chupathingy",
	"/usr/bin/other-game",
	"/home/me/Apps/OpenCE.AppImage",
	"C:\\Games\\Chupathingy\\chupathingy.exe",
	"C:\\Games\\Chupathingy\\halo.exe",
	"C:\\Games\\Other\\halo.exe",
};

static int fake_exists(const char *path)
{
	int index;

	for (index = 0; index < (int)(sizeof(present) / sizeof(*present)); index++)
	{
		if (!strcmp(present[index], path))
			return 1;
	}
	return 0;
}

static void test_desktop_entry(void)
{
	char entry[4096], named[1200];

	CHECK(link_register_desktop_entry(entry, sizeof(entry), game) > 0);
	CHECK(strstr(entry, "[Desktop Entry]\n") == entry);
	CHECK(strstr(entry, "\nName=Chupathingy\n"));
	CHECK(strstr(entry, "\nExec=\"/opt/Chupathingy/chupathingy\" %u\n"));
	CHECK(strstr(entry, "\nMimeType=x-scheme-handler/halo;\n"));
	CHECK(strstr(entry, "\nNoDisplay=true\n"));
	CHECK(strstr(entry, "\nX-Chupathingy=true\n"));
	CHECK(link_register_desktop_entry_ours(entry, game, fake_exists));
	/* (another copy's marked entry is still this game's) */
	CHECK(link_register_desktop_entry_ours(entry, "/elsewhere/chupathingy", fake_exists));
	CHECK(link_register_desktop_exec(entry, named, sizeof(named)) && !strcmp(named, game));

	/* a path's quotes, dollars and percents, quoted as the spec says; a line
	break is refused */
	CHECK(link_register_desktop_entry(entry, sizeof(entry), "/home/a b/$x%1/chupathingy") > 0);
	CHECK(strstr(entry, "\nExec=\"/home/a b/\\\\$x%%1/chupathingy\" %u\n"));
	CHECK(link_register_desktop_entry(entry, sizeof(entry), "/tmp/a\nExec=evil") < 0);
	CHECK(link_register_desktop_entry(entry, sizeof(entry), "") < 0);
	CHECK(link_register_desktop_entry(entry, sizeof(entry), NULL) < 0);
	CHECK(link_register_desktop_entry(entry, 40, game) < 0);

	/* Exec read back: quoted, or not */
	CHECK(link_register_desktop_exec("[Desktop Entry]\nExec=/usr/bin/other-game %u\n", named, sizeof(named)) &&
		!strcmp(named, "/usr/bin/other-game"));
	CHECK(link_register_desktop_exec("[Desktop Entry]\nExec=\"/a b/c\" %u\n", named, sizeof(named)) &&
		!strcmp(named, "/a b/c"));
	CHECK(!link_register_desktop_exec("[Desktop Entry]\nExec=\"/a b/c %u\n", named, sizeof(named)));
	CHECK(!link_register_desktop_exec("[Desktop Entry]\nName=x\n", named, sizeof(named)));
	CHECK(!link_register_desktop_exec(NULL, named, sizeof(named)));
}

static void test_program_ours(void)
{
	/* this executable, another copy by its name, the earlier name in this
	folder, or a program that is gone */
	CHECK(link_register_program_ours(game, game, fake_exists, 0));
	CHECK(link_register_program_ours("/home/me/Chupathingy-x86_64.AppImage", game, fake_exists, 0));
	CHECK(link_register_program_ours("/opt/Chupathingy/halo", game, fake_exists, 0));
	CHECK(link_register_program_ours("/old/place/halo", game, fake_exists, 0));
	CHECK(link_register_program_ours("C:\\Games\\Chupathingy\\halo.exe", "C:\\Games\\Chupathingy\\chupathingy.exe",
		fake_exists, 1));
	CHECK(link_register_program_ours("c:\\games\\chupathingy\\CHUPATHINGY.EXE",
		"C:\\Games\\Chupathingy\\chupathingy.exe", fake_exists, 1));
	/* another program that is there */
	CHECK(!link_register_program_ours("/usr/bin/other-game", game, fake_exists, 0));
	CHECK(!link_register_program_ours("/home/me/Apps/OpenCE.AppImage", game, fake_exists, 0));
	CHECK(!link_register_program_ours("C:\\Games\\Other\\halo.exe", "C:\\Games\\Chupathingy\\chupathingy.exe",
		fake_exists, 1));
	/* (another program in the same folder is not this game's) */
	CHECK(!link_register_program_ours("/usr/bin/other-game", "/usr/bin/chupathingy", fake_exists, 0));
	/* only a whole path can be gone: a bare name is found on PATH */
	CHECK(!link_register_program_ours("opence", game, fake_exists, 0));
	CHECK(!link_register_program_ours("env", game, fake_exists, 0));
	CHECK(!link_register_program_ours("flatpak", game, fake_exists, 0));
	CHECK(link_register_program_ours("/gone/opence", game, fake_exists, 0));
	CHECK(link_register_program_ours("E:\\Gone\\halo.exe", "C:\\Games\\Chupathingy\\chupathingy.exe", fake_exists, 1));
	CHECK(link_register_program_ours("\\\\server\\gone\\opence.exe", "C:\\Games\\Chupathingy\\chupathingy.exe",
		fake_exists, 1));
	/* (and none at all without exists) */
	CHECK(!link_register_program_ours("/gone/opence", game, NULL, 0));
	CHECK(link_register_path_absolute("/a") && link_register_path_absolute("C:\\a") &&
		link_register_path_absolute("c:/a") && link_register_path_absolute("\\\\s\\a"));
	CHECK(!link_register_path_absolute("a") && !link_register_path_absolute("C:a") &&
		!link_register_path_absolute("") && !link_register_path_absolute(NULL));
	/* a path's case counts on Linux */
	CHECK(!link_register_program_ours("/OPT/bin/game", "/opt/bin/game", NULL, 0));
	CHECK(!link_register_program_ours("", game, fake_exists, 0));
	CHECK(!link_register_program_ours(NULL, game, fake_exists, 0));
}

static void test_entry_ours(void)
{
	static const char old_ours[] = "[Desktop Entry]\nType=Application\nName=Halo: Combat Evolved invite\n"
		"Exec=\"/opt/Chupathingy/chupathingy\" %u\nNoDisplay=true\nMimeType=x-scheme-handler/halo;\n";
	static const char old_gone[] = "[Desktop Entry]\nType=Application\nName=Halo: Combat Evolved invite\n"
		"Exec=\"/tmp/.mount_ab12/usr/bin/halo\" %u\nNoDisplay=true\nMimeType=x-scheme-handler/halo;\n";
	static const char old_other[] = "[Desktop Entry]\nType=Application\nName=Halo: Combat Evolved invite\n"
		"Exec=\"/home/me/Apps/OpenCE.AppImage\" %u\nNoDisplay=true\nMimeType=x-scheme-handler/halo;\n";
	static const char other[] = "[Desktop Entry]\nName=Other\nExec=/opt/Chupathingy/chupathingy %u\n"
		"MimeType=x-scheme-handler/halo;\n";

	CHECK(link_register_desktop_entry_ours(old_ours, game, fake_exists));
	CHECK(link_register_desktop_entry_ours(old_gone, game, fake_exists));
	CHECK(!link_register_desktop_entry_ours(old_other, game, fake_exists));
	/* (an entry of another name is another program's, whatever it runs) */
	CHECK(!link_register_desktop_entry_ours(other, game, fake_exists));
	CHECK(!link_register_desktop_entry_ours("", game, fake_exists));
	CHECK(!link_register_desktop_entry_ours(NULL, game, fake_exists));
}

static void test_query_answer(void)
{
	char name[256];

	/* xdg-mime query default's answer: a desktop entry's name, or nothing */
	CHECK(link_register_query_answer("chupathingy-links.desktop\n", name, sizeof(name)) &&
		!strcmp(name, "chupathingy-links.desktop"));
	CHECK(link_register_query_answer("  com.example.OpenCE.desktop;\n", name, sizeof(name)) &&
		!strcmp(name, "com.example.OpenCE.desktop"));
	CHECK(!link_register_query_answer("\n", name, sizeof(name)) && !name[0]);
	CHECK(!link_register_query_answer("", name, sizeof(name)));
	CHECK(!link_register_query_answer(NULL, name, sizeof(name)));
	CHECK(!link_register_query_answer("xdg-mime: no method available\n", name, sizeof(name)));
	CHECK(!link_register_query_answer(".desktop\n", name, sizeof(name)));
	CHECK(!link_register_query_answer("../../x.desktop\n", name, sizeof(name)) && !name[0]);
	CHECK(!link_register_query_answer("a-long-name.desktop\n", name, 8));
}

static void test_desktop_file(const char *directory)
{
	char path[1024], old_path[1024], text[4096];
	int make_default;
	static const char old_ours[] = "[Desktop Entry]\nType=Application\nName=Halo: Combat Evolved invite\n"
		"Exec=\"/opt/Chupathingy/chupathingy\" %u\nNoDisplay=true\nMimeType=x-scheme-handler/halo;\n";
	static const char old_other[] = "[Desktop Entry]\nType=Application\nName=Halo: Combat Evolved invite\n"
		"Exec=\"/home/me/Apps/OpenCE.AppImage\" %u\nNoDisplay=true\nMimeType=x-scheme-handler/halo;\n";
	static const char other[] = "[Desktop Entry]\nName=Other\nExec=/usr/bin/other-game %u\n"
		"MimeType=x-scheme-handler/halo;\n";

	snprintf(path, sizeof(path), "%s/%s", directory, LINK_REGISTER_DESKTOP_NAME);
	snprintf(old_path, sizeof(old_path), "%s/%s", directory, LINK_REGISTER_OLD_DESKTOP_NAME);
	remove(path);
	remove(old_path);

	/* no handler: written, and made the default; then unchanged */
	CHECK(link_register_desktop_file(directory, "", NULL, game, fake_exists, &make_default) ==
		DISCORD_REGISTER_WRITTEN);
	CHECK(make_default);
	CHECK(read_file(path, text, sizeof(text)));
	CHECK(strstr(text, "Exec=\"/opt/Chupathingy/chupathingy\" %u"));
	CHECK(link_register_desktop_file(directory, LINK_REGISTER_DESKTOP_NAME, text, game, fake_exists, &make_default) ==
		DISCORD_REGISTER_UNCHANGED);
	CHECK(!make_default);

	/* the game moved (an AppImage elsewhere): its own entry rewritten */
	CHECK(link_register_desktop_file(directory, LINK_REGISTER_DESKTOP_NAME, text, "/home/me/Chupathingy.AppImage",
		fake_exists, &make_default) == DISCORD_REGISTER_WRITTEN);
	CHECK(!make_default);
	CHECK(read_file(path, text, sizeof(text)));
	CHECK(strstr(text, "Exec=\"/home/me/Chupathingy.AppImage\" %u"));

	/* the user's default is another program's: left, nothing written */
	remove(path);
	CHECK(link_register_desktop_file(directory, "other.desktop", other, game, fake_exists, &make_default) ==
		DISCORD_REGISTER_LEFT);
	CHECK(!make_default);
	CHECK(!read_file(path, text, sizeof(text)));
	/* (an earlier entry of the same name as ours, OpenCE's or another's
	program still there: left too) */
	CHECK(link_register_desktop_file(directory, LINK_REGISTER_OLD_DESKTOP_NAME, old_other, game, fake_exists,
		&make_default) == DISCORD_REGISTER_LEFT);

	/* a default this game can't find (a Flatpak's export, a folder left out
	of XDG_DATA_DIRS) is another program's: left */
	CHECK(link_register_desktop_file(directory, "com.example.OpenCE.desktop", NULL, game, fake_exists,
		&make_default) == DISCORD_REGISTER_LEFT);
	CHECK(!make_default);
	CHECK(!read_file(path, text, sizeof(text)));
	/* unless it has the name of this game's earlier entries: taken */
	CHECK(link_register_desktop_file(directory, LINK_REGISTER_OLD_DESKTOP_NAME, NULL, game, fake_exists,
		&make_default) == DISCORD_REGISTER_WRITTEN);
	CHECK(make_default);
	remove(path);
	/* (an entry that runs a bare name is never taken for gone) */
	CHECK(link_register_desktop_file(directory, LINK_REGISTER_OLD_DESKTOP_NAME,
		"[Desktop Entry]\nName=Halo: Combat Evolved invite\nExec=flatpak run com.example.OpenCE %u\n", game,
		fake_exists, &make_default) == DISCORD_REGISTER_LEFT);

	/* an earlier version's entry of this game's, the default: replaced by
	this one, which is made the default, and the old file goes */
	write_file(old_path, old_ours);
	CHECK(link_register_desktop_file(directory, LINK_REGISTER_OLD_DESKTOP_NAME, old_ours, game, fake_exists,
		&make_default) == DISCORD_REGISTER_WRITTEN);
	CHECK(make_default);
	CHECK(read_file(path, text, sizeof(text)));
	CHECK(!read_file(old_path, text, sizeof(text)));
	remove(path);

	/* another program's earlier-style entry, with no default named (xdg-open
	finds it by its MimeType): left as it is */
	write_file(old_path, old_other);
	CHECK(link_register_desktop_file(directory, "", NULL, game, fake_exists, &make_default) ==
		DISCORD_REGISTER_LEFT);
	CHECK(read_file(old_path, text, sizeof(text)) && !strcmp(text, old_other));
	CHECK(!read_file(path, text, sizeof(text)));
	remove(old_path);

	/* a file of this game's name with no mark is another program's */
	write_file(path, other);
	CHECK(link_register_desktop_file(directory, "", NULL, game, fake_exists, &make_default) ==
		DISCORD_REGISTER_LEFT);
	CHECK(read_file(path, text, sizeof(text)) && !strcmp(text, other));
	remove(path);

	/* no folder */
	CHECK(link_register_desktop_file("", "", NULL, game, fake_exists, &make_default) == DISCORD_REGISTER_FAILED);
	snprintf(text, sizeof(text), "%s/no-such-folder", directory);
	CHECK(link_register_desktop_file(text, "", NULL, game, fake_exists, &make_default) == DISCORD_REGISTER_FAILED);
	CHECK(!make_default);
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
	static const char key[] = "Software\\Classes\\halo";
	static const char command_key[] = "Software\\Classes\\halo\\shell\\open\\command";
	static const char exe[] = "C:\\Games\\Chupathingy\\chupathingy.exe";
	const char *value;

	memset(&registry_values, 0, sizeof(registry_values));
	/* none there: the URL protocol, with the link as the argument */
	CHECK(link_register_windows(&registry, exe, fake_exists) == DISCORD_REGISTER_WRITTEN);
	value = fake_value(key, NULL);
	CHECK(value && !strcmp(value, "URL:Chupathingy link"));
	value = fake_value(key, "URL Protocol");
	CHECK(value && !strcmp(value, ""));
	value = fake_value(key, "Chupathingy");
	CHECK(value != NULL);
	value = fake_value("Software\\Classes\\halo\\DefaultIcon", NULL);
	CHECK(value && !strcmp(value, exe));
	value = fake_value(command_key, NULL);
	CHECK(value && !strcmp(value, "\"C:\\Games\\Chupathingy\\chupathingy.exe\" \"%1\""));

	/* again: unchanged, nothing written */
	registry_values.sets = 0;
	CHECK(link_register_windows(&registry, exe, fake_exists) == DISCORD_REGISTER_UNCHANGED);
	CHECK(registry_values.sets == 0);

	/* the game moved: its own keys rewritten */
	CHECK(link_register_windows(&registry, "D:\\Chupathingy\\chupathingy.exe", fake_exists) == DISCORD_REGISTER_WRITTEN);
	value = fake_value(command_key, NULL);
	CHECK(value && !strcmp(value, "\"D:\\Chupathingy\\chupathingy.exe\" \"%1\""));

	/* our mark, but the key written over by another program that is there
	(OpenCE, as 0.9.0 did, leaves the mark): left */
	memset(&registry_values, 0, sizeof(registry_values));
	CHECK(link_register_windows(&registry, exe, fake_exists) == DISCORD_REGISTER_WRITTEN);
	fake_set(&registry_values, key, NULL, "URL:Halo: Combat Evolved invite");
	fake_set(&registry_values, command_key, NULL, "\"C:\\Games\\Other\\halo.exe\" \"%1\"");
	registry_values.sets = 0;
	CHECK(link_register_windows(&registry, exe, fake_exists) == DISCORD_REGISTER_LEFT);
	CHECK(registry_values.sets == 0);
	value = fake_value(command_key, NULL);
	CHECK(value && !strcmp(value, "\"C:\\Games\\Other\\halo.exe\" \"%1\""));
	/* (marked, another copy of this game's that is there: taken) */
	fake_set(&registry_values, command_key, NULL, "\"C:\\Games\\Chupathingy\\halo.exe\" \"%1\"");
	CHECK(link_register_windows(&registry, exe, fake_exists) == DISCORD_REGISTER_WRITTEN);
	/* (marked, a program that is gone: taken) */
	fake_set(&registry_values, command_key, NULL, "\"E:\\Gone\\opence.exe\" \"%1\"");
	CHECK(link_register_windows(&registry, exe, fake_exists) == DISCORD_REGISTER_WRITTEN);
	/* (an unquoted command's first word, C:\\Program, is not taken for
	gone) */
	fake_set(&registry_values, command_key, NULL, "C:\\Program Files\\OpenCE\\halo.exe \"%1\"");
	CHECK(link_register_windows(&registry, exe, fake_exists) == DISCORD_REGISTER_LEFT);
	/* (nor a bare name) */
	fake_set(&registry_values, command_key, NULL, "opence.exe \"%1\"");
	CHECK(link_register_windows(&registry, exe, fake_exists) == DISCORD_REGISTER_LEFT);

	/* an earlier version's (no mark, its label) for this install's halo.exe:
	taken, and marked */
	memset(&registry_values, 0, sizeof(registry_values));
	fake_set(&registry_values, key, NULL, "URL:Halo: Combat Evolved invite");
	fake_set(&registry_values, command_key, NULL, "\"C:\\Games\\Chupathingy\\halo.exe\" \"%1\"");
	CHECK(link_register_windows(&registry, exe, fake_exists) == DISCORD_REGISTER_WRITTEN);
	value = fake_value(command_key, NULL);
	CHECK(value && !strcmp(value, "\"C:\\Games\\Chupathingy\\chupathingy.exe\" \"%1\""));
	CHECK(fake_value(key, "Chupathingy") != NULL);

	/* an earlier version's for a program that is gone: taken */
	memset(&registry_values, 0, sizeof(registry_values));
	fake_set(&registry_values, key, NULL, "URL:Halo: Combat Evolved invite");
	fake_set(&registry_values, command_key, NULL, "\"E:\\Old\\halo.exe\" \"%1\"");
	CHECK(link_register_windows(&registry, exe, fake_exists) == DISCORD_REGISTER_WRITTEN);

	/* the same label for another program that is there (OpenCE's): left */
	memset(&registry_values, 0, sizeof(registry_values));
	fake_set(&registry_values, key, NULL, "URL:Halo: Combat Evolved invite");
	fake_set(&registry_values, command_key, NULL, "\"C:\\Games\\Other\\halo.exe\" \"%1\"");
	registry_values.sets = 0;
	CHECK(link_register_windows(&registry, exe, fake_exists) == DISCORD_REGISTER_LEFT);
	CHECK(registry_values.sets == 0);
	value = fake_value(command_key, NULL);
	CHECK(value && !strcmp(value, "\"C:\\Games\\Other\\halo.exe\" \"%1\""));

	/* another program's label, even for a program that is gone: left */
	memset(&registry_values, 0, sizeof(registry_values));
	fake_set(&registry_values, key, NULL, "URL:Some other halo");
	fake_set(&registry_values, command_key, NULL, "\"E:\\Gone\\other.exe\" \"%1\"");
	registry_values.sets = 0;
	CHECK(link_register_windows(&registry, exe, fake_exists) == DISCORD_REGISTER_LEFT);
	CHECK(registry_values.sets == 0);

	/* a key with no command: taken */
	memset(&registry_values, 0, sizeof(registry_values));
	fake_set(&registry_values, key, NULL, "URL:something");
	CHECK(link_register_windows(&registry, exe, fake_exists) == DISCORD_REGISTER_WRITTEN);

	/* failures, and what is refused */
	memset(&registry_values, 0, sizeof(registry_values));
	registry_values.fail_sets = 1;
	CHECK(link_register_windows(&registry, exe, fake_exists) == DISCORD_REGISTER_FAILED);
	registry_values.fail_sets = 0;
	CHECK(link_register_windows(&registry, "", fake_exists) == DISCORD_REGISTER_FAILED);
	CHECK(link_register_windows(&registry, "C:\\a\"b.exe", fake_exists) == DISCORD_REGISTER_FAILED);
	CHECK(link_register_windows(NULL, exe, fake_exists) == DISCORD_REGISTER_FAILED);
	CHECK(registry_values.count == 0);
}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		printf("usage: %s <scratch folder>\n", argv[0]);
		return 2;
	}
	test_desktop_entry();
	test_program_ours();
	test_entry_ours();
	test_query_answer();
	test_desktop_file(argv[1]);
	test_windows();
	if (failures)
		printf("%d failed\n", failures);
	else
		printf("ok\n");
	return failures ? 1 : 0;
}
