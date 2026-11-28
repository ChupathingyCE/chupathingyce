/*
H2_SCRIPT_TEXT.C

port: the text a Halo 2 level's scripts show (h2_scripts.c): the training
prompts (hud_set_training_text, hud_show_training_text), the objectives
(objectives_show_up_to, objectives_finish_up_to) and the cinematics'
subtitles (cinematic_subtitle), read from the player's own map as the
level loads and drawn on the HUD (hud.c, after the messages).

Where MCC's Halo 2 keeps its text (read from the owner's 01a_tutorial.map):
the globals tag (matg) holds a language pack a language, 0x1C each from
0x188 - the strings' count at +8, their data's size +0xC, the file offsets
of their index (+0x10: a string id and the string's offset in the data, 8
bytes each) and of their data (+0x14: UTF-8, each ending with a 0); English
is the first. A multilingual unicode string list (unic, the scenario's
objectives at 0x3B8) names a run of the pack's strings for each language
(at 0x10, 4 bytes a language: its first and how many). Button glyphs are
the font's private characters (U+E000 on): left out here (this engine's
fonts have none), the words around them kept.

Nothing of Halo 2's text is kept in the code: it is read at run time.
*/

/* ---------- headers */

#include "cseries.h"
#include "h2_scripts.h"
#include "h2_cache.h"
#include "game/game.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "interface/interface.h"
#include "interface/hud_draw.h"
#include "interface/hud_messaging.h"
#include "game/players.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "h2_menu.h"
#ifdef HALO_LINUX
#include "render_epoch.h" /* HALO_ENV_ONCE */
#endif

#undef malloc
#undef free
#undef realloc
#undef calloc

#define H2_ALIGN __attribute__((force_align_arg_pointer))

void platform_log(char const *format, ...);
long game_time_get(void);
long hud_get_font_index(void);
int config_boolean(const char *name);
const char *config_string(const char *name);
void rasterizer_draw_unicode_string(rectangle2d const *bounds, rectangle2d const *clip, point2d *cursor_reference, short height_adjust,
	wchar_t const *string);
boolean cinematic_in_progress(void);

/* ---------- constants */

enum
{
	MATG_LANGUAGE_PACKS = 0x188,
	LANGUAGE_PACK_COUNT = 8,
	LANGUAGE_PACK_SIZE = 0x1C,
	SCENARIO_OBJECTIVES = 0x3B8,
	/* the cutscene titles (0x24: name string id, bounds 4, justification 2,
	font 2, text colour 4, shadow colour 4, fade in, up, fade out seconds) */
	SCENARIO_TITLES = 0x1F0,
	TITLE_SIZE = 0x24,
	MAXIMUM_TITLES = 32,
	UNIC_RUNS = 0x10,
	MAXIMUM_STRINGS = 8192,
	MAXIMUM_DATA = 4 * 1024 * 1024,
	TEXT_CHARACTERS = 512,
	OBJECTIVE_TICKS = 6 * 30,
	FADE_TICKS = 15,
};

/* (the screen's height in a title's font units: Halo 2's 480 lines at
MCC's doubled fonts, larger as the Xbox's capture shows them) */
#define H2_TEXT_TITLE_UNITS 740.0f
/* (the chapter titles' Conduit larger still; a cutscene's letterbox bars,
a part of the screen each, the location cards' box within the picture) */
#define H2_TEXT_CHAPTER_SCALE 1.3f
#define H2_TEXT_LETTERBOX 0.14f

/* ---------- globals */

static struct
{
	boolean loaded;
	long count;
	unsigned long *index;     /* string id, offset: 2 a string */
	char *data;
	unsigned long data_size;
	char *names;              /* each string's string id's name, 0-ended, one after another */
	unsigned long *name_offsets;
	long objectives_first, objectives_count;

	wchar_t training[TEXT_CHARACTERS];
	boolean training_shown;
	wchar_t subtitle[TEXT_CHARACTERS];
	long subtitle_until;
	wchar_t objective[TEXT_CHARACTERS];
	/* the chapter titles (cinematic_set_title) and the one shown */
	struct h2_text_title
	{
		wchar_t text[TEXT_CHARACTERS];
		real red, green, blue;
		real fade_in, up, fade_out;
		/* its text bounds (Halo 2's: distances in from the screen's bottom
		- the box's top and bottom - and right edges, 640x480 units) and
		justification (0 left, 1 right, 2 centred) */
		short bounds[4];
		short justification;
		short font;                /* (Halo 2's font table's: 2 the location cards', 9 the chapter titles') */
	} titles[MAXIMUM_TITLES];
	long title_count;
	short title_shown;
	long title_start;
	long objective_until;
	long objectives_shown;
	long objectives_finished;  /* (objectives_finish_up_to: the first not finished) */
	/* how long a new objective shows on the HUD: the HUD globals'
	objective colours' uptime ticks (Halo 2's 0: none - its objectives are
	the pause screen's); NONE not read (this file's 6 s) */
	long objective_ticks;
	/* "Updating profile..." as a campaign level starts, as Halo 2 says it
	saving the profile's progress: its words as the level's files have
	them (the language pack's hud_saving_begin and hud_saving_end - MCC's
	"Updating profile (Do not turn off your computer)..." and "Updating
	profile... done"; the Xbox's begin was "Updating profile..."), where it is (0 none,
	1 to say, 2 said, the end to say, 3 done) and when said */
	wchar_t profile_words[128];
	wchar_t profile_end_words[128];
	long profile_state;
	long profile_said;
} h2_text;

