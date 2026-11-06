/*
BOT_RVB.C

The bots' cast (test build): the Reds and the Blues of Blood Gulch. Each
bot is a character with a name (no "[Bot]": the list's bot count and the
server's "Bots" machines say who is one), a side for team games, a color for
free-for-all games and a skill offset from bots.skill; and it talks in the
game's chat (bots.chatter), as the host's players do, so that every machine,
OpenCE's too, shows it as an ordinary player's line.

Dedicated server only (HALO_SERVER), as all of the bots' code is.
*/

#ifdef HALO_SERVER

#include "bot_internal.h"

#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *config_string(char const *name);
/* the host's chat (chat.c): a line said as a player of this game */
int chat_bot_say(wchar_t const *name, char const *text);
/* (game_time.c) */
long game_time_get(void);

/* ---------- the cast */

struct bot_rvb_character
{
	char const *name;
	/* 0 red, 1 blue, NONE either */
	long team;
	/* its color in free-for-all games (the game's 18) */
	long color;
	/* levels above or below bots.skill */
	long skill_offset;
};

enum
{
	_color_white, _color_black, _color_red, _color_blue, _color_gray, _color_yellow, _color_green, _color_pink,
	_color_purple, _color_cyan, _color_cobalt, _color_orange, _color_teal, _color_sage, _color_brown, _color_tan,
	_color_maroon, _color_salmon
};

/* the default eight first (an even four on four), then the rest in the
order they join */
static struct bot_rvb_character const bot_rvb_cast[] = {
	{ "Sarge", 0, _color_red, 1 },
	{ "Grif", 0, _color_orange, -2 },
	{ "Simmons", 0, _color_maroon, 0 },
	{ "Donut", 0, _color_pink, 0 },
	{ "Church", 1, _color_cyan, -1 },
	{ "Tucker", 1, _color_teal, 0 },
	{ "Caboose", 1, _color_blue, -1 },
	{ "Tex", 1, _color_black, 3 },
	{ "Lopez", 0, _color_brown, 0 },
	{ "Doc", NONE, _color_purple, -1 },
	{ "Sister", 0, _color_yellow, 0 },
	{ "Washington", 1, _color_gray, 1 },
	{ "Carolina", 1, _color_cobalt, 2 },
	{ "Meta", NONE, _color_white, 2 },
	{ "York", NONE, _color_tan, 1 },
	{ "N. Dakota", NONE, _color_sage, 1 },
	{ "S. Dakota", NONE, _color_green, 1 },
	{ "Wyoming", NONE, _color_salmon, 1 },
};
#define BOT_RVB_CAST_COUNT ((long)(sizeof(bot_rvb_cast) / sizeof(bot_rvb_cast[0])))

char const *bot_name(long slot)
{
	static char names[8][16];
	static long next;
	char *name;

	if (slot >= 0 && slot < BOT_RVB_CAST_COUNT)
		return bot_rvb_cast[slot].name;
	name = names[next++ & 7];
	/* (past the roster: numbered, at most 11 characters) */
	snprintf(name, sizeof(names[0]), "%.8s %ld", bot_rvb_cast[slot % 8].name, slot / 8);
	name[11] = 0;
	return name;
}

/* the character of a name (NONE: none of the cast) */
static long bot_rvb_character_of_name(wchar_t const *name)
{
	long index;

	if (!name)
		return NONE;
	for (index = 0; index < BOT_RVB_CAST_COUNT; index++)
	{
		char const *a = bot_rvb_cast[index].name;
		long i;

		for (i = 0; a[i] && name[i] == (wchar_t)(unsigned char)a[i]; i++)
			;
		if (!a[i] && !name[i])
			return index;
	}
	return NONE;
}

long bot_rvb_character_of(wchar_t const *name)
{
	return bot_rvb_character_of_name(name);
}

long bot_rvb_team(long character)
{
	return character >= 0 && character < BOT_RVB_CAST_COUNT ? bot_rvb_cast[character].team : NONE;
}

long bot_rvb_color(long character)
{
	return character >= 0 && character < BOT_RVB_CAST_COUNT ? bot_rvb_cast[character].color : NONE;
}

long bot_rvb_skill_offset(long character)
{
	return character >= 0 && character < BOT_RVB_CAST_COUNT ? bot_rvb_cast[character].skill_offset : 0;
}

