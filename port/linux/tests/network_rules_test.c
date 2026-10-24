/*
NETWORK_RULES_TEST.C

Tests of the rules of network versions 30 to 36 (port/linux/game/
network_rules.c): the bytes of the aim assist policy message and of a
player's input entry, the window of a datagram's stamp, what a client does
with the host's word on its player on an elevator, who rides one, the
policy's reach, and 33 to 36's settings record sizes, further gametype
options, shield effects message and static target reach, and the layouts a
signed legacy table's facts extend (sizes, dropped kinds, the shield effects'
largest value, within the limits). The expected values are those of the
network's documented behavior. tools/test_network_rules.py builds and runs
them.
*/

#include "../game/network_rules.h"
#include "../include/delta.h"

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

static void test_aim_assist_message(void)
{
	uint8_t message[NETWORK_RULES_AIM_ASSIST_SIZE];
	static const uint8_t blocked[12] = { 0xC8, 0x00, 90, 1, 0x04, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00 };
	static const uint8_t allowed[12] = { 0xC8, 0x00, 90, 1, 0x04, 0x03, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00 };
	uint8_t bad[12];
	int block = -1;

	/* twelve bytes: the header word (size 12 in bits 4 to 15, the data type
	2 in bits 2 and 3), the kind 90, one entry, the sender's tick, then 01 00
	00 00 for blocked and 00 00 00 00 for allowed */
	CHECK(NETWORK_RULES_AIM_ASSIST_SIZE == 12 && NETWORK_RULES_AIM_ASSIST_KIND == 90);
	network_rules_aim_assist_write(message, 1, 0x01020304);
	CHECK(!memcmp(message, blocked, sizeof(message)));
	network_rules_aim_assist_write(message, 0, 0x01020304);
	CHECK(!memcmp(message, allowed, sizeof(message)));
	/* (a value other than 0 is 1) */
	network_rules_aim_assist_write(message, 7, 0x01020304);
	CHECK(!memcmp(message, blocked, sizeof(message)));

	/* a client takes what it wrote, from the stream */
	CHECK(network_rules_aim_assist_read(blocked, 12, 0, &block) && block == 1);
	CHECK(network_rules_aim_assist_read(allowed, 12, 0, &block) && block == 0);

	/* and drops, with the policy unchanged: a value of 2 */
	block = -1;
	memcpy(bad, allowed, 12);
	bad[8] = 2;
	CHECK(!network_rules_aim_assist_read(bad, 12, 0, &block) && block == -1);
	bad[8] = 0;
	bad[9] = 1;
	CHECK(!network_rules_aim_assist_read(bad, 12, 0, &block));
	/* ... the reserved bits not 0 */
	memcpy(bad, allowed, 12);
	bad[10] = 1;
	CHECK(!network_rules_aim_assist_read(bad, 12, 0, &block));
	memcpy(bad, allowed, 12);
	bad[11] = 0x80;
	CHECK(!network_rules_aim_assist_read(bad, 12, 0, &block));
	/* ... a count of 0 or 2 */
	memcpy(bad, allowed, 12);
	bad[3] = 0;
	CHECK(!network_rules_aim_assist_read(bad, 12, 0, &block));
	bad[3] = 2;
	CHECK(!network_rules_aim_assist_read(bad, 12, 0, &block));
	/* ... a size of 11 or 13, by the bytes and by the header */
	CHECK(!network_rules_aim_assist_read(allowed, 11, 0, &block));
	{
		uint8_t longer[13];

		memcpy(longer, allowed, 12);
		longer[12] = 0;
		CHECK(!network_rules_aim_assist_read(longer, 13, 0, &block));
		longer[0] = 0xD8;
		CHECK(!network_rules_aim_assist_read(longer, 13, 0, &block));
	}
	memcpy(bad, allowed, 12);
	bad[0] = 0xB8;
	CHECK(!network_rules_aim_assist_read(bad, 12, 0, &block));
	/* ... a datagram (the unreliable kind) */
	CHECK(!network_rules_aim_assist_read(allowed, 12, 1, &block));
	/* ... another message type (packet 3, error 0) or kind */
	memcpy(bad, allowed, 12);
	bad[0] = 0xCC;
	CHECK(!network_rules_aim_assist_read(bad, 12, 0, &block));
	bad[0] = 0xC0;
	CHECK(!network_rules_aim_assist_read(bad, 12, 0, &block));
	memcpy(bad, allowed, 12);
	bad[2] = 89;
	CHECK(!network_rules_aim_assist_read(bad, 12, 0, &block));
	bad[2] = 91;
	CHECK(!network_rules_aim_assist_read(bad, 12, 0, &block));
	CHECK(block == -1);
}

