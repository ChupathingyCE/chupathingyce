/*
ONLINE_STRINGS.C

The game browser (configure.py --game-browser) makes the Multiplayer
menu's System Link the way to play on the local network and over the
internet alike (its list shows the games of the game list too: port/linux/
src/browser.c), so the menu calls it ONLINE PLAY, and the strings that
speak of Xbox system link cables say what is true on a computer.

The strings are the user interface's tags (unicode string lists), changed as
a map's tags load (cache_files.c, beside pal_tags.c): each one found is
rewritten in place, never longer than it was (so it stays within its own
data), and after whatever the string holds before the words (the game's
strings may start with a formatting mark).
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cache/cache_files.h"
#include "cseries/errors.h"
#include "tag_files/tag_groups.h"
#include "text/text_group.h"

/* ---------- constants */

static struct
{
	wchar_t const *original;
	wchar_t const *replacement;
} const online_strings[] =
{
	{ L"SYSTEM LINK PLAY", L"ONLINE PLAY" },
	{ L"Connect up to four Xbox video \r\ngame systems for a total of \r\nup to sixteen players.",
		L"Play with anyone on your \r\nnetwork, or anyone online \r\nfrom the game list." },
	{ L"The System Link game\r\nhas closed down.", L"The online game\r\nhas closed down." },
	{ L"Network connection lost. \r\nXbox System Link Cable \r\nmay be disconnected\r\nor the network is down.",
		L"Network connection lost. \r\nThe network may be down." },
	{ L"Failed to join the\r\nSystem Link game.", L"Failed to join the\r\nonline game." },
};

/* ---------- private code */

static long wide_length(
	wchar_t const *text)
{
	long length = 0;

	while (text[length])
		length++;
	return length;
}

/* the original at the end of the string (after any mark), rewritten */
static boolean rewrite_string(
	wchar_t *string,
	long capacity)
{
	long length = 0;
	long index;

	while (length < capacity && string[length])
		length++;
	for (index = 0; index < NUMBEROF(online_strings); index++)
	{
		long original_length = wide_length(online_strings[index].original);
		long replacement_length = wide_length(online_strings[index].replacement);
		long start = length - original_length;

		if (start >= 0 && replacement_length <= original_length &&
			!csmemcmp(string + start, online_strings[index].original, original_length * sizeof(wchar_t)))
		{
			csmemcpy(string + start, online_strings[index].replacement, replacement_length * sizeof(wchar_t));
			string[start + replacement_length] = 0;
			return TRUE;
		}
	}
	return FALSE;
}

/* ---------- public code */

void online_strings_loaded(
	void)
{
	struct tag_iterator iterator;
	long tag_index;
	long rewritten = 0;

	tag_iterator_new(&iterator, UNICODE_STRING_LIST_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct string_list *list = unicode_string_list_definition_get(tag_index);
		long string_index;

		for (string_index = 0; string_index < list->strings.count; string_index++)
		{
			struct string_list_entry *entry = TAG_BLOCK_GET_ELEMENT(&list->strings, string_index,
				struct string_list_entry);

			if (entry->string.size >= (long)sizeof(wchar_t) &&
				rewrite_string(xbox_pointer(entry->string.address), entry->string.size / (long)sizeof(wchar_t)))
			{
				rewritten++;
			}
		}
	}
	if (rewritten)
		error(_error_silent, "game browser: %ld of the user interface's strings say online play", rewritten);
}

#endif