/* ---------- chatter */

enum bot_rvb_event
{
	_say_kill,
	_say_death,
	_say_killed_by_person,
	_say_died_to,
	_say_spree,
	_say_idle,
	_say_join,
	_say_leave,
	_say_start,
	_say_end,
	_say_ambient,
	NUMBER_OF_BOT_RVB_EVENTS
};

struct bot_rvb_line
{
	signed char character;
	signed char event;
	signed char other;
	char const *text;
};

#include "bot_rvb_lines.h"

#define BOT_RVB_LINE_COUNT ((long)(sizeof(bot_rvb_lines) / sizeof(bot_rvb_lines[0])))
#define SECONDS(s) ((long)((s) * BOT_TICKS_PER_SECOND))

enum
{
	_chatter_off,
	_chatter_light,
	_chatter_full
};

/* each bot's line waiting for its turn (one at a time: a newer one of at
least its priority takes its place), and when it may speak again */
static struct
{
	char text[96];
	long queued;
	long expires;
	int priority;
	long next_allowed;
	/* the kills since its last death, and when it last killed or died */
	long streak;
	long last_action;
	wchar_t name[12];
	boolean present;
} bot_rvb_bots[BOT_MAXIMUM_PLAYERS];

/* the people in the game (for the joins and leaves) */
static struct
{
	wchar_t name[12];
	long since;
	boolean present, told;
} bot_rvb_people[BOT_MAXIMUM_PLAYERS];

static struct
{
	int level;
	boolean level_read;
	long last_tick;
	long next_line;
	long next_ambient;
	boolean started, ended;
	unsigned long random;
} bot_rvb;

static int bot_rvb_level(void)
{
	if (!bot_rvb.level_read)
	{
		char const *setting = config_string("bots.chatter");

		bot_rvb.level_read = TRUE;
		bot_rvb.level = !setting || !strcmp(setting, "light") ? _chatter_light :
			!strcmp(setting, "full") ? _chatter_full : _chatter_off;
		bot_rvb.random = (unsigned long)bot_time_microseconds() | 1;
	}
	return bot_rvb.level;
}

/* (the host's own draw: chatter is no part of the simulation, and takes
nothing from the game's random numbers) */
static unsigned long bot_rvb_next(void)
{
	bot_rvb.random ^= bot_rvb.random << 13;
	bot_rvb.random ^= bot_rvb.random >> 17;
	bot_rvb.random ^= bot_rvb.random << 5;
	return bot_rvb.random;
}

static boolean bot_rvb_chance(real full, real light)
{
	real chance = bot_rvb.level == _chatter_full ? full : light;

	return (real)(bot_rvb_next() % 10000) < chance * 10000.f;
}

static void bot_rvb_ascii_name(wchar_t const *name, char *out, int size)
{
	int i;

	for (i = 0; i < size - 1 && name && name[i]; i++)
		out[i] = name[i] >= 32 && name[i] < 127 ? (char)name[i] : '?';
	out[i] = 0;
}

/* a line of this character for this event (about that one, if it has
one), "%n" made the other's name; FALSE if it has none */
static boolean bot_rvb_pick(long character, int event, long other_character, wchar_t const *other_name, char *out,
	int size)
{
	long matches = 0, pick, index;
	boolean specific = FALSE;
	char name[16];
	char const *text = NULL;

	/* (a line about the other one, when there is one) */
	if (other_character != NONE)
	{
		for (index = 0; index < BOT_RVB_LINE_COUNT; index++)
		{
			if (bot_rvb_lines[index].character == character && bot_rvb_lines[index].event == event &&
				bot_rvb_lines[index].other == other_character)
			{
				matches++;
			}
		}
		specific = matches > 0;
	}
	if (!specific)
	{
		for (index = 0; index < BOT_RVB_LINE_COUNT; index++)
		{
			if (bot_rvb_lines[index].character == character && bot_rvb_lines[index].event == event &&
				bot_rvb_lines[index].other == NONE)
			{
				matches++;
			}
		}
	}
	if (!matches)
		return FALSE;
	pick = (long)(bot_rvb_next() % (unsigned long)matches);
	for (index = 0; index < BOT_RVB_LINE_COUNT && !text; index++)
	{
		if (bot_rvb_lines[index].character == character && bot_rvb_lines[index].event == event &&
			bot_rvb_lines[index].other == (specific ? other_character : NONE) && pick-- == 0)
		{
			text = bot_rvb_lines[index].text;
		}
	}
	if (!text)
		return FALSE;
	bot_rvb_ascii_name(other_name, name, (int)sizeof(name));
	{
		int used = 0;

		while (*text && used < size - 1)
		{
			if (text[0] == '%' && text[1] == 'n')
			{
				int i;

				for (i = 0; name[i] && used < size - 1; i++)
					out[used++] = name[i];
				text += 2;
			}
			else
				out[used++] = *text++;
		}
		out[used] = 0;
	}
	return TRUE;
}