/* ---------- private code */

/* Halo 2's strings carry tokens in the font's private characters (MCC's
EN_Halo2 pack, read from the owner's 01a_tutorial.map): a verb before a
control (U+E47C "Press", U+E47D "Hold", U+E47E / U+E47F the same verb's
other forms), the control an action is on (pick up, melee, the sticks'
directions...), a line break (U+E486) and values the game fills (a
weapon's name, a count). The controls are shown as this engine's button
icons (hud_draw.c's: the PlayStation's on the Vita, the keyboard's or the
pad's on the PC: halo_button_icons.h), a marker character (TOKEN_BUTTON +
the game's button: A B X Y Black White, the triggers, the D-pad, Start,
Back, the sticks' clicks, then 16 the left stick, 17 the right) in the
text until drawn; the values are left out. Which control each action is
on is this engine's layout of Halo 2's actions (the tokens' strings name
them: "to melee", "to fire Banshee bomb"...). */
enum
{
	TOKEN_BUTTON = 0xF000,
	TOKEN_BUTTON_LAST = TOKEN_BUTTON + 17,
};

static struct
{
	unsigned short token;
	short button;           /* NONE: a word */
	char const *word;
} const text_tokens[] =
{
	{ 0xE47C, NONE, "Press" }, { 0xE47D, NONE, "Hold" }, { 0xE47E, NONE, "Press" }, { 0xE47F, NONE, "Press" },
	{ 0xE486, NONE, "\n" },
	/* (the UI's buttons) */
	{ 0xE100, 0, NULL }, { 0xE101, 1, NULL }, { 0xE102, 2, NULL }, { 0xE103, 3, NULL }, { 0xE10C, 12, NULL },
	/* (play's actions) */
	{ 0xE448, 2, NULL },    /* hold until you pick up */
	{ 0xE449, 3, NULL },    /* switch weapons */
	{ 0xE44A, 1, NULL },    /* melee */
	{ 0xE44B, 5, NULL },    /* flashlight */
	{ 0xE44C, 6, NULL },    /* grenade */
	{ 0xE44D, 7, NULL },    /* fire */
	{ 0xE450, 14, NULL },   /* crouch */
	{ 0xE451, 15, NULL },   /* zoom out */
	{ 0xE452, 15, NULL },   /* zoom in */
	{ 0xE456, 0, NULL },    /* accept normal aiming */
	{ 0xE457, 3, NULL },    /* try inverted aiming */
	{ 0xE458, 6, NULL },    /* power slide */
	{ 0xE45A, 2, NULL },    /* pick up */
	{ 0xE45B, 2, NULL },    /* take an ally's weapon */
	{ 0xE45C, 6, NULL },    /* Ghost boost */
	{ 0xE45E, 1, NULL },    /* Banshee bomb (this engine's: the melee button) */
	{ 0xE45F, 5, NULL },    /* camouflage */
	{ 0xE460, 13, NULL },   /* team chat */
	{ 0xE461, 2, NULL },    /* use (a gate, eject) */
	{ 0xE466, 2, NULL },    /* flip */
	{ 0xE467, 2, NULL },    /* enter a vehicle */
	{ 0xE468, 2, NULL },    /* board */
	{ 0xE46A, 3, NULL },    /* dual-wield */
	{ 0xE46B, 16, NULL }, { 0xE46C, 16, NULL }, { 0xE46D, 16, NULL }, { 0xE46E, 16, NULL },   /* move: its directions */
	{ 0xE46F, 17, NULL }, { 0xE470, 17, NULL }, { 0xE471, 17, NULL }, { 0xE472, 17, NULL },   /* look: its directions */
	{ 0xE473, 16, NULL },   /* the move stick */
	{ 0xE474, 17, NULL },   /* look up */
	{ 0xE47B, 6, NULL },    /* the left hand's weapon */
	{ 0xE480, 17, NULL },   /* aim the camera */
	{ 0xE481, 17, NULL }, { 0xE482, 16, NULL }, { 0xE487, 17, NULL }, { 0xE488, 16, NULL },
};

