/*
CONTROL_PROTOCOL.C

The dedicated server's control API as bytes (control_protocol.h). Built
with the host's ABI, as server_control.c, which uses it.
*/

#include "control_protocol.h"

#include "monocypher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- private code */

static int lower(int character)
{
	return character >= 'A' && character <= 'Z' ? character - 'A' + 'a' : character;
}

/* whether a header's name (length bytes) is name, in either case */
static int name_is(const char *text, size_t length, const char *name)
{
	size_t index;

	if (strlen(name) != length)
		return 0;
	for (index = 0; index < length; index++)
	{
		if (lower((unsigned char)text[index]) != lower((unsigned char)name[index]))
			return 0;
	}
	return 1;
}

/* RFC 9110's tchar: a header's name's, a method's */
static int is_tchar(int character)
{
	return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
		(character >= '0' && character <= '9') || (character && strchr("!#$%&'*+-.^_`|~", character));
}

/* a bearer token's characters (RFC 6750's b64token), and = at its end */
static int is_token68(int character)
{
	return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
		(character >= '0' && character <= '9') || (character && strchr("-._~+/", character));
}

static int hex_value(int character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return -1;
}

/* exactly count bytes of lowercase hexadecimal: 1, else 0 */
static int hex_bytes(const char *text, size_t length, uint8_t *bytes, size_t count)
{
	size_t index;

	if (length != 2 * count)
		return 0;
	for (index = 0; index < count; index++)
	{
		int high = text[2 * index] >= 'A' && text[2 * index] <= 'F' ? -1 : hex_value((unsigned char)text[2 * index]);
		int low = text[2 * index + 1] >= 'A' && text[2 * index + 1] <= 'F' ? -1 :
			hex_value((unsigned char)text[2 * index + 1]);

		if (high < 0 || low < 0)
			return 0;
		bytes[index] = (uint8_t)(high << 4 | low);
	}
	return 1;
}

static int fail(int *status, const char **reason, int code, const char *text)
{
	*status = code;
	*reason = text;
	return CONTROL_PARSE_ERROR;
}

/* the Authorization header's value: "Bearer <token>" */
static void parse_authorization(const char *value, size_t length, struct control_request *request)
{
	size_t token_length;
	size_t index;

	request->authorization = CONTROL_AUTHORIZATION_OTHER;
	if (length < 8 || !name_is(value, 6, "bearer") || value[6] != ' ')
		return;
	value += 7;
	token_length = length - 7;
	if (!token_length || token_length >= CONTROL_MAXIMUM_TOKEN)
		return;
	for (index = 0; index < token_length && is_token68((unsigned char)value[index]); index++)
		;
	while (index < token_length && value[index] == '=')
		index++;
	if (index != token_length)
		return;
	memcpy(request->token, value, token_length);
	request->token[token_length] = 0;
	request->authorization = CONTROL_AUTHORIZATION_BEARER;
}

/* the Content-Type header's value: whether it is JSON's (application/json,
with ;charset=utf-8 if anything) */
static int content_type_is_json(const char *value, size_t length)
{
	size_t index;

	if (length < 16 || !name_is(value, 16, "application/json"))
		return 0;
	if (length == 16)
		return 1;
	index = 16;
	while (index < length && (value[index] == ' ' || value[index] == '\t'))
		index++;
	if (index >= length || value[index] != ';')
		return 0;
	index++;
	while (index < length && (value[index] == ' ' || value[index] == '\t'))
		index++;
	return length - index == 13 && name_is(value + index, 13, "charset=utf-8");
}

/* text (length bytes) into out (size bytes) if it fits: 1, else 0 (out
empty) */
static int copy_value(const char *text, size_t length, char *out, size_t size)
{
	if (length >= size)
	{
		out[0] = 0;
		return 0;
	}
	memcpy(out, text, length);
	out[length] = 0;
	return 1;
}

/* whether text (length bytes) is a session id's form: 64 lowercase hex
digits */
static int session_form(const char *text, size_t length)
{
	uint8_t bytes[CONTROL_SESSION_BYTES];

	return length == CONTROL_SESSION_LENGTH && hex_bytes(text, length, bytes, sizeof(bytes));
}

/* a Cookie header's value: "a=1; chce_session=<id>; b=2". The session's id
if it is there once and well formed; seen counts it across Cookie headers
(a second one, as another site's page could plant, leaves none) */
static void parse_cookie(const char *value, size_t length, struct control_request *request, int *seen)
{
	size_t index = 0;

	while (index < length)
	{
		size_t start, equals, end;

		while (index < length && (value[index] == ' ' || value[index] == ';'))
			index++;
		start = index;
		while (index < length && value[index] != ';')
			index++;
		end = index;
		while (end > start && value[end - 1] == ' ')
			end--;
		for (equals = start; equals < end && value[equals] != '='; equals++)
			;
		if (equals >= end || !name_is(value + start, equals - start, "chce_session") ||
			memcmp(value + start, "chce_session", 12))
		{
			continue;
		}
		if (++*seen == 1 && session_form(value + equals + 1, end - equals - 1))
			copy_value(value + equals + 1, end - equals - 1, request->session, sizeof(request->session));
		else
			request->session[0] = 0;
	}
}

