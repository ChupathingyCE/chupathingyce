/*
CONTROL_LINK.C

Delta Control's link to halo.milenko.org (server_link.h; docs/delta.md,
Delta Control). Off until the server's owner links it (sv_link), and off
for good with HALO_DEDICATED_LINK=false.

Linking: the server makes an Ed25519 key and a secret, tells the site the
key's public half and the secret (HTTPS, the site's certificate checked),
and is given a code; the owner enters the code on the site, signed in, and
confirms; the server, asking every 3 seconds, learns it is linked and keeps
its credential (delta_link.key in the data folder, readable by its owner
alone). From then on a thread of its own asks the site every few seconds
(POST /v1/control/poll, signed with the key, its time and a nonce never
used again), with the server's state and the answers to the commands it
ran; the site answers (with a MAC of the secret over the request's nonce)
the commands its accounts sent and, when it changed, the role list. The
server dials out: no port is opened, and it works behind any router.

The site's word is checked here and again by the main thread: a command is
one of those the link takes (control_link_protocol.c), from a handle the
role list has, run with that role's permissions (server_commands.c), never
above HALO_DEDICATED_LINK_ROLE (admin unless set: the site's owner role is
taken as admin), and audited. sv_unlink deletes the credential at once,
whatever the site says; the site unlinking is heard on the next poll.

Requests go through posix_browser.c (one at a time across the program's
threads), so polls are short: one every few seconds, no long poll.
*/

#include "control_link_protocol.h"
#include "control_protocol.h"
#include "control_roles.h"
#include "../src/server_roles.h"
#include "../src/server_link.h"

#include "monocypher.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* the platform layer's (xbox_files.c, posix_browser.c, the game's log):
plain types */
const char *platform_data_root(void);
int posix_browser_request(const char *url, const char *body, const char *content_type, char *response,
	int response_size, char *error, int error_size);
void platform_log(const char *format, ...);

/* ---------- constants */

enum
{
	/* a code's state asked for this often, and the code's life at most */
	STATUS_SECONDS = 3,
	/* the poll's room: an answer, and the status sent */
	ANSWER_SIZE = 96 * 1024,
	STATUS_SIZE = 24 * 1024,
	/* the commands waiting for the main thread, and the results waiting to
	be sent, at most */
	MAXIMUM_PENDING = 16,
	RESULT_SIZE = 1024,
	/* polls after a failure: this long at first, doubled up to the most */
	RETRY_SECONDS = 5,
	RETRY_MAXIMUM = 120,
};

enum
{
	LINK_OFF,
	/* asking the site for a code, or waiting for the owner to enter it */
	LINK_STARTING,
	LINK_WAITING,
	LINK_LINKED,
};

#define LINK_FILE "delta_link.key"

/* ---------- globals */

static pthread_mutex_t link_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t link_wake = PTHREAD_COND_INITIALIZER;

static struct
{
	int started;
	/* HALO_DEDICATED_LINK=false */
	int disabled;
	int state;
	char url[256];
	int cap;
	/* the credential */
	char server_id[CONTROL_LINK_SERVER_ID_SIZE];
	char owner[CONTROL_LINK_HANDLE_SIZE];
	uint8_t seed[CONTROL_LINK_SEED_BYTES];
	uint8_t secret[CONTROL_LINK_SECRET_BYTES];
	/* linking: the code, its poll token, until when; the server's name */
	char code[CONTROL_LINK_CODE_SIZE];
	char token[CONTROL_LINK_TOKEN_SIZE];
	int64_t code_until;
	char server_name[32];
	char version[32];
	/* an unlink asked for (the thread tells the site, with the credential it
	keeps for that) */
	int unlink_pending;
	char unlink_server_id[CONTROL_LINK_SERVER_ID_SIZE];
	uint8_t unlink_seed[CONTROL_LINK_SEED_BYTES];
	/* the site last answered right, and what went wrong last */
	int64_t heard;
	char problem[160];
	uint64_t roles_version;
	/* the commands for the main thread */
	struct control_link_command commands[MAXIMUM_PENDING];
	int command_count;
	/* their answers, for the next poll */
	struct
	{
		uint64_t id;
		int ok;
		char output[RESULT_SIZE];
	} results[MAXIMUM_PENDING];
	int result_count;
	char status[STATUS_SIZE];
} link_state;