/* a string's UTF-8 as this engine's text (16-bit), its tokens made words,
button markers or nothing, and the spaces around them made one */
static void text_from_utf8(char const *utf8, wchar_t *out, long out_size)
{
	unsigned char const *p = (unsigned char const *)utf8;
	long n = 0;

	while (*p && n < out_size - 1)
	{
		unsigned long c;

		if (*p < 0x80)
			c = *p++;
		else if ((*p & 0xE0) == 0xC0 && p[1])
			c = ((unsigned long)(p[0] & 0x1F) << 6) | (p[1] & 0x3F), p += 2;
		else if ((*p & 0xF0) == 0xE0 && p[1] && p[2])
			c = ((unsigned long)(p[0] & 0x0F) << 12) | ((unsigned long)(p[1] & 0x3F) << 6) | (p[2] & 0x3F), p += 3;
		else
		{
			p++;
			continue;
		}
		if (c >= 0xE000 && c <= 0xF8FF)
		{
			long t;

			for (t = 0; t < (long)NUMBEROF(text_tokens) && text_tokens[t].token != c; t++)
				;
			if (t == (long)NUMBEROF(text_tokens))
				continue;
			if (text_tokens[t].button != NONE)
				out[n++] = (wchar_t)(TOKEN_BUTTON + text_tokens[t].button);
			else if (text_tokens[t].word[0] == '\n')
			{
				while (n > 0 && out[n - 1] == ' ')
					n--;
				out[n++] = '\n';
			}
			else
			{
				char const *w;

				if (n > 0 && out[n - 1] != ' ' && out[n - 1] != '\n')
					out[n++] = ' ';
				for (w = text_tokens[t].word; *w && n < out_size - 1; w++)
					out[n++] = (wchar_t)*w;
			}
			continue;
		}
		if (c == '\r')
			continue;
		/* (Halo 2's line break in a string: "|n") */
		if (c == '|' && *p == 'n')
		{
			p++;
			while (n > 0 && out[n - 1] == ' ')
				n--;
			out[n++] = '\n';
			continue;
		}
		if (c == ' ' && (n == 0 || out[n - 1] == ' ' || out[n - 1] == '\n'))
			continue;
		out[n++] = (wchar_t)(c > 0xFFFF ? '?' : c);
	}
	while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\n'))
		n--;
	out[n] = 0;
}

static char const *string_text(long i)
{
	unsigned long offset = h2_text.index[2 * i + 1];

	return offset < h2_text.data_size && memchr(h2_text.data + offset, 0, h2_text.data_size - offset) ? h2_text.data + offset : NULL;
}

/* the English string named name (its string id's name), NULL if none */
static char const *string_named(char const *name)
{
	long i;

	if (!h2_text.loaded || !name)
		return NULL;
	for (i = 0; i < h2_text.count; i++)
	{
		if (!strcmp(h2_text.names + h2_text.name_offsets[i], name))
			return string_text(i);
	}
	return NULL;
}

/* ---------- public code */