static struct player_datum *bot_rvb_player(long absolute_index)
{
	struct data_iterator iterator;
	struct player_datum *player;

	if (!player_data || absolute_index < 0)
		return NULL;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index) == absolute_index)
			return player;
	}
	return NULL;
}

/* the character of a player that is one of the bots (NONE otherwise) */
static long bot_rvb_bot_character(long absolute_index, struct player_datum **out)
{
	struct player_datum *player = bot_rvb_player(absolute_index);

	if (out)
		*out = player;
	if (!player || !bot_player_is_bot(absolute_index))
		return NONE;
	return bot_rvb_character_of_name(player->name);
}

/* a line for this bot, if its character has one: waits for its turn */
static void bot_rvb_queue(long absolute_index, int event, long other_index, int priority)
{
	struct player_datum *player, *other = NULL;
	long character = bot_rvb_bot_character(absolute_index, &player);
	long other_character = NONE;
	char text[96];
	long now = game_time_get();

	if (character == NONE || absolute_index >= BOT_MAXIMUM_PLAYERS)
		return;
	if (other_index != NONE)
	{
		other = bot_rvb_player(other_index);
		if (other && bot_player_is_bot(other_index))
			other_character = bot_rvb_character_of_name(other->name);
	}
	if (bot_rvb_bots[absolute_index].text[0] && bot_rvb_bots[absolute_index].priority > priority &&
		now < bot_rvb_bots[absolute_index].expires)
	{
		return;
	}
	if (!bot_rvb_pick(character, event, other_character, other ? other->name : NULL, text, (int)sizeof(text)))
		return;
	strncpy(bot_rvb_bots[absolute_index].text, text, sizeof(bot_rvb_bots[0].text) - 1);
	bot_rvb_bots[absolute_index].text[sizeof(bot_rvb_bots[0].text) - 1] = 0;
	bot_rvb_bots[absolute_index].queued = now;
	bot_rvb_bots[absolute_index].expires = now + SECONDS(priority >= 2 ? 8 : 5);
	bot_rvb_bots[absolute_index].priority = priority;
	csmemcpy(bot_rvb_bots[absolute_index].name, player->name, sizeof(bot_rvb_bots[0].name));
}

/* a random bot of the cast (NONE: none), not this one */
static long bot_rvb_any_bot(long not_this)
{
	long candidates[BOT_MAXIMUM_PLAYERS];
	long count = 0, index;

	for (index = 0; index < BOT_MAXIMUM_PLAYERS; index++)
	{
		if (index != not_this && bot_rvb_bots[index].present)
			candidates[count++] = index;
	}
	return count ? candidates[bot_rvb_next() % (unsigned long)count] : NONE;
}

