/*
NETWORKS.C

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later

The networks whose names a game checks (networks.h; delta-spec's link.md,
"Networks"). Everything from outside is hostile: the size first, then the
signature, and only then the JSON, read by a strict parser that refuses the
whole document for anything wrong (bad UTF-8, a member twice in an object,
nesting past 8, a fraction or exponent anywhere, a whole number out of its
field's range, uppercase hex, bytes after the document) and skips members
it doesn't know.
*/

#include "networks.h"
#include "identity.h"

#include "monocypher.h"
#include "monocypher-ed25519.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char list_label[] = "open network list v1\n";

enum
{
	/* how deep a value may be (the document itself is 1) */
	JSON_DEPTH = 8,
	/* the member names a document may hold, all objects together (each
	takes at least 4 bytes: "":0,) */
	JSON_NAMES_MOST = NETWORKS_DOCUMENT_MOST / 4 + 1,
	/* a whole number's digits at most (Python's json, the reference's
	reader, refuses past 4300) */
	JSON_DIGITS_MOST = 4300,
	LIST_FORMAT = 1,
};

/* ---------- names */

static int label_valid(const char *text, int length)
{
	int index;

	if (length < 1 || length > 63 || text[0] == '-' || text[length - 1] == '-')
		return 0;
	if (length >= 4 && !memcmp(text, "xn--", 4))
		return 0;
	for (index = 0; index < length; index++)
	{
		char c = text[index];

		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
			return 0;
	}
	return 1;
}

int networks_domain_valid(const char *text, int length)
{
	int start = 0, index, labels = 0;

	if (!text || length < NETWORKS_DOMAIN_LEAST || length > NETWORKS_DOMAIN_MOST)
		return 0;
	for (index = 0; index <= length; index++)
	{
		if (index < length && text[index] != '.')
			continue;
		if (!label_valid(text + start, index - start))
			return 0;
		labels++;
		if (index == length)
		{
			int last;

			/* (the last label: 2 to 63 letters) */
			if (index - start < 2)
				return 0;
			for (last = start; last < index; last++)
			{
				if (text[last] < 'a' || text[last] > 'z')
					return 0;
			}
		}
		start = index + 1;
	}
	return labels >= 2;
}

static int handle_valid(const char *text, int length)
{
	int index;

	if (length < 3 || length > 20)
		return 0;
	for (index = 0; index < length; index++)
	{
		char c = text[index];

		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
			return 0;
	}
	return 1;
}

static int is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

int networks_name_parse(const char *typed, const char *home, char *handle, char *domain)
{
	char text[NETWORKS_NAME_MOST + 1];
	const char *start, *end, *colon;
	int length, index, handle_length;

	if (!typed || !home)
		return 0;
	start = typed;
	end = typed + strlen(typed);
	while (start < end && is_space(*start))
		start++;
	while (end > start && is_space(end[-1]))
		end--;
	length = (int)(end - start);
	if (length <= 0 || length > NETWORKS_NAME_MOST)
		return 0;
	for (index = 0; index < length; index++)
	{
		char c = start[index];

		text[index] = c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
	}
	text[length] = 0;
	colon = memchr(text, ':', (size_t)length);
	handle_length = colon ? (int)(colon - text) : length;
	if (!handle_valid(text, handle_length))
		return 0;
	if (colon)
	{
		int domain_length = length - handle_length - 1;

		if (memchr(colon + 1, ':', (size_t)domain_length) || !networks_domain_valid(colon + 1, domain_length))
			return 0;
		memcpy(domain, colon + 1, (size_t)domain_length);
		domain[domain_length] = 0;
	}
	else
	{
		if (!networks_domain_valid(home, (int)strlen(home)))
			return 0;
		snprintf(domain, NETWORKS_DOMAIN_SIZE, "%s", home);
	}
	memcpy(handle, text, (size_t)handle_length);
	handle[handle_length] = 0;
	return 1;
}

int networks_shown(const char *name, const char *domain, const char *home, char *out, int size)
{
	int length;

	if (size <= 0)
		return 0;
	if (!domain || !domain[0] || (home && !strcmp(domain, home)))
		length = snprintf(out, (size_t)size, "%s", name);
	else
		length = snprintf(out, (size_t)size, "%s:%s", name, domain);
	return length < 0 ? 0 : length >= size ? size - 1 : length;
}

/* ---------- strict JSON */

struct json_name
{
	const unsigned char *text;
	int length;
};

struct json
{
	const unsigned char *at, *end;
	/* every object's member names so far, the open objects' last */
	struct json_name names[JSON_NAMES_MOST];
	int name_count;
};

