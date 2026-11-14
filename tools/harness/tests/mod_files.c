/*
MOD_FILES.C (test)

The real checks that stand between a mod's files and the game: the keys a
map's tag names are looked up by in the mods' files
(port/linux/src/port_mods.c) and the bounds of a tag file's counts and
offsets (port/linux/game/mods.c): test_mod_files.py takes the code
(under_test.inc).
*/

#include "harness.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#include "under_test.inc"

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	/* a map's tag name and a file's path make the same key, whatever their
	case and slashes; one that does not fit is none */
	CASE("keys")
	{
		char key[64];
		char small[8];

		CHECK(key_make("Sound/SFX\\Weapons/fire.Sound", key, sizeof(key)), "a name was refused");
		CHECK(!strcmp(key, "sound\\sfx\\weapons\\fire.sound"), "made %s", key);
		CHECK(key_hash("a\\b.sound") == key_hash(key_make("A/B.SOUND", key, sizeof(key)) ? key : ""),
			"the same key hashed otherwise");
		CHECK(!key_make("12345678", small, sizeof(small)), "a key longer than its room was taken");
		CHECK(key_make("1234567", small, sizeof(small)), "the longest key that fits was refused");
		CHECK(names_equal("HD_Audio", "hd_audio"), "a folder named in another case was not found");
		CHECK(!names_equal("hd_audio", "hd_audio2"), "a folder was found by its start");
		return 0;
	}
	/* a count and offset from a file lie within it, however large */
	CASE("bounds")
	{
		CHECK(mods_in_file(10, 5, 15), "the last bytes of the file were refused");
		CHECK(mods_in_file(15, 0, 15), "nothing at the end was refused");
		CHECK(!mods_in_file(10, 6, 15), "a byte past the end was taken");
		CHECK(!mods_in_file(16, 0, 15), "an offset past the end was taken");
		CHECK(!mods_in_file(0xFFFFFFF0UL, 0x20, 0x100), "an offset that wraps was taken");
		CHECK(!mods_in_file(0x10, 0xFFFFFFF8UL, 0x100), "a size that wraps was taken");
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
