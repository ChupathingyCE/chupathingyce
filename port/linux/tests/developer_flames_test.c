/*
DEVELOPER_FLAMES_TEST.C

Tests of a verified developer's blue flame's rules (port/linux/src/
developer_flames_rules.h): who has one, when it is drawn, its budget, and
the blue copy of a flame sprite in every format it takes. Built and run by
tools/test_developer_flames.py; exits nonzero on a failure.
*/

#include "../src/developer_flames_rules.h"
#include "../src/identity.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

static struct developer_flames_card developer(void)
{
	struct developer_flames_card card;

	memset(&card, 0, sizeof(card));
	card.option_on = card.names_on = card.on_delta = card.checked = card.name_shown = card.bound = 1;
	card.capabilities = 0x1FUL | IDENTITY_MARK_DEVELOPER;
	return card;
}

static void test_who(void)
{
	struct developer_flames_card card = developer();

	CHECK(DEVELOPER_FLAMES_MARK == (unsigned long)IDENTITY_MARK_DEVELOPER);
	CHECK((DEVELOPER_FLAMES_MARK & IDENTITY_CAPABILITY_BITS) == 0);
	CHECK(developer_flames_card_shows(&card));
	/* (without the mark: a verified player, no flame) */
	card.capabilities = 0x1F;
	CHECK(!developer_flames_card_shows(&card));
	/* (another mark alone: none) */
	card.capabilities = 0x1FUL | 0x02000000UL;
	CHECK(!developer_flames_card_shows(&card));
	/* (each condition on its own takes it away) */
	card = developer();
	card.option_on = 0;
	CHECK(!developer_flames_card_shows(&card));
	card = developer();
	card.names_on = 0;
	CHECK(!developer_flames_card_shows(&card));
	card = developer();
	card.on_delta = 0;
	CHECK(!developer_flames_card_shows(&card));
	/* (no card this copy checked: a peer's word is never taken) */
	card = developer();
	card.checked = 0;
	CHECK(!developer_flames_card_shows(&card));
	/* (an unbound card: a host could replay it from another game) */
	card = developer();
	card.bound = 0;
	CHECK(!developer_flames_card_shows(&card));
	/* (another network's developer is its own, not ours) */
	card = developer();
	card.other_network = 1;
	CHECK(!developer_flames_card_shows(&card));
	/* (not shown by the verified name, or masked as a look-alike) */
	card = developer();
	card.name_shown = 0;
	CHECK(!developer_flames_card_shows(&card));
	card = developer();
	card.looks_like = 1;
	CHECK(!developer_flames_card_shows(&card));
}

static void test_when(void)
{
	struct developer_flames_wearer wearer;

	memset(&wearer, 0, sizeof(wearer));
	wearer.has_head = wearer.alive = wearer.drawn = 1;
	wearer.distance = 5.0f;
	CHECK(developer_flames_wearer_fade(&wearer) == 1.0f);
	wearer.distance = (DEVELOPER_FLAMES_NEAR + DEVELOPER_FLAMES_FAR) / 2;
	CHECK(developer_flames_wearer_fade(&wearer) > 0.4f && developer_flames_wearer_fade(&wearer) < 0.6f);
	wearer.distance = DEVELOPER_FLAMES_FAR;
	CHECK(developer_flames_wearer_fade(&wearer) == 0.0f);
	wearer.distance = 5.0f;
	wearer.camouflaged = 1;
	CHECK(developer_flames_wearer_fade(&wearer) == 0.0f);
	wearer.camouflaged = 0;
	wearer.own_first_person = 1;
	CHECK(developer_flames_wearer_fade(&wearer) == 0.0f);
	wearer.own_first_person = 0;
	wearer.alive = 0;
	CHECK(developer_flames_wearer_fade(&wearer) == 0.0f);
	wearer.alive = 1;
	wearer.drawn = 0;
	CHECK(developer_flames_wearer_fade(&wearer) == 0.0f);
	wearer.drawn = 1;
	wearer.has_head = 0;
	CHECK(developer_flames_wearer_fade(&wearer) == 0.0f);
}