/* whether the bytes are UTF-8, as RFC 3629 has it: no overlong forms, no
surrogates, nothing past U+10FFFF */
static int utf8_valid(const unsigned char *at, const unsigned char *end)
{
	while (at < end)
	{
		unsigned char c = *at;
		unsigned long point;
		int more, index;

		if (c < 0x80)
		{
			at++;
			continue;
		}
		if (c >= 0xC2 && c <= 0xDF)
			more = 1, point = c & 0x1Fu;
		else if (c >= 0xE0 && c <= 0xEF)
			more = 2, point = c & 0x0Fu;
		else if (c >= 0xF0 && c <= 0xF4)
			more = 3, point = c & 0x07u;
		else
			return 0;
		if (end - at <= more)
			return 0;
		for (index = 1; index <= more; index++)
		{
			if ((at[index] & 0xC0) != 0x80)
				return 0;
			point = point << 6 | (at[index] & 0x3Fu);
		}
		if ((more == 2 && point < 0x800) || (more == 3 && (point < 0x10000 || point > 0x10FFFF)) ||
			(point >= 0xD800 && point <= 0xDFFF))
		{
			return 0;
		}
		at += more + 1;
	}
	return 1;
}

static void json_space(struct json *json)
{
	while (json->at < json->end &&
		(*json->at == ' ' || *json->at == '\t' || *json->at == '\n' || *json->at == '\r'))
	{
		json->at++;
	}
}

static int json_take(struct json *json, char c)
{
	json_space(json);
	if (json->at < json->end && *json->at == (unsigned char)c)
	{
		json->at++;
		return 1;
	}
	return 0;
}

