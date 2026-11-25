/*
DEVELOPER_FLAMES_RULES.H

The rules of a verified developer's blue flame (port/linux/game/
developer_flames.c, display.developer_flames): who has one, when it is
drawn, how many there may be, and the blue copy of the map's own flame
sprite. Header only (static inline functions, no game or SDL dependency),
so tools/test_developer_flames.py builds and tests it anywhere.

Who: a player whose card this copy checked (docs/delta/peer.md, "Verified
names") from a bound proof (one a host can't replay from another game),
from a certificate of its own network, shown by their verified name
(not masked as a look-alike), whose certificate carries the developer mark
(docs/delta/link.md, "Device identity": account marks), with the viewer's
display.developer_flames and display.profile_names on, in a game on Delta.
Looks only: nothing here is sent anywhere or read by the simulation.
*/

#ifndef DEVELOPER_FLAMES_RULES_H
#define DEVELOPER_FLAMES_RULES_H

/* the mark (identity.h's IDENTITY_MARK_DEVELOPER, the registry's
device_account_marks) */
#define DEVELOPER_FLAMES_MARK 0x01000000UL

enum
{
	/* the most flame sprites alive on one helmet, and in all */
	DEVELOPER_FLAMES_PER_HELMET = 12,
	DEVELOPER_FLAMES_MOST = 96,
	/* the most helmets with a flame at once */
	DEVELOPER_FLAMES_WEARERS = 8,
};

/* a player's card as this copy has it */
struct developer_flames_card
{
	/* the viewer's display.developer_flames and display.profile_names, and
	the game on Delta */
	int option_on, names_on, on_delta;
	/* a card this copy checked (delta_peer_game_player_identity), of
	another network than this build's own, its player shown by their
	verified name, and masked as a look-alike */
	int checked, other_network, name_shown, looks_like;
	/* the proof was bound to the host its player connected to
	(DELTA_WIRE_IDENTITY_BOUND): an unbound card could be another game's,
	replayed by the host */
	int bound;
	unsigned long capabilities;
};

/* whether a player's helmet has the flame */
static inline int developer_flames_card_shows(const struct developer_flames_card *card)
{
	return card->option_on && card->names_on && card->on_delta && card->checked && card->bound &&
		!card->other_network && card->name_shown && !card->looks_like && (card->capabilities & DEVELOPER_FLAMES_MARK) != 0;
}

/* a wearer as the viewer sees them this frame */
struct developer_flames_wearer
{
	/* the unit has a head marker, is alive, drawn (not an invisible seat's)
	and not camouflaged; it is this view's own player in first person */
	int has_head, alive, drawn, camouflaged, own_first_person;
	/* from the camera, in world units */
	float distance;
};

/* (full to here, gone at the far distance: world units) */
#define DEVELOPER_FLAMES_NEAR 20.0f
#define DEVELOPER_FLAMES_FAR 32.0f

/* how much of a wearer's flame shows in this view, 0 to 1: none for a dead
player's body, an invisible or camouflaged one (a flame never gives one
away), the viewer's own head in first person, or far away */
static inline float developer_flames_wearer_fade(const struct developer_flames_wearer *wearer)
{
	if (!wearer->has_head || !wearer->alive || !wearer->drawn || wearer->camouflaged || wearer->own_first_person)
		return 0.0f;
	if (!(wearer->distance >= 0.0f) || wearer->distance >= DEVELOPER_FLAMES_FAR)
		return 0.0f;
	if (wearer->distance <= DEVELOPER_FLAMES_NEAR)
		return 1.0f;
	return (DEVELOPER_FLAMES_FAR - wearer->distance) / (DEVELOPER_FLAMES_FAR - DEVELOPER_FLAMES_NEAR);
}