/* ---------- private code */

static int64_t now_seconds(void)
{
	return (int64_t)time(NULL);
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

static void link_path(char *path, size_t size)
{
	const char *root = platform_data_root();

	snprintf(path, size, "%s/%s", root && root[0] ? root : ".", LINK_FILE);
}

static int hex_parse(const char *text, uint8_t *bytes, size_t count)
{
	size_t index;

	if (strlen(text) != 2 * count)
		return 0;
	for (index = 0; index < count; index++)
	{
		unsigned int value;

		if (sscanf(text + 2 * index, "%2x", &value) != 1 || !strchr("0123456789abcdef", text[2 * index]) ||
			!strchr("0123456789abcdef", text[2 * index + 1]))
		{
			return 0;
		}
		bytes[index] = (uint8_t)value;
	}
	return 1;
}

/* the credential file: "v1 <server id> <seed> <secret> <owner>" */
static int load_credential(void)
{
	char path[1024];
	char line[512];
	char server_id[CONTROL_LINK_SERVER_ID_SIZE], seed[80], secret[80], owner[CONTROL_LINK_HANDLE_SIZE];
	FILE *file;
	int ok = 0;

	link_path(path, sizeof(path));
	file = fopen(path, "r");
	if (!file)
		return 0;
	while (fgets(line, sizeof(line), file))
	{
		if (line[0] == '#')
			continue;
		owner[0] = 0;
		if (sscanf(line, "v1 %63s %79s %79s %31s", server_id, seed, secret, owner) >= 3 &&
			hex_parse(seed, link_state.seed, sizeof(link_state.seed)) &&
			hex_parse(secret, link_state.secret, sizeof(link_state.secret)))
		{
			snprintf(link_state.server_id, sizeof(link_state.server_id), "%s", server_id);
			snprintf(link_state.owner, sizeof(link_state.owner), "%s", owner);
			ok = 1;
		}
		break;
	}
	crypto_wipe(line, sizeof(line));
	crypto_wipe(seed, sizeof(seed));
	crypto_wipe(secret, sizeof(secret));
	fclose(file);
	return ok;
}

static int save_credential(void)
{
	char path[1024];
	char temporary[1040];
	char seed[2 * CONTROL_LINK_SEED_BYTES + 1];
	char secret[2 * CONTROL_LINK_SECRET_BYTES + 1];
	int fd;
	FILE *out;
	int ok;

	link_path(path, sizeof(path));
	snprintf(temporary, sizeof(temporary), "%s.new", path);
	unlink(temporary);
	fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	out = fd >= 0 ? fdopen(fd, "w") : NULL;
	if (!out)
	{
		if (fd >= 0)
			close(fd);
		return 0;
	}
	control_hex_text(link_state.seed, sizeof(link_state.seed), seed);
	control_hex_text(link_state.secret, sizeof(link_state.secret), secret);
	fprintf(out, "# The ChupathingyCE Dedicated Server's link to halo.milenko.org (Delta Control). Keep it\n"
		"# private; delete it (or sv_unlink) to unlink.\nv1 %s %s %s %s\n", link_state.server_id, seed, secret,
		link_state.owner[0] ? link_state.owner : "-");
	crypto_wipe(seed, sizeof(seed));
	crypto_wipe(secret, sizeof(secret));
	ok = !fflush(out) && !fsync(fileno(out)) && !fclose(out) && !rename(temporary, path);
	if (!ok)
		unlink(temporary);
	return ok;
}

static void forget_credential(void)
{
	char path[1024];

	link_path(path, sizeof(path));
	unlink(path);
	crypto_wipe(link_state.seed, sizeof(link_state.seed));
	crypto_wipe(link_state.secret, sizeof(link_state.secret));
	link_state.server_id[0] = 0;
	link_state.owner[0] = 0;
	link_state.roles_version = 0;
	link_state.command_count = 0;
	link_state.result_count = 0;
	server_roles_set_site(0, NULL, NULL, NULL, 0);
	server_roles_set_site_handles(0, NULL, NULL);
}

static void set_problem(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(link_state.problem, sizeof(link_state.problem), format, arguments);
	va_end(arguments);
}

/* a request to the site: its status, and its answer in answer (the lock not
held: it may take seconds) */
static int request(const char *path, const char *body, char *answer, int answer_size, char *error, int error_size)
{
	char url[400];

	snprintf(url, sizeof(url), "%s%s", link_state.url, path);
	return posix_browser_request(url, body, "application/json", answer, answer_size, error, error_size);
}

/* the site's role list taken, never above the link's cap */
static void take_roles(const struct control_link_poll *poll)
{
	static unsigned char keys[CONTROL_LINK_ROLE_ENTRIES * CONTROL_LINK_ENTRY_KEYS * 32];
	static int key_roles[CONTROL_LINK_ROLE_ENTRIES * CONTROL_LINK_ENTRY_KEYS];
	static const char *key_names[CONTROL_LINK_ROLE_ENTRIES * CONTROL_LINK_ENTRY_KEYS];
	static const char *handles[CONTROL_LINK_ROLE_ENTRIES];
	static int handle_roles[CONTROL_LINK_ROLE_ENTRIES];
	int key_count = 0;
	int index;

	for (index = 0; index < poll->role_count; index++)
	{
		const struct control_link_role *role = &poll->roles[index];
		int capped = role->role > link_state.cap ? link_state.cap : role->role;
		int key;

		handles[index] = role->handle;
		handle_roles[index] = capped;
		for (key = 0; key < role->key_count; key++)
		{
			memcpy(keys + key_count * 32, role->keys[key], 32);
			key_roles[key_count] = capped;
			key_names[key_count] = role->handle;
			key_count++;
		}
	}
	server_roles_set_site_handles(poll->role_count, handles, handle_roles);
	server_roles_set_site(key_count, keys, key_roles, key_names, (unsigned int)poll->roles_version);
}

/* the linking's part of the thread: a code asked for, then its state */
static void run_linking(void)
{
	static char answer[4096];
	char body[512];
	char error[256];
	int status;
	uint8_t public_key[32];
	char public_text[65], secret_text[65];
	char code[CONTROL_LINK_CODE_SIZE], token[CONTROL_LINK_TOKEN_SIZE];
	int expires = 600;

	pthread_mutex_lock(&link_mutex);
	control_link_public_key(link_state.seed, public_key);
	control_hex_text(public_key, sizeof(public_key), public_text);
	control_hex_text(link_state.secret, sizeof(link_state.secret), secret_text);
	{
		char name[96], version[96];

		control_json_string(link_state.server_name, name, sizeof(name));
		control_json_string(link_state.version, version, sizeof(version));
		snprintf(body, sizeof(body), "{\"public_key\": \"%s\", \"secret\": \"%s\", \"name\": %s, \"version\": %s}",
			public_text, secret_text, name, version);
	}
	crypto_wipe(secret_text, sizeof(secret_text));
	pthread_mutex_unlock(&link_mutex);
	status = request("/v1/control/link/start", body, answer, sizeof(answer), error, sizeof(error));
	crypto_wipe(body, sizeof(body));
	pthread_mutex_lock(&link_mutex);
	if (status != 200 || !control_link_parse_start(answer, strlen(answer), code, sizeof(code), token, sizeof(token),
		&expires))
	{
		link_state.state = LINK_OFF;
		set_problem(status ? "the site refused to link (HTTP %d)" : "the site could not be reached (%s)",
			status ? status : 0, error);
		if (status)
			set_problem("the site refused to link (HTTP %d)", status);
		pthread_mutex_unlock(&link_mutex);
		platform_log("Delta Control: %s", link_state.problem);
		return;
	}
	snprintf(link_state.code, sizeof(link_state.code), "%s", code);
	snprintf(link_state.token, sizeof(link_state.token), "%s", token);
	link_state.code_until = now_seconds() + expires;
	link_state.state = LINK_WAITING;
	pthread_mutex_unlock(&link_mutex);
	/* (on the console, as sv_link said it would be, and in the log: the code
	is good once, for a few minutes, and only with the owner's account) */
	printf("\nChupathingyCE Dedicated Server: to link this server to your account, sign in at\n"
		"%s/servers/link and enter the code\n\n    %s\n\nIt is good for %d minutes.\n\n", link_state.url, code,
		expires / 60);
	fflush(stdout);
	platform_log("Delta Control: the link's code is %s (enter it at %s/servers/link)", code, link_state.url);
	for (;;)
	{
		char state[16], server_id[CONTROL_LINK_SERVER_ID_SIZE], owner[CONTROL_LINK_HANDLE_SIZE];

		sleep(STATUS_SECONDS);
		pthread_mutex_lock(&link_mutex);
		if (link_state.state != LINK_WAITING)
		{
			pthread_mutex_unlock(&link_mutex);
			return;
		}
		if (now_seconds() >= link_state.code_until)
		{
			link_state.state = LINK_OFF;
			set_problem("the code ran out before it was entered: sv_link for another");
			pthread_mutex_unlock(&link_mutex);
			platform_log("Delta Control: %s", link_state.problem);
			return;
		}
		snprintf(body, sizeof(body), "{\"token\": \"%s\"}", link_state.token);
		pthread_mutex_unlock(&link_mutex);
		status = request("/v1/control/link/status", body, answer, sizeof(answer), error, sizeof(error));
		if (status != 200 || !control_link_parse_status(answer, strlen(answer), state, sizeof(state), server_id,
			sizeof(server_id), owner, sizeof(owner)))
		{
			continue;
		}
		if (!strcmp(state, "pending"))
			continue;
		pthread_mutex_lock(&link_mutex);
		if (link_state.state != LINK_WAITING)
		{
			pthread_mutex_unlock(&link_mutex);
			return;
		}
		if (strcmp(state, "linked"))
		{
			link_state.state = LINK_OFF;
			set_problem("the link was %s on the site", state);
			pthread_mutex_unlock(&link_mutex);
			platform_log("Delta Control: %s", link_state.problem);
			return;
		}
		snprintf(link_state.server_id, sizeof(link_state.server_id), "%s", server_id);
		snprintf(link_state.owner, sizeof(link_state.owner), "%s", owner);
		link_state.code[0] = 0;
		crypto_wipe(link_state.token, sizeof(link_state.token));
		if (!save_credential())
		{
			link_state.state = LINK_OFF;
			set_problem("cannot write %s in the data folder: the link is not kept (sv_unlink, then sv_link again)",
				LINK_FILE);
			pthread_mutex_unlock(&link_mutex);
			platform_log("Delta Control: %s", link_state.problem);
			return;
		}
		link_state.state = LINK_LINKED;
		link_state.problem[0] = 0;
		pthread_mutex_unlock(&link_mutex);
		server_audit("console", "", CONTROL_ROLE_OWNER, "link", owner, NULL, 1, NULL);
		platform_log("Delta Control: linked to %s's account (%s)", owner[0] ? owner : "the site", server_id);
		return;
	}
}

/* a signed request: its status, and the answer's payload (MAC checked) */
static int signed_request(const char *path, const char *payload, char *opened, size_t opened_size, char *error,
	int error_size)
{
	static char body[STATUS_SIZE * 7 + 4096];
	static char answer[ANSWER_SIZE];
	uint8_t nonce[CONTROL_LINK_NONCE_BYTES];
	uint8_t seed[CONTROL_LINK_SEED_BYTES];
	uint8_t secret[CONTROL_LINK_SECRET_BYTES];
	char server_id[CONTROL_LINK_SERVER_ID_SIZE];
	int status;
	int length;

	if (!random_bytes(nonce, sizeof(nonce)))
		return 0;
	pthread_mutex_lock(&link_mutex);
	memcpy(seed, link_state.seed, sizeof(seed));
	memcpy(secret, link_state.secret, sizeof(secret));
	snprintf(server_id, sizeof(server_id), "%s", link_state.server_id);
	pthread_mutex_unlock(&link_mutex);
	length = control_link_signed_body(path, server_id, seed, now_seconds(), nonce, payload, body, sizeof(body));
	crypto_wipe(seed, sizeof(seed));
	if (length < 0)
	{
		crypto_wipe(secret, sizeof(secret));
		snprintf(error, (size_t)error_size, "the request does not fit");
		return 0;
	}
	status = request(path, body, answer, sizeof(answer), error, error_size);
	if (status == 200 && !control_link_open_answer(answer, strlen(answer), secret, nonce, opened, opened_size))
	{
		snprintf(error, (size_t)error_size, "the site's answer was not signed with the link's secret: ignored");
		status = -1;
	}
	crypto_wipe(secret, sizeof(secret));
	return status;
}

/* a poll: the state and the results sent; the commands and roles taken */
static int run_poll(void)
{
	static char payload[STATUS_SIZE + MAXIMUM_PENDING * (RESULT_SIZE * 6 + 64) + 256];
	static char opened[ANSWER_SIZE];
	static struct control_link_poll poll;
	char error[256];
	size_t length;
	int index;
	int status;
	int sent_results;

	pthread_mutex_lock(&link_mutex);
	length = (size_t)snprintf(payload, sizeof(payload), "{\"status\": %s, \"results\": [",
		link_state.status[0] ? link_state.status : "null");
	for (index = 0; index < link_state.result_count && length < sizeof(payload); index++)
	{
		char output[RESULT_SIZE * 6 + 3];

		if (control_json_string(link_state.results[index].output, output, sizeof(output)) < 0)
			snprintf(output, sizeof(output), "\"\"");
		length += (size_t)snprintf(payload + length, sizeof(payload) - length, "%s{\"id\": %llu, \"ok\": %s, "
			"\"output\": %s}", index ? ", " : "", (unsigned long long)link_state.results[index].id,
			link_state.results[index].ok ? "true" : "false", output);
	}
	sent_results = link_state.result_count;
	if (length < sizeof(payload))
	{
		length += (size_t)snprintf(payload + length, sizeof(payload) - length, "], \"roles_version\": %llu, "
			"\"wait\": 0}", (unsigned long long)link_state.roles_version);
	}
	pthread_mutex_unlock(&link_mutex);
	if (length >= sizeof(payload))
		return 0;
	status = signed_request("/v1/control/poll", payload, opened, sizeof(opened), error, sizeof(error));
	pthread_mutex_lock(&link_mutex);
	if (status != 200)
	{
		set_problem(status > 0 ? "the site answered HTTP %d" : "%s", status > 0 ? status : 0);
		if (status <= 0)
			set_problem("%s", error);
		pthread_mutex_unlock(&link_mutex);
		return 0;
	}
	if (!control_link_parse_poll(opened, strlen(opened), &poll))
	{
		set_problem("the site's answer was not one the server takes");
		pthread_mutex_unlock(&link_mutex);
		return 0;
	}
	/* (the results sent are the site's now) */
	memmove(&link_state.results[0], &link_state.results[sent_results],
		(size_t)(link_state.result_count - sent_results) * sizeof(link_state.results[0]));
	link_state.result_count -= sent_results;
	link_state.heard = now_seconds();
	link_state.problem[0] = 0;
	if (poll.unlinked)
	{
		forget_credential();
		link_state.state = LINK_OFF;
		set_problem("unlinked on the site");
		pthread_mutex_unlock(&link_mutex);
		server_audit("site", "", CONTROL_ROLE_NONE, "unlink", NULL, "unlinked on the site", 1, NULL);
		platform_log("Delta Control: the server was unlinked on the site; its credential is deleted");
		return 1;
	}
	if (poll.has_roles)
	{
		link_state.roles_version = poll.roles_version;
		take_roles(&poll);
		platform_log("Delta Control: the site's role list, version %llu: %d accounts",
			(unsigned long long)poll.roles_version, poll.role_count);
	}
	for (index = 0; index < poll.command_count; index++)
	{
		if (link_state.command_count < MAXIMUM_PENDING)
			link_state.commands[link_state.command_count++] = poll.commands[index];
		else if (link_state.result_count < MAXIMUM_PENDING)
		{
			/* (too many waiting: answered so at once) */
			link_state.results[link_state.result_count].id = poll.commands[index].id;
			link_state.results[link_state.result_count].ok = 0;
			snprintf(link_state.results[link_state.result_count].output, RESULT_SIZE, "the server is busy: try again");
			link_state.result_count++;
		}
	}
	index = poll.poll_seconds;
	pthread_mutex_unlock(&link_mutex);
	return index;
}

static void *link_thread(void *argument)
{
	int retry = RETRY_SECONDS;

	(void)argument;
	for (;;)
	{
		int state;
		int unlink_pending;

		pthread_mutex_lock(&link_mutex);
		state = link_state.state;
		unlink_pending = link_state.unlink_pending;
		pthread_mutex_unlock(&link_mutex);
		if (unlink_pending)
		{
			/* the site told (with the credential kept for it), once */
			static char opened[4096];
			char error[256];
			char saved_id[CONTROL_LINK_SERVER_ID_SIZE];
			uint8_t saved_seed[CONTROL_LINK_SEED_BYTES];
			int status;

			pthread_mutex_lock(&link_mutex);
			snprintf(saved_id, sizeof(saved_id), "%s", link_state.server_id);
			memcpy(saved_seed, link_state.seed, sizeof(saved_seed));
			snprintf(link_state.server_id, sizeof(link_state.server_id), "%s", link_state.unlink_server_id);
			memcpy(link_state.seed, link_state.unlink_seed, sizeof(link_state.seed));
			pthread_mutex_unlock(&link_mutex);
			status = signed_request("/v1/control/unlink", "{}", opened, sizeof(opened), error, sizeof(error));
			pthread_mutex_lock(&link_mutex);
			snprintf(link_state.server_id, sizeof(link_state.server_id), "%s", saved_id);
			memcpy(link_state.seed, saved_seed, sizeof(link_state.seed));
			crypto_wipe(saved_seed, sizeof(saved_seed));
			crypto_wipe(link_state.unlink_seed, sizeof(link_state.unlink_seed));
			link_state.unlink_pending = 0;
			pthread_mutex_unlock(&link_mutex);
			platform_log("Delta Control: the site was told of the unlink (%s)", status == 200 ? "done" :
				"it did not answer: it unlinks the server when the server stops asking");
			continue;
		}
		if (state == LINK_STARTING)
		{
			run_linking();
			continue;
		}
		if (state == LINK_LINKED)
		{
			int seconds = run_poll();

			if (seconds > 0)
				retry = RETRY_SECONDS;
			else
			{
				seconds = retry;
				retry = retry * 2 > RETRY_MAXIMUM ? RETRY_MAXIMUM : retry * 2;
			}
			/* (at least every 3 seconds while commands come; the site may
			say less often) */
			sleep((unsigned int)(seconds < 3 ? 3 : seconds));
			continue;
		}
		/* (nothing to do: asleep until sv_link or sv_unlink) */
		pthread_mutex_lock(&link_mutex);
		while (link_state.state == LINK_OFF && !link_state.unlink_pending)
			pthread_cond_wait(&link_wake, &link_mutex);
		pthread_mutex_unlock(&link_mutex);
	}
	return NULL;
}

static int start_thread(void)
{
	pthread_t thread;
	pthread_attr_t attributes;
	int ok;

	if (link_state.started)
		return 1;
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&attributes, 1024 * 1024);
	ok = !pthread_create(&thread, &attributes, link_thread, NULL);
	pthread_attr_destroy(&attributes);
	link_state.started = ok;
	return ok;
}