/* the first of a list's values ("https, http": "https"), its length */
static size_t first_value(const char *value, size_t length)
{
	size_t index = 0;

	while (index < length && value[index] != ',')
		index++;
	while (index > 0 && (value[index - 1] == ' ' || value[index - 1] == '\t'))
		index--;
	return index;
}

/* whether a Forwarded header's first element says proto=https */
static int forwarded_https(const char *value, size_t length)
{
	size_t end = first_value(value, length);
	size_t index = 0;

	while (index < end)
	{
		size_t start;

		while (index < end && (value[index] == ' ' || value[index] == ';'))
			index++;
		start = index;
		while (index < end && value[index] != ';')
			index++;
		if (index - start >= 11 && name_is(value + start, 6, "proto=") &&
			(name_is(value + start + 6, index - start - 6, "https") ||
			name_is(value + start + 6, index - start - 6, "\"https\"")))
		{
			return 1;
		}
	}
	return 0;
}

/* ---------- requests */

int control_parse_request(const char *data, size_t length, struct control_request *request, int *status,
	const char **reason)
{
	size_t head_length = 0;
	size_t index;
	size_t line_end;
	size_t cursor;
	int version_minor;
	int header_count = 0;
	int have_host = 0, have_length = 0, have_authorization = 0, have_type = 0;
	int have_csrf = 0, have_forwarded_host = 0, sessions_seen = 0;
	size_t scan = length < CONTROL_MAXIMUM_HEAD ? length : CONTROL_MAXIMUM_HEAD;

	memset(request, 0, sizeof(*request));
	*status = 0;
	*reason = "";
	/* the head's end (an empty line), within its limit; what came so far
	plainly no head refused at once */
	for (index = 0; index < scan; index++)
	{
		unsigned char character = (unsigned char)data[index];

		if (character == '\n' && (index == 0 || data[index - 1] != '\r'))
			return fail(status, reason, 400, "a line ends without CR LF");
		if (index > 0 && data[index - 1] == '\r' && character != '\n')
			return fail(status, reason, 400, "a CR without LF");
		if (character == 0 || character >= 0x7f || (character < 0x20 && character != '\r' && character != '\n' &&
			character != '\t'))
		{
			return fail(status, reason, 400, "a character a request's head may not have");
		}
		if (index >= 3 && !memcmp(data + index - 3, "\r\n\r\n", 4))
		{
			head_length = index + 1;
			break;
		}
	}
	if (!head_length)
	{
		if (length >= CONTROL_MAXIMUM_HEAD)
			return fail(status, reason, 431, "the request's head is too large");
		return CONTROL_PARSE_INCOMPLETE;
	}

	/* the request line: method, target, version, a space between each */
	line_end = (size_t)((const char *)memchr(data, '\r', head_length) - data);
	for (index = 0; index < line_end && is_tchar((unsigned char)data[index]); index++)
		;
	if (!index || index > 16 || index >= line_end || data[index] != ' ')
		return fail(status, reason, 400, "no method");
	if (index == 3 && !memcmp(data, "GET", 3))
		request->method = CONTROL_METHOD_GET;
	else if (index == 4 && !memcmp(data, "POST", 4))
		request->method = CONTROL_METHOD_POST;
	else
		return fail(status, reason, 405, "only GET and POST");
	cursor = index + 1;
	for (index = cursor; index < line_end && data[index] != ' '; index++)
	{
		unsigned char character = (unsigned char)data[index];

		if (character < 0x21 || character > 0x7e || character == '#' || character == '%')
			return fail(status, reason, 400, "a character a target may not have");
	}
	if (index == cursor || data[cursor] != '/')
		return fail(status, reason, 400, "the target is no path");
	if (index - cursor >= CONTROL_MAXIMUM_TARGET)
		return fail(status, reason, 414, "the target is too long");
	{
		const char *question = memchr(data + cursor, '?', index - cursor);
		size_t path_length = question ? (size_t)(question - (data + cursor)) : index - cursor;

		memcpy(request->path, data + cursor, path_length);
		request->path[path_length] = 0;
		if (question)
		{
			size_t query_length = index - cursor - path_length - 1;

			memcpy(request->query, question + 1, query_length);
			request->query[query_length] = 0;
		}
	}
	if (index >= line_end || data[index] != ' ')
		return fail(status, reason, 400, "no version");
	cursor = index + 1;
	if (line_end - cursor != 8 || memcmp(data + cursor, "HTTP/1.", 7) ||
		(data[cursor + 7] != '0' && data[cursor + 7] != '1'))
	{
		if (line_end - cursor >= 5 && !memcmp(data + cursor, "HTTP/", 5))
			return fail(status, reason, 505, "only HTTP/1.0 and 1.1");
		return fail(status, reason, 400, "no version");
	}
	version_minor = data[cursor + 7] - '0';

	/* the headers, each "name: value" on a line of its own */
	cursor = line_end + 2;
	while (cursor < head_length - 2)
	{
		size_t name_length;
		size_t value_start, value_end;

		line_end = (size_t)((const char *)memchr(data + cursor, '\r', head_length - cursor) - data);
		if (++header_count > CONTROL_MAXIMUM_HEADERS)
			return fail(status, reason, 431, "too many headers");
		for (index = cursor; index < line_end && is_tchar((unsigned char)data[index]); index++)
			;
		name_length = index - cursor;
		if (!name_length || index >= line_end || data[index] != ':')
			return fail(status, reason, 400, "a header with no name, or space before its colon");
		value_start = index + 1;
		while (value_start < line_end && (data[value_start] == ' ' || data[value_start] == '\t'))
			value_start++;
		value_end = line_end;
		while (value_end > value_start && (data[value_end - 1] == ' ' || data[value_end - 1] == '\t'))
			value_end--;
		/* (a header's value is visible ASCII, spaces and tabs: the head's
		characters were checked above) */
		if (name_is(data + cursor, name_length, "host"))
		{
			if (have_host++ || value_end == value_start)
				return fail(status, reason, 400, "Host twice, or empty");
			copy_value(data + value_start, value_end - value_start, request->host, sizeof(request->host));
		}
		else if (name_is(data + cursor, name_length, "cookie"))
		{
			parse_cookie(data + value_start, value_end - value_start, request, &sessions_seen);
		}
		else if (name_is(data + cursor, name_length, "x-csrf-token"))
		{
			if (have_csrf++)
				return fail(status, reason, 400, "X-CSRF-Token twice");
			if (session_form(data + value_start, value_end - value_start))
				copy_value(data + value_start, value_end - value_start, request->csrf, sizeof(request->csrf));
		}
		else if (name_is(data + cursor, name_length, "origin"))
		{
			if (request->has_origin++)
				return fail(status, reason, 400, "Origin twice");
			copy_value(data + value_start, value_end - value_start, request->origin, sizeof(request->origin));
		}
		else if (name_is(data + cursor, name_length, "sec-fetch-site"))
		{
			if (name_is(data + value_start, value_end - value_start, "cross-site") ||
				name_is(data + value_start, value_end - value_start, "same-site"))
			{
				request->cross_site = 1;
			}
		}
		else if (name_is(data + cursor, name_length, "x-forwarded-host"))
		{
			request->proxied = 1;
			if (have_forwarded_host++)
				request->forwarded_host[0] = 0;
			else
			{
				copy_value(data + value_start, first_value(data + value_start, value_end - value_start),
					request->forwarded_host, sizeof(request->forwarded_host));
			}
		}
		else if (name_is(data + cursor, name_length, "x-forwarded-proto"))
		{
			request->proxied = 1;
			if (name_is(data + value_start, first_value(data + value_start, value_end - value_start), "https"))
				request->https = 1;
		}
		else if (name_is(data + cursor, name_length, "forwarded"))
		{
			request->proxied = 1;
			if (forwarded_https(data + value_start, value_end - value_start))
				request->https = 1;
		}
		else if (name_is(data + cursor, name_length, "x-background"))
		{
			request->background = value_end - value_start == 1 && data[value_start] == '1';
		}
		else if (name_is(data + cursor, name_length, "x-forwarded-for") ||
			name_is(data + cursor, name_length, "x-real-ip"))
		{
			request->proxied = 1;
		}
		else if (name_is(data + cursor, name_length, "content-length"))
		{
			size_t value = 0;

			if (have_length++)
				return fail(status, reason, 400, "Content-Length twice");
			if (value_end == value_start || value_end - value_start > 9)
				return fail(status, reason, value_end == value_start ? 400 : 413, "Content-Length");
			for (index = value_start; index < value_end; index++)
			{
				if (data[index] < '0' || data[index] > '9')
					return fail(status, reason, 400, "Content-Length is no number");
				value = value * 10 + (size_t)(data[index] - '0');
			}
			if (value > CONTROL_MAXIMUM_BODY)
				return fail(status, reason, 413, "the body is too large");
			request->content_length = value;
		}
		else if (name_is(data + cursor, name_length, "transfer-encoding"))
		{
			return fail(status, reason, 501, "no Transfer-Encoding is taken");
		}
		else if (name_is(data + cursor, name_length, "expect"))
		{
			return fail(status, reason, 417, "no Expect is taken");
		}
		else if (name_is(data + cursor, name_length, "authorization"))
		{
			if (have_authorization++)
				return fail(status, reason, 400, "Authorization twice");
			parse_authorization(data + value_start, value_end - value_start, request);
		}
		else if (name_is(data + cursor, name_length, "content-type"))
		{
			if (have_type++)
				return fail(status, reason, 400, "Content-Type twice");
			request->json_body = content_type_is_json(data + value_start, value_end - value_start);
		}
		cursor = line_end + 2;
	}
	if (version_minor == 1 && !have_host)
		return fail(status, reason, 400, "no Host");

	/* the body: none with GET, Content-Length's with POST; nothing after it
	(a connection takes one request) */
	if (request->method == CONTROL_METHOD_GET)
	{
		if (request->content_length || length > head_length)
			return fail(status, reason, 400, "a GET with a body");
		return CONTROL_PARSE_DONE;
	}
	if (!have_length)
		return fail(status, reason, 411, "a POST needs Content-Length");
	if (length < head_length + request->content_length)
		return CONTROL_PARSE_INCOMPLETE;
	if (length > head_length + request->content_length)
		return fail(status, reason, 400, "more than Content-Length");
	request->body = data + head_length;
	return CONTROL_PARSE_DONE;
}

