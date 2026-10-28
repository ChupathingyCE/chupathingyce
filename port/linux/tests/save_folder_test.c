/*
SAVE_FOLDER_TEST.C

Tests of the save root's usual place, the one-time copy of the shared
saves and the Halo profiles' player keys (port/linux/src/save_folder.c). Built and run by
tools/test_save_folder.py with a scratch folder as its argument; exits
nonzero on a failure. Everything it writes is under that folder.
*/

#include "../src/save_folder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
static const char *scratch;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

/* scratch/name, in a buffer of the caller's */
static const char *in_scratch(char *path, size_t size, const char *name)
{
	snprintf(path, size, "%s/%s", scratch, name);
	return path;
}

static void make_folder(const char *folder, const char *name)
{
	char path[1024];

	snprintf(path, sizeof(path), "%s/%s", folder, name);
	mkdir(path, 0755);
}

static void write_text(const char *folder, const char *name, const char *text)
{
	char path[1024];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s", folder, name);
	file = fopen(path, "wb");
	if (!file)
	{
		printf("FAIL cannot write %s\n", path);
		failures++;
		return;
	}
	fputs(text, file);
	fclose(file);
}

/* whether folder/name holds text (NULL: whether it is not there) */
static int holds(const char *folder, const char *name, const char *text)
{
	char path[1024];
	char contents[256] = "";
	FILE *file;
	size_t count;

	snprintf(path, sizeof(path), "%s/%s", folder, name);
	file = fopen(path, "rb");
	if (!file)
		return text == NULL;
	count = fread(contents, 1, sizeof(contents) - 1, file);
	contents[count] = '\0';
	fclose(file);
	return text && !strcmp(contents, text);
}

static int is_there(const char *path)
{
	struct stat information;

	return stat(path, &information) == 0;
}

/* a shared folder as the game leaves it: a profile, a saved game, a game
type, the player key, and the map cache */
static void make_shared(const char *shared)
{
	mkdir(shared, 0755);
	make_folder(shared, "u");
	make_folder(shared, "u/UDATA");
	make_folder(shared, "u/UDATA/0123456789AB");
	write_text(shared, "u/UDATA/0123456789AB/SaveMeta.xbx", "profile");
	make_folder(shared, "u/UDATA/0123456789AC");
	write_text(shared, "u/UDATA/0123456789AC/blam.lst", "slayer variant");
	make_folder(shared, "z");
	make_folder(shared, "z/saved");
	write_text(shared, "z/savegame.bin", "checkpoint");
	write_text(shared, "z/lastprof.txt", "last profile");
	write_text(shared, "z/cache000.map", "rebuilt map cache");
	write_text(shared, "game_list_player.key", "key");
}

/* ---------- the usual places */

static void test_linux_default_in_xdg_data_home(void)
{
	char path[256];

	CHECK(save_folder_default(_save_folder_linux, "C:/AppData", "/data", "/home/player", path, sizeof(path)));
	CHECK(!strcmp(path, "/data/chupathingyce"));
	CHECK(save_folder_shared(_save_folder_linux, NULL, "/data", "/home/player", path, sizeof(path)));
	CHECK(!strcmp(path, "/data/halo-linux"));
}

static void test_linux_default_without_xdg(void)
{
	char path[256];

	CHECK(save_folder_default(_save_folder_linux, NULL, "", "/home/player", path, sizeof(path)));
	CHECK(!strcmp(path, "/home/player/.local/share/chupathingyce"));
	CHECK(save_folder_shared(_save_folder_linux, NULL, NULL, "/home/player", path, sizeof(path)));
	CHECK(!strcmp(path, "/home/player/.local/share/halo-linux"));
	CHECK(!save_folder_default(_save_folder_linux, NULL, NULL, NULL, path, sizeof(path)));
	CHECK(!save_folder_default(_save_folder_linux, NULL, "", "", path, sizeof(path)));
}

static void test_windows_default(void)
{
	char path[256];

	CHECK(save_folder_default(_save_folder_windows, "C:\\Users\\Player\\AppData\\Roaming", "/data",
		"/home/player", path, sizeof(path)));
	CHECK(!strcmp(path, "C:\\Users\\Player\\AppData\\Roaming/ChupathingyCE"));
	CHECK(save_folder_shared(_save_folder_windows, "C:\\Roaming", NULL, NULL, path, sizeof(path)));
	CHECK(!strcmp(path, "C:\\Roaming/halo"));
	CHECK(!save_folder_default(_save_folder_windows, NULL, "/data", "/home/player", path, sizeof(path)));
	CHECK(!save_folder_default(_save_folder_windows, "", NULL, NULL, path, sizeof(path)));
}