/* ---------- public code */

void server_link_begin(void)
{
	const char *setting = getenv("HALO_DEDICATED_LINK");
	const char *url = getenv("HALO_DEDICATED_LINK_URL");
	const char *cap = getenv("HALO_DEDICATED_LINK_ROLE");

	pthread_mutex_lock(&link_mutex);
	if (setting && (!strcmp(setting, "0") || !strcasecmp(setting, "false") || !strcasecmp(setting, "no") ||
		!strcasecmp(setting, "off")))
	{
		link_state.disabled = 1;
		pthread_mutex_unlock(&link_mutex);
		platform_log("Delta Control: the link to halo.milenko.org is off (HALO_DEDICATED_LINK)");
		return;
	}
	if (!url || !url[0])
		url = getenv("HALO_NET_BROWSER");
	if (!url || !url[0])
		url = "https://halo.milenko.org";
	/* (HTTPS, or this machine for tests) */
	if (strncmp(url, "https://", 8) && strncmp(url, "http://127.0.0.1", 16) && strncmp(url, "http://localhost", 16))
	{
		link_state.disabled = 1;
		pthread_mutex_unlock(&link_mutex);
		platform_log("Delta Control: HALO_DEDICATED_LINK_URL is not an https:// address: the link is off");
		return;
	}
	snprintf(link_state.url, sizeof(link_state.url), "%s", url);
	while (strlen(link_state.url) && link_state.url[strlen(link_state.url) - 1] == '/')
		link_state.url[strlen(link_state.url) - 1] = 0;
	link_state.cap = CONTROL_ROLE_ADMIN;
	if (cap && cap[0])
	{
		int role = control_role_parse(cap);

		/* (moderator or admin: the site is never the owner here) */
		if (role == CONTROL_ROLE_MODERATOR || role == CONTROL_ROLE_ADMIN)
			link_state.cap = role;
		else
			platform_log("Delta Control: HALO_DEDICATED_LINK_ROLE is moderator or admin; admin it is");
	}
	if (load_credential())
	{
		link_state.state = LINK_LINKED;
		platform_log("Delta Control: linked to halo.milenko.org (%s, %s's); site roles at most %s", link_state.server_id,
			link_state.owner, control_role_name(link_state.cap));
		start_thread();
	}
	pthread_mutex_unlock(&link_mutex);
}

