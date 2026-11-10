/*
SERVER_REPORTS.H

Players' reports of other players (docs/delta.md, Reports;
server/docs/moderation.md): the moderators' queue. A ChupathingyCE player
reports another from DELTA's PLAYERS HERE or the scoreboard (Delta Peer's
REPORT); server_reports.c (the game's main thread) checks it and makes the
report, and the queue (server/platform/report_queue.c, under a lock of its
own: the control thread lists it, the link's thread sends it to the site)
keeps it, in memory and as a line of reports.log in the data folder. Never
an address. Plain types only.
*/

#ifndef SERVER_REPORTS_H
#define SERVER_REPORTS_H

enum
{
	SERVER_REPORT_NAME_SIZE = 48,
	SERVER_REPORT_HANDLE_SIZE = 32,
	SERVER_REPORT_ID_SIZE = 33,
	SERVER_REPORT_MAP_SIZE = 64,
	SERVER_REPORT_GAME_TYPE_SIZE = 32,
	SERVER_REPORT_CLOSED_BY_SIZE = 64,
	/* the reports the queue keeps, at most (the oldest closed go first; an
	open one never goes: a new report waits for room instead) */
	SERVER_REPORTS_KEPT = 200,
	/* the refusal counts kept with a report (host_evidence.h's: movement,
	vehicle, hits, clock) */
	SERVER_REPORT_EVIDENCE = 4,
};

/* a report's reasons (Delta Peer's REPORT's) */
enum
{
	SERVER_REPORT_CHEATING = 1,
	SERVER_REPORT_ABUSE,
	SERVER_REPORT_GRIEFING,
	SERVER_REPORT_NAME,
	SERVER_REPORT_OTHER,
};

struct server_report_player
{
	/* their name in the game */
	char name[SERVER_REPORT_NAME_SIZE];
	/* their verified handle and account (32 hex digits), empty if they
	proved none */
	char handle[SERVER_REPORT_HANDLE_SIZE];
	char account[SERVER_REPORT_ID_SIZE];
	/* a ChupathingyCE game (Delta Peer), or not (OpenCE's) */
	int delta;
};

struct server_report
{
	/* the queue's number (sv_reports), and the ref the site knows it by */
	unsigned int id;
	char ref[SERVER_REPORT_ID_SIZE];
	long long time;
	int reason;
	struct server_report_player target;
	struct server_report_player reporter;
	/* the target's number in sv_players when reported (0: gone) */
	long target_number;
	char map[SERVER_REPORT_MAP_SIZE];
	char game_type[SERVER_REPORT_GAME_TYPE_SIZE];
	/* the clip's token (32 hex digits), empty without one: made by
	server_reports_add when want_clip, the server's own, never the
	reporter's (the site takes a clip only for a token a report names) */
	int want_clip;
	char clip[SERVER_REPORT_ID_SIZE];
	int evidence[SERVER_REPORT_EVIDENCE];
	/* closed (sv_report_close), and by whom */
	int closed;
	char closed_by[SERVER_REPORT_CLOSED_BY_SIZE];
};

/* ---------- the limits on reports (the main thread's)

Each counted by who reports and who is reported: a verified player's
account (their proof bound to this server: docs/delta.md, "Verified
names"), else the player themselves this game (their datum index, which a
new player in a slot someone left, or the same player back after leaving,
never has). Never by a machine's slot. A reporter: 3 a minute, 10 a game,
each player once a game. A reported player: 5 a game from everyone. They
start over with each game. */
enum
{
	SERVER_REPORTS_PER_MINUTE = 3,
	SERVER_REPORTS_PER_GAME = 10,
	SERVER_REPORTS_PER_TARGET = 5,
	/* the players one reporter reported this game, remembered */
	SERVER_REPORTS_REPORTED_KEPT = 16,
	/* the reporters and reported players a game remembers (past them, a new
	one's report is refused: a count is never forgotten) */
	SERVER_REPORTS_REPORTERS_KEPT = 256,
	SERVER_REPORTS_TARGETS_KEPT = 256,
};

enum
{
	SERVER_REPORT_LIMIT_OK,
	/* too many from this reporter (a minute's, a game's), or no room */
	SERVER_REPORT_LIMIT_TOO_SOON,
	/* this reporter reported them this game, or they have the most */
	SERVER_REPORT_LIMIT_ALREADY,
};

struct server_report_who
{
	int has_account;
	unsigned char account[16];
	long player;
};

/* whether a report from reporter of target may be taken now
(SERVER_REPORT_LIMIT_*); then, once taken, counted */
int server_reports_limit_check(const struct server_report_who *reporter, const struct server_report_who *target,
	long long now);
void server_reports_limit_count(const struct server_report_who *reporter, const struct server_report_who *target,
	long long now);
/* a new game: the counts start over */
void server_reports_limit_reset(void);

/* (the main thread's, each frame: server_moderation_update) the report
host given to Delta Peer once; each machine's counts start over with each
game */
void server_reports_update(void);

/* a reason's plain name ("cheating"; "?" for none) */
const char *server_report_reason_name(int reason);

/* (the main thread's) a report into the queue: its number and ref given
here (a random ref; and a random clip token with want_clip), the line
written to reports.log. Its number, 0 if none (no room: every report kept
is open; one is never let go for a new one) */
unsigned int server_reports_add(struct server_report *report);
/* (any thread's) sv_reports: the open reports, newest first (all with
all), as text or JSON */
void server_reports_text(int json, int all, char *out, int size);
/* sv_report_close: 1, else 0 (no such open report) */
int server_reports_close(unsigned int id, const char *by);
/* how many are open */
int server_reports_open(void);

/* (the link's thread) the poll's "reports" member (with its key) for the
reports the site has not taken yet (Delta players' alone, at most 20):
its length, 0 none, -1 if it does not fit */
int server_reports_site_payload(char *out, int size);
/* ... the refs the site's answer says it took */
void server_reports_site_taken(const char refs[][SERVER_REPORT_ID_SIZE], int count);

/* a report as one JSON object (the queue's lines and the site's): its
length, or -1 if it does not fit; with site, the site's form */
int server_report_json(const struct server_report *report, int site, char *out, int size);

#endif
