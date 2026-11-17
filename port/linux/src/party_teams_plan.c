/*
PARTY_TEAMS_PLAN.C

The host's plan for keeping a party on one team (party_teams_plan.h).
*/

#include "party_teams_plan.h"

static int on_team(const struct party_teams_player *player)
{
	return player->team == 0 || player->team == 1;
}

int party_teams_plan(const struct party_teams_player *players, int count, struct party_teams_change *change)
{
	int counts[2];
	int index;

	change->player = -1;
	change->team = -1;
	counts[0] = counts[1] = 0;
	for (index = 0; index < count; index++)
	{
		if (on_team(&players[index]))
			counts[players[index].team]++;
	}
	/* (each party, at its first member) */
	for (index = 0; index < count; index++)
	{
		const struct party_teams_player *first = &players[index];
		int members[2], target, other, member, earlier, tries;

		if (first->party < 0 || !on_team(first))
			continue;
		for (earlier = 0; earlier < index; earlier++)
		{
			if (players[earlier].party == first->party && on_team(&players[earlier]))
				break;
		}
		if (earlier < index)
			continue;
		members[0] = members[1] = 0;
		for (member = index; member < count; member++)
		{
			if (players[member].party == first->party && on_team(&players[member]))
				members[players[member].team]++;
		}
		if (!members[0] || !members[1])
			continue;
		/* (where most of them are; a tie, the smaller team first, then the
		other) */
		if (members[0] != members[1])
			target = members[1] > members[0];
		else
			target = counts[1] < counts[0];
		for (tries = members[0] == members[1] ? 2 : 1; tries > 0; tries--, target = 1 - target)
		{
			other = 1 - target;
			for (member = index; member < count; member++)
			{
				const struct party_teams_player *player = &players[member];

				if (player->party != first->party || player->team != other || !player->movable)
					continue;
				/* (a move: the teams within one of each other after it; never a
				trade, so a player in no party is never moved for one) */
				if (counts[target] + 1 - (counts[other] - 1) <= 1)
				{
					change->player = member;
					change->team = target;
					return 1;
				}
			}
		}
	}
	return 0;
}