int server_link_start(const char *server_name, const char *version, char *text, int text_size)
{
	int ok = 0;

	pthread_mutex_lock(&link_mutex);
	if (link_state.disabled || !link_state.url[0])
		snprintf(text, (size_t)text_size, "the link to halo.milenko.org is off on this server (HALO_DEDICATED_LINK)");
	else if (link_state.state == LINK_LINKED)
		snprintf(text, (size_t)text_size, "linked already, to %s's account (sv_unlink first)", link_state.owner);
	else if (link_state.state != LINK_OFF)
	{
		if (link_state.code[0])
			snprintf(text, (size_t)text_size, "linking: sign in at %s/servers/link and enter %s", link_state.url,
				link_state.code);
		else
			snprintf(text, (size_t)text_size, "linking: asking the site for a code");
	}
	else if (!random_bytes(link_state.seed, sizeof(link_state.seed)) ||
		!random_bytes(link_state.secret, sizeof(link_state.secret)))
	{
		snprintf(text, (size_t)text_size, "no random bytes for the link's key");
	}
	else
	{
		snprintf(link_state.server_name, sizeof(link_state.server_name), "%s", server_name);
		snprintf(link_state.version, sizeof(link_state.version), "%s", version);
		link_state.state = LINK_STARTING;
		link_state.code[0] = 0;
		link_state.problem[0] = 0;
		if (!start_thread())
		{
			link_state.state = LINK_OFF;
			snprintf(text, (size_t)text_size, "the link's thread cannot start");
		}
		else
		{
			pthread_cond_signal(&link_wake);
			snprintf(text, (size_t)text_size, "asking %s for a code: it is printed on the server's console and in its "
				"log in a moment (sv_link_status shows it too); enter it at %s/servers/link, signed in", link_state.url,
				link_state.url);
			ok = 1;
		}
	}
	pthread_mutex_unlock(&link_mutex);
	return ok;
}