static int hex_value(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* a string: the text between its quotes, its escapes checked, not decoded */
static int json_string(struct json *json, struct json_name *string)
{
	const unsigned char *start;

	if (!json_take(json, '"'))
		return 0;
	start = json->at;
	while (json->at < json->end)
	{
		unsigned char c = *json->at;

		if (c == '"')
		{
			string->text = start;
			string->length = (int)(json->at - start);
			json->at++;
			return 1;
		}
		if (c < 0x20)
			return 0;
		if (c == '\\')
		{
			if (++json->at >= json->end)
				return 0;
			c = *json->at;
			if (c == 'u')
			{
				int index;

				for (index = 1; index <= 4; index++)
				{
					if (json->at + index >= json->end || hex_value(json->at[index]) < 0)
						return 0;
				}
				json->at += 4;
			}
			else if (!strchr("\"\\/bfnrt", c))
				return 0;
		}
		json->at++;
	}
	return 0;
}

/* a string's next character, decoded (UTF-8 already checked; \u escapes,
a surrogate pair as one character), or -1 at its end */
static long string_next(const unsigned char **at, const unsigned char *end)
{
	const unsigned char *p = *at;
	long point;

	if (p >= end)
		return -1;
	if (*p == '\\')
	{
		static const char escapes[] = "\"\\/bfnrt", values[] = "\"\\/\b\f\n\r\t";

		if (p[1] != 'u')
		{
			*at = p + 2;
			return (unsigned char)values[strchr(escapes, p[1]) - escapes];
		}
		point = (long)hex_value(p[2]) << 12 | hex_value(p[3]) << 8 | hex_value(p[4]) << 4 | hex_value(p[5]);
		p += 6;
		if (point >= 0xD800 && point <= 0xDBFF && end - p >= 6 && p[0] == '\\' && p[1] == 'u')
		{
			long low = (long)hex_value(p[2]) << 12 | hex_value(p[3]) << 8 | hex_value(p[4]) << 4 | hex_value(p[5]);

			if (low >= 0xDC00 && low <= 0xDFFF)
			{
				point = 0x10000 + ((point - 0xD800) << 10) + (low - 0xDC00);
				p += 6;
			}
		}
		*at = p;
		return point;
	}
	if (*p < 0x80)
		point = *p++;
	else if (*p < 0xE0)
		point = (long)(*p & 0x1F) << 6 | (p[1] & 0x3F), p += 2;
	else if (*p < 0xF0)
		point = (long)(*p & 0x0F) << 12 | (long)(p[1] & 0x3F) << 6 | (p[2] & 0x3F), p += 3;
	else
		point = (long)(*p & 0x07) << 18 | (long)(p[1] & 0x3F) << 12 | (long)(p[2] & 0x3F) << 6 | (p[3] & 0x3F),
			p += 4;
	*at = p;
	return point;
}

/* whether two strings read the same once decoded */
static int string_same(const struct json_name *a, const struct json_name *b)
{
	const unsigned char *pa = a->text, *pb = b->text;
	long ca, cb;

	do
	{
		ca = string_next(&pa, a->text + a->length);
		cb = string_next(&pb, b->text + b->length);
		if (ca != cb)
			return 0;
	} while (ca >= 0);
	return 1;
}

/* a string as ASCII (decoded) into out (size, its end included): 1, or 0
for one with any other character, or too long */
static int string_ascii(const struct json_name *string, char *out, int size)
{
	const unsigned char *p = string->text;
	long c;
	int used = 0;

	while ((c = string_next(&p, string->text + string->length)) >= 0)
	{
		if (c < 0x20 || c > 0x7E || used >= size - 1)
			return 0;
		out[used++] = (char)c;
	}
	out[used] = 0;
	return 1;
}

static int string_is(const struct json_name *string, const char *name)
{
	char text[32];

	return string_ascii(string, text, (int)sizeof(text)) && !strcmp(text, name);
}

/* a member name, refused if its object (whose first name is names[first])
already has it */
static int json_member_name(struct json *json, int first, struct json_name *name)
{
	int index;

	if (!json_string(json, name) || !json_take(json, ':'))
		return 0;
	for (index = first; index < json->name_count; index++)
	{
		if (string_same(&json->names[index], name))
			return 0;
	}
	if (json->name_count >= JSON_NAMES_MOST)
		return 0;
	json->names[json->name_count++] = *name;
	return 1;
}

/* a number: a whole one alone (no fraction or exponent anywhere), its
value into value when it is from minimum to maximum (value may be NULL:
any whole number, passed over) */
static int json_number(struct json *json, int negative_ok, unsigned long long minimum, unsigned long long maximum,
	unsigned long long *value)
{
	const unsigned char *start;
	unsigned long long number = 0;
	int negative = 0, digits = 0;

	json_space(json);
	if (json->at < json->end && *json->at == '-')
	{
		negative = 1;
		json->at++;
	}
	start = json->at;
	while (json->at < json->end && *json->at >= '0' && *json->at <= '9')
	{
		if (++digits > JSON_DIGITS_MOST)
			return 0;
		if (number <= 0xFFFFFFFFFFFFULL)
			number = number * 10 + (unsigned long long)(*json->at - '0');
		else
			number = 0xFFFFFFFFFFFFFFFFULL;
		json->at++;
	}
	if (!digits || (digits > 1 && *start == '0'))
		return 0;
	if (json->at < json->end && (*json->at == '.' || *json->at == 'e' || *json->at == 'E'))
		return 0;
	if (!value)
		return negative_ok || !negative;
	/* (-0 is 0, as the reference's reader has it; no other negative is in
	any field's range) */
	if (negative && number)
		return 0;
	if (number < minimum || number > maximum)
		return 0;
	*value = number;
	return 1;
}

static int json_literal(struct json *json, const char *literal)
{
	size_t length = strlen(literal);

	json_space(json);
	if ((size_t)(json->end - json->at) < length || memcmp(json->at, literal, length))
		return 0;
	json->at += length;
	return 1;
}

/* any value at depth, checked and passed over */
static int json_skip(struct json *json, int depth)
{
	struct json_name string;
	unsigned char c;

	if (depth > JSON_DEPTH)
		return 0;
	json_space(json);
	if (json->at >= json->end)
		return 0;
	c = *json->at;
	if (c == '"')
		return json_string(json, &string);
	if (c == '{')
	{
		int first = json->name_count, ok = 1;

		json->at++;
		if (!json_take(json, '}'))
		{
			do
			{
				ok = json_member_name(json, first, &string) && json_skip(json, depth + 1);
			} while (ok && json_take(json, ','));
			ok = ok && json_take(json, '}');
		}
		json->name_count = first;
		return ok;
	}
	if (c == '[')
	{
		int ok = 1;

		json->at++;
		if (!json_take(json, ']'))
		{
			do
			{
				ok = json_skip(json, depth + 1);
			} while (ok && json_take(json, ','));
			ok = ok && json_take(json, ']');
		}
		return ok;
	}
	if (c == 't')
		return json_literal(json, "true");
	if (c == 'f')
		return json_literal(json, "false");
	if (c == 'n')
		return json_literal(json, "null");
	return json_number(json, 1, 0, 0, NULL);
}

/* a root key: 64 lowercase hex digits, of no small order */
static int json_key(struct json *json, unsigned char *key)
{
	struct json_name string;
	int index;

	if (!json_string(json, &string) || string.length != 2 * NETWORKS_KEY_SIZE)
		return 0;
	for (index = 0; index < NETWORKS_KEY_SIZE; index++)
	{
		unsigned char high = string.text[2 * index], low = string.text[2 * index + 1];

		if (!((high >= '0' && high <= '9') || (high >= 'a' && high <= 'f')) ||
			!((low >= '0' && low <= '9') || (low >= 'a' && low <= 'f')))
		{
			return 0;
		}
		key[index] = (unsigned char)(hex_value(high) << 4 | hex_value(low));
	}
	return !identity_key_weak(key);
}

/* {"key": ..., "until": ...} at depth */
static int json_root(struct json *json, int depth, struct networks_root *root)
{
	struct json_name name;
	int first = json->name_count, has_key = 0, ok = 1;

	if (depth > JSON_DEPTH || !json_take(json, '{'))
		return 0;
	root->until = 0;
	if (!json_take(json, '}'))
	{
		do
		{
			unsigned long long value;

			if (!json_member_name(json, first, &name))
				ok = 0;
			else if (string_is(&name, "key"))
				ok = depth + 1 <= JSON_DEPTH && json_key(json, root->key), has_key = 1;
			else if (string_is(&name, "until"))
			{
				ok = depth + 1 <= JSON_DEPTH && json_number(json, 0, 0, 0xFFFFFFFFULL, &value);
				if (ok)
					root->until = (unsigned long)value;
			}
			else
				ok = json_skip(json, depth + 1);
		} while (ok && json_take(json, ','));
		ok = ok && json_take(json, '}');
	}
	json->name_count = first;
	return ok && has_key;
}

/* a network's {"roots": [...]} at depth */
static int json_network(struct json *json, int depth, struct networks_network *network)
{
	struct json_name name;
	int first = json->name_count, has_roots = 0, ok = 1;

	if (depth > JSON_DEPTH || !json_take(json, '{'))
		return 0;
	network->root_count = 0;
	if (!json_take(json, '}'))
	{
		do
		{
			if (!json_member_name(json, first, &name))
				ok = 0;
			else if (string_is(&name, "roots"))
			{
				has_roots = 1;
				ok = depth + 1 <= JSON_DEPTH && json_take(json, '[') && !json_take(json, ']');
				while (ok)
				{
					if (network->root_count >= NETWORKS_ROOTS_MOST ||
						!json_root(json, depth + 2, &network->roots[network->root_count]))
					{
						ok = 0;
						break;
					}
					network->root_count++;
					if (!json_take(json, ','))
						break;
				}
				ok = ok && json_take(json, ']');
			}
			else
				ok = json_skip(json, depth + 1);
		} while (ok && json_take(json, ','));
		ok = ok && json_take(json, '}');
	}
	json->name_count = first;
	return ok && has_roots && network->root_count >= 1;
}

/* "networks": {domain: network, ...} at depth */
static int json_networks(struct json *json, int depth, struct networks_list *list)
{
	struct json_name name;
	int first = json->name_count, ok = 1;

	if (depth > JSON_DEPTH || !json_take(json, '{'))
		return 0;
	if (!json_take(json, '}'))
	{
		do
		{
			struct networks_network *network;

			if (list->count >= NETWORKS_LIST_MOST || !json_member_name(json, first, &name))
			{
				ok = 0;
				break;
			}
			network = &list->networks[list->count];
			memset(network, 0, sizeof(*network));
			ok = string_ascii(&name, network->domain, NETWORKS_DOMAIN_SIZE) &&
				networks_domain_valid(network->domain, (int)strlen(network->domain)) &&
				json_network(json, depth + 1, network);
			network->source = 1;
			list->count++;
		} while (ok && json_take(json, ','));
		ok = ok && json_take(json, '}');
	}
	json->name_count = first;
	return ok;
}

/* whether any root key is in the list twice */
static int key_twice(const struct networks_list *list)
{
	int a, b, ra, rb;

	for (a = 0; a < list->count; a++)
	{
		for (ra = 0; ra < list->networks[a].root_count; ra++)
		{
			for (b = a; b < list->count; b++)
			{
				for (rb = b == a ? ra + 1 : 0; rb < list->networks[b].root_count; rb++)
				{
					if (!memcmp(list->networks[a].roots[ra].key, list->networks[b].roots[rb].key, NETWORKS_KEY_SIZE))
						return 1;
				}
			}
		}
	}
	return 0;
}

int networks_list_parse(const char *document, int size, struct networks_list *list, const char **why)
{
	struct json *json;
	struct json_name name;
	unsigned long long format = 0, value;
	int ok = 1, seen = 0;

	*why = "it is not a list of networks";
	memset(list, 0, sizeof(*list));
	if (!document || size <= 0 || size > NETWORKS_DOCUMENT_MOST)
	{
		*why = "it is too big";
		return 0;
	}
	if (!utf8_valid((const unsigned char *)document, (const unsigned char *)document + size))
	{
		*why = "it is not UTF-8";
		return 0;
	}
	json = (struct json *)malloc(sizeof(*json));
	if (!json)
	{
		*why = "no memory";
		return 0;
	}
	json->at = (const unsigned char *)document;
	json->end = json->at + size;
	json->name_count = 0;
	if (!json_take(json, '{') || json_take(json, '}'))
		ok = 0;
	while (ok)
	{
		int bit = 0;

		if (!json_member_name(json, 0, &name))
			ok = 0;
		else if (string_is(&name, "open_networks"))
			bit = 1, ok = json_number(json, 0, 0, 0xFFFFFFFFULL, &format);
		else if (string_is(&name, "serial"))
		{
			bit = 2, ok = json_number(json, 0, 1, NETWORKS_SERIAL_MOST, &value);
			if (ok)
				list->serial = (unsigned long)value;
		}
		else if (string_is(&name, "issued"))
		{
			ok = json_number(json, 0, 0, 0xFFFFFFFFULL, &value);
			if (ok)
				list->issued = (unsigned long)value;
		}
		else if (string_is(&name, "networks"))
			bit = 4, ok = json_networks(json, 2, list);
		else
			ok = json_skip(json, 2);
		seen |= bit;
		if (!ok || !json_take(json, ','))
			break;
	}
	ok = ok && json_take(json, '}');
	if (ok)
	{
		json_space(json);
		ok = json->at == json->end;
	}
	free(json);
	if (!ok || (seen & 7) != 7)
		return 0;
	if (format != LIST_FORMAT)
	{
		*why = "its format is not 1";
		return 0;
	}
	if (key_twice(list))
	{
		*why = "a root key is in it twice";
		return 0;
	}
	return 1;
}

int networks_signature_text(const char *text, int length, unsigned char *signature)
{
	int index;

	if (!text || (length != 2 * NETWORKS_SIGNATURE_SIZE &&
		!(length == 2 * NETWORKS_SIGNATURE_SIZE + 1 && text[length - 1] == '\n')))
	{
		return 0;
	}
	for (index = 0; index < 2 * NETWORKS_SIGNATURE_SIZE; index++)
	{
		char c = text[index];

		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return 0;
	}
	for (index = 0; index < NETWORKS_SIGNATURE_SIZE; index++)
		signature[index] = (unsigned char)(hex_value((unsigned char)text[2 * index]) << 4 |
			hex_value((unsigned char)text[2 * index + 1]));
	return 1;
}

static int key_set(const unsigned char *key)
{
	unsigned char any = 0;
	int index;

	for (index = 0; index < NETWORKS_KEY_SIZE; index++)
		any |= key[index];
	return any != 0;
}

int networks_list_check(const char *document, int size, const unsigned char *signature,
	const unsigned char (*keys)[NETWORKS_KEY_SIZE], const unsigned char *last_epochs, int key_count,
	struct networks_list *list, const char **why)
{
	unsigned char *message;
	int label = (int)strlen(list_label), index, signer = -1;

	memset(list, 0, sizeof(*list));
	if (!document || size <= 0 || size > NETWORKS_DOCUMENT_MOST)
	{
		*why = "it is too big";
		return 0;
	}
	message = (unsigned char *)malloc((size_t)(label + size));
	if (!message)
	{
		*why = "no memory";
		return 0;
	}
	memcpy(message, list_label, (size_t)label);
	memcpy(message + label, document, (size_t)size);
	for (index = 0; index < key_count && signer < 0; index++)
	{
		/* (Monocypher's check turns away an S past the group's order) */
		if (key_set(keys[index]) && crypto_ed25519_check(signature, keys[index], message, (size_t)(label + size)) == 0)
			signer = index;
	}
	free(message);
	if (signer < 0)
	{
		*why = "it is not signed by a list key";
		return 0;
	}
	if (!networks_list_parse(document, size, list, why))
		return 0;
	if ((list->serial >> 24) > last_epochs[signer])
	{
		memset(list, 0, sizeof(*list));
		*why = "its serial is past its key's last epoch";
		return 0;
	}
	return 1;
}

int networks_signed_check(const char *signed_list, int size, const unsigned char (*keys)[NETWORKS_KEY_SIZE],
	const unsigned char *last_epochs, int key_count, struct networks_list *list, const char **why)
{
	unsigned char signature[NETWORKS_SIGNATURE_SIZE];

	memset(list, 0, sizeof(*list));
	if (!signed_list || size < 2 * NETWORKS_SIGNATURE_SIZE + 1 || size > NETWORKS_SIGNED_MOST ||
		!networks_signature_text(signed_list, 2 * NETWORKS_SIGNATURE_SIZE + 1, signature))
	{
		*why = "it is not a signed list";
		return 0;
	}
	return networks_list_check(signed_list + 2 * NETWORKS_SIGNATURE_SIZE + 1, size - 2 * NETWORKS_SIGNATURE_SIZE - 1,
		signature, keys, last_epochs, key_count, list, why);
}

/* ---------- the networks a game trusts */

static void say_line(void (*say)(void *, const char *), void *context, const char *format, const char *domain,
	const char *why)
{
	char line[400];

	if (!say)
		return;
	snprintf(line, sizeof(line), format, domain, why);
	say(context, line);
}

static int trusted_index(const struct networks_trust *trust, const char *domain)
{
	int index;

	for (index = 0; index < trust->count; index++)
	{
		if (!strcmp(trust->networks[index].domain, domain))
			return index;
	}
	return -1;
}

static int key_owned(const struct networks_trust *trust, const unsigned char *key)
{
	int index, root;

	for (index = 0; index < trust->count; index++)
	{
		for (root = 0; root < trust->networks[index].root_count; root++)
		{
			if (!memcmp(trust->networks[index].roots[root].key, key, NETWORKS_KEY_SIZE))
				return 1;
		}
	}
	return 0;
}

/* the next comma-separated entry of text (from *at), spaces around it left
out: its start and length, or 0 at the end */
static int next_entry(const char **at, const char **start, int *length)
{
	const char *p = *at, *end;

	while (*p)
	{
		const char *comma = strchr(p, ',');

		end = comma ? comma : p + strlen(p);
		*at = comma ? comma + 1 : end;
		while (p < end && is_space(*p))
			p++;
		while (end > p && is_space(end[-1]))
			end--;
		if (end > p)
		{
			*start = p;
			*length = (int)(end - p);
			return 1;
		}
		p = *at;
	}
	return 0;
}

/* a domain as a setting gives it (any capitals) into out: 1, or 0 */
static int setting_domain(const char *text, int length, char *out)
{
	int index;

	if (length < NETWORKS_DOMAIN_LEAST || length > NETWORKS_DOMAIN_MOST)
		return 0;
	for (index = 0; index < length; index++)
		out[index] = text[index] >= 'A' && text[index] <= 'Z' ? (char)(text[index] - 'A' + 'a') : text[index];
	out[length] = 0;
	return networks_domain_valid(out, length);
}

/* a local addition, "domain key [key...]": 1 and the network, or 0 and why */
static int local_network(const char *text, int length, struct networks_network *network, const char **why)
{
	const char *at = text, *end = text + length;

	memset(network, 0, sizeof(*network));
	network->source = 2;
	*why = "it is not a domain and 1 to 4 root keys";
	while (at < end)
	{
		const char *word;
		int word_length;

		while (at < end && is_space(*at))
			at++;
		word = at;
		while (at < end && !is_space(*at))
			at++;
		word_length = (int)(at - word);
		if (!word_length)
			break;
		if (!network->domain[0])
		{
			if (!setting_domain(word, word_length, network->domain))
				return 0;
		}
		else
		{
			unsigned char *key;
			int index, other;

			if (network->root_count >= NETWORKS_ROOTS_MOST || word_length != 2 * NETWORKS_KEY_SIZE)
				return 0;
			key = network->roots[network->root_count].key;
			for (index = 0; index < NETWORKS_KEY_SIZE; index++)
			{
				int high = hex_value((unsigned char)word[2 * index]), low = hex_value((unsigned char)word[2 * index + 1]);

				if (high < 0 || low < 0)
					return 0;
				key[index] = (unsigned char)(high << 4 | low);
			}
			if (identity_key_weak(key))
			{
				*why = "a root key is a weak key";
				return 0;
			}
			for (other = 0; other < network->root_count; other++)
			{
				if (!memcmp(network->roots[other].key, key, NETWORKS_KEY_SIZE))
				{
					*why = "a root key is in it twice";
					return 0;
				}
			}
			network->roots[network->root_count++].until = 0;
		}
	}
	return network->domain[0] && network->root_count >= 1;
}

static void trust_take(struct networks_trust *trust, const struct networks_network *network,
	char (*removed)[NETWORKS_DOMAIN_SIZE], int removed_count, void (*say)(void *, const char *), void *context)
{
	int index;

	if (trusted_index(trust, network->domain) >= 0)
	{
		if (strcmp(network->domain, trust->networks[0].domain))
			say_line(say, context, "%s left out: %s", network->domain, "it is already trusted");
		return;
	}
	for (index = 0; index < removed_count; index++)
	{
		if (!strcmp(removed[index], network->domain))
		{
			say_line(say, context, "%s left out: %s", network->domain, "removed by a setting");
			return;
		}
	}
	for (index = 0; index < network->root_count; index++)
	{
		if (key_owned(trust, network->roots[index].key))
		{
			say_line(say, context, "%s left out: %s", network->domain, "a root key of another network");
			return;
		}
	}
	if (trust->count >= NETWORKS_TRUSTED_MOST)
		return;
	trust->networks[trust->count++] = *network;
}

void networks_trust_make(struct networks_trust *trust, const char *home,
	const unsigned char (*home_roots)[NETWORKS_KEY_SIZE], int home_root_count, const struct networks_list *list,
	const char *additions, const char *removals, void (*say)(void *context, const char *line), void *context)
{
	char removed[NETWORKS_REMOVED_MOST][NETWORKS_DOMAIN_SIZE];
	struct networks_network *own;
	const char *at, *start, *why;
	int length, index, removed_count = 0, taken = 0;

	memset(trust, 0, sizeof(*trust));
	own = &trust->networks[0];
	snprintf(own->domain, sizeof(own->domain), "%s", home ? home : "");
	for (index = 0; index < home_root_count && index < NETWORKS_ROOTS_MOST; index++)
	{
		memcpy(own->roots[index].key, home_roots[index], NETWORKS_KEY_SIZE);
		own->roots[index].until = 0;
	}
	own->root_count = index;
	trust->count = 1;
	at = removals ? removals : "";
	while (next_entry(&at, &start, &length))
	{
		char domain[NETWORKS_DOMAIN_SIZE];

		if (!setting_domain(start, length, domain))
		{
			say_line(say, context, "%.80s: %s", start, "a removal that is not a domain");
			continue;
		}
		/* (the home network can't be removed) */
		if (!strcmp(domain, own->domain))
		{
			say_line(say, context, "%s: %s", domain, "the home network is never removed");
			continue;
		}
		if (removed_count < NETWORKS_REMOVED_MOST)
			memcpy(removed[removed_count++], domain, sizeof(domain));
	}
	for (index = 0; list && index < list->count; index++)
		trust_take(trust, &list->networks[index], removed, removed_count, say, context);
	at = additions ? additions : "";
	while (taken < NETWORKS_LOCAL_MOST && next_entry(&at, &start, &length))
	{
		struct networks_network network;

		taken++;
		if (!local_network(start, length, &network, &why))
		{
			char shown[81];

			snprintf(shown, sizeof(shown), "%.*s", length < 80 ? length : 80, start);
			say_line(say, context, "a local network left out (%s): %s", shown, why);
			continue;
		}
		trust_take(trust, &network, removed, removed_count, say, context);
	}
	if (next_entry(&at, &start, &length))
		say_line(say, context, "%s%s", "local networks past the first 16 left out", "");
}

/* the trusted network one of whose root keys signed the issuer certificate,
whatever the key's date: its index (and the key's, into root), or -1 */
static int issuer_signer(const struct networks_trust *trust, const unsigned char *issuer, int issuer_size, int *root)
{
	int index, key;

	for (index = 0; index < trust->count; index++)
	{
		const struct networks_network *network = &trust->networks[index];

		for (key = 0; key < network->root_count; key++)
		{
			if (identity_issuer_signed_by(issuer, issuer_size, network->roots[key].key))
			{
				*root = key;
				return index;
			}
		}
	}
	return -1;
}

/* a network found (and the root key that signed): -1 when that key is past
its until; roots and root_count get the network's root keys in date */
static int in_date(const struct networks_trust *trust, int found, int root, unsigned long now,
	unsigned char (*roots)[NETWORKS_KEY_SIZE], int *root_count)
{
	const struct networks_network *network;
	int key;

	if (found < 0 || found >= trust->count)
		return -1;
	network = &trust->networks[found];
	if (network->roots[root].until && now > network->roots[root].until)
		return -1;
	if (roots && root_count)
	{
		*root_count = 0;
		for (key = 0; key < network->root_count; key++)
		{
			if (network->roots[key].until && now > network->roots[key].until)
				continue;
			memcpy(roots[(*root_count)++], network->roots[key].key, NETWORKS_KEY_SIZE);
		}
	}
	return found;
}

int networks_issuer_network(const struct networks_trust *trust, const unsigned char *issuer, int issuer_size,
	unsigned long now, unsigned char (*roots)[NETWORKS_KEY_SIZE], int *root_count)
{
	int root = 0, found = issuer_signer(trust, issuer, issuer_size, &root);

	return in_date(trust, found, root, now, roots, root_count);
}

void networks_memory_forget(struct networks_memory *memory)
{
	int index;

	for (index = 0; index < NETWORKS_SEEN_MOST; index++)
		memory->seen[index].used = 0;
	memory->next = 0;
}

static int issuer_refused(const struct networks_memory *memory, const unsigned char *key)
{
	int index;

	for (index = 0; index < memory->refused_count; index++)
	{
		if (!memcmp(memory->refused[index], key, NETWORKS_KEY_SIZE))
			return 1;
	}
	return 0;
}

static int home_key(const struct networks_memory *memory, const unsigned char *key)
{
	int index;

	for (index = 0; index < memory->home_key_count; index++)
	{
		if (!memcmp(memory->home_keys[index], key, NETWORKS_KEY_SIZE))
			return 1;
	}
	return 0;
}

/* whether an issuer certificate found under network (by its root key root)
is fully proven now: that root key in date, its signature good, the key
not weak, the certificate in date */
static int issuer_proven(const struct networks_trust *trust, const unsigned char *issuer, int network, int root,
	unsigned long now)
{
	struct identity_issuer parsed;

	if (network < 0 || network >= trust->count || root < 0 || root >= trust->networks[network].root_count ||
		in_date(trust, network, root, now, NULL, NULL) < 0)
	{
		return 0;
	}
	return identity_check_issuer(issuer, NETWORKS_ISSUER_SIZE,
		(const unsigned char (*)[IDENTITY_KEY_SIZE])trust->networks[network].roots[root].key, 1, now, &parsed) ==
		IDENTITY_OK;
}

int networks_issuer_lookup(const struct networks_trust *trust, struct networks_memory *memory,
	const unsigned char *issuer, int issuer_size, unsigned long now, int machine,
	unsigned char (*roots)[NETWORKS_KEY_SIZE], int *root_count)
{
	struct identity_issuer parsed;
	int index, found = -1, root = 0, slot;

	if (issuer_size != NETWORKS_ISSUER_SIZE || !identity_parse_issuer(issuer, issuer_size, &parsed))
		return NETWORKS_NONE;
	for (index = 0; index < NETWORKS_SEEN_MOST; index++)
	{
		if (memory->seen[index].used && !memcmp(memory->seen[index].issuer, issuer, NETWORKS_ISSUER_SIZE))
		{
			found = memory->seen[index].network;
			root = memory->seen[index].root;
			break;
		}
	}
	if (index == NETWORKS_SEEN_MOST)
	{
		/* (one not seen before: at most NETWORKS_LOOKS_A_MINUTE of them from
		one machine a minute) */
		int bucket = machine >= 0 && machine < NETWORKS_MACHINES ? machine : NETWORKS_MACHINES - 1;

		if (memory->machines[bucket].minute != now / 60 + 1)
		{
			memory->machines[bucket].minute = now / 60 + 1;
			memory->machines[bucket].count = 0;
			memory->machines[bucket].said = 0;
		}
		if (memory->machines[bucket].count >= NETWORKS_LOOKS_A_MINUTE)
			return NETWORKS_LIMITED;
		memory->machines[bucket].count++;
		found = issuer_signer(trust, issuer, issuer_size, &root);
		/* (one issuer, one network, counted only once a claim is fully
		proven. Home's claim always stands: any other network's claim on
		the same key is refused. Two other networks' claims refuse the key
		for both) */
		if (found == 0 && issuer_proven(trust, issuer, found, root, now))
		{
			if (!home_key(memory, parsed.public_key))
			{
				if (memory->home_key_count < NETWORKS_HOME_KEYS_MOST)
					slot = memory->home_key_count++;
				else
				{
					slot = memory->home_key_next;
					memory->home_key_next = (memory->home_key_next + 1) % NETWORKS_HOME_KEYS_MOST;
				}
				memcpy(memory->home_keys[slot], parsed.public_key, NETWORKS_KEY_SIZE);
			}
			for (index = 0; index < NETWORKS_SEEN_MOST; index++)
			{
				if (memory->seen[index].used && memory->seen[index].network > 0 &&
					!memcmp(memory->seen[index].issuer + 16, parsed.public_key, NETWORKS_KEY_SIZE))
				{
					memory->seen[index].network = NETWORKS_TWO_NETWORKS;
					memory->changes++;
				}
			}
		}
		else if (found > 0 && issuer_proven(trust, issuer, found, root, now))
		{
			if (home_key(memory, parsed.public_key))
				found = NETWORKS_TWO_NETWORKS;
			for (index = 0; found > 0 && index < NETWORKS_SEEN_MOST; index++)
			{
				const unsigned char *other = memory->seen[index].issuer;

				if (memory->seen[index].used && memory->seen[index].network > 0 &&
					memory->seen[index].network != found && !memcmp(other + 16, parsed.public_key, NETWORKS_KEY_SIZE) &&
					issuer_proven(trust, other, memory->seen[index].network, memory->seen[index].root, now))
				{
					if (memory->refused_count < NETWORKS_REFUSED_MOST)
						memcpy(memory->refused[memory->refused_count++], parsed.public_key, NETWORKS_KEY_SIZE);
					memory->changes++;
					return NETWORKS_TWO_NETWORKS;
				}
			}
			if (found == NETWORKS_TWO_NETWORKS)
				memory->changes++;
		}
		slot = memory->next;
		memory->next = (memory->next + 1) % NETWORKS_SEEN_MOST;
		memcpy(memory->seen[slot].issuer, issuer, NETWORKS_ISSUER_SIZE);
		memory->seen[slot].network = found;
		memory->seen[slot].root = root;
		memory->seen[slot].used = 1;
	}
	if (found == NETWORKS_TWO_NETWORKS)
		return NETWORKS_TWO_NETWORKS;
	/* (home's own issuer is never refused; another network's claim on a
	refused key, or on one of home's, is) */
	if (found > 0 && (issuer_refused(memory, parsed.public_key) || home_key(memory, parsed.public_key)))
		return NETWORKS_TWO_NETWORKS;
	found = in_date(trust, found, root, now, roots, root_count);
	return found < 0 ? NETWORKS_NONE : found;
}