H2_ALIGN void h2_script_text_load(struct h2_cache *cache)
{
	long matg, scenario, objectives, i;
	struct h2_cache_tag tag;
	unsigned char const *pack;
	unsigned long index_offset, data_offset, names_size = 0, names_used = 0;
	void const *bytes;

	h2_script_text_unload();
	if (!cache || !h2_cache_is_mcc(cache))
		return;
	matg = h2_cache_tag_find(cache, 'matg', "globals\\globals");
	if (matg == NONE || !h2_cache_tag_get(cache, matg, &tag) ||
		!(pack = h2_cache_at(cache, tag.address + MATG_LANGUAGE_PACKS, LANGUAGE_PACK_SIZE)))
	{
		platform_log("h2 text: no globals' language pack");
		return;
	}
	memcpy(&h2_text.count, pack + 8, 4);
	memcpy(&h2_text.data_size, pack + 0xC, 4);
	memcpy(&index_offset, pack + 0x10, 4);
	memcpy(&data_offset, pack + 0x14, 4);
	if (h2_text.count <= 0 || h2_text.count > MAXIMUM_STRINGS || !h2_text.data_size || h2_text.data_size > MAXIMUM_DATA)
	{
		platform_log("h2 text: the language pack has %ld strings, %lu bytes: not read", h2_text.count, h2_text.data_size);
		h2_text.count = 0;
		return;
	}
	h2_text.index = malloc(8 * (size_t)h2_text.count);
	h2_text.data = malloc(h2_text.data_size + 1);
	h2_text.name_offsets = malloc(sizeof(unsigned long) * (size_t)h2_text.count);
	names_size = 48 * (unsigned long)h2_text.count;
	h2_text.names = malloc(names_size);
	if (!h2_text.index || !h2_text.data || !h2_text.name_offsets || !h2_text.names ||
		!(bytes = h2_cache_file_bytes(cache, index_offset, 8 * (unsigned long)h2_text.count)))
	{
		h2_script_text_unload();
		return;
	}
	memcpy(h2_text.index, bytes, 8 * (size_t)h2_text.count);
	/* (the data a piece at a time: the file is inflated in pieces) */
	for (i = 0; i < (long)h2_text.data_size;)
	{
		unsigned long piece = h2_text.data_size - (unsigned long)i > 0x8000 ? 0x8000 : h2_text.data_size - (unsigned long)i;

		if (!(bytes = h2_cache_file_bytes(cache, data_offset + (unsigned long)i, piece)))
		{
			h2_script_text_unload();
			return;
		}
		memcpy(h2_text.data + i, bytes, piece);
		i += (long)piece;
	}
	h2_text.data[h2_text.data_size] = 0;
	for (i = 0; i < h2_text.count; i++)
	{
		char name[256];
		unsigned long length;

		if (!h2_cache_string(cache, h2_text.index[2 * i], name, sizeof(name)))
			name[0] = 0;
		length = (unsigned long)strlen(name) + 1;
		if (names_used + length > names_size)
		{
			char *grown = realloc(h2_text.names, 2 * names_size + length);

			if (!grown)
				break;
			h2_text.names = grown;
			names_size = 2 * names_size + length;
		}
		memcpy(h2_text.names + names_used, name, length);
		h2_text.name_offsets[i] = names_used;
		names_used += length;
	}
	for (; i < h2_text.count; i++)
		h2_text.name_offsets[i] = 0;
	/* the objectives: the scenario's list's English run */
	scenario = h2_cache_scenario_tag(cache);
	if (h2_cache_tag_get(cache, scenario, &tag) && (objectives = h2_cache_reference(cache, tag.address + SCENARIO_OBJECTIVES)) != NONE &&
		h2_cache_tag_get(cache, objectives, &tag))
	{
		unsigned char const *run = h2_cache_at(cache, tag.address + UNIC_RUNS, 4);

		if (run)
		{
			unsigned short first, count;

			memcpy(&first, run, 2);
			memcpy(&count, run + 2, 2);
			if ((long)first + count <= h2_text.count)
				h2_text.objectives_first = first, h2_text.objectives_count = count;
		}
	}
	h2_text.loaded = TRUE;
	h2_text.profile_state = 0;
	{
		char const *saving = string_named("hud_saving_begin");
		char const *path = h2_cache_scenario_path(cache);

		if (saving && saving[0] && path && !strncmp(path, "scenarios\\solo\\", 15))
		{
			char const *saved = string_named("hud_saving_end");

			text_from_utf8(saving, h2_text.profile_words, (long)NUMBEROF(h2_text.profile_words));
			text_from_utf8(saved ? saved : "", h2_text.profile_end_words, (long)NUMBEROF(h2_text.profile_end_words));
			h2_text.profile_state = 1;
		}
		else
			platform_log("h2 text: no profile message (%s, %s)", saving ? saving : "no hud_saving_begin", path ? path : "?");
	}
	/* the chapter titles: their text by name, colour and timing */
	h2_text.title_shown = NONE;
	if (h2_cache_tag_get(cache, scenario, &tag))
	{
		long count;
		unsigned long titles;

		if (h2_cache_block(cache, tag.address + SCENARIO_TITLES, TITLE_SIZE, &count, &titles))
		{
			for (i = 0; i < count && i < MAXIMUM_TITLES; i++)
			{
				unsigned char const *t = h2_cache_at(cache, titles + TITLE_SIZE * (unsigned long)i, TITLE_SIZE);
				char name[128];
				char const *text;
				unsigned long id;
				float times[3];

				if (!t)
					break;
				memcpy(&id, t, 4);
				memcpy(times, t + 0x18, 12);
				name[0] = 0;
				h2_cache_string(cache, id, name, sizeof(name));
				text = string_named(name);
				text_from_utf8(text ? text : "", h2_text.titles[i].text, TEXT_CHARACTERS);
				h2_text.titles[i].red = t[0x12] / 255.0f, h2_text.titles[i].green = t[0x11] / 255.0f, h2_text.titles[i].blue = t[0x10] / 255.0f;
				h2_text.titles[i].fade_in = times[0], h2_text.titles[i].up = times[1], h2_text.titles[i].fade_out = times[2];
				memcpy(h2_text.titles[i].bounds, t + 4, 8);
				memcpy(&h2_text.titles[i].justification, t + 0xC, 2);
				memcpy(&h2_text.titles[i].font, t + 0xE, 2);
				h2_text.title_count = i + 1;
				if (getenv("HALO_H2_TITLE_LOG"))
					platform_log("h2 text: title %ld %s \"%s\" bounds %d %d %d %d justification %d font %d", i, name, text ? text : "",
						h2_text.titles[i].bounds[0], h2_text.titles[i].bounds[1], h2_text.titles[i].bounds[2], h2_text.titles[i].bounds[3],
						h2_text.titles[i].justification, (int)(short)(t[0xE] | t[0xF] << 8));
			}
		}
	}
	/* the HUD globals' objective colours' uptime: the two shorts before
	the waypoints' four offsets (top, bottom, left, right), which the
	reference to the waypoints' bitmap follows (as h2_hud_make.c finds it) */
	h2_text.objective_ticks = NONE;
	{
		long hudg = h2_cache_tag_find(cache, 'hudg', "ui\\hud\\default");
		unsigned long o;

		for (o = 24; hudg != NONE && h2_cache_tag_get(cache, hudg, &tag) && o < 0x400; o += 4)
		{
			long reference = h2_cache_reference(cache, tag.address + o);
			char const *name = reference != NONE ? h2_cache_tag_name(cache, reference) : NULL;
			unsigned char const *before = name && strstr(name, "hud_waypoints") ? h2_cache_at(cache, tag.address + o - 20, 20) : NULL;

			if (before)
			{
				float top;
				short uptime;

				memcpy(&top, before + 4, 4);
				memcpy(&uptime, before, 2);
				if (top >= 0.0f && top < 200.0f && uptime >= 0)
					h2_text.objective_ticks = uptime;
				break;
			}
		}
	}
	platform_log("h2 text: %ld strings (%lu KB), %ld objectives (shown on the HUD for %ld ticks), %ld chapter titles", h2_text.count,
		h2_text.data_size / 1024, h2_text.objectives_count, h2_text.objective_ticks, h2_text.title_count);
}

