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
	"FRIENDS", "PARTY", "CLAN", "PLAYERS HERE", "RECENT", "WORKSHOP", "SERVICE RECORD", "SETTINGS",
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

/* how many polls went out in the last window before now (only those that
went out early for a ring, if rings) */
static int polls_within(const struct social_poll *poll, unsigned long now, unsigned long window, int rings)
{
	int index, count = 0;

	for (index = 0; index < poll->time_count; index++)
	{
		if ((!rings || poll->rang[index]) && !passed(now, poll->times[index], window))
			count++;
	}
	return count;
}

int social_poll_due(struct social_poll *poll, unsigned long now, int in_game, int signed_in)
{
	unsigned long interval = in_game ? SOCIAL_POLL_GAME : poll->party ? SOCIAL_POLL_PARTY : SOCIAL_POLL_MENUS;
	int ring, early;

	if (!signed_in)
	{
		poll->started = 0;
		poll->wait = 0;
		poll->failures = 0;
		poll->soon = 0;
		poll->rung = 0;
		poll->time_count = 0;
		poll->next_time = 0;
		return 0;
	}
	if (poll->wait > interval)
		interval = poll->wait;
	else if (poll->soon)
		interval = SOCIAL_POLL_SOONEST;
	/* (a ring: at once when there is room, never through a wait the site
	asked for; only the polls that went out early for a ring count against
	its room, so the schedule's own never use it up) */
	ring = poll->rung && !poll->wait && passed(now, poll->last, SOCIAL_POLL_RING_GAP) &&
		polls_within(poll, now, SOCIAL_POLL_RING_WINDOW, 1) < SOCIAL_POLL_RING_ROOM &&
		polls_within(poll, now, SOCIAL_POLL_RING_WINDOW, 0) < SOCIAL_POLL_ROOM;
	early = poll->started && !passed(now, poll->last, interval);
	if (early && !ring)
		return 0;
	poll->started = 1;
	poll->last = now;
	poll->soon = 0;
	poll->rung = 0;
	poll->times[poll->next_time] = now;
	poll->rang[poll->next_time] = early;
	poll->next_time = (poll->next_time + 1) % SOCIAL_POLL_TIMES;
	if (poll->time_count < SOCIAL_POLL_TIMES)
		poll->time_count++;
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

void social_poll_ring(struct social_poll *poll)
{
	if (!poll->wait)
		poll->rung = 1;
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

int social_clan_rank(const char *rank)
{
	if (!rank)
		return 0;
	return !strcmp(rank, "leader") ? 3 : !strcmp(rank, "officer") ? 2 : !strcmp(rank, "member") ? 1 : 0;
}

int social_clan_can_invite(const char *rank)
{
	return social_clan_rank(rank) >= 2;
}

int social_clan_can_kick(const char *rank, const char *their_rank)
{
	int mine = social_clan_rank(rank), theirs = social_clan_rank(their_rank);

	return theirs > 0 && theirs < 3 && (mine == 3 || (mine == 2 && theirs == 1));
}

void social_clan_status(const char *rank, int you, int online, int playing, const struct social_game *game, char *text,
	int size)
{
	static const char *const words[] = { "Member", "Member", "Officer", "Leader" };
	char where[SOCIAL_STATUS_SIZE];
	const char *word = words[social_clan_rank(rank)];
	int length;

	if (size <= 0)
		return;
	if (you)
		length = snprintf(text, (size_t)size, "You \xC2\xB7 %s", word);
	else
	{
		if (!playing || social_game_line(game, where, sizeof(where)) < 0)
			social_copy_text(where, sizeof(where), playing ? "In a game" : online ? "Online" : "Offline");
		length = snprintf(text, (size_t)size, "%s \xC2\xB7 %s", word, where);
	}
	/* (cut at a whole character, as every line) */
	if (length < 0 || length >= size)
	{
		char whole[SOCIAL_STATUS_SIZE * 2];

		snprintf(whole, sizeof(whole), "%s \xC2\xB7 %s", word, you ? "" : where);
		social_copy_text(text, size, whole);
	}
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

/* ---------- RECENT */

void social_recent_forget(struct social_recent_seen *seen, const char *handle)
{
	int index;

	for (index = 0; index < seen->count; index++)
	{
		if (!strcmp(seen->players[index].handle, handle))
		{
			memmove(&seen->players[index], &seen->players[index + 1],
				(size_t)(seen->count - index - 1) * sizeof(seen->players[0]));
			seen->count--;
			return;
		}
	}
}

void social_recent_note(struct social_recent_seen *seen, const char *handle, const char *display, const char *map,
	long long time)
{
	struct social_recent player;

	if (!social_handle_valid(handle))
		return;
	memset(&player, 0, sizeof(player));
	social_copy_text(player.handle, sizeof(player.handle), handle);
	social_copy_text(player.display, sizeof(player.display), display ? display : "");
	social_copy_text(player.map, sizeof(player.map), map ? map : "");
	player.time = time;
	player.here = 1;
	social_recent_forget(seen, handle);
	if (seen->count >= SOCIAL_MAXIMUM_RECENT)
		seen->count = SOCIAL_MAXIMUM_RECENT - 1;
	memmove(&seen->players[1], &seen->players[0], (size_t)seen->count * sizeof(seen->players[0]));
	seen->players[0] = player;
	seen->count++;
}

static int recent_has(const struct social_recent *list, int count, const char *handle)
{
	int index;

	for (index = 0; index < count; index++)
	{
		if (!strcmp(list[index].handle, handle))
			return 1;
	}
	return 0;
}

int social_recent_merge(const struct social_state *state, const struct social_recent_seen *seen,
	const char *own_handle, struct social_recent *out, int most)
{
	int count = 0, index;

	for (index = 0; seen && index < seen->count && count < most; index++)
	{
		const struct social_recent *player = &seen->players[index];
		int site;

		if ((own_handle && !strcmp(player->handle, own_handle)) || recent_has(out, count, player->handle))
			continue;
		out[count] = *player;
		out[count].is_friend = find_friend(state, player->handle) != NULL;
		/* (what the site knows of them: its name, and a request waiting) */
		for (site = 0; site < state->recent_count; site++)
		{
			if (!strcmp(state->recent[site].handle, player->handle))
			{
				out[count].requested = state->recent[site].requested;
				if (state->recent[site].display[0])
					social_copy_text(out[count].display, sizeof(out[count].display), state->recent[site].display);
			}
		}
		count++;
	}
	for (index = 0; index < state->recent_count && count < most; index++)
	{
		const struct social_recent *player = &state->recent[index];

		if (!social_handle_valid(player->handle) || (own_handle && !strcmp(player->handle, own_handle)) ||
			recent_has(out, count, player->handle))
		{
			continue;
		}
		out[count] = *player;
		out[count].here = 0;
		out[count].is_friend = player->is_friend || find_friend(state, player->handle) != NULL;
		count++;
	}
	return count;
}

void social_recent_status(const struct social_recent *player, long long now, char *text, int size)
{
	char map[SOCIAL_GAME_TEXT_SIZE];
	char when[32];
	long long age = now - player->time;

	if (size <= 0)
		return;
	if (player->time <= 0 || age < 0)
		when[0] = 0;
	else if (age < 120)
		snprintf(when, sizeof(when), "Just now");
	else if (age < 3600)
		snprintf(when, sizeof(when), "%lld minutes ago", age / 60);
	else if (age < 86400)
		snprintf(when, sizeof(when), "%lld hour%s ago", age / 3600, age / 3600 == 1 ? "" : "s");
	else if (age < 30 * 86400LL)
		snprintf(when, sizeof(when), "%lld day%s ago", age / 86400, age / 86400 == 1 ? "" : "s");
	else
		snprintf(when, sizeof(when), "A while ago");
	map[0] = 0;
	if (player->map[0])
		discord_activity_map_name(player->map, 0, map, (int)sizeof(map));
	if (map[0] && when[0])
		snprintf(text, (size_t)size, "%s \xC2\xB7 %s", map, when);
	else
		social_copy_text(text, size, map[0] ? map : when);
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
		return fields_done(fields, size, snprintf(fields, (size_t)size, "\"since\": \"%s\", \"limit\": %d, \"clan\": true",
			cursor, limit));
	return fields_done(fields, size, snprintf(fields, (size_t)size, "\"limit\": %d, \"clan\": true", limit));
}

int social_action_fields(char *fields, int size, const char *action, const char *handle)
{
	if (size <= 0)
		return -1;
	fields[0] = 0;
	if (!social_handle_valid(handle) || !action ||
		(strcmp(action, "accept") && strcmp(action, "decline") && strcmp(action, "remove") && strcmp(action, "block") &&
			strcmp(action, "request") && strcmp(action, "cancel") && strcmp(action, "unblock")))
	{
		return -1;
	}
	return fields_done(fields, size, snprintf(fields, (size_t)size, "\"action\": \"%s\", \"handle\": \"%s\"",
		action, handle));
}

/* a value among a rule's (NULL after the last) */
static int one_of(const char *value, const char *const *values)
{
	for (; *values; values++)
	{
		if (!strcmp(value, *values))
			return 1;
	}
	return 0;
}

int social_friends_privacy_fields(char *fields, int size, const char *requests, const char *join,
	const char *clan_presence)
{
	static const char *const request_rules[] = { "everyone", "friends_of_friends", "nobody", NULL };
	static const char *const join_rules[] = { "anyone", "friends", "invite", NULL };
	static const char *const presence_rules[] = { "friends", "friends_and_clan", NULL };
	int length;

	if (size <= 0)
		return -1;
	fields[0] = 0;
	if ((requests && requests[0] && !one_of(requests, request_rules)) || (join && join[0] && !one_of(join, join_rules)) ||
		(clan_presence && clan_presence[0] && !one_of(clan_presence, presence_rules)) ||
		!((requests && requests[0]) || (join && join[0]) || (clan_presence && clan_presence[0])))
	{
		return -1;
	}
	length = snprintf(fields, (size_t)size, "\"action\": \"privacy\"");
	if (requests && requests[0] && length > 0 && length < size)
		length += snprintf(fields + length, (size_t)(size - length), ", \"requests\": \"%s\"", requests);
	if (join && join[0] && length > 0 && length < size)
		length += snprintf(fields + length, (size_t)(size - length), ", \"join\": \"%s\"", join);
	if (clan_presence && clan_presence[0] && length > 0 && length < size)
		length += snprintf(fields + length, (size_t)(size - length), ", \"clan_presence\": \"%s\"", clan_presence);
	return fields_done(fields, size, length);
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

/* ---------- parties */

static const char code_letters[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";

/* the length of text's start that is prefix, ignoring case (0 if it isn't) */
static size_t starts_with(const char *text, const char *prefix)
{
	size_t index;

	for (index = 0; prefix[index]; index++)
	{
		char c = text[index] >= 'A' && text[index] <= 'Z' ? (char)(text[index] - 'A' + 'a') : text[index];

		if (c != prefix[index])
			return 0;
	}
	return index;
}

/* the length of a party page's address at text's start
(https://stats.chupathingy.org/party/, any site's: http or https, a host,
then /party/), 0 if it isn't one */
static size_t party_page_prefix(const char *text)
{
	size_t length = starts_with(text, "https://");
	size_t host;

	if (!length)
		length = starts_with(text, "http://");
	if (!length)
		return 0;
	/* (a host's own letters, digits, dots and dashes, then the path) */
	for (host = 0;; host++)
	{
		char c = text[length + host];

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-'))
			break;
	}
	if (!host || !starts_with(text + length + host, "/party/"))
		return 0;
	return length + host + strlen("/party/");
}

int social_party_code_clean(const char *text, char *code)
{
	size_t prefix;
	int length = 0;

	code[0] = 0;
	if (!text)
		return 0;
	while (*text == ' ')
		text++;
	/* (a link: the game's, or the site's party page) */
	prefix = starts_with(text, "halo://party/");
	if (!prefix)
		prefix = party_page_prefix(text);
	text += prefix;
	for (; *text; text++)
	{
		char c = *text >= 'a' && *text <= 'z' ? (char)(*text - 'a' + 'A') : *text;

		if (c == '-' || c == ' ')
			continue;
		/* (a link's slash at its end; else the code ends there) */
		if (c == '/' || c == '\r' || c == '\n')
		{
			while (*text == '/' || *text == ' ' || *text == '\r' || *text == '\n')
				text++;
			if (*text)
				length = 0;
			break;
		}
		if (!strchr(code_letters, c) || length >= SOCIAL_PARTY_CODE_LENGTH)
		{
			code[0] = 0;
			return 0;
		}
		code[length++] = c;
	}
	code[length] = 0;
	if (length != SOCIAL_PARTY_CODE_LENGTH)
	{
		code[0] = 0;
		return 0;
	}
	return 1;
}

void social_party_code_shown(const char *code, char *text, int size)
{
	if (size <= 0)
		return;
	if (!code || (int)strlen(code) != SOCIAL_PARTY_CODE_LENGTH)
		social_copy_text(text, size, code ? code : "");
	else
		snprintf(text, (size_t)size, "%.3s-%.3s", code, code + 3);
}

/* a party's link in form ("...%s": the code goes there) */
static int party_link(const char *code, char *link, int size, const char *form)
{
	char clean[SOCIAL_PARTY_CODE_SIZE];

	if (size <= 0)
		return 0;
	link[0] = 0;
	if (!social_party_code_clean(code, clean))
		return 0;
	return fields_done(link, size, snprintf(link, (size_t)size, form, clean)) >= 0;
}

int social_party_link(const char *code, char *link, int size)
{
	/* (a web page's address: Discord and chat programs make it a link and
	show a preview, which they never do for halo://) */
	return party_link(code, link, size, SOCIAL_PARTY_PAGE "%s");
}

int social_party_app_link(const char *code, char *link, int size)
{
	return party_link(code, link, size, "halo://party/%s");
}

int social_party_tag_bytes(const char *tag, unsigned char *bytes)
{
	int index;

	if (!tag || strlen(tag) != 16)
		return 0;
	for (index = 0; index < 16; index++)
	{
		char c = tag[index];
		int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;

		if (digit < 0)
			return 0;
		if (index & 1)
			bytes[index / 2] = (unsigned char)(bytes[index / 2] | digit);
		else
			bytes[index / 2] = (unsigned char)(digit << 4);
	}
	return 1;
}

static int list_key_same(const struct social_list_key *a, const struct social_list_key *b)
{
	return a->kind == b->kind && a->index == b->index && !strncmp(a->id, b->id, SOCIAL_LIST_KEY_ID_SIZE);
}

int social_list_keep(const struct social_list_key *kept, int kept_row, const struct social_list_key *keys, int count,
	int row, int *changed)
{
	int index, moved = row != kept_row;

	*changed = 0;
	if (count <= 0)
	{
		*changed = kept != NULL;
		return 0;
	}
	if (row < 0)
		row = 0;
	if (row >= count)
		row = count - 1;
	if (!kept)
		return row;
	if (list_key_same(&keys[row], kept))
		return row;
	/* (the list changed under a row nobody moved: its item followed) */
	if (!moved)
	{
		for (index = 0; index < count; index++)
		{
			if (list_key_same(&keys[index], kept))
				return index;
		}
	}
	*changed = 1;
	return row;
}

int social_list_same_item(const struct social_list_key *drawn, int drawn_count, const struct social_list_key *keys,
	int count, int index)
{
	return index >= 0 && index < drawn_count && index < count && list_key_same(&drawn[index], &keys[index]);
}

const struct social_party_member *social_party_leader(const struct social_party *party)
{
	int index;

	for (index = 0; party && party->has && index < party->member_count; index++)
	{
		if (party->members[index].leader)
			return &party->members[index];
	}
	return NULL;
}

static const char *const party_rules[][2] =
{
	{ "anyone", "ANYONE" },
	{ "friends", "FRIENDS" },
	{ "invite", "INVITE ONLY" },
};

#define NUMBER_OF_PARTY_RULES ((int)(sizeof(party_rules) / sizeof(party_rules[0])))

static int party_rule_index(const char *rule)
{
	int index;

	for (index = 0; rule && index < NUMBER_OF_PARTY_RULES; index++)
	{
		if (!strcmp(rule, party_rules[index][0]))
			return index;
	}
	return 1;
}

const char *social_party_rule_shown(const char *rule)
{
	return party_rules[party_rule_index(rule)][1];
}

const char *social_party_rule_next(const char *rule, int direction)
{
	int index = party_rule_index(rule) + (direction < 0 ? NUMBER_OF_PARTY_RULES - 1 : 1);

	return party_rules[index % NUMBER_OF_PARTY_RULES][0];
}

/* text the site takes in a JSON string as it is: printable ASCII and UTF-8,
no quote or backslash */
static int plain_text(const char *text)
{
	for (; text && *text; text++)
	{
		if ((unsigned char)*text < 0x20 || *text == '"' || *text == '\\')
			return 0;
	}
	return 1;
}

static int hex_of_length(const char *text, size_t length)
{
	size_t index;

	if (!text || strlen(text) != length)
		return 0;
	for (index = 0; index < length; index++)
	{
		if (!((text[index] >= '0' && text[index] <= '9') || (text[index] >= 'a' && text[index] <= 'f')))
			return 0;
	}
	return 1;
}

/* the actions /v1/party takes (puma's PARTY_ACTIONS, "" the read aside):
each one DELTA sends is one of these (tools/test_social.py checks guide.c) */
const char *const social_party_actions[] =
{
	"create", "invite", "accept", "decline", "join", "leave", "kick", "lead", "rule", "code", "go", NULL,
};

/* text copied as plain_text would take it: what it wouldn't, left out */
static void plain_copy(char *to, int size, const char *text)
{
	int length = 0;

	if (size <= 0)
		return;
	for (; text && *text && length < size - 1; text++)
	{
		if ((unsigned char)*text >= 0x20 && *text != '"' && *text != '\\')
			to[length++] = *text;
	}
	/* (a UTF-8 character cut at the end goes too) */
	if (text && ((unsigned char)*text & 0xC0) == 0x80)
	{
		while (length > 0 && ((unsigned char)to[length - 1] & 0xC0) == 0x80)
			length--;
		if (length > 0 && ((unsigned char)to[length - 1] & 0xC0) == 0xC0)
			length--;
	}
	to[length] = 0;
}

int social_party_fields(char *fields, int size, const char *action, const char *value, const char *name,
	const char *platform)
{
	char extra[160], code[SOCIAL_PARTY_CODE_SIZE], who[64];
	int length;

	if (size <= 0)
		return -1;
	fields[0] = 0;
	extra[0] = 0;
	if (!action || !plain_text(platform))
		return -1;
	if (!strcmp(action, "invite"))
	{
		if (!social_handle_valid(value))
			return -1;
		snprintf(extra, sizeof(extra), ", \"handle\": \"%s\"", value);
	}
	else if (!strcmp(action, "accept") || !strcmp(action, "decline"))
	{
		if (!hex_of_length(value, 16))
			return -1;
		snprintf(extra, sizeof(extra), ", \"party\": \"%s\"", value);
	}
	else if (!strcmp(action, "join"))
	{
		if (!social_party_code_clean(value, code))
			return -1;
		snprintf(extra, sizeof(extra), ", \"code\": \"%s\"", code);
	}
	else if (!strcmp(action, "kick") || !strcmp(action, "lead"))
	{
		if (!hex_of_length(value, 8))
			return -1;
		snprintf(extra, sizeof(extra), ", \"member\": \"%s\"", value);
	}
	else if (!strcmp(action, "rule"))
	{
		if (!value || (strcmp(value, "anyone") && strcmp(value, "friends") && strcmp(value, "invite")))
			return -1;
		snprintf(extra, sizeof(extra), ", \"join\": \"%s\"", value);
	}
	else if (!strcmp(action, "go"))
	{
		if (!social_presence_invite_valid(value))
			return -1;
		snprintf(extra, sizeof(extra), ", \"invite\": \"%s\"", value);
	}
	else if (strcmp(action, "") && strcmp(action, "create") && strcmp(action, "leave") && strcmp(action, "code"))
		return -1;
	/* (a game name is the player's own typing: a quote or a backslash in it
	is left out, not a reason to send nothing) */
	plain_copy(who, sizeof(who), name ? name : "");
	length = snprintf(fields, (size_t)size, "\"action\": \"%s\"%s", action, extra);
	if (length > 0 && length < size && who[0])
		length += snprintf(fields + length, (size_t)(size - length), ", \"name\": \"%s\"", who);
	if (length > 0 && length < size && platform && platform[0])
		length += snprintf(fields + length, (size_t)(size - length), ", \"platform\": \"%s\"", platform);
	return fields_done(fields, size, length);
}

static const char *const party_reasons[][2] =
{
	{ "in_party", "You're in a party already." },
	{ "no_party", "You're not in a party." },
	{ "not_leader", "Only the leader can do that." },
	{ "full", "The party is full." },
	{ "friends_only", "Only the leader's friends can join." },
	{ "invite_only", "This party is invite only." },
	{ "kicked", "The leader took you out of this party." },
	{ "unknown", "No party has that code." },
	{ "gone", "That invite is gone." },
	{ "signed_out", "Sign in to invite friends." },
	{ "member", "They're in the party already." },
	{ "busy", "Too many tries. Wait a minute." },
};

const char *social_party_refusal(const char *reason, int status)
{
	size_t index;

	for (index = 0; reason && index < sizeof(party_reasons) / sizeof(party_reasons[0]); index++)
	{
		if (!strcmp(reason, party_reasons[index][0]))
			return party_reasons[index][1];
	}
	if (status == 403)
		return "Friends only.";
	if (status == 429)
		return "Too many tries. Wait a minute.";
	return "Can't do that right now.";
}

/* an invite's refusals say it as the sender sees it (the same reasons, the
site's party.py and list_server.py, read for the one who invited) */
static const char *const party_invite_reasons[][2] =
{
	{ "no_party", "Start a party first." },
	{ "signed_out", "Sign in to invite friends." },
	{ "you", "That's you." },
	{ "friends_only", "You can invite friends only." },
	{ "member", "They're in your party already." },
	{ "full", "Your party is full." },
	{ "busy", "Too many invites. Wait a minute." },
	{ "unknown", "No profile has that handle." },
};

const char *social_party_action_refusal(const char *action, const char *reason, int status)
{
	size_t index;

	if (action && !strcmp(action, "invite"))
	{
		for (index = 0; reason && index < sizeof(party_invite_reasons) / sizeof(party_invite_reasons[0]); index++)
		{
			if (!strcmp(reason, party_invite_reasons[index][0]))
				return party_invite_reasons[index][1];
		}
		if (status == 403)
			return "You can invite friends only.";
		if (status == 429)
			return "Too many invites. Wait a minute.";
		return "Couldn't send the invite. Try again.";
	}
	return social_party_refusal(reason, status);
}

/* ---------- toasts waiting */

void social_toast_forget(struct social_toast_queue *queue, int kind, const char *key)
{
	int index = 0;

	while (queue && key && index < queue->count)
	{
		if (queue->toasts[index].kind == kind && !strcmp(queue->toasts[index].key, key))
		{
			memmove(&queue->toasts[index], &queue->toasts[index + 1],
				(size_t)(queue->count - index - 1) * sizeof(queue->toasts[0]));
			queue->count--;
		}
		else
			index++;
	}
}

void social_toast_push(struct social_toast_queue *queue, int kind, const char *key, const char *title, const char *line,
	const char *name)
{
	struct social_toast *toast;

	if (!queue)
		return;
	social_toast_forget(queue, kind, key ? key : "");
	if (queue->count >= SOCIAL_TOAST_QUEUE_MOST)
	{
		memmove(&queue->toasts[0], &queue->toasts[1], (SOCIAL_TOAST_QUEUE_MOST - 1) * sizeof(queue->toasts[0]));
		queue->count = SOCIAL_TOAST_QUEUE_MOST - 1;
	}
	toast = &queue->toasts[queue->count++];
	toast->kind = kind;
	social_copy_text(toast->key, sizeof(toast->key), key ? key : "");
	social_copy_text(toast->title, sizeof(toast->title), title ? title : "");
	social_copy_text(toast->line, sizeof(toast->line), line ? line : "");
	social_copy_text(toast->name, sizeof(toast->name), name ? name : "");
}

int social_toast_pop(struct social_toast_queue *queue, struct social_toast *toast)
{
	if (!queue || queue->count <= 0)
		return 0;
	*toast = queue->toasts[0];
	memmove(&queue->toasts[0], &queue->toasts[1], (size_t)(queue->count - 1) * sizeof(queue->toasts[0]));
	queue->count--;
	return 1;
}

/* ---------- invites this player sent */

void social_sent_add(struct social_sent_list *list, const char *handle, int kind, unsigned long now)
{
	int index;

	if (!list || !handle || !handle[0])
		return;
	social_sent_forget(list, handle, kind);
	if (list->count >= SOCIAL_MAXIMUM_SENT)
	{
		memmove(&list->sent[0], &list->sent[1], (SOCIAL_MAXIMUM_SENT - 1) * sizeof(list->sent[0]));
		list->count = SOCIAL_MAXIMUM_SENT - 1;
	}
	index = list->count++;
	social_copy_text(list->sent[index].handle, sizeof(list->sent[index].handle), handle);
	list->sent[index].kind = kind;
	list->sent[index].time = now;
}

int social_sent_has(const struct social_sent_list *list, const char *handle, int kind, unsigned long now)
{
	int index;

	for (index = 0; list && handle && index < list->count; index++)
	{
		const struct social_sent *sent = &list->sent[index];

		if (sent->kind == kind && !strcmp(sent->handle, handle) && now - sent->time < SOCIAL_SENT_LIFETIME)
			return 1;
	}
	return 0;
}

void social_sent_forget(struct social_sent_list *list, const char *handle, int kind)
{
	int index = 0;

	while (list && index < list->count)
	{
		if (list->sent[index].kind == kind && (!handle || !strcmp(list->sent[index].handle, handle)))
		{
			memmove(&list->sent[index], &list->sent[index + 1], (size_t)(list->count - index - 1) * sizeof(list->sent[0]));
			list->count--;
		}
		else
			index++;
	}
}

void social_sent_line(char *text, int size, int kind, const char *name, int online, const char *refusal)
{
	if (!text || size <= 0)
		return;
	if (refusal && refusal[0])
	{
		social_copy_text(text, size, refusal);
		return;
	}
	if (!name || !name[0])
		name = "them";
	if (kind == SOCIAL_SENT_REQUEST)
		snprintf(text, (size_t)size, "Request sent to %s", name);
	else if (!online)
		snprintf(text, (size_t)size, "Invite sent to %s. It waits 5 minutes for them.", name);
	else
		snprintf(text, (size_t)size, "Invite sent to %s", name);
}

/* the code of an invite or a halo://join link ("" for none) */
static const char *join_code(const char *link)
{
	static const char prefix[] = "halo://join/";

	if (!link)
		return "";
	return !strncmp(link, prefix, sizeof(prefix) - 1) ? link + sizeof(prefix) - 1 : link;
}

int social_follow_mode(const char *text)
{
	if (text && !strcmp(text, "always"))
		return SOCIAL_FOLLOW_ALWAYS;
	if (text && !strcmp(text, "never"))
		return SOCIAL_FOLLOW_NEVER;
	return SOCIAL_FOLLOW_ASK;
}

const char *social_follow_mode_next(const char *text)
{
	switch (social_follow_mode(text))
	{
	case SOCIAL_FOLLOW_ASK: return "always";
	case SOCIAL_FOLLOW_ALWAYS: return "never";
	default: return "ask";
	}
}

int social_follow_timeout_joins(int mode, int in_game)
{
	if (mode == SOCIAL_FOLLOW_NEVER)
		return 0;
	return mode == SOCIAL_FOLLOW_ALWAYS || !in_game;
}

int social_follow_decide(const struct social_party *party, const struct social_follow_inputs *inputs)
{
	const struct social_party_go *go;

	if (!party || !party->has || party->leader || !party->go.has)
		return SOCIAL_FOLLOW_NONE;
	go = &party->go;
	if (go->serial <= inputs->seen_serial || inputs->mode == SOCIAL_FOLLOW_NEVER)
		return SOCIAL_FOLLOW_NONE;
	if (go->join[0] && inputs->current_invite && inputs->current_invite[0] &&
		!strcmp(join_code(go->join), join_code(inputs->current_invite)))
	{
		return SOCIAL_FOLLOW_THERE;
	}
	if (!go->join[0] || inputs->hosting_others)
		return SOCIAL_FOLLOW_CANT;
	return SOCIAL_FOLLOW_OFFER;
}

void social_follow_reason(const struct social_party *party, const struct social_follow_inputs *inputs, char *text,
	int size)
{
	if (size <= 0)
		return;
	if (inputs->hosting_others && party->go.join[0])
		social_copy_text(text, size, "Can't follow: you're hosting a game.");
	else if (party->go.error[0])
		social_copy_text(text, size, party->go.error);
	else
		social_copy_text(text, size, "Can't follow right now.");
}

void social_follow_line(const struct social_party *party, char *text, int size)
{
	const struct social_party_member *leader = social_party_leader(party);
	char map[SOCIAL_GAME_TEXT_SIZE];

	if (size <= 0)
		return;
	if (party->go.game.listed && party->go.game.map[0])
	{
		discord_activity_map_name(party->go.game.map, party->go.game.coop, map, sizeof(map));
		snprintf(text, (size_t)size, "Following %s to %s", leader ? leader->name : "the leader", map);
	}
	else
		snprintf(text, (size_t)size, "Following %s", leader ? leader->name : "the leader");
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