static void skip_json_space(const char **cursor, const char *end)
{
	while (*cursor < end && (**cursor == ' ' || **cursor == '\t' || **cursor == '\r' || **cursor == '\n'))
		(*cursor)++;
}

/* a JSON string at the cursor, into out (size bytes): printable ASCII and
tabs only; 1, else 0 and why */
static int parse_json_string(const char **cursor, const char *end, char *out, size_t size, const char **reason,
	int multiline)
{
	const char *at = *cursor;
	size_t length = 0;

	if (at >= end || *at != '"')
	{
		*reason = "a string was expected";
		return 0;
	}
	at++;
	for (;;)
	{
		int character;

		if (at >= end)
		{
			*reason = "a string is not closed";
			return 0;
		}
		character = (unsigned char)*at++;
		if (character == '"')
			break;
		if (character == '\\')
		{
			if (at >= end)
			{
				*reason = "a string is not closed";
				return 0;
			}
			character = (unsigned char)*at++;
			switch (character)
			{
			case '"': case '\\': case '/': break;
			case 't': character = '\t'; break;
			case 'n': case 'r':
				/* (a file's text: its lines) */
				if (!multiline)
				{
					*reason = "an escape a command may not have";
					return 0;
				}
				character = character == 'n' ? '\n' : '\r';
				break;
			case 'u':
			{
				int index;
				int value = 0;

				if (end - at < 4)
				{
					*reason = "a \\u escape is cut short";
					return 0;
				}
				for (index = 0; index < 4; index++)
				{
					int digit = hex_value((unsigned char)at[index]);

					if (digit < 0)
					{
						*reason = "a \\u escape is not hexadecimal";
						return 0;
					}
					value = value << 4 | digit;
				}
				at += 4;
				character = value;
				break;
			}
			default:
				/* (\b \f \n \r, and anything else, are no command's) */
				*reason = "an escape a command may not have";
				return 0;
			}
		}
		if ((character < 0x20 && character != '\t' && !(multiline && (character == '\n' || character == '\r'))) ||
			character > 0x7e)
		{
			*reason = "a command is printable ASCII";
			return 0;
		}
		if (length + 1 >= size)
		{
			*reason = "a string is too long";
			return 0;
		}
		out[length++] = (char)character;
	}
	out[length] = 0;
	*cursor = at;
	return 1;
}