static void test_input_entry(void)
{
	uint8_t head[NETWORK_RULES_INPUT_HEAD_SIZE];
	static const uint8_t declined[12] = { 3, 7, 1, 0, 0x78, 0x56, 0x34, 0x12, 0xEF, 0xCD, 0xAB, 0x00 };
	int player, bsp, no_friendly_fire;
	int32_t tick, host_time;

	/* the entry is 52 bytes: these 12, the 32-byte action, four 16-bit
	buttons; byte 2 was padding */
	CHECK(NETWORK_RULES_INPUT_SIZE == 12 + 0x20 + 8 && NETWORK_RULES_INPUT_HEAD_SIZE == 12);
	memset(head, 0xAA, sizeof(head));
	network_rules_input_head_write(head, 3, 7, 1, 0x12345678, 0x00ABCDEF);
	CHECK(!memcmp(head, declined, sizeof(head)));
	/* a player who plays with friendly fire (or any game but co-op) is 0, and
	byte 3 is padding of 0 */
	network_rules_input_head_write(head, 3, 7, 0, 0x12345678, 0x00ABCDEF);
	CHECK(head[2] == 0 && head[3] == 0 && !memcmp(head + 4, declined + 4, 8));
	network_rules_input_head_read(declined, &player, &bsp, &no_friendly_fire, &tick, &host_time);
	CHECK(player == 3 && bsp == 7 && no_friendly_fire == 1 && tick == 0x12345678 && host_time == 0x00ABCDEF);
	network_rules_input_head_read(head, &player, &bsp, &no_friendly_fire, &tick, &host_time);
	CHECK(no_friendly_fire == 0);
	/* any value but 0 is declined, 2 and 255 too (older senders' padding was 0) */
	head[2] = 2;
	network_rules_input_head_read(head, &player, &bsp, &no_friendly_fire, &tick, &host_time);
	CHECK(no_friendly_fire == 1);
	head[2] = 255;
	network_rules_input_head_read(head, &player, &bsp, &no_friendly_fire, &tick, &host_time);
	CHECK(no_friendly_fire == 1);
	/* a negative tick (the host's none) survives */
	network_rules_input_head_write(head, 0, 0, 0, 5, -1);
	network_rules_input_head_read(head, &player, &bsp, &no_friendly_fire, &tick, &host_time);
	CHECK(tick == 5 && host_time == -1);
}

static void test_stamp_window(void)
{
	int32_t latest = -1;

	CHECK(NETWORK_RULES_TIME_WINDOW_TICKS == 15 * 30);
	/* none yet: taken, and recorded */
	CHECK(network_rules_stamp_window(&latest, 1000, 0, NETWORK_RULES_TIME_WINDOW_TICKS) == 1 && latest == 1000);
	/* 451 away either way is dropped; 450 is taken */
	CHECK(network_rules_stamp_window(&latest, 1451, 0, NETWORK_RULES_TIME_WINDOW_TICKS) == 0 && latest == 1000);
	CHECK(network_rules_stamp_window(&latest, 549, 0, NETWORK_RULES_TIME_WINDOW_TICKS) == 0 && latest == 1000);
	CHECK(network_rules_stamp_window(&latest, 550, 0, NETWORK_RULES_TIME_WINDOW_TICKS) == 1 && latest == 1000);
	/* (the latest moves forward only) */
	CHECK(network_rules_stamp_window(&latest, 1450, 0, NETWORK_RULES_TIME_WINDOW_TICKS) == 1 && latest == 1450);
	CHECK(network_rules_stamp_window(&latest, 1100, 0, NETWORK_RULES_TIME_WINDOW_TICKS) == 1 && latest == 1450);
	/* the same stamps over the stream are taken, and a far one moves the latest
	and says the kinds' memory is to be forgotten */
	latest = 1000;
	CHECK(network_rules_stamp_window(&latest, 1451, 1, NETWORK_RULES_TIME_WINDOW_TICKS) == 2 && latest == 1451);
	latest = 1000;
	CHECK(network_rules_stamp_window(&latest, 549, 1, NETWORK_RULES_TIME_WINDOW_TICKS) == 2 && latest == 549);
	latest = 1000;
	CHECK(network_rules_stamp_window(&latest, 1450, 1, NETWORK_RULES_TIME_WINDOW_TICKS) == 1 && latest == 1450);
	latest = 1000;
	CHECK(network_rules_stamp_window(&latest, 550, 1, NETWORK_RULES_TIME_WINDOW_TICKS) == 1 && latest == 1000);
	/* no stamp, however far, overflows the distance */
	latest = 0x7FFFFFF0;
	CHECK(network_rules_stamp_window(&latest, -2, 0, NETWORK_RULES_TIME_WINDOW_TICKS) == 0 && latest == 0x7FFFFFF0);
	latest = -2147483647 - 1;
	CHECK(network_rules_stamp_window(&latest, 0x7FFFFFFF, 0, NETWORK_RULES_TIME_WINDOW_TICKS) == 0);
	CHECK(network_rules_stamp_window(&latest, 0x7FFFFFFF, 1, NETWORK_RULES_TIME_WINDOW_TICKS) == 2 && latest == 0x7FFFFFFF);
}

static float corrected(int32_t recorded, int32_t current, float x, float y, float z, int *applied)
{
	float error[3];

	error[0] = x;
	error[1] = y;
	error[2] = z;
	*applied = network_rules_elevator_correction(recorded, current, error);
	return error[2];
}

