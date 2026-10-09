/*
SERVER_CONTROL.C

The dedicated server's console and control API (server/docs/admin.md): a
thread of their own reads commands typed on the server's standard input
and the API's HTTP requests, and queues them for the game's main thread,
which runs them (server/src/server_commands.c, each frame) and hands back
their output. Nothing here touches the game: it is built with the host's
ABI, and what crosses to the game's units is plain types.

The console: commands typed on standard input, when it is a terminal (or
HALO_DEDICATED_CONSOLE says so), their output on standard output.

The control API: HTTP/1.1 and JSON on a TCP listener of its own, off unless
HALO_DEDICATED_CONTROL gives an address (127.0.0.1 unless it says another,
which is warned of: it is plain HTTP, for a tunnel or a TLS proxy in front).
  GET  /v1/status    the server's state (sv_status, as JSON)
  GET  /v1/players   its players (sv_players, as JSON)
  POST /v1/command   {"command": "sv_kick 3"}: a command run, its output
  GET  /v1/log?since=<n>  the recent lines of the server's log after line n
  GET  /v1/bans, /v1/mapcycle, /v1/maps  sv_banlist, sv_mapcycle, sv_maps
Every request needs a token (Authorization: Bearer <token>). There is no
default: the first time the API is on, the server makes one, prints it once
on its standard output, and keeps only its Argon2id hash, salted, in the
data folder (control_credentials.txt). Wrong tokens are limited (five an
address, then five minutes refused, and so many checks a minute for
everyone); a right one is remembered for the run (a keyed hash of it), so
that only the first request of each pays for Argon2id, and it is let in
even from an address refused for others' wrong tokens. A request is read
whole with limits on every part (control_protocol.c), one a connection;
a connection that does not send one soon is closed. Every command the API
runs is logged with the credential it came with; reads (status, players,
the log) are not, a web page asks for them every few seconds.

The web admin page (control_web.h): its files (GET /, /app.js, ...) from
the program itself, to anyone; everything else as the API, with a session
instead of a bearer token: POST /v1/login with a token makes one (a random
id in an HttpOnly, SameSite=Strict cookie; only its keyed hash kept, with
an idle and an absolute expiry), GET /v1/session tells its CSRF token, and
every POST of a session must carry it (X-CSRF-Token) and come from this
server's own page; POST /v1/logout ends it. Logins count against the same
limits as tokens, and are logged, as is every command a session runs
("web <name> <id>"). Every response carries a strict
Content-Security-Policy and no-caching headers.

The console's sv_admin_* commands (run here, never the API's) manage the
credentials file: one credential (a token of its own) for each admin, a
new token for one, or one taken out; a token is printed once, on the
console, and its hash kept.

The log lines the API hands out (control_log, from errors.c and
platform_log) are kept in a ring here, any public address in them hidden
(control_protocol.h), whatever debug.log_addresses says.
*/

#include "server_control.h"
#include "control_protocol.h"
#include "control_web.h"
#include "control_roles.h"
#include "control_accounts.h"
#include "control_tls.h"
#include "../src/server_roles.h"
#include "../src/server_link.h"

#include "monocypher.h"
#include "qrcodegen.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* (Linux's; the tests' harness builds this unit on macOS too, which has
neither: server/tests/control_harness.c) */
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* the platform layer's (xbox_files.c, log_address.c): plain types */
const char *platform_data_root(void);
const char *log_address(const unsigned char *bytes, int length, int port, char *text, int size);

/* ---------- constants */

enum
{
	/* commands waiting for the main thread, or running, at most */
	QUEUE_SIZE = 32,
	/* the API's connections at once, at most */
	MAXIMUM_CONNECTIONS = 16,
	/* a connection's request, whole, within this long ... */
	REQUEST_SECONDS = 10,
	/* ... its command's output within this long (a map loading holds the
	main thread a few seconds) ... */
	COMMAND_SECONDS = 20,
	/* ... and its response taken within this long */
	RESPONSE_SECONDS = 10,
	/* a command's output kept, at most */
	MAXIMUM_OUTPUT = 64 * 1024,
	/* the log's lines kept, and the most one response hands out */
	LOG_LINES = 1024,
	LOG_LINE_SIZE = 256,
	LOG_RESPONSE_LINES = 500,
	/* the credentials a file may hold, at most */
	MAXIMUM_CREDENTIALS = 8,
	/* the console's line, at most (a command's, command_line.h) */
	CONSOLE_LINE_SIZE = CONTROL_MAXIMUM_COMMAND,
	/* the control panel's accounts and invitations, at most */
	MAXIMUM_ACCOUNTS = 64,
	MAXIMUM_INVITES = 32,
	/* the first owner's setup code is good this long (a new one each start
	while there is no owner, or sv_account_setup) */
	SETUP_SECONDS = 24 * 60 * 60,
	/* the audit file's last lines a request hands out, at most */
	AUDIT_LINES = 200,
};

enum
{
	TICKET_FREE,
	TICKET_QUEUED,
	TICKET_TAKEN,
	TICKET_DONE,
	/* (its connection gave up on it; the main thread's answer is dropped) */
	TICKET_ABANDONED,
};

enum
{
	CONNECTION_FREE,
	/* TLS's handshake (the listener's TLS: beyond the machine) */
	CONNECTION_HANDSHAKING,
	CONNECTION_READING,
	CONNECTION_WAITING,
	CONNECTION_WRITING,
};

enum
{
	/* what a waiting connection's command answers */
	ANSWER_JSON,
	ANSWER_COMMAND,
};

#define CREDENTIALS_FILE "control_credentials.txt"
#define ACCOUNTS_FILE "control_accounts.txt"
#define AUDIT_FILE "control_audit.log"

/* ---------- structures */

struct ticket
{
	int state;
	int flags;
	/* who it is for: their permissions and role (control_roles.h) */
	unsigned int permissions;
	int role;
	/* a file's text the request brought (POST /v1/file), or NULL */
	char *payload;
	/* (the order they came in) */
	unsigned long long sequence;
	char line[CONTROL_MAXIMUM_COMMAND];
	char source[80];
	/* the console's (its output printed), else the API connection's */
	int console;
	int ok;
	char *output;
};

struct connection
{
	int state;
	int fd;
	/* its address (for the log, as the log shows addresses), and a keyed
	hash of it, the run's (what the limits count: no address is kept) */
	uint8_t address[16];
	uint8_t tag[16];
	/* its TLS, if the listener speaks it */
	struct control_tls_connection *tls;
	int64_t deadline;
	char request[CONTROL_MAXIMUM_HEAD + CONTROL_MAXIMUM_BODY + 1];
	size_t length;
	int ticket;
	int answer;
	char *response;
	size_t response_length;
	size_t sent;
};

struct log_line
{
	unsigned long long sequence;
	long long time;
	char text[LOG_LINE_SIZE];
};

/* ---------- globals */

static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct ticket tickets[QUEUE_SIZE];
static unsigned long long ticket_sequence;
/* the control thread's wake-up: a byte written when a command is done */
static int wake_pipe[2] = { -1, -1 };

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct log_line log_lines[LOG_LINES];
static unsigned long long log_sequence;

/* the control thread's own */
static struct
{
	int started;
	int console;
	int console_open;
	char console_line[CONSOLE_LINE_SIZE];
	size_t console_length;
	int console_overlong;
	int listener;
	struct connection connections[MAXIMUM_CONNECTIONS];
	struct control_credential credentials[MAXIMUM_CREDENTIALS];
	int credential_count;
	/* the credentials file's lines that were no credential (it is then not
	rewritten: sv_admin_* refuse) */
	int credentials_skipped;
	/* the listener's address is the loopback's; the web page was reached
	straight over the network once (warned of) */
	int listener_loopback;
	int warned_direct;
	struct control_web_sessions sessions;
	/* the tokens checked right this run, by a keyed hash of each */
	uint8_t run_key[32];
	uint8_t remembered[MAXIMUM_CREDENTIALS][32];
	int remembered_valid[MAXIMUM_CREDENTIALS];
	struct control_limiter limiter;
	/* the listener speaks TLS (beyond the machine: control_tls.h), its
	certificate's fingerprint, and when the operator's files were last
	looked at for a renewal */
	int tls;
	char fingerprint[CONTROL_TLS_FINGERPRINT_SIZE];
	char listen_setting[96];
} control;

/* the control panel's accounts (control_accounts.h), the control thread's */
static struct
{
	int loaded;
	struct control_account accounts[MAXIMUM_ACCOUNTS];
	struct control_account_backoff backoff[MAXIMUM_ACCOUNTS];
	int count;
	struct control_invite invites[MAXIMUM_INVITES];
	int invite_count;
	int require_2fa;
	/* the file's lines that were nothing it takes (it is then not written:
	changes refuse) */
	int skipped;
	/* the first owner's setup code (while there is no owner): its hash, and
	until when it is good */
	int setup_active;
	uint8_t setup_hash[32];
	int64_t setup_expires;
	/* each account's bind request (server_roles.h), 0 none */
	unsigned int bind_request[MAXIMUM_ACCOUNTS];
} panel;

/* ---------- private code */

static int64_t monotonic_seconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec;
}

static int random_bytes(void *buffer, size_t size)
{
	int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	size_t done = 0;

	if (fd < 0)
		return 0;
	while (done < size)
	{
		ssize_t count = read(fd, (char *)buffer + done, size - done);

		if (count <= 0)
		{
			if (count < 0 && errno == EINTR)
				continue;
			close(fd);
			return 0;
		}
		done += (size_t)count;
	}
	close(fd);
	return 1;
}

/* a free ticket (the queue's lock held): its index, or -1 */
static int free_ticket(void)
{
	int index;

	for (index = 0; index < QUEUE_SIZE; index++)
	{
		if (tickets[index].state == TICKET_FREE)
			return index;
	}
	return -1;
}

static void release_ticket(int index)
{
	free(tickets[index].output);
	if (tickets[index].payload)
	{
		crypto_wipe(tickets[index].payload, strlen(tickets[index].payload));
		free(tickets[index].payload);
	}
	memset(&tickets[index], 0, sizeof(tickets[index]));
}

/* a line of the control's own for the log, which the main thread writes
(the control thread does not touch the game's log) */
static void notice(const char *format, ...)
{
	int index;
	va_list arguments;

	pthread_mutex_lock(&queue_mutex);
	index = free_ticket();
	if (index >= 0)
	{
		va_start(arguments, format);
		vsnprintf(tickets[index].line, sizeof(tickets[index].line), format, arguments);
		va_end(arguments);
		tickets[index].state = TICKET_QUEUED;
		tickets[index].flags = CONTROL_NOTICE;
		tickets[index].sequence = ++ticket_sequence;
	}
	pthread_mutex_unlock(&queue_mutex);
}

/* a command queued for the main thread, for someone of a role with those
permissions (with a file's text, payload, copied; may be NULL): its ticket,
or -1 if the queue is full (or there is no memory for the payload) */
static int queue_command(const char *line, const char *source, int flags, int console, int role,
	unsigned int permissions, const char *payload)
{
	int index;
	char *copy = NULL;

	if (payload)
	{
		size_t length = strlen(payload);

		copy = malloc(length + 1);
		if (!copy)
			return -1;
		memcpy(copy, payload, length + 1);
	}
	pthread_mutex_lock(&queue_mutex);
	index = free_ticket();
	if (index >= 0)
	{
		snprintf(tickets[index].line, sizeof(tickets[index].line), "%s", line);
		snprintf(tickets[index].source, sizeof(tickets[index].source), "%s", source);
		tickets[index].state = TICKET_QUEUED;
		tickets[index].flags = flags;
		tickets[index].console = console;
		tickets[index].role = role;
		tickets[index].permissions = permissions;
		tickets[index].payload = copy;
		tickets[index].sequence = ++ticket_sequence;
		copy = NULL;
	}
	pthread_mutex_unlock(&queue_mutex);
	free(copy);
	return index;
}

static int truthy(const char *value)
{
	return !strcmp(value, "1") || !strcasecmp(value, "true") || !strcasecmp(value, "yes") || !strcasecmp(value, "on");
}

static int falsy(const char *value)
{
	return !value[0] || !strcmp(value, "0") || !strcasecmp(value, "false") || !strcasecmp(value, "no") ||
		!strcasecmp(value, "off");
}

/* ---------- credentials */

static void credentials_path(char *path, size_t size)
{
	const char *root = platform_data_root();

	snprintf(path, size, "%s/%s", root && root[0] ? root : ".", CREDENTIALS_FILE);
}

/* the credentials file's, or a new one made (and its token printed once):
whether there is a credential to check tokens against */
static int load_credentials(void)
{
	char path[1024];
	char line[CONTROL_CREDENTIAL_LINE + 2];
	FILE *file;
	struct stat information;
	int line_number = 0;

	credentials_path(path, sizeof(path));
	file = fopen(path, "r");
	if (file)
	{
		if (!fstat(fileno(file), &information) && (information.st_mode & 077))
			notice("%s can be read by others than its owner (chmod 600 it)", CREDENTIALS_FILE);
		while (fgets(line, sizeof(line), file))
		{
			line_number++;
			if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
				continue;
			if (control.credential_count >= MAXIMUM_CREDENTIALS ||
				!control_credential_parse(line, &control.credentials[control.credential_count]))
			{
				notice("line %d of %s is not a credential; it is left out", line_number, CREDENTIALS_FILE);
				control.credentials_skipped++;
				continue;
			}
			control.credential_count++;
		}
		fclose(file);
		if (!control.credential_count)
		{
			notice("%s holds no credential, so the control API refuses every request: delete it, and restart "
				"the server, for a new token", CREDENTIALS_FILE);
			return 0;
		}
		notice("the control API's credentials: %d, from %s", control.credential_count, CREDENTIALS_FILE);
		return 1;
	}
	/* the first time: a token made, its hash kept, the token shown once */
	{
		uint8_t token_bytes[CONTROL_TOKEN_BYTES];
		uint8_t id[CONTROL_ID_LENGTH / 2];
		uint8_t salt[CONTROL_SALT_BYTES];
		char token[CONTROL_TOKEN_LENGTH + 1];
		char text[CONTROL_CREDENTIAL_LINE];
		struct control_credential *credential = &control.credentials[0];
		int fd;
		FILE *out;

		if (!random_bytes(token_bytes, sizeof(token_bytes)) || !random_bytes(id, sizeof(id)) ||
			!random_bytes(salt, sizeof(salt)))
		{
			notice("no random bytes for a control token: the control API is off");
			return 0;
		}
		control_token_text(token_bytes, token);
		crypto_wipe(token_bytes, sizeof(token_bytes));
		if (!control_credential_make(token, "admin", id, salt, CONTROL_ARGON2_KIB, CONTROL_ARGON2_PASSES, credential))
		{
			crypto_wipe(token, sizeof(token));
			notice("no memory to hash a control token: the control API is off");
			return 0;
		}
		control_credential_line(credential, text);
		fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
		out = fd >= 0 ? fdopen(fd, "w") : NULL;
		if (!out)
		{
			if (fd >= 0)
				close(fd);
			crypto_wipe(token, sizeof(token));
			notice("cannot write %s in the data folder (%s): the control API is off", CREDENTIALS_FILE, strerror(errno));
			return 0;
		}
		fprintf(out,
			"# The ChupathingyCE Dedicated Server's control API credentials (server/docs/admin.md).\n"
			"# Each line is a token's Argon2id hash, never the token. Delete this file and restart the\n"
			"# server for a new token; the old one stops working.\n"
			"%s\n", text);
		if (fflush(out) || fsync(fileno(out)) || fclose(out))
		{
			crypto_wipe(token, sizeof(token));
			notice("cannot write %s in the data folder: the control API is off", CREDENTIALS_FILE);
			unlink(path);
			return 0;
		}
		control.credential_count = 1;
		/* (on the console only: never in debug.txt, nor the log the API
		hands out) */
		printf("\n"
			"ChupathingyCE Dedicated Server: the control API's token, shown this once:\n"
			"\n"
			"    %s\n"
			"\n"
			"Keep it somewhere safe. The server keeps only its hash (%s in the data\n"
			"folder): delete that file and restart the server for a new token.\n"
			"\n", token, CREDENTIALS_FILE);
		fflush(stdout);
		crypto_wipe(token, sizeof(token));
		notice("a new control API credential (%s %s): its token was printed once on the server's output, and "
			"its hash kept in %s", credential->name, credential->id, CREDENTIALS_FILE);
		return 1;
	}
}

/* the credential a token checked right this run is (its index), by its
keyed hash; -1 if it is none */
static int remembered_credential(const char *token)
{
	uint8_t remembered[32];
	int index;
	int found = -1;

	crypto_blake2b_keyed(remembered, sizeof(remembered), control.run_key, sizeof(control.run_key),
		(const uint8_t *)token, strlen(token));
	for (index = 0; index < control.credential_count; index++)
	{
		if (control.remembered_valid[index] && !crypto_verify32(remembered, control.remembered[index]))
			found = index;
	}
	crypto_wipe(remembered, sizeof(remembered));
	return found;
}

