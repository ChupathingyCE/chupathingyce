/*
CONTROL_LINK_PROTOCOL.C

Delta Control's link to halo.milenko.org as bytes (control_link_protocol.h).
*/

#include "control_link_protocol.h"
#include "control_protocol.h"
#include "control_roles.h"

#include "monocypher.h"
#include "monocypher-ed25519.h"

#include <stdio.h>
#include <string.h>

/* ---------- JSON */

struct json_reader
{
	const char *text;
	size_t length;
	size_t at;
	struct control_json_token *tokens;
	int count;
	int used;
};

static void skip_space(struct json_reader *reader)
{
	while (reader->at < reader->length && (reader->text[reader->at] == ' ' || reader->text[reader->at] == '\t' ||
		reader->text[reader->at] == '\r' || reader->text[reader->at] == '\n'))
	{
		reader->at++;
	}
}

static int new_token(struct json_reader *reader, int type, size_t start)
{
	struct control_json_token *token;

	if (reader->used >= reader->count)
		return -1;
	token = &reader->tokens[reader->used];
	memset(token, 0, sizeof(*token));
	token->type = type;
	token->start = (int)start;
	return reader->used++;
}

static int read_value(struct json_reader *reader, int depth);

static int read_string(struct json_reader *reader)
{
	int index;

	/* (at its opening quote) */
	reader->at++;
	index = new_token(reader, CONTROL_JSON_STRING, reader->at);
	if (index < 0)
		return -1;
	while (reader->at < reader->length)
	{
		unsigned char character = (unsigned char)reader->text[reader->at];

		if (character == '"')
		{
			reader->tokens[index].end = (int)reader->at;
			reader->at++;
			reader->tokens[index].next = reader->used;
			return index;
		}
		if (character < 0x20)
			return -1;
		if (character == '\\')
		{
			if (reader->at + 1 >= reader->length)
				return -1;
			reader->at++;
			if (reader->text[reader->at] == 'u')
			{
				int digit;

				if (reader->at + 4 >= reader->length)
					return -1;
				for (digit = 1; digit <= 4; digit++)
				{
					char hex = reader->text[reader->at + digit];

					if (!((hex >= '0' && hex <= '9') || (hex >= 'a' && hex <= 'f') || (hex >= 'A' && hex <= 'F')))
						return -1;
				}
				reader->at += 4;
			}
			else if (!strchr("\"\\/bfnrt", reader->text[reader->at]))
				return -1;
		}
		reader->at++;
	}
	return -1;
}

static int read_primitive(struct json_reader *reader)
{
	size_t start = reader->at;
	int index;

	while (reader->at < reader->length && strchr("0123456789+-.eEtrufalsn", reader->text[reader->at]))
		reader->at++;
	if (reader->at == start)
		return -1;
	{
		size_t length = reader->at - start;
		const char *text = reader->text + start;

		/* (true, false, null, or a number) */
		if (!((length == 4 && !memcmp(text, "true", 4)) || (length == 5 && !memcmp(text, "false", 5)) ||
			(length == 4 && !memcmp(text, "null", 4)) || strspn(text, "0123456789+-.eE") >= length))
		{
			return -1;
		}
	}
	index = new_token(reader, CONTROL_JSON_PRIMITIVE, start);
	if (index < 0)
		return -1;
	reader->tokens[index].end = (int)reader->at;
	reader->tokens[index].next = reader->used;
	return index;
}

static int read_container(struct json_reader *reader, int depth, int object)
{
	int index = new_token(reader, object ? CONTROL_JSON_OBJECT : CONTROL_JSON_ARRAY, reader->at);
	char close = object ? '}' : ']';

	if (index < 0 || depth > 16)
		return -1;
	reader->at++;
	skip_space(reader);
	if (reader->at < reader->length && reader->text[reader->at] == close)
	{
		reader->at++;
		reader->tokens[index].end = (int)reader->at;
		reader->tokens[index].next = reader->used;
		return index;
	}
	for (;;)
	{
		skip_space(reader);
		if (object)
		{
			if (reader->at >= reader->length || reader->text[reader->at] != '"' || read_string(reader) < 0)
				return -1;
			skip_space(reader);
			if (reader->at >= reader->length || reader->text[reader->at] != ':')
				return -1;
			reader->at++;
		}
		if (read_value(reader, depth + 1) < 0)
			return -1;
		reader->tokens[index].size++;
		skip_space(reader);
		if (reader->at >= reader->length)
			return -1;
		if (reader->text[reader->at] == ',')
		{
			reader->at++;
			continue;
		}
		if (reader->text[reader->at] != close)
			return -1;
		reader->at++;
		reader->tokens[index].end = (int)reader->at;
		reader->tokens[index].next = reader->used;
		return index;
	}
}

