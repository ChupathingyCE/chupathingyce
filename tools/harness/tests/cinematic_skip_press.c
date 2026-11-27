/*
CINEMATIC_SKIP_PRESS.C (test)

A cinematic's skip (source/game/player_control.c's
player_control_skip_press_new): a press skips once, and a button held on past
the revert that skip makes skips nothing more until it is let go. a10's start
checkpoint is its opening cinematic's, so a revert makes the cinematic
skippable again, and a held A reverted on every tick. The player's accept
drops out on some frames while the button is still down, which is not a let
go. test_cinematic_skip_press.py takes the code.
*/

#include "harness.h"

#define MAXIMUM_GAMEPADS 4
enum { _gamepad_analog_button_a, NUMBER_OF_GAMEPAD_BUTTONS };
struct gamepad_state { byte buttons[NUMBER_OF_GAMEPAD_BUTTONS]; };

/* the world: the controllers' A, the keyboard's accept, the reverts */
static struct gamepad_state gamepads[MAXIMUM_GAMEPADS];
static boolean gamepad_present[MAXIMUM_GAMEPADS];
static byte keyboard_accept[MAXIMUM_GAMEPADS];
static unsigned long reverts;

static boolean input_has_gamepad(short gamepad_index) { return gamepad_present[gamepad_index]; }
static struct gamepad_state const *input_get_gamepad_state(short gamepad_index) { return &gamepads[gamepad_index]; }
static byte input_abstraction_port_accept(short gamepad_index) { return keyboard_accept[gamepad_index]; }
static unsigned long game_state_port_revert_count(void) { return reverts; }

#include "under_test.inc"

/* a frame of player input: a skip reverts, as main_skip_cinematic does */
static int frame(boolean accept)
{
	if (player_control_skip_press_new(accept))
	{
		reverts++;
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	const char *case_name = argv[1];
	int skips, index;

	gamepad_present[0] = TRUE;
	CASE("press")
	{
		CHECK(!frame(FALSE), "nothing pressed");
		gamepads[0].buttons[_gamepad_analog_button_a] = 1;
		CHECK(frame(TRUE), "a press skips");
	}
	CASE("held")
	{
		frame(FALSE);
		gamepads[0].buttons[_gamepad_analog_button_a] = 1;
		for (skips = 0, index = 0; index < 60; index++)
			skips += frame(TRUE);
		CHECK(skips == 1, "a press held two seconds skips once: %d", skips);
	}
	CASE("flicker")
	{
		/* (the player's accept gone on every other frame, the key down) */
		frame(FALSE);
		keyboard_accept[0] = 1;
		for (skips = 0, index = 0; index < 60; index++)
			skips += frame(index % 2 == 0);
		CHECK(skips == 1, "a key held with the accept dropping out skips once: %d", skips);
	}
	CASE("again")
	{
		frame(FALSE);
		gamepads[0].buttons[_gamepad_analog_button_a] = 1;
		skips = frame(TRUE) + frame(TRUE);
		gamepads[0].buttons[_gamepad_analog_button_a] = 0;
		skips += frame(FALSE);
		gamepads[0].buttons[_gamepad_analog_button_a] = 1;
		skips += frame(TRUE);
		CHECK(skips == 2, "let go and pressed again skips again: %d", skips);
	}
	CASE("other")
	{
		/* (a second controller's A held is still the skip button down) */
		gamepad_present[1] = TRUE;
		frame(FALSE);
		gamepads[1].buttons[_gamepad_analog_button_a] = 1;
		skips = frame(TRUE);
		for (index = 0; index < 10; index++)
			skips += frame(index % 2 == 0);
		CHECK(skips == 1, "the second controller's held A skips once: %d", skips);
	}
	return 0;
}