/* the credential a token is (its index), checking it; -1 if none, -2 if it
cannot be checked now (too many checks: 429), -3 if it could not be at all
(no memory: 503) */
static int authenticate(const char *token, int64_t now)
{
	uint8_t remembered[32];
	int index;
	int found = remembered_credential(token);

	if (found >= 0)
		return found;
	crypto_blake2b_keyed(remembered, sizeof(remembered), control.run_key, sizeof(control.run_key),
		(const uint8_t *)token, strlen(token));
	if (!control_limiter_take_check(&control.limiter, now))
		return -2;
	for (index = 0; index < control.credential_count; index++)
	{
		int result = control_credential_check(&control.credentials[index], token);

		if (result < 0)
			return -3;
		if (result > 0 && found < 0)
			found = index;
	}
	if (found >= 0)
	{
		memcpy(control.remembered[found], remembered, sizeof(remembered));
		control.remembered_valid[found] = 1;
	}
	return found;
}

/* ---------- the control panel's accounts */

static void accounts_path(char *path, size_t size)
{
	const char *root = platform_data_root();

	snprintf(path, size, "%s/%s", root && root[0] ? root : ".", ACCOUNTS_FILE);
}

/* the accounts bound to a game, for the roles (server_roles.h) */
static void publish_account_keys(void)
{
	unsigned char keys[MAXIMUM_ACCOUNTS * CONTROL_KEY_BYTES];
	int roles[MAXIMUM_ACCOUNTS];
	const char *names[MAXIMUM_ACCOUNTS];
	int count = 0;
	int index;

	for (index = 0; index < panel.count; index++)
	{
		if (!panel.accounts[index].has_key)
			continue;
		memcpy(keys + count * CONTROL_KEY_BYTES, panel.accounts[index].key, CONTROL_KEY_BYTES);
		roles[count] = panel.accounts[index].role;
		names[count] = panel.accounts[index].name;
		count++;
	}
	server_roles_set_accounts(count, keys, roles, names);
}

static int owner_count(void)
{
	int count = 0;
	int index;

	for (index = 0; index < panel.count; index++)
		count += panel.accounts[index].role == CONTROL_ROLE_OWNER;
	return count;
}

static int account_named(const char *name)
{
	int index;

	for (index = 0; index < panel.count; index++)
	{
		if (!strcmp(panel.accounts[index].name, name))
			return index;
	}
	return -1;
}

static int account_with_id(const char *id)
{
	int index;

	for (index = 0; index < panel.count; index++)
	{
		if (!strcmp(panel.accounts[index].id, id))
			return index;
	}
	return -1;
}

/* the invitations past their time forgotten */
static void forget_expired_invites(void)
{
	int64_t now = (int64_t)time(NULL);
	int index = 0;

	while (index < panel.invite_count)
	{
		if (panel.invites[index].expires <= now)
		{
			memmove(&panel.invites[index], &panel.invites[index + 1],
				(size_t)(panel.invite_count - index - 1) * sizeof(panel.invites[0]));
			panel.invite_count--;
			continue;
		}
		index++;
	}
}

static void load_accounts(void)
{
	char path[1024];
	char line[CONTROL_ACCOUNT_LINE + 2];
	FILE *file;
	struct stat information;
	int line_number = 0;

	panel.loaded = 1;
	accounts_path(path, sizeof(path));
	file = fopen(path, "r");
	if (!file)
		return;
	if (!fstat(fileno(file), &information) && (information.st_mode & 077))
		notice("%s can be read by others than its owner (chmod 600 it)", ACCOUNTS_FILE);
	while (fgets(line, sizeof(line), file))
	{
		int value;

		line_number++;
		if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
			continue;
		if (panel.count < MAXIMUM_ACCOUNTS && control_account_parse(line, &panel.accounts[panel.count]))
		{
			if (account_named(panel.accounts[panel.count].name) >= 0 ||
				account_with_id(panel.accounts[panel.count].id) >= 0)
			{
				crypto_wipe(&panel.accounts[panel.count], sizeof(panel.accounts[0]));
				panel.skipped++;
				notice("line %d of %s is an account already there; it is left out", line_number, ACCOUNTS_FILE);
				continue;
			}
			panel.count++;
		}
		else if (panel.invite_count < MAXIMUM_INVITES && control_invite_parse(line, &panel.invites[panel.invite_count]))
			panel.invite_count++;
		else if (control_require_2fa_parse(line, &value))
			panel.require_2fa = value;
		else
		{
			panel.skipped++;
			notice("line %d of %s is not one it takes; it is left out (changes to accounts refuse until it is fixed)",
				line_number, ACCOUNTS_FILE);
		}
	}
	crypto_wipe(line, sizeof(line));
	fclose(file);
	forget_expired_invites();
	notice("the control panel's accounts: %d (%d owners), from %s", panel.count, owner_count(), ACCOUNTS_FILE);
}

/* the accounts file written again, whole (a new file, then renamed over
it): 1, else 0 and why in problem */
static int write_accounts(char *problem, size_t problem_size)
{
	char path[1024];
	char temporary[1040];
	char line[CONTROL_ACCOUNT_LINE];
	int index;
	int fd;
	FILE *out;

	if (panel.skipped)
	{
		snprintf(problem, problem_size, "%s has lines it does not take (the log says which): fix them, and restart "
			"the server, before accounts are changed", ACCOUNTS_FILE);
		return 0;
	}
	forget_expired_invites();
	accounts_path(path, sizeof(path));
	snprintf(temporary, sizeof(temporary), "%s.new", path);
	unlink(temporary);
	fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	out = fd >= 0 ? fdopen(fd, "w") : NULL;
	if (!out)
	{
		if (fd >= 0)
			close(fd);
		snprintf(problem, problem_size, "cannot write %s.new in the data folder (%s): nothing changed", ACCOUNTS_FILE,
			strerror(errno));
		return 0;
	}
	fprintf(out,
		"# The ChupathingyCE Dedicated Server's control panel accounts (server/docs/moderation.md).\n"
		"# Passwords are kept as Argon2id hashes; second factors' secrets are kept as they are, so\n"
		"# keep this file private (and back it up with the rest of the data folder).\n"
		"v1 require_2fa %d\n", panel.require_2fa ? 1 : 0);
	for (index = 0; index < panel.count; index++)
	{
		if (control_account_line(&panel.accounts[index], line, sizeof(line)) > 0)
			fprintf(out, "%s\n", line);
	}
	for (index = 0; index < panel.invite_count; index++)
	{
		if (control_invite_line(&panel.invites[index], line, sizeof(line)) > 0)
			fprintf(out, "%s\n", line);
	}
	crypto_wipe(line, sizeof(line));
	if (fflush(out) || fsync(fileno(out)) || fclose(out) || rename(temporary, path))
	{
		snprintf(problem, problem_size, "cannot write %s in the data folder (%s): nothing changed", ACCOUNTS_FILE,
			strerror(errno));
		unlink(temporary);
		return 0;
	}
	publish_account_keys();
	return 1;
}

/* the first owner's setup code: a new one made (and printed, on the
console alone) while there is no owner */
static void make_setup_code(void)
{
	uint8_t bytes[CONTROL_CODE_BYTES];
	char code[CONTROL_CODE_TEXT + 1];

	if (owner_count() || control.listener < 0)
		return;
	if (!random_bytes(bytes, sizeof(bytes)))
	{
		notice("no random bytes for the control panel's setup code");
		return;
	}
	control_code_text("set_", bytes, code, sizeof(code));
	crypto_wipe(bytes, sizeof(bytes));
	control_code_hash(code, panel.setup_hash);
	panel.setup_expires = (int64_t)time(NULL) + SETUP_SECONDS;
	panel.setup_active = 1;
	/* (on the console only: never in debug.txt, nor the log the API hands
	out) */
	printf("\n"
		"ChupathingyCE Dedicated Server: the control panel has no owner yet. Open\n"
		"\n"
		"    %s://%s/#setup=%s\n"
		"\n"
		"(through your SSH tunnel, or at the server's address) and make the owner's account\n"
		"with this one-time code. It is good for 24 hours, and until the account is made;\n"
		"sv_account_setup on this console makes a new one.\n"
		"\n", control.tls ? "https" : "http", control.listen_setting, code);
	if (control.tls)
	{
		printf("The panel's certificate is the server's own. Check that your browser shows this\n"
			"SHA-256 fingerprint before you type a password into it:\n"
			"\n"
			"    %s\n"
			"\n", control.fingerprint);
	}
	fflush(stdout);
	crypto_wipe(code, sizeof(code));
	notice("the control panel's setup code was printed on the server's output (it has no owner yet)");
}

/* an invitation made, its code printed into code (code_size bytes) for
whoever made it: 1, else 0 and why */
static int make_invite(int role, const char *account, const char *made_by, char *code, size_t code_size, char *problem,
	size_t problem_size)
{
	uint8_t bytes[CONTROL_CODE_BYTES];
	uint8_t id[CONTROL_ID_LENGTH / 2];
	struct control_invite *invite;

	forget_expired_invites();
	if (panel.invite_count >= MAXIMUM_INVITES)
	{
		snprintf(problem, problem_size, "%d invitations are waiting: revoke one first", (int)MAXIMUM_INVITES);
		return 0;
	}
	if (!random_bytes(bytes, sizeof(bytes)) || !random_bytes(id, sizeof(id)))
	{
		snprintf(problem, problem_size, "no random bytes for an invitation");
		return 0;
	}
	invite = &panel.invites[panel.invite_count];
	memset(invite, 0, sizeof(*invite));
	control_hex_text(id, sizeof(id), invite->id);
	invite->role = role;
	invite->expires = (int64_t)time(NULL) + CONTROL_INVITE_SECONDS;
	snprintf(invite->account, sizeof(invite->account), "%s", account ? account : "");
	snprintf(invite->made_by, sizeof(invite->made_by), "%s", made_by);
	control_code_text("inv_", bytes, code, code_size);
	crypto_wipe(bytes, sizeof(bytes));
	control_code_hash(code, invite->code_hash);
	panel.invite_count++;
	if (!write_accounts(problem, problem_size))
	{
		panel.invite_count--;
		crypto_wipe(code, code_size);
		return 0;
	}
	return 1;
}

/* the invitation a code is (its index; every one compared, in constant
time), or -1 */
static int invite_for_code(const char *code)
{
	uint8_t hash[32];
	int found = -1;
	int index;

	forget_expired_invites();
	if (!control_code_valid("inv_", code))
		return -1;
	control_code_hash(code, hash);
	for (index = 0; index < panel.invite_count; index++)
	{
		if (!crypto_verify32(hash, panel.invites[index].code_hash))
			found = index;
	}
	crypto_wipe(hash, sizeof(hash));
	return found;
}

static void remove_invite(int index)
{
	memmove(&panel.invites[index], &panel.invites[index + 1],
		(size_t)(panel.invite_count - index - 1) * sizeof(panel.invites[0]));
	panel.invite_count--;
}

/* an account's sessions ended (taken out, its password set again) */
static int end_account_sessions(const char *id)
{
	return control_web_sessions_end_credential(&control.sessions, id);
}

/* an account made (from the setup code, or an invitation): 1, else 0 and
why */
static int add_account(const char *name, int role, const char *password, char *problem, size_t problem_size)
{
	uint8_t id[CONTROL_ID_LENGTH / 2];
	uint8_t salt[CONTROL_SALT_BYTES];
	const char *reason;

	if (!control_account_name_valid(name))
	{
		snprintf(problem, problem_size, "a name is 1 to 31 of a-z, 0-9, _ . - (lowercase), beginning with a letter "
			"or digit");
		return 0;
	}
	if (account_named(name) >= 0)
	{
		snprintf(problem, problem_size, "there is an account %s already", name);
		return 0;
	}
	if (!control_password_acceptable(password, &reason))
	{
		snprintf(problem, problem_size, "%s", reason);
		return 0;
	}
	if (panel.count >= MAXIMUM_ACCOUNTS)
	{
		snprintf(problem, problem_size, "%d accounts at most", (int)MAXIMUM_ACCOUNTS);
		return 0;
	}
	if (!random_bytes(id, sizeof(id)) || !random_bytes(salt, sizeof(salt)))
	{
		snprintf(problem, problem_size, "no random bytes");
		return 0;
	}
	if (!control_account_make(name, id, role, password, salt, CONTROL_ARGON2_KIB, CONTROL_ARGON2_PASSES,
		(int64_t)time(NULL), &panel.accounts[panel.count]))
	{
		snprintf(problem, problem_size, "no memory to hash the password");
		return 0;
	}
	memset(&panel.backoff[panel.count], 0, sizeof(panel.backoff[0]));
	panel.bind_request[panel.count] = 0;
	panel.count++;
	if (!write_accounts(problem, problem_size))
	{
		panel.count--;
		crypto_wipe(&panel.accounts[panel.count], sizeof(panel.accounts[0]));
		return 0;
	}
	return 1;
}

static void remove_account_at(int index)
{
	crypto_wipe(&panel.accounts[index], sizeof(panel.accounts[0]));
	memmove(&panel.accounts[index], &panel.accounts[index + 1],
		(size_t)(panel.count - index - 1) * sizeof(panel.accounts[0]));
	memmove(&panel.backoff[index], &panel.backoff[index + 1],
		(size_t)(panel.count - index - 1) * sizeof(panel.backoff[0]));
	memmove(&panel.bind_request[index], &panel.bind_request[index + 1],
		(size_t)(panel.count - index - 1) * sizeof(panel.bind_request[0]));
	panel.count--;
}

/* ---------- the console's account commands */

/* a console line that is one of the accounts' commands (sv_account_*), run
here, where the accounts are: 1, else 0 */
static int console_account(const char *line)
{
	char words[4][64];
	char extra[2];
	char problem[256];
	int count;
	int index;

	words[0][0] = words[1][0] = words[2][0] = 0;
	count = sscanf(line, "%63s %63s %63s %1s", words[0], words[1], words[2], extra);
	if (count < 1 || strncmp(words[0], "sv_account_", 11))
		return 0;
	notice("console: %s", line);
	if (control.listener < 0)
	{
		printf("the control panel is off (HALO_DEDICATED_CONTROL): no accounts here to manage\n");
	}
	else if (!strcmp(words[0], "sv_account_list"))
	{
		if (!panel.count)
			printf("no accounts%s\n", panel.setup_active ? " (the setup code printed at start makes the owner's)" : "");
		for (index = 0; index < panel.count; index++)
		{
			char key[9] = "-";

			if (panel.accounts[index].has_key)
				control_key_short(panel.accounts[index].key, key);
			printf("%-31s %-9s %s  second factor: %s  game: %s\n", panel.accounts[index].name,
				control_role_name(panel.accounts[index].role), panel.accounts[index].id,
				panel.accounts[index].has_totp ? "on" : "off", key);
		}
		printf("second factor required: %s\n", panel.require_2fa ? "yes" : "no");
	}
	else if (!strcmp(words[0], "sv_account_setup"))
	{
		if (owner_count())
			printf("there is an owner already: sv_account_invite owner makes another\n");
		else
			make_setup_code();
	}
	else if (!strcmp(words[0], "sv_account_invite") && count == 2)
	{
		int role = control_role_parse(words[1]);
		char code[CONTROL_CODE_TEXT + 1];

		if (role < 0)
			printf("a role is moderator, admin or owner\n");
		else if (!make_invite(role, NULL, "console", code, sizeof(code), problem, sizeof(problem)))
			printf("%s\n", problem);
		else
		{
			printf("\nAn invitation for a new %s, good once, for 48 hours:\n\n    %s://%s/#invite=%s\n\n",
				control_role_name(role), control.tls ? "https" : "http", control.listen_setting, code);
			server_audit("console", "", CONTROL_ROLE_OWNER, "invite", control_role_name(role), NULL, 1, NULL);
			crypto_wipe(code, sizeof(code));
		}
	}
	else if (!strcmp(words[0], "sv_account_role") && count == 3)
	{
		int role = control_role_parse(words[2]);

		index = account_named(words[1]);
		if (index < 0)
			printf("no account %s (sv_account_list)\n", words[1]);
		else if (role < 0)
			printf("a role is moderator, admin or owner\n");
		else if (panel.accounts[index].role == CONTROL_ROLE_OWNER && role != CONTROL_ROLE_OWNER && owner_count() == 1)
			printf("%s is the only owner: make another owner first\n", words[1]);
		else
		{
			int old = panel.accounts[index].role;

			panel.accounts[index].role = role;
			if (!write_accounts(problem, sizeof(problem)))
			{
				panel.accounts[index].role = old;
				printf("%s\n", problem);
			}
			else
			{
				printf("%s is %s now\n", words[1], control_role_name(role));
				server_audit("console", "", CONTROL_ROLE_OWNER, "account_role", words[1], control_role_name(role), 1,
					NULL);
			}
		}
	}
	else if ((!strcmp(words[0], "sv_account_remove") || !strcmp(words[0], "sv_account_reset")) && count == 2)
	{
		index = account_named(words[1]);
		if (index < 0)
			printf("no account %s (sv_account_list)\n", words[1]);
		else if (!strcmp(words[0], "sv_account_remove"))
		{
			if (panel.accounts[index].role == CONTROL_ROLE_OWNER && owner_count() == 1)
				printf("%s is the only owner: make another owner first\n", words[1]);
			else
			{
				struct control_account removed = panel.accounts[index];
				struct control_account_backoff backoff = panel.backoff[index];

				remove_account_at(index);
				if (!write_accounts(problem, sizeof(problem)))
				{
					memmove(&panel.accounts[index + 1], &panel.accounts[index],
						(size_t)(panel.count - index) * sizeof(panel.accounts[0]));
					memmove(&panel.backoff[index + 1], &panel.backoff[index],
						(size_t)(panel.count - index) * sizeof(panel.backoff[0]));
					panel.accounts[index] = removed;
					panel.backoff[index] = backoff;
					panel.count++;
					printf("%s\n", problem);
				}
				else
				{
					int ended = end_account_sessions(removed.id);

					printf("%s taken out; %d session%s ended\n", words[1], ended, ended == 1 ? "" : "s");
					server_audit("console", "", CONTROL_ROLE_OWNER, "account_remove", words[1], NULL, 1, NULL);
				}
				crypto_wipe(&removed, sizeof(removed));
			}
		}
		else
		{
			char code[CONTROL_CODE_TEXT + 1];

			if (!make_invite(panel.accounts[index].role, words[1], "console", code, sizeof(code), problem,
				sizeof(problem)))
			{
				printf("%s\n", problem);
			}
			else
			{
				printf("\nA code for %s to set a new password (and second factor), good once, for 48 hours:\n\n"
					"    %s://%s/#invite=%s\n\n", words[1], control.tls ? "https" : "http", control.listen_setting, code);
				server_audit("console", "", CONTROL_ROLE_OWNER, "account_reset", words[1], NULL, 1, NULL);
				crypto_wipe(code, sizeof(code));
			}
		}
	}
	else
		printf("usage: sv_account_list, sv_account_setup, sv_account_invite <role>, sv_account_role <name> <role>, "
			"sv_account_remove <name>, sv_account_reset <name>\n");
	fflush(stdout);
	return 1;
}