/* (bots.c, as a player dies: who, and who killed them, NONE for nobody) */
void bot_rvb_death(long victim, long killer)
{
	long now = game_time_get();

	if (bot_rvb_level() == _chatter_off)
		return;
	if (victim >= 0 && victim < BOT_MAXIMUM_PLAYERS)
	{
		bot_rvb_bots[victim].streak = 0;
		bot_rvb_bots[victim].last_action = now;
	}
	if (killer != NONE && killer != victim && killer >= 0 && killer < BOT_MAXIMUM_PLAYERS &&
		bot_player_is_bot(killer))
	{
		long streak = ++bot_rvb_bots[killer].streak;
		struct player_datum *dead = bot_rvb_player(victim);
		boolean pair = dead && bot_player_is_bot(victim) &&
			bot_rvb_character_of_name(dead->name) != NONE;

		bot_rvb_bots[killer].last_action = now;
		if (streak == 3 || streak == 5 || streak == 8 || streak == 12)
			bot_rvb_queue(killer, _say_spree, NONE, 2);
		else if ((pair && bot_rvb_chance(0.85f, 0.4f)) || bot_rvb_chance(0.55f, 0.15f))
			bot_rvb_queue(killer, _say_kill, victim, pair ? 1 : 0);
	}
	if (victim >= 0 && bot_player_is_bot(victim))
	{
		boolean by_person = killer != NONE && killer != victim && !bot_player_is_bot(killer);
		boolean by_bot = killer != NONE && killer != victim && !by_person;

		if (by_bot && bot_rvb_chance(0.7f, 0.3f))
			bot_rvb_queue(victim, _say_died_to, killer, 1);
		else if (by_person && bot_rvb_chance(0.6f, 0.25f))
			bot_rvb_queue(victim, _say_killed_by_person, killer, 1);
		else if (bot_rvb_chance(0.35f, 0.1f))
			bot_rvb_queue(victim, _say_death, NONE, 0);
	}
}

/* the bots and people now; joins and leaves told */
static void bot_rvb_roster(long now)
{
	boolean seen_bot[BOT_MAXIMUM_PLAYERS];
	boolean seen_person[BOT_MAXIMUM_PLAYERS];
	struct data_iterator iterator;
	struct player_datum *player;
	long index;

	csmemset(seen_bot, 0, sizeof(seen_bot));
	csmemset(seen_person, 0, sizeof(seen_person));
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);

		if (absolute >= BOT_MAXIMUM_PLAYERS || player->quit_out_of_game)
			continue;
		if (bot_player_is_bot(absolute))
		{
			seen_bot[absolute] = bot_rvb_character_of_name(player->name) != NONE;
			if (!bot_rvb_bots[absolute].present && seen_bot[absolute])
				bot_rvb_bots[absolute].last_action = now;
			continue;
		}
		seen_person[absolute] = TRUE;
		if (!bot_rvb_people[absolute].present ||
			memcmp(bot_rvb_people[absolute].name, player->name, sizeof(player->name)))
		{
			csmemcpy(bot_rvb_people[absolute].name, player->name, sizeof(player->name));
			bot_rvb_people[absolute].present = TRUE;
			bot_rvb_people[absolute].since = now;
			/* (those in the game as it starts: no welcome) */
			bot_rvb_people[absolute].told = now < SECONDS(5);
		}
		else if (!bot_rvb_people[absolute].told && now - bot_rvb_people[absolute].since >= SECONDS(3))
		{
			long speaker = bot_rvb_any_bot(NONE);

			bot_rvb_people[absolute].told = TRUE;
			if (speaker != NONE && bot_rvb_chance(1.f, 0.5f))
				bot_rvb_queue(speaker, _say_join, absolute, 1);
		}
	}
	for (index = 0; index < BOT_MAXIMUM_PLAYERS; index++)
	{
		bot_rvb_bots[index].present = seen_bot[index];
		if (!seen_bot[index])
			bot_rvb_bots[index].text[0] = 0;
		if (bot_rvb_people[index].present && !seen_person[index])
		{
			long speaker = bot_rvb_any_bot(NONE);

			if (bot_rvb_people[index].told && speaker != NONE && bot_rvb_chance(0.9f, 0.4f))
			{
				/* (the name, the player gone: said by hand) */
				char text[96];
				long character = bot_rvb_character_of_name(bot_rvb_bots[speaker].name);

				if (character != NONE &&
					bot_rvb_pick(character, _say_leave, NONE, bot_rvb_people[index].name, text, (int)sizeof(text)))
				{
					strncpy(bot_rvb_bots[speaker].text, text, sizeof(bot_rvb_bots[0].text) - 1);
					bot_rvb_bots[speaker].queued = now;
					bot_rvb_bots[speaker].expires = now + SECONDS(6);
					bot_rvb_bots[speaker].priority = 1;
				}
			}
			bot_rvb_people[index].present = FALSE;
		}
	}
}

