/*
OPENCE_WELCOME.H

A host's one line to each OpenCE player (a machine with no Delta session)
that this is a Chupathingy game: the parts that need no game to decide.
Delta players never get it; an OpenCE player gets it once a join, a few
seconds after they first spawn, as the host's notice (the game's notice
message, which OpenCE's own servers use for their server messages, and
which OpenCE's client shows in its chat). Off unless the host turns it on:
a dedicated server's opence_welcome, a player's host's
network.opence_welcome. Nothing here has a dependency but the C library;
port/linux/tests/opence_welcome_test.c tests it
(tools/test_opence_welcome.py).

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef __OPENCE_WELCOME_H
#define __OPENCE_WELCOME_H

/* ---------- constants */

enum
{
	/* the machines a game holds (chat.c's) */
	OPENCE_WELCOME_MACHINES = 128,
	/* the line's characters, at most: one line of OpenCE's chat (its chat
	lines' 63) */
	OPENCE_WELCOME_TEXT_LENGTH = 63,
	OPENCE_WELCOME_TEXT_SIZE = OPENCE_WELCOME_TEXT_LENGTH + 1,
	/* how long after a player first spawns the line is sent (milliseconds) */
	OPENCE_WELCOME_DELAY = 3000
};

/* what the host knows a machine to be */
enum
{
	/* not yet known: still in Delta's handshake window, or the host has no
	Delta (nobody can be told apart: nothing is sent) */
	_opence_welcome_unknown,
	/* a Delta session: never sent the line */
	_opence_welcome_delta,
	/* no Delta session after the handshake's window: OpenCE's */
	_opence_welcome_opence
};

/* ---------- types */

struct opence_welcome
{
	/* sent (or never to be: a Delta machine), and waiting since it spawned */
	unsigned char done[OPENCE_WELCOME_MACHINES];
	unsigned char waiting[OPENCE_WELCOME_MACHINES];
	unsigned long spawned_at[OPENCE_WELCOME_MACHINES];
};

/* ---------- prototypes */

/* the line unless the host sets its own */
extern char const opence_welcome_default_text[];

/* every machine forgotten (a new host) */
void opence_welcome_reset(struct opence_welcome *welcome);

/* (each frame, for each machine of the game) whether to send it the line
now: present, whether it has a player in the game; kind, what it is (enum
above); spawned, whether its player is alive in the game now; now, the
milliseconds. 1 once per join, for an OpenCE machine only; a machine whose
players have all left is forgotten, so its next join is told again */
int opence_welcome_due(struct opence_welcome *welcome, int machine, int present, int kind, int spawned,
	unsigned long now);

/* whether a line may be sent as it is: 1 to 63 characters of printable
ASCII without '|' (OpenCE's chat's own rule) */
int opence_welcome_text_valid(char const *text);

#endif
