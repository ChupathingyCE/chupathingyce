/*
BOT_PORT.C

What the bots take of the platform (bot_port.h): a clock, and with it what
their parts cost (HALO_BOT_PROFILE).
*/

#include "cseries.h"
#include "bot_port.h"

#include <xtl.h>

/* microseconds, from the platform's performance counter (seeds, and timing
the navigation's build and the bots' own cost) */
unsigned long long bot_time_microseconds(void)
{
	static LARGE_INTEGER frequency;
	LARGE_INTEGER counter;

	if (!frequency.QuadPart)
	{
		QueryPerformanceFrequency(&frequency);
		if (frequency.QuadPart <= 0)
			frequency.QuadPart = 1;
	}
	QueryPerformanceCounter(&counter);
	return (unsigned long long)(counter.QuadPart / frequency.QuadPart) * 1000000ull +
		(unsigned long long)(counter.QuadPart % frequency.QuadPart) * 1000000ull / (unsigned long long)frequency.QuadPart;
}

/* ---------- what the bots cost */

int bot_profiling;

enum
{
	MAXIMUM_PROFILE_SECTIONS = 32,
};

static struct
{
	char const *name;
	unsigned long long total;
	unsigned long long worst;
	unsigned long calls;
	/* (in the bot's think going on) */
	unsigned long long call_total;
} bot_profile_sections[MAXIMUM_PROFILE_SECTIONS];

void bot_profile_add(char const *section, unsigned long long microseconds)
{
	long index;

	for (index = 0; index < MAXIMUM_PROFILE_SECTIONS; index++)
	{
		if (!bot_profile_sections[index].name)
			bot_profile_sections[index].name = section;
		if (bot_profile_sections[index].name == section)
		{
			bot_profile_sections[index].total += microseconds;
			bot_profile_sections[index].call_total += microseconds;
			bot_profile_sections[index].calls++;
			if (microseconds > bot_profile_sections[index].worst)
				bot_profile_sections[index].worst = microseconds;
			return;
		}
	}
}

void bot_profile_mark(char const *section)
{
	static char const *marked;
	static unsigned long long marked_time;
	unsigned long long now = bot_time_microseconds();

	if (marked)
		bot_profile_add(marked, now - marked_time);
	marked = section;
	marked_time = now;
}

/* one bot's think is over: when it took long, where its time went */
void bot_profile_call_end(long bot, long tick, unsigned long long microseconds, unsigned long long slow)
{
	long index;

	if (microseconds >= slow)
	{
		char text[512];
		long length = 0;

		text[0] = 0;
		for (index = 0; index < MAXIMUM_PROFILE_SECTIONS && bot_profile_sections[index].name; index++)
		{
			if (bot_profile_sections[index].call_total * 10 >= microseconds && length < (long)sizeof(text) - 64)
				length += snprintf(text + length, sizeof(text) - length, " %s %.2f", bot_profile_sections[index].name,
					(double)bot_profile_sections[index].call_total / 1000.0);
		}
		bot_log("bot slow: bot%ld t%ld %.2f ms:%s", bot, tick, (double)microseconds / 1000.0, text);
	}
	for (index = 0; index < MAXIMUM_PROFILE_SECTIONS && bot_profile_sections[index].name; index++)
		bot_profile_sections[index].call_total = 0;
}

void bot_profile_report(long ticks)
{
	long index;

	for (index = 0; index < MAXIMUM_PROFILE_SECTIONS && bot_profile_sections[index].name; index++)
	{
		if (bot_profile_sections[index].calls)
		{
			bot_log("bot prof: %-18s %8.3f ms a tick, %7.1f calls a tick, %8.1f us a call, worst %8.2f ms",
				bot_profile_sections[index].name, (double)bot_profile_sections[index].total / 1000.0 / MAX(ticks, 1),
				(double)bot_profile_sections[index].calls / MAX(ticks, 1),
				(double)bot_profile_sections[index].total / bot_profile_sections[index].calls,
				(double)bot_profile_sections[index].worst / 1000.0);
		}
		bot_profile_sections[index].total = 0;
		bot_profile_sections[index].worst = 0;
		bot_profile_sections[index].calls = 0;
	}
}
