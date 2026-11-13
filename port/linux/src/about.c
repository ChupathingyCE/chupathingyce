/*
ABOUT.C

The About screen's page (about.h): CHUPATHINGY, CONTRIBUTORS, THANKS and a
LICENSES row, top to bottom, as lines that scroll; the LICENSES row opens
the list of what Chupathingy uses and their licenses in its place. The same
words as the README's Credits, by handle only.

Drawn in DELTA (guide.c) with the overlay's text and rows, so it's built
where the Guide is (HALO_GUIDE). The consoles have no Guide yet
(HALO_CONSOLE_NO_GUIDE) and keep the game menu's About.
*/

#include "halo_console_features.h"

#ifdef HALO_GUIDE

#include "about.h"
#include "build_identity.h"
#include "ui_overlay.h"

#include <stdio.h>
#include <string.h>

enum
{
	_line_heading,
	_line_name,
	_line_text,
	_line_gap,
	_line_licenses,
	/* (made when drawn: the version, and the platform) */
	_line_version,
	_line_platform,
};

struct about_entry
{
	int kind;
	const char *text;
};

static const struct about_entry page[] =
{
	{ _line_heading, "CHUPATHINGY" },
	{ _line_version, NULL },
	{ _line_platform, NULL },
	{ _line_text, "stats.chupathingy.org" },
	{ _line_text, "github.com/ChupathingyCE/chupathingyce" },
	{ _line_text, "By Milenko and contributors." },
	{ _line_gap, NULL },

	{ _line_heading, "CONTRIBUTORS" },
	{ _line_text, "Tyberious, thelinkin3000, xshxdex98, zimm3rmann, nsafran1217, saulob, natsu-anon, lantos1618, "
		"pfista, JoshRob297, HiIAmMoot, oatkrs, Renkoto, JizzyRivers, startupfoundry, pastudan, chestahh, "
		"mOTA-lEV, bitmoda, theLlamaNet." },
	{ _line_gap, NULL },

	{ _line_heading, "THANKS" },
	{ _line_text, "HUD art: Aerocatia and Jessie (Halo PC Restored)." },
	{ _line_text, "Interface font: fizzy (Cyborg Interface Gothic)." },
	{ _line_text, "Controller icons: snapchipper." },
	{ _line_text, "HD HUD testing: ugoboom." },
	{ _line_text, "Chimera: SnowyMouse and contributors." },
	{ _line_text, "Xbox 3D sound and reverb: the OpenH1 contributors and chestahh." },
	{ _line_text, "Security reports: nickarcade." },
	{ _line_text, "Grifball court: SlappyThePirate." },
	{ _line_text, "Testing: Sabriel, ugoboom, roundzabout and Mega." },
	{ _line_text, "HaloMD map names: MacGamingMods." },
	{ _line_text, "Map downloads: HaloNet and the HAC2 map repository." },
	{ _line_gap, NULL },
	{ _line_text, "Built on the Halo decompilation by punpckhdq and bnunu." },
	{ _line_text, "Began as a fork of OpenCE, up to October 9, 2026." },
	{ _line_text, "Fixes from OpenCE contributors: startupfoundry, "
		"xshxdex98, hkmodd, eltanschauung, justinfilip, bertoaldo, addisonbair, thelinkin3000, bmanturner, "
		"chestahh, FDervish, frankyfife, actes2, crowtrx and MrBruh." },
	{ _line_text, "Also from OpenCE: the icon's helmet and Custom Edition loader findings (MrBruh), the wire format "
		"tool (addisonbair and MrBruh), Custom Edition map findings (bnunu)." },
	{ _line_gap, NULL },

	{ _line_licenses, "LICENSES" },
};