void h2_script_text_unload(void)
{
	free(h2_text.index);
	free(h2_text.data);
	free(h2_text.names);
	free(h2_text.name_offsets);
	memset(&h2_text, 0, sizeof(h2_text));
}

/* hud_set_training_text: the string named; FALSE if none */
boolean h2_script_text_training(char const *name)
{
	char const *text = string_named(name);

	if (!text)
	{
		h2_text.training[0] = 0;
		return FALSE;
	}
	text_from_utf8(text, h2_text.training, TEXT_CHARACTERS);
	return TRUE;
}

void h2_script_text_training_show(boolean shown)
{
	h2_text.training_shown = shown;
}

/* objectives_show_up_to / objectives_finish_up_to n: the newest shown for
a while; FALSE if the list has no such objective */
boolean h2_script_text_objective(long n, boolean finished)
{
	char const *text;
	char line[TEXT_CHARACTERS * 3];

	if (!h2_text.loaded || n < 0 || n >= h2_text.objectives_count || !(text = string_text(h2_text.objectives_first + n)))
		return FALSE;
	if (!finished && n < h2_text.objectives_shown)
		return TRUE;
	if (!finished)
		h2_text.objectives_shown = n + 1;
	else if (n + 1 > h2_text.objectives_finished)
		h2_text.objectives_finished = n + 1;
	platform_log("h2 text: objective %ld %s", n, finished ? "finished" : "shown");
	snprintf(line, sizeof(line), "%s%s", finished ? "Completed: " : "", text);
	text_from_utf8(line, h2_text.objective, TEXT_CHARACTERS);
	h2_text.objective_until = game_time_get() + (h2_text.objective_ticks != NONE ? h2_text.objective_ticks : OBJECTIVE_TICKS);
	return TRUE;
}

/* objectives_clear: none shown, the list begun again */
void h2_script_text_objectives_clear(void)
{
	h2_text.objectives_shown = 0;
	h2_text.objectives_finished = 0;
	h2_text.objective[0] = 0;
	h2_text.objective_until = 0;
}

/* (the pause, h2_menu.c) the objectives shown so far, a line each (the
finished ones marked), in ASCII - the font's private characters left out;
the characters written */
long h2_script_text_objectives_list(char *out, long out_size)
{
	long n, length = 0;

	if (out_size < 1)
		return 0;
	out[0] = 0;
	for (n = 0; h2_text.loaded && n < h2_text.objectives_shown && n < h2_text.objectives_count; n++)
	{
		char const *text = string_text(h2_text.objectives_first + n);
		unsigned char const *c;

		if (!text)
			continue;
		if (length && length < out_size - 1)
			out[length++] = '\n';
		if (n < h2_text.objectives_finished)
		{
			int written = snprintf(out + length, (size_t)(out_size - length), "%s", "(Completed) ");

			length = written > 0 && length + written < out_size ? length + written : out_size - 1;
		}
		if (length >= out_size - 1)
			break;
		for (c = (unsigned char const *)text; *c && length < out_size - 1; c++)
		{
			if (*c < 0x80 && *c != '\r' && *c != '\n')
				out[length++] = (char)*c;
			else if (*c == '\n' && length < out_size - 1)
				out[length++] = ' ';
		}
		out[length] = 0;
	}
	return length;
}

/* cinematic_set_title (title): shown, faded in and out as its times say;
FALSE if there is no such title */
boolean h2_script_text_title(short index)
{
	if (index < 0 || index >= h2_text.title_count || !h2_text.titles[index].text[0])
		return FALSE;
	h2_text.title_shown = index;
	h2_text.title_start = game_time_get();
	platform_log("h2 text: chapter title %d shown at tick %ld", index, h2_text.title_start);
	return TRUE;
}

