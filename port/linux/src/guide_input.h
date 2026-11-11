/*
GUIDE_INPUT.H

The first player's input while the Guide is open (port/linux/game/guide.c):
the platform layer (xinput_sdl.c) hands the Guide what the keyboard and the
first player's controllers hold, and the game gets a controller at rest and
no keys, so nothing reaches the game or the menus under the Guide. Buttons
still held as it closes count as up until let go of. Plain types only: the
game's units and the platform's both include it.
*/

#ifndef GUIDE_INPUT_H
#define GUIDE_INPUT_H

/* what is held (bits): the d-pad (or the left stick, or the arrow keys and
WASD), the face buttons (A: Enter, Space; B: Escape, Backspace; X: M; Y:
Tab), the bumpers (Q and E), Start and Back */
enum
{
	GUIDE_INPUT_UP = 1 << 0,
	GUIDE_INPUT_DOWN = 1 << 1,
	GUIDE_INPUT_LEFT = 1 << 2,
	GUIDE_INPUT_RIGHT = 1 << 3,
	GUIDE_INPUT_A = 1 << 4,
	GUIDE_INPUT_B = 1 << 5,
	GUIDE_INPUT_X = 1 << 6,
	GUIDE_INPUT_Y = 1 << 7,
	GUIDE_INPUT_LEFT_BUMPER = 1 << 8,
	GUIDE_INPUT_RIGHT_BUMPER = 1 << 9,
	GUIDE_INPUT_START = 1 << 10,
	GUIDE_INPUT_BACK = 1 << 11,
};

/* the Guide takes the first player's input (nonzero) or gives it back */
void platform_guide_capture(int capture);
/* what is held now, while it takes the input (0 when it does not) */
unsigned int platform_guide_held(void);
/* the times the first player asked to open or close it since the last
call: its keys (controls.delta: F3, and on a Mac the backslash key too),
their controller's Guide button where SDL reads it (controls.delta_button),
or, with delta.hold_back, Back held DELTA_BACK_HOLD milliseconds in the
menus, which the menus never get (delta_input_rules.h). Not over chat's or
a text field's typing */
int platform_guide_toggles(void);

#endif