static const struct about_entry licenses[] =
{
	{ _line_heading, "LICENSES" },
	{ _line_text, "Chupathingy: GPL-3.0 or later." },
	{ _line_gap, NULL },
	{ _line_text, "SDL3: zlib License." },
	{ _line_text, "stb: MIT or public domain." },
	{ _line_text, "Mbed TLS: Apache-2.0." },
	{ _line_text, "miniupnpc: BSD-3-Clause." },
	{ _line_text, "KCP: MIT." },
	{ _line_text, "tomlc17: MIT." },
	{ _line_text, "musl's maths: MIT." },
	{ _line_text, "Expat: MIT." },
	{ _line_text, "Monocypher: BSD-2-Clause or CC0." },
	{ _line_text, "zlib: zlib License." },
	{ _line_text, "Zstandard: BSD-3-Clause." },
	{ _line_text, "LZMA SDK decoder (Igor Pavlov): public domain." },
	{ _line_text, "Opus: BSD-3-Clause." },
	{ _line_text, "bcdec: MIT." },
	{ _line_text, "SMAA: MIT." },
	{ _line_text, "QR Code generator (Project Nayuki): MIT." },
	{ _line_text, "bgfx, bx and bimg (Branimir Karad\xC5\xBEi\xC4\x87): BSD-2-Clause." },
	{ _line_text, "glslang: BSD-3-Clause. SPIRV-Cross and the ASTC encoder: Apache-2.0." },
	{ _line_text, "Steam link code (Milenko x Horrible Dev): MIT." },
	{ _line_gap, NULL },
	{ _line_text, "Cyborg Interface Gothic (fizzy): SIL OFL." },
	{ _line_text, "Chupathingy Titles (Newtown by Roger White, public domain): SIL OFL." },
	{ _line_text, "Overpass: SIL OFL." },
	{ _line_text, "Noto Sans (letters Cyborg lacks): SIL OFL." },
	{ _line_text, "Kenney's Input Prompts: CC0." },
	{ _line_text, "Controller icons (snapchipper): CC0." },
	{ _line_text, "Platform logos: Simple Icons (CC0) and Wikimedia Commons (public domain)." },
	{ _line_text, "BSD Daemon used with permission." },
	{ _line_text, "Halo PC Restored HUD art: with Aerocatia's permission." },
	{ _line_gap, NULL },
	{ _line_text, "Full texts: THIRD-PARTY-NOTICES.txt in the download, and on GitHub." },
};

enum
{
	MAXIMUM_LINES = 96,
	LINE_SIZE = 160,
	LICENSES_WIDTH = 150,
	LICENSES_HEIGHT = 24,
};

struct about_line
{
	int kind;
	char text[LINE_SIZE];
};

static struct about_line lines[MAXIMUM_LINES];

static int line_font(int kind)
{
	return kind == _line_text || kind == _line_platform ? UI_FONT_MENU_SMALL : UI_FONT_MENU;
}

/* (as ui_style_text's kinds draw them) */
static float line_size(int kind)
{
	return ui_style_text_size(kind == _line_heading ? UI_TEXT_HEADING :
		kind == _line_version ? UI_TEXT_NAME : UI_TEXT_LINE);
}

static float line_height(int kind)
{
	switch (kind)
	{
	case _line_heading: return 18.0f;
	case _line_version: return 17.0f;
	case _line_gap: return 8.0f;
	case _line_licenses: return LICENSES_HEIGHT + 4.0f;
	default: return 14.0f;
	}
}

static int line_add(int count, int kind, const char *text, int length)
{
	if (count >= MAXIMUM_LINES)
		return count;
	if (length >= LINE_SIZE)
		length = LINE_SIZE - 1;
	lines[count].kind = kind;
	memcpy(lines[count].text, text, (size_t)length);
	lines[count].text[length] = '\0';
	return count + 1;
}

/* an entry's text, broken into lines at spaces to fit width */
static int line_wrap(int count, int kind, const char *text, float width)
{
	int font = line_font(kind);
	float size = line_size(kind);
	char trial[LINE_SIZE];

	while (*text)
	{
		int fit = 0, end;

		/* (the most words that fit; at least one) */
		for (end = 0;; end++)
		{
			if (text[end] == ' ' || !text[end])
			{
				int length = end < LINE_SIZE - 1 ? end : LINE_SIZE - 1;

				memcpy(trial, text, (size_t)length);
				trial[length] = '\0';
				if (fit && ui_overlay_text_width(font, size, trial) > width)
					break;
				fit = end;
				if (!text[end])
					break;
			}
		}
		count = line_add(count, kind, text, fit);
		text += fit;
		while (*text == ' ')
			text++;
	}
	return count;
}

static void version_text(char *text, int size)
{
	const char *watermark = build_identity_watermark();
	/* (without an update's notice after it) */
	const char *notice = strstr(watermark, " - ");

	snprintf(text, (size_t)size, "Chupathingy %.*s", notice ? (int)(notice - watermark) : (int)strlen(watermark),
		watermark);
}

static int lines_make(const struct about_entry *entries, int entry_count, float width)
{
	int count = 0, index;
	char text[LINE_SIZE];

	for (index = 0; index < entry_count; index++)
	{
		const struct about_entry *entry = &entries[index];

		switch (entry->kind)
		{
		case _line_version:
			/* (a long version, a test build's or a nightly's, broken at its
			spaces to fit; about_render shrinks a piece still too wide) */
			version_text(text, sizeof(text));
			count = line_wrap(count, _line_version, text, width);
			break;
		case _line_platform:
			snprintf(text, sizeof(text), "%s %s, %d-bit", build_identity_platform(), build_identity_architecture(),
				(int)sizeof(void *) * 8);
			count = line_wrap(count, _line_platform, text, width);
			break;
		case _line_text:
			count = line_wrap(count, _line_text, entry->text, width);
			break;
		default:
			count = line_add(count, entry->kind, entry->text ? entry->text : "", entry->text ? (int)strlen(entry->text) : 0);
			break;
		}
	}
	return count;
}

