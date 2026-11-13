/*
STUBBS_FUNCTIONS.H

Stubbs the Zombie's script functions, a table of their own after Halo's
(port/linux/game/stubbs_functions.c; Stubbs mode only). A function whose
body belongs to another part of the mode calls that part's hook
(stubbs_campaign.h, stubbs_player.h).

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef STUBBS_FUNCTIONS_H
#define STUBBS_FUNCTIONS_H

/* how many there are (none outside Stubbs mode), one by its index among
them, and a name's index (NONE) (hs.c) */
struct hs_function_definition;
long stubbs_function_count(void);
struct hs_function_definition *stubbs_function_get(long index);
long stubbs_function_find(char const *name);

#endif