/* ---------- the listener */

/* HALO_DEDICATED_CONTROL's address: "<port>", "<IPv4>:<port>",
"localhost:<port>" or "[<IPv6>]:<port>". 1, else 0 */
static int parse_listen_address(const char *text, struct sockaddr_storage *address, socklen_t *length,
	int *loopback)
{
	char host[64];
	const char *port_text;
	char *end;
	long port;

	memset(address, 0, sizeof(*address));
	if (text[0] == '[')
	{
		const char *close = strchr(text, ']');

		if (!close || close[1] != ':' || (size_t)(close - text - 1) >= sizeof(host))
			return 0;
		memcpy(host, text + 1, (size_t)(close - text - 1));
		host[close - text - 1] = 0;
		port_text = close + 2;
	}
	else
	{
		const char *colon = strrchr(text, ':');

		if (!colon)
		{
			snprintf(host, sizeof(host), "127.0.0.1");
			port_text = text;
		}
		else
		{
			if ((size_t)(colon - text) >= sizeof(host))
				return 0;
			memcpy(host, text, (size_t)(colon - text));
			host[colon - text] = 0;
			port_text = colon + 1;
		}
		if (!strcmp(host, "localhost"))
			snprintf(host, sizeof(host), "127.0.0.1");
	}
	if (!port_text[0] || port_text[0] < '0' || port_text[0] > '9')
		return 0;
	port = strtol(port_text, &end, 10);
	if (*end || port < 1 || port > 65535)
		return 0;
	if (text[0] == '[')
	{
		struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)address;

		if (inet_pton(AF_INET6, host, &ipv6->sin6_addr) != 1)
			return 0;
		ipv6->sin6_family = AF_INET6;
		ipv6->sin6_port = htons((uint16_t)port);
		*length = sizeof(*ipv6);
		*loopback = IN6_IS_ADDR_LOOPBACK(&ipv6->sin6_addr);
	}
	else
	{
		struct sockaddr_in *ipv4 = (struct sockaddr_in *)address;

		if (inet_pton(AF_INET, host, &ipv4->sin_addr) != 1)
			return 0;
		ipv4->sin_family = AF_INET;
		ipv4->sin_port = htons((uint16_t)port);
		*length = sizeof(*ipv4);
		*loopback = (ntohl(ipv4->sin_addr.s_addr) >> 24) == 127;
	}
	return 1;
}

/* whether an address is one no one on the internet has: private (10/8,
172.16/12, 192.168/16), shared (100.64/10, as Tailscale's), link-local, or
IPv6's unique local and link-local */
static int address_private(const struct sockaddr_storage *address)
{
	if (address->ss_family == AF_INET)
	{
		uint32_t value = ntohl(((const struct sockaddr_in *)address)->sin_addr.s_addr);

		return (value >> 24) == 10 || (value >> 20) == (172u << 4 | 1) || (value >> 16) == (192u << 8 | 168) ||
			(value >> 22) == (100u << 2 | 1) || (value >> 16) == (169u << 8 | 254);
	}
	if (address->ss_family == AF_INET6)
	{
		const uint8_t *bytes = ((const struct sockaddr_in6 *)address)->sin6_addr.s6_addr;

		return (bytes[0] & 0xFE) == 0xFC || (bytes[0] == 0xFE && (bytes[1] & 0xC0) == 0x80);
	}
	return 0;
}

static int start_listener(const char *setting)
{
	struct sockaddr_storage address;
	socklen_t length;
	int loopback = 0;
	int fd;
	int one = 1;
	const char *tls_setting = getenv("HALO_DEDICATED_CONTROL_TLS");
	int tls;

	if (!parse_listen_address(setting, &address, &length, &loopback))
	{
		notice("HALO_DEDICATED_CONTROL is not an address to listen on (port, 127.0.0.1:port, [::1]:port): the "
			"control API is off");
		return 0;
	}
	/* HTTPS beyond the machine (HALO_DEDICATED_CONTROL_TLS: auto, the
	default; on, always; off, never, which a public address refuses) */
	if (tls_setting && tls_setting[0] && truthy(tls_setting))
		tls = 1;
	else if (tls_setting && tls_setting[0] && falsy(tls_setting))
	{
		tls = 0;
		if (!loopback && !address_private(&address))
		{
			notice("HALO_DEDICATED_CONTROL_TLS is off, but %s is a public address: the control panel is HTTPS there, "
				"always. The control API is off", setting);
			return 0;
		}
	}
	else
		tls = !loopback;
	if (tls)
	{
		char problem[256];
		const char *name = getenv("HALO_DEDICATED_NAME");
		int made = 0;

		if (!control_tls_start(platform_data_root(), getenv("HALO_DEDICATED_CONTROL_CERT"),
			getenv("HALO_DEDICATED_CONTROL_KEY"), name && name[0] ? name : "chupathingyce-server", control.fingerprint,
			sizeof(control.fingerprint), &made, problem, sizeof(problem)))
		{
			notice("the control panel's HTTPS cannot start (%s): the control API is off", problem);
			return 0;
		}
		control.tls = 1;
		if (made)
			notice("the control panel made its own certificate (%s, %s)", CONTROL_TLS_CERTIFICATE_FILE,
				CONTROL_TLS_KEY_FILE);
	}
	fd = socket(address.ss_family, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
	{
		notice("the control API cannot listen (%s): it is off", strerror(errno));
		return 0;
	}
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (address.ss_family == AF_INET6)
		setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one));
	if (bind(fd, (struct sockaddr *)&address, length) || listen(fd, 16) ||
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK))
	{
		notice("the control API cannot listen on %s (%s): it is off", setting, strerror(errno));
		close(fd);
		return 0;
	}
	control.listener = fd;
	control.listener_loopback = loopback;
	/* (the address the setup link names: a port alone is this machine's;
	every address, "the server's") */
	if (!strchr(setting, ':') && !strchr(setting, '['))
		snprintf(control.listen_setting, sizeof(control.listen_setting), "127.0.0.1:%s", setting);
	else if (!strncmp(setting, "0.0.0.0:", 8))
		snprintf(control.listen_setting, sizeof(control.listen_setting), "<this server's address>:%s", setting + 8);
	else if (!strncmp(setting, "[::]:", 5))
		snprintf(control.listen_setting, sizeof(control.listen_setting), "<this server's address>:%s", setting + 5);
	else
		snprintf(control.listen_setting, sizeof(control.listen_setting), "%s", setting);
	notice("the control API and web admin page listen on %s (%s; server/docs/admin.md)", setting,
		control.tls ? "HTTPS" : "HTTP");
	if (control.tls)
	{
		/* (on the console too, for the operator to compare with what the
		browser shows) */
		printf("\nChupathingyCE Dedicated Server: the control panel is HTTPS on %s.\n"
			"Its certificate's SHA-256 fingerprint is\n\n    %s\n\n"
			"Check that your browser shows the same before you log in.\n\n", control.listen_setting,
			control.fingerprint);
		fflush(stdout);
		notice("the control panel's certificate's SHA-256 fingerprint is %s", control.fingerprint);
	}
	else if (!loopback)
	{
		notice("WARNING: the control API and web admin page listen beyond this machine (%s) as plain HTTP "
			"(HALO_DEDICATED_CONTROL_TLS=off): only on a private network such as Tailscale, or behind a TLS reverse "
			"proxy", setting);
	}
	return 1;
}

/* ---------- responses */

static void close_connection(struct connection *connection)
{
	if (connection->tls)
	{
		control_tls_close_notify(connection->tls);
		control_tls_free(connection->tls);
		connection->tls = NULL;
	}
	if (connection->fd >= 0)
		close(connection->fd);
	free(connection->response);
	if (connection->state == CONNECTION_WAITING && connection->ticket >= 0)
	{
		pthread_mutex_lock(&queue_mutex);
		if (tickets[connection->ticket].state == TICKET_QUEUED || tickets[connection->ticket].state == TICKET_DONE)
			release_ticket(connection->ticket);
		else if (tickets[connection->ticket].state == TICKET_TAKEN)
			tickets[connection->ticket].state = TICKET_ABANDONED;
		pthread_mutex_unlock(&queue_mutex);
	}
	memset(connection, 0, sizeof(*connection));
	connection->fd = -1;
	connection->ticket = -1;
	connection->state = CONNECTION_FREE;
}

/* a response: its body (length bytes, copied) of a type, and the security
headers every response has (control_web.h) */
static void respond_bytes(struct connection *connection, int status, const char *type, const void *body,
	size_t body_length, const char *extra_headers)
{
	char head[2048];
	int head_length = snprintf(head, sizeof(head),
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %lu\r\n"
		"%s"
		"Connection: close\r\n"
		"%s"
		"\r\n", status, control_status_text(status), type, (unsigned long)body_length, control_web_security_headers(),
		extra_headers ? extra_headers : "");
	char *response = head_length > 0 && (size_t)head_length < sizeof(head) ?
		malloc((size_t)head_length + body_length + 1) : NULL;

	free(connection->response);
	connection->response = NULL;
	if (!response)
	{
		close_connection(connection);
		return;
	}
	memcpy(response, head, (size_t)head_length);
	if (body_length)
		memcpy(response + head_length, body, body_length);
	response[head_length + body_length] = 0;
	connection->response = response;
	connection->response_length = (size_t)head_length + body_length;
	connection->sent = 0;
	connection->state = CONNECTION_WRITING;
	connection->deadline = monotonic_seconds() + RESPONSE_SECONDS;
}

/* a response, with a JSON body (taken: freed here) */
static void respond(struct connection *connection, int status, char *body, const char *extra_headers)
{
	respond_bytes(connection, status, "application/json; charset=utf-8", body, body ? strlen(body) : 0,
		extra_headers);
	if (body)
		crypto_wipe(body, strlen(body));
	free(body);
}

/* {"error": "<reason>"} */
static void respond_error(struct connection *connection, int status, const char *reason, const char *extra_headers)
{
	char text[256];
	char *body = malloc(320);

	if (body)
	{
		if (control_json_string(reason, text, sizeof(text)) < 0)
			snprintf(text, sizeof(text), "\"error\"");
		snprintf(body, 320, "{\"error\": %s}\n", text);
	}
	respond(connection, status, body, extra_headers);
}

/* the log's lines after since, as JSON: the oldest of them kept, up to
LOG_RESPONSE_LINES; "next" the last given (the next request's since), and
"missed" whether lines after since were no longer kept */
static void respond_log(struct connection *connection, uint64_t since)
{
	size_t size = 96 + (size_t)LOG_RESPONSE_LINES * (LOG_LINE_SIZE * 6 + 96);
	char *body = malloc(size);
	size_t length = 0;
	unsigned long long first, last, sequence, end, next;
	int count = 0;
	int missed;

	if (!body)
	{
		respond_error(connection, 503, "no memory", NULL);
		return;
	}
	pthread_mutex_lock(&log_mutex);
	last = log_sequence;
	first = last > LOG_LINES ? last - LOG_LINES + 1 : 1;
	sequence = since + 1 > first ? since + 1 : first;
	missed = last && since + 1 < first;
	end = sequence + LOG_RESPONSE_LINES - 1 < last ? sequence + LOG_RESPONSE_LINES - 1 : last;
	next = since > last ? last : since;
	length += (size_t)snprintf(body + length, size - length, "{\"lines\": [");
	for (; sequence <= end; sequence++)
	{
		struct log_line const *line = &log_lines[sequence % LOG_LINES];
		char text[LOG_LINE_SIZE * 6 + 3];

		if (line->sequence != sequence || control_json_string(line->text, text, sizeof(text)) < 0)
			continue;
		length += (size_t)snprintf(body + length, size - length, "%s{\"n\": %llu, \"time\": %lld, \"text\": %s}",
			count++ ? ", " : "", line->sequence, line->time, text);
		next = sequence;
	}
	pthread_mutex_unlock(&log_mutex);
	snprintf(body + length, size - length, "], \"next\": %llu, \"missed\": %s}\n", next, missed ? "true" : "false");
	respond(connection, 200, body, NULL);
}

/* a command's answer, once the main thread has run it */
static void respond_command(struct connection *connection, int ok, const char *output)
{
	size_t size = strlen(output) * 6 + 64;
	char *text = malloc(size);
	char *body = malloc(size + 32);

	if (!text || !body)
	{
		free(text);
		free(body);
		respond_error(connection, 503, "no memory", NULL);
		return;
	}
	if (connection->answer == ANSWER_JSON)
	{
		snprintf(body, size + 32, "%s\n", output);
		free(text);
		respond(connection, ok ? 200 : 500, body, NULL);
		return;
	}
	control_json_string(output, text, size);
	snprintf(body, size + 32, "{\"ok\": %s, \"output\": %s}\n", ok ? "true" : "false", text);
	free(text);
	respond(connection, 200, body, NULL);
}

/* ---------- requests */

static void address_text(const uint8_t address[16], char *text, int size)
{
	static const uint8_t mapped[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };

	if (!memcmp(address, mapped, sizeof(mapped)))
		log_address(address + 12, 4, -1, text, size);
	else
		log_address(address, 16, -1, text, size);
}

/* the credential with an id (its index), or -1 */
static int credential_by_id(const char *id)
{
	int index;

	for (index = 0; index < control.credential_count; index++)
	{
		if (!strcmp(control.credentials[index].id, id))
			return index;
	}
	return -1;
}

/* the web page reached straight over the network, no reverse proxy in
front: warned of once a run (its token and cookie cross it in the clear,
unless the network is private, as Tailscale's is) */
static void warn_direct(struct connection *connection, const struct control_request *request)
{
	static const uint8_t loopback4[13] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 127 };
	static const uint8_t loopback6[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
	char address[64];

	if (control.warned_direct || control.listener_loopback || request->proxied ||
		!memcmp(connection->address, loopback4, sizeof(loopback4)) ||
		!memcmp(connection->address, loopback6, sizeof(loopback6)))
	{
		return;
	}
	control.warned_direct = 1;
	address_text(connection->address, address, sizeof(address));
	notice("WARNING: the web admin page was reached from %s straight over the network, with no reverse proxy in "
		"front: its token and session cookie cross the network unencrypted. Unless this is a private network such "
		"as Tailscale, reach it through an SSH tunnel or a TLS reverse proxy instead (server/docs/admin.md)", address);
}

