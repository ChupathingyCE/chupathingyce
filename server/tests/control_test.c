/*
CONTROL_TEST.C

Tests of the dedicated server's command lines (server/src/command_line.c)
and its control API's requests, credentials, limits and log
(server/platform/control_protocol.c), and its web admin page's sessions,
checks, headers and files (server/platform/control_web.c, with the page's
files as tools/embed_webui.py embeds them): well-formed input, malformed,
oversized and cut-short input, and a few thousand random requests (built
with AddressSanitizer and UndefinedBehaviorSanitizer where the compiler has
them). Built and run by tools/test_server_control.py; exits nonzero on a
failure.
*/

#include "../src/command_line.h"
#include "../platform/control_protocol.h"
#include "../platform/control_web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(condition) \
	do \
	{ \
		checks++; \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

/* a small xorshift, so a failure's input can be found again */
static unsigned long long random_state = 0x9e3779b97f4a7c15ULL;

static unsigned int random_next(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 7;
	random_state ^= random_state << 17;
	return (unsigned int)(random_state >> 11);
}

/* ---------- command lines */

static void test_command_line(void)
{
	struct command_line line;
	char error[96];
	char long_line[400];
	long value;

	CHECK(command_line_parse("sv_kick 3", &line, error, sizeof(error)));
	CHECK(line.count == 2 && !strcmp(line.words[0], "sv_kick") && !strcmp(line.words[1], "3"));
	CHECK(command_line_parse("  sv_ban\t\"Master Chief\"  2h ", &line, error, sizeof(error)));
	CHECK(line.count == 3 && !strcmp(line.words[1], "Master Chief") && !strcmp(line.words[2], "2h"));
	CHECK(command_line_parse("sv_name \"a \\\"q\\\" \\\\b\"", &line, error, sizeof(error)));
	CHECK(line.count == 2 && !strcmp(line.words[1], "a \"q\" \\b"));
	CHECK(command_line_parse("", &line, error, sizeof(error)) && line.count == 0);
	CHECK(command_line_parse("   ", &line, error, sizeof(error)) && line.count == 0);
	CHECK(command_line_parse("# a comment", &line, error, sizeof(error)) && line.count == 0);
	CHECK(command_line_parse(NULL, &line, error, sizeof(error)) && line.count == 0);
	CHECK(command_line_parse("\"\"", &line, error, sizeof(error)) && line.count == 1 && !line.words[0][0]);
	/* refused */
	CHECK(!command_line_parse("sv_kick \"open", &line, error, sizeof(error)) && line.count == 0 && error[0]);
	CHECK(!command_line_parse("sv_kick a\"b", &line, error, sizeof(error)));
	CHECK(!command_line_parse("sv_kick \"a\"b", &line, error, sizeof(error)));
	CHECK(!command_line_parse("sv_kick \x01", &line, error, sizeof(error)));
	CHECK(!command_line_parse("sv_kick \xc3\xa9", &line, error, sizeof(error)));
	CHECK(!command_line_parse("sv_kick x\ny", &line, error, sizeof(error)));
	CHECK(!command_line_parse("a b c d e f g h i", &line, error, sizeof(error)));
	CHECK(command_line_parse("a b c d e f g h", &line, error, sizeof(error)) && line.count == 8);
	memset(long_line, 'x', sizeof(long_line));
	long_line[COMMAND_LINE_MAXIMUM_LENGTH] = 0;
	CHECK(!command_line_parse(long_line, &line, error, sizeof(error)));
	long_line[COMMAND_LINE_MAXIMUM_LENGTH - 1] = 0;
	/* (one word longer than a word may be) */
	CHECK(!command_line_parse(long_line, &line, error, sizeof(error)));
	long_line[COMMAND_LINE_WORD_SIZE - 1] = 0;
	CHECK(command_line_parse(long_line, &line, error, sizeof(error)) && line.count == 1);
	long_line[COMMAND_LINE_WORD_SIZE - 1] = 'x';
	long_line[COMMAND_LINE_WORD_SIZE] = 0;
	CHECK(!command_line_parse(long_line, &line, error, sizeof(error)));
	/* (a tiny error buffer is not overrun) */
	CHECK(!command_line_parse("\"", &line, error, 1) && error[0] == 0);

	CHECK(command_line_integer("12", 1, 128, &value) && value == 12);
	CHECK(command_line_integer("-5", -10, 10, &value) && value == -5);
	CHECK(!command_line_integer("0", 1, 128, &value));
	CHECK(!command_line_integer("129", 1, 128, &value));
	CHECK(!command_line_integer("12a", 1, 128, &value));
	CHECK(!command_line_integer("", 1, 128, &value));
	CHECK(!command_line_integer("-", -1, 1, &value));
	CHECK(!command_line_integer("+3", 1, 128, &value));
	CHECK(!command_line_integer("9999999999999999999", 1, 128, &value));

	CHECK(command_line_duration("30", &value) && value == 30 * 60);
	CHECK(command_line_duration("90s", &value) && value == 90);
	CHECK(command_line_duration("2h", &value) && value == 7200);
	CHECK(command_line_duration("1d12h", &value) && value == 36 * 3600);
	CHECK(command_line_duration("1w", &value) && value == 7 * 86400);
	CHECK(command_line_duration("forever", &value) && value == 0);
	CHECK(command_line_duration("3H", &value) && value == 3 * 3600);
	CHECK(!command_line_duration("", &value));
	CHECK(!command_line_duration("0", &value));
	CHECK(!command_line_duration("0m", &value));
	CHECK(!command_line_duration("h", &value));
	CHECK(!command_line_duration("1d12", &value));
	CHECK(!command_line_duration("2y", &value));
	CHECK(!command_line_duration("-2h", &value));
	CHECK(!command_line_duration("999999999w", &value));
	CHECK(!command_line_duration("600w", &value));
	CHECK(command_line_duration("520w", &value));
	CHECK(!command_line_duration("520w520w", &value));
	{
		char text[32];

		command_line_duration_text(0, text, sizeof(text));
		CHECK(!strcmp(text, "forever"));
		command_line_duration_text(2 * 86400 + 3 * 3600 + 5, text, sizeof(text));
		CHECK(!strcmp(text, "2d 3h"));
		command_line_duration_text(45 * 60, text, sizeof(text));
		CHECK(!strcmp(text, "45m"));
		command_line_duration_text(2 * 86400 + 3 * 3600, text, 3);
		CHECK(strlen(text) <= 2);
	}

	CHECK(command_line_name_match("Milenko", "milenko") == 2);
	CHECK(command_line_name_match("Milenko", "MIL") == 1);
	CHECK(command_line_name_match("Milenko", "Milenkos") == 0);
	CHECK(command_line_name_match("Milenko", "") == 0);
	CHECK(command_line_name_match("Milenko", "x") == 0);

	CHECK(command_line_map_name_valid("bloodgulch"));
	CHECK(command_line_map_name_valid("timberland@ce"));
	CHECK(command_line_map_name_valid("phoenix3_15@md"));
	CHECK(command_line_map_name_valid("a-b.c"));
	CHECK(!command_line_map_name_valid(""));
	CHECK(!command_line_map_name_valid("@ce"));
	CHECK(!command_line_map_name_valid("../ui"));
	CHECK(!command_line_map_name_valid("..@ce"));
	CHECK(!command_line_map_name_valid("levels\\test\\x"));
	CHECK(!command_line_map_name_valid("a/b"));
	CHECK(!command_line_map_name_valid("a@xx"));
	CHECK(!command_line_map_name_valid("a b"));

	CHECK(command_line_server_name_valid("[D] Slayer"));
	CHECK(command_line_server_name_valid("123456789012345"));
	CHECK(!command_line_server_name_valid("1234567890123456"));
	CHECK(!command_line_server_name_valid(""));
	CHECK(!command_line_server_name_valid("   "));
	CHECK(!command_line_server_name_valid("a\tb"));

	{
		char buffer[16];
		struct command_output output;

		command_output_begin(&output, buffer, sizeof(buffer));
		command_output_printf(&output, "%s", "0123456789");
		CHECK(!output.truncated && output.length == 10);
		command_output_printf(&output, "%s", "abcdefghij");
		CHECK(output.truncated && output.length == 15 && strlen(buffer) == 15);
		command_output_begin(&output, buffer, sizeof(buffer));
		command_output_json_string(&output, "a\"\\\n\x80");
		CHECK(!strncmp(buffer, "\"a\\\"\\\\\\u000a", 12));
		CHECK(output.truncated);
	}
}

/* every command line random bytes make is parsed or refused, never overrun */
static void fuzz_command_line(int rounds)
{
	int round;

	for (round = 0; round < rounds; round++)
	{
		char text[300];
		struct command_line line;
		char error[64];
		int length = (int)(random_next() % (sizeof(text) - 1));
		int index;
		long value;

		for (index = 0; index < length; index++)
		{
			unsigned int pick = random_next() % 8;

			text[index] = pick == 0 ? '"' : pick == 1 ? ' ' : pick == 2 ? '\\' : (char)(random_next() % 256);
			if (!text[index])
				text[index] = 'z';
		}
		text[length] = 0;
		if (command_line_parse(text, &line, error, sizeof(error)))
		{
			CHECK(line.count >= 0 && line.count <= COMMAND_LINE_MAXIMUM_WORDS);
			for (index = 0; index < line.count; index++)
				CHECK(strlen(line.words[index]) < COMMAND_LINE_WORD_SIZE);
		}
		command_line_duration(text, &value);
		command_line_integer(text, -100, 100, &value);
		command_line_map_name_valid(text);
	}
}

/* ---------- requests */

static int parse(const char *text, struct control_request *request, int *status)
{
	const char *reason;

	return control_parse_request(text, strlen(text), request, status, &reason);
}

static void test_requests(void)
{
	struct control_request request;
	int status;
	char big[CONTROL_MAXIMUM_HEAD + 64];
	const char *reason;
	char command[CONTROL_MAXIMUM_COMMAND];
	uint64_t since;

	CHECK(parse("GET /v1/status HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer abc_123\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_DONE);
	CHECK(request.method == CONTROL_METHOD_GET && !strcmp(request.path, "/v1/status") && !request.query[0]);
	CHECK(request.authorization == CONTROL_AUTHORIZATION_BEARER && !strcmp(request.token, "abc_123"));
	CHECK(parse("GET /v1/log?since=42 HTTP/1.0\r\n\r\n", &request, &status) == CONTROL_PARSE_DONE);
	CHECK(!strcmp(request.path, "/v1/log") && !strcmp(request.query, "since=42"));
	CHECK(parse("GET /v1/status HTTP/1.1\r\nhost: x\r\nauthorization: bearer t0k3n==\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_DONE && request.authorization == CONTROL_AUTHORIZATION_BEARER);
	CHECK(parse("POST /v1/command HTTP/1.1\r\nHost: x\r\nContent-Type: application/json; charset=UTF-8\r\n"
		"Content-Length: 24\r\n\r\n{\"command\": \"sv_status\"}", &request, &status) == CONTROL_PARSE_DONE);
	CHECK(request.method == CONTROL_METHOD_POST && request.json_body && request.content_length == 24 &&
		!memcmp(request.body, "{\"command\"", 10));
	/* cut short: more needed */
	CHECK(parse("GET /v1/status HTTP/1.1\r\nHost: x\r\n", &request, &status) == CONTROL_PARSE_INCOMPLETE);
	CHECK(parse("GET /v1/sta", &request, &status) == CONTROL_PARSE_INCOMPLETE);
	CHECK(parse("", &request, &status) == CONTROL_PARSE_INCOMPLETE);
	CHECK(parse("POST /v1/command HTTP/1.1\r\nHost: x\r\nContent-Length: 25\r\n\r\n{\"comm", &request, &status) ==
		CONTROL_PARSE_INCOMPLETE);
	/* refused */
	CHECK(parse("GET /v1/status HTTP/1.1\r\n\r\n", &request, &status) == CONTROL_PARSE_ERROR && status == 400);
	CHECK(parse("GET /v1/status HTTP/1.1\nHost: x\n\n", &request, &status) == CONTROL_PARSE_ERROR && status == 400);
	CHECK(parse("GET /v1/status HTTP/1.1\r\nHost: x\rY: z\r\n\r\n", &request, &status) == CONTROL_PARSE_ERROR);
	CHECK(parse("DELETE /v1/status HTTP/1.1\r\nHost: x\r\n\r\n", &request, &status) == CONTROL_PARSE_ERROR &&
		status == 405);
	CHECK(parse("get /v1/status HTTP/1.1\r\nHost: x\r\n\r\n", &request, &status) == CONTROL_PARSE_ERROR);
	CHECK(parse("GET  /v1/status HTTP/1.1\r\nHost: x\r\n\r\n", &request, &status) == CONTROL_PARSE_ERROR);
	CHECK(parse("GET v1/status HTTP/1.1\r\nHost: x\r\n\r\n", &request, &status) == CONTROL_PARSE_ERROR);
	CHECK(parse("GET /v1/%73tatus HTTP/1.1\r\nHost: x\r\n\r\n", &request, &status) == CONTROL_PARSE_ERROR);
	CHECK(parse("GET /v1/status#x HTTP/1.1\r\nHost: x\r\n\r\n", &request, &status) == CONTROL_PARSE_ERROR);
	CHECK(parse("GET /v1/status HTTP/2.0\r\nHost: x\r\n\r\n", &request, &status) == CONTROL_PARSE_ERROR &&
		status == 505);
	CHECK(parse("GET /v1/status HTTP/1.1 \r\nHost: x\r\n\r\n", &request, &status) == CONTROL_PARSE_ERROR);
	CHECK(parse("GET /v1/status\r\nHost: x\r\n\r\n", &request, &status) == CONTROL_PARSE_ERROR);
	CHECK(parse("GET /v1/status HTTP/1.1\r\nHost : x\r\n\r\n", &request, &status) == CONTROL_PARSE_ERROR);
	CHECK(parse("GET /v1/status HTTP/1.1\r\nHost: x\r\n folded\r\n\r\n", &request, &status) == CONTROL_PARSE_ERROR);
	CHECK(parse("GET /v1/status HTTP/1.1\r\nHost: x\r\nHost: y\r\n\r\n", &request, &status) == CONTROL_PARSE_ERROR);
	CHECK(parse("GET /v1/status HTTP/1.1\r\nHost: x\r\nX: \x80\r\n\r\n", &request, &status) == CONTROL_PARSE_ERROR);
	{
		/* (a NUL in the head) */
		static const char nul[] = "GET /v1/status HTTP/1.1\r\nHost: x\0\r\n\r\n";

		CHECK(control_parse_request(nul, sizeof(nul) - 1, &request, &status, &reason) == CONTROL_PARSE_ERROR);
	}
	CHECK(parse("GET /v1/status HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_ERROR && status == 501);
	CHECK(parse("POST /v1/command HTTP/1.1\r\nHost: x\r\nExpect: 100-continue\r\nContent-Length: 2\r\n\r\n{}",
		&request, &status) == CONTROL_PARSE_ERROR && status == 417);
	CHECK(parse("POST /v1/command HTTP/1.1\r\nHost: x\r\n\r\n", &request, &status) == CONTROL_PARSE_ERROR &&
		status == 411);
	CHECK(parse("POST /v1/command HTTP/1.1\r\nHost: x\r\nContent-Length: 4097\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_ERROR && status == 413);
	CHECK(parse("POST /v1/command HTTP/1.1\r\nHost: x\r\nContent-Length: 99999999999\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_ERROR && status == 413);
	CHECK(parse("POST /v1/command HTTP/1.1\r\nHost: x\r\nContent-Length: -1\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_ERROR && status == 400);
	CHECK(parse("POST /v1/command HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\nx", &request,
		&status) == CONTROL_PARSE_ERROR);
	CHECK(parse("POST /v1/command HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\n\r\nxy", &request, &status) ==
		CONTROL_PARSE_ERROR);
	CHECK(parse("GET /v1/status HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\n\r\nx", &request, &status) ==
		CONTROL_PARSE_ERROR);
	CHECK(parse("GET /v1/status HTTP/1.1\r\nHost: x\r\n\r\nGET /v1/status HTTP/1.1\r\n", &request, &status) ==
		CONTROL_PARSE_ERROR);
	/* authorization of other kinds, and tokens too long */
	CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\nAuthorization: Basic YTpi\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_DONE && request.authorization == CONTROL_AUTHORIZATION_OTHER);
	CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer  two\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_DONE && request.authorization == CONTROL_AUTHORIZATION_OTHER);
	CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer a=b\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_DONE && request.authorization == CONTROL_AUTHORIZATION_OTHER);
	CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_DONE && request.authorization == CONTROL_AUTHORIZATION_OTHER);
	CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer a\r\nAuthorization: Bearer b\r\n\r\n",
		&request, &status) == CONTROL_PARSE_ERROR);
	{
		char text[512];

		snprintf(text, sizeof(text), "GET / HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer %0200d\r\n\r\n", 7);
		CHECK(parse(text, &request, &status) == CONTROL_PARSE_DONE &&
			request.authorization == CONTROL_AUTHORIZATION_OTHER && !request.token[0]);
	}
	/* too large: the head, the target, the headers */
	memset(big, 'a', sizeof(big));
	memcpy(big, "GET /", 5);
	big[sizeof(big) - 1] = 0;
	CHECK(parse(big, &request, &status) == CONTROL_PARSE_ERROR && status == 431);
	{
		char text[1024];

		snprintf(text, sizeof(text), "GET /%0300d HTTP/1.1\r\nHost: x\r\n\r\n", 1);
		CHECK(parse(text, &request, &status) == CONTROL_PARSE_ERROR && status == 414);
	}
	{
		char text[4096] = "GET / HTTP/1.1\r\nHost: x\r\n";
		int index;

		for (index = 0; index < CONTROL_MAXIMUM_HEADERS; index++)
			strcat(text, "X-A: b\r\n");
		strcat(text, "\r\n");
		CHECK(parse(text, &request, &status) == CONTROL_PARSE_ERROR && status == 431);
	}
	/* content types */
	CHECK(parse("POST /v1/command HTTP/1.1\r\nHost: x\r\nContent-Type: text/plain\r\nContent-Length: 0\r\n\r\n",
		&request, &status) == CONTROL_PARSE_DONE && !request.json_body);
	CHECK(parse("POST /v1/command HTTP/1.1\r\nHost: x\r\nContent-Type: application/jsonx\r\nContent-Length: 0\r\n"
		"\r\n", &request, &status) == CONTROL_PARSE_DONE && !request.json_body);
	CHECK(parse("POST /v1/command HTTP/1.1\r\nHost: x\r\nContent-Type: application/json;charset=latin1\r\n"
		"Content-Length: 0\r\n\r\n", &request, &status) == CONTROL_PARSE_DONE && !request.json_body);

	/* bodies */
	CHECK(control_parse_command_body("{\"command\": \"sv_kick 3\"}", 24, command, sizeof(command), &reason));
	CHECK(!strcmp(command, "sv_kick 3"));
	{
		static const char body[] = " {\n\"command\":\"sv_name \\\"x\\\" \\u0041\\/\"} \n";

		CHECK(control_parse_command_body(body, sizeof(body) - 1, command, sizeof(command), &reason) &&
			!strcmp(command, "sv_name \"x\" A/"));
	}
#define BODY_REFUSED(text) CHECK(!control_parse_command_body(text, strlen(text), command, sizeof(command), &reason))
	BODY_REFUSED("");
	BODY_REFUSED("{}");
	BODY_REFUSED("[]");
	BODY_REFUSED("{\"command\": 3}");
	BODY_REFUSED("{\"command\": \"a\", \"x\": \"b\"}");
	BODY_REFUSED("{\"x\": \"b\"}");
	BODY_REFUSED("{\"command\": \"a\"");
	BODY_REFUSED("{\"command\": \"a}");
	BODY_REFUSED("{\"command\": \"a\"} x");
	BODY_REFUSED("{\"command\": \"a\\nb\"}");
	BODY_REFUSED("{\"command\": \"a\\u0000b\"}");
	BODY_REFUSED("{\"command\": \"a\\u00e9\"}");
	BODY_REFUSED("{\"command\": \"\xc3\xa9\"}");
	BODY_REFUSED("{\"command\": \"a\x01\"}");
	BODY_REFUSED("{\"command\": \"a\\u12\"}");
	BODY_REFUSED("{\"command\": \"a\\q\"}");
	BODY_REFUSED("{\"command\" \"a\"}");
	BODY_REFUSED("{\"command\": \"a\"}{}");
	{
		char text[600] = "{\"command\": \"";

		memset(text + 13, 'x', 300);
		strcpy(text + 313, "\"}");
		BODY_REFUSED(text);
	}
	{
		/* (a NUL inside the body) */
		static const char nul[] = "{\"command\": \"a\0b\"}";

		CHECK(!control_parse_command_body(nul, sizeof(nul) - 1, command, sizeof(command), &reason));
	}

	CHECK(control_parse_log_query("", &since) && since == 0);
	CHECK(control_parse_log_query("since=17", &since) && since == 17);
	CHECK(!control_parse_log_query("since=", &since));
	CHECK(!control_parse_log_query("since=-1", &since));
	CHECK(!control_parse_log_query("since=1&x=2", &since));
	CHECK(!control_parse_log_query("until=1", &since));
	CHECK(!control_parse_log_query("since=9999999999999999999999", &since));

	{
		char out[64];

		CHECK(control_json_string("a\"b\\c\nd\x01\x7f\xff", out, sizeof(out)) > 0);
		CHECK(!strcmp(out, "\"a\\\"b\\\\c\\nd\\u0001\\u007f?\""));
		CHECK(control_json_string("abcdef", out, 7) < 0);
		CHECK(control_json_string("abcde", out, 8) == 7);
	}
}

/* random requests: parsed, waited on or refused, never overrun; then a
real request with random bytes changed, and cut at every length */
static void fuzz_requests(int rounds)
{
	static const char *const pieces[] = {
		"GET ", "POST ", "/v1/status", "/v1/command", "?since=", " HTTP/1.1", " HTTP/1.0", "\r\n", "\r\n\r\n",
		"Host: x", "Content-Length: ", "12", "Authorization: Bearer ", "chce_00", "Content-Type: application/json",
		"Transfer-Encoding: chunked", ": ", " ", "\t", "\n", "\r", "{\"command\": \"sv_status\"}", "\"", "\\u0041",
		"\x00", "\xff", "%", "#", "/v1/login", "/v1/session", "/v1/logout", "/", "/index.html", "/../", "Cookie: ",
		"chce_session=", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", "; ", "X-CSRF-Token: ",
		"Origin: http://x", "Origin: null", "Sec-Fetch-Site: cross-site", "X-Forwarded-Proto: https",
		"X-Forwarded-Host: y", "Forwarded: for=1;proto=https", "X-Background: 1", "{\"token\": \"chce_\"}",
	};
	static const char good[] = "POST /v1/command HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer chce_ab\r\n"
		"Content-Type: application/json\r\nContent-Length: 24\r\n\r\n{\"command\": \"sv_kick 3\"}";
	int round;

	for (round = 0; round < rounds; round++)
	{
		size_t size = 1 + random_next() % (CONTROL_MAXIMUM_HEAD + CONTROL_MAXIMUM_BODY + 64);
		/* (heap, sized exactly: AddressSanitizer catches a read past it) */
		char *data = malloc(size);
		size_t length = 0;
		struct control_request request;
		int status;
		const char *reason;
		int result;
		char command[CONTROL_MAXIMUM_COMMAND];

		while (length < size)
		{
			if (random_next() % 4)
			{
				const char *piece = pieces[random_next() % (sizeof(pieces) / sizeof(pieces[0]))];
				size_t piece_length = piece[0] ? strlen(piece) : 1;

				if (piece_length > size - length)
					piece_length = size - length;
				memcpy(data + length, piece, piece_length);
				length += piece_length;
			}
			else
				data[length++] = (char)(random_next() % 256);
			if (!(random_next() % 64))
				break;
		}
		result = control_parse_request(data, length, &request, &status, &reason);
		CHECK(result == CONTROL_PARSE_INCOMPLETE || result == CONTROL_PARSE_DONE || result == CONTROL_PARSE_ERROR);
		if (result == CONTROL_PARSE_ERROR)
			CHECK(status >= 400 && status < 600 && reason && reason[0]);
		if (result == CONTROL_PARSE_DONE)
		{
			CHECK(request.path[0] == '/' && strlen(request.path) < CONTROL_MAXIMUM_TARGET);
			CHECK(strlen(request.token) < CONTROL_MAXIMUM_TOKEN);
			CHECK(!request.session[0] || control_session_text_valid(request.session));
			CHECK(!request.csrf[0] || control_session_text_valid(request.csrf));
			CHECK(strlen(request.host) < sizeof(request.host) && strlen(request.origin) < sizeof(request.origin));
			CHECK(strlen(request.forwarded_host) < sizeof(request.forwarded_host));
			/* (a file is one of the page's, by its whole path, or none) */
			{
				const struct control_web_asset *asset = control_web_find_asset(request.path);

				CHECK(!asset || !strcmp(asset->path, request.path) || !strcmp(request.path, "/"));
				CHECK(!asset || (!strstr(request.path, "..") && !strstr(request.path, "//")));
			}
			if (request.body)
			{
				char token[CONTROL_MAXIMUM_TOKEN];

				control_parse_login_body(request.body, request.content_length, token, sizeof(token), &reason);
				CHECK(request.body + request.content_length == data + length);
				if (control_parse_command_body(request.body, request.content_length, command, sizeof(command),
					&reason))
				{
					size_t index;

					for (index = 0; command[index]; index++)
						CHECK((command[index] >= 0x20 && command[index] < 0x7f) || command[index] == '\t');
				}
			}
		}
		free(data);

		/* a good request with a few bytes changed, cut short */
		{
			size_t good_length = sizeof(good) - 1;
			size_t cut = random_next() % (good_length + 1);
			char *copy = malloc(cut ? cut : 1);
			int changes = (int)(random_next() % 4);

			memcpy(copy, good, cut);
			while (cut && changes--)
				copy[random_next() % cut] = (char)(random_next() % 256);
			result = control_parse_request(copy, cut, &request, &status, &reason);
			if (cut == good_length && changes < 0 && result == CONTROL_PARSE_DONE)
				CHECK(control_parse_command_body(request.body, request.content_length, command, sizeof(command),
					&reason) || 1);
			free(copy);
		}
		/* random command bodies */
		{
			size_t body_length = random_next() % 400;
			char *body = malloc(body_length ? body_length : 1);
			size_t index;

			for (index = 0; index < body_length; index++)
				body[index] = (random_next() % 3) ? "{}\":\\ u0aZcommand"[random_next() % 17] : (char)random_next();
			control_parse_command_body(body, body_length, command, sizeof(command), &reason);
			free(body);
		}
	}
	/* the good request whole, at every length: never done early */
	{
		size_t cut;
		size_t good_length = sizeof(good) - 1;

		for (cut = 0; cut < good_length; cut++)
		{
			char *copy = malloc(cut ? cut : 1);
			struct control_request request;
			int status;
			const char *reason;

			memcpy(copy, good, cut);
			CHECK(control_parse_request(copy, cut, &request, &status, &reason) == CONTROL_PARSE_INCOMPLETE);
			free(copy);
		}
	}
}

/* ---------- credentials and limits */

static void test_credentials(void)
{
	uint8_t bytes[CONTROL_TOKEN_BYTES];
	uint8_t id[CONTROL_ID_LENGTH / 2] = { 0x1a, 0x2b, 0x3c, 0x4d };
	uint8_t salt[CONTROL_SALT_BYTES];
	char token[CONTROL_TOKEN_LENGTH + 1];
	char other[CONTROL_TOKEN_LENGTH + 1];
	char line[CONTROL_CREDENTIAL_LINE];
	struct control_credential credential, parsed;
	int index;

	for (index = 0; index < CONTROL_TOKEN_BYTES; index++)
		bytes[index] = (uint8_t)(index * 7 + 1);
	for (index = 0; index < CONTROL_SALT_BYTES; index++)
		salt[index] = (uint8_t)(255 - index);
	control_token_text(bytes, token);
	CHECK(strlen(token) == CONTROL_TOKEN_LENGTH && !strncmp(token, "chce_", 5));
	CHECK(control_token_valid(token));
	bytes[0] ^= 1;
	control_token_text(bytes, other);
	CHECK(control_token_valid(other) && strcmp(token, other));
	CHECK(!control_token_valid("chce_"));
	CHECK(!control_token_valid(""));
	{
		char upper[CONTROL_TOKEN_LENGTH + 1];

		strcpy(upper, token);
		upper[10] = upper[10] >= 'a' ? (char)(upper[10] - 32) : 'A';
		CHECK(!control_token_valid(upper));
		strcpy(upper, token);
		upper[0] = 'x';
		CHECK(!control_token_valid(upper));
	}

	/* (a small cost: the test's, not the server's) */
	CHECK(control_credential_make(token, "admin", id, salt, 64, 1, &credential));
	CHECK(!strcmp(credential.id, "1a2b3c4d"));
	CHECK(control_credential_check(&credential, token) == 1);
	CHECK(control_credential_check(&credential, other) == 0);
	CHECK(control_credential_check(&credential, "") == 0);
	control_credential_line(&credential, line);
	CHECK(!strncmp(line, "v1 argon2id admin 1a2b3c4d 64 1 ", 32));
	CHECK(control_credential_parse(line, &parsed));
	CHECK(!memcmp(&parsed, &credential, sizeof(parsed)));
	CHECK(control_credential_check(&parsed, token) == 1);
	{
		char text[CONTROL_CREDENTIAL_LINE + 8];

		snprintf(text, sizeof(text), "%s\n", line);
		CHECK(control_credential_parse(text, &parsed));
		snprintf(text, sizeof(text), "%s x", line);
		CHECK(!control_credential_parse(text, &parsed));
		snprintf(text, sizeof(text), "%s", line);
		text[strlen(text) - 1] = 0;
		CHECK(!control_credential_parse(text, &parsed));
		CHECK(!control_credential_parse("v1 argon2id admin 1a2b3c4d 64 1", &parsed));
		CHECK(!control_credential_parse("v2 argon2id admin 1a2b3c4d 64 1 00 00", &parsed));
		CHECK(!control_credential_parse("", &parsed));
		/* (costs out of bounds: a tampered file cannot make every check take
		gigabytes) */
		snprintf(text, sizeof(text), "%s", line);
		memcpy(text + 27, "0", 1);
		CHECK(!control_credential_parse(text, &parsed));
		snprintf(text, sizeof(text), "v1 argon2id admin 1a2b3c4d 999999999 1 %s", line + 32);
		CHECK(!control_credential_parse(text, &parsed));
		snprintf(text, sizeof(text), "v1 argon2id admin 1a2b3c4d 64 0 %s", line + 32);
		CHECK(!control_credential_parse(text, &parsed));
		snprintf(text, sizeof(text), "v1 argon2id ad\"min 1a2b3c4d 64 1 %s", line + 32);
		CHECK(!control_credential_parse(text, &parsed));
		snprintf(text, sizeof(text), "v1  argon2id admin 1a2b3c4d 64 1 %s", line + 32);
		CHECK(!control_credential_parse(text, &parsed));
	}
	/* random lines: refused or parsed, never overrun */
	for (index = 0; index < 2000; index++)
	{
		char text[300];
		int length = (int)(random_next() % 299);
		int position;

		snprintf(text, sizeof(text), "%s", line);
		for (position = 0; position < 1 + (int)(random_next() % 3); position++)
			text[random_next() % (unsigned int)strlen(line)] = (char)(random_next() % 256);
		if (random_next() % 2)
			text[length < (int)strlen(text) ? length : (int)strlen(text)] = 0;
		control_credential_parse(text, &parsed);
	}
}

static void test_limiter(void)
{
	struct control_limiter limiter;
	uint8_t one[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 192, 0, 2, 1 };
	uint8_t two[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 192, 0, 2, 2 };
	int64_t retry;
	int index;
	int taken = 0;

	control_limiter_initialize(&limiter);
	CHECK(control_limiter_allowed(&limiter, one, 1000, &retry));
	for (index = 0; index < CONTROL_LIMITER_FAILURES - 1; index++)
		control_limiter_failed(&limiter, one, 1000);
	CHECK(control_limiter_allowed(&limiter, one, 1000, &retry));
	control_limiter_failed(&limiter, one, 1001);
	CHECK(!control_limiter_allowed(&limiter, one, 1001, &retry) && retry == CONTROL_LIMITER_BLOCK_SECONDS);
	/* (another address is not) */
	CHECK(control_limiter_allowed(&limiter, two, 1001, &retry));
	/* (a right token does not lift a block early) */
	control_limiter_succeeded(&limiter, one);
	CHECK(!control_limiter_allowed(&limiter, one, 1100, &retry));
	CHECK(control_limiter_allowed(&limiter, one, 1001 + CONTROL_LIMITER_BLOCK_SECONDS, &retry));
	/* failures spread out past the window do not block */
	for (index = 0; index < 20; index++)
		control_limiter_failed(&limiter, two, 5000 + index * (CONTROL_LIMITER_WINDOW_SECONDS / 2));
	CHECK(control_limiter_allowed(&limiter, two, 5000 + 20 * (CONTROL_LIMITER_WINDOW_SECONDS / 2), &retry));
	/* the checks for everyone: a burst, then so many a minute */
	for (index = 0; index < 100; index++)
		taken += control_limiter_take_check(&limiter, 9000);
	CHECK(taken == CONTROL_LIMITER_CHECK_BURST);
	CHECK(!control_limiter_take_check(&limiter, 9000));
	CHECK(control_limiter_take_check(&limiter, 9002));
	taken = 0;
	for (index = 0; index < 100; index++)
		taken += control_limiter_take_check(&limiter, 9062);
	CHECK(taken == CONTROL_LIMITER_CHECK_BURST);
	/* many addresses: the table is not overrun, and the blocked stay so */
	control_limiter_initialize(&limiter);
	for (index = 0; index < CONTROL_LIMITER_FAILURES; index++)
		control_limiter_failed(&limiter, one, 100);
	for (index = 0; index < 1000; index++)
	{
		uint8_t address[16] = { 0x20, 0x01, 0x0d, 0xb8 };

		address[14] = (uint8_t)(index >> 8);
		address[15] = (uint8_t)index;
		control_limiter_failed(&limiter, address, 150);
	}
	CHECK(!control_limiter_allowed(&limiter, one, 200, &retry));
}

/* ---------- the log */

static void test_log(void)
{
	char out[256];

	control_log_scrub("joined from 8.8.8.8:2302 and 10.0.0.2\r\n", out, sizeof(out));
	CHECK(!strcmp(out, "joined from addr#hidden:2302 and 10.0.0.2"));
	control_log_scrub("127.0.0.1 192.168.1.5 100.64.0.1 172.16.0.1 172.32.0.1", out, sizeof(out));
	CHECK(!strcmp(out, "127.0.0.1 192.168.1.5 100.64.0.1 172.16.0.1 addr#hidden"));
	control_log_scrub("10.04.26 12:34:56  version 01.10.12.2276 v1.2.3.4 1.2.3.4.5", out, sizeof(out));
	CHECK(!strcmp(out, "10.04.26 12:34:56  version 01.10.12.2276 v1.2.3.4 1.2.3.4.5"));
	control_log_scrub("peer [2001:db8::1]:2302 and [::1]:5 and fe80::1 and 2001:db8:0:0:0:0:0:2", out, sizeof(out));
	CHECK(!strcmp(out, "peer [addr#hidden]:2302 and [::1]:5 and fe80::1 and addr#hidden"));
	control_log_scrub("addr#3f2a9c joined; a\x01" "b\xc3\xa9", out, sizeof(out));
	CHECK(!strcmp(out, "addr#3f2a9c joined; a?b??"));
	control_log_scrub("1.2.3.4", out, 5);
	CHECK(strlen(out) == 4);
	control_log_scrub("x", out, 1);
	CHECK(out[0] == 0);
	/* random lines: never overrun, never an address left */
	{
		int round;

		for (round = 0; round < 3000; round++)
		{
			char line[200];
			int length = (int)(random_next() % 199);
			int index;

			for (index = 0; index < length; index++)
				line[index] = "0123456789.:abcdef[] x"[random_next() % 22];
			line[length] = 0;
			control_log_scrub(line, out, 1 + random_next() % sizeof(out));
			CHECK(strlen(out) < sizeof(out));
		}
	}
}

/* ---------- the web page */

static void test_web_requests(void)
{
	struct control_request request;
	int status;
	char token[CONTROL_MAXIMUM_TOKEN];
	const char *reason;
	static const char id[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

	/* the session cookie, among others; the CSRF header; the browser's */
	CHECK(parse("GET /v1/status HTTP/1.1\r\nHost: 127.0.0.1:8080\r\nCookie: a=1; chce_session="
		"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef; b=2\r\nX-CSRF-Token: "
		"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\r\nOrigin: http://127.0.0.1:8080\r\n"
		"Sec-Fetch-Site: same-origin\r\nX-Background: 1\r\n\r\n", &request, &status) == CONTROL_PARSE_DONE);
	CHECK(!strcmp(request.session, id) && !strcmp(request.csrf, id));
	CHECK(!strcmp(request.host, "127.0.0.1:8080") && request.has_origin &&
		!strcmp(request.origin, "http://127.0.0.1:8080"));
	CHECK(!request.cross_site && !request.proxied && !request.https && request.background);
	CHECK(request.authorization == CONTROL_AUTHORIZATION_NONE);
	/* a cookie in a header of its own, as a proxy may split them */
	CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\nCookie: a=1\r\nCookie: chce_session="
		"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_DONE && !strcmp(request.session, id));
	/* none if it is there twice (another site's page may plant one), not
	the form, or another cookie's name */
	CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\nCookie: chce_session="
		"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef; chce_session="
		"1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_DONE && !request.session[0]);
	CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\nCookie: chce_session=0123\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_DONE && !request.session[0]);
	CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\nCookie: chce_session="
		"0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_DONE && !request.session[0]);
	CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\nCookie: CHCE_SESSION="
		"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef; xchce_session="
		"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_DONE && !request.session[0]);
	CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\nX-CSRF-Token: nope\r\n\r\n", &request, &status) == CONTROL_PARSE_DONE &&
		!request.csrf[0]);
	/* twice is refused */
	CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\nX-CSRF-Token: a\r\nX-CSRF-Token: b\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_ERROR && status == 400);
	CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\nOrigin: http://x\r\norigin: http://x\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_ERROR && status == 400);
	/* a reverse proxy's: HTTPS, its host */
	CHECK(parse("GET / HTTP/1.1\r\nHost: 127.0.0.1:8080\r\nX-Forwarded-Proto: https\r\nX-Forwarded-Host: "
		"admin.example.org, other\r\n\r\n", &request, &status) == CONTROL_PARSE_DONE);
	CHECK(request.proxied && request.https && !strcmp(request.forwarded_host, "admin.example.org"));
	CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\nForwarded: for=192.0.2.1;proto=https;host=a\r\n\r\n", &request,
		&status) == CONTROL_PARSE_DONE && request.proxied && request.https);
	CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\nForwarded: for=192.0.2.1;proto=http, proto=https\r\n\r\n", &request,
		&status) == CONTROL_PARSE_DONE && request.proxied && !request.https);
	CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\nX-Forwarded-For: 192.0.2.1\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_DONE && request.proxied && !request.https);
	CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\nSec-Fetch-Site: cross-site\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_DONE && request.cross_site);
	CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\nSec-Fetch-Site: none\r\n\r\n", &request, &status) ==
		CONTROL_PARSE_DONE && !request.cross_site);

	/* a login's body */
	CHECK(control_parse_login_body("{\"token\": \"chce_ab\"}", 20, token, sizeof(token), &reason) &&
		!strcmp(token, "chce_ab"));
	CHECK(!control_parse_login_body("{\"command\": \"x\"}", 16, token, sizeof(token), &reason) && !token[0]);
	CHECK(!control_parse_login_body("{\"token\": \"a\", \"b\": 1}", 22, token, sizeof(token), &reason));
	CHECK(!control_parse_login_body("{}", 2, token, sizeof(token), &reason) && !strcmp(reason, "no token"));
	CHECK(!control_parse_login_body("{\"token\": \"\\u0000\"}", 19, token, sizeof(token), &reason));
	CHECK(control_credential_name_valid("admin") && control_credential_name_valid("ops-2.eu"));
	CHECK(!control_credential_name_valid("") && !control_credential_name_valid("a b") &&
		!control_credential_name_valid("a\"b") && !control_credential_name_valid("0123456789012345678901234567890x"));
}