static void test_too_long(void)
{
	char path[16];

	CHECK(!save_folder_default(_save_folder_linux, NULL, "/a/long/data/home", NULL, path, sizeof(path)));
}

static void test_override_is_used_as_it_is(void)
{
	char root[256], shared[256];

	CHECK(save_folder_choose(_save_folder_linux, "/srv/saves", NULL, "/data", "/home/player",
		root, sizeof(root), shared, sizeof(shared)));
	CHECK(!strcmp(root, "/srv/saves"));
	CHECK(shared[0] == '\0');
	CHECK(save_folder_choose(_save_folder_windows, "D:\\Saves", "C:\\Roaming", NULL, NULL,
		root, sizeof(root), shared, sizeof(shared)));
	CHECK(!strcmp(root, "D:\\Saves"));
	CHECK(shared[0] == '\0');
}

static void test_choose_without_override(void)
{
	char root[256], shared[256];

	CHECK(save_folder_choose(_save_folder_linux, "", NULL, "/data", "/home/player",
		root, sizeof(root), shared, sizeof(shared)));
	CHECK(!strcmp(root, "/data/chupathingyce"));
	CHECK(!strcmp(shared, "/data/halo-linux"));
	CHECK(save_folder_choose(_save_folder_windows, NULL, "C:\\Roaming", NULL, NULL,
		root, sizeof(root), shared, sizeof(shared)));
	CHECK(!strcmp(root, "C:\\Roaming/ChupathingyCE"));
	CHECK(!strcmp(shared, "C:\\Roaming/halo"));
	CHECK(!save_folder_choose(_save_folder_windows, NULL, NULL, "/data", "/home/player",
		root, sizeof(root), shared, sizeof(shared)));
}

/* ---------- the copy */

static void test_copy_into_a_new_save_root(void)
{
	char shared[512], root[512], copying[512];
	unsigned long files = 0;

	make_shared(in_scratch(shared, sizeof(shared), "new/halo-linux"));
	in_scratch(root, sizeof(root), "new/deeper/chupathingyce");
	CHECK(save_folder_copy_once(shared, root, &files) == _save_folder_copy_copied);
	CHECK(files == 5);
	CHECK(holds(root, "u/UDATA/0123456789AB/SaveMeta.xbx", "profile"));
	CHECK(holds(root, "u/UDATA/0123456789AC/blam.lst", "slayer variant"));
	CHECK(holds(root, "z/savegame.bin", "checkpoint"));
	CHECK(holds(root, "z/lastprof.txt", "last profile"));
	CHECK(holds(root, "game_list_player.key", "key"));
	CHECK(holds(root, "z/cache000.map", NULL));
	CHECK(!holds(root, SAVE_FOLDER_MARKER, NULL));
	snprintf(copying, sizeof(copying), "%s.copying", root);
	CHECK(!is_there(copying));
	/* (the shared folder is as it was) */
	CHECK(holds(shared, "u/UDATA/0123456789AB/SaveMeta.xbx", "profile"));
	CHECK(holds(shared, "z/cache000.map", "rebuilt map cache"));
	CHECK(holds(shared, SAVE_FOLDER_MARKER, NULL));

	/* never twice: a profile changed since is kept */
	write_text(root, "u/UDATA/0123456789AB/SaveMeta.xbx", "changed");
	CHECK(save_folder_copy_once(shared, root, &files) == _save_folder_copy_done_before);
	CHECK(files == 0);
	CHECK(holds(root, "u/UDATA/0123456789AB/SaveMeta.xbx", "changed"));
}

