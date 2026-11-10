/*
REPORT_PLAYER.C

Reporting a player to a dedicated server's moderators (docs/delta.md,
Reports). DELTA's PLAYERS HERE offers REPORT on another player when the
host takes reports (Delta Peer's reports capability: only a dedicated
server does). The player picks a reason from a short list, then whether to
attach a clip: none, the last one saved, or one of clips/ (capture's). The
report goes to the host (REPORT); when the host is linked to the site and
asks for the clip (REPORT_RESULT, with the token the host made), the
browser's thread sends the file to the site with that token
(browser_clip_upload). Never an address,
and nothing is sent anywhere else.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "report_player.h"

#if defined(HALO_GAME_BROWSER) && defined(HALO_GUIDE)

#include "../src/browser.h"
#include "../src/delta_peer.h"

#include <stdio.h>
#include <string.h>


/* ---------- constants */

enum
{
	/* the host's answer waited for this long */
	ANSWER_MILLISECONDS = 5000,
	/* an outcome's line shown this long */
	MESSAGE_MILLISECONDS = 6000,
	CLIPS_SHOWN = 6,
};

/* ---------- globals */

static char const *const reasons[] =
{
	"CHEATING", "ABUSE IN CHAT OR VOICE", "GRIEFING OR BETRAYING", "OFFENSIVE NAME", "SOMETHING ELSE"
};

static struct
{
	short step;
	long player;
	long machine;
	char name[48];
	int reason;
	char title[64];
	/* the clips offered (newest first) */
	char clip_paths[CLIPS_SHOWN][512];
	char clip_names[CLIPS_SHOWN][48];
	short clip_count;
	/* the report sent: its sequence, its clip, when */
	unsigned int sequence;
	unsigned int results_before;
	boolean clip;
	char clip_path[512];
	unsigned long sent_time;
	/* the clip's upload being watched */
	boolean uploading;
	char message[64];
	unsigned long message_time;
} report;

/* ---------- private code */

static void say(char const *text)
{
	snprintf(report.message, sizeof(report.message), "%s", text);
	report.message_time = system_milliseconds();
}

static void send_report(short clip_choice)
{
	struct delta_wire_report_result last;

	report.clip = FALSE;
	report.clip_path[0] = 0;
	/* (0: no clip; then the clips offered, the last first) */
	if (clip_choice > 0 && clip_choice <= report.clip_count)
	{
		snprintf(report.clip_path, sizeof(report.clip_path), "%s", report.clip_paths[clip_choice - 1]);
		report.clip = TRUE;
	}
	report.results_before = delta_peer_game_report_result(&last);
	report.sequence = delta_peer_game_report((int)report.machine, (int)report.player, report.reason, report.clip);
	report.step = _report_step_none;
	if (!report.sequence)
	{
		say("Couldn't send the report");
		return;
	}
	report.sent_time = system_milliseconds();
	say("Sending");
}

/* ---------- public code */

boolean report_player_available(
	long machine_index)
{
	return delta_peer_game_report_ready() && machine_index >= 0 && machine_index != DELTA_WIRE_NO_MACHINE;
}

void report_player_begin(
	long player_list_index,
	long machine_index,
	char const *name)
{
	report.player = player_list_index;
	report.machine = machine_index;
	snprintf(report.name, sizeof(report.name), "%s", name ? name : "");
	snprintf(report.title, sizeof(report.title), "REPORT %s", report.name);
	report.reason = 0;
	report.step = _report_step_reason;
}

short report_player_step(
	void)
{
	return report.step;
}

char const *report_player_title(
	void)
{
	return report.title;
}

short report_player_choices(
	char choices[][REPORT_PLAYER_CHOICE_SIZE],
	short most)
{
	short count = 0, index;

	if (report.step == _report_step_reason)
	{
		for (index = 0; index < (short)NUMBEROF(reasons) && count < most; index++)
			snprintf(choices[count++], REPORT_PLAYER_CHOICE_SIZE, "%s", reasons[index]);
	}
	else if (report.step == _report_step_clip)
	{
		if (count < most)
			snprintf(choices[count++], REPORT_PLAYER_CHOICE_SIZE, "NO CLIP");
		for (index = 0; index < report.clip_count && count < most; index++)
		{
			if (!index)
				snprintf(choices[count++], REPORT_PLAYER_CHOICE_SIZE, "LAST CLIP");
			else
				snprintf(choices[count++], REPORT_PLAYER_CHOICE_SIZE, "%.40s", report.clip_names[index]);
		}
	}
	return count;
}

void report_player_choose(
	short index)
{
	if (report.step == _report_step_reason)
	{
		if (index < 0 || index >= (short)NUMBEROF(reasons))
			return;
		report.reason = index + 1;
		report.clip_count = (short)browser_clips_list(report.clip_paths, report.clip_names, CLIPS_SHOWN);
		report.step = _report_step_clip;
	}
	else if (report.step == _report_step_clip)
		send_report(index);
}

void report_player_back(
	void)
{
	report.step = report.step == _report_step_clip ? _report_step_reason : _report_step_none;
}

char const *report_player_message(
	void)
{
	if (!report.message[0] || system_milliseconds() - report.message_time > MESSAGE_MILLISECONDS)
		return "";
	return report.message;
}

void report_player_update(
	void)
{
	struct delta_wire_report_result result;

	if (report.sequence)
	{
		unsigned int results = delta_peer_game_report_result(&result);

		if (results != report.results_before && result.sequence == report.sequence)
		{
			report.sequence = 0;
			switch (result.result)
			{
			case _delta_report_taken:
				/* (with the token the host made: the site takes the clip
				only for the report that names it) */
				if (report.clip && (result.flags & DELTA_WIRE_REPORT_SEND_CLIP) &&
					browser_clip_upload(report.clip_path, result.clip_token))
				{
					report.uploading = TRUE;
					say("Report sent. Sending the clip");
				}
				else
					say(report.clip ? "Report sent. This server takes no clips" : "Report sent");
				break;
			case _delta_report_too_soon: say("Too many reports. Try again later"); break;
			case _delta_report_no_player: say("That player has left"); break;
			case _delta_report_already: say("You reported them already"); break;
			default: say("This server takes no reports"); break;
			}
		}
		else if (system_milliseconds() - report.sent_time > ANSWER_MILLISECONDS)
		{
			report.sequence = 0;
			say("The server didn't answer");
		}
	}
	if (report.uploading)
	{
		switch (browser_clip_state())
		{
		case BROWSER_CLIP_SENDING: break;
		case BROWSER_CLIP_SENT: report.uploading = FALSE; say("Report and clip sent"); break;
		case BROWSER_CLIP_TOO_BIG: report.uploading = FALSE; say("Report sent. The clip is too big"); break;
		case BROWSER_CLIP_TOO_MANY: report.uploading = FALSE; say("Report sent. Too many clips today"); break;
		default: report.uploading = FALSE; say("Report sent. The clip didn't go"); break;
		}
	}
}

#else

boolean report_player_available(long machine_index) { (void)machine_index; return FALSE; }
void report_player_begin(long player_list_index, long machine_index, char const *name)
{
	(void)player_list_index;
	(void)machine_index;
	(void)name;
}
short report_player_step(void) { return _report_step_none; }
char const *report_player_title(void) { return ""; }
short report_player_choices(char choices[][REPORT_PLAYER_CHOICE_SIZE], short most)
{
	(void)choices;
	(void)most;
	return 0;
}
void report_player_choose(short index) { (void)index; }
void report_player_back(void) {}
char const *report_player_message(void) { return ""; }
void report_player_update(void) {}

#endif