/* a token checked as the API checks one (the limits, Argon2id, the audit
line for a wrong one): the credential's index, or -1 with the response
given */
static int check_token(struct connection *connection, const char *token, int64_t now, const char *what,
	const char *challenge)
{
	int64_t retry_after;
	int credential;
	char address[64];

	/* (an address refused for wrong tokens still gets in with a token
already checked right this run: behind a reverse proxy, everyone's address
is the proxy's, and the admin is not locked out by someone guessing) */
	if (!(control_token_valid(token) && remembered_credential(token) >= 0) &&
		!control_limiter_allowed(&control.limiter, connection->tag, now, &retry_after))
	{
		char headers[64];

		snprintf(headers, sizeof(headers), "Retry-After: %lld\r\n", (long long)retry_after);
		respond_error(connection, 429, "too many wrong tokens from this address: try again later", headers);
		return -1;
	}
	if (!control_token_valid(token) || !control.credential_count)
	{
		control_limiter_failed(&control.limiter, connection->tag, now);
		address_text(connection->address, address, sizeof(address));
		notice("a %s with a malformed token, from %s", what, address);
		respond_error(connection, 401, "the token is not right", challenge);
		return -1;
	}
	credential = authenticate(token, now);
	if (credential == -2)
	{
		respond_error(connection, 429, "too many tokens checked: try again shortly", "Retry-After: 5\r\n");
		return -1;
	}
	if (credential == -3)
	{
		respond_error(connection, 503, "no memory to check the token", NULL);
		return -1;
	}
	if (credential < 0)
	{
		control_limiter_failed(&control.limiter, connection->tag, now);
		address_text(connection->address, address, sizeof(address));
		notice("a %s with a wrong token, from %s", what, address);
		respond_error(connection, 401, "the token is not right", challenge);
		return -1;
	}
	control_limiter_succeeded(&control.limiter, connection->tag);
	return credential;
}

/* who a request is from, once it is checked: a control token (the owner's,
for scripts), or a web session (a token's, or an account's) */
struct auth
{
	/* the session (its index), or -1 for a bearer token */
	int session;
	/* the account (its index), or -1 for a token */
	int account;
	/* "api <name> <id>", "web <name> <id>" */
	char source[96];
	char name[CONTROL_ACCOUNT_NAME_SIZE];
	int role;
	unsigned int permissions;
	/* an account that must set up its second factor before anything else
	(the owner requires one) */
	int restricted;
};

/* a JSON body being built: malloc'd, grown as it is written; NULL once
out of memory (respond() then answers 503) */
struct json_body
{
	char *text;
	size_t length;
	size_t size;
};

static void json_begin(struct json_body *body)
{
	body->size = 1024;
	body->length = 0;
	body->text = malloc(body->size);
	if (body->text)
		body->text[0] = 0;
}

static void json_printf(struct json_body *body, const char *format, ...)
	__attribute__((format(printf, 2, 3)));

static void json_printf(struct json_body *body, const char *format, ...)
{
	va_list arguments;
	int length;

	if (!body->text)
		return;
	for (;;)
	{
		va_start(arguments, format);
		length = vsnprintf(body->text + body->length, body->size - body->length, format, arguments);
		va_end(arguments);
		if (length < 0)
			return;
		if ((size_t)length < body->size - body->length)
		{
			body->length += (size_t)length;
			return;
		}
		{
			size_t size = body->size * 2 + (size_t)length;
			char *grown = realloc(body->text, size);

			if (!grown)
			{
				crypto_wipe(body->text, body->size);
				free(body->text);
				body->text = NULL;
				return;
			}
			body->text = grown;
			body->size = size;
		}
	}
}

/* a string, as JSON (with its quotes; null for NULL) */
static void json_string(struct json_body *body, const char *text)
{
	char quoted[1024];

	if (!text)
		json_printf(body, "null");
	else if (control_json_string(text, quoted, sizeof(quoted)) >= 0)
		json_printf(body, "%s", quoted);
	else
		json_printf(body, "\"\"");
}

static void respond_json(struct connection *connection, int status, struct json_body *body, const char *headers)
{
	if (!body->text)
	{
		respond_error(connection, 503, "no memory", NULL);
		return;
	}
	json_printf(body, "\n");
	respond(connection, status, body->text, headers);
	body->text = NULL;
}

static void respond_ok(struct connection *connection, const char *message)
{
	struct json_body body;

	json_begin(&body);
	json_printf(&body, "{\"ok\": true, \"message\": ");
	json_string(&body, message);
	json_printf(&body, "}");
	respond_json(connection, 200, &body, NULL);
}

/* a request's body, a JSON object of string fields (control_parse_fields):
1, else 0 with the response given (415, 400) */
static int body_fields(struct connection *connection, const struct control_request *request, const char *const *names,
	char **outs, const size_t *sizes, int count, unsigned int multiline)
{
	const char *reason;

	if (request->method != CONTROL_METHOD_POST)
	{
		respond_error(connection, 405, "POST only", "Allow: POST\r\n");
		return 0;
	}
	if (!request->json_body)
	{
		respond_error(connection, 415, "the body is JSON: Content-Type: application/json", NULL);
		return 0;
	}
	if (!control_parse_fields(request->body, request->content_length, names, outs, sizes, count, multiline, &reason))
	{
		respond_error(connection, 400, reason, NULL);
		return 0;
	}
	return 1;
}

/* the session's answer: who it is, its CSRF token, its role and powers */
static void session_body(struct json_body *body, const struct control_web_session *session, const struct auth *auth)
{
	json_printf(body, "{\"name\": ");
	json_string(body, session->name);
	json_printf(body, ", \"id\": \"%s\", \"csrf\": \"%s\", \"idle_seconds\": %d, \"kind\": \"%s\", \"role\": \"%s\", "
		"\"permissions\": %u, \"restricted\": %s", session->credential_id, session->csrf, (int)CONTROL_WEB_IDLE_SECONDS,
		session->account ? "account" : "token", control_role_name(auth->role), auth->permissions,
		auth->restricted ? "true" : "false");
	if (auth->account >= 0)
	{
		const struct control_account *account = &panel.accounts[auth->account];

		json_printf(body, ", \"totp\": %s, \"require_2fa\": %s, \"key\": ", account->has_totp ? "true" : "false",
			panel.require_2fa ? "true" : "false");
		if (account->has_key)
		{
			char key[CONTROL_KEY_TEXT + 1];

			control_key_text(account->key, key);
			json_printf(body, "\"%s\"", key);
		}
		else
			json_printf(body, "null");
	}
	json_printf(body, "}");
}

/* a session made for a token's credential or an account, its cookie set,
and its answer given */
static void start_session(struct connection *connection, const struct control_request *request, const char *name,
	const char *id, int account, int64_t now)
{
	uint8_t id_bytes[CONTROL_WEB_SECRET_BYTES];
	uint8_t csrf_bytes[CONTROL_WEB_SECRET_BYTES];
	char id_text[CONTROL_WEB_SECRET_LENGTH + 1];
	char headers[256];
	struct json_body body;
	struct auth auth;
	int session;

	if (!random_bytes(id_bytes, sizeof(id_bytes)) || !random_bytes(csrf_bytes, sizeof(csrf_bytes)))
	{
		respond_error(connection, 503, "no random bytes for a session", NULL);
		return;
	}
	session = control_web_session_create(&control.sessions, id_bytes, csrf_bytes, name, id, now, id_text);
	crypto_wipe(id_bytes, sizeof(id_bytes));
	crypto_wipe(csrf_bytes, sizeof(csrf_bytes));
	control.sessions.entries[session].account = account >= 0;
	control_web_session_cookie(headers, sizeof(headers), id_text, request->https || control.tls);
	crypto_wipe(id_text, sizeof(id_text));
	memset(&auth, 0, sizeof(auth));
	auth.session = session;
	auth.account = account;
	auth.role = account >= 0 ? panel.accounts[account].role : CONTROL_ROLE_OWNER;
	auth.permissions = account >= 0 ? control_role_permissions(auth.role) : CONTROL_PERMISSION_ALL &
		~CONTROL_PERMISSION_CONSOLE;
	auth.restricted = account >= 0 && panel.require_2fa && !panel.accounts[account].has_totp;
	json_begin(&body);
	session_body(&body, &control.sessions.entries[session], &auth);
	respond_json(connection, 200, &body, headers);
	crypto_wipe(headers, sizeof(headers));
}

/* POST /v1/login: {"token": "..."} (a control token's credential), or
{"user": "...", "password": "...", "code": "..."} (an account; code its
second factor's, when it has one): a session, its cookie set */
static void handle_login(struct connection *connection, struct control_request *request, int64_t now)
{
	static const char *const names[] = { "token", "user", "password", "code" };
	char token[CONTROL_MAXIMUM_TOKEN];
	char user[CONTROL_ACCOUNT_NAME_SIZE + 8];
	char password[CONTROL_PASSWORD_MAXIMUM + 2];
	char code[16];
	char *outs[] = { token, user, password, code };
	const size_t sizes[] = { sizeof(token), sizeof(user), sizeof(password), sizeof(code) };
	char address[64];
	const char *reason;
	int64_t retry_after;
	int status;
	int index;

	if (request->method != CONTROL_METHOD_POST)
	{
		respond_error(connection, 405, "POST only", "Allow: POST\r\n");
		return;
	}
	warn_direct(connection, request);
	status = control_web_check_change(NULL, request, &reason);
	if (status)
	{
		respond_error(connection, status, reason, NULL);
		return;
	}
	if (!body_fields(connection, request, names, outs, sizes, 4, 0))
		return;
	address_text(connection->address, address, sizeof(address));
	/* a control token's */
	if (token[0])
	{
		int credential = check_token(connection, token, now, "web login", NULL);

		crypto_wipe(token, sizeof(token));
		crypto_wipe(password, sizeof(password));
		if (credential < 0)
			return;
		notice("web login: %s %s (a control token), from %s", control.credentials[credential].name,
			control.credentials[credential].id, address);
		server_audit("web", control.credentials[credential].name, CONTROL_ROLE_OWNER, "login", NULL, "a control token",
			1, NULL);
		start_session(connection, request, control.credentials[credential].name, control.credentials[credential].id, -1,
			now);
		return;
	}
	/* an account's: the address's limits, then the account's own backoff,
	then its password (Argon2id, within the checks a minute for everyone),
	then its second factor */
	if (!control_limiter_allowed(&control.limiter, connection->tag, now, &retry_after))
	{
		char headers[64];

		crypto_wipe(password, sizeof(password));
		snprintf(headers, sizeof(headers), "Retry-After: %lld\r\n", (long long)retry_after);
		respond_error(connection, 429, "too many wrong logins from this address: try again later", headers);
		return;
	}
	index = account_named(user);
	if (index >= 0 && !control_backoff_allowed(&panel.backoff[index], now, &retry_after))
	{
		char headers[64];

		crypto_wipe(password, sizeof(password));
		snprintf(headers, sizeof(headers), "Retry-After: %lld\r\n", (long long)retry_after);
		respond_error(connection, 429, "too many wrong logins for this account: try again later", headers);
		return;
	}
	if (!control_limiter_take_check(&control.limiter, now))
	{
		crypto_wipe(password, sizeof(password));
		respond_error(connection, 429, "too many logins checked: try again shortly", "Retry-After: 5\r\n");
		return;
	}
	{
		int right;

		if (index >= 0)
			right = control_account_check(&panel.accounts[index], password);
		else
		{
			/* (no such account: the same work, so the time taken does not
			tell) */
			struct control_account decoy;
			static const uint8_t decoy_salt[CONTROL_SALT_BYTES] = { 0 };

			memset(&decoy, 0, sizeof(decoy));
			memcpy(decoy.salt, decoy_salt, sizeof(decoy_salt));
			decoy.kib = CONTROL_ARGON2_KIB;
			decoy.passes = CONTROL_ARGON2_PASSES;
			control_account_check(&decoy, password);
			right = 0;
		}
		crypto_wipe(password, sizeof(password));
		if (right < 0)
		{
			respond_error(connection, 503, "no memory to check the password", NULL);
			return;
		}
		if (!right)
		{
			control_limiter_failed(&control.limiter, connection->tag, now);
			if (index >= 0)
				control_backoff_failed(&panel.backoff[index], now);
			notice("a web login with a wrong name or password, from %s", address);
			server_audit("web", index >= 0 ? panel.accounts[index].name : "", CONTROL_ROLE_NONE, "login", NULL,
				"wrong name or password", 0, NULL);
			respond_error(connection, 401, "the name or password is not right", NULL);
			return;
		}
	}
	if (panel.accounts[index].has_totp)
	{
		if (!code[0])
		{
			struct json_body body;

			json_begin(&body);
			json_printf(&body, "{\"error\": \"the code from your authenticator app is needed\", \"need_code\": true}");
			respond_json(connection, 401, &body, NULL);
			return;
		}
		if (!control_totp_check(panel.accounts[index].totp_secret, code, (int64_t)time(NULL),
			&panel.accounts[index].totp_last_step))
		{
			control_limiter_failed(&control.limiter, connection->tag, now);
			control_backoff_failed(&panel.backoff[index], now);
			notice("a web login with a wrong second factor code: %s, from %s", panel.accounts[index].name, address);
			server_audit("web", panel.accounts[index].name, panel.accounts[index].role, "login", NULL,
				"wrong second factor code", 0, NULL);
			{
				struct json_body body;

				json_begin(&body);
				json_printf(&body, "{\"error\": \"the code is not right (or was used already)\", \"need_code\": true}");
				respond_json(connection, 401, &body, NULL);
			}
			return;
		}
		/* (the step a code was taken at: kept, so it is not taken again) */
		{
			char problem[256];

			if (!write_accounts(problem, sizeof(problem)))
				notice("%s", problem);
		}
	}
	control_backoff_succeeded(&panel.backoff[index]);
	control_limiter_succeeded(&control.limiter, connection->tag);
	notice("web login: %s %s (%s), from %s", panel.accounts[index].name, panel.accounts[index].id,
		control_role_name(panel.accounts[index].role), address);
	server_audit("web", panel.accounts[index].name, panel.accounts[index].role, "login", NULL, NULL, 1, NULL);
	start_session(connection, request, panel.accounts[index].name, panel.accounts[index].id, index, now);
}

/* a code (the setup code's, or an invitation's) checked against the
limits as a login is: 1, else 0 with the response given */
static int code_allowed(struct connection *connection, int64_t now)
{
	int64_t retry_after;

	if (!control_limiter_allowed(&control.limiter, connection->tag, now, &retry_after))
	{
		char headers[64];

		snprintf(headers, sizeof(headers), "Retry-After: %lld\r\n", (long long)retry_after);
		respond_error(connection, 429, "too many wrong codes from this address: try again later", headers);
		return 0;
	}
	return 1;
}

/* POST /v1/setup {"code", "user", "password"}: the first owner's account,
from the setup code the server printed (while there is no owner) */
static void handle_setup(struct connection *connection, struct control_request *request, int64_t now)
{
	static const char *const names[] = { "code", "user", "password" };
	char code[CONTROL_CODE_TEXT + 8];
	char user[CONTROL_ACCOUNT_NAME_SIZE + 8];
	char password[CONTROL_PASSWORD_MAXIMUM + 2];
	char *outs[] = { code, user, password };
	const size_t sizes[] = { sizeof(code), sizeof(user), sizeof(password) };
	char problem[256];
	const char *reason;
	uint8_t hash[32];
	int status = control_web_check_change(NULL, request, &reason);

	if (status)
	{
		respond_error(connection, status, reason, NULL);
		return;
	}
	if (!body_fields(connection, request, names, outs, sizes, 3, 0) || !code_allowed(connection, now))
	{
		crypto_wipe(password, sizeof(password));
		return;
	}
	control_code_hash(code, hash);
	if (owner_count() || !panel.setup_active || panel.setup_expires <= (int64_t)time(NULL) ||
		!control_code_valid("set_", code) || crypto_verify32(hash, panel.setup_hash))
	{
		char address[64];

		crypto_wipe(password, sizeof(password));
		control_limiter_failed(&control.limiter, connection->tag, now);
		address_text(connection->address, address, sizeof(address));
		notice("a wrong or spent setup code, from %s", address);
		respond_error(connection, 403, owner_count() ? "the server has an owner already" :
			"the setup code is not right, or has run out (sv_account_setup on the console makes a new one)", NULL);
		return;
	}
	if (!add_account(user, CONTROL_ROLE_OWNER, password, problem, sizeof(problem)))
	{
		crypto_wipe(password, sizeof(password));
		respond_error(connection, 400, problem, NULL);
		return;
	}
	crypto_wipe(password, sizeof(password));
	panel.setup_active = 0;
	crypto_wipe(panel.setup_hash, sizeof(panel.setup_hash));
	notice("the control panel's owner account was made: %s", user);
	server_audit("web", user, CONTROL_ROLE_OWNER, "setup", user, NULL, 1, NULL);
	respond_ok(connection, "the owner's account is made: log in with it, and set up a second factor");
}