static void test_save_root_with_profiles_is_kept(void)
{
	char shared[512], root[512];

	make_shared(in_scratch(shared, sizeof(shared), "kept/halo-linux"));
	in_scratch(root, sizeof(root), "kept/chupathingyce");
	mkdir(root, 0755);
	make_folder(root, "u");
	make_folder(root, "u/UDATA");
	make_folder(root, "u/UDATA/FFFFFFFFFFFF");
	write_text(root, "u/UDATA/FFFFFFFFFFFF/SaveMeta.xbx", "own profile");
	CHECK(save_folder_copy_once(shared, root, NULL) == _save_folder_copy_has_profiles);
	CHECK(holds(root, "u/UDATA/0123456789AB/SaveMeta.xbx", NULL));
	CHECK(holds(root, "game_list_player.key", NULL));
	CHECK(holds(root, "u/UDATA/FFFFFFFFFFFF/SaveMeta.xbx", "own profile"));
	CHECK(!holds(root, SAVE_FOLDER_MARKER, NULL));
	CHECK(save_folder_copy_once(shared, root, NULL) == _save_folder_copy_done_before);
}

static void test_save_root_without_profiles_is_filled_in(void)
{
	char shared[512], root[512];
	unsigned long files = 0;

	make_shared(in_scratch(shared, sizeof(shared), "fill/halo-linux"));
	in_scratch(root, sizeof(root), "fill/chupathingyce");
	mkdir(root, 0755);
	make_folder(root, "z");
	write_text(root, "game_list_player.key", "own key");
	CHECK(save_folder_copy_once(shared, root, &files) == _save_folder_copy_copied);
	CHECK(files == 4);
	CHECK(holds(root, "game_list_player.key", "own key"));
	CHECK(holds(root, "u/UDATA/0123456789AB/SaveMeta.xbx", "profile"));
	CHECK(holds(root, "z/savegame.bin", "checkpoint"));
	CHECK(holds(root, "z/savegame.bin.copying", NULL));
	CHECK(!holds(root, SAVE_FOLDER_MARKER, NULL));
}

static void test_nothing_shared(void)
{
	char shared[512], root[512];

	in_scratch(shared, sizeof(shared), "none/halo-linux");
	in_scratch(root, sizeof(root), "none/chupathingyce");
	CHECK(save_folder_copy_once(shared, root, NULL) == _save_folder_copy_nothing_shared);
	CHECK(is_there(root));
	CHECK(!holds(root, SAVE_FOLDER_MARKER, NULL));
	/* (a shared folder made later is not copied: the check was made) */
	make_shared(shared);
	CHECK(save_folder_copy_once(shared, root, NULL) == _save_folder_copy_done_before);
	CHECK(holds(root, "game_list_player.key", NULL));
}

static void test_interrupted_copy_is_redone(void)
{
	char shared[512], root[512], copying[512];
	unsigned long files = 0;

	make_shared(in_scratch(shared, sizeof(shared), "partial/halo-linux"));
	in_scratch(root, sizeof(root), "partial/chupathingyce");
	/* a copy cut short: its folder, half filled, and no save root */
	snprintf(copying, sizeof(copying), "%s.copying", root);
	mkdir(copying, 0755);
	make_folder(copying, "u");
	write_text(copying, "u/half", "half");
	CHECK(!is_there(root));
	CHECK(save_folder_copy_once(shared, root, &files) == _save_folder_copy_copied);
	CHECK(files == 5);
	CHECK(!is_there(copying));
	CHECK(holds(root, "u/half", NULL));
	CHECK(holds(root, "u/UDATA/0123456789AB/SaveMeta.xbx", "profile"));
	CHECK(!holds(root, SAVE_FOLDER_MARKER, NULL));
}

static void test_failed_copy_leaves_no_save_root(void)
{
	char shared[512], root[512], copying[512], blocked[512];

	make_shared(in_scratch(shared, sizeof(shared), "failed/halo-linux"));
	/* (a file where the save root's folder should be: nothing can be made in it) */
	write_text(scratch, "failed/blocked", "a file");
	in_scratch(blocked, sizeof(blocked), "failed/blocked");
	snprintf(root, sizeof(root), "%s/chupathingyce", blocked);
	CHECK(save_folder_copy_once(shared, root, NULL) == _save_folder_copy_failed);
	snprintf(copying, sizeof(copying), "%s.copying", root);
	CHECK(!is_there(root));
	CHECK(!is_there(copying));
	CHECK(holds(shared, "u/UDATA/0123456789AB/SaveMeta.xbx", "profile"));
}

/* ---------- the player keys of the Halo profiles */

