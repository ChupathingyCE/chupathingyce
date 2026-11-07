/*
CONSOLE_REPORTS.H

A console's reports of the games it joins (HALO_CONSOLE_GAME_REPORTS), as
the desktop builds send theirs (port/linux/src/browser.c's
browser_client_report): when an online game it joined through an invite
ends, the statistics recorder (port/linux/game/game_stats.c) hands over the
report, and this sends it to the site's POST /v1/client_report over the
console's HTTPS (port/console/https: the site's key pinned), so that the
player's games are on their service record.

The same report and the same rules as the desktop's:
- the same JSON (port/linux/src/game_report.c), the reporter's own line in
  full and everyone else's as the scoreboard's numbers; no address of
  anyone, and the site keeps the game only with 2 players or more at its
  end and 60 seconds or more of play (list_server.py's clean_client_report);
- network.report_joined_games = false (config.toml) sends nothing, as on
  the desktop;
- only a game joined through an invite (p2p_joined_invite) is reported;
- tried again a few times when the site cannot be reached or is busy.

Who the player is, as Delta Peer's PROFILE says it (console_profile_id):
- an identity file (the port's player_key, D:\game_list_player.key on the
  original Xbox): its player key goes with the report, as a desktop copy's
  does, and only over HTTPS;
- else the console's own link (port/console/link): the report is signed
  with the console's device key, over a one-use challenge of the site's
  (GET /v1/claim_challenge), and carries the public key, never the seed.
  The signed message is "chupathingy console link v1\nreport\n" and the
  body's bytes up to its last member, ", \"signature\": \"...\"}";
- else nothing is sent: a console that never linked sends no report.

Memory: the report is allocated when the game ends (a few kilobytes: one
full line and the others' scoreboard lines) and freed once it is sent; the
request runs on a short-lived thread of the port's (start_thread), whose
stack holds the TLS session (CONSOLE_HTTPS_STACK_SIZE). The port may refuse
it when memory is short (memory_ok). One report is sent at a time; a game
that ends while one is still being sent is not reported.

Plain C over the port's hooks; port/console/tests/console_reports_test.c
builds it on the host.
*/

#ifndef __CONSOLE_REPORTS_H
#define __CONSOLE_REPORTS_H

#include "port/linux/src/browser.h"

#define CONSOLE_REPORTS_PATH "/v1/client_report"
#define CONSOLE_REPORTS_CHALLENGE_PATH "/v1/claim_challenge"
/* the label of a linked console's signed report (the site's console_link.py
SIGNATURE_LABEL and "report\n") */
#define CONSOLE_REPORTS_SIGNATURE_LABEL "chupathingy console link v1\nreport\n"
/* the player key and its hex digits */
#define CONSOLE_REPORTS_KEY_SIZE 32
/* tries, and the wait between them (browser.c's CLIENT_REPORT_ATTEMPTS and
CLIENT_REPORT_INTERVAL) */
#define CONSOLE_REPORTS_ATTEMPTS 4
#define CONSOLE_REPORTS_RETRY_WAIT 8000
/* the site's answer, headers and all */
#define CONSOLE_REPORTS_REPLY_SIZE 1024
/* what a body adds to the report: the key or the signature's members */
#define CONSOLE_REPORTS_BODY_EXTRA 512

struct console_reports_port
{
	/* the Delta platform name ("xbox") and the build's name ("Warthog
	0.8.1d"), as the report says them */
	const char *platform;
	const char *build;
	/* one HTTPS request to the site (stats.chupathingy.org, its key
	pinned): path, a JSON body (NULL for a GET); the answer's status and
	body (NUL terminated, cut to reply_size - 1). 1 if an answer came */
	int (*request)(const char *path, const char *body, int *status, char *reply, int reply_size);
	/* runs function(context) on a thread of its own with room for a request
	(CONSOLE_HTTPS_STACK_SIZE and a few KB), which ends when it returns: 1,
	or 0 if it could not */
	int (*start_thread)(void (*function)(void *context), void *context);
	void (*sleep)(unsigned long milliseconds);
	/* whether a report of this many bytes, and the thread, fit in the
	memory free now; NULL for always */
	int (*memory_ok)(unsigned long bytes);
	/* the identity file's player key: 1 and its 32 bytes (the caller wipes
	them), or 0 for none; NULL for no identity file on this console */
	int (*player_key)(unsigned char *key);
	/* one line for the log (never a key, a signature, a body or an address) */
	void (*write_log)(const char *text);
};

/* the port's hooks (kept, not copied) */
void console_reports_initialize(const struct console_reports_port *port);
/* the port's: calls console_reports_initialize, when the first report
comes (a console that sets HALO_CONSOLE_GAME_REPORTS gives it) */
void console_reports_port_start(void);

/* game_stats.c's, as browser_client_report (browser.h): the report of a
game this console joined, which ended. 1 if it is on its way, 0 if not
(no hooks, reports of joined games off, a game not joined through an
invite, no identity, one still being sent, or no memory for it) */
int console_reports_joined_game(int teams, int red_score, int blue_score, int duration_seconds,
	const struct browser_report_player *players, int count, const char *extra);

/* the build's name for the report's "build" (the port's) */
const char *console_reports_build(void);

/* ---------- the pure parts (console_reports_test.c) */

/* the body of a report sent with a player key: {"key", "platform", then
the report's members}, into body (size bytes): its length, or 0 if it
does not fit */
int console_reports_key_body(const char *report, const char *key_hex, const char *platform, char *body,
	unsigned long size);

/* the body of a linked console's report up to its signature (the report's
members, then "platform", "console_key" and "challenge", without the
closing brace), after CONSOLE_REPORTS_SIGNATURE_LABEL in buffer (size
bytes), so that the signed message is the buffer's start: the message's
length (label and body), or 0 if it does not fit */
int console_reports_signed_message(const char *report, const char *platform, const char *public_hex,
	const char *challenge, char *buffer, unsigned long size);

/* the signature's member and the closing brace after the message at
buffer (its length, length): the body's length from the end of the label,
or 0 if it does not fit */
int console_reports_finish_signed(char *buffer, unsigned long size, int length, const char *signature_hex);

#endif
