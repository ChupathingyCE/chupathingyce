/*
CONTROLLER_LAYOUT_TEST.C

Tests of the controller's layouts (port/linux/src/controller_layout.c), for
tools/test_controller_layout.py.

	controller_layout_test                 runs the tests
	controller_layout_test format <text>   prints the layout text reads as,
	                                       written back as config.toml keeps it
*/

#include "controller_layout.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do { if (!(condition)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)

/* every movable action on its own movable button, Start and Back where they are */
static int is_layout(const unsigned char layout[CONTROLLER_ACTION_COUNT])
{
	int action, used = 0;

	for (action = 0; action < CONTROLLER_ACTION_COUNT; action++)
	{
		if (!controller_layout_movable_action(action))
			continue;
		if (!controller_layout_movable_button(layout[action]) || (used & (1 << layout[action])))
			return 0;
		used |= 1 << layout[action];
	}
	return layout[CONTROLLER_ACTION_START] == CONTROLLER_BUTTON_START &&
		layout[CONTROLLER_ACTION_BACK] == CONTROLLER_BUTTON_BACK;
}

static void test_presets(void)
{
	unsigned char layout[CONTROLLER_ACTION_COUNT], standard[CONTROLLER_ACTION_COUNT];
	int preset;

	controller_layout_preset(CONTROLLER_PRESET_DEFAULT, NULL, standard);
	CHECK(standard[CONTROLLER_ACTION_JUMP] == CONTROLLER_BUTTON_A);
	CHECK(standard[CONTROLLER_ACTION_CROUCH] == CONTROLLER_BUTTON_LEFT_STICK);
	CHECK(standard[CONTROLLER_ACTION_ZOOM] == CONTROLLER_BUTTON_RIGHT_STICK);
	for (preset = 0; preset < CONTROLLER_PRESET_COUNT; preset++)
	{
		controller_layout_preset(preset, "", layout);
		CHECK(is_layout(layout));
	}
	/* (an unknown preset, from a newer profile, and a custom one with no
	layout written: the default) */
	controller_layout_preset(42, NULL, layout);
	CHECK(!memcmp(layout, standard, sizeof(layout)));
	controller_layout_preset(CONTROLLER_PRESET_CUSTOM, "", layout);
	CHECK(!memcmp(layout, standard, sizeof(layout)));
	controller_layout_preset(CONTROLLER_PRESET_BUMPER_JUMPER, NULL, layout);
	CHECK(layout[CONTROLLER_ACTION_JUMP] == CONTROLLER_BUTTON_WHITE);
}

static void test_assign(void)
{
	unsigned char layout[CONTROLLER_ACTION_COUNT];
	int moved;

	controller_layout_preset(CONTROLLER_PRESET_DEFAULT, NULL, layout);
	/* melee onto the left trigger: grenade takes B */
	moved = controller_layout_assign(layout, CONTROLLER_ACTION_MELEE, CONTROLLER_BUTTON_LEFT_TRIGGER);
	CHECK(moved == CONTROLLER_ACTION_GRENADE);
	CHECK(layout[CONTROLLER_ACTION_MELEE] == CONTROLLER_BUTTON_LEFT_TRIGGER);
	CHECK(layout[CONTROLLER_ACTION_GRENADE] == CONTROLLER_BUTTON_B);
	CHECK(is_layout(layout));
	/* Start, Back and the d-pad are not taken; the same button changes nothing */
	CHECK(controller_layout_assign(layout, CONTROLLER_ACTION_JUMP, CONTROLLER_BUTTON_START) == -1);
	CHECK(controller_layout_assign(layout, CONTROLLER_ACTION_JUMP, CONTROLLER_BUTTON_DPAD_UP) == -1);
	CHECK(controller_layout_assign(layout, CONTROLLER_ACTION_BACK, CONTROLLER_BUTTON_A) == -1);
	CHECK(controller_layout_assign(layout, CONTROLLER_ACTION_JUMP, CONTROLLER_BUTTON_A) == -1);
	CHECK(is_layout(layout));
}

static void test_text(void)
{
	unsigned char layout[CONTROLLER_ACTION_COUNT], read[CONTROLLER_ACTION_COUNT];
	char text[256];
	int preset;

	/* each preset's layout written and read back */
	for (preset = 0; preset < CONTROLLER_PRESET_CUSTOM; preset++)
	{
		controller_layout_preset(preset, NULL, layout);
		controller_layout_format(layout, text, sizeof(text));
		CHECK(controller_layout_parse(text, read) == 10);
		CHECK(!memcmp(layout, read, sizeof(layout)));
	}
	/* other names, any case and spacing; nonsense skipped; a layout whatever it says */
	controller_layout_parse(" Jump = white ; MELEE=r1, nothing=A, fire=nowhere, zoom", read);
	CHECK(read[CONTROLLER_ACTION_JUMP] == CONTROLLER_BUTTON_WHITE);
	CHECK(read[CONTROLLER_ACTION_MELEE] == CONTROLLER_BUTTON_BLACK);
	CHECK(read[CONTROLLER_ACTION_FIRE] == CONTROLLER_BUTTON_RIGHT_TRIGGER);
	CHECK(is_layout(read));
	controller_layout_parse("jump=B, melee=B, jump=start, pause=A", read);
	CHECK(is_layout(read));
	controller_layout_format(layout, text, 8);
	CHECK(strlen(text) < 8);
}

static void test_names(void)
{
	CHECK(!strcmp(controller_layout_button_glyph(CONTROLLER_KIND_DUALSENSE, CONTROLLER_BUTTON_BLACK), "R1"));
	CHECK(!strcmp(controller_layout_button_glyph(CONTROLLER_KIND_DUKE, CONTROLLER_BUTTON_WHITE), "White"));
	CHECK(!strcmp(controller_layout_button_glyph(CONTROLLER_KIND_SWITCH_PRO, CONTROLLER_BUTTON_A), "B"));
	CHECK(!strcmp(controller_layout_button_glyph(99, CONTROLLER_BUTTON_A), "A"));
	CHECK(controller_layout_kind(2, "Xbox Wireless Controller") == CONTROLLER_KIND_XBOX);
	CHECK(controller_layout_kind(1, "Xbox 360 Controller") == CONTROLLER_KIND_XBOX_360);
	CHECK(controller_layout_kind(0, "OGX Duke adapter") == CONTROLLER_KIND_DUKE);
	CHECK(controller_layout_kind(1, "Microsoft X-Box pad v2 (US)") == CONTROLLER_KIND_CONTROLLER_S);
	CHECK(controller_layout_kind(5, "DualSense Wireless Controller") == CONTROLLER_KIND_DUALSENSE);
	CHECK(controller_layout_kind(6, "Nintendo Wii U Pro Controller") == CONTROLLER_KIND_WII_U_PRO);
	CHECK(controller_layout_kind(1, "Steam Deck") == CONTROLLER_KIND_STEAM_DECK);
	CHECK(controller_layout_kind(0, NULL) == CONTROLLER_KIND_GENERIC);
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "format"))
	{
		unsigned char layout[CONTROLLER_ACTION_COUNT];
		char text[256];

		controller_layout_parse(argv[2], layout);
		controller_layout_format(layout, text, sizeof(text));
		printf("%s\n", text);
		return 0;
	}
	test_presets();
	test_assign();
	test_text();
	test_names();
	if (!failures)
		printf("ok\n");
	return failures ? 1 : 0;
}