/* a request body that is a JSON object of one field, {"<field>": "..."}:
1 and its string in out, else 0 and why */
static int parse_one_field_body(const char *body, size_t length, const char *field, char *out, size_t out_size,
	const char **reason)
{
	const char *cursor = body;
	const char *end = body + length;
	char key[16];
	char *command = out;
	size_t command_size = out_size;

	*reason = "";
	if (command_size)
		command[0] = 0;
	skip_json_space(&cursor, end);
	if (cursor >= end || *cursor != '{')
	{
		*reason = "the body is not a JSON object";
		return 0;
	}
	cursor++;
	skip_json_space(&cursor, end);
	if (!parse_json_string(&cursor, end, key, sizeof(key), reason, 0))
	{
		if (cursor < end && *cursor == '}')
			*reason = !strcmp(field, "command") ? "no command" : "no token";
		return 0;
	}
	if (strcmp(key, field))
	{
		*reason = !strcmp(field, "command") ? "the only field is command" : "the only field is token";
		return 0;
	}
	skip_json_space(&cursor, end);
	if (cursor >= end || *cursor != ':')
	{
		*reason = "a colon was expected";
		return 0;
	}
	cursor++;
	skip_json_space(&cursor, end);
	if (!parse_json_string(&cursor, end, command, command_size, reason, 0))
		return 0;
	skip_json_space(&cursor, end);
	if (cursor >= end || *cursor != '}')
	{
		*reason = cursor < end && *cursor == ',' ? (!strcmp(field, "command") ? "the only field is command" :
			"the only field is token") : "the object is not closed";
		return 0;
	}
	cursor++;
	skip_json_space(&cursor, end);
	if (cursor != end)
	{
		*reason = "more after the object";
		return 0;
	}
	return 1;
}

int control_parse_command_body(const char *body, size_t length, char *command, size_t command_size,
	const char **reason)
{
	return parse_one_field_body(body, length, "command", command, command_size, reason);
}

int control_parse_login_body(const char *body, size_t length, char *token, size_t token_size, const char **reason)
{
	if (!parse_one_field_body(body, length, "token", token, token_size, reason))
	{
		if (token_size)
			crypto_wipe(token, token_size);
		return 0;
	}
	return 1;
}

