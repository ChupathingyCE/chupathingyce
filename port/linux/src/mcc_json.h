/*
MCC_JSON.H
Lookups in the JSON of MCC packages and Steam Workshop items (ModInfo.json,
CampaignInfo.json and the maps' own files: steam_workshop.c, mcc_manifest.c,
mcc_menu_metadata.c), on json_reader.h: a value found by the names of the
members leading to it from the document's root object. A document is checked
whole first; one that is not an object of well-formed JSON has nothing in it.
No allocation: each lookup reads the text again, which is small.
*/
#ifndef MCC_JSON_H
#define MCC_JSON_H

#include "json_reader.h"

enum
{
	/* the longest member name compared; longer ones are passed over */
	MCC_JSON_NAME_SIZE = 128,
};

/* whether text is one JSON object, with only white space after it */
static inline int mcc_json_valid(const char *text, int length)
{
	struct json json;

	if (!text || length <= 0)
		return 0;
	json.at = text;
	json.end = text + length;
	if (!peek(&json, '{') || !json_skip(&json, 0))
		return 0;
	skip_space(&json);
	/* (a file read whole may end in a NUL) */
	while (json.at < json.end && !*json.at)
		json.at++;
	return json.at == json.end;
}

/* json at the value of the member of the object it is at named name; 0 if it
is not an object or has no such member (json is then past the object, or
where reading failed) */
static inline int mcc_json_member(struct json *json, const char *name)
{
	char key[MCC_JSON_NAME_SIZE];

	if (!take(json, '{'))
		return 0;
	if (take(json, '}'))
		return 0;
	do
	{
		struct json before = *json;
		int known = json_string(json, key, sizeof(key));

		if (!known)
		{
			/* (a name too long to be the one looked for) */
			*json = before;
			if (!json_string(json, NULL, 0))
				return 0;
		}
		if (!take(json, ':'))
			return 0;
		if (known && !strcmp(key, name))
		{
			skip_space(json);
			return 1;
		}
		if (!json_skip(json, 1))
			return 0;
	} while (take(json, ','));
	return 0;
}

/* json at the value path names (member names, NULL at the end) in the
document text; 0 if the document is not valid or has none there */
static inline int mcc_json_find(const char *text, int length, const char *const *path, struct json *json)
{
	if (!mcc_json_valid(text, length))
		return 0;
	json->at = text;
	json->end = text + length;
	for (; *path; path++)
	{
		if (!mcc_json_member(json, *path))
			return 0;
	}
	return 1;
}

/* the string at path into text (size bytes); 0 if there is none, it is not a
string, or it does not fit */
static inline int mcc_json_string(const char *text, int length, const char *const *path, char *out, size_t size)
{
	struct json json;

	return mcc_json_find(text, length, path, &json) && peek(&json, '"') && json_string(&json, out, size);
}

/* the true or false at path into *value; 0 (and *value as it was) if there is
none */
static inline int mcc_json_boolean(const char *text, int length, const char *const *path, int *value)
{
	struct json json;

	if (!mcc_json_find(text, length, path, &json))
		return 0;
	if (json_literal(&json, "true"))
		*value = 1;
	else if (json_literal(&json, "false"))
		*value = 0;
	else
		return 0;
	return 1;
}

/* the number of values in the array at path; -1 if there is none */
static inline int mcc_json_count(const char *text, int length, const char *const *path)
{
	struct json json;
	int count = 0;

	if (!mcc_json_find(text, length, path, &json) || !take(&json, '['))
		return -1;
	if (take(&json, ']'))
		return 0;
	do
	{
		if (!json_skip(&json, 1))
			return -1;
		count++;
	} while (take(&json, ','));
	return count;
}

/* the strings of the array at path, at most maximum, each into out + index *
stride (size bytes): their count; -1 if there is no such array, -2 if it has
more than maximum, -3 if an element is not a string that fits */
static inline int mcc_json_strings(const char *text, int length, const char *const *path,
	char *out, size_t stride, size_t size, int maximum)
{
	struct json json, counted;
	int count = 0;

	if (!mcc_json_find(text, length, path, &json) || !take(&json, '['))
		return -1;
	if (take(&json, ']'))
		return 0;
	/* (counted first: an array too long is refused whole) */
	counted = json;
	do
	{
		if (!json_skip(&counted, 1))
			return -3;
		count++;
	} while (take(&counted, ','));
	if (count > maximum)
		return -2;
	for (count = 0; ; )
	{
		if (!peek(&json, '"') || !json_string(&json, out + (size_t)count * stride, size))
			return -3;
		count++;
		if (!take(&json, ','))
			break;
	}
	return count;
}

#endif