static int read_value(struct json_reader *reader, int depth)
{
	skip_space(reader);
	if (reader->at >= reader->length)
		return -1;
	switch (reader->text[reader->at])
	{
	case '{': return read_container(reader, depth, 1);
	case '[': return read_container(reader, depth, 0);
	case '"': return read_string(reader);
	default: return read_primitive(reader);
	}
}

int control_json_parse(const char *text, size_t length, struct control_json_token *tokens, int count)
{
	struct json_reader reader;

	reader.text = text;
	reader.length = length;
	reader.at = 0;
	reader.tokens = tokens;
	reader.count = count;
	reader.used = 0;
	if (read_value(&reader, 0) < 0)
		return -1;
	skip_space(&reader);
	return reader.at == length ? reader.used : -1;
}

/* whether a string token's text, undone, is key (a key is plain: no
escapes) */
static int key_is(const char *text, const struct control_json_token *token, const char *key)
{
	size_t length = strlen(key);

	return token->type == CONTROL_JSON_STRING && (size_t)(token->end - token->start) == length &&
		!memcmp(text + token->start, key, length);
}

int control_json_member(const char *text, const struct control_json_token *tokens, int object, const char *key)
{
	int member;
	int at;

	if (object < 0 || tokens[object].type != CONTROL_JSON_OBJECT)
		return -1;
	at = object + 1;
	for (member = 0; member < tokens[object].size; member++)
	{
		int value = at + 1;

		if (key_is(text, &tokens[at], key))
			return value;
		at = tokens[value].next;
	}
	return -1;
}

static int hex_digit(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return -1;
}

int control_json_text(const char *text, const struct control_json_token *token, char *out, size_t size)
{
	int at;
	size_t length = 0;

	if (token->type != CONTROL_JSON_STRING || !size)
		return 0;
	for (at = token->start; at < token->end; at++)
	{
		int character = (unsigned char)text[at];

		if (character == '\\')
		{
			at++;
			switch (text[at])
			{
			case 'n': character = '\n'; break;
			case 't': character = '\t'; break;
			case 'r': character = '\r'; break;
			case 'b': case 'f': return 0;
			case 'u':
				character = hex_digit(text[at + 1]) << 12 | hex_digit(text[at + 2]) << 8 | hex_digit(text[at + 3]) << 4 |
					hex_digit(text[at + 4]);
				at += 4;
				break;
			default: character = (unsigned char)text[at]; break;
			}
		}
		/* (printable ASCII, and line ends and tabs in a payload's text) */
		if ((character < 0x20 && character != '\n' && character != '\t' && character != '\r') || character > 0x7E)
			return 0;
		if (length + 1 >= size)
			return 0;
		out[length++] = (char)character;
	}
	out[length] = 0;
	return 1;
}

int control_json_number(const char *text, const struct control_json_token *token, uint64_t *value)
{
	uint64_t number = 0;
	int at;

	if (token->type != CONTROL_JSON_PRIMITIVE || token->end <= token->start || token->end - token->start > 18)
		return 0;
	for (at = token->start; at < token->end; at++)
	{
		if (text[at] < '0' || text[at] > '9')
			return 0;
		number = number * 10 + (uint64_t)(text[at] - '0');
	}
	*value = number;
	return 1;
}

int control_json_boolean(const char *text, const struct control_json_token *token, int *value)
{
	if (token->type != CONTROL_JSON_PRIMITIVE)
		return 0;
	if (token->end - token->start == 4 && !memcmp(text + token->start, "true", 4))
		*value = 1;
	else if (token->end - token->start == 5 && !memcmp(text + token->start, "false", 5))
		*value = 0;
	else
		return 0;
	return 1;
}

/* ---------- requests and answers */

/* (the tokens' room, shared: the link's thread alone uses this unit) */
static struct control_json_token json_tokens[CONTROL_JSON_TOKENS];