int control_parse_fields(const char *body, size_t length, const char *const *names, char **outs,
	const size_t *sizes, int count, unsigned int multiline, const char **reason)
{
	const char *cursor = body;
	const char *end = body + length;
	unsigned int seen = 0;
	int index;

	*reason = "";
	for (index = 0; index < count; index++)
	{
		if (sizes[index])
			outs[index][0] = 0;
	}
	skip_json_space(&cursor, end);
	if (cursor >= end || *cursor != '{')
	{
		*reason = "the body is not a JSON object";
		return 0;
	}
	cursor++;
	skip_json_space(&cursor, end);
	if (cursor < end && *cursor == '}')
		cursor++;
	else
	{
		for (;;)
		{
			char key[32];
			int field = -1;

			if (!parse_json_string(&cursor, end, key, sizeof(key), reason, 0))
				goto refused;
			for (index = 0; index < count && index < 32; index++)
			{
				if (!strcmp(key, names[index]))
					field = index;
			}
			if (field < 0)
			{
				*reason = "a field the request does not take";
				goto refused;
			}
			if (seen & (1u << field))
			{
				*reason = "a field given twice";
				goto refused;
			}
			seen |= 1u << field;
			skip_json_space(&cursor, end);
			if (cursor >= end || *cursor != ':')
			{
				*reason = "a colon was expected";
				goto refused;
			}
			cursor++;
			skip_json_space(&cursor, end);
			if (!parse_json_string(&cursor, end, outs[field], sizes[field], reason,
				(multiline >> field) & 1))
				goto refused;
			skip_json_space(&cursor, end);
			if (cursor < end && *cursor == ',')
			{
				cursor++;
				skip_json_space(&cursor, end);
				continue;
			}
			if (cursor < end && *cursor == '}')
			{
				cursor++;
				break;
			}
			*reason = "the object is not closed";
			goto refused;
		}
	}
	skip_json_space(&cursor, end);
	if (cursor != end)
	{
		*reason = "more after the object";
		goto refused;
	}
	return 1;
refused:
	for (index = 0; index < count; index++)
		crypto_wipe(outs[index], sizes[index]);
	return 0;
}

int control_session_text_valid(const char *text)
{
	return session_form(text, strlen(text));
}

int control_parse_log_query(const char *query, uint64_t *since)
{
	uint64_t value = 0;
	int digits = 0;

	*since = 0;
	if (!query[0])
		return 1;
	if (strncmp(query, "since=", 6))
		return 0;
	for (query += 6; *query; query++)
	{
		if (*query < '0' || *query > '9' || ++digits > 18)
			return 0;
		value = value * 10 + (uint64_t)(*query - '0');
	}
	if (!digits)
		return 0;
	*since = value;
	return 1;
}

int control_json_string(const char *text, char *out, size_t size)
{
	static const char digits[] = "0123456789abcdef";
	size_t length = 0;

#define PUT(character) \
	do \
	{ \
		if (length + 1 >= size) \
			return -1; \
		out[length++] = (character); \
	} while (0)

	PUT('"');
	for (; *text; text++)
	{
		unsigned char character = (unsigned char)*text;

		if (character == '"' || character == '\\')
		{
			PUT('\\');
			PUT((char)character);
		}
		else if (character == '\n')
		{
			PUT('\\');
			PUT('n');
		}
		else if (character == '\t')
		{
			PUT('\\');
			PUT('t');
		}
		else if (character < 0x20 || character == 0x7f)
		{
			PUT('\\');
			PUT('u');
			PUT('0');
			PUT('0');
			PUT(digits[character >> 4]);
			PUT(digits[character & 15]);
		}
		else if (character > 0x7f)
		{
			/* (the server's text is ASCII: no half a character in the JSON) */
			PUT('?');
		}
		else
		{
			PUT((char)character);
		}
	}
	PUT('"');
#undef PUT
	out[length] = 0;
	return (int)length;
}

const char *control_status_text(int status)
{
	switch (status)
	{
	case 200: return "OK";
	case 400: return "Bad Request";
	case 401: return "Unauthorized";
	case 403: return "Forbidden";
	case 404: return "Not Found";
	case 405: return "Method Not Allowed";
	case 408: return "Request Timeout";
	case 411: return "Length Required";
	case 413: return "Content Too Large";
	case 414: return "URI Too Long";
	case 415: return "Unsupported Media Type";
	case 417: return "Expectation Failed";
	case 429: return "Too Many Requests";
	case 431: return "Request Header Fields Too Large";
	case 500: return "Internal Server Error";
	case 501: return "Not Implemented";
	case 503: return "Service Unavailable";
	case 505: return "HTTP Version Not Supported";
	default: return "Error";
	}
}

/* ---------- credentials */

void control_token_text(const uint8_t bytes[CONTROL_TOKEN_BYTES], char token[CONTROL_TOKEN_LENGTH + 1])
{
	memcpy(token, "chce_", 5);
	control_hex_text(bytes, CONTROL_TOKEN_BYTES, token + 5);
}