/* a save root with the given profiles (u/UDATA/<id>/blam.sav), a saved game
type beside them, and the install's key when key is not NULL */
static void make_keys_root(const char *root, const char *const *profiles, int count, const char *key)
{
	char name[256];
	int index;

	mkdir(root, 0755);
	make_folder(root, "u");
	make_folder(root, "u/UDATA");
	make_folder(root, "u/UDATA/00000000000F");
	write_text(root, "u/UDATA/00000000000F/blam.lst", "slayer variant");
	for (index = 0; index < count; index++)
	{
		snprintf(name, sizeof(name), "u/UDATA/%s", profiles[index]);
		make_folder(root, name);
		snprintf(name, sizeof(name), "u/UDATA/%s/blam.sav", profiles[index]);
		write_text(root, name, "profile");
	}
	if (key)
		write_text(root, SAVE_FOLDER_PLAYER_KEY, key);
}

static const char *profile_folder(char *path, size_t size, const char *root, const char *id)
{
	snprintf(path, size, "%s/u/UDATA/%s", root, id);
	return path;
}

static int private_file(const char *folder, const char *name)
{
	char path[1024];
	struct stat information;

	snprintf(path, sizeof(path), "%s/%s", folder, name);
	return stat(path, &information) == 0 && !(information.st_mode & 077);
}

static void test_key_path(void)
{
	char path[256];

	CHECK(save_folder_profile_key_path("/s/u/UDATA/AB", path, sizeof(path)));
	CHECK(!strcmp(path, "/s/u/UDATA/AB/" SAVE_FOLDER_PLAYER_KEY));
	CHECK(save_folder_profile_key_path("/s/u/UDATA/AB/", path, sizeof(path)));
	CHECK(!strcmp(path, "/s/u/UDATA/AB/" SAVE_FOLDER_PLAYER_KEY));
	CHECK(!save_folder_profile_key_path("", path, sizeof(path)));
	CHECK(!save_folder_profile_key_path("/a/long/folder", path, 8));
}

/* one profile, never picked: it adopts the install key, which stays */
static void test_one_profile_adopts(void)
{
	static const char *const profiles[] = { "0000000000A1" };
	char root[512], folder[512], key_path[1024];

	make_keys_root(in_scratch(root, sizeof(root), "keys/one"), profiles, 1, "install key");
	profile_folder(folder, sizeof(folder), root, "0000000000A1");
	CHECK(save_folder_player_key(root, "", key_path, sizeof(key_path)));
	CHECK(strstr(key_path, "0000000000A1/" SAVE_FOLDER_PLAYER_KEY) != NULL);
	CHECK(holds(folder, SAVE_FOLDER_PLAYER_KEY, "install key"));
	CHECK(private_file(folder, SAVE_FOLDER_PLAYER_KEY));
	CHECK(holds(root, SAVE_FOLDER_PLAYER_KEY, "install key"));
	CHECK(!holds(root, SAVE_FOLDER_KEY_ADOPTED, NULL));
	/* (once: a second try finds the marker) */
	CHECK(save_folder_adopt_install_key(root, folder) == _save_folder_adopt_done_before);
}

/* several profiles: none adopts it until player 1 has one, then that one
alone does; the others are guests */
static void test_many_profiles_wait_for_player1(void)
{
	static const char *const profiles[] = { "0000000000B1", "0000000000B2", "0000000000B3" };
	char root[512], first[512], second[512], third[512], key_path[1024];

	make_keys_root(in_scratch(root, sizeof(root), "keys/many"), profiles, 3, "install key");
	profile_folder(first, sizeof(first), root, "0000000000B1");
	profile_folder(second, sizeof(second), root, "0000000000B2");
	profile_folder(third, sizeof(third), root, "0000000000B3");
	CHECK(!save_folder_only_profile(root, key_path, sizeof(key_path)));
	CHECK(!save_folder_player_key(root, NULL, key_path, sizeof(key_path)));
	CHECK(!key_path[0]);
	CHECK(holds(root, SAVE_FOLDER_KEY_ADOPTED, NULL));
	CHECK(!save_folder_profile_has_key(first) && !save_folder_profile_has_key(second) &&
		!save_folder_profile_has_key(third));
	/* player 1 picks the second */
	CHECK(save_folder_player_key(root, second, key_path, sizeof(key_path)));
	CHECK(holds(second, SAVE_FOLDER_PLAYER_KEY, "install key"));
	CHECK(!holds(root, SAVE_FOLDER_KEY_ADOPTED, NULL));
	/* then the third: a guest, no key given */
	CHECK(save_folder_player_key(root, third, key_path, sizeof(key_path)));
	CHECK(strstr(key_path, "0000000000B3/" SAVE_FOLDER_PLAYER_KEY) != NULL);
	CHECK(!save_folder_profile_has_key(third));
	CHECK(!save_folder_profile_has_key(first));
	CHECK(holds(root, SAVE_FOLDER_PLAYER_KEY, "install key"));
}

