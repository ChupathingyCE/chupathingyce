/*
HALO_XBOX_PAD_BUTTONS.H

The original Xbox controllers (the Duke and the Controller S) on Linux's xpad
driver report their black and white buttons as BTN_C and BTN_Z, which SDL's
gamepad mapping leaves out (it only knows shoulder buttons). SDL numbers a
joystick's buttons in the order of their evdev codes, so on these pads that
is: A 0, B 1, BTN_C (black) 2, X 3, Y 4, BTN_Z (white) 5. Macros and one
function only; plain C89.
*/

#ifndef HALO_XBOX_PAD_BUTTONS_H
#define HALO_XBOX_PAD_BUTTONS_H

#define XBOX_PAD_VENDOR 0x045e
#define XBOX_PAD_BLACK_JOYSTICK_BUTTON 2
#define XBOX_PAD_WHITE_JOYSTICK_BUTTON 5
#define XBOX_PAD_MINIMUM_JOYSTICK_BUTTONS 6

/* an original Xbox pad (xpad's X-Box pad v1 US, Japan, Controller S, pad US) */
static int xbox_pad_has_black_white_buttons(unsigned int vendor, unsigned int product)
{
	return vendor == XBOX_PAD_VENDOR &&
		(product == 0x0202 || product == 0x0285 || product == 0x0287 || product == 0x0289);
}

#endif
