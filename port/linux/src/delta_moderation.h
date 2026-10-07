/*
DELTA_MODERATION.H

Delta Peer's moderation, as the game sees it (docs/delta.md, Moderation;
delta_peer_game.c): plain types only, so the dedicated server's units
(server/src) and the game's screens both call it.

The host's side is the dedicated server's: it gives its moderation
(struct delta_peer_moderation_host, delta_peer.h) and Delta Peer offers
moderation to the clients that say HELLO from then on; a game a player
hosts has none, and offers none. A client signs in with its moderator key
(browser.c: an Ed25519 key made from its player key) only when its player
acts, and its actions are signed with it; the host's moderation decides
what a key may do.
*/

#ifndef HALO_DELTA_MODERATION_H
#define HALO_DELTA_MODERATION_H

#include "delta_peer.h"

/* ---------- the host's (the dedicated server's) */

/* the host's moderation (NULL: none); kept, not copied */
void delta_peer_game_set_moderation_host(const struct delta_peer_moderation_host *host);
/* a machine's moderator key, proved this session: 1 and it in key (32 bytes) */
int delta_peer_game_moderation_key(int machine_index, unsigned char *key);
/* whether a machine speaks Delta Peer and agreed to moderation */
int delta_peer_game_moderation_capable(int machine_index);
/* a warning or notice (_delta_moderation_notice_*) to a machine's player:
1 if sent (the machine agreed to moderation) */
int delta_peer_game_moderation_notice(int machine_index, int kind, const char *text);
/* asks a machine's player whether to link the game to an account of the
server's panel (account) on this server (server): 1 if asked; the answer
comes to the host's bind_answer, or a decline after
DELTA_PEER_MODERATION_BIND_TIME */
int delta_peer_game_moderation_bind(int machine_index, unsigned int request, const char *account, const char *server);
/* the roles changed: each signed-in machine is told its role again */
void delta_peer_game_moderation_roles_changed(void);

/* ---------- the client's (the game's screens) */

struct delta_peer_game_moderation
{
	/* the host agreed to moderation and said its challenge */
	int available;
	/* the player signed in; the host said the key's role */
	int signed_in;
	int has_state;
	int role;
	unsigned int permissions;
	unsigned int ban_minutes;
	/* the last action's result, and how many have come */
	delta_u32 result_count;
	int result_ok;
	char result[DELTA_WIRE_MODERATION_TEXT_SIZE + 1];
	/* the last notice, and how many have come */
	delta_u32 notice_count;
	int notice_kind;
	char notice[DELTA_WIRE_MODERATION_TEXT_SIZE + 1];
	/* a link waiting for the player's answer */
	int bind_waiting;
	char bind_account[DELTA_WIRE_MODERATION_NAME_SIZE + 1];
	char bind_server[DELTA_WIRE_MODERATION_NAME_SIZE + 1];
};

/* the client's moderation with its host, now */
void delta_peer_game_client_moderation(struct delta_peer_game_moderation *moderation);
/* the player signs in as a moderator: 1 if sent */
int delta_peer_game_moderation_sign_in(void);
/* an action (enum delta_moderation_action) on a machine
(DELTA_WIRE_NO_MACHINE: none), a ban's minutes (0: for ever) and a reason:
its sequence, 0 if not sent */
unsigned int delta_peer_game_moderation_action(int action, int target_machine, int minutes, const char *reason);
/* the player's answer to the waiting link: 1 if sent */
int delta_peer_game_moderation_bind_answer(int accepted);

#endif