/* no profile yet: the install key waits, and the first profile takes it */
static void test_no_profile_waits(void)
{
	char root[512], folder[512], key_path[1024];

	make_keys_root(in_scratch(root, sizeof(root), "keys/none"), NULL, 0, "install key");
	CHECK(!save_folder_player_key(root, "", key_path, sizeof(key_path)));
	CHECK(save_folder_adopt_install_key(root, NULL) == _save_folder_adopt_waiting);
	CHECK(holds(root, SAVE_FOLDER_KEY_ADOPTED, NULL));
	make_folder(root, "u/UDATA/0000000000C1");
	write_text(root, "u/UDATA/0000000000C1/blam.sav", "profile");
	profile_folder(folder, sizeof(folder), root, "0000000000C1");
	CHECK(save_folder_player_key(root, "", key_path, sizeof(key_path)));
	CHECK(holds(folder, SAVE_FOLDER_PLAYER_KEY, "install key"));
	CHECK(holds(root, SAVE_FOLDER_PLAYER_KEY, "install key"));
}

/* a new install: nothing to move, and every profile starts as a guest */
static void test_new_install_has_guests(void)
{
	static const char *const profiles[] = { "0000000000D1" };
	char root[512], folder[512], key_path[1024];

	make_keys_root(in_scratch(root, sizeof(root), "keys/new"), profiles, 1, NULL);
	profile_folder(folder, sizeof(folder), root, "0000000000D1");
	CHECK(save_folder_adopt_install_key(root, folder) == _save_folder_adopt_no_install_key);
	CHECK(save_folder_player_key(root, folder, key_path, sizeof(key_path)));
	/* (a guest: its key's place, with no key there) */
	CHECK(!save_folder_profile_has_key(folder));
	CHECK(!is_there(key_path));
	CHECK(holds(root, SAVE_FOLDER_PLAYER_KEY, NULL));
	CHECK(holds(root, SAVE_FOLDER_KEY_ADOPTED, NULL));
}

/* a profile with a key of its own keeps it; the install key waits for another */
static void test_profile_key_is_never_replaced(void)
{
	static const char *const profiles[] = { "0000000000E1", "0000000000E2" };
	char root[512], own[512], other[512], key_path[1024];

	make_keys_root(in_scratch(root, sizeof(root), "keys/own"), profiles, 2, "install key");
	profile_folder(own, sizeof(own), root, "0000000000E1");
	profile_folder(other, sizeof(other), root, "0000000000E2");
	write_text(own, SAVE_FOLDER_PLAYER_KEY, "own key");
	CHECK(save_folder_adopt_install_key(root, own) == _save_folder_adopt_profile_has_key);
	CHECK(holds(own, SAVE_FOLDER_PLAYER_KEY, "own key"));
	CHECK(holds(root, SAVE_FOLDER_KEY_ADOPTED, NULL));
	CHECK(save_folder_player_key(root, other, key_path, sizeof(key_path)));
	CHECK(holds(other, SAVE_FOLDER_PLAYER_KEY, "install key"));
}

/* the copy made but its marker not written: found the same, marked, and
never given to a second profile */
static void test_unmarked_copy_is_marked(void)
{
	static const char *const profiles[] = { "0000000000F1", "0000000000F2" };
	char root[512], first[512], second[512], marker[1024];

	make_keys_root(in_scratch(root, sizeof(root), "keys/unmarked"), profiles, 2, "install key");
	profile_folder(first, sizeof(first), root, "0000000000F1");
	profile_folder(second, sizeof(second), root, "0000000000F2");
	CHECK(save_folder_adopt_install_key(root, first) == _save_folder_adopt_adopted);
	snprintf(marker, sizeof(marker), "%s/%s", root, SAVE_FOLDER_KEY_ADOPTED);
	unlink(marker);
	CHECK(save_folder_adopt_install_key(root, first) == _save_folder_adopt_adopted);
	CHECK(!holds(root, SAVE_FOLDER_KEY_ADOPTED, NULL));
	CHECK(save_folder_adopt_install_key(root, second) == _save_folder_adopt_done_before);
	CHECK(!save_folder_profile_has_key(second));
}