static void test_budget(void)
{
	CHECK(developer_flames_may_spawn(0, 0, 0, 1024));
	CHECK(!developer_flames_may_spawn(DEVELOPER_FLAMES_PER_HELMET, 12, 0, 1024));
	CHECK(!developer_flames_may_spawn(0, DEVELOPER_FLAMES_MOST, 0, 1024));
	/* (the map's particles past three quarters of the limit: none) */
	CHECK(developer_flames_may_spawn(0, 0, 767, 1024));
	CHECK(!developer_flames_may_spawn(0, 0, 768, 1024));
	CHECK(developer_flames_may_spawn(0, 0, 100000, 0));
	CHECK(DEVELOPER_FLAMES_MOST >= DEVELOPER_FLAMES_PER_HELMET * DEVELOPER_FLAMES_WEARERS);
}

static int bluish(unsigned int r, unsigned int g, unsigned int b)
{
	return b >= g && g >= r;
}

static void test_ramp(void)
{
	unsigned int r, g, b, last = 0, level;

	developer_flames_blue(0, 0, 0, &r, &g, &b);
	CHECK(r == 0 && g == 0 && b == 0);
	developer_flames_blue(255, 255, 255, &r, &g, &b);
	CHECK(r == 255 && g == 255 && b == 255);
	/* (orange fire comes out blue, brighter for brighter, never orange) */
	for (level = 0; level < 256; level += 5)
	{
		developer_flames_blue(level, level * 6 / 10, level / 5, &r, &g, &b);
		CHECK(bluish(r, g, b));
		CHECK(r + g + b >= last);
		last = r + g + b;
	}
}

static void test_recolor_32(void)
{
	/* B, G, R, A: an orange texel and a transparent black one */
	unsigned char pixels[8] = { 0x20, 0x90, 0xF0, 0x80, 0, 0, 0, 0 };

	CHECK(developer_flames_recolor(DEVELOPER_FLAMES_A8R8G8B8, pixels, 8));
	CHECK(bluish(pixels[2], pixels[1], pixels[0]) && pixels[0] > 200);
	CHECK(pixels[3] == 0x80);
	CHECK(!pixels[4] && !pixels[5] && !pixels[6] && !pixels[7]);
	CHECK(!developer_flames_recolor(DEVELOPER_FLAMES_A8R8G8B8, pixels, 6));
}

static void test_recolor_16(void)
{
	/* (orange in each 16-bit format, its alpha bits kept) */
	unsigned char pixels[2];
	unsigned int texel;

	pixels[0] = (unsigned char)((0x1F << 11 | 0x24 << 5 | 0x04) & 255);
	pixels[1] = (unsigned char)((0x1F << 11 | 0x24 << 5 | 0x04) >> 8);
	CHECK(developer_flames_recolor(DEVELOPER_FLAMES_R5G6B5, pixels, 2));
	texel = (unsigned int)pixels[0] | ((unsigned int)pixels[1] << 8);
	CHECK((texel & 31) >= ((texel >> 11) & 31));
	texel = 0x8000 | (31 << 10) | (18 << 5) | 2;
	pixels[0] = (unsigned char)(texel & 255);
	pixels[1] = (unsigned char)(texel >> 8);
	CHECK(developer_flames_recolor(DEVELOPER_FLAMES_A1R5G5B5, pixels, 2));
	texel = (unsigned int)pixels[0] | ((unsigned int)pixels[1] << 8);
	CHECK((texel & 0x8000) && (texel & 31) >= ((texel >> 10) & 31));
	texel = 0xA000 | (15 << 8) | (9 << 4) | 1;
	pixels[0] = (unsigned char)(texel & 255);
	pixels[1] = (unsigned char)(texel >> 8);
	CHECK(developer_flames_recolor(DEVELOPER_FLAMES_A4R4G4B4, pixels, 2));
	texel = (unsigned int)pixels[0] | ((unsigned int)pixels[1] << 8);
	CHECK((texel & 0xF000) == 0xA000 && (texel & 15) >= ((texel >> 8) & 15));
}

/* a DXT1 texel's color (0 to 3 codes, as the format reads them), its
blue channel's top bits, or -1 for transparent */
static int dxt1_texel_blue(const unsigned char *block, int texel)
{
	unsigned int c0 = block[0] | (block[1] << 8), c1 = block[2] | (block[3] << 8);
	unsigned int code = (block[4 + texel / 4] >> (2 * (texel % 4))) & 3;
	unsigned int b0 = c0 & 31, b1 = c1 & 31;

	if (c0 > c1)
		return (int)(code == 0 ? b0 : code == 1 ? b1 : code == 2 ? (2 * b0 + b1) / 3 : (b0 + 2 * b1) / 3);
	return code == 3 ? -1 : (int)(code == 0 ? b0 : code == 1 ? b1 : (b0 + b1) / 2);
}

