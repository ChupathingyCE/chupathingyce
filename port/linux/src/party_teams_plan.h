/*
PARTY_TEAMS_PLAN.H

Keeping a party on one team (docs/delta.md, Party teams): the host's plan,
one change at a time, from its players' teams and parties
(port/linux/game/party_teams.c gathers them and makes the change). No game
here, so its tests build anywhere (port/linux/tests/party_teams_test.c,
tools/test_party_teams.py).

The rule: a party's members go to the team most of them are on (a tie: the
team with fewer players), one at a time, only where the teams then stay
within one player of each other. Where a move would unbalance them, the
party stays split: only party members are ever moved, never a player in no
party (a party's tag is the client's word, so it must not move anyone
else). A player the host may not move now (one playing: party_teams.c moves
only players who have not spawned yet) is never moved.

Plain C89 (the consoles' compilers read it too).
*/

#ifndef PARTY_TEAMS_PLAN_H
#define PARTY_TEAMS_PLAN_H

struct party_teams_player
{
	/* 0 red, 1 blue; anything else: not on a team (left out) */
	int team;
	/* the player's party, by a number the caller gives each tag (-1: in no
	party) */
	int party;
	/* the host may move them now */
	int movable;
};

struct party_teams_change
{
	/* the party member moved, and to which team */
	int player;
	int team;
};

/* the next change that brings a party together: 1 and the change, or 0
when there is none to make */
int party_teams_plan(const struct party_teams_player *players, int count, struct party_teams_change *change);

#endif