int control_token_valid(const char *text)
{
	uint8_t bytes[CONTROL_TOKEN_BYTES];

	return strlen(text) == CONTROL_TOKEN_LENGTH && !memcmp(text, "chce_", 5) &&
		hex_bytes(text + 5, CONTROL_TOKEN_LENGTH - 5, bytes, CONTROL_TOKEN_BYTES);
}

int control_credential_name_valid(const char *text)
{
	size_t length = strlen(text);
	size_t index;

	if (!length || length >= CONTROL_NAME_SIZE)
		return 0;
	for (index = 0; index < length; index++)
	{
		if (!is_tchar((unsigned char)text[index]))
			return 0;
	}
	return 1;
}

/* a token's Argon2id hash, at a credential's salt and cost: 1, else 0 (no
memory) */
int control_argon2id(const char *token, const uint8_t salt[CONTROL_SALT_BYTES], uint32_t kib, uint32_t passes,
	uint8_t hash[CONTROL_HASH_BYTES])
{
	crypto_argon2_config config;
	crypto_argon2_inputs inputs;
	void *work_area = malloc((size_t)kib * 1024);

	if (!work_area)
		return 0;
	config.algorithm = CRYPTO_ARGON2_ID;
	config.nb_blocks = kib;
	config.nb_passes = passes;
	config.nb_lanes = 1;
	inputs.pass = (const uint8_t *)token;
	inputs.pass_size = (uint32_t)strlen(token);
	inputs.salt = salt;
	inputs.salt_size = CONTROL_SALT_BYTES;
	crypto_argon2(hash, CONTROL_HASH_BYTES, work_area, config, inputs, crypto_argon2_no_extras);
	free(work_area);
	return 1;
}

int control_credential_make(const char *token, const char *name, const uint8_t id_bytes[CONTROL_ID_LENGTH / 2],
	const uint8_t salt[CONTROL_SALT_BYTES], uint32_t kib, uint32_t passes, struct control_credential *credential)
{
	memset(credential, 0, sizeof(*credential));
	snprintf(credential->name, sizeof(credential->name), "%s", name);
	control_hex_text(id_bytes, CONTROL_ID_LENGTH / 2, credential->id);
	memcpy(credential->salt, salt, CONTROL_SALT_BYTES);
	credential->kib = kib;
	credential->passes = passes;
	return control_argon2id(token, salt, kib, passes, credential->hash);
}

void control_credential_line(const struct control_credential *credential, char line[CONTROL_CREDENTIAL_LINE])
{
	char salt[2 * CONTROL_SALT_BYTES + 1];
	char hash[2 * CONTROL_HASH_BYTES + 1];

	control_hex_text(credential->salt, CONTROL_SALT_BYTES, salt);
	control_hex_text(credential->hash, CONTROL_HASH_BYTES, hash);
	snprintf(line, CONTROL_CREDENTIAL_LINE, "v1 argon2id %s %s %lu %lu %s %s", credential->name, credential->id,
		(unsigned long)credential->kib, (unsigned long)credential->passes, salt, hash);
}

/* the next word of a line (no longer than size - 1), past single spaces:
its length, or 0 if there is none or it is too long */
static size_t next_word(const char **line, char *word, size_t size)
{
	size_t length = 0;

	while ((*line)[length] && (*line)[length] != ' ' && (*line)[length] != '\n' && (*line)[length] != '\r')
	{
		if (length + 1 >= size)
			return 0;
		word[length] = (*line)[length];
		length++;
	}
	word[length] = 0;
	*line += length;
	if (**line == ' ')
		(*line)++;
	return length;
}

/* a whole decimal number (no sign) from minimum to maximum */
static int word_number(const char *word, uint32_t minimum, uint32_t maximum, uint32_t *value)
{
	uint64_t number = 0;
	int digits = 0;

	for (; *word; word++)
	{
		if (*word < '0' || *word > '9' || ++digits > 9)
			return 0;
		number = number * 10 + (uint64_t)(*word - '0');
	}
	if (!digits || number < minimum || number > maximum)
		return 0;
	*value = (uint32_t)number;
	return 1;
}

int control_credential_parse(const char *line, struct control_credential *credential)
{
	char word[2 * CONTROL_HASH_BYTES + 1];
	size_t length;
	size_t index;

	memset(credential, 0, sizeof(*credential));
	if (next_word(&line, word, sizeof(word)) != 2 || strcmp(word, "v1") ||
		next_word(&line, word, sizeof(word)) != 8 || strcmp(word, "argon2id"))
	{
		return 0;
	}
	length = next_word(&line, word, sizeof(credential->name));
	if (!length)
		return 0;
	for (index = 0; index < length; index++)
	{
		if (!is_tchar((unsigned char)word[index]))
			return 0;
	}
	memcpy(credential->name, word, length + 1);
	if (next_word(&line, word, sizeof(word)) != CONTROL_ID_LENGTH)
		return 0;
	{
		uint8_t id[CONTROL_ID_LENGTH / 2];

		if (!hex_bytes(word, CONTROL_ID_LENGTH, id, sizeof(id)))
			return 0;
		memcpy(credential->id, word, CONTROL_ID_LENGTH + 1);
	}
	if (!next_word(&line, word, sizeof(word)) ||
		!word_number(word, CONTROL_ARGON2_MINIMUM_KIB, CONTROL_ARGON2_MAXIMUM_KIB, &credential->kib))
	{
		return 0;
	}
	if (!next_word(&line, word, sizeof(word)) ||
		!word_number(word, 1, CONTROL_ARGON2_MAXIMUM_PASSES, &credential->passes))
	{
		return 0;
	}
	length = next_word(&line, word, sizeof(word));
	if (!hex_bytes(word, length, credential->salt, CONTROL_SALT_BYTES))
		return 0;
	length = next_word(&line, word, sizeof(word));
	if (!hex_bytes(word, length, credential->hash, CONTROL_HASH_BYTES))
		return 0;
	/* (nothing after it but the line's end) */
	while (*line == '\r' || *line == '\n')
		line++;
	return *line == 0;
}