static void test_elevator_correction(void)
{
	int applied;
	float z;

	/* the same elevator: the height is discarded within 4.0 (and the error
	across within 3.0), and what is left is within 3.0: nothing is corrected */
	z = corrected(7, 7, 0.0f, 0.0f, 3.9f, &applied);
	CHECK(!applied && z == 0.0f);
	z = corrected(7, 7, 0.0f, 0.0f, 4.0f, &applied);
	CHECK(!applied && z == 0.0f);
	z = corrected(7, 7, 0.0f, 0.0f, -4.0f, &applied);
	CHECK(!applied && z == 0.0f);
	z = corrected(7, 7, 0.0f, 0.0f, 4.1f, &applied);
	CHECK(applied && z == 4.1f);
	z = corrected(7, 7, 0.0f, 0.0f, -4.1f, &applied);
	CHECK(applied && z == -4.1f);
	/* across beyond 3.0 keeps the height: corrected by the whole error */
	z = corrected(7, 7, 3.1f, 0.0f, 2.0f, &applied);
	CHECK(applied && z == 2.0f);
	z = corrected(7, 7, 3.1f, 0.0f, 0.0f, &applied);
	CHECK(applied);
	z = corrected(7, 7, 3.0f, 0.0f, 3.0f, &applied);
	CHECK(!applied && z == 0.0f);
	/* across and height together: the height dropped when the across is within
	3.0 (so nothing is left to correct), kept beyond it */
	z = corrected(7, 7, 2.0f, 2.0f, 3.5f, &applied);
	CHECK(!applied && z == 0.0f);
	z = corrected(7, 7, 2.5f, 2.5f, 3.5f, &applied);
	CHECK(applied && z == 3.5f);
	/* a different elevator, none recorded (a jump), or none now: the error is
	whole, within 3.0 ignored, beyond corrected */
	z = corrected(7, 8, 0.0f, 0.0f, 2.0f, &applied);
	CHECK(!applied && z == 2.0f);
	z = corrected(7, 8, 0.0f, 0.0f, 3.5f, &applied);
	CHECK(applied && z == 3.5f);
	z = corrected(-1, -1, 0.0f, 0.0f, 3.5f, &applied);
	CHECK(applied && z == 3.5f);
	z = corrected(-1, 7, 0.0f, 0.0f, 2.0f, &applied);
	CHECK(!applied && z == 2.0f);
	z = corrected(7, -1, 0.0f, 0.0f, 3.5f, &applied);
	CHECK(applied && z == 3.5f);
	/* an error not a number corrects nothing */
	z = corrected(-1, -1, 0.0f / 0.0f, 0.0f, 0.0f, &applied);
	CHECK(!applied);
	(void)z;
}

static void test_riding(void)
{
	/* contact: not airborne, a support surface, and the counter just set (60) */
	CHECK(network_rules_floor_contact(0, 1, 60));
	CHECK(!network_rules_floor_contact(1, 1, 60));
	CHECK(!network_rules_floor_contact(0, 0, 60));
	CHECK(!network_rules_floor_contact(0, 1, 59));
	CHECK(!network_rules_floor_contact(0, 1, 0));
	/* riding: a machine that moves, or that the unit stands on at rest; an
	elevator remembered alone (a jump off a stopped one) is not */
	CHECK(network_rules_riding_elevator(1, 1, 0));
	CHECK(network_rules_riding_elevator(1, 0, 1));
	CHECK(network_rules_riding_elevator(1, 1, 1));
	CHECK(!network_rules_riding_elevator(1, 0, 0));
	CHECK(!network_rules_riding_elevator(0, 1, 1));
	CHECK(!network_rules_riding_elevator(0, 0, 0));
}

static void test_aim_assist_policy(void)
{
	/* in effect only in a game, off the main menu's scene: the host's setting
	on a host; on a client what the host last said (blocked until it speaks);
	never in single player */
	CHECK(network_rules_aim_assist_blocks(1, 0, 1, 0, 1, 0));
	CHECK(!network_rules_aim_assist_blocks(1, 0, 1, 0, 0, 1));
	CHECK(network_rules_aim_assist_blocks(1, 0, 0, 1, 0, 1));
	CHECK(!network_rules_aim_assist_blocks(1, 0, 0, 1, 1, 0));
	CHECK(!network_rules_aim_assist_blocks(1, 1, 1, 0, 1, 1));
	CHECK(!network_rules_aim_assist_blocks(1, 1, 0, 1, 1, 1));
	CHECK(!network_rules_aim_assist_blocks(0, 0, 1, 0, 1, 1));
	CHECK(!network_rules_aim_assist_blocks(0, 0, 0, 1, 1, 1));
	CHECK(!network_rules_aim_assist_blocks(1, 0, 0, 0, 1, 1));
}

static void test_aim_device(void)
{
	/* no stick aiming ever: the mouse (a controller merely connected changes
	nothing) */
	CHECK(network_rules_mouse_is_aim_device(0, 0, 0));
	CHECK(network_rules_mouse_is_aim_device(500, 0, 0));
	/* the mouse after the stick, or at the same time, is; before it is not */
	CHECK(network_rules_mouse_is_aim_device(600, 500, 0));
	CHECK(network_rules_mouse_is_aim_device(500, 500, 0));
	CHECK(!network_rules_mouse_is_aim_device(400, 500, 0));
	CHECK(!network_rules_mouse_is_aim_device(0, 500, 0));
	/* touch at or after both is not the mouse; before either is */
	CHECK(!network_rules_mouse_is_aim_device(500, 400, 700));
	CHECK(!network_rules_mouse_is_aim_device(500, 400, 500));
	CHECK(network_rules_mouse_is_aim_device(500, 400, 450));
	CHECK(!network_rules_mouse_is_aim_device(0, 0, 100));
	CHECK(network_rules_mouse_is_aim_device(600, 500, 550));
}

