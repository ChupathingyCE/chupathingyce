/*
H2_CAPS.C

The Halo 2 / Halo 3 converters' limits by platform tier (h2_caps.h).
*/

/* ---------- headers */

#include "h2_caps.h"

#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);

/* ---------- code */

int h2_caps_tier(void)
{
#if H2_CAPS_PC_BUILD
	static int tier = -1;

	if (tier < 0)
	{
		char const *text = getenv("HALO_H2_CAPS");

		tier = text && !strcmp(text, "vita") ? _h2_caps_tier_vita : _h2_caps_tier_pc;
		platform_log("h2 caps: the %s tier%s", tier == _h2_caps_tier_pc ? "PC" : "Vita",
			text && tier == _h2_caps_tier_vita ? " (HALO_H2_CAPS=vita)" : "");
	}
	return tier;
#else
	return _h2_caps_tier_vita;
#endif
}

long h2_caps_graph_animations(void)
{
	return h2_caps_tier() == _h2_caps_tier_pc ? H2_CAPS_MAXIMUM_GRAPH_ANIMATIONS : 1024;
}

long h2_caps_animation_variants(void)
{
	return h2_caps_tier() == _h2_caps_tier_pc ? H2_CAPS_MAXIMUM_ANIMATION_VARIANTS : 3;
}

long h2_caps_graph_seats(void)
{
	return h2_caps_tier() == _h2_caps_tier_pc ? H2_CAPS_MAXIMUM_GRAPH_SEATS : 64;
}

long h2_caps_seat_classes(void)
{
	return h2_caps_tier() == _h2_caps_tier_pc ? H2_CAPS_MAXIMUM_SEAT_CLASSES : 16;
}

long h2_caps_class_types(void)
{
	return h2_caps_tier() == _h2_caps_tier_pc ? H2_CAPS_MAXIMUM_CLASS_TYPES : 32;
}

unsigned long h2_caps_level_word(void)
{
	/* (the Vita tier's 'VT': its graphs past the Xbox's 256 too, Oct 24
	2026 - 0 before, a level made then made again) */
	return h2_caps_tier() == _h2_caps_tier_pc ? 0x50430006UL : 0x56540001UL;   /* ('PC', the PC tier's caps and fixes: 4 the seats' hovering and flying, the acceleration overlays Halo 2's again; 5 a vehicle seat's aiming its own or none; 6 boarding, the Warthog's passenger third person) */
}

int h2_caps_seat_fixes(void)
{
	/* (a fix rather than a cap: both tiers since Oct 24 2026 - the Vita's
	riders were bent double and the Ghost's lay across it) */
	return 1;
}

int h2_caps_boarding(void)
{
	/* (both tiers since Oct 24 2026, the Vita's graphs lifted) */
	return 1;
}

static unsigned long read32_le(unsigned char const *b)
{
	return (unsigned long)b[0] | (unsigned long)b[1] << 8 | (unsigned long)b[2] << 16 | (unsigned long)b[3] << 24;
}

int h2_caps_level_lifted(void const *header)
{
	unsigned char const *b = header;

	/* (h2_level.c's file: 'h2lv' at 0x700, its tier at 0x71C) */
	return b && read32_le(b + 0x700) == 0x68326C76UL && read32_le(b + 0x71C) != 0 &&
		read32_le(b + 0x71C) == h2_caps_level_word();
}