/* (bots.c, once a tick of a game with bots, on the host) */
void bot_rvb_tick(void)
{
	long now = game_time_get();
	long index, best = NONE;
	long per_bot = bot_rvb_level() == _chatter_full ? SECONDS(6) : SECONDS(20);
	long server_wide = bot_rvb.level == _chatter_full ? SECONDS(2) : SECONDS(8);

	if (bot_rvb.level == _chatter_off || !player_data)
		return;
	/* (a new game: everything of the last forgotten) */
	if (now < bot_rvb.last_tick)
	{
		long level = bot_rvb.level;
		unsigned long random = bot_rvb.random;

		csmemset(bot_rvb_bots, 0, sizeof(bot_rvb_bots));
		csmemset(bot_rvb_people, 0, sizeof(bot_rvb_people));
		csmemset(&bot_rvb, 0, sizeof(bot_rvb));
		bot_rvb.level = (int)level;
		bot_rvb.level_read = TRUE;
		bot_rvb.random = random;
	}
	if (now == bot_rvb.last_tick)
		return;
	bot_rvb.last_tick = now;
	if (now % BOT_TICKS_PER_SECOND == 0)
		bot_rvb_roster(now);
	/* the start, the end */
	if (!bot_rvb.started && now >= SECONDS(4))
	{
		long first = bot_rvb_any_bot(NONE);

		bot_rvb.started = TRUE;
		bot_rvb.next_ambient = now + (bot_rvb.level == _chatter_full ? SECONDS(25) : SECONDS(120));
		if (first != NONE)
		{
			bot_rvb_queue(first, _say_start, NONE, 2);
			if (bot_rvb.level == _chatter_full)
				bot_rvb_queue(bot_rvb_any_bot(first), _say_start, NONE, 2);
		}
	}
	if (!bot_rvb.ended && game_engine_showing_postgame())
	{
		long first = bot_rvb_any_bot(NONE), second;

		bot_rvb.ended = TRUE;
		if (first != NONE)
		{
			bot_rvb_queue(first, _say_end, NONE, 3);
			second = bot_rvb_any_bot(first);
			if (second != NONE && bot_rvb.level == _chatter_full)
				bot_rvb_queue(second, _say_end, NONE, 3);
		}
	}
	if (bot_rvb.ended)
		per_bot = 0;
	/* a while with nothing to do; now and then a word for no reason */
	if (bot_rvb.started && !bot_rvb.ended && now % BOT_TICKS_PER_SECOND == 15)
	{
		for (index = 0; index < BOT_MAXIMUM_PLAYERS; index++)
		{
			if (bot_rvb_bots[index].present && now - bot_rvb_bots[index].last_action > SECONDS(50))
			{
				bot_rvb_bots[index].last_action = now;
				if (bot_rvb_chance(0.8f, 0.3f))
					bot_rvb_queue(index, _say_idle, NONE, 0);
			}
		}
		if (now >= bot_rvb.next_ambient)
		{
			long speaker = bot_rvb_any_bot(NONE);

			bot_rvb.next_ambient = now + (bot_rvb.level == _chatter_full ? SECONDS(20 + bot_rvb_next() % 20) :
				SECONDS(100 + bot_rvb_next() % 80));
			if (speaker != NONE)
				bot_rvb_queue(speaker, _say_ambient, NONE, 0);
		}
	}
	/* one line at a time, the server's pace and each bot's */
	if (now < bot_rvb.next_line)
		return;
	for (index = 0; index < BOT_MAXIMUM_PLAYERS; index++)
	{
		if (!bot_rvb_bots[index].text[0])
			continue;
		if (now >= bot_rvb_bots[index].expires || !bot_rvb_bots[index].present)
		{
			bot_rvb_bots[index].text[0] = 0;
			continue;
		}
		if (now < bot_rvb_bots[index].next_allowed)
			continue;
		if (best == NONE || bot_rvb_bots[index].priority > bot_rvb_bots[best].priority ||
			(bot_rvb_bots[index].priority == bot_rvb_bots[best].priority &&
				bot_rvb_bots[index].queued < bot_rvb_bots[best].queued))
		{
			best = index;
		}
	}
	if (best == NONE)
		return;
	chat_bot_say(bot_rvb_bots[best].name, bot_rvb_bots[best].text);
	bot_rvb_bots[best].text[0] = 0;
	bot_rvb_bots[best].next_allowed = now + per_bot;
	bot_rvb.next_line = now + server_wide;
}

#endif /* HALO_SERVER */
