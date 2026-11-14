/*
OPENCE_WELCOME.C

A host's one line to each OpenCE player: its rules (opence_welcome.h).

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "opence_welcome.h"

#include <string.h>

/* ---------- globals */

char const opence_welcome_default_text[] = "Welcome! This server plays best in Chupathingy: chupathingy.org";

/* ---------- public code */

void opence_welcome_reset(
	struct opence_welcome *welcome)
{
	memset(welcome, 0, sizeof(*welcome));
}

int opence_welcome_due(
	struct opence_welcome *welcome,
	int machine,
	int present,
	int kind,
	int spawned,
	unsigned long now)
{
	if (machine < 0 || machine >= OPENCE_WELCOME_MACHINES)
		return 0;
	/* (gone: its next join is a new one) */
	if (!present)
	{
		welcome->done[machine] = 0;
		welcome->waiting[machine] = 0;
		return 0;
	}
	if (welcome->done[machine])
		return 0;
	/* (a Delta machine never: not even if its session drops later) */
	if (kind == _opence_welcome_delta)
	{
		welcome->done[machine] = 1;
		return 0;
	}
	if (kind != _opence_welcome_opence)
		return 0;
	if (!welcome->waiting[machine])
	{
		if (!spawned)
			return 0;
		welcome->waiting[machine] = 1;
		welcome->spawned_at[machine] = now;
	}
	if (now - welcome->spawned_at[machine] < OPENCE_WELCOME_DELAY)
		return 0;
	welcome->done[machine] = 1;
	welcome->waiting[machine] = 0;
	return 1;
}

int opence_welcome_text_valid(
	char const *text)
{
	size_t index, length;

	if (!text)
		return 0;
	length = strlen(text);
	if (!length || length > OPENCE_WELCOME_TEXT_LENGTH)
		return 0;
	for (index = 0; index < length; index++)
	{
		unsigned char character = (unsigned char)text[index];

		if (character < 0x20 || character > 0x7e || character == '|')
			return 0;
	}
	return 1;
}