/* POST /v1/invite {"code", "user", "password"}: an account from an
invitation (or a new password for one, from a reset's) */
static void handle_invite(struct connection *connection, struct control_request *request, int64_t now)
{
	static const char *const names[] = { "code", "user", "password" };
	char code[CONTROL_CODE_TEXT + 8];
	char user[CONTROL_ACCOUNT_NAME_SIZE + 8];
	char password[CONTROL_PASSWORD_MAXIMUM + 2];
	char *outs[] = { code, user, password };
	const size_t sizes[] = { sizeof(code), sizeof(user), sizeof(password) };
	char problem[256];
	const char *reason;
	struct control_invite invite;
	int index;
	int status = control_web_check_change(NULL, request, &reason);

	if (status)
	{
		respond_error(connection, status, reason, NULL);
		return;
	}
	if (!body_fields(connection, request, names, outs, sizes, 3, 0) || !code_allowed(connection, now))
	{
		crypto_wipe(password, sizeof(password));
		return;
	}
	index = invite_for_code(code);
	crypto_wipe(code, sizeof(code));
	if (index < 0)
	{
		char address[64];

		crypto_wipe(password, sizeof(password));
		control_limiter_failed(&control.limiter, connection->tag, now);
		address_text(connection->address, address, sizeof(address));
		notice("a wrong or spent invitation code, from %s", address);
		respond_error(connection, 403, "the invitation is not right, was used, or has run out", NULL);
		return;
	}
	invite = panel.invites[index];
	if (invite.account[0])
	{
		/* a reset: the account's password set again, its second factor
		turned off (to be set up again), its sessions ended */
		int account = account_named(invite.account);
		uint8_t salt[CONTROL_SALT_BYTES];
		struct control_account old;

		if (account < 0 || strcmp(user, invite.account))
		{
			crypto_wipe(password, sizeof(password));
			respond_error(connection, 400, account < 0 ? "the account is no longer there" :
				"this code is for another account's password", NULL);
			return;
		}
		if (!control_password_acceptable(password, &reason) || !random_bytes(salt, sizeof(salt)))
		{
			crypto_wipe(password, sizeof(password));
			respond_error(connection, 400, reason, NULL);
			return;
		}
		old = panel.accounts[account];
		if (!control_account_set_password(&panel.accounts[account], password, salt, CONTROL_ARGON2_KIB,
			CONTROL_ARGON2_PASSES))
		{
			crypto_wipe(password, sizeof(password));
			respond_error(connection, 503, "no memory to hash the password", NULL);
			return;
		}
		crypto_wipe(password, sizeof(password));
		panel.accounts[account].has_totp = 0;
		crypto_wipe(panel.accounts[account].totp_secret, sizeof(panel.accounts[account].totp_secret));
		remove_invite(index);
		if (!write_accounts(problem, sizeof(problem)))
		{
			panel.accounts[account] = old;
			panel.invite_count++;
			memmove(&panel.invites[index + 1], &panel.invites[index],
				(size_t)(panel.invite_count - 1 - index) * sizeof(panel.invites[0]));
			panel.invites[index] = invite;
			crypto_wipe(&old, sizeof(old));
			respond_error(connection, 500, problem, NULL);
			return;
		}
		crypto_wipe(&old, sizeof(old));
		end_account_sessions(panel.accounts[account].id);
		control_backoff_succeeded(&panel.backoff[account]);
		notice("an account's password was set again from a reset code: %s", user);
		server_audit("web", user, panel.accounts[account].role, "password_reset", user, NULL, 1, NULL);
		respond_ok(connection, "the password is set: log in with it, and set up a second factor again");
		return;
	}
	remove_invite(index);
	if (!add_account(user, invite.role, password, problem, sizeof(problem)))
	{
		crypto_wipe(password, sizeof(password));
		/* (the invitation is kept for another try) */
		panel.invite_count++;
		memmove(&panel.invites[index + 1], &panel.invites[index],
			(size_t)(panel.invite_count - 1 - index) * sizeof(panel.invites[0]));
		panel.invites[index] = invite;
		respond_error(connection, 400, problem, NULL);
		return;
	}
	crypto_wipe(password, sizeof(password));
	notice("an account was made from an invitation by %s: %s (%s)", invite.made_by, user,
		control_role_name(invite.role));
	server_audit("web", user, invite.role, "invite_accept", user, invite.made_by, 1, NULL);
	respond_ok(connection, "the account is made: log in with it");
}

/* a request with a session cookie: who it is from, and for a POST the
checks a browser's change must pass; 0 with the response given */
static int session_request(struct connection *connection, struct control_request *request, int64_t now,
	struct auth *auth)
{
	int session = control_web_session_find(&control.sessions, request->session, now, request->background);
	const char *reason;
	char cookie[160];
	int status;

	crypto_wipe(request->session, sizeof(request->session));
	memset(auth, 0, sizeof(*auth));
	auth->session = -1;
	auth->account = -1;
	if (session >= 0)
	{
		const struct control_web_session *entry = &control.sessions.entries[session];

		/* (its credential, or account, taken out since) */
		if (entry->account)
			auth->account = account_with_id(entry->credential_id);
		if (entry->account ? auth->account < 0 : credential_by_id(entry->credential_id) < 0)
		{
			control_web_session_end(&control.sessions, session);
			session = -1;
		}
	}
	if (session < 0)
	{
		control_web_session_cookie(cookie, sizeof(cookie), NULL, request->https || control.tls);
		respond_error(connection, 401, "not logged in, or the session ended: log in again", cookie);
		return 0;
	}
	if (request->method == CONTROL_METHOD_POST)
	{
		status = control_web_check_change(&control.sessions.entries[session], request, &reason);
		if (status)
		{
			char address[64];

			address_text(connection->address, address, sizeof(address));
			notice("a web request refused (%s), from %s", reason, address);
			respond_error(connection, status, reason, NULL);
			return 0;
		}
	}
	auth->session = session;
	snprintf(auth->name, sizeof(auth->name), "%s", control.sessions.entries[session].name);
	snprintf(auth->source, sizeof(auth->source), "web %s %s", control.sessions.entries[session].name,
		control.sessions.entries[session].credential_id);
	if (auth->account >= 0)
	{
		auth->role = panel.accounts[auth->account].role;
		auth->permissions = control_role_permissions(auth->role);
		auth->restricted = panel.require_2fa && !panel.accounts[auth->account].has_totp;
	}
	else
	{
		auth->role = CONTROL_ROLE_OWNER;
		auth->permissions = CONTROL_PERMISSION_ALL & ~CONTROL_PERMISSION_CONSOLE;
	}
	return 1;
}

/* the permission a request needs, refused (403) if its auth lacks it: 1 if
it has it */
static int require(struct connection *connection, const struct auth *auth, unsigned int permission)
{
	if (auth->restricted)
	{
		respond_error(connection, 403, "set up a second factor first: the owner requires one", NULL);
		return 0;
	}
	if (!(auth->permissions & permission))
	{
		respond_error(connection, 403, "your role does not allow this", NULL);
		return 0;
	}
	return 1;
}

/* a command queued for the main thread as the request's, its answer to
come (answer: ANSWER_JSON, a read's JSON as it is; ANSWER_COMMAND, {"ok",
"output"}) */
static void queue_for(struct connection *connection, const struct auth *auth, const char *line, int flags, int answer,
	const char *payload, int64_t now)
{
	int ticket = queue_command(line, auth->source, flags, 0, auth->role, auth->permissions, payload);

	if (ticket < 0)
	{
		respond_error(connection, 503, "too many commands waiting: try again shortly", "Retry-After: 1\r\n");
		return;
	}
	connection->answer = answer;
	connection->ticket = ticket;
	connection->state = CONNECTION_WAITING;
	connection->deadline = now + COMMAND_SECONDS;
}

/* GET /v1/audit: the audit file's last lines, as they are (JSON objects) */
static void respond_audit(struct connection *connection)
{
	char path[1024];
	struct json_body body;
	FILE *file;
	long size;
	char *text = NULL;
	long start;
	int count = 0;

	snprintf(path, sizeof(path), "%s/%s", platform_data_root() && platform_data_root()[0] ? platform_data_root() : ".",
		AUDIT_FILE);
	json_begin(&body);
	json_printf(&body, "{\"lines\": [");
	file = fopen(path, "r");
	if (file && !fseek(file, 0, SEEK_END) && (size = ftell(file)) > 0)
	{
		/* (the last 64 KB at most: enough for AUDIT_LINES lines) */
		start = size > 64 * 1024 ? size - 64 * 1024 : 0;
		text = malloc((size_t)(size - start) + 1);
		if (text && !fseek(file, start, SEEK_SET))
		{
			size_t got = fread(text, 1, (size_t)(size - start), file);
			char *line_start = text;
			char *lines[AUDIT_LINES];
			int kept = 0;
			int first = 0;
			int index;

			text[got] = 0;
			/* (a cut first line left out) */
			if (start > 0)
			{
				char *end = strchr(text, '\n');

				line_start = end ? end + 1 : text + got;
			}
			while (*line_start)
			{
				char *end = strchr(line_start, '\n');

				if (!end)
					break;
				*end = 0;
				if (line_start[0] == '{')
				{
					lines[kept % AUDIT_LINES] = line_start;
					kept++;
				}
				line_start = end + 1;
			}
			first = kept > AUDIT_LINES ? kept - AUDIT_LINES : 0;
			for (index = first; index < kept; index++)
				json_printf(&body, "%s%s", count++ ? ", " : "", lines[index % AUDIT_LINES]);
		}
	}
	if (file)
		fclose(file);
	free(text);
	json_printf(&body, "], \"count\": %d}", count);
	respond_json(connection, 200, &body, NULL);
}

/* the accounts, for the panel's owners */
static void respond_accounts(struct connection *connection)
{
	struct json_body body;
	int64_t now = (int64_t)time(NULL);
	int index;

	forget_expired_invites();
	json_begin(&body);
	json_printf(&body, "{\"accounts\": [");
	for (index = 0; index < panel.count; index++)
	{
		const struct control_account *account = &panel.accounts[index];
		char key[CONTROL_KEY_TEXT + 1];

		json_printf(&body, "%s{\"name\": ", index ? ", " : "");
		json_string(&body, account->name);
		json_printf(&body, ", \"id\": \"%s\", \"role\": \"%s\", \"totp\": %s, \"key\": ", account->id,
			control_role_name(account->role), account->has_totp ? "true" : "false");
		if (account->has_key)
		{
			control_key_text(account->key, key);
			json_printf(&body, "\"%s\"", key);
		}
		else
			json_printf(&body, "null");
		json_printf(&body, ", \"locked\": %s, \"created\": %lld}", panel.backoff[index].locked_until >
			monotonic_seconds() ? "true" : "false", (long long)account->created);
	}
	json_printf(&body, "], \"invites\": [");
	for (index = 0; index < panel.invite_count; index++)
	{
		json_printf(&body, "%s{\"id\": \"%s\", \"role\": \"%s\", \"account\": ", index ? ", " : "",
			panel.invites[index].id, control_role_name(panel.invites[index].role));
		json_string(&body, panel.invites[index].account[0] ? panel.invites[index].account : NULL);
		json_printf(&body, ", \"made_by\": ");
		json_string(&body, panel.invites[index].made_by);
		json_printf(&body, ", \"seconds_left\": %lld}", (long long)(panel.invites[index].expires - now));
	}
	json_printf(&body, "], \"require_2fa\": %s}", panel.require_2fa ? "true" : "false");
	respond_json(connection, 200, &body, NULL);
}

/* a QR code of text, as rows of 0s and 1s (for the page to draw) */
static void json_qr(struct json_body *body, const char *text)
{
	uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(15)];
	uint8_t temporary[qrcodegen_BUFFER_LEN_FOR_VERSION(15)];
	int size;
	int x, y;

	if (!qrcodegen_encodeText(text, temporary, qr, qrcodegen_Ecc_MEDIUM, 1, 15, qrcodegen_Mask_AUTO, true))
	{
		json_printf(body, "null");
		return;
	}
	size = qrcodegen_getSize(qr);
	json_printf(body, "[");
	for (y = 0; y < size; y++)
	{
		char row[4 * 15 + 17 + 1];

		for (x = 0; x < size && x < (int)sizeof(row) - 1; x++)
			row[x] = qrcodegen_getModule(qr, x, y) ? '1' : '0';
		row[x] = 0;
		json_printf(body, "%s\"%s\"", y ? ", " : "", row);
	}
	json_printf(body, "]");
	crypto_wipe(qr, sizeof(qr));
	crypto_wipe(temporary, sizeof(temporary));
}

