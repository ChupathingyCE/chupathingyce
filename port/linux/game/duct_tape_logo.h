/*
DUCT_TAPE_LOGO.H

The Chupathingy menus' main menu logo, taped over: the Konami code (Up Up
Down Down Left Right Left Right B A) on the main menu swaps the Halo logo
for the site's duct tape one and back, and display.duct_tape_logo keeps the
choice. The swap takes its time: the logo sinks a little, slowly, as if
something big were coming, and then the other one is just there, with one
small click. The parts that need no game to decide: the code's sequence
and the swap's timing. Nothing here has a dependency but the C library;
port/linux/tests/duct_tape_logo_test.c tests it
(tools/test_duct_tape_logo.py).

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef __DUCT_TAPE_LOGO_H
#define __DUCT_TAPE_LOGO_H

/* ---------- constants */

/* a press the code is made of (anything else: _konami_other) */
enum
{
	_konami_up,
	_konami_down,
	_konami_left,
	_konami_right,
	_konami_b,
	_konami_a,
	_konami_other,

	/* the code's presses */
	KONAMI_CODE_LENGTH = 10
};

enum
{
	/* which logo shows (duct_tape_logo_step's frame): the Halo logo
	alone, or the tape over it (chupathingy/main_menu.xml) */
	DUCT_TAPE_LOGO_HALO_FRAME = 0,
	DUCT_TAPE_LOGO_TAPE_FRAME = 1,
	/* the swap's build-up, in milliseconds, and how far the logo sinks
	in it, in menu units (of 480) */
	DUCT_TAPE_LOGO_BUILD_UP = 2400,
	DUCT_TAPE_LOGO_DROP = 10
};

/* ---------- types */

struct konami_code
{
	/* the presses of the code matched so far */
	int matched;
};

struct duct_tape_logo
{
	/* the logo shown (1 the duct tape) and the one being swapped to */
	int shown;
	int target;
	/* whether a swap is building up, and since when (milliseconds) */
	int swapping;
	unsigned long started;
};

/* ---------- prototypes */

void konami_code_reset(struct konami_code *code);

/* whether the menus should not get this press: the code's B and A, once the
eight arrows before them are in (the main menu's B asks to quit, its A
opens the row with the focus) */
int konami_code_swallows(struct konami_code const *code, int press);

/* a press (enum above) in: 1 when it finishes the code (it starts over
then). A wrong press starts it over, from what it still matches: Up Up Up
is still two in */
int konami_code_feed(struct konami_code *code, int press);

/* the logo as the setting has it, with no swap */
void duct_tape_logo_reset(struct duct_tape_logo *logo, int shown);

/* the setting, read each frame: changed by hand, the logo follows at once
(not while a swap is building up: it is going there anyway) */
void duct_tape_logo_sync(struct duct_tape_logo *logo, int setting);

/* the code was entered: swap to the other logo, starting now. The one it
swaps to, for the setting */
int duct_tape_logo_toggle(struct duct_tape_logo *logo, unsigned long now);

/* (each frame) the frame to draw and how far down: 1 on the frame the
swap happens (the click) */
int duct_tape_logo_step(struct duct_tape_logo *logo, unsigned long now, int *frame, int *drop);

#endif
