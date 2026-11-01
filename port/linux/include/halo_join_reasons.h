/*
HALO_JOIN_REASONS.H

Why a game can't be joined, in the player's words: one short sentence for
each reason, the same in every place that says it (docs/delta-compatibility.md
section 6). The Xbox-style Online Games list (port/linux/game/browser_screen.c),
the PC Server Browser (port/linux/game/menu_functions.c, server_browser.c), a
failed join (source/networking/network_client_manager.c) and the site's game
list (puma, web/site.js JOIN_REASONS) all use these strings.

Only macros: every build includes it, the consoles too (no source file to
add to their builds). A reason with %s takes the detail named beside it.
Plain C89.
*/

#ifndef HALO_JOIN_REASONS_H
#define HALO_JOIN_REASONS_H

enum join_reason
{
	_join_reason_none = 0,
	/* the host plays a newer version than this machine joins */
	_join_reason_needs_update,
	/* the host plays an older version than this machine joins (or the old
	lockstep netcode) */
	_join_reason_host_older,
	/* the host plays another Chupathingy version, neither clearly newer nor
	older */
	_join_reason_other_version,
	/* the map is not in this machine's folders */
	_join_reason_map_missing,
	/* ... and may be downloaded on joining */
	_join_reason_map_download,
	/* the build plays no maps of the map's family (Halo PC, HaloMD) */
	_join_reason_maps_unsupported,
	/* one of OpenCE's game types this build doesn't have */
	_join_reason_opence_game_type,
	/* Bomb (Grifball) on a build without it */
	_join_reason_needs_bomb,
	/* the game is not taking players */
	_join_reason_not_open,
	_join_reason_full,
	_join_reason_password,
	/* the host never answered */
	_join_reason_no_answer,
	/* the host answered, but no connection got through (strict NATs) */
	_join_reason_no_connection,

	NUMBER_OF_JOIN_REASONS
};

/* the reasons, each a sentence */
#define JOIN_REASON_NEEDS_UPDATE "Needs a newer Chupathingy."
#define JOIN_REASON_HOST_OLDER "The host is on an older version."
#define JOIN_REASON_OTHER_VERSION "The host is on another version."
/* %s: the map's file, as the player finds it ("maps_ce/bloodgulch.map") */
#define JOIN_REASON_MAP_MISSING "Map not installed: %s."
#define JOIN_REASON_MAP_DOWNLOAD "Map not installed: %s. Join to download it."
/* %s: one of Halo PC's own maps ("bloodgulch.map"), never downloaded */
#define JOIN_REASON_MAP_FROM_HALO_PC "Map not installed: %s. Copy it from Halo PC into maps_pc or maps_ce."
/* %s: the family, "Halo PC" or "HaloMD" */
#define JOIN_REASON_MAPS_UNSUPPORTED "%s maps don't work on this build."
/* %s: the game type's name */
#define JOIN_REASON_OPENCE_GAME_TYPE "Uses an OpenCE game type Chupathingy doesn't have yet: %s."
#define JOIN_REASON_NEEDS_BOMB "Needs a Chupathingy build with Bomb."
#define JOIN_REASON_NOT_OPEN "Not taking players right now."
#define JOIN_REASON_FULL "Game is full."
#define JOIN_REASON_PASSWORD "Password needed."
#define JOIN_REASON_NO_ANSWER "The host didn't answer."
#define JOIN_REASON_NO_CONNECTION "Couldn't connect to the host."
/* an OpenCE dedicated server's refusals of network version 40 and 41 (codes 8
to 13 of the host's refusal): a VPN, Tor or data center address (8, 9); a
Discord link of OpenCE's own, which this build has none of (10 to 12); and
the machines one address may have (13) */
#define JOIN_REASON_NO_VPN "This server doesn't allow VPNs."
#define JOIN_REASON_DISCORD_LINK "This server only lets in players who link a Discord account with OpenCE."
#define JOIN_REASON_ADDRESS_FULL "Too many players from your address."
/* a map of Halo: The Master Chief Collection (cache version 13) in a map
folder: said in the map lists, in the log and when it is picked to play (no
detail, and no join reason: a host never announces one) */
#define MAP_REASON_MCC_UNSUPPORTED "MCC map: not supported yet"

/* a reason without a detail as its sentence ("" for none, or one with a
detail) */
#define JOIN_REASON_TEXT(reason) \
	((reason) == _join_reason_needs_update ? JOIN_REASON_NEEDS_UPDATE : \
	(reason) == _join_reason_host_older ? JOIN_REASON_HOST_OLDER : \
	(reason) == _join_reason_other_version ? JOIN_REASON_OTHER_VERSION : \
	(reason) == _join_reason_needs_bomb ? JOIN_REASON_NEEDS_BOMB : \
	(reason) == _join_reason_not_open ? JOIN_REASON_NOT_OPEN : \
	(reason) == _join_reason_full ? JOIN_REASON_FULL : \
	(reason) == _join_reason_password ? JOIN_REASON_PASSWORD : \
	(reason) == _join_reason_no_answer ? JOIN_REASON_NO_ANSWER : \
	(reason) == _join_reason_no_connection ? JOIN_REASON_NO_CONNECTION : "")

/* a game of another network version's reason (browser_version_state's
state: -1 older, 1 newer) */
#define JOIN_REASON_OF_VERSION_STATE(state) ((state) < 0 ? _join_reason_host_older : _join_reason_needs_update)

/* what the player can do about it, after the reason in a message */
#define JOIN_HINT_UPDATE "Update Chupathingy to join this game."
#define JOIN_HINT_HOST_UPDATE "Ask the host to update."
#define JOIN_HINT_NO_CONNECTION "The host can forward its UDP port or turn on UPnP."
/* a failed connection's reason and hint, as one line */
#define JOIN_REASON_NO_CONNECTION_LINE JOIN_REASON_NO_CONNECTION " " JOIN_HINT_NO_CONNECTION

/* the title of a failed join's message */
#define JOIN_FAILED_TITLE "Halo: can't join this game"

/* a host's neutral label, beside the OpenCE (legacy) marking: an OpenCE
dedicated server, as its advertisement or public listing says it
(delta_compat_opence_server, delta_compat_opence_server_listing), or the
game list does (an OpenCE master lists it: browser.c, field 24). The same
words in Online Games, the Server Browser and the site's game list (puma,
web/site.js HOST_LABEL_OPENCE_SERVER); a description, never a badge or a
judgment, and it grants nothing */
#define HOST_LABEL_OPENCE_SERVER "OpenCE dedicated"

/* a reason as a wide string (menu_functions.c's text): JOIN_WIDE(JOIN_REASON_FULL) */
#define JOIN_WIDE_TEXT(text) L##text
#define JOIN_WIDE(text) JOIN_WIDE_TEXT(text)

#endif
