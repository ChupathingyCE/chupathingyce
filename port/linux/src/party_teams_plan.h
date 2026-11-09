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
member trades places with a player in no party on that team. A player the
host may not move now (one playing: party_teams.c moves only players who
have not spawned yet) is never moved, and never traded.

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
	/* the player moved, and to which team */
	int player;
	int team;
	/* the player in no party traded to the other team (-1: none, a plain
	move) */
	int traded;
};

/* the next change that brings a party together: 1 and the change, or 0
when there is none to make */
int party_teams_plan(const struct party_teams_player *players, int count, struct party_teams_change *change);

#endif
