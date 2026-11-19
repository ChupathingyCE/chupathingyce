/*
DUCT_TAPE_LOGO.C

The main menu's duct tape logo: the Konami code and the swap's timing
(duct_tape_logo.h).

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "duct_tape_logo.h"

#include <string.h>

/* ---------- globals */

static int const konami_code_presses[KONAMI_CODE_LENGTH] =
{
	_konami_up, _konami_up, _konami_down, _konami_down,
	_konami_left, _konami_right, _konami_left, _konami_right,
	_konami_b, _konami_a
};

/* ---------- public code */

void konami_code_reset(
	struct konami_code *code)
{
	code->matched = 0;
}

int konami_code_swallows(
	struct konami_code const *code,
	int press)
{
	return (press == _konami_b || press == _konami_a) &&
		code->matched >= KONAMI_CODE_LENGTH - 2 &&
		code->matched < KONAMI_CODE_LENGTH &&
		konami_code_presses[code->matched] == press;
}

int konami_code_feed(
	struct konami_code *code,
	int press)
{
	int length;

	if (code->matched < 0 || code->matched >= KONAMI_CODE_LENGTH)
		code->matched = 0;
	if (konami_code_presses[code->matched] == press)
	{
		code->matched++;
		if (code->matched == KONAMI_CODE_LENGTH)
		{
			code->matched = 0;
			return 1;
		}
		return 0;
	}
	/* (the longest end of what was pressed that the code starts with) */
	for (length = code->matched; length > 0; length--)
	{
		int start = code->matched + 1 - length;
		int index;
		int same = 1;

		for (index = 0; index < length && same; index++)
		{
			int pressed = start + index < code->matched ? konami_code_presses[start + index] : press;

			same = pressed == konami_code_presses[index];
		}
		if (same)
			break;
	}
	code->matched = length;
	return 0;
}

void duct_tape_logo_reset(
	struct duct_tape_logo *logo,
	int shown)
{
	memset(logo, 0, sizeof(*logo));
	logo->shown = logo->target = shown != 0;
}

void duct_tape_logo_sync(
	struct duct_tape_logo *logo,
	int setting)
{
	if (!logo->swapping)
		logo->shown = logo->target = setting != 0;
}

int duct_tape_logo_toggle(
	struct duct_tape_logo *logo,
	unsigned long now)
{
	logo->target = !logo->target;
	/* (back to the one shown, mid build-up: it just stops) */
	logo->swapping = logo->target != logo->shown;
	logo->started = now;
	return logo->target;
}

int duct_tape_logo_step(
	struct duct_tape_logo *logo,
	unsigned long now,
	int *frame,
	int *drop)
{
	int swapped = 0;

	*drop = 0;
	if (logo->swapping)
	{
		unsigned long elapsed = now - logo->started;

		if (elapsed >= DUCT_TAPE_LOGO_BUILD_UP)
		{
			logo->shown = logo->target;
			logo->swapping = 0;
			swapped = 1;
		}
		else
		{
			/* (slowly at first, a little faster at the end: t squared; at
			most 2400 * 2400 * 10, which fits 32 bits) */
			unsigned long scaled = elapsed * elapsed * DUCT_TAPE_LOGO_DROP;

			*drop = (int)(scaled / ((unsigned long)DUCT_TAPE_LOGO_BUILD_UP * DUCT_TAPE_LOGO_BUILD_UP));
		}
	}
	*frame = logo->shown ? DUCT_TAPE_LOGO_TAPE_FRAME : DUCT_TAPE_LOGO_HALO_FRAME;
	return swapped;
}
