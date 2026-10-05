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

#include "control_protocol.h"
#include "control_web.h"

#include "monocypher.h"

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

	/* a command's flags, as server_commands.c reads them */
	CONTROL_JSON = 1,
	CONTROL_NOTICE = 2,
	CONTROL_QUIET = 4,
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

/* ---------- structures */

struct ticket
{
	int state;
	int flags;
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
	uint8_t address[16];
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
} control;

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

/* a command queued for the main thread: its ticket, or -1 if the queue is
full */
static int queue_command(const char *line, const char *source, int flags, int console)
{
	int index;

	pthread_mutex_lock(&queue_mutex);
	index = free_ticket();
	if (index >= 0)
	{
		snprintf(tickets[index].line, sizeof(tickets[index].line), "%s", line);
		snprintf(tickets[index].source, sizeof(tickets[index].source), "%s", source);
		tickets[index].state = TICKET_QUEUED;
		tickets[index].flags = flags;
		tickets[index].console = console;
		tickets[index].sequence = ++ticket_sequence;
	}
	pthread_mutex_unlock(&queue_mutex);
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

static int start_listener(const char *setting)
{
	struct sockaddr_storage address;
	socklen_t length;
	int loopback = 0;
	int fd;
	int one = 1;

	if (!parse_listen_address(setting, &address, &length, &loopback))
	{
		notice("HALO_DEDICATED_CONTROL is not an address to listen on (port, 127.0.0.1:port, [::1]:port): the "
			"control API is off");
		return 0;
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
	notice("the control API and web admin page listen on %s (HTTP; server/docs/admin.md)", setting);
	if (!loopback)
	{
		notice("WARNING: the control API and web admin page listen beyond this machine (%s). They are plain HTTP: "
			"reach them through an SSH tunnel, a private network such as Tailscale, or a TLS reverse proxy, and "
			"firewall the port", setting);
	}
	return 1;
}

/* ---------- responses */

static void close_connection(struct connection *connection)
{
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
		!control_limiter_allowed(&control.limiter, connection->address, now, &retry_after))
	{
		char headers[64];

		snprintf(headers, sizeof(headers), "Retry-After: %lld\r\n", (long long)retry_after);
		respond_error(connection, 429, "too many wrong tokens from this address: try again later", headers);
		return -1;
	}
	if (!control_token_valid(token) || !control.credential_count)
	{
		control_limiter_failed(&control.limiter, connection->address, now);
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
		control_limiter_failed(&control.limiter, connection->address, now);
		address_text(connection->address, address, sizeof(address));
		notice("a %s with a wrong token, from %s", what, address);
		respond_error(connection, 401, "the token is not right", challenge);
		return -1;
	}
	control_limiter_succeeded(&control.limiter, connection->address);
	return credential;
}

/* POST /v1/login, {"token": "..."}: a session, its cookie set */
static void handle_login(struct connection *connection, struct control_request *request, int64_t now)
{
	char token[CONTROL_MAXIMUM_TOKEN];
	uint8_t id_bytes[CONTROL_WEB_SECRET_BYTES];
	uint8_t csrf_bytes[CONTROL_WEB_SECRET_BYTES];
	char id_text[CONTROL_WEB_SECRET_LENGTH + 1];
	char headers[256];
	char address[64];
	const char *reason;
	char *body;
	int credential;
	int session;
	int status;

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
	if (!request->json_body)
	{
		respond_error(connection, 415, "the body is JSON: Content-Type: application/json", NULL);
		return;
	}
	if (!control_parse_login_body(request->body, request->content_length, token, sizeof(token), &reason))
	{
		respond_error(connection, 400, reason, NULL);
		return;
	}
	credential = check_token(connection, token, now, "web login", NULL);
	crypto_wipe(token, sizeof(token));
	if (credential < 0)
		return;
	if (!random_bytes(id_bytes, sizeof(id_bytes)) || !random_bytes(csrf_bytes, sizeof(csrf_bytes)))
	{
		respond_error(connection, 503, "no random bytes for a session", NULL);
		return;
	}
	session = control_web_session_create(&control.sessions, id_bytes, csrf_bytes, control.credentials[credential].name,
		control.credentials[credential].id, now, id_text);
	crypto_wipe(id_bytes, sizeof(id_bytes));
	crypto_wipe(csrf_bytes, sizeof(csrf_bytes));
	control_web_session_cookie(headers, sizeof(headers), id_text, request->https);
	crypto_wipe(id_text, sizeof(id_text));
	address_text(connection->address, address, sizeof(address));
	notice("web login: %s %s, from %s", control.credentials[credential].name, control.credentials[credential].id,
		address);
	body = malloc(256);
	if (body)
	{
		snprintf(body, 256, "{\"name\": \"%s\", \"id\": \"%s\", \"csrf\": \"%s\", \"idle_seconds\": %d}\n",
			control.credentials[credential].name, control.credentials[credential].id,
			control.sessions.entries[session].csrf, (int)CONTROL_WEB_IDLE_SECONDS);
	}
	respond(connection, 200, body, headers);
	crypto_wipe(headers, sizeof(headers));
}

/* a request with a session cookie: its session (its index), the source
its commands are logged with, and for a POST the checks a browser's
change must pass; -1 with the response given */
static int session_request(struct connection *connection, struct control_request *request, int64_t now,
	char *source, size_t source_size)
{
	int session = control_web_session_find(&control.sessions, request->session, now, request->background);
	const char *reason;
	char cookie[160];
	int status;

	crypto_wipe(request->session, sizeof(request->session));
	if (session >= 0 && credential_by_id(control.sessions.entries[session].credential_id) < 0)
	{
		/* (its credential taken out since) */
		control_web_session_end(&control.sessions, session);
		session = -1;
	}
	if (session < 0)
	{
		control_web_session_cookie(cookie, sizeof(cookie), NULL, request->https);
		respond_error(connection, 401, "not logged in, or the session ended: log in again", cookie);
		return -1;
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
			return -1;
		}
	}
	snprintf(source, source_size, "web %s %s", control.sessions.entries[session].name,
		control.sessions.entries[session].credential_id);
	return session;
}