static void test_settings_size(void)
{
	static uint8_t record[NETWORK_RULES_SETTINGS_EXTENDED_SIZE];
	uint8_t extended[NETWORK_RULES_EXTENDED_OPTIONS_SIZE];
	int index;

	/* 13,120 bytes (0x3340) from 11 to 34, 13,136 (0x3350) from 35 */
	CHECK(NETWORK_RULES_SETTINGS_SIZE == 0x3340 && NETWORK_RULES_SETTINGS_EXTENDED_SIZE == 0x3350);
	CHECK(network_rules_settings_size(11) == 0x3340);
	CHECK(network_rules_settings_size(32) == 0x3340);
	CHECK(network_rules_settings_size(34) == 0x3340);
	CHECK(network_rules_settings_size(35) == 0x3350);
	CHECK(network_rules_settings_size(36) == 0x3350);
	CHECK(network_rules_settings_size(NETWORK_RULES_VERSION_UNKNOWN) == 0);
	/* a known version's own size alone; one not known, either */
	CHECK(network_rules_settings_size_taken(34, 0x3340) && !network_rules_settings_size_taken(34, 0x3350));
	CHECK(network_rules_settings_size_taken(36, 0x3350) && !network_rules_settings_size_taken(36, 0x3340));
	CHECK(network_rules_settings_size_taken(NETWORK_RULES_VERSION_UNKNOWN, 0x3340));
	CHECK(network_rules_settings_size_taken(NETWORK_RULES_VERSION_UNKNOWN, 0x3350));
	CHECK(!network_rules_settings_size_taken(NETWORK_RULES_VERSION_UNKNOWN, 0x3348));
	/* the pieces' total size words of the test vectors: 50 33 and 40 33 */
	CHECK((network_rules_settings_size(35) & 0xFF) == 0x50 && (network_rules_settings_size(35) >> 8) == 0x33);
	CHECK((network_rules_settings_size(34) & 0xFF) == 0x40 && (network_rules_settings_size(34) >> 8) == 0x33);

	/* a 35 record: options at 0x3320 (0x2C bytes), local data at 0x334C;
	folded, the first 0x1C options stay, the local data is at 0x333C */
	for (index = 0; index < (int)sizeof(record); index++)
		record[index] = (uint8_t)(index * 7);
	network_rules_settings_fold_options(record, extended);
	for (index = 0; index < 0x333C; index++)
		CHECK(record[index] == (uint8_t)(index * 7));
	for (index = 0; index < 16; index++)
		CHECK(extended[index] == (uint8_t)((0x333C + index) * 7));
	for (index = 0; index < 4; index++)
		CHECK(record[0x333C + index] == (uint8_t)((0x334C + index) * 7));
	for (index = 0x3340; index < 0x3350; index++)
		CHECK(record[index] == 0);
}

static void test_extended_options(void)
{
	static const uint8_t defaults[16] = { 0x00, 0x00, 0x01, 0x96, 0xC8, 0x05, 0x0A, 0x01, 0x00, 0x01 };
	uint8_t options[16];
	int hardcore;

	network_rules_extended_options_default(options);
	CHECK(memcmp(options, defaults, 16) == 0);
	/* the built-ins' bytes pass unchanged */
	options[0] = NETWORK_RULES_MODE_INFECTION;
	network_rules_extended_options_sanitize(options);
	CHECK(options[0] == 2 && memcmp(options + 1, defaults + 1, 15) == 0);

	/* the sanitize vectors */
	memcpy(options, defaults, 16);
	options[0] = 4; options[1] = 7; options[2] = 0; options[3] = 99; options[4] = 251; options[5] = 0;
	options[6] = 0; options[7] = 9; options[8] = 3; options[9] = 0; options[10] = 1; options[15] = 0xFF;
	network_rules_extended_options_sanitize(options);
	CHECK(options[0] == 0 && options[1] == 1 && options[2] == 1 && options[3] == 0x96 && options[4] == 0xC8 &&
		options[5] == 5 && options[6] == 0 && options[7] == 1 && options[8] == 0 && options[9] == 0 &&
		options[10] == 0 && options[15] == 0);
	memcpy(options, defaults, 16);
	options[2] = 17; options[3] = 201; options[5] = 31; options[6] = 31;
	network_rules_extended_options_sanitize(options);
	CHECK(options[2] == 1 && options[3] == 0x96 && options[5] == 5 && options[6] == 0x0A);
	/* the range ends are kept */
	options[2] = 16; options[3] = 200; options[4] = 100; options[5] = 30; options[6] = 30;
	network_rules_extended_options_sanitize(options);
	CHECK(options[2] == 16 && options[3] == 200 && options[4] == 100 && options[5] == 30 && options[6] == 30);

	/* which game type a variant plays */
	memcpy(options, defaults, 16);
	CHECK(network_rules_extended_options_mode(options, 1, 1, &hardcore) == NETWORK_RULES_MODE_NONE && !hardcore);
	options[0] = NETWORK_RULES_MODE_SWAT;
	CHECK(network_rules_extended_options_mode(options, 1, 0, &hardcore) == NETWORK_RULES_MODE_SWAT);
	CHECK(network_rules_extended_options_mode(options, 1, 1, &hardcore) == NETWORK_RULES_MODE_SWAT);
	CHECK(network_rules_extended_options_mode(options, 0, 1, &hardcore) == NETWORK_RULES_MODE_NONE);
	options[0] = NETWORK_RULES_MODE_INFECTION;
	CHECK(network_rules_extended_options_mode(options, 1, 1, &hardcore) == NETWORK_RULES_MODE_INFECTION);
	CHECK(network_rules_extended_options_mode(options, 1, 0, &hardcore) == NETWORK_RULES_MODE_NONE);
	options[0] = NETWORK_RULES_MODE_GUN_GAME;
	CHECK(network_rules_extended_options_mode(options, 1, 0, &hardcore) == NETWORK_RULES_MODE_GUN_GAME);
	CHECK(network_rules_extended_options_mode(options, 1, 1, &hardcore) == NETWORK_RULES_MODE_NONE);
	/* hardcore on any engine (a CTF variant: plain CTF, hardcore still) */
	options[1] = 1;
	CHECK(network_rules_extended_options_mode(options, 0, 1, &hardcore) == NETWORK_RULES_MODE_NONE && hardcore);
}

