/*
CONTROL_HARNESS.C

The dedicated server's console, control API and web admin page
(server/platform/server_control.c) as a program of their own, with a
stand-in for the game's main thread, for tools/test_server_control.py to
drive over real sockets: logins, sessions, CSRF, headers, files, limits,
the console's sv_admin_* commands, and junk requests (built with
AddressSanitizer and UndefinedBehaviorSanitizer where the compiler has
them).

It takes server_control.c's settings (HALO_DEDICATED_CONTROL,
HALO_DEDICATED_CONSOLE) and HARNESS_DATA (the data folder). On its standard
output: the token printed once, the console's output, and a line for each
thing the main thread is handed:
  notice: <the control's own log line>
  audit: <source>: <command>      (a command that is logged)
  quiet: <command>                (a read, not logged)
The stand-in answers sv_status, sv_players, sv_banlist, sv_mapcycle and
sv_maps with fixed JSON, and every other command with "ran <command>". It
runs until it is killed.
*/

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum
{
	CONTROL_JSON = 1,
	CONTROL_NOTICE = 2,
	CONTROL_QUIET = 4,
};

void server_control_start(void);
int server_control_next(char *line, int line_size, char *source, int source_size, int *flags);
void server_control_finish(int ticket, int ok, const char *output);
void server_control_log(const char *text);

/* ---------- the platform layer's, stood in for */

const char *platform_data_root(void)
{
	const char *root = getenv("HARNESS_DATA");

	return root ? root : ".";
}

/* (as log_address.c writes one: loopback and private plainly, a public
one as a tag) */
const char *log_address(const unsigned char *bytes, int length, int port, char *text, int size)
{
	(void)port;
	if (length == 4 && (bytes[0] == 127 || bytes[0] == 10 || (bytes[0] == 192 && bytes[1] == 168)))
		snprintf(text, (size_t)size, "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
	else
		snprintf(text, (size_t)size, "addr#%02x%02x", bytes[0], bytes[length - 1]);
	return text;
}

/* ---------- the main thread, stood in for */

static const char *answer(const char *line, int json)
{
	static char output[512];

	if (json && !strcmp(line, "sv_status"))
	{
		return "{\"name\": \"Harness\", \"version\": \"0.0.0\", \"network_version\": 11, \"state\": \"in_game\", "
			"\"map\": \"bloodgulch\", \"game_type\": \"slayer\", \"chosen\": false, \"next_map\": null, "
			"\"next_game_type\": null, \"playlist\": \"playlists/test.txt\", \"entry\": 1, \"entries\": 2, "
			"\"players\": 1, \"maximum_players\": 16, \"minimum_players\": 1, \"public\": false, "
			"\"idle_limit_minutes\": 0, \"uptime_seconds\": 90}";
	}
	if (json && !strcmp(line, "sv_players"))
	{
		return "{\"players\": [{\"number\": 1, \"name\": \"<b>x</b>\", \"machine\": 0, \"team\": null, \"score\": 3, "
			"\"ping\": 20, \"id\": null}], \"count\": 1, \"maximum_players\": 16}";
	}
	if (json && !strcmp(line, "sv_banlist"))
		return "{\"bans\": [], \"count\": 0}";
	if (json && !strcmp(line, "sv_mapcycle"))
	{
		return "{\"playlist\": \"playlists/test.txt\", \"entry\": 1, \"state\": \"in_game\", \"entries\": [{\"number\": "
			"1, \"map\": \"bloodgulch\", \"game_type\": \"slayer\"}, {\"number\": 2, \"map\": \"prisoner\", "
			"\"game_type\": \"team_slayer\"}], \"chosen\": null, \"next\": null}";
	}
	if (json && !strcmp(line, "sv_maps"))
		return "{\"maps\": [\"bloodgulch\", \"prisoner\"], \"game_types\": [\"slayer\", \"ctf\"]}";
	snprintf(output, sizeof(output), "ran %s\n", line);
	return output;
}

int main(void)
{
	struct timespec pause = { 0, 5 * 1000 * 1000 };
	unsigned long frame = 0;

	signal(SIGPIPE, SIG_IGN);
	setvbuf(stdout, NULL, _IOLBF, 0);
	server_control_start();
	printf("harness: started\n");
	for (;;)
	{
		char line[256];
		char source[80];
		int flags = 0;
		int ticket;

		while ((ticket = server_control_next(line, sizeof(line), source, sizeof(source), &flags)) != 0)
		{
			if (flags & CONTROL_NOTICE)
			{
				printf("notice: %s\n", line);
				server_control_log(line);
				continue;
			}
			if (flags & CONTROL_QUIET)
				printf("quiet: %s\n", line);
			else
				printf("audit: %s: %s\n", source, line);
			server_control_finish(ticket, 1, answer(line, (flags & CONTROL_JSON) != 0));
		}
		/* (a log line now and then, one with a public address) */
		if (!(++frame % 200))
			server_control_log(frame % 400 ? "a log line\n" : "joined from 8.8.8.8:2302\n");
		nanosleep(&pause, NULL);
	}
	return 0;
}