static void handle_request(struct connection *connection, struct control_request *request)
{
	int64_t now = monotonic_seconds();
	int credential;
	int session = -1;
	char source[80];
	char command[CONTROL_MAXIMUM_COMMAND];
	const char *line = NULL;
	int flags = 0;
	int ticket;

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
	/* a browser's session (a request with no Authorization, and its cookie),
	else a bearer token */
	if (request->authorization == CONTROL_AUTHORIZATION_NONE && request->session[0])
	{
		session = session_request(connection, request, now, source, sizeof(source));
		if (session < 0)
			return;
	}
	else
	{
		if (request->authorization != CONTROL_AUTHORIZATION_BEARER)
		{
			int64_t retry_after;

			if (!control_limiter_allowed(&control.limiter, connection->address, now, &retry_after))
			{
				char headers[64];

				snprintf(headers, sizeof(headers), "Retry-After: %lld\r\n", (long long)retry_after);
				respond_error(connection, 429, "too many wrong tokens from this address: try again later", headers);
				return;
			}
			if (request->authorization != CONTROL_AUTHORIZATION_NONE)
			{
				char address[64];

				control_limiter_failed(&control.limiter, connection->address, now);
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
		snprintf(source, sizeof(source), "api %s %s", control.credentials[credential].name,
			control.credentials[credential].id);
	}

	if (!strcmp(request->path, "/v1/session") || !strcmp(request->path, "/v1/logout"))
	{
		int logout = !strcmp(request->path, "/v1/logout");
		char *body;

		if (session < 0)
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

			notice("web logout: %s", source + 4);
			control_web_session_end(&control.sessions, session);
			control_web_session_cookie(cookie, sizeof(cookie), NULL, request->https);
			body = malloc(32);
			if (body)
				snprintf(body, 32, "{\"ok\": true}\n");
			respond(connection, 200, body, cookie);
			return;
		}
		body = malloc(256);
		if (body)
		{
			snprintf(body, 256, "{\"name\": \"%s\", \"id\": \"%s\", \"csrf\": \"%s\", \"idle_seconds\": %d}\n",
				control.sessions.entries[session].name, control.sessions.entries[session].credential_id,
				control.sessions.entries[session].csrf, (int)CONTROL_WEB_IDLE_SECONDS);
		}
		respond(connection, 200, body, NULL);
		return;
	}
	if (!strcmp(request->path, "/v1/log"))
	{
		uint64_t since;

		if (request->method != CONTROL_METHOD_GET)
		{
			respond_error(connection, 405, "GET only", "Allow: GET\r\n");
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
	if (!strcmp(request->path, "/v1/status") || !strcmp(request->path, "/v1/players") ||
		!strcmp(request->path, "/v1/bans") || !strcmp(request->path, "/v1/mapcycle") ||
		!strcmp(request->path, "/v1/maps"))
	{
		if (request->method != CONTROL_METHOD_GET)
		{
			respond_error(connection, 405, "GET only", "Allow: GET\r\n");
			return;
		}
		line = !strcmp(request->path, "/v1/status") ? "sv_status" : !strcmp(request->path, "/v1/players") ?
			"sv_players" : !strcmp(request->path, "/v1/bans") ? "sv_banlist" :
			!strcmp(request->path, "/v1/mapcycle") ? "sv_mapcycle" : "sv_maps";
		flags = CONTROL_JSON | CONTROL_QUIET;
		connection->answer = ANSWER_JSON;
	}
	else if (!strcmp(request->path, "/v1/command"))
	{
		const char *reason;

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
		line = command;
		connection->answer = ANSWER_COMMAND;
	}
	else
	{
		respond_error(connection, 404, "no such endpoint (/v1/status, /v1/players, /v1/bans, /v1/mapcycle, /v1/maps, "
			"/v1/command, /v1/log)", NULL);
		return;
	}
	if (request->query[0])
	{
		respond_error(connection, 400, "no query is taken here", NULL);
		return;
	}
	ticket = queue_command(line, source, flags, 0);
	if (ticket < 0)
	{
		respond_error(connection, 503, "too many commands waiting: try again shortly", "Retry-After: 1\r\n");
		return;
	}
	connection->ticket = ticket;
	connection->state = CONNECTION_WAITING;
	connection->deadline = now + COMMAND_SECONDS;
}

static void read_request(struct connection *connection)
{
	struct control_request request;
	const char *reason;
	int status;
	int result;
	ssize_t count = recv(connection->fd, connection->request + connection->length,
		sizeof(connection->request) - 1 - connection->length, 0);

	if (count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
	{
		close_connection(connection);
		return;
	}
	if (count < 0)
		return;
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
	handle_request(connection, &request);
	/* (the request's bytes, its token among them, not kept) */
	crypto_wipe(connection->request, sizeof(connection->request));
	connection->length = 0;
}

static void write_response(struct connection *connection)
{
	ssize_t count = send(connection->fd, connection->response + connection->sent,
		connection->response_length - connection->sent, MSG_NOSIGNAL);

	if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
		return;
	if (count <= 0)
	{
		close_connection(connection);
		return;
	}
	connection->sent += (size_t)count;
	if (connection->sent >= connection->response_length)
	{
		shutdown(connection->fd, SHUT_WR);
		close_connection(connection);
	}
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
	if (!*line || console_admin(line))
		return;
	if (queue_command(line, "console", 0, 1) < 0)
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

			if (connection->state == CONNECTION_READING || connection->state == CONNECTION_WRITING)
			{
				fds[count].fd = connection->fd;
				fds[count].events = connection->state == CONNECTION_READING ? POLLIN : POLLOUT;
				map[count++] = index;
			}
		}
		if (poll(fds, (nfds_t)count, 500) < 0 && errno != EINTR)
			continue;
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

				if (connection->state == CONNECTION_READING)
					read_request(connection);
				else if (connection->state == CONNECTION_WRITING)
					write_response(connection);
			}
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
					if (tickets[connection->ticket].state == TICKET_QUEUED)
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
			start_listener(setting);
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

int server_control_next(char *line, int line_size, char *source, int source_size, int *flags)
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
		if (tickets[found].flags & CONTROL_NOTICE)
			release_ticket(found);
		else
			tickets[found].state = TICKET_TAKEN;
	}
	pthread_mutex_unlock(&queue_mutex);
	return found + 1;
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