static void test_shield_effects_message(void)
{
	static const uint8_t vector[12] = { 0xC8, 0x00, 0x5B, 0x01, 0x04, 0x03, 0x02, 0x01, 0x05, 0x00, 0x00, 0x00 };
	uint8_t message[NETWORK_RULES_SHIELD_EFFECTS_SIZE];
	uint8_t bad[12];
	int effects = -1;

	CHECK(NETWORK_RULES_SHIELD_EFFECTS_SIZE == 12 && NETWORK_RULES_SHIELD_EFFECTS_KIND == 91);
	network_rules_shield_effects_write(message, 5, 0x01020304);
	CHECK(memcmp(message, vector, 12) == 0);
	CHECK(network_rules_shield_effects_read(vector, 12, 0, NETWORK_RULES_SHIELD_EFFECTS_MAXIMUM, &effects) && effects == 5);
	/* the rejects: a size of 11 or 13, effects 6, reserved 1, two entries,
	a datagram */
	effects = -1;
	CHECK(!network_rules_shield_effects_read(vector, 11, 0, NETWORK_RULES_SHIELD_EFFECTS_MAXIMUM, &effects) && effects == -1);
	CHECK(!network_rules_shield_effects_read(vector, 13, 0, NETWORK_RULES_SHIELD_EFFECTS_MAXIMUM, &effects));
	CHECK(!network_rules_shield_effects_read(vector, 12, 1, NETWORK_RULES_SHIELD_EFFECTS_MAXIMUM, &effects));
	memcpy(bad, vector, 12);
	bad[8] = 6;
	CHECK(network_rules_shield_effects_read(bad, 12, 0, NETWORK_RULES_SHIELD_EFFECTS_MAXIMUM, &effects) && effects == 6);
	bad[8] = 7;
	CHECK(!network_rules_shield_effects_read(bad, 12, 0, NETWORK_RULES_SHIELD_EFFECTS_MAXIMUM, &effects));
	memcpy(bad, vector, 12);
	bad[10] = 1;
	CHECK(!network_rules_shield_effects_read(bad, 12, 0, NETWORK_RULES_SHIELD_EFFECTS_MAXIMUM, &effects));
	memcpy(bad, vector, 12);
	bad[3] = 2;
	CHECK(!network_rules_shield_effects_read(bad, 12, 0, NETWORK_RULES_SHIELD_EFFECTS_MAXIMUM, &effects));
	memcpy(bad, vector, 12);
	bad[2] = 90;
	CHECK(!network_rules_shield_effects_read(bad, 12, 0, NETWORK_RULES_SHIELD_EFFECTS_MAXIMUM, &effects));
	/* effects past the last are written as stock */
	network_rules_shield_effects_write(message, 9, 0);
	CHECK(message[8] == 0 && message[9] == 0);
	network_rules_shield_effects_write(message, 6, 0);
	CHECK(message[8] == 6 && message[9] == 0);
}

static void test_listing_kind(void)
{
	/* a mode only on the Slayer engine (2) */
	CHECK(strcmp(network_rules_kind_name(0x83, 2, 0), "HARDCORE GUN GAME") == 0);
	CHECK(strcmp(network_rules_kind_name(0x83, 2, 1), "HC GUN GAME") == 0);
	CHECK(strcmp(network_rules_kind_name(0x01, 2, 0), "SWAT") == 0);
	CHECK(strcmp(network_rules_kind_name(0x02, 2, 0), "INFECTION") == 0);
	CHECK(strcmp(network_rules_kind_name(0x03, 2, 1), "GUN GAME") == 0);
	CHECK(strcmp(network_rules_kind_name(0x81, 2, 1), "HC SWAT") == 0);
	CHECK(strcmp(network_rules_kind_name(0x82, 2, 1), "HC INFECTION") == 0);
	/* any other engine: the mode counts as none, hardcore stays */
	CHECK(strcmp(network_rules_kind_name(0x83, 1, 0), "HARDCORE CTF") == 0);
	CHECK(strcmp(network_rules_kind_name(0x83, 1, 1), "HC CTF") == 0);
	CHECK(network_rules_kind_name(0x03, 1, 0) == NULL);
	CHECK(strcmp(network_rules_kind_name(0x80, 5, 0), "HARDCORE RACE") == 0);
	CHECK(strcmp(network_rules_kind_name(0x80, 4, 1), "HC KING") == 0);
	/* a mode it does not know is none; no name for an engine outside 1 to 5 */
	CHECK(network_rules_kind_name(0x05, 2, 0) == NULL);
	CHECK(strcmp(network_rules_kind_name(0x85, 2, 0), "HARDCORE SLAYER") == 0);
	CHECK(network_rules_kind_name(0x80, 0, 0) == NULL && network_rules_kind_name(0x80, 6, 0) == NULL);
	CHECK(network_rules_kind_name(0x00, 2, 0) == NULL);
}

static void test_static_reach(void)
{
	float reach = network_rules_static_event_reach(1, 1.5f);

	/* an area effect of cutoff 1.5 reaches 3.5; a tripmine of radius 0.3
	at 0.5 is within its 3.8 */
	CHECK(reach == 3.5f);
	CHECK(network_rules_static_within(0.25f, 0.3f, reach));
	CHECK(network_rules_static_within(3.8f * 3.8f, 0.3f, reach));
	CHECK(!network_rules_static_within(3.9f * 3.9f, 0.3f, reach));
	/* not an area effect: 2.0 alone */
	CHECK(network_rules_static_event_reach(0, 1.5f) == 2.0f);
}

