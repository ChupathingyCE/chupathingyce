/*
SERVER_CONDUCT.H

A dedicated server's betrayal and AFK limits (server/docs/moderation.md),
both off unless set: a player who betrays a teammate the limit's number of
times in one game is warned one short of it and kicked at it, and a second
such kick within a day keeps them out for 30 minutes (bans.txt, as sv_ban
does); a player who sends no input for the AFK limit's minutes while a game
is played is warned 30 seconds before and kicked (moderators never). Each
warning, kick and ban is in the log and the audit file. The rules
themselves are port/linux/game/conduct_rules.c's.
*/

#ifndef SERVER_CONDUCT_H
#define SERVER_CONDUCT_H

/* each frame (server_commands.c): the players looked at */
void server_conduct_update(void);

/* the limits: betrayals in a game (0: off), idle minutes (0: off) */
void server_conduct_set_betrayal_limit(int betrayals);
void server_conduct_set_afk_minutes(int minutes);
int server_conduct_betrayal_limit(void);
int server_conduct_afk_minutes(void);

#endif
