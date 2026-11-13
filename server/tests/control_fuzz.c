/*
CONTROL_FUZZ.C

libFuzzer's entry for the dedicated server's control API
(server/platform/control_protocol.c), as tools/test_host_fuzz.py builds it:
each input is the bytes a connection sent, parsed as the server does as they
arrive (whole, and cut at a point the input's last byte picks: an
incomplete request is asked again with more), then the parsed request's
body through the command, login and field parsers (each request's own
fields: the login's, the setup's and invitation's, the accounts', the
account's, the file's), its query through the log's, its headers through
the web page's checks (a browser's change, the file a path is), and the
input as a log line through the scrubber.

The input is also each line the server reads from a file or a stranger:
a command line (its words, its permission, a ban's duration, a map's
name), a credential's line, a moderator's line, the blocked names' list,
and the site's answers (a poll, the link's start and state, a command the
site relays as a command line, JSON as the link reads it).
*/

#include "../platform/control_protocol.h"
#include "../platform/control_web.h"
#include "../platform/control_roles.h"
#include "../platform/control_link_protocol.h"
#include "../src/command_line.h"
#include "../src/server_names.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* (tools/embed_webui.py's, for control_web.c: two files stand in) */
static const unsigned char page[] = "<!doctype html>";
const struct control_web_asset control_web_assets[] = {
	{ "/index.html", "text/html; charset=utf-8", page, sizeof(page) - 1 },
	{ "/app.js", "text/javascript; charset=utf-8", page, sizeof(page) - 1 },
};
const int control_web_asset_count = 2;

/* a body through the fields of one request kind (names, their out sizes,
the multiline ones) */
static void fields(const struct control_request *request, const char *const *names, const size_t *sizes, int count,
	unsigned int multiline)
{
	char *outs[8];
	const char *reason;
	int ok = 1;
	int index;

	for (index = 0; index < count; index++)
	{
		outs[index] = malloc(sizes[index]);
		ok &= outs[index] != NULL;
	}
	if (ok)
		control_parse_fields(request->body, request->content_length, names, outs, sizes, count, multiline, &reason);
	for (index = 0; index < count; index++)
		free(outs[index]);
}

static void parse_body(const struct control_request *request)
{
	/* (each request's fields, as server_control.c takes them) */
	static const char *const login[] = { "token", "user", "password", "code" };
	static const size_t login_sizes[] = { CONTROL_MAXIMUM_TOKEN, 40, 130, 16 };
	static const char *const setup[] = { "code", "user", "password" };
	static const size_t setup_sizes[] = { 44, 40, 130 };
	static const char *const accounts[] = { "user", "role", "id", "value" };
	static const size_t accounts_sizes[] = { 40, 16, 12, 16 };
	static const char *const password[] = { "password", "new_password" };
	static const size_t password_sizes[] = { 130, 130 };
	static const char *const totp[] = { "password", "code" };
	static const size_t totp_sizes[] = { 130, 16 };
	static const char *const bind[] = { "player", "request" };
	static const size_t bind_sizes[] = { 16, 16 };
	static const char *const file[] = { "kind", "name", "text" };
	static const size_t file_sizes[] = { 16, 40, CONTROL_MAXIMUM_BODY };
	static const char *const names[] = { "name", "text", "path" };
	static const size_t names_sizes[] = { 32, 512, 64 };
	char command[CONTROL_MAXIMUM_COMMAND];
	char token[CONTROL_MAXIMUM_TOKEN];
	const char *reason;
	uint64_t since;
	struct control_web_session session;
	int changes;

	if (request->body)
	{
		if (control_parse_command_body(request->body, request->content_length, command, sizeof(command), &reason))
			control_command_permission(command, &changes);
		control_parse_login_body(request->body, request->content_length, token, sizeof(token), &reason);
		fields(request, names, names_sizes, 3, 2);
		fields(request, login, login_sizes, 4, 0);
		fields(request, setup, setup_sizes, 3, 0);
		fields(request, accounts, accounts_sizes, 4, 0);
		fields(request, password, password_sizes, 2, 0);
		fields(request, totp, totp_sizes, 2, 0);
		fields(request, bind, bind_sizes, 2, 0);
		fields(request, file, file_sizes, 3, 1u << 2);
	}
	control_parse_log_query(request->query, &since);
	control_session_text_valid(request->session);
	control_token_valid(request->token);
	control_web_find_asset(request->path);
	control_host_allowed(request->host, "panel.example.org, 203.0.113.9:8443, [2001:db8::1]", 1);
	control_host_allowed(request->forwarded_host, request->host, 0);
	/* (a browser's change: with no session, and with one whose CSRF token
	is another's, then the request's own) */
	control_web_check_change(NULL, request, &reason);
	memset(&session, 0, sizeof(session));
	session.used = 1;
	memset(session.csrf, 'a', CONTROL_WEB_SECRET_LENGTH);
	control_web_check_change(&session, request, &reason);
	if (request->csrf[0])
	{
		memcpy(session.csrf, request->csrf, sizeof(session.csrf));
		session.csrf[CONTROL_WEB_SECRET_LENGTH] = 0;
		control_web_check_change(&session, request, &reason);
	}
}