static void test_host_settings(void)
{
	static uint8_t record[NETWORK_RULES_SETTINGS_SIZE];
	static uint8_t wire[NETWORK_RULES_SETTINGS_EXTENDED_SIZE];
	static uint8_t folded[NETWORK_RULES_SETTINGS_EXTENDED_SIZE];
	uint8_t further[NETWORK_RULES_EXTENDED_OPTIONS_SIZE];
	uint8_t extended[NETWORK_RULES_EXTENDED_OPTIONS_SIZE];
	int index, offset;

	/* a host of 34 or older: the older record to everyone, at once */
	CHECK(network_rules_host_settings_size(32, 0, 1) == 0x3340);
	CHECK(network_rules_host_settings_size(34, 36, 0) == 0x3340);
	/* of 36: an OpenCE machine (no HELLO, none to wait for) the newer */
	CHECK(network_rules_host_settings_size(36, 0, 0) == 0x3350);
	/* ... one whose HELLO may still come is waited for */
	CHECK(network_rules_host_settings_size(36, 0, 1) == 0);
	/* ... a Delta build of 32 (0.8.0e's HELLO) the older, one of 36 the newer */
	CHECK(network_rules_host_settings_size(36, 32, 1) == 0x3340);
	CHECK(network_rules_host_settings_size(36, 36, 1) == 0x3350);
	CHECK(network_rules_host_settings_size(35, 36, 0) == 0x3350);
	/* a Delta build newer than the host's number: the host's */
	CHECK(network_rules_host_settings_size(36, 40, 0) == 0x3350);

	/* who reports a static target's own damage: OpenCE's from 34 do not */
	CHECK(network_rules_host_static_damage_reported(32, 0));
	CHECK(!network_rules_host_static_damage_reported(34, 0));
	CHECK(!network_rules_host_static_damage_reported(36, 0));
	/* ... Delta builds before 35 do, as do this build's of a host under 35
	(dealt once, by their report, or lost; never twice) */
	CHECK(network_rules_host_static_damage_reported(36, 32));
	CHECK(network_rules_host_static_damage_reported(32, 36));
	CHECK(network_rules_host_static_damage_reported(34, 36));
	CHECK(!network_rules_host_static_damage_reported(36, 36));

	/* the record of 35's layout made from the older, piece by piece (the
	pieces of 0xE00 of the test vectors: the last 0x950), folds back to it */
	for (index = 0; index < (int)sizeof(record); index++)
		record[index] = (uint8_t)(index * 13 + 5);
	network_rules_extended_options_default(further);
	for (offset = 0; offset < (int)sizeof(wire); offset += 0xE00)
	{
		int length = (int)sizeof(wire) - offset < 0xE00 ? (int)sizeof(wire) - offset : 0xE00;

		if (offset == 0x2A00)
			CHECK(length == 0x950);
		network_rules_settings_extended_copy(record, further, offset, length, wire + offset);
	}
	/* the options' further bytes of a plain game, at 0x333C to 0x334B */
	CHECK(wire[0x333C] == 0x00 && wire[0x333D] == 0x00 && wire[0x333E] == 0x01 && wire[0x333F] == 0x96 &&
		wire[0x3340] == 0xC8 && wire[0x3341] == 0x05 && wire[0x3342] == 0x0A && wire[0x3343] == 0x01 &&
		wire[0x3344] == 0x00 && wire[0x3345] == 0x01);
	for (index = 0x3346; index < 0x334C; index++)
		CHECK(wire[index] == 0);
	/* the local data after them */
	for (index = 0; index < 4; index++)
		CHECK(wire[0x334C + index] == record[0x333C + index]);
	for (index = 0; index < (int)sizeof(folded); index++)
		folded[index] = wire[index];
	network_rules_settings_fold_options(folded, extended);
	for (index = 0; index < (int)sizeof(record); index++)
		CHECK(folded[index] == record[index]);
	for (index = 0; index < NETWORK_RULES_EXTENDED_OPTIONS_SIZE; index++)
		CHECK(extended[index] == further[index]);
	/* one piece across the further bytes' edges */
	network_rules_settings_extended_copy(record, further, 0x3338, 0x18, folded);
	CHECK(folded[0] == record[0x3338] && folded[3] == record[0x333B] && folded[4] == further[0] &&
		folded[19] == further[15] && folded[20] == record[0x333C] && folded[23] == record[0x333F]);
}

/* ---------- layouts */

static struct delta_layout_fact test_facts[DELTA_LAYOUT_FACTS];
static int test_fact_count;

static int test_source(struct delta_layout_fact *facts, int count)
{
	int used = test_fact_count < count ? test_fact_count : count;

	memcpy(facts, test_facts, sizeof(*facts) * (size_t)used);
	return used;
}

static struct delta_layout_fact *test_fact(unsigned version)
{
	struct delta_layout_fact *fact = &test_facts[test_fact_count++];

	memset(fact, 0, sizeof(*fact));
	fact->version = (unsigned short)version;
	return fact;
}

