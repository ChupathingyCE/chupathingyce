/*
SERVER_CONTROL.H

The dedicated server's console and control API (server_control.c) as the
units that use it see them: the game's main thread (server/src/
server_commands.c, built with the Xbox's ABI) and the tests' stand-in for
it (server/tests/control_harness.c). server_control.c is built with the
host's ABI, so only plain int and char cross here, which every ABI the
program is built with lays out alike (no long, no wchar_t, no host
headers), in C89.
*/

#ifndef SERVER_CONTROL_H
#define SERVER_CONTROL_H

/* a command's flags (server_control_next) */
enum
{
	/* its output as JSON */
	CONTROL_JSON = 1,
	/* a notice of the control's own to log, not a command */
	CONTROL_NOTICE = 2,
	/* a command not logged (the API's reads, which a web page asks for
	every few seconds) */
	CONTROL_QUIET = 4
};

/* the control panel's address when HALO_DEDICATED_CONTROL is not set: on,
for this machine only */
#define SERVER_CONTROL_DEFAULT "127.0.0.1:8080"
/* HALO_DEDICATED_CONTROL, else SERVER_CONTROL_DEFAULT */
const char *server_control_setting(void);
/* the console's and the API's thread started */
void server_control_start(void);
/* the server was started without a playlist, for the control panel's
first-run setup (before server_control_start): with no owner yet, it waits
to host until the owner finishes the setup */
void server_control_setup_mode(void);
/* whether the first-run setup still holds the server from hosting */
int server_control_setup_waiting(void);
/* the next command queued, in line, where it came from, in source, its
flags, and the permissions and role of the account or key it came with:
its ticket, which server_control_finish takes, or 0 for none */
int server_control_next(char *line, int line_size, char *source, int source_size, int *flags,
	unsigned int *permissions, int *role);
/* a command's payload (a request's body), by its ticket */
const char *server_control_payload(int ticket);
/* a command run: whether it succeeded, and its output */
void server_control_finish(int ticket, int ok, const char *output);
/* a line of the server's log, for the API's recent log */
void server_control_log(const char *text);

#endif