static void test_recolor_dxt(void)
{
	/* orange (bright) and dark red: four colors, its order flips on the
	ramp? either way each texel keeps its brightness rank */
	unsigned int bright = 0x1F << 11 | 0x30 << 5 | 0x04, dark = 0x08 << 11 | 0x02 << 5;
	unsigned char block[8], three[8], dxt5[16];
	int texel, before[16], after[16];

	block[0] = (unsigned char)(bright & 255);
	block[1] = (unsigned char)(bright >> 8);
	block[2] = (unsigned char)(dark & 255);
	block[3] = (unsigned char)(dark >> 8);
	block[4] = 0xE4;
	block[5] = 0x1B;
	block[6] = 0x00;
	block[7] = 0xFF;
	for (texel = 0; texel < 16; texel++)
		before[texel] = (block[4 + texel / 4] >> (2 * (texel % 4))) & 3;
	CHECK(developer_flames_recolor(DEVELOPER_FLAMES_DXT1, block, 8));
	CHECK((block[0] | (block[1] << 8)) > (block[2] | (block[3] << 8)));
	for (texel = 0; texel < 16; texel++)
		after[texel] = dxt1_texel_blue(block, texel);
	/* (the bright texels, code 0 before, are the bluest; none transparent) */
	for (texel = 0; texel < 16; texel++)
	{
		CHECK(after[texel] >= 0);
		if (before[texel] == 0)
			CHECK(after[texel] >= after[15]);
	}
	/* three colors and transparent black, kept so */
	three[0] = (unsigned char)(dark & 255);
	three[1] = (unsigned char)(dark >> 8);
	three[2] = (unsigned char)(bright & 255);
	three[3] = (unsigned char)(bright >> 8);
	three[4] = 0xFF;
	three[5] = 0x00;
	three[6] = 0x55;
	three[7] = 0xAA;
	CHECK(developer_flames_recolor(DEVELOPER_FLAMES_DXT1, three, 8));
	CHECK((three[0] | (three[1] << 8)) <= (three[2] | (three[3] << 8)));
	CHECK(dxt1_texel_blue(three, 0) == -1 && dxt1_texel_blue(three, 3) == -1);
	CHECK(dxt1_texel_blue(three, 4) >= 0);
	/* DXT5: the alpha half untouched */
	memset(dxt5, 0x5A, 8);
	memcpy(dxt5 + 8, block, 8);
	dxt5[8] = (unsigned char)(bright & 255);
	dxt5[9] = (unsigned char)(bright >> 8);
	CHECK(developer_flames_recolor(DEVELOPER_FLAMES_DXT5, dxt5, 16));
	for (texel = 0; texel < 8; texel++)
		CHECK(dxt5[texel] == 0x5A);
	CHECK(((dxt5[8] | (dxt5[9] << 8)) & 31) >= (((dxt5[8] | (dxt5[9] << 8)) >> 11) & 31));
	CHECK(!developer_flames_recolor(DEVELOPER_FLAMES_DXT5, dxt5, 12));
}

static void test_tinted_formats(void)
{
	unsigned char pixels[4] = { 1, 2, 3, 4 };

	/* (A8, Y8, AY8, A8Y8, P8: no color of their own, tinted; nothing written) */
	CHECK(!developer_flames_recolor(0, pixels, 4) && !developer_flames_recolor(1, pixels, 4));
	CHECK(!developer_flames_recolor(2, pixels, 4) && !developer_flames_recolor(3, pixels, 4));
	CHECK(!developer_flames_recolor(17, pixels, 4));
	CHECK(pixels[0] == 1 && pixels[3] == 4);
	CHECK(!developer_flames_recolor(DEVELOPER_FLAMES_A8R8G8B8, NULL, 4));
	CHECK(bluish((unsigned int)(DEVELOPER_FLAMES_TINT_R * 255), (unsigned int)(DEVELOPER_FLAMES_TINT_G * 255),
		(unsigned int)(DEVELOPER_FLAMES_TINT_B * 255)));
}

int main(void)
{
	test_who();
	test_when();
	test_budget();
	test_ramp();
	test_recolor_32();
	test_recolor_16();
	test_recolor_dxt();
	test_tinted_formats();
	if (failures)
	{
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("developer flames: ok\n");
	return 0;
}
