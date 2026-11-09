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
	unsigned long interval = in_game ? SOCIAL_POLL_GAME : SOCIAL_POLL_MENUS;

	if (!signed_in)
	{
		poll->started = 0;
		poll->wait = 0;
		poll->failures = 0;
		poll->soon = 0;
		return 0;
	}
	if (poll->wait > interval)
		interval = poll->wait;
	else if (poll->soon)
		interval = SOCIAL_POLL_SOONEST;
	if (poll->started && !passed(now, poll->last, interval))
		return 0;
	poll->started = 1;
	poll->last = now;
	poll->soon = 0;
	return 1;
}

void social_poll_answer(struct social_poll *poll, int status, int understood)
{
	if (status == 200 && understood)
	{
		poll->wait = 0;
		poll->failures = 0;
	}
	else if (status == 429)
		poll->wait = SOCIAL_POLL_BUSY_WAIT;
	else if (status == 404)
		poll->wait = SOCIAL_POLL_UNLINKED_WAIT;
	else
	{
		/* (no answer, or one not understood: 40 s, 80 s, ... up to five
		minutes) */
		unsigned long wait = SOCIAL_POLL_MENUS;
		int doubling;

		if (poll->failures < 16)
			poll->failures++;
		for (doubling = 0; doubling < poll->failures && wait < SOCIAL_POLL_FAILED_WAIT; doubling++)
			wait *= 2;
		poll->wait = wait < SOCIAL_POLL_FAILED_WAIT ? wait : SOCIAL_POLL_FAILED_WAIT;
	}
}

void social_poll_soon(struct social_poll *poll)
{
	if (!poll->wait)
		poll->soon = 1;
}

/* ---------- the site's friends */

const char *social_name(const char *display, const char *handle)
{
	return display && display[0] ? display : handle ? handle : "";
}

int social_handle_valid(const char *handle)
{
	size_t length, index;

	if (!handle)
		return 0;
	length = strlen(handle);
	if (length < 3 || length > 20)
		return 0;
	for (index = 0; index < length; index++)
	{
		char c = handle[index];

		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
			return 0;
	}
	return 1;
}

int social_game_line(const struct social_game *game, char *text, int size)
{
	struct discord_activity_game shown;

	if (size <= 0)
		return -1;
	text[0] = 0;
	if (!game || !game->listed || !game->map[0])
		return -1;
	memset(&shown, 0, sizeof(shown));
	shown.screen = game->coop ? DISCORD_SCREEN_CAMPAIGN : DISCORD_SCREEN_MULTIPLAYER;
	shown.coop = game->coop;
	social_copy_text(shown.map, sizeof(shown.map), game->map);
	shown.mode = game->engine > 0 && game->engine < NUMBER_OF_DISCORD_MODES ? game->engine : DISCORD_MODE_NONE;
	shown.teams = game->teams;
	shown.player_count = game->players;
	shown.maximum_player_count = game->maximum_players;
	return discord_activity_status_line(&shown, text, size);
}

void social_friend_status(const struct social_friend *buddy, char *text, int size)
{
	if (size <= 0)
		return;
	if (buddy->playing && social_game_line(&buddy->game, text, size) >= 0)
		return;
	social_copy_text(text, size, buddy->playing ? "In a game" : buddy->online ? "Online" : "Offline");
}

int social_online_count(const struct social_state *state)
{
	int index, count = 0;

	for (index = 0; index < state->friend_count; index++)
		count += state->friends[index].online != 0;
	return count;
}

static const struct social_friend *find_friend(const struct social_state *state, const char *handle)
{
	int index;

	for (index = 0; index < state->friend_count; index++)
	{
		if (!strcmp(state->friends[index].handle, handle))
			return &state->friends[index];
	}
	return NULL;
}