/* whether one more flame sprite may start: under the helmet's and the
whole budget, and the game's particles under three quarters of their limit
(pool_limit 0 or less: no limit), so a flame never crowds out the map's own
effects */
static inline int developer_flames_may_spawn(int helmet_live, int all_live, long pool_live, long pool_limit)
{
	if (helmet_live >= DEVELOPER_FLAMES_PER_HELMET || all_live >= DEVELOPER_FLAMES_MOST)
		return 0;
	return pool_limit <= 0 || pool_live * 4 < pool_limit * 3;
}

/* a texel's color on the blue ramp: its luminance mapped from black, to
deep blue, to a violet blue, to white at the core (white reads against blue
maps too: the flame is drawn additively) */
static inline void developer_flames_blue(unsigned int r, unsigned int g, unsigned int b, unsigned int *out_r,
	unsigned int *out_g, unsigned int *out_b)
{
	static const unsigned int stops[4][4] =
	{
		/* luminance, red, green, blue */
		{ 0, 0, 0, 0 },
		{ 96, 4, 24, 150 },
		{ 176, 90, 190, 255 },
		{ 255, 255, 255, 255 },
	};
	unsigned int luminance = (77 * (r & 255) + 150 * (g & 255) + 29 * (b & 255)) >> 8;
	int stop = 0;
	unsigned int span, into;

	while (stop < 2 && luminance > stops[stop + 1][0])
		stop++;
	span = stops[stop + 1][0] - stops[stop][0];
	into = luminance - stops[stop][0];
	*out_r = (stops[stop][1] * (span - into) + stops[stop + 1][1] * into) / span;
	*out_g = (stops[stop][2] * (span - into) + stops[stop + 1][2] * into) / span;
	*out_b = (stops[stop][3] * (span - into) + stops[stop + 1][3] * into) / span;
}

/* the tint of a flame whose sprite has no color of its own (a luminance
format: the shader multiplies it), red, green, blue */
#define DEVELOPER_FLAMES_TINT_R 0.5f
#define DEVELOPER_FLAMES_TINT_G 0.8f
#define DEVELOPER_FLAMES_TINT_B 1.0f

/* the map's bitmap formats (bitmap_group.c's _bitmap_format_ numbers) */
enum
{
	DEVELOPER_FLAMES_R5G6B5 = 6,
	DEVELOPER_FLAMES_A1R5G5B5 = 8,
	DEVELOPER_FLAMES_A4R4G4B4 = 9,
	DEVELOPER_FLAMES_X8R8G8B8 = 10,
	DEVELOPER_FLAMES_A8R8G8B8 = 11,
	DEVELOPER_FLAMES_DXT1 = 14,
	DEVELOPER_FLAMES_DXT3 = 15,
	DEVELOPER_FLAMES_DXT5 = 16,
};

/* (a 16-bit 5:6:5 color on the ramp) */
static inline unsigned int developer_flames_blue_565(unsigned int color)
{
	unsigned int r = (color >> 11) & 31, g = (color >> 5) & 63, b = color & 31, out_r, out_g, out_b;

	developer_flames_blue((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), &out_r, &out_g, &out_b);
	return ((out_r >> 3) << 11) | ((out_g >> 2) << 5) | (out_b >> 3);
}

