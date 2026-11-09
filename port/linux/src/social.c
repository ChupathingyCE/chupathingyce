/*
SOCIAL.C

The Guide's data: its rows and their cache, the site's polling schedule and
presence's (social.h). No SDL, no drawing, no game.
*/

#include "social.h"
#include "discord_activity.h"

#include <stdio.h>
#include <string.h>

static const char *const section_names[NUMBER_OF_SOCIAL_SECTIONS] =
{
	"FRIENDS", "PARTY", "PLAYERS HERE", "RECENT", "SETTINGS",
};

const char *social_section_name(int section)
{
	return section >= 0 && section < NUMBER_OF_SOCIAL_SECTIONS ? section_names[section] : "";
}

/* ---------- rows */

void social_copy_text(char *destination, int size, const char *text)
{
	int length;

	if (size <= 0)
		return;
	if (!text)
		text = "";
	length = (int)strlen(text);
	if (length > size - 1)
	{
		length = size - 1;
		/* (back to the start of the character the cut fell in) */
		while (length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80)
			length--;
	}
	memcpy(destination, text, (size_t)length);
	destination[length] = 0;
}

void social_list_clear(struct social_list *list)
{
	list->count = 0;
	memset(list->rows, 0, sizeof(list->rows));
}

int social_list_add(struct social_list *list, const struct social_row *row)
{
	struct social_row *added;

	if (list->count < 0 || list->count >= SOCIAL_MAXIMUM_ROWS)
		return -1;
	added = &list->rows[list->count];
	/* (field by field over zeros, so that commit's comparison sees only
	what the row holds, not what was past its texts' ends) */
	memset(added, 0, sizeof(*added));
	added->platform = row->platform;
	added->state = row->state;
	added->actions = row->actions;
	added->muted = row->muted;
	added->verified = row->verified;
	added->key = row->key;
	social_copy_text(added->tag, sizeof(added->tag), row->tag);
	social_copy_text(added->status, sizeof(added->status), row->status);
	return list->count++;
}

int social_list_find(const struct social_list *list, int key)
{
	int index;

	for (index = 0; index < list->count; index++)
	{
		if (list->rows[index].key == key)
			return index;
	}
	return -1;
}

/* (in a game first, then online, away, offline) */
static int state_rank(int state)
{
	switch (state)
	{
	case SOCIAL_STATE_IN_GAME: return 0;
	case SOCIAL_STATE_ONLINE: return 1;
	case SOCIAL_STATE_AWAY: return 2;
	default: return 3;
	}
}

static int compare_tags(const char *a, const char *b)
{
	for (;; a++, b++)
	{
		int x = (unsigned char)*a, y = (unsigned char)*b;

		if (x >= 'A' && x <= 'Z')
			x += 'a' - 'A';
		if (y >= 'A' && y <= 'Z')
			y += 'a' - 'A';
		if (x != y || !x)
			return x - y;
	}
}

static int row_before(const struct social_row *a, const struct social_row *b)
{
	int rank = state_rank(a->state) - state_rank(b->state);

	return rank ? rank < 0 : compare_tags(a->tag, b->tag) < 0;
}

void social_list_sort(struct social_list *list)
{
	int index, place;

	/* (an insertion sort: a few dozen rows, and it keeps equal rows' order) */
	for (index = 1; index < list->count; index++)
	{
		struct social_row row = list->rows[index];

		for (place = index; place > 0 && row_before(&row, &list->rows[place - 1]); place--)
			list->rows[place] = list->rows[place - 1];
		list->rows[place] = row;
	}
}

int social_list_commit(struct social_list *cache, const struct social_list *fresh)
{
	if (cache->count == fresh->count &&
		!memcmp(cache->rows, fresh->rows, (size_t)fresh->count * sizeof(fresh->rows[0])))
	{
		return 0;
	}
	cache->count = fresh->count;
	memcpy(cache->rows, fresh->rows, (size_t)fresh->count * sizeof(fresh->rows[0]));
	cache->serial++;
	return 1;
}