/* /v1/account/...: a person's own account (an account's session only) */
static void handle_account(struct connection *connection, struct control_request *request, struct auth *auth,
	int64_t now)
{
	const char *path = request->path + strlen("/v1/account");
	struct control_web_session *session = &control.sessions.entries[auth->session];
	struct control_account *account;
	char problem[256];

	if (auth->account < 0)
	{
		respond_error(connection, 400, "an account's only (a control token has none)", NULL);
		return;
	}
	account = &panel.accounts[auth->account];
	if (!strcmp(path, "/totp/begin"))
	{
		char uri[512];
		struct json_body body;

		if (request->method != CONTROL_METHOD_POST)
		{
			respond_error(connection, 405, "POST only", "Allow: POST\r\n");
			return;
		}
		if (!random_bytes(session->totp_secret, sizeof(session->totp_secret)))
		{
			respond_error(connection, 503, "no random bytes", NULL);
			return;
		}
		session->totp_pending = 1;
		if (control_totp_uri(session->totp_secret, "ChupathingyCE Server", account->name, uri, sizeof(uri)) < 0)
		{
			respond_error(connection, 500, "the link does not fit", NULL);
			return;
		}
		json_begin(&body);
		{
			char secret[CONTROL_TOTP_SECRET_TEXT + 1];

			control_base32(session->totp_secret, sizeof(session->totp_secret), secret, sizeof(secret));
			json_printf(&body, "{\"secret\": \"%s\", \"uri\": ", secret);
			crypto_wipe(secret, sizeof(secret));
		}
		json_string(&body, uri);
		json_printf(&body, ", \"qr\": ");
		json_qr(&body, uri);
		json_printf(&body, "}");
		crypto_wipe(uri, sizeof(uri));
		respond_json(connection, 200, &body, NULL);
		return;
	}
	if (!strcmp(path, "/totp/enable"))
	{
		static const char *const names[] = { "code" };
		char code[16];
		char *outs[] = { code };
		const size_t sizes[] = { sizeof(code) };
		int64_t step = 0;

		if (!body_fields(connection, request, names, outs, sizes, 1, 0))
			return;
		if (!session->totp_pending || !control_totp_check(session->totp_secret, code, (int64_t)time(NULL), &step))
		{
			respond_error(connection, 400, "the code is not right: check the app's time, or begin again", NULL);
			return;
		}
		memcpy(account->totp_secret, session->totp_secret, sizeof(account->totp_secret));
		account->totp_last_step = step;
		account->has_totp = 1;
		if (!write_accounts(problem, sizeof(problem)))
		{
			account->has_totp = 0;
			respond_error(connection, 500, problem, NULL);
			return;
		}
		session->totp_pending = 0;
		crypto_wipe(session->totp_secret, sizeof(session->totp_secret));
		server_audit("web", account->name, account->role, "totp_enable", account->name, NULL, 1, NULL);
		respond_ok(connection, "your second factor is on: logins ask for a code from now");
		return;
	}
	if (!strcmp(path, "/totp/disable"))
	{
		static const char *const names[] = { "password", "code" };
		char password[CONTROL_PASSWORD_MAXIMUM + 2];
		char code[16];
		char *outs[] = { password, code };
		const size_t sizes[] = { sizeof(password), sizeof(code) };
		int right;

		if (!body_fields(connection, request, names, outs, sizes, 2, 0))
			return;
		if (panel.require_2fa)
		{
			crypto_wipe(password, sizeof(password));
			respond_error(connection, 403, "the owner requires a second factor", NULL);
			return;
		}
		if (!control_limiter_take_check(&control.limiter, now))
		{
			crypto_wipe(password, sizeof(password));
			respond_error(connection, 429, "too many checks: try again shortly", "Retry-After: 5\r\n");
			return;
		}
		right = control_account_check(account, password) > 0 && account->has_totp &&
			control_totp_check(account->totp_secret, code, (int64_t)time(NULL), &account->totp_last_step);
		crypto_wipe(password, sizeof(password));
		if (!right)
		{
			control_backoff_failed(&panel.backoff[auth->account], now);
			respond_error(connection, 403, "the password or code is not right", NULL);
			return;
		}
		account->has_totp = 0;
		crypto_wipe(account->totp_secret, sizeof(account->totp_secret));
		if (!write_accounts(problem, sizeof(problem)))
		{
			respond_error(connection, 500, problem, NULL);
			return;
		}
		server_audit("web", account->name, account->role, "totp_disable", account->name, NULL, 1, NULL);
		respond_ok(connection, "your second factor is off");
		return;
	}
	if (!strcmp(path, "/password"))
	{
		static const char *const names[] = { "password", "new_password" };
		char password[CONTROL_PASSWORD_MAXIMUM + 2];
		char fresh[CONTROL_PASSWORD_MAXIMUM + 2];
		char *outs[] = { password, fresh };
		const size_t sizes[] = { sizeof(password), sizeof(fresh) };
		uint8_t salt[CONTROL_SALT_BYTES];
		const char *reason;
		struct control_account old = *account;
		int right;

		if (!body_fields(connection, request, names, outs, sizes, 2, 0))
			return;
		if (!control_password_acceptable(fresh, &reason))
		{
			crypto_wipe(password, sizeof(password));
			crypto_wipe(fresh, sizeof(fresh));
			respond_error(connection, 400, reason, NULL);
			return;
		}
		if (!control_limiter_take_check(&control.limiter, now))
		{
			crypto_wipe(password, sizeof(password));
			crypto_wipe(fresh, sizeof(fresh));
			respond_error(connection, 429, "too many checks: try again shortly", "Retry-After: 5\r\n");
			return;
		}
		right = control_account_check(account, password);
		crypto_wipe(password, sizeof(password));
		if (right <= 0 || !random_bytes(salt, sizeof(salt)) || !control_account_set_password(account, fresh, salt,
			CONTROL_ARGON2_KIB, CONTROL_ARGON2_PASSES))
		{
			crypto_wipe(fresh, sizeof(fresh));
			if (!right)
				control_backoff_failed(&panel.backoff[auth->account], now);
			respond_error(connection, right ? 503 : 403, right ? "no memory" : "the password is not right", NULL);
			return;
		}
		crypto_wipe(fresh, sizeof(fresh));
		if (!write_accounts(problem, sizeof(problem)))
		{
			*account = old;
			crypto_wipe(&old, sizeof(old));
			respond_error(connection, 500, problem, NULL);
			return;
		}
		crypto_wipe(&old, sizeof(old));
		/* (every other session of the account ended) */
		{
			int index;

			for (index = 0; index < CONTROL_WEB_SESSIONS; index++)
			{
				if (index != auth->session && control.sessions.entries[index].used &&
					control.sessions.entries[index].account &&
					!strcmp(control.sessions.entries[index].credential_id, account->id))
				{
					control_web_session_end(&control.sessions, index);
				}
			}
		}
		server_audit("web", account->name, account->role, "password_change", account->name, NULL, 1, NULL);
		respond_ok(connection, "your password is changed; your other sessions have ended");
		return;
	}
	if (auth->restricted)
	{
		respond_error(connection, 403, "set up a second factor first: the owner requires one", NULL);
		return;
	}
	if (!strcmp(path, "/bind"))
	{
		static const char *const names[] = { "player", "request" };
		char player[16];
		char request_text[16];
		char *outs[] = { player, request_text };
		const size_t sizes[] = { sizeof(player), sizeof(request_text) };
		struct json_body body;

		if (request->method == CONTROL_METHOD_GET)
		{
			/* (what became of the account's request) */
			unsigned char key[CONTROL_KEY_BYTES];
			int state = panel.bind_request[auth->account] ?
				server_roles_bind_state(panel.bind_request[auth->account], NULL, 0, key) : -1;
			static const char *const state_names[] = { "pending", "sent", "accepted", "declined", "no_player",
				"not_delta", "expired" };

			if (state == SERVER_BIND_ACCEPTED)
			{
				struct control_account old = *account;

				memcpy(account->key, key, CONTROL_KEY_BYTES);
				account->has_key = 1;
				panel.bind_request[auth->account] = 0;
				if (!write_accounts(problem, sizeof(problem)))
				{
					*account = old;
					respond_error(connection, 500, problem, NULL);
					return;
				}
				server_audit("web", account->name, account->role, "bind", account->name, NULL, 1, NULL);
				server_roles_moderation_changed();
			}
			else if (state > SERVER_BIND_SENT)
				panel.bind_request[auth->account] = 0;
			json_begin(&body);
			json_printf(&body, "{\"state\": \"%s\"}", state < 0 ? "none" : state_names[state]);
			respond_json(connection, 200, &body, NULL);
			return;
		}
		if (!body_fields(connection, request, names, outs, sizes, 2, 0))
			return;
		{
			char *end;
			long number = strtol(player, &end, 10);
			unsigned int id;

			if (!player[0] || *end || number < 1 || number > 255)
			{
				respond_error(connection, 400, "player is a number from the player list", NULL);
				return;
			}
			id = server_roles_bind_request(account->name, (int)number);
			if (!id)
			{
				respond_error(connection, 503, "too many requests waiting: try again in a minute", NULL);
				return;
			}
			panel.bind_request[auth->account] = id;
			server_audit("web", account->name, account->role, "bind_request", player, NULL, 1, NULL);
			json_begin(&body);
			json_printf(&body, "{\"ok\": true, \"state\": \"pending\"}");
			respond_json(connection, 200, &body, NULL);
		}
		return;
	}
	if (!strcmp(path, "/unbind"))
	{
		if (request->method != CONTROL_METHOD_POST)
		{
			respond_error(connection, 405, "POST only", "Allow: POST\r\n");
			return;
		}
		account->has_key = 0;
		crypto_wipe(account->key, sizeof(account->key));
		if (!write_accounts(problem, sizeof(problem)))
		{
			respond_error(connection, 500, problem, NULL);
			return;
		}
		server_audit("web", account->name, account->role, "unbind", account->name, NULL, 1, NULL);
		server_roles_moderation_changed();
		respond_ok(connection, "your game is no longer bound to your account");
		return;
	}
	respond_error(connection, 404, "no such endpoint", NULL);
}

/* /v1/accounts/...: the accounts and invitations, for owners (and
admins' invitations for moderators) */
static void handle_accounts(struct connection *connection, struct control_request *request, struct auth *auth)
{
	const char *path = request->path + strlen("/v1/accounts");
	static const char *const names[] = { "user", "role", "id", "value" };
	char user[CONTROL_ACCOUNT_NAME_SIZE + 8];
	char role_text[16];
	char id[CONTROL_ID_LENGTH + 4];
	char value[16];
	char *outs[] = { user, role_text, id, value };
	const size_t sizes[] = { sizeof(user), sizeof(role_text), sizeof(id), sizeof(value) };
	char problem[256];
	int index;
	int role;

	if (!path[0])
	{
		if (request->method != CONTROL_METHOD_GET)
		{
			respond_error(connection, 405, "GET only", "Allow: GET\r\n");
			return;
		}
		if (require(connection, auth, CONTROL_PERMISSION_INVITE))
			respond_accounts(connection);
		return;
	}
	if (!body_fields(connection, request, names, outs, sizes, 4, 0))
		return;
	role = role_text[0] ? control_role_parse(role_text) : -1;
	if (!strcmp(path, "/invite"))
	{
		char code[CONTROL_CODE_TEXT + 1];
		struct json_body body;

		if (!require(connection, auth, CONTROL_PERMISSION_INVITE))
			return;
		if (role < 0)
		{
			respond_error(connection, 400, "role is moderator, admin or owner", NULL);
			return;
		}
		/* (an admin invites moderators; an owner, anyone) */
		if (!(auth->permissions & CONTROL_PERMISSION_ROLES) && role >= auth->role)
		{
			respond_error(connection, 403, "you may invite only roles below yours", NULL);
			return;
		}
		if (!make_invite(role, NULL, auth->name, code, sizeof(code), problem, sizeof(problem)))
		{
			respond_error(connection, 500, problem, NULL);
			return;
		}
		server_audit("web", auth->name, auth->role, "invite", control_role_name(role), NULL, 1, NULL);
		json_begin(&body);
		json_printf(&body, "{\"ok\": true, \"code\": \"%s\", \"seconds\": %d}", code, (int)CONTROL_INVITE_SECONDS);
		crypto_wipe(code, sizeof(code));
		respond_json(connection, 200, &body, NULL);
		return;
	}
	if (!strcmp(path, "/revoke"))
	{
		if (!require(connection, auth, CONTROL_PERMISSION_INVITE))
			return;
		for (index = 0; index < panel.invite_count && strcmp(panel.invites[index].id, id); index++)
			;
		if (index >= panel.invite_count)
		{
			respond_error(connection, 404, "no such invitation", NULL);
			return;
		}
		if (!(auth->permissions & CONTROL_PERMISSION_ROLES) && strcmp(panel.invites[index].made_by, auth->name))
		{
			respond_error(connection, 403, "you may revoke only your own invitations", NULL);
			return;
		}
		{
			struct control_invite kept = panel.invites[index];

			remove_invite(index);
			if (!write_accounts(problem, sizeof(problem)))
			{
				panel.invites[panel.invite_count++] = kept;
				respond_error(connection, 500, problem, NULL);
				return;
			}
		}
		server_audit("web", auth->name, auth->role, "invite_revoke", id, NULL, 1, NULL);
		respond_ok(connection, "the invitation is revoked");
		return;
	}
	/* (the rest are the owners') */
	if (!require(connection, auth, CONTROL_PERMISSION_ROLES))
		return;
	if (!strcmp(path, "/require_2fa"))
	{
		int old = panel.require_2fa;

		if (strcmp(value, "true") && strcmp(value, "false"))
		{
			respond_error(connection, 400, "value is true or false", NULL);
			return;
		}
		panel.require_2fa = !strcmp(value, "true");
		if (!write_accounts(problem, sizeof(problem)))
		{
			panel.require_2fa = old;
			respond_error(connection, 500, problem, NULL);
			return;
		}
		server_audit("web", auth->name, auth->role, "require_2fa", value, NULL, 1, NULL);
		respond_ok(connection, panel.require_2fa ? "every account needs a second factor now" :
			"a second factor is each account's choice now");
		return;
	}
	index = account_named(user);
	if (index < 0)
	{
		respond_error(connection, 404, "no such account", NULL);
		return;
	}
	if (!strcmp(path, "/role"))
	{
		int old = panel.accounts[index].role;

		if (role < 0)
		{
			respond_error(connection, 400, "role is moderator, admin or owner", NULL);
			return;
		}
		if (old == CONTROL_ROLE_OWNER && role != CONTROL_ROLE_OWNER && owner_count() == 1)
		{
			respond_error(connection, 409, "the only owner keeps the role: make another owner first", NULL);
			return;
		}
		panel.accounts[index].role = role;
		if (!write_accounts(problem, sizeof(problem)))
		{
			panel.accounts[index].role = old;
			respond_error(connection, 500, problem, NULL);
			return;
		}
		server_audit("web", auth->name, auth->role, "account_role", user, control_role_name(role), 1, NULL);
		server_roles_moderation_changed();
		respond_ok(connection, "the role is changed");
		return;
	}
	if (!strcmp(path, "/remove"))
	{
		char removed_id[CONTROL_ID_LENGTH + 1];

		if (panel.accounts[index].role == CONTROL_ROLE_OWNER && owner_count() == 1)
		{
			respond_error(connection, 409, "the only owner cannot be taken out: make another owner first", NULL);
			return;
		}
		snprintf(removed_id, sizeof(removed_id), "%s", panel.accounts[index].id);
		{
			struct control_account kept = panel.accounts[index];
			struct control_account_backoff backoff = panel.backoff[index];

			remove_account_at(index);
			if (!write_accounts(problem, sizeof(problem)))
			{
				memmove(&panel.accounts[index + 1], &panel.accounts[index],
					(size_t)(panel.count - index) * sizeof(panel.accounts[0]));
				memmove(&panel.backoff[index + 1], &panel.backoff[index],
					(size_t)(panel.count - index) * sizeof(panel.backoff[0]));
				panel.accounts[index] = kept;
				panel.backoff[index] = backoff;
				panel.count++;
				crypto_wipe(&kept, sizeof(kept));
				respond_error(connection, 500, problem, NULL);
				return;
			}
			crypto_wipe(&kept, sizeof(kept));
		}
		end_account_sessions(removed_id);
		server_audit("web", auth->name, auth->role, "account_remove", user, NULL, 1, NULL);
		server_roles_moderation_changed();
		respond_ok(connection, "the account is taken out, and its sessions ended");
		return;
	}
	if (!strcmp(path, "/reset"))
	{
		char code[CONTROL_CODE_TEXT + 1];
		struct json_body body;

		if (!make_invite(panel.accounts[index].role, user, auth->name, code, sizeof(code), problem, sizeof(problem)))
		{
			respond_error(connection, 500, problem, NULL);
			return;
		}
		server_audit("web", auth->name, auth->role, "account_reset", user, NULL, 1, NULL);
		json_begin(&body);
		json_printf(&body, "{\"ok\": true, \"code\": \"%s\", \"seconds\": %d}", code, (int)CONTROL_INVITE_SECONDS);
		crypto_wipe(code, sizeof(code));
		respond_json(connection, 200, &body, NULL);
		return;
	}
	respond_error(connection, 404, "no such endpoint", NULL);
}

/* GET /v1/hello: what the page needs before a login, to anyone: whether
the server has an owner yet, and its certificate's fingerprint (to check
against the one the server printed) */
static void respond_hello(struct connection *connection)
{
	struct json_body body;

	json_begin(&body);
	json_printf(&body, "{\"setup\": %s, \"tls\": %s, \"fingerprint\": ", owner_count() ? "false" : "true",
		control.tls ? "true" : "false");
	json_string(&body, control.tls ? control.fingerprint : NULL);
	json_printf(&body, "}");
	respond_json(connection, 200, &body, NULL);
}