/* the input as a line the server reads (text: NUL-terminated, length
bytes before it) */
static void parse_line(const char *text, size_t length)
{
	struct command_line words;
	struct control_credential credential;
	struct control_moderator moderator;
	char problem[96];
	char line[512];
	const char *reason;
	long seconds;
	long number;
	int changes;

	if (command_line_parse(text, &words, problem, sizeof(problem)))
	{
		int index;

		for (index = 0; index < words.count; index++)
		{
			command_line_duration(words.words[index], &seconds);
			command_line_integer(words.words[index], -1000, 1000, &number);
			command_line_map_name_valid(words.words[index]);
			command_line_server_name_valid(words.words[index]);
			command_line_name_match("Odb718", words.words[index]);
		}
	}
	control_command_permission(text, &changes);
	if (command_line_duration(text, &seconds))
	{
		char duration[32];

		command_line_duration_text(seconds, duration, sizeof(duration));
	}
	if (control_credential_parse(text, &credential))
		control_credential_line(&credential, line);
	if (control_moderator_parse(text, &moderator, &reason) > 0)
		control_moderator_line(&moderator, line, sizeof(line));
	control_display_name_valid(text);
	control_credential_name_valid(text);
	server_names_parse(text, length);
	server_names_blocked_text(text);
	server_names_clear();
	{
		/* (a command the site relays, its reason the input's second half) */
		size_t half = length / 2;
		char *command = malloc(half + 1);

		if (command)
		{
			memcpy(command, text, half);
			command[half] = 0;
			control_link_command_line(command, text + half, line, sizeof(line), &reason);
			free(command);
		}
	}
}

/* the input as the site's answers */
static void parse_site(const char *text, size_t length)
{
	static struct control_link_poll poll;
	struct control_json_token tokens[64];
	char code[CONTROL_LINK_CODE_SIZE];
	char token[CONTROL_LINK_TOKEN_SIZE];
	char state[16], server_id[CONTROL_LINK_SERVER_ID_SIZE], owner[CONTROL_LINK_HANDLE_SIZE];
	int expires;
	int count = control_json_parse(text, length, tokens, 64);

	if (count > 0 && tokens[0].type == CONTROL_JSON_OBJECT)
	{
		int member = control_json_member(text, tokens, 0, "state");
		char out[64];
		uint64_t value;
		int flag;

		if (member >= 0)
		{
			control_json_text(text, &tokens[member], out, sizeof(out));
			control_json_number(text, &tokens[member], &value);
			control_json_boolean(text, &tokens[member], &flag);
		}
	}
	memset(&poll, 0, sizeof(poll));
	control_link_parse_poll(text, length, &poll);
	control_link_parse_start(text, length, code, sizeof(code), token, sizeof(token), &expires);
	control_link_parse_status(text, length, state, sizeof(state), server_id, sizeof(server_id), owner, sizeof(owner));
	control_link_url_allowed(text);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct control_request request;
	const char *reason;
	char *copy;
	char *line;
	char scrubbed[512];
	int status;
	size_t cut;

	/* (the server's buffer: the bytes so far, in an allocation of their own
	so a read past them is caught) */
	copy = malloc(size ? size : 1);
	if (!copy)
		return 0;
	memcpy(copy, data, size);
	cut = size ? data[size - 1] % (size + 1) : 0;
	memset(&request, 0, sizeof(request));
	if (control_parse_request(copy, cut, &request, &status, &reason) == CONTROL_PARSE_DONE)
		parse_body(&request);
	memset(&request, 0, sizeof(request));
	if (control_parse_request(copy, size, &request, &status, &reason) == CONTROL_PARSE_DONE)
		parse_body(&request);

	/* (a body on its own, as the parsers take it) */
	control_parse_command_body(copy, size, scrubbed, sizeof(scrubbed), &reason);
	free(copy);

	/* (a line a client's text ends up in; a line the server reads; the
	site's answer) */
	line = malloc(size + 1);
	if (!line)
		return 0;
	memcpy(line, data, size);
	line[size] = 0;
	control_log_scrub(line, scrubbed, sizeof(scrubbed));
	if (size < 1024)
		parse_line(line, size);
	parse_site(line, size);
	free(line);
	return 0;
}