void control_link_public_key(const uint8_t seed[CONTROL_LINK_SEED_BYTES], uint8_t public_key[32])
{
	uint8_t secret[64];
	uint8_t copy[CONTROL_LINK_SEED_BYTES];

	/* (Monocypher wipes the seed it is given) */
	memcpy(copy, seed, sizeof(copy));
	crypto_ed25519_key_pair(secret, public_key, copy);
	crypto_wipe(secret, sizeof(secret));
}

int control_link_signed_body(const char *path, const char *server_id, const uint8_t seed[CONTROL_LINK_SEED_BYTES],
	int64_t time, const uint8_t nonce[CONTROL_LINK_NONCE_BYTES], const char *payload, char *out, size_t size)
{
	char nonce_text[2 * CONTROL_LINK_NONCE_BYTES + 1];
	char signature_text[129];
	uint8_t secret[64];
	uint8_t public_key[32];
	uint8_t signature[64];
	uint8_t copy[CONTROL_LINK_SEED_BYTES];
	size_t message_size = strlen(path) + strlen(server_id) + strlen(payload) + 96;
	char *message;
	char *quoted;
	size_t quoted_size = strlen(payload) * 6 + 3;
	int length;

	control_hex_text(nonce, CONTROL_LINK_NONCE_BYTES, nonce_text);
	if (message_size > size || quoted_size > size)
		return -1;
	/* (the message signed: in the out buffer's room, before the body is
	written there) */
	message = out;
	length = snprintf(message, size, "delta control request v1\n%s\n%s\n%lld\n%s\n%s", path, server_id, (long long)time,
		nonce_text, payload);
	if (length < 0 || (size_t)length >= size)
		return -1;
	memcpy(copy, seed, sizeof(copy));
	crypto_ed25519_key_pair(secret, public_key, copy);
	crypto_ed25519_sign(signature, secret, (const uint8_t *)message, (size_t)length);
	crypto_wipe(secret, sizeof(secret));
	control_hex_text(signature, sizeof(signature), signature_text);
	/* the body: the payload as a JSON string */
	{
		static char quoted_buffer[64 * 1024];

		if (quoted_size > sizeof(quoted_buffer))
			return -1;
		quoted = quoted_buffer;
		if (control_json_string(payload, quoted, sizeof(quoted_buffer)) < 0)
			return -1;
		length = snprintf(out, size, "{\"server_id\": \"%s\", \"time\": %lld, \"nonce\": \"%s\", \"payload\": %s, "
			"\"signature\": \"%s\"}", server_id, (long long)time, nonce_text, quoted, signature_text);
	}
	return length < 0 || (size_t)length >= size ? -1 : length;
}

int control_link_open_answer(const char *answer, size_t length, const uint8_t secret[CONTROL_LINK_SECRET_BYTES],
	const uint8_t nonce[CONTROL_LINK_NONCE_BYTES], char *payload, size_t payload_size)
{
	int count = control_json_parse(answer, length, json_tokens, CONTROL_JSON_TOKENS);
	int payload_token, mac_token;
	char mac_text[80];
	uint8_t mac[32];
	uint8_t expected[32];
	char nonce_text[2 * CONTROL_LINK_NONCE_BYTES + 1];
	crypto_blake2b_ctx context;
	int index;

	if (count <= 0 || payload_size < 2)
		return 0;
	payload_token = control_json_member(answer, json_tokens, 0, "payload");
	mac_token = control_json_member(answer, json_tokens, 0, "mac");
	if (payload_token < 0 || mac_token < 0 || !control_json_text(answer, &json_tokens[mac_token], mac_text,
		sizeof(mac_text)) || strlen(mac_text) != 64 || !control_json_text(answer, &json_tokens[payload_token], payload,
		payload_size))
	{
		return 0;
	}
	for (index = 0; index < 32; index++)
	{
		int high = hex_digit(mac_text[2 * index]);
		int low = hex_digit(mac_text[2 * index + 1]);

		if (high < 0 || low < 0)
			return 0;
		mac[index] = (uint8_t)(high << 4 | low);
	}
	control_hex_text(nonce, CONTROL_LINK_NONCE_BYTES, nonce_text);
	crypto_blake2b_keyed_init(&context, 32, secret, CONTROL_LINK_SECRET_BYTES);
	crypto_blake2b_update(&context, (const uint8_t *)"delta control response v1\n", 26);
	crypto_blake2b_update(&context, (const uint8_t *)nonce_text, strlen(nonce_text));
	crypto_blake2b_update(&context, (const uint8_t *)"\n", 1);
	crypto_blake2b_update(&context, (const uint8_t *)payload, strlen(payload));
	crypto_blake2b_final(&context, expected);
	if (crypto_verify32(mac, expected))
	{
		crypto_wipe(payload, payload_size);
		return 0;
	}
	return 1;
}