void social_news(const struct social_state *old, const struct social_state *fresh, struct social_news *news)
{
	int index, seen;

	news->invite = -1;
	news->request = -1;
	news->online = -1;
	for (index = 0; index < fresh->invite_count && news->invite < 0; index++)
	{
		int other;

		for (other = 0, seen = 0; other < old->invite_count && !seen; other++)
			seen = !strcmp(old->invites[other].id, fresh->invites[index].id);
		if (!seen)
			news->invite = index;
	}
	for (index = 0; index < fresh->request_count && news->request < 0; index++)
	{
		int other;

		for (other = 0, seen = 0; other < old->request_count && !seen; other++)
			seen = !strcmp(old->requests[other].handle, fresh->requests[index].handle);
		if (!seen)
			news->request = index;
	}
	/* (a friend who was there and offline; a new friend is not news) */
	for (index = 0; index < fresh->friend_count && news->online < 0; index++)
	{
		const struct social_friend *before;

		if (!fresh->friends[index].online)
			continue;
		before = find_friend(old, fresh->friends[index].handle);
		if (before && !before->online)
			news->online = index;
	}
}

/* fields printed, or -1 when they do not fit */
static int fields_done(char *fields, int size, int length)
{
	if (length < 0 || length >= size)
	{
		fields[0] = 0;
		return -1;
	}
	return length;
}

int social_state_fields(char *fields, int size, const char *cursor, int limit)
{
	size_t index;

	if (size <= 0)
		return -1;
	fields[0] = 0;
	/* (the cursor is the site's own: hexadecimal digits) */
	for (index = 0; cursor && cursor[index]; index++)
	{
		char c = cursor[index];

		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return -1;
	}
	if (cursor && cursor[0])
		return fields_done(fields, size, snprintf(fields, (size_t)size, "\"since\": \"%s\", \"limit\": %d", cursor, limit));
	return fields_done(fields, size, snprintf(fields, (size_t)size, "\"limit\": %d", limit));
}

int social_action_fields(char *fields, int size, const char *action, const char *handle)
{
	if (size <= 0)
		return -1;
	fields[0] = 0;
	if (!social_handle_valid(handle) || !action ||
		(strcmp(action, "accept") && strcmp(action, "decline") && strcmp(action, "remove") && strcmp(action, "block")))
	{
		return -1;
	}
	return fields_done(fields, size, snprintf(fields, (size_t)size, "\"action\": \"%s\", \"handle\": \"%s\"",
		action, handle));
}

int social_join_fields(char *fields, int size, const char *handle)
{
	if (size <= 0)
		return -1;
	fields[0] = 0;
	if (!social_handle_valid(handle))
		return -1;
	return fields_done(fields, size, snprintf(fields, (size_t)size, "\"friend\": \"%s\"", handle));
}

int social_invite_fields(char *fields, int size, const char *handle, const char *invite)
{
	if (size <= 0)
		return -1;
	fields[0] = 0;
	if (!social_handle_valid(handle) || (invite && invite[0] && !social_presence_invite_valid(invite)))
		return -1;
	if (invite && invite[0])
	{
		return fields_done(fields, size, snprintf(fields, (size_t)size, "\"to\": \"%s\", \"invite\": \"%s\"",
			handle, invite));
	}
	return fields_done(fields, size, snprintf(fields, (size_t)size, "\"to\": \"%s\"", handle));
}

static const char *const join_reasons[][2] =
{
	{ "offline", "They're offline." },
	{ "not_playing", "They're not in a game." },
	{ "friends_only", "Friends only." },
	{ "invite_only", "Invite only." },
	{ "unlisted", "Their game isn't listed." },
	{ "locked", "Their game has a password." },
	{ "full", "Their game is full." },
	{ "closed", "Their game is closed." },
	{ "unknown", "No such player." },
};

const char *social_join_refusal(const char *reason)
{
	size_t index;

	for (index = 0; reason && index < sizeof(join_reasons) / sizeof(join_reasons[0]); index++)
	{
		if (!strcmp(reason, join_reasons[index][0]))
			return join_reasons[index][1];
	}
	return "Can't join right now.";
}

const char *social_invite_refusal(const char *reason, int status)
{
	if (reason && !strcmp(reason, "not_playing"))
		return "You're not in a game.";
	if (reason && !strcmp(reason, "locked"))
		return "Your game has a password.";
	if (reason && !strcmp(reason, "full"))
		return "Your game is full.";
	if (reason && !strcmp(reason, "closed"))
		return "Your game is closed.";
	if (status == 403)
		return "Friends only.";
	if (status == 429)
		return "Too many invites. Wait a minute.";
	return "Can't invite right now.";
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
