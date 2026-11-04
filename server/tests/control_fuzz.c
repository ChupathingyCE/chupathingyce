/*
CONTROL_FUZZ.C

libFuzzer's entry for the dedicated server's control API
(server/platform/control_protocol.c), as tools/test_host_fuzz.py builds it:
each input is the bytes a connection sent, parsed as the server does as they
arrive (whole, and cut at a point the input's last byte picks: an
incomplete request is asked again with more), then the parsed request's
body through the command, login and field parsers, its query through the
log's, and the input as a log line through the scrubber.
*/

#include "../platform/control_protocol.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void parse_body(const struct control_request *request)
{
	static const char *const names[] = {"name", "text", "path"};
	char command[CONTROL_MAXIMUM_COMMAND];
	char token[CONTROL_MAXIMUM_TOKEN];
	char name[32], text[512], path[64];
	char *outs[] = {name, text, path};
	const size_t sizes[] = {sizeof(name), sizeof(text), sizeof(path)};
	const char *reason;
	uint64_t since;

	if (request->body)
	{
		control_parse_command_body(request->body, request->content_length, command, sizeof(command), &reason);
		control_parse_login_body(request->body, request->content_length, token, sizeof(token), &reason);
		control_parse_fields(request->body, request->content_length, names, outs, sizes, 3, 2, &reason);
	}
	control_parse_log_query(request->query, &since);
	control_session_text_valid(request->session);
	control_token_valid(request->token);
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

	/* (a line a client's text ends up in) */
	line = malloc(size + 1);
	if (!line)
		return 0;
	memcpy(line, data, size);
	line[size] = 0;
	control_log_scrub(line, scrubbed, sizeof(scrubbed));
	free(line);
	return 0;
}