/* a string member's text: 1, else 0 (absent, or not a string that fits) */
static int member_text(const char *text, int object, const char *key, char *out, size_t size)
{
	int value = control_json_member(text, json_tokens, object, key);

	return value >= 0 && control_json_text(text, &json_tokens[value], out, size);
}

int control_link_parse_poll(const char *payload, size_t length, struct control_link_poll *poll)
{
	int count = control_json_parse(payload, length, json_tokens, CONTROL_JSON_TOKENS);
	int value;

	memset(poll, 0, sizeof(*poll));
	if (count <= 0 || json_tokens[0].type != CONTROL_JSON_OBJECT)
		return 0;
	value = control_json_member(payload, json_tokens, 0, "unlinked");
	if (value >= 0 && !control_json_boolean(payload, &json_tokens[value], &poll->unlinked))
		return 0;
	value = control_json_member(payload, json_tokens, 0, "poll_seconds");
	{
		uint64_t seconds = 5;

		if (value >= 0 && !control_json_number(payload, &json_tokens[value], &seconds))
			return 0;
		poll->poll_seconds = seconds < 1 ? 1 : seconds > 60 ? 60 : (int)seconds;
	}
	value = control_json_member(payload, json_tokens, 0, "commands");
	if (value >= 0)
	{
		int item;
		int at = value + 1;

		if (json_tokens[value].type != CONTROL_JSON_ARRAY || json_tokens[value].size > CONTROL_LINK_COMMANDS)
			return 0;
		for (item = 0; item < json_tokens[value].size; item++)
		{
			struct control_link_command *command = &poll->commands[poll->command_count];
			int id = control_json_member(payload, json_tokens, at, "id");

			if (json_tokens[at].type != CONTROL_JSON_OBJECT || id < 0 ||
				!control_json_number(payload, &json_tokens[id], &command->id) ||
				!member_text(payload, at, "actor", command->actor, sizeof(command->actor)) ||
				!member_text(payload, at, "command", command->command, sizeof(command->command)))
			{
				return 0;
			}
			if (control_json_member(payload, json_tokens, at, "reason") >= 0 &&
				json_tokens[control_json_member(payload, json_tokens, at, "reason")].type == CONTROL_JSON_STRING &&
				!member_text(payload, at, "reason", command->reason, sizeof(command->reason)))
			{
				return 0;
			}
			poll->command_count++;
			at = json_tokens[at].next;
		}
	}
	value = control_json_member(payload, json_tokens, 0, "roles");
	if (value >= 0 && json_tokens[value].type == CONTROL_JSON_OBJECT)
	{
		int version = control_json_member(payload, json_tokens, value, "version");
		int entries = control_json_member(payload, json_tokens, value, "entries");
		int item;
		int at;

		if (version < 0 || entries < 0 || !control_json_number(payload, &json_tokens[version], &poll->roles_version) ||
			json_tokens[entries].type != CONTROL_JSON_ARRAY || json_tokens[entries].size > CONTROL_LINK_ROLE_ENTRIES)
		{
			return 0;
		}
		poll->has_roles = 1;
		at = entries + 1;
		for (item = 0; item < json_tokens[entries].size; item++)
		{
			struct control_link_role *role = &poll->roles[poll->role_count];
			char role_text[16];
			int keys = control_json_member(payload, json_tokens, at, "keys");

			if (json_tokens[at].type != CONTROL_JSON_OBJECT ||
				!member_text(payload, at, "handle", role->handle, sizeof(role->handle)) ||
				!member_text(payload, at, "role", role_text, sizeof(role_text)) ||
				(role->role = control_role_parse(role_text)) < 0)
			{
				return 0;
			}
			if (keys >= 0)
			{
				int key;
				int key_at = keys + 1;

				if (json_tokens[keys].type != CONTROL_JSON_ARRAY)
					return 0;
				for (key = 0; key < json_tokens[keys].size; key++)
				{
					char key_text[CONTROL_KEY_TEXT + 2];

					if (!control_json_text(payload, &json_tokens[key_at], key_text, sizeof(key_text)))
						return 0;
					/* (keys past the most an entry keeps are left out) */
					if (role->key_count < CONTROL_LINK_ENTRY_KEYS)
					{
						if (!control_key_parse(key_text, role->keys[role->key_count]))
							return 0;
						role->key_count++;
					}
					key_at = json_tokens[key_at].next;
				}
			}
			poll->role_count++;
			at = json_tokens[at].next;
		}
	}
	return 1;
}