static void handle_request(struct connection *connection, struct control_request *request)
{
	int64_t now = monotonic_seconds();
	struct auth auth;
	char command[CONTROL_MAXIMUM_COMMAND];

	/* the web page's own files, to anyone: they are the program's, and hold
	nothing of the server's */
	if (strncmp(request->path, "/v1/", 4))
	{
		const struct control_web_asset *asset = control_web_find_asset(request->path);

		crypto_wipe(request->token, sizeof(request->token));
		if (request->method != CONTROL_METHOD_GET)
		{
			respond_error(connection, asset ? 405 : 404, asset ? "GET only" : "no such page",
				asset ? "Allow: GET\r\n" : NULL);
			return;
		}
		if (!asset)
		{
			respond_error(connection, 404, "no such page", NULL);
			return;
		}
		warn_direct(connection, request);
		respond_bytes(connection, 200, asset->type, asset->data, asset->size, NULL);
		return;
	}
	if (!strcmp(request->path, "/v1/login"))
	{
		crypto_wipe(request->token, sizeof(request->token));
		handle_login(connection, request, now);
		return;
	}
	if (!strcmp(request->path, "/v1/hello") || !strcmp(request->path, "/v1/setup") ||
		!strcmp(request->path, "/v1/invite"))
	{
		crypto_wipe(request->token, sizeof(request->token));
		if (!strcmp(request->path, "/v1/hello"))
		{
			if (request->method != CONTROL_METHOD_GET)
				respond_error(connection, 405, "GET only", "Allow: GET\r\n");
			else
				respond_hello(connection);
		}
		else if (!strcmp(request->path, "/v1/setup"))
			handle_setup(connection, request, now);
		else
			handle_invite(connection, request, now);
		return;
	}
	/* a browser's session (a request with no Authorization, and its cookie),
	else a bearer token */
	if (request->authorization == CONTROL_AUTHORIZATION_NONE && request->session[0])
	{
		if (!session_request(connection, request, now, &auth))
			return;
	}
	else
	{
		int credential;

		if (request->authorization != CONTROL_AUTHORIZATION_BEARER)
		{
			int64_t retry_after;

			if (!control_limiter_allowed(&control.limiter, connection->tag, now, &retry_after))
			{
				char headers[64];

				snprintf(headers, sizeof(headers), "Retry-After: %lld\r\n", (long long)retry_after);
				respond_error(connection, 429, "too many wrong tokens from this address: try again later", headers);
				return;
			}
			if (request->authorization != CONTROL_AUTHORIZATION_NONE)
			{
				char address[64];

				control_limiter_failed(&control.limiter, connection->tag, now);
				address_text(connection->address, address, sizeof(address));
				notice("a control API request with a malformed token, from %s", address);
			}
			crypto_wipe(request->token, sizeof(request->token));
			respond_error(connection, 401, "a token is needed: Authorization: Bearer <token>",
				"WWW-Authenticate: Bearer realm=\"chupathingyce-server\"\r\n");
			return;
		}
		credential = check_token(connection, request->token, now, "control API request",
			"WWW-Authenticate: Bearer realm=\"chupathingyce-server\", error=\"invalid_token\"\r\n");
		crypto_wipe(request->token, sizeof(request->token));
		if (credential < 0)
			return;
		memset(&auth, 0, sizeof(auth));
		auth.session = -1;
		auth.account = -1;
		auth.role = CONTROL_ROLE_OWNER;
		auth.permissions = CONTROL_PERMISSION_ALL & ~CONTROL_PERMISSION_CONSOLE;
		snprintf(auth.name, sizeof(auth.name), "%s", control.credentials[credential].name);
		snprintf(auth.source, sizeof(auth.source), "api %s %s", control.credentials[credential].name,
			control.credentials[credential].id);
	}

	if (!strcmp(request->path, "/v1/session") || !strcmp(request->path, "/v1/logout"))
	{
		int logout = !strcmp(request->path, "/v1/logout");
		struct json_body body;

		if (auth.session < 0)
		{
			respond_error(connection, 400, "a web session's only (log in at /)", NULL);
			return;
		}
		if (request->method != (logout ? CONTROL_METHOD_POST : CONTROL_METHOD_GET))
		{
			respond_error(connection, 405, logout ? "POST only" : "GET only", logout ? "Allow: POST\r\n" :
				"Allow: GET\r\n");
			return;
		}
		if (logout)
		{
			char cookie[160];

			notice("web logout: %s", auth.source + 4);
			control_web_session_end(&control.sessions, auth.session);
			control_web_session_cookie(cookie, sizeof(cookie), NULL, request->https || control.tls);
			json_begin(&body);
			json_printf(&body, "{\"ok\": true}");
			respond_json(connection, 200, &body, cookie);
			return;
		}
		json_begin(&body);
		session_body(&body, &control.sessions.entries[auth.session], &auth);
		respond_json(connection, 200, &body, NULL);
		return;
	}
	if (!strncmp(request->path, "/v1/account/", 12))
	{
		if (auth.session < 0)
			respond_error(connection, 400, "a web session's only (log in at /)", NULL);
		else
			handle_account(connection, request, &auth, now);
		return;
	}
	if (!strcmp(request->path, "/v1/accounts") || !strncmp(request->path, "/v1/accounts/", 13))
	{
		handle_accounts(connection, request, &auth);
		return;
	}
	if (request->query[0] && strcmp(request->path, "/v1/log"))
	{
		respond_error(connection, 400, "no query is taken here", NULL);
		return;
	}
	if (!strcmp(request->path, "/v1/log") || !strcmp(request->path, "/v1/audit"))
	{
		uint64_t since;

		if (request->method != CONTROL_METHOD_GET)
		{
			respond_error(connection, 405, "GET only", "Allow: GET\r\n");
			return;
		}
		if (!require(connection, &auth, CONTROL_PERMISSION_VIEW))
			return;
		if (!strcmp(request->path, "/v1/audit"))
		{
			respond_audit(connection);
			return;
		}
		if (!control_parse_log_query(request->query, &since))
		{
			respond_error(connection, 400, "the query is since=<number>, or none", NULL);
			return;
		}
		respond_log(connection, since);
		return;
	}
	{
		/* the reads, as JSON: each the command it is */
		static const struct
		{
			const char *path;
			const char *line;
			unsigned int permission;
		} reads[] =
		{
			{ "/v1/status", "sv_status", CONTROL_PERMISSION_VIEW },
			{ "/v1/players", "sv_players", CONTROL_PERMISSION_VIEW },
			{ "/v1/bans", "sv_banlist", CONTROL_PERMISSION_VIEW },
			{ "/v1/mapcycle", "sv_mapcycle", CONTROL_PERMISSION_VIEW },
			{ "/v1/maps", "sv_maps", CONTROL_PERMISSION_VIEW },
			{ "/v1/playlists", "sv_playlists", CONTROL_PERMISSION_VIEW },
			{ "/v1/gametypes", "sv_gametypes", CONTROL_PERMISSION_VIEW },
			{ "/v1/settings", "sv_settings", CONTROL_PERMISSION_VIEW },
			{ "/v1/moderators", "sv_mod_list", CONTROL_PERMISSION_ROLES },
			{ "/v1/link", "sv_link_status", CONTROL_PERMISSION_VIEW },
		};
		size_t index;

		for (index = 0; index < sizeof(reads) / sizeof(reads[0]); index++)
		{
			if (strcmp(request->path, reads[index].path))
				continue;
			if (request->method != CONTROL_METHOD_GET)
			{
				respond_error(connection, 405, "GET only", "Allow: GET\r\n");
				return;
			}
			if (require(connection, &auth, reads[index].permission))
				queue_for(connection, &auth, reads[index].line, CONTROL_JSON | CONTROL_QUIET, ANSWER_JSON, NULL, now);
			return;
		}
	}
	if (!strcmp(request->path, "/v1/command") || !strcmp(request->path, "/v1/query"))
	{
		/* a command (its output as text), or a read (sv_playlist <name>,
		sv_gametype <name>: its JSON) */
		int query = !strcmp(request->path, "/v1/query");
		const char *reason;
		unsigned int needed;
		int changes = 0;

		if (request->method != CONTROL_METHOD_POST)
		{
			respond_error(connection, 405, "POST only", "Allow: POST\r\n");
			return;
		}
		if (!request->json_body)
		{
			respond_error(connection, 415, "the body is JSON: Content-Type: application/json", NULL);
			return;
		}
		if (!control_parse_command_body(request->body, request->content_length, command, sizeof(command), &reason))
		{
			respond_error(connection, 400, reason, NULL);
			return;
		}
		/* (the permission checked here, and again by the main thread, which
		has the last word) */
		needed = control_command_permission(command, &changes);
		if (needed && !require(connection, &auth, needed))
			return;
		if (query && (changes || !needed))
		{
			respond_error(connection, 400, "a query is a read: sv_playlist, sv_gametype, ...", NULL);
			return;
		}
		queue_for(connection, &auth, command, query ? CONTROL_JSON | CONTROL_QUIET : 0, query ? ANSWER_JSON :
			ANSWER_COMMAND, NULL, now);
		return;
	}
	if (!strcmp(request->path, "/v1/file"))
	{
		/* a playlist's or game type's whole file, saved */
		static const char *const names[] = { "kind", "name", "text" };
		char kind[16];
		char name[40];
		char *text = malloc(CONTROL_MAXIMUM_BODY);
		char *outs[3];
		size_t sizes[3];
		int playlist;

		if (!text)
		{
			respond_error(connection, 503, "no memory", NULL);
			return;
		}
		outs[0] = kind;
		outs[1] = name;
		outs[2] = text;
		sizes[0] = sizeof(kind);
		sizes[1] = sizeof(name);
		sizes[2] = CONTROL_MAXIMUM_BODY;
		if (!body_fields(connection, request, names, outs, sizes, 3, 1u << 2))
		{
			free(text);
			return;
		}
		playlist = !strcmp(kind, "playlist");
		if (!playlist && strcmp(kind, "gametype"))
		{
			free(text);
			respond_error(connection, 400, "kind is playlist or gametype", NULL);
			return;
		}
		snprintf(command, sizeof(command), "%s %s", playlist ? "sv_playlist_save" : "sv_gametype_save", name);
		if (strchr(name, ' ') || strchr(name, '"') || !name[0] ||
			!require(connection, &auth, playlist ? CONTROL_PERMISSION_MAP : CONTROL_PERMISSION_SETTINGS))
		{
			if (connection->state != CONNECTION_WRITING)
				respond_error(connection, 400, "name is a playlist's or game type's name", NULL);
			free(text);
			return;
		}
		queue_for(connection, &auth, command, 0, ANSWER_COMMAND, text, now);
		free(text);
		return;
	}
	respond_error(connection, 404, "no such endpoint", NULL);
}

static void read_request(struct connection *connection)
{
	struct control_request request;
	const char *reason;
	int status;
	int result;
	ssize_t count;

	if (connection->tls)
	{
		long got = control_tls_read(connection->tls, connection->request + connection->length,
			sizeof(connection->request) - 1 - connection->length);

		if (got == CONTROL_TLS_WANT_READ || got == CONTROL_TLS_WANT_WRITE)
			return;
		if (got <= 0)
		{
			close_connection(connection);
			return;
		}
		count = (ssize_t)got;
	}
	else
	{
		count = recv(connection->fd, connection->request + connection->length,
			sizeof(connection->request) - 1 - connection->length, 0);
		if (count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
		{
			close_connection(connection);
			return;
		}
		if (count < 0)
			return;
	}
	connection->length += (size_t)count;
	result = control_parse_request(connection->request, connection->length, &request, &status, &reason);
	if (result == CONTROL_PARSE_INCOMPLETE && connection->length >= sizeof(connection->request) - 1)
	{
		result = CONTROL_PARSE_ERROR;
		status = 413;
		reason = "the request is too large";
	}
	if (result == CONTROL_PARSE_INCOMPLETE)
		return;
	if (result == CONTROL_PARSE_ERROR)
	{
		respond_error(connection, status, reason, NULL);
		return;
	}
	/* (over TLS, the browser's cookie is marked Secure) */
	if (connection->tls)
		request.https = 1;
	handle_request(connection, &request);
	/* (the request's bytes, its token among them, not kept) */
	crypto_wipe(connection->request, sizeof(connection->request));
	connection->length = 0;
}

static void write_response(struct connection *connection)
{
	ssize_t count;

	if (connection->tls)
	{
		long sent = control_tls_write(connection->tls, connection->response + connection->sent,
			connection->response_length - connection->sent);

		if (sent == CONTROL_TLS_WANT_READ || sent == CONTROL_TLS_WANT_WRITE)
			return;
		if (sent <= 0)
		{
			close_connection(connection);
			return;
		}
		count = (ssize_t)sent;
	}
	else
	{
		count = send(connection->fd, connection->response + connection->sent,
			connection->response_length - connection->sent, MSG_NOSIGNAL);
		if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
			return;
		if (count <= 0)
		{
			close_connection(connection);
			return;
		}
	}
	connection->sent += (size_t)count;
	if (connection->sent >= connection->response_length)
	{
		if (!connection->tls)
			shutdown(connection->fd, SHUT_WR);
		close_connection(connection);
	}
}

/* a TLS connection's handshake, a step: on to reading its request once it
is done; a client speaking plain HTTP to it answered plainly, that it is
HTTPS */
static void handshake(struct connection *connection)
{
	int result = control_tls_handshake(connection->tls);

	if (result == CONTROL_TLS_WANT_READ || result == CONTROL_TLS_WANT_WRITE)
		return;
	if (result == CONTROL_TLS_DONE)
	{
		connection->state = CONNECTION_READING;
		return;
	}
	if (result == CONTROL_TLS_NOT_TLS)
	{
		static const char answer[] = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain; charset=utf-8\r\n"
			"Content-Length: 46\r\nConnection: close\r\n\r\nThis server's control panel is HTTPS: https://\n";
		char drain[2048];
		ssize_t sent;

		/* (the request read first: a close with it unread resets the
		connection, and the client never sees the answer) */
		while (recv(connection->fd, drain, sizeof(drain), 0) > 0)
			;
		control_tls_free(connection->tls);
		connection->tls = NULL;
		sent = send(connection->fd, answer, sizeof(answer) - 1, MSG_NOSIGNAL);
		(void)sent;
		shutdown(connection->fd, SHUT_WR);
	}
	close_connection(connection);
}

static void accept_connection(void)
{
	struct sockaddr_storage address;
	socklen_t length = sizeof(address);
	int fd = accept(control.listener, (struct sockaddr *)&address, &length);
	int index;

	if (fd < 0)
		return;
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	for (index = 0; index < MAXIMUM_CONNECTIONS; index++)
	{
		if (control.connections[index].state == CONNECTION_FREE)
			break;
	}
	if (index >= MAXIMUM_CONNECTIONS || fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK))
	{
		close(fd);
		return;
	}
	{
		struct connection *connection = &control.connections[index];

		memset(connection, 0, sizeof(*connection));
		connection->fd = fd;
		connection->ticket = -1;
		connection->state = CONNECTION_READING;
		connection->deadline = monotonic_seconds() + REQUEST_SECONDS;
		if (address.ss_family == AF_INET)
		{
			connection->address[10] = 0xff;
			connection->address[11] = 0xff;
			memcpy(connection->address + 12, &((struct sockaddr_in *)&address)->sin_addr, 4);
		}
		else if (address.ss_family == AF_INET6)
			memcpy(connection->address, &((struct sockaddr_in6 *)&address)->sin6_addr, 16);
		/* (the limits count a keyed hash of the address, the run's: no
		address is kept in them) */
		crypto_blake2b_keyed(connection->tag, sizeof(connection->tag), control.run_key, sizeof(control.run_key),
			connection->address, sizeof(connection->address));
		if (control.tls)
		{
			connection->tls = control_tls_accept(fd);
			if (!connection->tls)
			{
				close_connection(connection);
				return;
			}
			connection->state = CONNECTION_HANDSHAKING;
		}
	}
}

/* ---------- the console's credential commands */

/* the credentials file written again, whole, from the credentials kept
(a new file beside it, then renamed over it): 1, else 0 (printed) */
static int write_credentials(void)
{
	char path[1024];
	char temporary[1040];
	char text[CONTROL_CREDENTIAL_LINE];
	int index;
	int fd;
	FILE *out;

	credentials_path(path, sizeof(path));
	snprintf(temporary, sizeof(temporary), "%s.new", path);
	unlink(temporary);
	fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	out = fd >= 0 ? fdopen(fd, "w") : NULL;
	if (!out)
	{
		if (fd >= 0)
			close(fd);
		printf("cannot write %s.new in the data folder (%s): nothing changed\n", CREDENTIALS_FILE, strerror(errno));
		return 0;
	}
	fprintf(out,
		"# The ChupathingyCE Dedicated Server's control API credentials (server/docs/admin.md).\n"
		"# Each line is a token's Argon2id hash, never the token: one for each admin. Change them\n"
		"# with the console's sv_admin_add, sv_admin_rotate and sv_admin_remove, or delete this\n"
		"# file and restart the server for a single new token.\n");
	for (index = 0; index < control.credential_count; index++)
	{
		control_credential_line(&control.credentials[index], text);
		fprintf(out, "%s\n", text);
	}
	if (fflush(out) || fsync(fileno(out)) || fclose(out) || rename(temporary, path))
	{
		printf("cannot write %s in the data folder (%s): nothing changed\n", CREDENTIALS_FILE, strerror(errno));
		unlink(temporary);
		return 0;
	}
	return 1;
}

/* the credential a console command names (its name, or its id): its
index, or -1 (printed) */
static int credential_named(const char *text)
{
	int index;

	for (index = 0; index < control.credential_count; index++)
	{
		if (!strcmp(control.credentials[index].name, text) || !strcmp(control.credentials[index].id, text))
			return index;
	}
	printf("no credential %s (sv_admin_list)\n", text);
	return -1;
}

/* a new credential named name, into *credential, and its token printed
once: 1, else 0 (printed) */
static int make_credential(const char *name, struct control_credential *credential)
{
	uint8_t token_bytes[CONTROL_TOKEN_BYTES];
	uint8_t id[CONTROL_ID_LENGTH / 2];
	uint8_t salt[CONTROL_SALT_BYTES];
	char token[CONTROL_TOKEN_LENGTH + 1];
	int made;

	if (!random_bytes(token_bytes, sizeof(token_bytes)) || !random_bytes(id, sizeof(id)) ||
		!random_bytes(salt, sizeof(salt)))
	{
		printf("no random bytes for a token: nothing changed\n");
		return 0;
	}
	control_token_text(token_bytes, token);
	crypto_wipe(token_bytes, sizeof(token_bytes));
	made = control_credential_make(token, name, id, salt, CONTROL_ARGON2_KIB, CONTROL_ARGON2_PASSES, credential);
	if (made)
	{
		printf("\n"
			"The token of %s (%s), shown this once:\n"
			"\n"
			"    %s\n"
			"\n", credential->name, credential->id, token);
	}
	else
		printf("no memory to hash a token: nothing changed\n");
	crypto_wipe(token, sizeof(token));
	return made;
}

/* the tokens remembered as checked right, all forgotten (a credential's
index has moved, or its token changed) */
static void forget_remembered(void)
{
	crypto_wipe(control.remembered, sizeof(control.remembered));
	memset(control.remembered_valid, 0, sizeof(control.remembered_valid));
}

