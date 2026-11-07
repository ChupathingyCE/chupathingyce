/*
CONSOLE_REPORTS.C

A console's reports of the games it joins (console_reports.h): the pure
parts first (the bodies), then the console's (the report handed over at
the game's end, and its thread).
*/

#include "console_reports.h"

#include "port/console/link/console_link.h"
#include "port/linux/src/game_report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the settings' (port/linux/src/port_config.c) and internet play's
(port/linux/src/p2p.c) */
int config_boolean(const char *name);
int p2p_joined_invite(char *text, int size);

static void wipe(void *data, unsigned long size)
{
	volatile unsigned char *bytes = (volatile unsigned char *)data;

	while (size--)
		*bytes++ = 0;
}

/* ---------- the pure parts */

int console_reports_key_body(const char *report, const char *key_hex, const char *platform, char *body,
	unsigned long size)
{
	unsigned long length;

	if (!report || report[0] != '{' || strlen(key_hex) != 2 * CONSOLE_REPORTS_KEY_SIZE)
		return 0;
	length = (unsigned long)snprintf(body, size, "{\"key\": \"%s\", \"platform\": \"%s\", %s", key_hex, platform,
		report + 1);
	return length > 0 && length < size ? (int)length : 0;
}

int console_reports_signed_message(const char *report, const char *platform, const char *public_hex,
	const char *challenge, char *buffer, unsigned long size)
{
	unsigned long report_length;
	int length;

	if (!report || report[0] != '{')
		return 0;
	report_length = (unsigned long)strlen(report);
	if (report_length < 2 || report[report_length - 1] != '}')
		return 0;
	/* (the label, then the report without its closing brace) */
	length = snprintf(buffer, size, "%s%.*s, \"platform\": \"%s\", \"console_key\": \"%s\", \"challenge\": \"%s\"",
		CONSOLE_REPORTS_SIGNATURE_LABEL, (int)(report_length - 1), report, platform, public_hex, challenge);
	return length > 0 && (unsigned long)length < size ? length : 0;
}

int console_reports_finish_signed(char *buffer, unsigned long size, int length, const char *signature_hex)
{
	int added;

	if (length <= 0 || (unsigned long)length >= size)
		return 0;
	added = snprintf(buffer + length, size - (unsigned long)length, ", \"signature\": \"%s\"}", signature_hex);
	if (added <= 0 || (unsigned long)(length + added) >= size)
		return 0;
	return length + added - (int)(sizeof(CONSOLE_REPORTS_SIGNATURE_LABEL) - 1);
}

/* ---------- on the console */

static const struct console_reports_port *port;
/* a report being sent (its thread frees it), so one at a time: the main
thread sets it only while it is clear, the thread clears it last */
static volatile int sending;
static int told_no_identity;
static int told_no_invite;

static void log_line(const char *text)
{
	if (port && port->write_log)
		port->write_log(text);
}

static void bytes_hex(const unsigned char *data, int size, char *text)
{
	static const char digits[] = "0123456789abcdef";
	int index;

	for (index = 0; index < size; index++)
	{
		text[2 * index] = digits[data[index] >> 4];
		text[2 * index + 1] = digits[data[index] & 15];
	}
	text[2 * size] = 0;
}

/* this console's identity, as the report proves it: 1 the identity file's
key, 2 the console's link, 0 none */
static int identity(void)
{
	unsigned char key[CONSOLE_REPORTS_KEY_SIZE], id[CONSOLE_LINK_ID_SIZE];

	if (port->player_key && port->player_key(key))
	{
		wipe(key, sizeof(key));
		return 1;
	}
	return console_link_player_id(id) ? 2 : 0;
}

