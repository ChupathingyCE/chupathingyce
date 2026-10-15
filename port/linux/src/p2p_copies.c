/*
P2P_COPIES.C

One public game listed under every network version, shown once
(p2p_listing_older_copy, p2p.h).
*/

#include "p2p.h"

#include <string.h>

static int is_space(char character)
{
	return character == ' ' || (character >= '\t' && character <= '\r');
}

static char lower(char character)
{
	return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

/* whether two texts are the same but for case (and, trimmed, the spaces
around them) */
static int same_text(const char *a, const char *b, int trimmed)
{
	size_t a_length, b_length, index;

	if (trimmed)
	{
		while (is_space(*a))
			a++;
		while (is_space(*b))
			b++;
	}
	a_length = strlen(a);
	b_length = strlen(b);
	if (trimmed)
	{
		while (a_length && is_space(a[a_length - 1]))
			a_length--;
		while (b_length && is_space(b[b_length - 1]))
			b_length--;
	}
	if (a_length != b_length)
		return 0;
	for (index = 0; index < a_length; index++)
	{
		if (lower(a[index]) != lower(b[index]))
			return 0;
	}
	return 1;
}

static int alike(const struct p2p_listing *a, const struct p2p_listing *b)
{
	return a->engine_type == b->engine_type && a->player_count == b->player_count &&
		a->maximum_player_count == b->maximum_player_count && a->in_progress == b->in_progress &&
		same_text(a->name, b->name, 1) && same_text(a->map, b->map, 0) && same_text(a->gametype, b->gametype, 0);
}

int p2p_listing_older_copy(const struct p2p_listing *games, const unsigned long *heard_times, int count, int index)
{
	const struct p2p_listing *game = &games[index];
	int older = 0, later = 0;
	int other;

	for (other = 0; other < count; other++)
	{
		const struct p2p_listing *copy = &games[other];

		if (other == index || !alike(game, copy))
			continue;
		if (copy->version > game->version)
			return 1;
		if (copy->version < game->version)
			older = 1;
		else if ((long)(heard_times[other] - heard_times[index]) > 0 ||
			(heard_times[other] == heard_times[index] && other < index))
		{
			/* (of copies of one version heard at once, the first) */
			later = 1;
		}
	}
	/* (no other version: each its own game) */
	return older && later;
}
