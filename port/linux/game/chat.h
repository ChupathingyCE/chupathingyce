/*
CHAT.H

Text chat between Chupathingy machines (chat.c; docs/delta.md, Chat).
*/

#ifndef __CHAT_H
#define __CHAT_H

/* each frame, from the main loop (main.c): the host's routing, the lines
heard, the keys */
void chat_update(void);

/* over the HUD of a local player (hud.c): the recent lines, the line
being typed, the log */
void chat_draw(short local_player_index);

/* the host's (a dedicated server's sv_say, its notices): a server line to
every Chupathingy machine of the game, and this machine's own player; 0 if
no machine could take it */
int chat_server_say(char const *text);

/* the host's mode (enum delta_chat_mode) and whether it filters blocked
words: a dedicated server's settings (a player's host reads network.chat
and network.chat_filter) */
void chat_host_set(int mode, int filter);
int chat_host_mode(void);
int chat_host_filter(void);

/* (the dedicated server's) whether a machine's player is signed in as a
moderator with a role here: its lines are marked as a moderator's */
void chat_set_moderator_check(int (*check)(int machine_index));

#endif