/* whether bytes hold needle */
static int contains(const void *bytes, size_t size, const void *needle, size_t length)
{
	size_t index;

	for (index = 0; index + length <= size; index++)
	{
		if (!memcmp((const char *)bytes + index, needle, length))
			return 1;
	}
	return 0;
}

static void make_secret(uint8_t bytes[CONTROL_WEB_SECRET_BYTES], int seed)
{
	int index;

	for (index = 0; index < CONTROL_WEB_SECRET_BYTES; index++)
		bytes[index] = (uint8_t)(seed * 31 + index * 7);
}

static void test_sessions(void)
{
	static struct control_web_sessions sessions;
	uint8_t key[32] = { 1, 2, 3 };
	uint8_t id[CONTROL_WEB_SECRET_BYTES], csrf[CONTROL_WEB_SECRET_BYTES];
	char text[CONTROL_WEB_SECRET_LENGTH + 1];
	char other[CONTROL_WEB_SECRET_LENGTH + 1];
	char first[CONTROL_WEB_SECRET_LENGTH + 1];
	int index, session;
	struct control_request request;
	const char *reason;

	control_web_sessions_initialize(&sessions, key);
	make_secret(id, 1);
	make_secret(csrf, 2);
	session = control_web_session_create(&sessions, id, csrf, "admin", "e76c2bf3", 1000, text);
	CHECK(session >= 0 && control_session_text_valid(text));
	CHECK(control_session_text_valid(sessions.entries[session].csrf) && strcmp(sessions.entries[session].csrf, text));
	/* (only its hash is kept) */
	CHECK(!contains(&sessions.entries[session], sizeof(sessions.entries[session]), text, CONTROL_WEB_SECRET_LENGTH));
	CHECK(control_web_session_find(&sessions, text, 1001, 0) == session);
	/* another id, a malformed one, an empty one: none */
	snprintf(other, sizeof(other), "%s", text);
	other[0] = other[0] == 'a' ? 'b' : 'a';
	CHECK(control_web_session_find(&sessions, other, 1001, 0) < 0);
	CHECK(control_web_session_find(&sessions, "", 1001, 0) < 0);
	CHECK(control_web_session_find(&sessions, "zz", 1001, 0) < 0);
	/* idle: used just before the limit lives on; polling alone does not
	keep it */
	CHECK(control_web_session_find(&sessions, text, 1001 + CONTROL_WEB_IDLE_SECONDS - 1, 0) == session);
	CHECK(control_web_session_find(&sessions, text, 1001 + 2 * CONTROL_WEB_IDLE_SECONDS - 10, 1) == session);
	CHECK(control_web_session_find(&sessions, text, 1001 + 2 * CONTROL_WEB_IDLE_SECONDS, 1) < 0);
	/* (and once ended, it is forgotten) */
	CHECK(!sessions.entries[session].used);
	CHECK(control_web_session_find(&sessions, text, 1002 + 2 * CONTROL_WEB_IDLE_SECONDS, 0) < 0);
	/* its whole life, however busy */
	session = control_web_session_create(&sessions, id, csrf, "admin", "e76c2bf3", 5000, text);
	for (index = 1; index * 600 < CONTROL_WEB_LIFETIME_SECONDS; index++)
		CHECK(control_web_session_find(&sessions, text, 5000 + index * 600, 0) == session);
	CHECK(control_web_session_find(&sessions, text, 5000 + CONTROL_WEB_LIFETIME_SECONDS, 0) < 0);
	/* logout */
	session = control_web_session_create(&sessions, id, csrf, "admin", "e76c2bf3", 100000, text);
	control_web_session_end(&sessions, session);
	CHECK(control_web_session_find(&sessions, text, 100001, 0) < 0);
	control_web_session_end(&sessions, -1);
	control_web_session_end(&sessions, CONTROL_WEB_SESSIONS);
	/* a credential's sessions all end with it, no one else's */
	control_web_sessions_initialize(&sessions, key);
	make_secret(id, 10);
	control_web_session_create(&sessions, id, csrf, "a", "aaaaaaaa", 100, text);
	make_secret(id, 11);
	control_web_session_create(&sessions, id, csrf, "a", "aaaaaaaa", 100, other);
	make_secret(id, 12);
	control_web_session_create(&sessions, id, csrf, "b", "bbbbbbbb", 100, first);
	CHECK(control_web_sessions_end_credential(&sessions, "aaaaaaaa") == 2);
	CHECK(control_web_session_find(&sessions, text, 101, 0) < 0 && control_web_session_find(&sessions, other, 101, 0) < 0);
	CHECK(control_web_session_find(&sessions, first, 101, 0) >= 0);
	/* full: the least recently used goes */
	control_web_sessions_initialize(&sessions, key);
	for (index = 0; index < CONTROL_WEB_SESSIONS; index++)
	{
		make_secret(id, 100 + index);
		control_web_session_create(&sessions, id, csrf, "a", "aaaaaaaa", 200 + index, index ? text : first);
	}
	CHECK(control_web_session_find(&sessions, first, 300, 0) >= 0);
	make_secret(id, 999);
	control_web_session_create(&sessions, id, csrf, "a", "aaaaaaaa", 301, other);
	CHECK(control_web_session_find(&sessions, first, 302, 0) >= 0);
	CHECK(control_web_session_find(&sessions, other, 302, 0) >= 0);

	/* a change: its CSRF token, its origin, not another site's */
	control_web_sessions_initialize(&sessions, key);
	make_secret(id, 5);
	session = control_web_session_create(&sessions, id, csrf, "admin", "e76c2bf3", 10, text);
	CHECK(parse("POST /v1/command HTTP/1.1\r\nHost: 127.0.0.1:8080\r\nContent-Length: 0\r\n\r\n", &request, &index) ==
		CONTROL_PARSE_DONE);
	CHECK(control_web_check_change(&sessions.entries[session], &request, &reason) == 403 && reason[0]);
	snprintf(request.csrf, sizeof(request.csrf), "%s", sessions.entries[session].csrf);
	CHECK(control_web_check_change(&sessions.entries[session], &request, &reason) == 0);
	request.csrf[5] = request.csrf[5] == 'a' ? 'b' : 'a';
	CHECK(control_web_check_change(&sessions.entries[session], &request, &reason) == 403);
	snprintf(request.csrf, sizeof(request.csrf), "%s", text);
	CHECK(control_web_check_change(&sessions.entries[session], &request, &reason) == 403);
	snprintf(request.csrf, sizeof(request.csrf), "%s", sessions.entries[session].csrf);
	request.has_origin = 1;
	snprintf(request.origin, sizeof(request.origin), "http://127.0.0.1:8080");
	CHECK(control_web_check_change(&sessions.entries[session], &request, &reason) == 0);
	snprintf(request.origin, sizeof(request.origin), "https://127.0.0.1:8080");
	CHECK(control_web_check_change(&sessions.entries[session], &request, &reason) == 0);
	snprintf(request.origin, sizeof(request.origin), "http://evil.example");
	CHECK(control_web_check_change(&sessions.entries[session], &request, &reason) == 403);
	snprintf(request.origin, sizeof(request.origin), "null");
	CHECK(control_web_check_change(&sessions.entries[session], &request, &reason) == 403);
	snprintf(request.origin, sizeof(request.origin), "http://127.0.0.1:8080.evil.example");
	CHECK(control_web_check_change(&sessions.entries[session], &request, &reason) == 403);
	snprintf(request.origin, sizeof(request.origin), "https://admin.example.org");
	snprintf(request.forwarded_host, sizeof(request.forwarded_host), "admin.example.org");
	CHECK(control_web_check_change(&sessions.entries[session], &request, &reason) == 0);
	request.cross_site = 1;
	CHECK(control_web_check_change(&sessions.entries[session], &request, &reason) == 403);
	/* a login: no session, the same origin checks */
	request.cross_site = 0;
	CHECK(control_web_check_change(NULL, &request, &reason) == 0);
	snprintf(request.origin, sizeof(request.origin), "http://evil.example");
	CHECK(control_web_check_change(NULL, &request, &reason) == 403);
	request.host[0] = 0;
	request.forwarded_host[0] = 0;
	snprintf(request.origin, sizeof(request.origin), "http://");
	CHECK(control_web_check_change(NULL, &request, &reason) == 403);
}