/* a renamed profile (a new folder) takes its key along, and the marker
follows it; the profile that adopted the key deleted: the next one adopts
it again */
static void test_renamed_and_deleted_profiles(void)
{
	static const char *const profiles[] = { "000000000011", "000000000012" };
	char root[512], first[512], renamed[512], second[512], path[1024];

	make_keys_root(in_scratch(root, sizeof(root), "keys/rename"), profiles, 2, "install key");
	profile_folder(first, sizeof(first), root, "000000000011");
	profile_folder(second, sizeof(second), root, "000000000012");
	profile_folder(renamed, sizeof(renamed), root, "000000000013");
	CHECK(save_folder_adopt_install_key(root, first) == _save_folder_adopt_adopted);
	/* (a guest renamed: nothing to carry) */
	CHECK(save_folder_profile_key_copy(root, second, renamed));
	CHECK(!is_there(renamed));
	/* the game's rename: a new folder, the saves copied, the old one deleted */
	mkdir(renamed, 0755);
	write_text(renamed, "blam.sav", "profile");
	CHECK(save_folder_profile_key_copy(root, first, renamed));
	CHECK(holds(renamed, SAVE_FOLDER_PLAYER_KEY, "install key"));
	CHECK(private_file(renamed, SAVE_FOLDER_PLAYER_KEY));
	snprintf(path, sizeof(path), "%s/blam.sav", first);
	unlink(path);
	snprintf(path, sizeof(path), "%s/" SAVE_FOLDER_PLAYER_KEY, first);
	unlink(path);
	rmdir(first);
	CHECK(save_folder_adopt_install_key(root, second) == _save_folder_adopt_done_before);
	CHECK(!save_folder_profile_has_key(second));
	/* (a key already in the new folder is never replaced) */
	CHECK(!save_folder_profile_key_copy(root, renamed, renamed));
	/* the renamed profile deleted: the install key, kept, goes to the next */
	snprintf(path, sizeof(path), "%s/blam.sav", renamed);
	unlink(path);
	snprintf(path, sizeof(path), "%s/" SAVE_FOLDER_PLAYER_KEY, renamed);
	unlink(path);
	rmdir(renamed);
	CHECK(save_folder_adopt_install_key(root, second) == _save_folder_adopt_adopted);
	CHECK(holds(second, SAVE_FOLDER_PLAYER_KEY, "install key"));
	CHECK(holds(root, SAVE_FOLDER_PLAYER_KEY, "install key"));
}

int main(int argc, char **argv)
{
	char path[512];

	if (argc != 2)
	{
		printf("usage: save_folder_test <scratch folder>\n");
		return 2;
	}
	scratch = argv[1];
	mkdir(in_scratch(path, sizeof(path), "new"), 0755);
	mkdir(in_scratch(path, sizeof(path), "kept"), 0755);
	mkdir(in_scratch(path, sizeof(path), "fill"), 0755);
	mkdir(in_scratch(path, sizeof(path), "none"), 0755);
	mkdir(in_scratch(path, sizeof(path), "partial"), 0755);
	mkdir(in_scratch(path, sizeof(path), "failed"), 0755);
	mkdir(in_scratch(path, sizeof(path), "keys"), 0755);

	test_linux_default_in_xdg_data_home();
	test_linux_default_without_xdg();
	test_windows_default();
	test_too_long();
	test_override_is_used_as_it_is();
	test_choose_without_override();
	test_copy_into_a_new_save_root();
	test_save_root_with_profiles_is_kept();
	test_save_root_without_profiles_is_filled_in();
	test_nothing_shared();
	test_interrupted_copy_is_redone();
	test_failed_copy_leaves_no_save_root();
	test_key_path();
	test_one_profile_adopts();
	test_many_profiles_wait_for_player1();
	test_no_profile_waits();
	test_new_install_has_guests();
	test_profile_key_is_never_replaced();
	test_unmarked_copy_is_marked();
	test_renamed_and_deleted_profiles();

	if (failures)
	{
		printf("%d failures\n", failures);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