int server_link_stop(char *text, int text_size)
{
	int ok = 0;

	pthread_mutex_lock(&link_mutex);
	if (link_state.state == LINK_LINKED)
	{
		/* (the credential gone at once; the site told after, with a copy) */
		snprintf(link_state.unlink_server_id, sizeof(link_state.unlink_server_id), "%s", link_state.server_id);
		memcpy(link_state.unlink_seed, link_state.seed, sizeof(link_state.unlink_seed));
		link_state.unlink_pending = 1;
		forget_credential();
		link_state.state = LINK_OFF;
		pthread_cond_signal(&link_wake);
		snprintf(text, (size_t)text_size, "unlinked: the credential is deleted, and the site is told");
		ok = 1;
	}
	else if (link_state.state == LINK_WAITING || link_state.state == LINK_STARTING)
	{
		link_state.state = LINK_OFF;
		link_state.code[0] = 0;
		crypto_wipe(link_state.seed, sizeof(link_state.seed));
		crypto_wipe(link_state.secret, sizeof(link_state.secret));
		snprintf(text, (size_t)text_size, "linking stopped");
		ok = 1;
	}
	else
		snprintf(text, (size_t)text_size, "the server is not linked");
	pthread_mutex_unlock(&link_mutex);
	return ok;
}

void server_link_status(int json, char *text, int text_size)
{
	static const char *const names[] = { "off", "linking", "linking", "linked" };
	int64_t now = now_seconds();
	char problem[200];

	pthread_mutex_lock(&link_mutex);
	control_json_string(link_state.problem, problem, sizeof(problem));
	if (json)
	{
		snprintf(text, (size_t)text_size, "{\"state\": \"%s\", \"enabled\": %s, \"site\": \"%s\", \"server_id\": %s%s%s, "
			"\"owner\": %s%s%s, \"code\": %s%s%s, \"heard_seconds_ago\": %lld, \"role_cap\": \"%s\", \"problem\": %s}",
			link_state.disabled ? "off" : names[link_state.state], link_state.disabled ? "false" : "true", link_state.url,
			link_state.server_id[0] ? "\"" : "", link_state.server_id[0] ? link_state.server_id : "null",
			link_state.server_id[0] ? "\"" : "", link_state.owner[0] ? "\"" : "", link_state.owner[0] ? link_state.owner :
			"null", link_state.owner[0] ? "\"" : "", link_state.code[0] ? "\"" : "", link_state.code[0] ? link_state.code :
			"null", link_state.code[0] ? "\"" : "", link_state.heard ? (long long)(now - link_state.heard) : -1LL,
			control_role_name(link_state.cap ? link_state.cap : CONTROL_ROLE_ADMIN), link_state.problem[0] ? problem :
			"null");
	}
	else if (link_state.disabled)
		snprintf(text, (size_t)text_size, "the link to halo.milenko.org is off (HALO_DEDICATED_LINK)");
	else if (link_state.state == LINK_LINKED)
		snprintf(text, (size_t)text_size, "linked to %s's account on %s (%s); site roles at most %s; last heard %s%s%s",
			link_state.owner, link_state.url, link_state.server_id, control_role_name(link_state.cap),
			link_state.heard ? "a few seconds ago" : "not yet", link_state.problem[0] ? "; " : "", link_state.problem);
	else if (link_state.state == LINK_WAITING)
		snprintf(text, (size_t)text_size, "linking: sign in at %s/servers/link and enter %s", link_state.url,
			link_state.code);
	else if (link_state.state == LINK_STARTING)
		snprintf(text, (size_t)text_size, "linking: asking the site for a code");
	else
		snprintf(text, (size_t)text_size, "not linked (sv_link links it)%s%s", link_state.problem[0] ? ": " : "",
			link_state.problem);
	pthread_mutex_unlock(&link_mutex);
}