static void test_web_headers(void)
{
	char cookie[256];
	const char *headers = control_web_security_headers();
	static const char id[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

	control_web_session_cookie(cookie, sizeof(cookie), id, 0);
	CHECK(!strncmp(cookie, "Set-Cookie: chce_session=0123456789abcdef", 41));
	CHECK(strstr(cookie, "; HttpOnly") && strstr(cookie, "; SameSite=Strict") && strstr(cookie, "; Path=/"));
	CHECK(!strstr(cookie, "Secure") && strstr(cookie, "Max-Age=43200"));
	CHECK(!strcmp(cookie + strlen(cookie) - 2, "\r\n"));
	control_web_session_cookie(cookie, sizeof(cookie), id, 1);
	CHECK(strstr(cookie, "; Secure\r\n"));
	control_web_session_cookie(cookie, sizeof(cookie), NULL, 0);
	CHECK(strstr(cookie, "chce_session=;") && strstr(cookie, "Max-Age=0") && strstr(cookie, "HttpOnly"));
	CHECK(strstr(headers, "Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self';"));
	CHECK(strstr(headers, "frame-ancestors 'none'") && !strstr(headers, "unsafe"));
	CHECK(strstr(headers, "X-Frame-Options: DENY\r\n") && strstr(headers, "Referrer-Policy: no-referrer\r\n"));
	CHECK(strstr(headers, "Cache-Control: no-store\r\n") && strstr(headers, "X-Content-Type-Options: nosniff\r\n"));
}

static void test_web_files(void)
{
	static const char *const outside[] = {
		"", "/v1/status", "/index.htm", "/INDEX.HTML", "/index.html/", "//index.html", "/./index.html",
		"/../index.html", "/x/../index.html", "/index.html/..", "/..", "/.", "/app.js.map", "/app", "/server_control.c",
		"/control_credentials.txt", "/bans.txt", "/etc/passwd", "/../../../../etc/passwd", "index.html", "\\index.html",
		"/index.html\\", "/index.html%00",
	};
	const struct control_web_asset *page = control_web_find_asset("/");
	int index;
	int found_js = 0, found_css = 0;

	CHECK(page && !strcmp(page->path, "/index.html") && !strncmp(page->type, "text/html", 9));
	CHECK(control_web_find_asset("/index.html") == page);
	for (index = 0; index < (int)(sizeof(outside) / sizeof(outside[0])); index++)
		CHECK(!control_web_find_asset(outside[index]));
	for (index = 0; index < control_web_asset_count; index++)
	{
		const struct control_web_asset *asset = &control_web_assets[index];

		CHECK(control_web_find_asset(asset->path) == asset && asset->size > 0 && asset->data);
		CHECK(asset->path[0] == '/' && !strchr(asset->path + 1, '/') && !strstr(asset->path, ".."));
		found_js += !strcmp(asset->path, "/app.js") && !strncmp(asset->type, "text/javascript", 15);
		found_css += !strcmp(asset->path, "/app.css") && !strncmp(asset->type, "text/css", 8);
	}
	CHECK(found_js == 1 && found_css == 1);
	/* (the page loads its script and style as files, none inline) */
	{
		char *text = calloc(1, page->size + 1);

		memcpy(text, page->data, page->size);
		CHECK(strstr(text, "<script src=\"/app.js\"") && strstr(text, "href=\"/app.css\""));
		CHECK(!strstr(text, "<style") && !strstr(text, " style=") && !strstr(text, "onclick"));
		free(text);
	}
}

int main(int argc, char **argv)
{
	int rounds = argc > 1 ? atoi(argv[1]) : 5000;

	test_command_line();
	fuzz_command_line(rounds);
	test_requests();
	fuzz_requests(rounds);
	test_credentials();
	test_limiter();
	test_log();
	test_web_requests();
	test_sessions();
	test_web_headers();
	test_web_files();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
