/*
CHAT.H

Text chat between Chupathingy machines, bridged to OpenCE's (chat.c;
docs/delta.md, Chat).
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

/* the host's notice to one OpenCE machine, over OpenCE's chat (as its
welcome is sent): 0 if it could not be sent (not hosting a game in
progress, a Chupathingy machine, or OpenCE's chat off here) */
int chat_host_notice_to_machine(short machine, char const *text);

/* (the host) a server message to one machine alone: a Chupathingy game's
chat line, an OpenCE game's host notice (while its chat is bridged); 0 if
that game cannot be told */
int chat_host_notice(int machine_index, char const *text);

/* a note of this machine's own in the chat (as its refusals are): what
came of the player's ask to change teams (team_change.c) */
void chat_note(char const *text);

/* (network_distributed.c) one of OpenCE's chat messages
(_distributed_message_chat_up, _down or _config; delta_chat_bridge.h): its
first entry (size bytes; NULL or 0 for a malformed message, dropped and
logged once), from a machine (the host's, NONE on a client), and whether it
came over the sender's stream (anything else is dropped) */
void chat_bridge_message(long machine_index, int type, void const *entry, int size, int from_stream);

/* (network_distributed.c, a client) the host's notice for the console
(_distributed_message_notice), printable ASCII: also a line of the chat
when the host has no Delta chat (an OpenCE host, which sends its server
messages, its message of the day and its dedicated server's word that way) */
void chat_host_console_notice(char const *text);

/* the host's mode (enum delta_chat_mode) and whether it filters blocked
words: a dedicated server's settings (a player's host reads network.chat
and network.chat_filter) */
void chat_host_set(int mode, int filter);
int chat_host_mode(void);
int chat_host_filter(void);

/* whether OpenCE players may chat in this host's games (over OpenCE's own
chat, bridged), while its chat is on: a dedicated server's chat_legacy, or
a player's host's network.chat_legacy */
void chat_host_set_legacy(int legacy);
int chat_host_legacy(void);

/* (a dedicated server's) the seconds a player's chat pauses once they say
too much too fast (0: none; 5 unless set), and the welcome each Chupathingy
player is sent as they join (NULL or empty: none) */
void chat_host_set_cooldown(int seconds);
int chat_host_cooldown(void);
void chat_host_set_welcome(char const *text);
char const *chat_host_welcome(void);

/* this copy's own mute of a machine's player (the chat log's M, and the
Guide's Players here): whether they are muted, and muted or not. wchar_t
is the game's UTF-16 */
boolean chat_local_muted(short machine, wchar_t const *name);
void chat_local_mute(short machine, wchar_t const *name, boolean mute);

/* (the dedicated server's) whether a machine's player is signed in as a
moderator with a role here: its lines are marked as a moderator's */
void chat_set_moderator_check(int (*check)(int machine_index));

#endif
