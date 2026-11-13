/*
WORKSHOP_DECISIONS.H

The five open choices of the MCC Workshop's player flow, one switch each,
set to what the flow recommends. Flip one to 0 and rebuild for the other
way; nothing else needs changing.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef WORKSHOP_DECISIONS_H
#define WORKSHOP_DECISIONS_H

/* 1. Items the game's own Workshop got (its own Steam library) always join
the map lists: getting one is the yes. game.workshop_maps (shown as STEAM'S
MCC MAPS, off by default) then covers only the maps in Steam's own MCC
folders. 0: game.workshop_maps covers every Workshop map, as before, and an
item's page shows that row in place of GET and PLAY while it is off. */
#ifndef WORKSHOP_OWN_ITEMS_ALWAYS_LISTED
#define WORKSHOP_OWN_ITEMS_ALWAYS_LISTED 1
#endif

/* 2. A Workshop campaign item plays in its own mission order whatever
game.mcc_campaigns says; the setting stays for loose files in the MCC maps
folder. 0: the mission order only with game.mcc_campaigns on, as before. */
#ifndef WORKSHOP_CAMPAIGN_ORDER_ALWAYS
#define WORKSHOP_CAMPAIGN_ORDER_ALWAYS 1
#endif

/* 3. The join prompt's buttons: YES, ALWAYS, NO (ALWAYS sets MAPS WHEN
JOINING to Always and goes ahead). 0: YES and NO with the DON'T ASK AGAIN
tick box, as before. */
#ifndef WORKSHOP_JOIN_ALWAYS_BUTTON
#define WORKSHOP_JOIN_ALWAYS_BUTTON 1
#endif

/* 4. The old install paths (the Steam client's subscribe, steamcmd's setup
rows and sign-in, CONNECT STEAM) only with debug.workshop_steamcmd on: every
desktop build has the game's own downloader. 0: shown wherever they apply,
as before. */
#ifndef WORKSHOP_OLD_INSTALL_PATHS_DEBUG_ONLY
#define WORKSHOP_OLD_INSTALL_PATHS_DEBUG_ONLY 1
#endif

/* 5. An item Steam itself installed (in a Steam library, not the game's
own): listed as Installed and playable, with OPEN IN STEAM (where it is
unsubscribed) in REMOVE's place; the game never deletes Steam's files.
0: no second button for those. */
#ifndef WORKSHOP_STEAM_ITEMS_OPEN_IN_STEAM
#define WORKSHOP_STEAM_ITEMS_OPEN_IN_STEAM 1
#endif

#endif