int control_credential_check(const struct control_credential *credential, const char *token)
{
	uint8_t hash[CONTROL_HASH_BYTES];
	int same;

	if (!control_argon2id(token, credential->salt, credential->kib, credential->passes, hash))
		return -1;
	same = crypto_verify32(hash, credential->hash) == 0;
	crypto_wipe(hash, sizeof(hash));
	return same;
}

/* ---------- failed attempts */

void control_limiter_initialize(struct control_limiter *limiter)
{
	memset(limiter, 0, sizeof(*limiter));
}

static struct control_limiter_entry *limiter_entry(struct control_limiter *limiter, const uint8_t address[16],
	int64_t now, int create)
{
	struct control_limiter_entry *oldest = NULL;
	int index;

	for (index = 0; index < CONTROL_LIMITER_ADDRESSES; index++)
	{
		struct control_limiter_entry *entry = &limiter->entries[index];

		if (entry->used && !memcmp(entry->address, address, 16))
			return entry;
	}
	if (!create)
		return NULL;
	/* (a free one, else the one blocked least and longest ago: one an
	attacker's many addresses would push out is still blocked if it is) */
	for (index = 0; index < CONTROL_LIMITER_ADDRESSES; index++)
	{
		struct control_limiter_entry *entry = &limiter->entries[index];

		if (!entry->used)
		{
			oldest = entry;
			break;
		}
		if (entry->blocked_until > now)
			continue;
		if (!oldest || entry->window_start < oldest->window_start)
			oldest = entry;
	}
	if (!oldest)
		return NULL;
	memset(oldest, 0, sizeof(*oldest));
	oldest->used = 1;
	memcpy(oldest->address, address, 16);
	oldest->window_start = now;
	return oldest;
}

int control_limiter_allowed(struct control_limiter *limiter, const uint8_t address[16], int64_t now,
	int64_t *retry_after)
{
	struct control_limiter_entry *entry = limiter_entry(limiter, address, now, 0);

	*retry_after = 0;
	if (entry && entry->blocked_until > now)
	{
		*retry_after = entry->blocked_until - now;
		return 0;
	}
	return 1;
}

int control_limiter_take_check(struct control_limiter *limiter, int64_t now)
{
	if (!limiter->started)
	{
		limiter->started = 1;
		limiter->check_tokens = (int64_t)CONTROL_LIMITER_CHECK_BURST * 1000;
		limiter->refilled = now;
	}
	if (now > limiter->refilled)
	{
		limiter->check_tokens += (now - limiter->refilled) * CONTROL_LIMITER_CHECKS_PER_MINUTE * 1000 / 60;
		if (limiter->check_tokens > (int64_t)CONTROL_LIMITER_CHECK_BURST * 1000)
			limiter->check_tokens = (int64_t)CONTROL_LIMITER_CHECK_BURST * 1000;
		limiter->refilled = now;
	}
	if (limiter->check_tokens < 1000)
		return 0;
	limiter->check_tokens -= 1000;
	return 1;
}

void control_limiter_failed(struct control_limiter *limiter, const uint8_t address[16], int64_t now)
{
	struct control_limiter_entry *entry = limiter_entry(limiter, address, now, 1);

	if (!entry)
		return;
	if (now - entry->window_start >= CONTROL_LIMITER_WINDOW_SECONDS)
	{
		entry->window_start = now;
		entry->failures = 0;
	}
	if (++entry->failures >= CONTROL_LIMITER_FAILURES)
	{
		entry->blocked_until = now + CONTROL_LIMITER_BLOCK_SECONDS;
		entry->failures = 0;
		entry->window_start = now;
	}
}

void control_limiter_succeeded(struct control_limiter *limiter, const uint8_t address[16])
{
	struct control_limiter_entry *entry = limiter_entry(limiter, address, 0, 0);

	if (entry && entry->blocked_until <= 0)
		memset(entry, 0, sizeof(*entry));
	else if (entry)
		entry->failures = 0;
}

/* ---------- hexadecimal */

void control_hex_text(const uint8_t *bytes, size_t count, char *text)
{
	static const char digits[] = "0123456789abcdef";
	size_t index;

	for (index = 0; index < count; index++)
	{
		text[2 * index] = digits[bytes[index] >> 4];
		text[2 * index + 1] = digits[bytes[index] & 15];
	}
	text[2 * count] = 0;
}