/* (a DXT color block, its two 5:6:5 colors and their indices, on the ramp;
four_color: the block is read as four colors whatever its order (DXT3's
and DXT5's), else DXT1's rule: color 0 above color 1 is four colors, else
three and transparent black, which the order must keep) */
static inline void developer_flames_recolor_block(unsigned char *block, int four_color)
{
	unsigned int c0 = (unsigned int)block[0] | ((unsigned int)block[1] << 8);
	unsigned int c1 = (unsigned int)block[2] | ((unsigned int)block[3] << 8);
	unsigned int n0 = developer_flames_blue_565(c0), n1 = developer_flames_blue_565(c1);
	int was_four = four_color || c0 > c1;
	int index;

	if (!four_color && (was_four ? n0 < n1 : n0 > n1))
	{
		/* (the order flipped: the colors swapped back, and each texel's
		index to match: 0 and 1 always, 2 and 3 in a four-color block) */
		unsigned int swap = n0;

		n0 = n1;
		n1 = swap;
		for (index = 4; index < 8; index++)
		{
			unsigned int bits = block[index], out = 0;
			int texel;

			for (texel = 0; texel < 4; texel++)
			{
				unsigned int code = (bits >> (2 * texel)) & 3;

				if (code < 2 || was_four)
					code ^= 1;
				out |= code << (2 * texel);
			}
			block[index] = (unsigned char)out;
		}
	}
	if (!four_color && was_four && n0 == n1)
	{
		/* (one color: four-color order can't hold, every texel color 0) */
		for (index = 4; index < 8; index++)
			block[index] = 0;
	}
	block[0] = (unsigned char)(n0 & 255);
	block[1] = (unsigned char)(n0 >> 8);
	block[2] = (unsigned char)(n1 & 255);
	block[3] = (unsigned char)(n1 >> 8);
}

/* a bitmap's pixels (every mip level: each texel and block on its own, so
swizzling doesn't matter) put on the blue ramp in place, alpha kept: 1, or
0 for a format with no color of its own (a luminance or palette format,
which DEVELOPER_FLAMES_TINT colors) or a size that isn't whole texels */
static inline int developer_flames_recolor(int format, unsigned char *pixels, long size)
{
	long offset;
	unsigned int r, g, b;

	if (!pixels || size <= 0)
		return 0;
	switch (format)
	{
	case DEVELOPER_FLAMES_X8R8G8B8:
	case DEVELOPER_FLAMES_A8R8G8B8:
		if (size % 4)
			return 0;
		for (offset = 0; offset < size; offset += 4)
		{
			developer_flames_blue(pixels[offset + 2], pixels[offset + 1], pixels[offset], &r, &g, &b);
			pixels[offset] = (unsigned char)b;
			pixels[offset + 1] = (unsigned char)g;
			pixels[offset + 2] = (unsigned char)r;
		}
		return 1;
	case DEVELOPER_FLAMES_R5G6B5:
	case DEVELOPER_FLAMES_A1R5G5B5:
	case DEVELOPER_FLAMES_A4R4G4B4:
		if (size % 2)
			return 0;
		for (offset = 0; offset < size; offset += 2)
		{
			unsigned int texel = (unsigned int)pixels[offset] | ((unsigned int)pixels[offset + 1] << 8);

			if (format == DEVELOPER_FLAMES_R5G6B5)
			{
				texel = developer_flames_blue_565(texel);
			}
			else if (format == DEVELOPER_FLAMES_A1R5G5B5)
			{
				unsigned int cr = (texel >> 10) & 31, cg = (texel >> 5) & 31, cb = texel & 31;

				developer_flames_blue((cr << 3) | (cr >> 2), (cg << 3) | (cg >> 2), (cb << 3) | (cb >> 2), &r, &g, &b);
				texel = (texel & 0x8000) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
			}
			else
			{
				unsigned int cr = (texel >> 8) & 15, cg = (texel >> 4) & 15, cb = texel & 15;

				developer_flames_blue(cr * 17, cg * 17, cb * 17, &r, &g, &b);
				texel = (texel & 0xF000) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
			}
			pixels[offset] = (unsigned char)(texel & 255);
			pixels[offset + 1] = (unsigned char)(texel >> 8);
		}
		return 1;
	case DEVELOPER_FLAMES_DXT1:
		if (size % 8)
			return 0;
		for (offset = 0; offset < size; offset += 8)
			developer_flames_recolor_block(pixels + offset, 0);
		return 1;
	case DEVELOPER_FLAMES_DXT3:
	case DEVELOPER_FLAMES_DXT5:
		if (size % 16)
			return 0;
		for (offset = 0; offset < size; offset += 16)
			developer_flames_recolor_block(pixels + offset + 8, 1);
		return 1;
	default:
		return 0;
	}
}

#endif