int server_link_next_command(char *line, int line_size, char *handle, int handle_size, unsigned int *id)
{
	int found = 0;

	pthread_mutex_lock(&link_mutex);
	while (link_state.command_count && !found)
	{
		struct control_link_command command = link_state.commands[0];
		const char *why = "";

		memmove(&link_state.commands[0], &link_state.commands[1],
			(size_t)(link_state.command_count - 1) * sizeof(link_state.commands[0]));
		link_state.command_count--;
		if (!control_link_command_line(command.command, command.reason, line, (size_t)line_size, &why))
		{
			/* (refused here: answered so, and audited) */
			if (link_state.result_count < MAXIMUM_PENDING)
			{
				link_state.results[link_state.result_count].id = command.id;
				link_state.results[link_state.result_count].ok = 0;
				snprintf(link_state.results[link_state.result_count].output, RESULT_SIZE, "refused: %s", why);
				link_state.result_count++;
			}
			pthread_mutex_unlock(&link_mutex);
			server_audit("site", command.actor, server_roles_site_handle_role(command.actor), "command", NULL, why, 0,
				NULL);
			pthread_mutex_lock(&link_mutex);
			continue;
		}
		snprintf(handle, (size_t)handle_size, "%s", command.actor);
		*id = (unsigned int)command.id;
		found = 1;
	}
	pthread_mutex_unlock(&link_mutex);
	return found;
}

void server_link_finish_command(unsigned int id, int ok, const char *output)
{
	pthread_mutex_lock(&link_mutex);
	if (link_state.result_count < MAXIMUM_PENDING)
	{
		link_state.results[link_state.result_count].id = id;
		link_state.results[link_state.result_count].ok = ok;
		snprintf(link_state.results[link_state.result_count].output, RESULT_SIZE, "%s", output);
		link_state.result_count++;
	}
	pthread_mutex_unlock(&link_mutex);
}

void server_link_set_status(const char *json)
{
	pthread_mutex_lock(&link_mutex);
	if (link_state.state == LINK_LINKED && strlen(json) < sizeof(link_state.status))
		snprintf(link_state.status, sizeof(link_state.status), "%s", json);
	pthread_mutex_unlock(&link_mutex);
}