/* one try: 1 if the site answered (status), 0 if not */
static int send_once(const char *report, int *status, char *reply)
{
	unsigned long size = (unsigned long)strlen(report) + CONSOLE_REPORTS_BODY_EXTRA;
	unsigned char key[CONSOLE_REPORTS_KEY_SIZE];
	char *body = malloc(size);
	int answered = 0;

	*status = 0;
	if (!body)
	{
		log_line("Game list: no memory for the joined game's report");
		return 0;
	}
	if (port->player_key && port->player_key(key))
	{
		/* (the identity file's key, over HTTPS alone, as the desktop's) */
		char key_hex[2 * CONSOLE_REPORTS_KEY_SIZE + 1];

		bytes_hex(key, CONSOLE_REPORTS_KEY_SIZE, key_hex);
		wipe(key, sizeof(key));
		if (console_reports_key_body(report, key_hex, port->platform, body, size))
			answered = port->request(CONSOLE_REPORTS_PATH, body, status, reply, CONSOLE_REPORTS_REPLY_SIZE);
		wipe(key_hex, sizeof(key_hex));
	}
	else
	{
		/* (the console's link: a one-use challenge, and the report signed
		over it with the device key) */
		char challenge[33], public_hex[65], signature[129];
		int length = 0, message;

		answered = port->request(CONSOLE_REPORTS_CHALLENGE_PATH, NULL, status, reply, CONSOLE_REPORTS_REPLY_SIZE);
		if (answered && *status == 200 && console_link_parse_challenge(reply, challenge))
		{
			message = console_link_linked_key(public_hex)
				? console_reports_signed_message(report, port->platform, public_hex, challenge, body, size)
				: 0;
			if (message && console_link_sign_linked(body, message, signature))
				length = console_reports_finish_signed(body, size, message, signature);
			wipe(signature, sizeof(signature));
			if (length)
				answered = port->request(CONSOLE_REPORTS_PATH, body + sizeof(CONSOLE_REPORTS_SIGNATURE_LABEL) - 1,
					status, reply, CONSOLE_REPORTS_REPLY_SIZE);
			else
			{
				/* (unlinked meanwhile: nothing to send, nor to try again) */
				log_line("Game list: the joined game's report could not be signed (is the console still linked?)");
				*status = 403;
				reply[0] = 0;
			}
		}
	}
	wipe(body, size);
	free(body);
	return answered;
}

static void report_thread(void *context)
{
	char *report = context;
	char reply[CONSOLE_REPORTS_REPLY_SIZE];
	int attempt, status = 0, answered = 0, retry = 1;

	for (attempt = 1; attempt <= CONSOLE_REPORTS_ATTEMPTS && retry; attempt++)
	{
		if (attempt > 1 && port->sleep)
			port->sleep(CONSOLE_REPORTS_RETRY_WAIT);
		reply[0] = 0;
		answered = send_once(report, &status, reply);
		/* (no answer, or the site busy: again in a while) */
		retry = !answered || status == 429 || status >= 500;
	}
	reply[strcspn(reply, "\r\n")] = 0;
	{
		char line[160];

		if (answered && status == 200 && !strncmp(reply, "ok ", 3))
			snprintf(line, sizeof(line), "Game list: the joined game's report was taken (%.64s)", reply);
		else if (answered && status == 200)
			snprintf(line, sizeof(line), "Game list: the game list did not answer the joined game's report as expected");
		else if (answered)
			snprintf(line, sizeof(line), "Game list: the joined game's report was not taken (%d %.64s)", status, reply);
		else
			snprintf(line, sizeof(line), "Game list: could not send the joined game's report");
		log_line(line);
	}
	free(report);
	sending = 0;
}

void console_reports_initialize(const struct console_reports_port *the_port)
{
	port = the_port;
}

const char *console_reports_build(void)
{
	return port && port->build ? port->build : "";
}

int console_reports_joined_game(int teams, int red_score, int blue_score, int duration_seconds,
	const struct browser_report_player *players, int count, const char *extra)
{
	char invite[BROWSER_INVITE_LENGTH + 1], prefix[BROWSER_INVITE_LENGTH + 24];
	char *report;
	int kind;

	/* (the player's choice first: off sends nothing, nor asks anything) */
	if (!config_boolean("network.report_joined_games"))
		return 0;
	/* (only a game joined from Online Games has an invite the site knows it
	by: a system link game is not reported. Said once, so that a player who
	expects a report sees why there is none in debug.txt) */
	if (!p2p_joined_invite(invite, sizeof(invite)))
	{
		if (!told_no_invite)
			log_line("Game list: the joined game is not reported: it has no Online Games invite (only games joined from Online Games are reported)");
		told_no_invite = 1;
		return 0;
	}
	if (!port)
		console_reports_port_start();
	if (!port)
		return 0;
	kind = identity();
	if (!kind)
	{
		if (!told_no_identity)
			log_line("Game list: the joined game is not reported: link the console to a Delta profile (Online "
				"Games), or put an identity file beside config.toml");
		told_no_identity = 1;
		return 0;
	}
	if (sending)
	{
		log_line("Game list: the last game's report is still being sent; this game is not reported");
		return 0;
	}
	snprintf(prefix, sizeof(prefix), "\"invite\": \"%s\", ", invite);
	if (port->memory_ok &&
		!port->memory_ok(2 * game_report_size(prefix, players, count, extra) + CONSOLE_REPORTS_BODY_EXTRA))
	{
		log_line("Game list: too little memory free to send the joined game's report");
		return 0;
	}
	report = game_report_json(prefix, teams, red_score, blue_score, duration_seconds, players, count, extra, NULL,
		NULL);
	if (!report)
		return 0;
	sending = 1;
	if (!port->start_thread(report_thread, report))
	{
		log_line("Game list: no thread for the joined game's report");
		sending = 0;
		free(report);
		return 0;
	}
	return 1;
}