static void test_layouts_built_in(void)
{
	struct network_rules_layout layout;
	unsigned version;

	CHECK(DELTA_LEGACY_NEWEST == 38);
	/* (today's: the record by version, 0 to 6 for message 91 from every
	host, 91 a kind a machine without it drops from 36) */
	for (version = 11; version <= 40; version++)
	{
		network_rules_layout_built_in(version, &layout);
		CHECK(layout.version == version && layout.learned == 0 && layout.refused == 0);
		CHECK(layout.settings_size == (version >= 35 ? 0x3350 : 0x3340));
		CHECK(layout.options_size == (version >= 35 ? 0x2C : 0x1C));
		CHECK(layout.shield_effects_maximum == 6);
		CHECK(network_rules_layout_drops(&layout, 91) == (version >= 36));
		CHECK(!network_rules_layout_drops(&layout, 92) && !network_rules_layout_drops(&layout, 0));
	}
	network_rules_layout_built_in(NETWORK_RULES_VERSION_UNKNOWN, &layout);
	CHECK(layout.settings_size == 0 && layout.options_size == 0 && layout.shield_effects_maximum == 6);
	/* (no source: the built-in facts; the newest is the build's) */
	network_rules_set_layout_source(NULL);
	network_rules_layout(39, &layout);
	CHECK(layout.settings_size == 0x3350 && layout.learned == 0);
	CHECK(network_rules_layout_newest() == 38);
	CHECK(network_rules_settings_options_size(36, 0x3350) == 0x2C && network_rules_settings_options_size(34, 0x3340) == 0x1C);
	CHECK(network_rules_settings_options_size(NETWORK_RULES_VERSION_UNKNOWN, 0x3350) == 0x2C);
	CHECK(network_rules_settings_options_size(NETWORK_RULES_VERSION_UNKNOWN, 0x3340) == 0x1C);
	CHECK(network_rules_host_settings_options(32, 0) == 0x1C && network_rules_host_settings_options(36, 32) == 0x1C);
	CHECK(network_rules_host_settings_options(36, 0) == 0x2C && network_rules_host_settings_options(36, 38) == 0x2C);
}

static void test_layouts_from_the_table(void)
{
	struct network_rules_layout layout;
	struct delta_layout_fact *fact;

	test_fact_count = 0;
	/* (facts of the build's own versions are its rules': never taken) */
	fact = test_fact(35);
	fact->options_size = 0x30;
	fact->shield_effects_maximum = 9;
	fact = test_fact(36);
	fact->dropped[92 >> 3] |= 1 << (92 & 7);
	/* 39: four more option bytes, kinds 92 and 93 dropped, a seventh effect */
	fact = test_fact(39);
	fact->options_size = 0x30;
	fact->shield_effects_maximum = 7;
	fact->dropped[92 >> 3] |= 1 << (92 & 7);
	fact->dropped[93 >> 3] |= 1 << (93 & 7);
	/* 41: six bytes after the local data */
	fact = test_fact(41);
	fact->settings_size = 0x3354 + 6;
	network_rules_set_layout_source(test_source);

	network_rules_layout(38, &layout);
	CHECK(layout.options_size == 0x2C && layout.settings_size == 0x3350 && layout.shield_effects_maximum == 6);
	CHECK(!network_rules_layout_drops(&layout, 92) && layout.learned == 0);
	network_rules_layout(39, &layout);
	CHECK(layout.options_size == 0x30 && layout.settings_size == 0x3354 && layout.shield_effects_maximum == 7);
	CHECK(network_rules_layout_drops(&layout, 92) && network_rules_layout_drops(&layout, 93) &&
		network_rules_layout_drops(&layout, 91) && !network_rules_layout_drops(&layout, 94));
	CHECK(layout.refused == 0);
	/* (a fact holds until the next) */
	network_rules_layout(40, &layout);
	CHECK(layout.options_size == 0x30 && layout.settings_size == 0x3354);
	network_rules_layout(41, &layout);
	CHECK(layout.options_size == 0x30 && layout.settings_size == 0x335A && layout.shield_effects_maximum == 7);
	network_rules_layout(60, &layout);
	CHECK(layout.settings_size == 0x335A);
	network_rules_layout(NETWORK_RULES_VERSION_UNKNOWN, &layout);
	CHECK(layout.settings_size == 0 && layout.learned == 0);
	CHECK(network_rules_layout_newest() == 41);

	/* what the client and the host make of them */
	CHECK(network_rules_settings_size(39) == 0x3354 && network_rules_settings_size(38) == 0x3350);
	CHECK(network_rules_settings_size_taken(39, 0x3354) && !network_rules_settings_size_taken(39, 0x3350));
	CHECK(!network_rules_settings_size_taken(NETWORK_RULES_VERSION_UNKNOWN, 0x3354));
	CHECK(network_rules_settings_options_size(39, 0x3354) == 0x30);
	CHECK(network_rules_settings_options_size(41, 0x335A) == 0x30);
	CHECK(network_rules_host_settings_size(39, 0, 0) == 0x3354 && network_rules_host_settings_options(39, 0) == 0x30);
	CHECK(network_rules_host_settings_size(39, 38, 0) == 0x3350 && network_rules_host_settings_options(39, 38) == 0x2C);
	CHECK(network_rules_host_settings_size(39, 41, 0) == 0x3354 && network_rules_host_settings_options(39, 41) == 0x30);
	CHECK(network_rules_host_settings_size(41, 32, 0) == 0x3340 && network_rules_host_settings_options(41, 32) == 0x1C);
	network_rules_set_layout_source(NULL);
}

