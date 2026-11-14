/*
GAMEPAD_TAPS.C (test)

A controller's taps (port/linux/src/sdl_platform.c's gamepad presses, which
its event pump records, read by port/linux/src/xinput_sdl.c's
gamepad_button_down and gamepad_trigger): a button or trigger pressed and let
go of between two readings is seen at the next reading once, and not held at
the one after; each gamepad's are its own; a removed gamepad's and those the
window's focus loss drops are not seen. Based on the OpenH1 team's test
(g0atbutt, OpenH1 0473c477). test_gamepad_taps.py takes the code.
*/

#include "harness.h"

typedef int BOOL;
typedef unsigned int Uint32;
typedef short Sint16;

#include "config.inc"

/* a gamepad as SDL holds it now: its buttons down, its axes */
typedef struct
{
	unsigned char buttons[SDL_GAMEPAD_BUTTON_COUNT];
	Sint16 axes[SDL_GAMEPAD_AXIS_COUNT];
} SDL_Gamepad;

static BOOL SDL_GetGamepadButton(SDL_Gamepad *gamepad, SDL_GamepadButton button)
{
	return gamepad->buttons[button] != 0;
}

static Sint16 SDL_GetGamepadAxis(SDL_Gamepad *gamepad, SDL_GamepadAxis axis)
{
	return gamepad->axes[axis];
}

#include "under_test.inc"

/* a reading of a button and the right trigger, as sdl_gamepad_state takes them */
static void reading(SDL_Gamepad *gamepad, Uint32 id, SDL_GamepadButton button, BOOL *down, int *trigger)
{
	struct gamepad_taps taps;

	gamepad_presses_take(id, &taps.buttons, &taps.triggers);
	*down = gamepad_button_down(gamepad, &taps, button);
	*trigger = gamepad_trigger(gamepad, &taps, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 2);
}

int main(int argc, char **argv)
{
	const char *case_name = argv[1];
	SDL_Gamepad pad, other;
	BOOL down;
	int trigger;

	memset(&pad, 0, sizeof(pad));
	memset(&other, 0, sizeof(other));
	memset(gamepad_presses, 0, sizeof(gamepad_presses));
	CASE("tap")
	{
		/* A down and up before the reading */
		gamepad_button_pressed(7, SDL_GAMEPAD_BUTTON_SOUTH);
		reading(&pad, 7, SDL_GAMEPAD_BUTTON_SOUTH, &down, &trigger);
		CHECK(down, "the tap seen");
		reading(&pad, 7, SDL_GAMEPAD_BUTTON_SOUTH, &down, &trigger);
		CHECK(!down, "the tap not held");
		/* held: seen at every reading */
		pad.buttons[SDL_GAMEPAD_BUTTON_SOUTH] = 1;
		reading(&pad, 7, SDL_GAMEPAD_BUTTON_SOUTH, &down, &trigger);
		CHECK(down, "a held button seen");
	}
	CASE("trigger")
	{
		/* a pull and release before the reading: fully pulled once */
		gamepad_trigger_moved(7, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 30000);
		gamepad_trigger_moved(7, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
		reading(&pad, 7, SDL_GAMEPAD_BUTTON_SOUTH, &down, &trigger);
		CHECK(trigger == 255, "the pull seen: %d", trigger);
		reading(&pad, 7, SDL_GAMEPAD_BUTTON_SOUTH, &down, &trigger);
		CHECK(trigger == 0, "the pull not held: %d", trigger);
		/* held half way: its own depth, not a full pull */
		gamepad_trigger_moved(7, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 16000);
		gamepad_trigger_moved(7, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 16500);
		pad.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 16500;
		reading(&pad, 7, SDL_GAMEPAD_BUTTON_SOUTH, &down, &trigger);
		CHECK(trigger == 16500 * 255 / 32767, "a held trigger keeps its depth: %d", trigger);
		/* moving while held is not a new pull */
		pad.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 0;
		gamepad_trigger_moved(7, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 17000);
		reading(&pad, 7, SDL_GAMEPAD_BUTTON_SOUTH, &down, &trigger);
		CHECK(trigger == 0, "a move while held counted as a pull: %d", trigger);
	}
	CASE("ports")
	{
		gamepad_button_pressed(7, SDL_GAMEPAD_BUTTON_NORTH);
		reading(&other, 8, SDL_GAMEPAD_BUTTON_NORTH, &down, &trigger);
		CHECK(!down, "another gamepad's tap seen");
		reading(&pad, 7, SDL_GAMEPAD_BUTTON_NORTH, &down, &trigger);
		CHECK(down, "its own tap seen");
	}
	CASE("stale")
	{
		/* a tap on a pad nobody read for many pumps: let go */
		gamepad_button_pressed(7, SDL_GAMEPAD_BUTTON_SOUTH);
		gamepad_pumps += GAMEPAD_TAP_PUMPS + 1;
		reading(&pad, 7, SDL_GAMEPAD_BUTTON_SOUTH, &down, &trigger);
		CHECK(!down, "a stale tap fired");
		/* one read within the frame's pumps still counts */
		gamepad_button_pressed(7, SDL_GAMEPAD_BUTTON_SOUTH);
		gamepad_pumps += 3;
		reading(&pad, 7, SDL_GAMEPAD_BUTTON_SOUTH, &down, &trigger);
		CHECK(down, "a fresh tap lost");
	}
	CASE("dropped")
	{
		gamepad_button_pressed(7, SDL_GAMEPAD_BUTTON_EAST);
		gamepad_presses_forget(7);
		reading(&pad, 7, SDL_GAMEPAD_BUTTON_EAST, &down, &trigger);
		CHECK(!down, "a removed gamepad's tap seen");
		gamepad_button_pressed(7, SDL_GAMEPAD_BUTTON_EAST);
		gamepad_presses_clear();
		reading(&pad, 7, SDL_GAMEPAD_BUTTON_EAST, &down, &trigger);
		CHECK(!down, "a tap seen after the focus was lost");
	}
	return 0;
}