/* cinematic_subtitle name seconds */
boolean h2_script_text_subtitle(char const *name, real seconds)
{
	char const *text = string_named(name);

	if (!text)
		return FALSE;
	/* (subtitles not on - Halo 2's profiles' Automatic, their default, or
	Off; game.h2_subtitles -: the line known, not shown) */
	{
		char const *setting = config_string("game.h2_subtitles");

		if (!setting || (strcmp(setting, "on") && strcmp(setting, "true")))
			return TRUE;
	}
	text_from_utf8(text, h2_text.subtitle, TEXT_CHARACTERS);
	h2_text.subtitle_until = game_time_get() + (long)(seconds * 30.0f + 0.5f);
	return TRUE;
}

/* a line of text centred (justification 2) or left at x, its top y, faded
by alpha: white with a dark shadow, as Halo 2's */
static real text_red = 1.0f, text_green = 1.0f, text_blue = 1.0f;

/* a run's width as the HUD's font draws it */
static short text_width(wchar_t const *text)
{
	rectangle2d bounds = { 0, 0, 480, 4000 }, text_bounds, cursor_bounds;
	real_argb_color color = { 1.0f, 1.0f, 1.0f, 1.0f };

	if (!text[0])
		return 0;
	draw_string_set_draw_mode(hud_get_font_index(), NONE, 0, 0, &color);
	draw_unicode_string_compute_bounds(&bounds, text, &text_bounds, &cursor_bounds);
	return (short)(text_bounds.x1 - text_bounds.x0);
}

/* a button marker's icon (hud_draw.c), or its name where it has none */
static wchar_t const *button_name(short button)
{
	static wchar_t const *const names[18] =
	{
		L"[A]", L"[B]", L"[X]", L"[Y]", L"[BLACK]", L"[WHITE]", L"[LT]", L"[RT]", L"[D-PAD UP]", L"[D-PAD DOWN]",
		L"[D-PAD LEFT]", L"[D-PAD RIGHT]", L"[START]", L"[BACK]", L"[LS]", L"[RS]", L"[MOVE]", L"[LOOK]",
	};

	return button >= 0 && button < 18 ? names[button] : L"";
}

static void text_line_draw(wchar_t const *text, short x0, short x1, short y, short height, short justification, real alpha);
/* (a chapter title drawn: text_line_draw's style for it) */
static boolean text_title_style;
long tag_loaded(long group_tag, char const *name);

/* a line with button markers: its runs of text and its icons one after
another, centred (justification 2) or from the left */
static void text_line_icons_draw(wchar_t const *text, short x0, short x1, short y, short height, short justification, real alpha)
{
	wchar_t run[TEXT_CHARACTERS];
	short icon = (short)(height * 3 / 4), width = 0, x;
	long i, n;

	/* (measured first) */
	for (i = 0, n = 0; ; i++)
	{
		if (!text[i] || (text[i] >= TOKEN_BUTTON && text[i] <= TOKEN_BUTTON_LAST))
		{
			run[n] = 0;
			width = (short)(width + text_width(run) + (n && text[i] ? icon / 4 : 0));
			n = 0;
			if (!text[i])
				break;
#ifdef HALO_BUTTON_GLYPHS
			if (hud_vita_glyph_drawn(hud_vita_button_glyph((short)(text[i] - TOKEN_BUTTON))))
				width = (short)(width + icon + 4);
			else
#endif
				width = (short)(width + text_width(button_name((short)(text[i] - TOKEN_BUTTON))) + 4);
			continue;
		}
		if (n < TEXT_CHARACTERS - 1)
			run[n++] = text[i];
	}
	x = justification == 2 ? (short)((x0 + x1 - width) / 2) : x0;
	for (i = 0, n = 0; ; i++)
	{
		if (!text[i] || (text[i] >= TOKEN_BUTTON && text[i] <= TOKEN_BUTTON_LAST))
		{
			run[n] = 0;
			if (n)
			{
				text_line_draw(run, x, x1 + 400, y, height, 0, alpha);
				/* (and a space's width before an icon after it) */
				x = (short)(x + text_width(run) + (text[i] ? icon / 4 : 0));
			}
			n = 0;
			if (!text[i])
				break;
			{
				short button = (short)(text[i] - TOKEN_BUTTON);
				boolean drawn = FALSE;
#ifdef HALO_BUTTON_GLYPHS
				short glyph = hud_vita_button_glyph(button);
				real_rectangle2d rectangle;

				rectangle.x0 = (real)(x + 2), rectangle.x1 = (real)(x + 2 + icon);
				rectangle.y0 = (real)(y + (height - icon) / 2 - 1), rectangle.y1 = rectangle.y0 + (real)icon;
				if (hud_vita_glyph_drawn(glyph) &&
					hud_vita_glyph_draw_in_rect(glyph, &rectangle, ((unsigned long)(alpha * 255.0f) << 24) | 0xFFFFFFUL))
				{
					x = (short)(x + icon + 4);
					drawn = TRUE;
				}
#endif
				if (!drawn)
				{
					wchar_t const *name = button_name(button);

					text_line_draw(name, (short)(x + 2), x1 + 400, y, height, 0, alpha);
					x = (short)(x + text_width(name) + 4);
				}
			}
			continue;
		}
		if (n < TEXT_CHARACTERS - 1)
			run[n++] = text[i];
	}
}

