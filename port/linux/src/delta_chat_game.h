/*
DELTA_CHAT_GAME.H

Text chat over Delta Peer, as the game sees it (docs/delta.md, Chat;
delta_peer_game.c): plain types only, so the dedicated server's units
(server/src) and the game's chat (port/linux/game/chat.c) both call it.

The host's side routes: Delta Peer checks a line's session, rate and text,
then hands it to the host's chat (delta_peer_game_set_chat_host), which
names its speaker, decides who hears it and sends it on. A client says its
player's lines and reads the lines its host sends.
*/

#ifndef HALO_DELTA_CHAT_GAME_H
#define HALO_DELTA_CHAT_GAME_H

#include "delta_peer.h"

/* the machine's part in a game now: 0 none, 1 a client, 2 the host */
int delta_peer_game_role(void);

/* ---------- the host's */

/* its chat (NULL: none); kept, not copied */
void delta_peer_game_set_chat_host(const struct delta_peer_chat_host *host);
/* what it relays (enum delta_chat_mode) and CHAT_CONFIG's flags */
void delta_peer_game_set_chat_mode(int mode, int flags);
/* the seconds a machine's chat pauses once it says too much too fast (0:
none; delta_peer_set_chat_cooldown) */
void delta_peer_game_set_chat_cooldown(int seconds);
/* whether a machine takes lines of a kind (enum delta_chat_kind) */
int delta_peer_game_chat_capable(int machine_index, int kind);
/* a line to a machine: 1 if sent */
int delta_peer_game_chat_line(int machine_index, const struct delta_wire_chat_line *line);
/* a machine muted (minutes; 0 until it leaves the game) or not */
void delta_peer_game_chat_mute(int machine_index, int mute, int minutes);
int delta_peer_game_chat_muted(int machine_index);

/* ---------- the client's */

/* the host agreed to chat (this player's lines may be said) */
int delta_peer_game_chat_ready(void);
/* the host agreed to server_messages (its own lines may come) */
int delta_peer_game_chat_server_messages(void);
/* a line its player says: its sequence, 0 if not sent */
unsigned int delta_peer_game_chat_say(int scope, const char *text, int length);
/* the lines heard so far (a count that grows; back to 0 with a new host),
and one of them (1 if kept) */
unsigned int delta_peer_game_chat_received(void);
int delta_peer_game_chat_get(unsigned int index, struct delta_wire_chat_line *line);
/* the host's CHAT_CONFIG (0 if none heard), and its last refusal and how
many have come */
int delta_peer_game_chat_config(struct delta_wire_chat_config *config);
unsigned int delta_peer_game_chat_refused(struct delta_wire_chat_refused *refused);

/* a file of the save folder (a plain name: chat.filter_file) into text
(size bytes with its end): its length, -1 if it cannot be read */
int delta_peer_game_read_save_file(const char *name, char *text, int size);

#endif