static void test_layouts_hostile(void)
{
	struct network_rules_layout layout;
	struct delta_layout_fact *fact;

	/* sizes that shrink, past the limits, a lower largest effect, kind 0 */
	test_fact_count = 0;
	fact = test_fact(39);
	fact->options_size = 0x20;
	fact->settings_size = 0x3300;
	fact->shield_effects_maximum = 5;
	fact->dropped[0] = 0x01;
	fact = test_fact(40);
	fact->options_size = 0x2C + NETWORK_RULES_LAYOUT_GROWTH_MAXIMUM + 1;
	fact->shield_effects_maximum = NETWORK_RULES_SHIELD_EFFECTS_LIMIT + 1;
	fact = test_fact(41);
	fact->settings_size = NETWORK_RULES_SETTINGS_MAXIMUM + 1;
	fact = test_fact(42);
	fact->options_size = 0x2C + 40;
	fact->settings_size = 0x3350 + 40 + 2;
	fact = test_fact(43);
	/* (the options grow, the bytes after the local data would shrink) */
	fact->options_size = 0x2C + 44;
	fact->settings_size = 0x3350 + 42;
	fact = test_fact(44);
	fact->options_size = 0x2C + 64;
	network_rules_set_layout_source(test_source);

	network_rules_layout(39, &layout);
	CHECK(layout.options_size == 0x2C && layout.settings_size == 0x3350 && layout.shield_effects_maximum == 6);
	CHECK(layout.refused == 3 && !network_rules_layout_drops(&layout, 0));
	network_rules_layout(41, &layout);
	CHECK(layout.options_size == 0x2C && layout.settings_size == 0x3350 && layout.shield_effects_maximum == 6);
	CHECK(layout.refused == 6);
	network_rules_layout(42, &layout);
	CHECK(layout.options_size == 0x2C + 40 && layout.settings_size == 0x3350 + 42);
	network_rules_layout(43, &layout);
	CHECK(layout.options_size == 0x2C + 44 && layout.settings_size == 0x3350 + 46);
	/* (past the record's limit with the bytes kept after the local data) */
	network_rules_layout(44, &layout);
	CHECK(layout.options_size == 0x2C + 44 && layout.settings_size <= NETWORK_RULES_SETTINGS_MAXIMUM);
	CHECK(layout.settings_size >= NETWORK_RULES_SETTINGS_HEAD_SIZE + layout.options_size + 4);
	network_rules_set_layout_source(NULL);
	/* (the shield effects message never past its limit) */
	{
		uint8_t message[NETWORK_RULES_SHIELD_EFFECTS_SIZE];
		int effects = -1;

		network_rules_shield_effects_write(message, 0, 1);
		message[8] = 7;
		CHECK(!network_rules_shield_effects_read(message, 12, 0, 6, &effects));
		CHECK(network_rules_shield_effects_read(message, 12, 0, 7, &effects) && effects == 7);
		message[8] = 16;
		CHECK(!network_rules_shield_effects_read(message, 12, 0, 255, &effects));
		message[8] = 15;
		CHECK(network_rules_shield_effects_read(message, 12, 0, 255, &effects) && effects == 15);
	}
}

static void test_layout_record(void)
{
	/* a record of 0x30 bytes of options and 6 after the local data, made
	from the older and folded back */
	static uint8_t record[0x3340], wire[0x3354 + 6], folded[NETWORK_RULES_SETTINGS_MAXIMUM];
	uint8_t further[NETWORK_RULES_EXTENDED_OPTIONS_SIZE], extended[NETWORK_RULES_EXTENDED_OPTIONS_SIZE];
	int index, offset;

	for (index = 0; index < (int)sizeof(record); index++)
		record[index] = (uint8_t)(index * 7 + 3);
	for (index = 0; index < (int)sizeof(further); index++)
		further[index] = (uint8_t)(0xA0 + index);
	for (offset = 0; offset < (int)sizeof(wire); offset += 0xE00)
	{
		int length = (int)sizeof(wire) - offset < 0xE00 ? (int)sizeof(wire) - offset : 0xE00;

		network_rules_settings_layout_copy(record, further, 0x30, offset, length, wire + offset);
	}
	CHECK(memcmp(wire, record, 0x333C) == 0);
	CHECK(memcmp(wire + 0x333C, further, 16) == 0);
	for (index = 0x334C; index < 0x3350; index++)
		CHECK(wire[index] == 0);
	CHECK(memcmp(wire + 0x3350, record + 0x333C, 4) == 0);
	for (index = 0x3354; index < (int)sizeof(wire); index++)
		CHECK(wire[index] == 0);
	memset(folded, 0xEE, sizeof(folded));
	memcpy(folded, wire, sizeof(wire));
	network_rules_settings_fold(folded, (int)sizeof(wire), 0x30, extended);
	CHECK(memcmp(folded, record, sizeof(record)) == 0);
	CHECK(memcmp(extended, further, sizeof(extended)) == 0);
	for (index = (int)sizeof(record); index < (int)sizeof(wire); index++)
		CHECK(folded[index] == 0);
	CHECK(folded[sizeof(wire)] == 0xEE);
	/* (a fold that does not fit is not made) */
	memcpy(folded, wire, sizeof(wire));
	network_rules_settings_fold(folded, NETWORK_RULES_SETTINGS_MAXIMUM + 1, 0x30, extended);
	CHECK(memcmp(folded, wire, sizeof(wire)) == 0);
	network_rules_settings_fold(folded, 0x3340, 0x30, extended);
	CHECK(memcmp(folded, wire, sizeof(wire)) == 0);
	/* (the older layouts as before: 0x1C copies the record as it is) */
	network_rules_settings_layout_copy(record, further, 0x1C, 0x3300, 0x40, wire);
	CHECK(memcmp(wire, record + 0x3300, 0x40) == 0);
}

int main(void)
{
	test_aim_assist_message();
	test_input_entry();
	test_stamp_window();
	test_elevator_correction();
	test_riding();
	test_aim_assist_policy();
	test_aim_device();
	test_settings_size();
	test_extended_options();
	test_shield_effects_message();
	test_listing_kind();
	test_static_reach();
	test_host_settings();
	test_layouts_built_in();
	test_layouts_from_the_table();
	test_layouts_hostile();
	test_layout_record();
	if (failures)
	{
		printf("network_rules_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("network_rules_test: ok\n");
	return 0;
}
