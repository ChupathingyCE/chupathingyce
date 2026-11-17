/*
GAME_LIST_RULES.H

Which listed games the game lists show (Online Games, browser_screen.c,
and the Server Browser, menu_functions.c): only games this build can join.
A game is shown when:

- its host speaks Delta and so does this build: the host's game wire, in
  its advertisement, decides on joining (network_game_client_advertised_
  game_compatible), whatever number it announces for OpenCE's builds; or
- its network version is one this build joins (delta_legacy_minimum to
  delta_legacy_maximum: the signed Delta table's range, else the built-in
  one, halo_port_limits.h).

A GOAT H1 host closed to other builds advertises its version with bit 15
set (GAME_LIST_CLOSED_VERSION): never shown. Neither is a game of a newer
or older number than the range, nor one that says no number (0: a host
from before there were numbers). debug.show_all_games shows them all again,
for tests. The site's list hides the same games (puma's game_list_rules).

A game heard only on the brokers (OpenCE's public listings, p2p_lobby.c)
is shown only when its number is GAME_LIST_BROKER_MINIMUM_VERSION or newer
(game_list_broker_listed): older builds' listings are left out of the
lists, though a game of theirs is still joined by its invite. A game whose
name is on the blocked names list (server_names.h) is never shown.

Header only, no game dependency; tested by port/linux/tests/
game_list_rules_test.c (tools/test_game_list_rules.py).

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef GAME_LIST_RULES_H
#define GAME_LIST_RULES_H

/* the bit a GOAT H1 host sets in its advertised version while its game is
closed to other builds (network.allow_opence off) */
#define GAME_LIST_CLOSED_VERSION 0x8000u

/* the oldest network version a broker listing is shown under (the site
lists the same: puma's game_list_rules.BROKER_MINIMUM_VERSION) */
#define GAME_LIST_BROKER_MINIMUM_VERSION 35u

/* whether a lists' game is one this build can join, and so shown: version,
the number its host advertises (a list's, or a broker listing's); delta,
nonzero when its host speaks Delta and this build does too; minimum and
maximum, the numbers this build joins */
static inline int game_list_joinable(unsigned int version, int delta, int minimum, int maximum)
{
	if (version & GAME_LIST_CLOSED_VERSION)
		return 0;
	if (delta)
		return 1;
	return version != 0 && (int)version >= minimum && (int)version <= maximum;
}

/* whether a game heard only on the brokers is listed, by the version its
listing is under */
static inline int game_list_broker_listed(unsigned int version)
{
	return (version & ~GAME_LIST_CLOSED_VERSION) >= GAME_LIST_BROKER_MINIMUM_VERSION;
}

#endif