/* ---------- the log */

/* whether IPv4 bytes are no one's on the internet (log_address.c's) */
static int local_ipv4(const unsigned int bytes[4])
{
	return bytes[0] == 0 || bytes[0] == 10 || bytes[0] == 127 || (bytes[0] == 172 && (bytes[1] & 0xF0) == 16) ||
		(bytes[0] == 192 && bytes[1] == 168) || (bytes[0] == 169 && bytes[1] == 254) ||
		(bytes[0] == 100 && (bytes[1] & 0xC0) == 64);
}

/* a dotted IPv4 address at text (not inside a longer number or name): its
length, with its bytes, else 0 */
static size_t ipv4_at(const char *text, size_t start, unsigned int bytes[4])
{
	size_t index = start;
	int part;

	if (start > 0 && ((text[start - 1] >= '0' && text[start - 1] <= '9') || text[start - 1] == '.' ||
		(lower((unsigned char)text[start - 1]) >= 'a' && lower((unsigned char)text[start - 1]) <= 'z')))
	{
		return 0;
	}
	for (part = 0; part < 4; part++)
	{
		unsigned int value = 0;
		int digits = 0;

		while (text[index] >= '0' && text[index] <= '9' && digits < 4)
		{
			value = value * 10 + (unsigned int)(text[index] - '0');
			digits++;
			index++;
		}
		if (!digits || digits > 3 || value > 255)
			return 0;
		bytes[part] = value;
		if (part < 3)
		{
			if (text[index] != '.')
				return 0;
			index++;
		}
	}
	/* (not "1.2.3.4.5", nor a version's "1.2.3.4b") */
	if ((text[index] >= '0' && text[index] <= '9') || (text[index] == '.' && text[index + 1] >= '0' &&
		text[index + 1] <= '9'))
	{
		return 0;
	}
	return index - start;
}

/* an IPv6 address at text, as the log writes one (hexadecimal groups and
colons, at least two of them, :: for zeros): its length, and whether it is
local, else 0 */
static size_t ipv6_at(const char *text, size_t start, int *local)
{
	size_t index = start;
	int colons = 0;
	int double_colon = 0;
	int groups = 0;
	unsigned int first = 0;
	int group_digits = 0;
	unsigned int group = 0;

	if (start > 0 && (hex_value((unsigned char)text[start - 1]) >= 0 || text[start - 1] == ':' ||
		(lower((unsigned char)text[start - 1]) >= 'g' && lower((unsigned char)text[start - 1]) <= 'z')))
	{
		return 0;
	}
	while (text[index])
	{
		int digit = hex_value((unsigned char)text[index]);

		if (digit >= 0)
		{
			if (++group_digits > 4)
				return 0;
			group = group << 4 | (unsigned int)digit;
		}
		else if (text[index] == ':')
		{
			if (group_digits)
			{
				if (!groups)
					first = group;
				groups++;
			}
			if (index > start && text[index - 1] == ':')
			{
				if (double_colon)
					return 0;
				double_colon = 1;
			}
			colons++;
			group_digits = 0;
			group = 0;
		}
		else
			break;
		index++;
	}
	if (group_digits)
	{
		if (!groups)
			first = group;
		groups++;
	}
	/* (a time of day, 12:34:56, is not one: an address has :: or eight
	groups) */
	if (colons < 2 || (!double_colon && groups != 8) || groups > 8 ||
		(lower((unsigned char)text[index]) >= 'g' && lower((unsigned char)text[index]) <= 'z'))
	{
		return 0;
	}
	/* (::, ::1, fc00::/7, fe80::/10) */
	*local = (!groups) || (groups == 1 && text[start] == ':' && first <= 1) || (first & 0xFE00) == 0xFC00 ||
		(first & 0xFFC0) == 0xFE80;
	return index - start;
}

void control_log_scrub(const char *line, char *out, size_t size)
{
	static const char hidden[] = "addr#hidden";
	size_t index = 0;
	size_t length = 0;

	if (!size)
		return;
	while (line[index] && line[index] != '\r' && line[index] != '\n' && length + 1 < size)
	{
		unsigned int bytes[4];
		size_t matched = ipv4_at(line, index, bytes);
		int local = 0;

		if (!matched)
		{
			matched = ipv6_at(line, index, &local);
		}
		else
			local = local_ipv4(bytes);
		if (matched && !local)
		{
			size_t room = size - 1 - length;
			size_t count = sizeof(hidden) - 1 < room ? sizeof(hidden) - 1 : room;

			memcpy(out + length, hidden, count);
			length += count;
			index += matched;
			continue;
		}
		if (matched)
		{
			while (matched-- && length + 1 < size)
				out[length++] = line[index++];
			continue;
		}
		out[length++] = (unsigned char)line[index] >= 0x20 && (unsigned char)line[index] < 0x7f ? line[index] :
			line[index] == '\t' ? ' ' : '?';
		index++;
	}
	out[length] = 0;
}