static void text_line_draw(wchar_t const *text, short x0, short x1, short y, short height, short justification, real alpha)
{
	rectangle2d bounds;
	real_argb_color color;
	long i;

	if (!text[0] || alpha <= 0.0f)
		return;
	for (i = 0; text[i]; i++)
	{
		if (text[i] >= TOKEN_BUTTON && text[i] <= TOKEN_BUTTON_LAST)
		{
			text_line_icons_draw(text, x0, x1, y, height, justification, alpha);
			return;
		}
	}
	/* (a chapter title: as MCC's Halo 2 draws it - small, thin, light
	grey, no shadow -: the menus' small face, pc\h3\font_small, where the
	map has it; the rest with the HUD's font and a shadow) */
	if (text_title_style)
	{
		long font = tag_loaded('font', "pc\\h3\\font_small");

		color.alpha = alpha, color.red = text_red, color.green = text_green, color.blue = text_blue;
		bounds.x0 = x0, bounds.x1 = x1, bounds.y0 = y, bounds.y1 = (short)(y + height);
		draw_string_set_draw_mode(font != NONE ? font : hud_get_font_index(), NONE, justification, 0, &color);
		rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, text);
		return;
	}
	color.alpha = alpha * 0.8f, color.red = 0.0f, color.green = 0.0f, color.blue = 0.0f;
	bounds.x0 = (short)(x0 + 1), bounds.x1 = (short)(x1 + 1), bounds.y0 = (short)(y + 1), bounds.y1 = (short)(y + height + 1);
	draw_string_set_draw_mode(hud_get_font_index(), NONE, justification, 0, &color);
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, text);
	color.alpha = alpha, color.red = text_red, color.green = text_green, color.blue = text_blue;
	bounds.x0 = x0, bounds.x1 = x1, bounds.y0 = y, bounds.y1 = (short)(y + height);
	draw_string_set_draw_mode(hud_get_font_index(), NONE, justification, 0, &color);
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, text);
}

/* text_line_draw a line at a time: broken at spaces to the bounds'
width (this engine's string drawing does not wrap), the lines a
twenty-eighth of the screen apart */
static void text_draw(wchar_t const *text, short x0, short x1, short y, short screen_height, short justification, real alpha)
{
	long per_line = (x1 - x0) / 9, line_height = screen_height / 28, start = 0, length = (long)wcslen(text), line = 0;

	if (per_line < 16)
		per_line = 16;
	while (start < length && line < 4)
	{
		wchar_t piece[TEXT_CHARACTERS];
		long end = start + per_line, k;

		if (end >= length)
			end = length;
		else
		{
			for (k = end; k > start && text[k] != ' '; k--)
				;
			if (k > start)
				end = k;
		}
		for (k = start; k < end && text[k] != '\n'; k++)
			piece[k - start] = text[k];
		piece[k - start] = 0;
		end = k;
		text_line_draw(piece, x0, x1, (short)(y + line * line_height), (short)(line_height + 4), justification, alpha);
		start = end;
		while (start < length && (text[start] == ' ' || text[start] == '\n'))
			start++;
		line++;
	}
}

static real fade(long until, long now)
{
	long left = until - now;

	return left <= 0 ? 0.0f : left >= FADE_TICKS ? 1.0f : (real)left / (real)FADE_TICKS;
}