int social_own_status(const struct discord_activity_game *game, char *text, int size)
{
	char doing[SOCIAL_STATUS_SIZE];
	int length;

	if (size <= 0)
		return -1;
	if (!game || discord_activity_status_line(game, doing, sizeof(doing)) < 0)
		length = snprintf(text, (size_t)size, "Online");
	else
		length = snprintf(text, (size_t)size, "Online \xC2\xB7 %s", doing);
	if (length < 0 || length >= size)
	{
		social_copy_text(text, size, "Online");
		return -1;
	}
	return length;
}

/* ---------- polling */

static int passed(unsigned long now, unsigned long since, unsigned long interval)
{
	return now - since >= interval;
}

int social_poll_due(struct social_poll *poll, unsigned long now, int in_game, int signed_in)
{
	if (!signed_in)
	{
		poll->started = 0;
		return 0;
	}
	if (poll->started && !passed(now, poll->last, in_game ? SOCIAL_POLL_GAME : SOCIAL_POLL_MENUS))
		return 0;
	poll->started = 1;
	poll->last = now;
	return 1;
}

/* ---------- presence */

void social_presence_reset(struct social_presence *presence)
{
	memset(presence, 0, sizeof(*presence));
	presence->choices = -1;
}

int social_presence_due(struct social_presence *presence, const struct social_presence_inputs *inputs,
	unsigned long now)
{
	int allowed = inputs->share && inputs->in_game && inputs->on_delta && inputs->linked && !inputs->hidden &&
		inputs->has_invite;

	int choices = (inputs->share ? 1 : 0) | (inputs->linked ? 2 : 0) | (inputs->hidden ? 4 : 0);

	/* (the player changed what they share: told at once, by the new
	choices; an answer still out is let go) */
	if (choices != presence->choices)
	{
		presence->choices = choices;
		presence->active = 0;
		presence->waiting = 0;
		presence->wait = 0;
	}
	if (!allowed)
		return 0;
	if (presence->waiting)
		return 0;
	if (presence->active && !passed(now, presence->last, presence->wait))
		return 0;
	presence->active = 1;
	presence->waiting = 1;
	presence->last = now;
	presence->wait = SOCIAL_PRESENCE_INTERVAL;
	presence->sent++;
	return 1;
}

int social_presence_answer(struct social_presence *presence, int status, const char *body, unsigned long now)
{
	int answer;

	if (!presence->waiting)
		return SOCIAL_PRESENCE_NONE;
	presence->waiting = 0;
	presence->last = now;
	if (status == 200 && body && !strncmp(body, "ok", 2))
	{
		answer = SOCIAL_PRESENCE_OK;
		presence->wait = SOCIAL_PRESENCE_INTERVAL;
	}
	else if (status == 200)
	{
		answer = SOCIAL_PRESENCE_OFF;
		presence->wait = SOCIAL_PRESENCE_OFF_WAIT;
	}
	else if (status == 429)
	{
		answer = SOCIAL_PRESENCE_BUSY;
		presence->wait = SOCIAL_PRESENCE_BUSY_WAIT;
	}
	else if (status >= 400 && status < 500)
	{
		/* (a request the site does not take: not sent again soon) */
		answer = SOCIAL_PRESENCE_REFUSED;
		presence->wait = SOCIAL_PRESENCE_OFF_WAIT;
	}
	else
	{
		answer = SOCIAL_PRESENCE_NO_ANSWER;
		presence->wait = SOCIAL_PRESENCE_INTERVAL;
	}
	presence->answer = answer;
	return answer;
}

int social_presence_invite_valid(const char *invite)
{
	size_t length, index;

	if (!invite)
		return 0;
	length = strlen(invite);
	if (length != 64 && length != 44)
		return 0;
	for (index = 0; index < length; index++)
	{
		char c = invite[index];

		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return 0;
	}
	return 1;
}

int social_presence_fields(char *fields, int size, const char *invite)
{
	int length;

	if (size <= 0)
		return -1;
	fields[0] = 0;
	if (!social_presence_invite_valid(invite))
		return -1;
	length = snprintf(fields, (size_t)size, "\"invite\": \"%s\"", invite);
	if (length < 0 || length >= size)
	{
		fields[0] = 0;
		return -1;
	}
	return length;
}