int control_link_parse_start(const char *answer, size_t length, char *code, size_t code_size, char *token,
	size_t token_size, int *expires)
{
	int count = control_json_parse(answer, length, json_tokens, CONTROL_JSON_TOKENS);
	int value;
	uint64_t seconds = 600;

	if (count <= 0 || json_tokens[0].type != CONTROL_JSON_OBJECT || !member_text(answer, 0, "code", code, code_size) ||
		!member_text(answer, 0, "token", token, token_size) || !code[0] || !token[0])
	{
		return 0;
	}
	value = control_json_member(answer, json_tokens, 0, "expires");
	if (value >= 0 && !control_json_number(answer, &json_tokens[value], &seconds))
		return 0;
	*expires = seconds > 3600 ? 3600 : (int)seconds;
	return 1;
}

int control_link_parse_status(const char *answer, size_t length, char *state, size_t state_size, char *server_id,
	size_t server_id_size, char *owner, size_t owner_size)
{
	int count = control_json_parse(answer, length, json_tokens, CONTROL_JSON_TOKENS);

	server_id[0] = owner[0] = 0;
	if (count <= 0 || json_tokens[0].type != CONTROL_JSON_OBJECT || !member_text(answer, 0, "state", state, state_size))
		return 0;
	if (!strcmp(state, "linked") && (!member_text(answer, 0, "server_id", server_id, server_id_size) || !server_id[0] ||
		strspn(server_id, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != strlen(server_id)))
	{
		return 0;
	}
	member_text(answer, 0, "owner", owner, owner_size);
	return 1;
}

int control_link_command_line(const char *command, const char *reason, char *line, size_t size, const char **why)
{
	static const char *const taken[] = { "sv_warn", "sv_kick", "sv_ban", "sv_unban", "sv_map", "sv_mapcycle_next",
		"sv_end_game", "sv_name", "sv_maxplayers" };
	size_t word = strcspn(command, " \t");
	size_t index;
	int known = 0;
	int reasoned;
	size_t length;

	for (index = 0; command[index]; index++)
	{
		unsigned char character = (unsigned char)command[index];

		if (character < 0x20 || character > 0x7E || character == ';')
		{
			*why = "a command is printable ASCII, without ;";
			return 0;
		}
	}
	for (index = 0; index < sizeof(taken) / sizeof(taken[0]); index++)
	{
		if (strlen(taken[index]) == word && !strncmp(command, taken[index], word))
			known = 1;
	}
	if (!known)
	{
		*why = "not a command the site may send";
		return 0;
	}
	reasoned = reason && reason[0] && (!strncmp(command, "sv_warn", word) || !strncmp(command, "sv_kick", word) ||
		!strncmp(command, "sv_ban", word));
	length = (size_t)snprintf(line, size, "%s", command);
	if (length >= size)
	{
		*why = "the command is too long";
		return 0;
	}
	if (reasoned)
	{
		/* the reason, the line's last word, in quotes */
		if (length + 3 >= size)
		{
			*why = "the command is too long";
			return 0;
		}
		line[length++] = ' ';
		line[length++] = '"';
		for (; *reason; reason++)
		{
			unsigned char character = (unsigned char)*reason;

			if (character < 0x20 || character > 0x7E)
			{
				*why = "a reason is printable ASCII";
				return 0;
			}
			if (length + 4 >= size)
			{
				*why = "the command is too long";
				return 0;
			}
			if (character == '"' || character == '\\')
				line[length++] = '\\';
			line[length++] = (char)character;
		}
		line[length++] = '"';
		line[length] = 0;
	}
	return 1;
}