/* (hud.c, after the messages) the scripts' text on the HUD */
void h2_script_text_draw(short local_player_index)
{
	short x0 = render.camera.viewport_bounds.x0, x1 = render.camera.viewport_bounds.x1;
	short y0 = render.camera.viewport_bounds.y0, y1 = render.camera.viewport_bounds.y1;
	short height = (short)(y1 - y0), width = (short)(x1 - x0);
	long now = game_time_get();

	(void)local_player_index;
	if (!h2_text.loaded)
		return;
	/* (the profile's saving message a second into the level's play - after
	its opening cutscene -, its saved message a second and a half on, on
	each local player's HUD messages) */
	if (local_player_index == 0 && (h2_text.profile_state == 1 || h2_text.profile_state == 2) && !cinematic_in_progress() && now >= 30 &&
		(h2_text.profile_state == 1 || now - h2_text.profile_said >= 45))
	{
		wchar_t const *message = h2_text.profile_state == 1 ? h2_text.profile_words : h2_text.profile_end_words;
		short k;

		/* (the end's words in the place of the beginning's, as the
		checkpoints' are; none: the beginning's left to fade) */
		if (h2_text.profile_state == 2 && message[0])
			scripted_hud_messages_clear();
		for (k = 0; message[0] && k < MAXIMUM_LOCAL_PLAYERS; k++)
		{
			if (local_player_get_player_index(k) != NONE)
				hud_print_message(k, message);
		}
		platform_log("h2 text: the profile's %s message at tick %ld%s", h2_text.profile_state == 1 ? "saving" : "saved", now,
			message[0] ? "" : " (none in the files)");
		h2_text.profile_said = now;
		h2_text.profile_state++;
	}
	/* (a test: HALO_H2_TITLE_AT="tick:title" shows that title then) */
	{
		static long shown_at = NONE;
		char const *test = HALO_ENV_ONCE("HALO_H2_TITLE_AT");
		long tick, title;

		if (test && shown_at == NONE && sscanf(test, "%ld:%ld", &tick, &title) == 2 && now >= tick)
		{
			shown_at = now;
			h2_script_text_title((short)title);
		}
	}
	/* (the subtitles: under the letterbox's middle, a fifth up from the
	bottom) */
	if (h2_text.subtitle[0] && now < h2_text.subtitle_until)
		text_draw(h2_text.subtitle, (short)(x0 + width / 8), (short)(x1 - width / 8), (short)(y1 - height / 5), height, 2,
			fade(h2_text.subtitle_until, now));
	/* (a chapter title as Halo 2 draws it - its tag-test build's title
	draw, read from the H2EK's halo2_tag_test.exe: the box's top its
	bounds' top above the screen's bottom, its bottom its bottom above it,
	its right edge its right in from the screen's right (640x480 units
	over the screen's height), its left the screen's; the text in it from
	its top, justified as the title says (the campaign's: right) - so the
	bottom right corner; faded in over its fade in time, held, faded out
	from its up time over its fade out time, gone at fade in + up) */
	if (h2_text.title_shown != NONE)
	{
		struct h2_text_title const *title = &h2_text.titles[h2_text.title_shown];
		real seconds = (real)(now - h2_text.title_start) / 30.0f;
		real in = title->fade_in, up = title->up, out = title->fade_out;
		real alpha = seconds < in ? (in > 0.0f ? seconds / in : 1.0f) : seconds > up ? (out > 0.0f ? 1.0f - (seconds - up) / out : 0.0f) :
			1.0f;

		if (alpha < 0.0f)
			alpha = 0.0f;
		if (seconds >= in + up)
			h2_text.title_shown = NONE;
		else
		{
			real s = (real)height / 480.0f;
			short top = (short)(y1 - title->bounds[0] * s), bottom = (short)(y1 - title->bounds[2] * s);
			short right = (short)(x1 - title->bounds[3] * s);
			short justification = title->justification >= 0 && title->justification <= 2 ? title->justification : 1;

			text_red = title->red;
			text_green = title->green;
			text_blue = title->blue;
			/* (none or white: MCC's light grey) */
			if (text_red + text_green + text_blue < 0.05f || text_red + text_green + text_blue > 2.9f)
				text_red = text_green = text_blue = 0.8f;
			/* (in Halo 2's own face from the level's menu file, as the
			Xbox's: the last line's baseline on the box's bottom - in a
			cutscene, the box within the letterbox's picture) */
			{
				char ascii[TEXT_CHARACTERS];
				long k;
				real picture_top = (real)y0, picture = (real)height;
				boolean drawn;

				if (cinematic_in_progress())
					picture_top += H2_TEXT_LETTERBOX * (real)height, picture *= 1.0f - 2.0f * H2_TEXT_LETTERBOX;
				for (k = 0; title->text[k] && k < TEXT_CHARACTERS - 1; k++)
					ascii[k] = title->text[k] == '\n' || (title->text[k] >= 0x20 && title->text[k] < 0x100) ? (char)title->text[k] : '?';
				ascii[k] = 0;
				drawn = x0 < right && h2_menu_hud_text(title->font, ascii, (real)x0, (real)right,
					picture_top + picture - (real)title->bounds[2] * picture / 480.0f, justification, text_red, text_green, text_blue, alpha,
					(real)height / H2_TEXT_TITLE_UNITS * (title->font == 9 ? H2_TEXT_CHAPTER_SCALE : 1.0f));
				/* (no such face: this engine's small one, from the box's top) */
				if (!drawn)
				{
					text_title_style = TRUE;
					if (top < bottom && x0 < right)
						text_draw(title->text, x0, right, top, height, justification, alpha);
					else
						text_draw(title->text, (short)(x0 + width / 8), (short)(x1 - width / 8), (short)(y0 + height * 3 / 5), height, 2, alpha);
					text_title_style = FALSE;
				}
			}
			text_red = text_green = text_blue = 1.0f;
		}
	}
	if (cinematic_in_progress())
		return;
	/* (the training prompt: centred, a third down) */
	if (h2_text.training_shown && h2_text.training[0])
		text_draw(h2_text.training, (short)(x0 + width / 6), (short)(x1 - width / 6), (short)(y0 + height / 3), height, 2, 1.0f);
	/* (a new objective: left, under the HUD's top) */
	if (h2_text.objective[0] && now < h2_text.objective_until)
		text_draw(h2_text.objective, (short)(x0 + width / 24), (short)(x0 + width / 2), (short)(y0 + height / 5), height, 0,
			fade(h2_text.objective_until, now));
}
