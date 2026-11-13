/* The campaign snapshot's place in an MCC checkpoint's fixed slot
 * (port/linux/game/mcc_checkpoint_footer.inl): the whole version-7 snapshot
 * goes in and comes back, the older footer versions still read, and the
 * footer's map name fits its room. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../game/mcc_campaign.h"
#include "../game/mcc_checkpoint_footer.inl"

static int failures;
#define CHECK(condition) do { if (!(condition)) { failures++; \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static void fill(unsigned char *out, unsigned long bytes, unsigned seed)
{
	unsigned long i;
	for (i = 0; i < bytes; i++)
		out[i] = (unsigned char)(seed + i * 7u);
}

int main(void)
{
	unsigned char snapshot[MCC_CAMPAIGN_SNAPSHOT_BYTES], payload[MCC_CAMPAIGN_SNAPSHOT_BYTES];
	unsigned char footer[96], out[MCC_CAMPAIGN_SNAPSHOT_BYTES];
	char name[80];

	/* the current snapshot: the payload holds its version-5 bytes, the
	footer the two words it grew by, and version 4 gives all of it back */
	fill(snapshot, sizeof(snapshot), 3u);
	memset(payload, 0xEE, sizeof(payload));
	memset(footer, 0, sizeof(footer));
	CHECK(mcc_checkpoint_campaign_pack(snapshot, sizeof(snapshot), payload, footer) == 1);
	CHECK(!memcmp(payload, snapshot, MCC_CAMPAIGN_SNAPSHOT_V5_BYTES));
	CHECK(payload[MCC_CAMPAIGN_SNAPSHOT_V5_BYTES] == 0xEE);
	CHECK(!memcmp(footer + 88, snapshot + MCC_CAMPAIGN_SNAPSHOT_V5_BYTES, 4));
	CHECK(!memcmp(footer + 84, snapshot + MCC_CAMPAIGN_SNAPSHOT_V6_BYTES, 4));
	memset(out, 0, sizeof(out));
	CHECK(mcc_checkpoint_campaign_unpack(4u, payload, MCC_CAMPAIGN_SNAPSHOT_V5_BYTES, footer, out) ==
		MCC_CAMPAIGN_SNAPSHOT_BYTES);
	CHECK(!memcmp(out, snapshot, sizeof(snapshot)));

	/* (the footer's first 84 bytes are the checkpoint's own: untouched) */
	{
		unsigned char zero[84];
		memset(zero, 0, sizeof(zero));
		CHECK(!memcmp(footer, zero, sizeof(zero)));
	}

	/* a snapshot of another size is refused, nothing written */
	memset(payload, 0xEE, sizeof(payload));
	memset(footer, 0, sizeof(footer));
	CHECK(mcc_checkpoint_campaign_pack(snapshot, MCC_CAMPAIGN_SNAPSHOT_V6_BYTES, payload, footer) == 0);
	CHECK(mcc_checkpoint_campaign_pack(snapshot, sizeof(snapshot) + 4, payload, footer) == 0);
	CHECK(payload[0] == 0xEE && !footer[84] && !footer[88]);

	/* version 3 (snapshot version 6): the payload and the ambient word */
	fill(snapshot, sizeof(snapshot), 9u);
	memset(footer, 0, sizeof(footer));
	memcpy(footer + 88, snapshot + MCC_CAMPAIGN_SNAPSHOT_V5_BYTES, 4);
	memset(out, 0, sizeof(out));
	CHECK(mcc_checkpoint_campaign_unpack(3u, snapshot, MCC_CAMPAIGN_SNAPSHOT_V5_BYTES, footer, out) ==
		MCC_CAMPAIGN_SNAPSHOT_V6_BYTES);
	CHECK(!memcmp(out, snapshot, MCC_CAMPAIGN_SNAPSHOT_V6_BYTES));
	CHECK(out[MCC_CAMPAIGN_SNAPSHOT_V6_BYTES] == 0);

	/* version 2: the payload alone, at each size it had */
	CHECK(mcc_checkpoint_campaign_unpack(2u, snapshot, MCC_CAMPAIGN_SNAPSHOT_V1_BYTES, footer, out) ==
		MCC_CAMPAIGN_SNAPSHOT_V1_BYTES);
	CHECK(mcc_checkpoint_campaign_unpack(2u, snapshot, MCC_CAMPAIGN_SNAPSHOT_V4_BYTES, footer, out) ==
		MCC_CAMPAIGN_SNAPSHOT_V4_BYTES);
	CHECK(mcc_checkpoint_campaign_unpack(2u, snapshot, MCC_CAMPAIGN_SNAPSHOT_V5_BYTES, footer, out) ==
		MCC_CAMPAIGN_SNAPSHOT_V5_BYTES);
	CHECK(!memcmp(out, snapshot, MCC_CAMPAIGN_SNAPSHOT_V5_BYTES));

	/* sizes and versions that don't go together */
	CHECK(mcc_checkpoint_campaign_unpack(2u, snapshot, MCC_CAMPAIGN_SNAPSHOT_V6_BYTES, footer, out) == 0);
	CHECK(mcc_checkpoint_campaign_unpack(3u, snapshot, MCC_CAMPAIGN_SNAPSHOT_V4_BYTES, footer, out) == 0);
	CHECK(mcc_checkpoint_campaign_unpack(4u, snapshot, MCC_CAMPAIGN_SNAPSHOT_V6_BYTES, footer, out) == 0);
	CHECK(mcc_checkpoint_campaign_unpack(1u, snapshot, MCC_CAMPAIGN_SNAPSHOT_V5_BYTES, footer, out) == 0);
	CHECK(mcc_checkpoint_campaign_unpack(5u, snapshot, MCC_CAMPAIGN_SNAPSHOT_V5_BYTES, footer, out) == 0);

	/* the map name: 60 bytes from version 4, beside the skulls word */
	CHECK(mcc_checkpoint_footer_name_bytes(4u) == 60u && mcc_checkpoint_footer_name_bytes(3u) == 64u &&
		mcc_checkpoint_footer_name_bytes(1u) == 64u);
	memset(footer, 0xAA, sizeof(footer));
	mcc_checkpoint_footer_name_write(footer, "mcc_maps\\[2886943774]a30");
	CHECK(!strcmp((char const *)footer + 24, "mcc_maps\\[2886943774]a30"));
	CHECK(footer[83] == 0 && footer[84] == 0xAA && footer[23] == 0xAA);
	CHECK(mcc_checkpoint_footer_name_matches(4u, footer, "mcc_maps\\[2886943774]a30"));
	CHECK(!mcc_checkpoint_footer_name_matches(4u, footer, "mcc_maps\\[2886943774]a50"));
	CHECK(!mcc_checkpoint_footer_name_matches(4u, footer, "mcc_maps\\[2886943774]a30x"));

	/* a long name keeps its first 59 characters, and matches by them */
	memset(name, 'n', sizeof(name));
	name[70] = 0;
	memset(footer, 0xAA, sizeof(footer));
	mcc_checkpoint_footer_name_write(footer, name);
	CHECK(strlen((char const *)footer + 24) == 59 && footer[84] == 0xAA);
	CHECK(mcc_checkpoint_footer_name_matches(4u, footer, name));
	name[30] = 'm';
	CHECK(!mcc_checkpoint_footer_name_matches(4u, footer, name));

	/* up to version 3 the name is whole, in 64 bytes */
	memset(footer, 0, sizeof(footer));
	memcpy(footer + 24, "levels\\a30\\a30", 15);
	CHECK(mcc_checkpoint_footer_name_matches(3u, footer, "levels\\a30\\a30"));
	CHECK(!mcc_checkpoint_footer_name_matches(3u, footer, "levels\\a30\\a3"));
	memset(footer + 24, 'x', 64);
	CHECK(!mcc_checkpoint_footer_name_matches(3u, footer, "x"));
	CHECK(!mcc_checkpoint_footer_name_matches(4u, footer, "x"));

	if (failures)
		fprintf(stderr, "%d failed\n", failures);
	else
		puts("mcc_checkpoint_footer_test: ok");
	return failures ? 1 : 0;
}