/* a console line that is one of the credentials' commands (sv_admin_*),
run here, where the credentials are: 1, else 0 (the main thread's) */
static int console_admin(const char *line)
{
	char words[3][64];
	char extra[2];
	int count;
	int index;

	words[0][0] = words[1][0] = 0;
	count = sscanf(line, "%63s %63s %63s %1s", words[0], words[1], words[2], extra);
	if (count < 1 || strncmp(words[0], "sv_admin_", 9))
		return 0;
	if (strcmp(words[0], "sv_admin_list") && strcmp(words[0], "sv_admin_add") && strcmp(words[0], "sv_admin_rotate") &&
		strcmp(words[0], "sv_admin_remove"))
	{
		return 0;
	}
	/* (logged as the main thread logs the console's other commands) */
	notice("console: %s", line);
	if (control.listener < 0)
	{
		printf("the control API is off (HALO_DEDICATED_CONTROL): no credentials here to manage\n");
	}
	else if (!strcmp(words[0], "sv_admin_list"))
	{
		if (count != 1)
			printf("usage: sv_admin_list\n");
		for (index = 0; count == 1 && index < control.credential_count; index++)
		{
			int sessions = 0;
			int session;

			for (session = 0; session < CONTROL_WEB_SESSIONS; session++)
			{
				sessions += control.sessions.entries[session].used &&
					!strcmp(control.sessions.entries[session].credential_id, control.credentials[index].id);
			}
			printf("%-31s %s  (%d web session%s)\n", control.credentials[index].name, control.credentials[index].id,
				sessions, sessions == 1 ? "" : "s");
		}
	}
	else if (count != 2)
	{
		printf("usage: %s <name>\n", words[0]);
	}
	else if (control.credentials_skipped)
	{
		printf("%s has lines that are no credential (the log says which): fix or delete them, and restart the "
			"server, before it is changed here\n", CREDENTIALS_FILE);
	}
	else if (!strcmp(words[0], "sv_admin_add"))
	{
		struct control_credential credential;

		if (!control_credential_name_valid(words[1]))
			printf("a name is 1 to 31 letters, digits, or -._~ and the like (no spaces or quotes)\n");
		else if (control.credential_count >= MAXIMUM_CREDENTIALS)
			printf("%d credentials at most: sv_admin_remove one first\n", (int)MAXIMUM_CREDENTIALS);
		else
		{
			for (index = 0; index < control.credential_count && strcmp(control.credentials[index].name, words[1]);
				index++)
			{
				;
			}
			if (index < control.credential_count)
				printf("there is a credential %s already (sv_admin_rotate gives it a new token)\n", words[1]);
			else if (make_credential(words[1], &credential))
			{
				control.credentials[control.credential_count++] = credential;
				if (write_credentials())
				{
					printf("%s can log in with it now; it was not written anywhere but here\n", credential.name);
					notice("a new credential: %s %s (sv_admin_add)", credential.name, credential.id);
				}
				else
				{
					control.credential_count--;
					printf("(the token above does not work)\n");
				}
				crypto_wipe(&credential, sizeof(credential));
			}
		}
	}
	else if ((index = credential_named(words[1])) >= 0)
	{
		struct control_credential old = control.credentials[index];

		if (!strcmp(words[0], "sv_admin_rotate"))
		{
			struct control_credential credential;

			if (make_credential(old.name, &credential))
			{
				control.credentials[index] = credential;
				if (write_credentials())
				{
					int ended = control_web_sessions_end_credential(&control.sessions, old.id);

					forget_remembered();
					printf("%s's old token (%s) and its %d web session%s stop working now\n", old.name, old.id, ended,
						ended == 1 ? "" : "s");
					notice("a credential's token rotated: %s %s, now %s (sv_admin_rotate)", old.name, old.id,
						credential.id);
				}
				else
				{
					control.credentials[index] = old;
					printf("(the token above does not work; the old one still does)\n");
				}
				crypto_wipe(&credential, sizeof(credential));
			}
		}
		else if (control.credential_count == 1)
		{
			printf("%s is the only credential: sv_admin_add another first (or sv_admin_rotate it)\n", old.name);
		}
		else
		{
			memmove(&control.credentials[index], &control.credentials[index + 1],
				(size_t)(control.credential_count - index - 1) * sizeof(control.credentials[0]));
			control.credential_count--;
			if (write_credentials())
			{
				int ended = control_web_sessions_end_credential(&control.sessions, old.id);

				forget_remembered();
				printf("%s (%s) taken out: its token and its %d web session%s stop working now\n", old.name, old.id,
					ended, ended == 1 ? "" : "s");
				notice("a credential taken out: %s %s (sv_admin_remove)", old.name, old.id);
			}
			else
			{
				memmove(&control.credentials[index + 1], &control.credentials[index],
					(size_t)(control.credential_count - index) * sizeof(control.credentials[0]));
				control.credentials[index] = old;
				control.credential_count++;
			}
		}
		crypto_wipe(&old, sizeof(old));
	}
	fflush(stdout);
	return 1;
}

/* ---------- the console */

static void console_line(const char *line)
{
	while (*line == ' ' || *line == '\t')
		line++;
	if (!*line || console_admin(line) || console_account(line))
		return;
	if (queue_command(line, "console", 0, 1, CONTROL_ROLE_OWNER, CONTROL_PERMISSION_ALL, NULL) < 0)
	{
		printf("too many commands waiting: try again shortly\n");
		fflush(stdout);
	}
}

static void read_console(void)
{
	char buffer[512];
	ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
	ssize_t index;

	if (count < 0 && (errno == EAGAIN || errno == EINTR))
		return;
	if (count <= 0)
	{
		control.console_open = 0;
		return;
	}
	for (index = 0; index < count; index++)
	{
		char character = buffer[index];

		if (character == '\n')
		{
			if (control.console_overlong)
			{
				printf("a command is at most %d characters\n", CONSOLE_LINE_SIZE - 1);
				fflush(stdout);
			}
			else
			{
				while (control.console_length && control.console_line[control.console_length - 1] == '\r')
					control.console_length--;
				control.console_line[control.console_length] = 0;
				console_line(control.console_line);
			}
			control.console_length = 0;
			control.console_overlong = 0;
			continue;
		}
		if (control.console_length + 1 >= sizeof(control.console_line))
			control.console_overlong = 1;
		else
			control.console_line[control.console_length++] = character;
	}
}

/* ---------- the control thread */

static void *control_thread(void *argument)
{
	(void)argument;
	for (;;)
	{
		struct pollfd fds[MAXIMUM_CONNECTIONS + 3];
		int map[MAXIMUM_CONNECTIONS + 3];
		int count = 0;
		int index;
		int64_t now;

		fds[count].fd = wake_pipe[0];
		fds[count].events = POLLIN;
		map[count++] = -1;
		if (control.listener >= 0)
		{
			fds[count].fd = control.listener;
			fds[count].events = POLLIN;
			map[count++] = -2;
		}
		if (control.console_open)
		{
			fds[count].fd = STDIN_FILENO;
			fds[count].events = POLLIN;
			map[count++] = -3;
		}
		for (index = 0; index < MAXIMUM_CONNECTIONS; index++)
		{
			struct connection *connection = &control.connections[index];

			if (connection->state == CONNECTION_READING || connection->state == CONNECTION_WRITING ||
				connection->state == CONNECTION_HANDSHAKING)
			{
				fds[count].fd = connection->fd;
				/* (TLS may need to write while it reads, or read while it
				writes: it says which) */
				fds[count].events = connection->tls && connection->state != CONNECTION_WRITING ?
					control_tls_poll_events(connection->tls) : connection->state == CONNECTION_READING ? POLLIN :
					POLLOUT;
				if (connection->tls && connection->state == CONNECTION_WRITING)
					fds[count].events = (short)(control_tls_poll_events(connection->tls) | POLLOUT);
				map[count++] = index;
			}
		}
		if (poll(fds, (nfds_t)count, 500) < 0 && errno != EINTR)
			continue;
		/* (a renewed certificate of the operator's taken, once a minute) */
		if (control.tls)
		{
			char problem[256];
			int reloaded = control_tls_reload_if_changed(monotonic_seconds(), problem, sizeof(problem));

			if (reloaded > 0)
			{
				snprintf(control.fingerprint, sizeof(control.fingerprint), "%s", control_tls_fingerprint());
				notice("the control panel's certificate was renewed: its SHA-256 fingerprint is %s", control.fingerprint);
			}
			else if (reloaded < 0)
				notice("the control panel's certificate files changed but cannot be read (%s): the old one is kept",
					problem);
		}
		for (index = 0; index < count; index++)
		{
			if (!fds[index].revents)
				continue;
			if (map[index] == -1)
			{
				char drain[64];

				while (read(wake_pipe[0], drain, sizeof(drain)) > 0)
					;
			}
			else if (map[index] == -2)
				accept_connection();
			else if (map[index] == -3)
				read_console();
			else
			{
				struct connection *connection = &control.connections[map[index]];

				if (connection->state == CONNECTION_HANDSHAKING)
					handshake(connection);
				else if (connection->state == CONNECTION_READING)
					read_request(connection);
				else if (connection->state == CONNECTION_WRITING)
					write_response(connection);
			}
		}
		/* (bytes TLS has read and not handed out yet: poll does not wake for
		them) */
		for (index = 0; index < MAXIMUM_CONNECTIONS; index++)
		{
			struct connection *connection = &control.connections[index];

			if (connection->tls && connection->state == CONNECTION_HANDSHAKING &&
				control_tls_pending(connection->tls))
				handshake(connection);
			if (connection->tls && connection->state == CONNECTION_READING && control_tls_pending(connection->tls))
				read_request(connection);
		}
		/* the commands the main thread has answered; the connections out of
		time */
		now = monotonic_seconds();
		for (index = 0; index < MAXIMUM_CONNECTIONS; index++)
		{
			struct connection *connection = &control.connections[index];

			if (connection->state == CONNECTION_WAITING)
			{
				char *output = NULL;
				int ok = 0;
				int done = 0;

				pthread_mutex_lock(&queue_mutex);
				if (tickets[connection->ticket].state == TICKET_DONE)
				{
					done = 1;
					ok = tickets[connection->ticket].ok;
					output = tickets[connection->ticket].output;
					tickets[connection->ticket].output = NULL;
					release_ticket(connection->ticket);
					connection->ticket = -1;
				}
				pthread_mutex_unlock(&queue_mutex);
				if (done)
				{
					respond_command(connection, ok, output ? output : "");
					free(output);
					continue;
				}
				if (now >= connection->deadline)
				{
					pthread_mutex_lock(&queue_mutex);
					/* (answered since it was looked at above: no one else
					will release it) */
					if (tickets[connection->ticket].state == TICKET_QUEUED ||
						tickets[connection->ticket].state == TICKET_DONE)
						release_ticket(connection->ticket);
					else
						tickets[connection->ticket].state = TICKET_ABANDONED;
					pthread_mutex_unlock(&queue_mutex);
					connection->ticket = -1;
					respond_error(connection, 503, "the server did not answer in time (loading a map?): try again",
						"Retry-After: 5\r\n");
				}
			}
			else if (connection->state != CONNECTION_FREE && now >= connection->deadline)
				close_connection(connection);
		}
	}
	return NULL;
}

/* ---------- public code */

void server_control_start(void)
{
	const char *console = getenv("HALO_DEDICATED_CONSOLE");
	const char *setting = getenv("HALO_DEDICATED_CONTROL");
	pthread_t thread;
	pthread_attr_t attributes;
	int index;

	if (control.started)
		return;
	control.started = 1;
	control.listener = -1;
	for (index = 0; index < MAXIMUM_CONNECTIONS; index++)
	{
		control.connections[index].fd = -1;
		control.connections[index].ticket = -1;
	}
	control_limiter_initialize(&control.limiter);
	/* the console: standard input, when it is a terminal or when asked */
	control.console = console && console[0] ? truthy(console) : isatty(STDIN_FILENO);
	if (setting && !falsy(setting))
	{
		uint8_t session_key[32];

		if (!random_bytes(control.run_key, sizeof(control.run_key)) || !random_bytes(session_key, sizeof(session_key)))
			notice("no random bytes: the control API is off");
		else if (load_credentials())
		{
			control_web_sessions_initialize(&control.sessions, session_key);
			if (start_listener(setting))
			{
				/* the control panel's accounts, and the first owner's setup
				code while there is none */
				load_accounts();
				publish_account_keys();
				make_setup_code();
			}
		}
		crypto_wipe(session_key, sizeof(session_key));
	}
	if (!control.console && control.listener < 0)
		return;
	if (pipe(wake_pipe))
	{
		notice("the console and control API cannot start (%s)", strerror(errno));
		if (control.listener >= 0)
			close(control.listener);
		control.listener = -1;
		return;
	}
	fcntl(wake_pipe[0], F_SETFL, fcntl(wake_pipe[0], F_GETFL) | O_NONBLOCK);
	fcntl(wake_pipe[1], F_SETFL, fcntl(wake_pipe[1], F_GETFL) | O_NONBLOCK);
	fcntl(wake_pipe[0], F_SETFD, FD_CLOEXEC);
	fcntl(wake_pipe[1], F_SETFD, FD_CLOEXEC);
	control.console_open = control.console;
	if (control.console)
		notice("the console reads commands on standard input (help lists them)");
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	/* (a stack of its own size: musl's default is small, and a response is
	built on it) */
	pthread_attr_setstacksize(&attributes, 1024 * 1024);
	if (pthread_create(&thread, &attributes, control_thread, NULL))
	{
		notice("the console and control API cannot start");
		if (control.listener >= 0)
			close(control.listener);
		control.listener = -1;
	}
	pthread_attr_destroy(&attributes);
}

int server_control_next(char *line, int line_size, char *source, int source_size, int *flags,
	unsigned int *permissions, int *role)
{
	int found = -1;
	int index;

	pthread_mutex_lock(&queue_mutex);
	for (index = 0; index < QUEUE_SIZE; index++)
	{
		if (tickets[index].state == TICKET_QUEUED &&
			(found < 0 || tickets[index].sequence < tickets[found].sequence))
		{
			found = index;
		}
	}
	if (found >= 0)
	{
		snprintf(line, (size_t)line_size, "%s", tickets[found].line);
		snprintf(source, (size_t)source_size, "%s", tickets[found].source);
		*flags = tickets[found].flags;
		*permissions = tickets[found].permissions;
		*role = tickets[found].role;
		if (tickets[found].flags & CONTROL_NOTICE)
			release_ticket(found);
		else
			tickets[found].state = TICKET_TAKEN;
	}
	pthread_mutex_unlock(&queue_mutex);
	return found + 1;
}

const char *server_control_payload(int ticket)
{
	const char *payload = NULL;
	int index = ticket - 1;

	/* (the main thread's, while it runs the ticket's command: the control
	thread does not free it before it is finished) */
	if (index >= 0 && index < QUEUE_SIZE)
	{
		pthread_mutex_lock(&queue_mutex);
		if (tickets[index].state == TICKET_TAKEN)
			payload = tickets[index].payload;
		pthread_mutex_unlock(&queue_mutex);
	}
	return payload;
}

void server_control_finish(int ticket, int ok, const char *output)
{
	int index = ticket - 1;

	if (index < 0 || index >= QUEUE_SIZE)
		return;
	pthread_mutex_lock(&queue_mutex);
	if (tickets[index].state == TICKET_ABANDONED)
		release_ticket(index);
	else if (tickets[index].state == TICKET_TAKEN && tickets[index].console)
	{
		/* (the console's: printed for whoever typed it) */
		fputs(output, stdout);
		if (output[0] && output[strlen(output) - 1] != '\n')
			fputc('\n', stdout);
		fflush(stdout);
		release_ticket(index);
	}
	else if (tickets[index].state == TICKET_TAKEN)
	{
		size_t length = strnlen(output, MAXIMUM_OUTPUT);

		tickets[index].output = malloc(length + 1);
		if (tickets[index].output)
		{
			memcpy(tickets[index].output, output, length);
			tickets[index].output[length] = 0;
		}
		tickets[index].ok = ok;
		tickets[index].state = TICKET_DONE;
	}
	pthread_mutex_unlock(&queue_mutex);
	if (wake_pipe[1] >= 0)
	{
		ssize_t written = write(wake_pipe[1], "", 1);

		(void)written;
	}
}

void server_control_log(const char *text)
{
	long long now = (long long)time(NULL);

	/* (each of its lines, as the API hands them out) */
	while (text && *text)
	{
		const char *end = text;
		struct log_line *line;

		while (*end && *end != '\n')
			end++;
		/* (no empty line, as "\r\n" ends one) */
		if (end > text && !(end - text == 1 && *text == '\r'))
		{
			pthread_mutex_lock(&log_mutex);
			line = &log_lines[++log_sequence % LOG_LINES];
			line->sequence = log_sequence;
			line->time = now;
			control_log_scrub(text, line->text, sizeof(line->text));
			pthread_mutex_unlock(&log_mutex);
		}
		text = *end ? end + 1 : end;
	}
}