/* a small arrow: more above (up) or below */
static void arrow(float x, float y, int up)
{
	int step;

	for (step = 0; step < 4; step++)
	{
		float half = up ? (float)step + 0.5f : 3.5f - (float)step;

		ui_overlay_rect(x - half, y + (float)step, half * 2.0f, 1.0f, 0, ui_style_color(UI_COLOR_LABEL));
	}
}

void about_reset(struct about_view *view)
{
	memset(view, 0, sizeof(*view));
}

int about_input(struct about_view *view, int input)
{
	switch (input)
	{
	case ABOUT_INPUT_UP:
		if (view->scroll > 0)
			view->scroll--;
		return 1;
	case ABOUT_INPUT_DOWN:
		if (view->scroll < view->scroll_limit)
			view->scroll++;
		return 1;
	case ABOUT_INPUT_A:
		if (!view->licenses)
		{
			view->licenses = 1;
			view->scroll = 0;
			view->scroll_limit = 0;
		}
		return 1;
	case ABOUT_INPUT_B:
		if (view->licenses)
		{
			view->licenses = 0;
			view->scroll = 0;
			view->scroll_limit = 0;
			return 1;
		}
		return 0;
	default:
		return 0;
	}
}

const char *about_a_word(const struct about_view *view)
{
	return view->licenses ? NULL : "LICENSES";
}

const char *about_b_word(const struct about_view *view, const char *close)
{
	return view->licenses ? "BACK" : close;
}

void about_render(struct about_view *view, float left, float top, float right, float bottom)
{
	float width = right - left - 16.0f;
	int count = view->licenses ? lines_make(licenses, (int)(sizeof(licenses) / sizeof(licenses[0])), width) :
		lines_make(page, (int)(sizeof(page) / sizeof(page[0])), width);
	float total = 0.0f, y, height = bottom - top;
	int index, last;

	/* (how far it scrolls: the first line from which the rest fit) */
	view->scroll_limit = 0;
	for (index = count - 1; index >= 0; index--)
	{
		total += line_height(lines[index].kind);
		if (total > height)
		{
			view->scroll_limit = index + 1;
			break;
		}
	}
	if (view->scroll > view->scroll_limit)
		view->scroll = view->scroll_limit;
	view->licenses_x0 = view->licenses_x1 = 0.0f;
	y = top;
	for (index = view->scroll, last = view->scroll; index < count; index++)
	{
		const struct about_line *line = &lines[index];
		float line_bottom = y + line_height(line->kind);

		if (line_bottom > bottom + 0.5f)
			break;
		switch (line->kind)
		{
		case _line_heading:
			ui_style_text(UI_TEXT_HEADING, left, y + 4.0f, UI_ALIGN_LEFT, line->text);
			break;
		case _line_version:
		{
			/* (a piece with no space to break at, smaller to fit: never past
			the panel's edge) */
			float shown = ui_overlay_text_width(line_font(line->kind), line_size(line->kind), line->text);

			ui_style_text_scaled(UI_TEXT_NAME, shown > width ? width / shown : 1.0f, left, y, UI_ALIGN_LEFT,
				line->text);
			break;
		}
		case _line_licenses:
			/* (the one row that does something: lit, as the selection) */
			ui_style_row(left, y + 2.0f, LICENSES_WIDTH, LICENSES_HEIGHT, 1);
			ui_overlay_text_glow(UI_FONT_MENU, 11.0f, left + LICENSES_WIDTH * 0.5f, y + 8.0f, UI_ALIGN_CENTER,
				0xFFFFFFFF, ui_style_color(UI_COLOR_HEAD_GLOW), line->text);
			view->licenses_x0 = left;
			view->licenses_y0 = y + 2.0f;
			view->licenses_x1 = left + LICENSES_WIDTH;
			view->licenses_y1 = y + 2.0f + LICENSES_HEIGHT;
			break;
		case _line_text:
		case _line_platform:
			ui_style_text(UI_TEXT_LINE, left + 8.0f, y, UI_ALIGN_LEFT, line->text);
			break;
		default:
			break;
		}
		y = line_bottom;
		last = index + 1;
	}
	if (view->scroll > 0)
		arrow(right - 6.0f, top - 6.0f, 1);
	if (last < count)
		arrow(right - 6.0f, bottom + 1.0f, 0);
}

#endif /* HALO_GUIDE */
