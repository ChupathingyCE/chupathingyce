/*
LOG_SEND.H

Send my logs: the end of debug.txt, the newest crash report and, when the
player says so, config.toml, with their addresses and home folder taken
out (crash_report_redact), sent to the bug site (puma's dumps.py: its
page's form, POST /upload) on a thread of its own. debug.dump_url names
another site (a local one for tests). The setup screen's LOGS & HELP
(launcher.c) and the Delta Guide's SETTINGS (guide.c) both send through it,
each only after the player said SEND.
*/

#ifndef __LOG_SEND_H
#define __LOG_SEND_H

/* the site, as the rows name it */
#define LOG_SEND_SITE "dump.chupathingy.org"

enum
{
	LOG_SEND_IDLE,
	LOG_SEND_SENDING,
	LOG_SEND_SENT,
	LOG_SEND_FAILED,
	/* (log_send_start's: no debug.txt in the folder yet) */
	LOG_SEND_NOTHING,
};

/* the logs in logs_folder (and the newest of its crashes/) sent, with
config.toml when with_settings: LOG_SEND_SENDING once begun, else
LOG_SEND_NOTHING or LOG_SEND_FAILED with why for the player in error. One
send at a time: while one is on its way, another is not begun (FAILED) */
int log_send_start(const char *logs_folder, int with_settings, char *error, int error_size);
/* where it is: SENDING, then SENT with the site's code for the report
(capitals) or FAILED with why for the player; IDLE before any. The answer
stays until log_send_forget or the next start */
int log_send_poll(char *code, int code_size, char *error, int error_size);
/* the answer read: IDLE again (a send on its way goes on, unread) */
void log_send_forget(void);

/* a folder as a file:// URL for the system to open (OPEN LOGS FOLDER, on
the setup screen and in the Guide) */
void log_send_folder_url(const char *path, char *url, int size);

#endif
