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

/* a hit report's stamp of the host's tick (network_damage.c,
REPORT_MAXIMUM_AGE_TICKS of 90 there) */
static void test_report_stamp(void)
{
	/* now, and up to the age before it, are taken; later, older or none not */
	CHECK(network_rules_report_stamp_taken(1000, 1000, 90));
	CHECK(network_rules_report_stamp_taken(1000, 910, 90));
	CHECK(!network_rules_report_stamp_taken(1000, 909, 90));
	CHECK(!network_rules_report_stamp_taken(1000, 1001, 90));
	CHECK(!network_rules_report_stamp_taken(1000, -1, 90));
	/* (the first ticks of a game, as before: a stamp before 0 within the age) */
	CHECK(network_rules_report_stamp_taken(10, 0, 90));
	CHECK(network_rules_report_stamp_taken(10, -5, 90));
	/* one long before the game began is refused (its age wrapped, it was
	taken) */
	CHECK(!network_rules_report_stamp_taken(1000, -2147483647 - 1, 90));
	CHECK(!network_rules_report_stamp_taken(1000, -2147482700, 90));
	CHECK(!network_rules_report_stamp_taken(2147483647, -2147483647 - 1, 90));
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
	nothing once the mouse has aimed) */
	CHECK(network_rules_mouse_is_aim_device(0, 0, 0, 0));
	CHECK(network_rules_mouse_is_aim_device(500, 0, 0, 0));
	CHECK(network_rules_mouse_is_aim_device(500, 0, 0, 1));
	/* nothing aimed yet with a controller connected: the pad player keeps
	the stick's magnetism (#861) */
	CHECK(!network_rules_mouse_is_aim_device(0, 0, 0, 1));
	CHECK(!network_rules_mouse_is_aim_device(0, 500, 0, 1));
	CHECK(network_rules_mouse_is_aim_device(600, 500, 0, 1));
	/* the mouse after the stick, or at the same time, is; before it is not */
	CHECK(network_rules_mouse_is_aim_device(600, 500, 0, 0));
	CHECK(network_rules_mouse_is_aim_device(500, 500, 0, 0));
	CHECK(!network_rules_mouse_is_aim_device(400, 500, 0, 0));
	CHECK(!network_rules_mouse_is_aim_device(0, 500, 0, 0));
	/* touch at or after both is not the mouse; before either is */
	CHECK(!network_rules_mouse_is_aim_device(500, 400, 700, 0));
	CHECK(!network_rules_mouse_is_aim_device(500, 400, 500, 0));
	CHECK(network_rules_mouse_is_aim_device(500, 400, 450, 0));
	CHECK(!network_rules_mouse_is_aim_device(0, 0, 100, 0));
	CHECK(network_rules_mouse_is_aim_device(600, 500, 550, 0));
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
	/* whether a HELLO could change the record: not for a host under 35, nor
	when every version a Delta machine may say takes the host's; a Delta build
	of 32 takes another */
	CHECK(!network_rules_host_hello_matters(34, 0));
	CHECK(!network_rules_host_hello_matters(39, 39) && !network_rules_host_hello_matters(39, 35));
	CHECK(!network_rules_host_hello_matters(35, 40));
	CHECK(network_rules_host_hello_matters(39, 32) && network_rules_host_hello_matters(39, 0));

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

	CHECK(DELTA_LEGACY_NEWEST == 43);
	/* (today's: the record by version, 0 to 6 for message 91 from every
	host, 91 a kind a machine without it drops from 36) */
	for (version = 11; version <= 43; version++)
	{
		network_rules_layout_built_in(version, &layout);
		CHECK(layout.version == version && layout.learned == 0 && layout.refused == 0);
		CHECK(layout.settings_size == (version >= 42 ? 0x3390 : version >= 35 ? 0x3350 : 0x3340));
		CHECK(layout.options_size == (version >= 35 ? 0x2C : 0x1C));
		CHECK(layout.shield_effects_maximum == 6);
		CHECK(network_rules_layout_drops(&layout, 91) == (version >= 36));
		CHECK(!network_rules_layout_drops(&layout, 92) && !network_rules_layout_drops(&layout, 0));
		/* (41's votes, which this build drops; 40's longer join request) */
		CHECK(network_rules_layout_drops(&layout, 78) == (version >= 41) &&
			network_rules_layout_drops(&layout, 79) == (version >= 41) &&
			network_rules_layout_drops(&layout, 94) == (version >= 41) &&
			network_rules_layout_drops(&layout, 95) == (version >= 41));
		CHECK(layout.join_request_extra == (version >= 42 ? 236 : version >= 40 ? 234 : 0));
		/* (42 and 43: the word, the header's epoch, the pose; 43 the deck) */
		CHECK(layout.join_request_version_at == (version >= 42 ? 234 : -1));
		CHECK(layout.header_extra == (version >= 42 ? 4 : 0) && layout.epoch_kind == (version >= 42 ? 96 : 0));
		CHECK(layout.unit_part_size[6] == (version >= 42 ? 30 : 0) && layout.unit_part_size[7] == (version >= 43 ? 28 : 0));
		CHECK(layout.rider_ejection_up == (version >= 43 ? 200 : 0));
		CHECK(network_rules_layout_drops(&layout, 97) == (version >= 42) &&
			network_rules_layout_drops(&layout, 107) == (version >= 43) && !network_rules_layout_drops(&layout, 96));
	}
	network_rules_layout_built_in(NETWORK_RULES_VERSION_UNKNOWN, &layout);
	CHECK(layout.settings_size == 0 && layout.options_size == 0 && layout.shield_effects_maximum == 6);
	/* (no source: the built-in facts; the newest is the build's) */
	network_rules_set_layout_source(NULL);
	network_rules_layout(44, &layout);
	CHECK(layout.settings_size == 0x3390 && layout.learned == 0 && layout.epoch_kind == 96);
	CHECK(network_rules_layout_newest() == 43);
	/* (the join request: 0x70 bytes to 39 and older, and to a host not
	known; 234 zero bytes more from 40, and to a newer host than the build
	knows) */
	CHECK(network_rules_join_request_extra(39) == 0 && network_rules_join_request_extra(11) == 0);
	CHECK(network_rules_join_request_extra(40) == 234 && network_rules_join_request_extra(41) == 234);
	CHECK(network_rules_join_request_extra(42) == 236 && network_rules_join_request_extra(44) == 236);
	CHECK(network_rules_join_request_extra(NETWORK_RULES_VERSION_UNKNOWN) == 0);
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
	/* 44: four more option bytes, kinds 92 and 93 dropped, a seventh effect */
	fact = test_fact(44);
	fact->options_size = 0x30;
	fact->shield_effects_maximum = 7;
	fact->dropped[92 >> 3] |= 1 << (92 & 7);
	fact->dropped[93 >> 3] |= 1 << (93 & 7);
	/* 46: six bytes after the local data */
	fact = test_fact(46);
	fact->settings_size = 0x3394 + 6;
	/* 46 also: the join request 300 zero bytes past 0x70 */
	fact->join_request_said = 1;
	fact->join_request_extra = 300;
	network_rules_set_layout_source(test_source);

	network_rules_layout(43, &layout);
	CHECK(layout.options_size == 0x2C && layout.settings_size == 0x3390 && layout.shield_effects_maximum == 6);
	CHECK(!network_rules_layout_drops(&layout, 92) && layout.learned == 0);
	network_rules_layout(44, &layout);
	CHECK(layout.options_size == 0x30 && layout.settings_size == 0x3394 && layout.shield_effects_maximum == 7);
	CHECK(network_rules_layout_drops(&layout, 92) && network_rules_layout_drops(&layout, 93) &&
		network_rules_layout_drops(&layout, 91) && !network_rules_layout_drops(&layout, 96));
	CHECK(layout.refused == 0);
	/* (a fact holds until the next) */
	network_rules_layout(45, &layout);
	CHECK(layout.options_size == 0x30 && layout.settings_size == 0x3394);
	network_rules_layout(46, &layout);
	CHECK(layout.options_size == 0x30 && layout.settings_size == 0x339A && layout.shield_effects_maximum == 7);
	network_rules_layout(65, &layout);
	CHECK(layout.settings_size == 0x339A);
	network_rules_layout(NETWORK_RULES_VERSION_UNKNOWN, &layout);
	CHECK(layout.settings_size == 0 && layout.learned == 0);
	CHECK(network_rules_layout_newest() == 46);
	CHECK(network_rules_join_request_extra(45) == 236 && network_rules_join_request_extra(46) == 300);
	CHECK(network_rules_join_request_extra(65) == 300 && network_rules_join_request_extra(43) == 236);
	CHECK(network_rules_join_request_extra(NETWORK_RULES_VERSION_UNKNOWN) == 0);

	/* what the client and the host make of them */
	CHECK(network_rules_settings_size(44) == 0x3394 && network_rules_settings_size(43) == 0x3390);
	CHECK(network_rules_settings_size_taken(44, 0x3394) && !network_rules_settings_size_taken(44, 0x3390));
	CHECK(!network_rules_settings_size_taken(NETWORK_RULES_VERSION_UNKNOWN, 0x3394));
	CHECK(network_rules_settings_options_size(44, 0x3394) == 0x30);
	CHECK(network_rules_settings_options_size(46, 0x339A) == 0x30);
	CHECK(network_rules_host_settings_size(44, 0, 0) == 0x3394 && network_rules_host_settings_options(44, 0) == 0x30);
	CHECK(network_rules_host_settings_size(44, 41, 0) == 0x3350 && network_rules_host_settings_options(44, 41) == 0x2C);
	CHECK(network_rules_host_settings_size(44, 43, 0) == 0x3390 && network_rules_host_settings_options(44, 43) == 0x2C);
	CHECK(network_rules_host_settings_size(44, 44, 0) == 0x3394 && network_rules_host_settings_options(44, 44) == 0x30);
	CHECK(network_rules_host_settings_size(46, 32, 0) == 0x3340 && network_rules_host_settings_options(46, 32) == 0x1C);
	/* (a host of 44's layout: a HELLO of 43 takes the older one; of 44 on
	the same) */
	CHECK(network_rules_host_hello_matters(44, 43) && !network_rules_host_hello_matters(44, 44));
	CHECK(!network_rules_host_hello_matters(45, 44) && network_rules_host_hello_matters(46, 45));
	network_rules_set_layout_source(NULL);
}

static void test_layouts_hostile(void)
{
	struct network_rules_layout layout;
	struct delta_layout_fact *fact;

	/* sizes that shrink, past the limits, a lower largest effect, kind 0 */
	test_fact_count = 0;
	fact = test_fact(44);
	fact->options_size = 0x20;
	fact->settings_size = 0x3300;
	fact->shield_effects_maximum = 5;
	fact->dropped[0] = 0x01;
	fact = test_fact(45);
	fact->options_size = 0x2C + NETWORK_RULES_LAYOUT_GROWTH_MAXIMUM + 1;
	fact->shield_effects_maximum = NETWORK_RULES_SHIELD_EFFECTS_LIMIT + 1;
	fact = test_fact(46);
	fact->settings_size = NETWORK_RULES_SETTINGS_MAXIMUM + 1;
	fact = test_fact(47);
	fact->options_size = 0x2C + 40;
	fact->settings_size = 0x3390 + 40 + 2;
	fact = test_fact(48);
	/* (the options grow, the bytes after the local data would shrink) */
	fact->options_size = 0x2C + 44;
	fact->settings_size = 0x3390 + 42;
	fact = test_fact(49);
	fact->options_size = 0x2C + 64;
	network_rules_set_layout_source(test_source);

	network_rules_layout(44, &layout);
	CHECK(layout.options_size == 0x2C && layout.settings_size == 0x3390 && layout.shield_effects_maximum == 6);
	CHECK(layout.refused == 3 && !network_rules_layout_drops(&layout, 0));
	network_rules_layout(46, &layout);
	CHECK(layout.options_size == 0x2C && layout.settings_size == 0x3390 && layout.shield_effects_maximum == 6);
	CHECK(layout.refused == 6);
	network_rules_layout(47, &layout);
	CHECK(layout.options_size == 0x2C + 40 && layout.settings_size == 0x3390 + 42);
	network_rules_layout(48, &layout);
	CHECK(layout.options_size == 0x2C + 44 && layout.settings_size == 0x3390 + 46);
	/* (past the record's limit with the bytes kept after the local data) */
	network_rules_layout(49, &layout);
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

static void test_layouts_join_request(void)
{
	struct network_rules_layout layout;
	struct delta_layout_fact *fact;

	/* a table's join request length: none again (0), past the limit, of a
	fill this build does not write, and for a version the build knows */
	test_fact_count = 0;
	fact = test_fact(40);
	fact->join_request_said = 1;
	fact->join_request_extra = 10;
	fact = test_fact(44);
	fact->join_request_said = 1;
	fact->join_request_extra = 0;
	fact = test_fact(45);
	fact->join_request_said = 1;
	fact->join_request_extra = NETWORK_RULES_JOIN_REQUEST_EXTRA_LIMIT + 1;
	fact = test_fact(46);
	fact->join_request_said = 1;
	fact->join_request_extra = 64;
	fact->join_request_fill = DELTA_LAYOUT_FILL_UNKNOWN;
	fact = test_fact(47);
	fact->join_request_said = 1;
	fact->join_request_extra = NETWORK_RULES_JOIN_REQUEST_EXTRA_LIMIT;
	/* (a fact that does not say it leaves it as it was) */
	fact = test_fact(48);
	fact->shield_effects_maximum = 7;
	network_rules_set_layout_source(test_source);

	CHECK(network_rules_join_request_extra(40) == 234 && network_rules_join_request_extra(43) == 236);
	CHECK(network_rules_join_request_extra(44) == 0);
	network_rules_layout(46, &layout);
	CHECK(layout.join_request_extra == 0 && layout.refused == 2);
	CHECK(network_rules_join_request_extra(47) == NETWORK_RULES_JOIN_REQUEST_EXTRA_LIMIT);
	CHECK(network_rules_join_request_extra(48) == NETWORK_RULES_JOIN_REQUEST_EXTRA_LIMIT);
	network_rules_set_layout_source(NULL);
	CHECK(network_rules_join_request_extra(48) == 236);
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

/* the facts of OpenCE's network version 43 as a table row says them
(docs/delta.md, "Layouts"): the record 64 bytes longer, the join request's
version word, the 12-byte header with its checkpoint epoch and boundary (96), the unit
state's deck (bit 7, 28 bytes after the up) and scripted pose (bit 6, 30
bytes last), riders thrown from a vehicle on its side, and the MCC-only
kinds and 107 dropped */
static void test_fact_43(struct delta_layout_fact *fact)
{
	static const int dropped[] = { 97, 98, 99, 100, 103, 104, 105, 106, 107 };
	size_t index;

	fact->settings_size = 13200;
	fact->join_request_said = 1;
	fact->join_request_extra = 236;
	fact->join_request_version_said = 1;
	fact->join_request_version_at = 234;
	fact->header_extra = 4;
	fact->epoch_kind = 96;
	fact->unit_part_size[7] = 28;
	fact->unit_part_at[7] = DELTA_LAYOUT_PART_AFTER_UP;
	fact->unit_part_size[6] = 30;
	fact->unit_part_at[6] = DELTA_LAYOUT_PART_LAST;
	fact->rider_ejection_up = 200;
	for (index = 0; index < sizeof(dropped) / sizeof(dropped[0]); index++)
		fact->dropped[dropped[index] >> 3] |= (uint8_t)(1 << (dropped[index] & 7));
}

static void test_layouts_43(void)
{
	struct network_rules_layout layout;
	uint8_t tail[NETWORK_RULES_JOIN_REQUEST_EXTRA_LIMIT];
	int index;

	test_fact_count = 0;
	test_fact_43(test_fact(43));
	network_rules_set_layout_source(test_source);

	network_rules_layout(43, &layout);
	CHECK(layout.refused == 0);
	CHECK(layout.settings_size == 0x3390 && layout.options_size == 0x2C);
	CHECK(layout.join_request_extra == 236 && layout.join_request_version_at == 234);
	CHECK(layout.header_extra == 4 && layout.epoch_kind == 96);
	CHECK(layout.unit_part_size[7] == 28 && layout.unit_part_at[7] == DELTA_LAYOUT_PART_AFTER_UP);
	CHECK(layout.unit_part_size[6] == 30 && layout.unit_part_at[6] == DELTA_LAYOUT_PART_LAST);
	CHECK(network_rules_rider_ejection_up(&layout) > 0.1999f && network_rules_rider_ejection_up(&layout) < 0.2001f);
	CHECK(network_rules_layout_drops(&layout, 97) && network_rules_layout_drops(&layout, 100) &&
		!network_rules_layout_drops(&layout, 101) && !network_rules_layout_drops(&layout, 102) &&
		network_rules_layout_drops(&layout, 103) && network_rules_layout_drops(&layout, 107) &&
		!network_rules_layout_drops(&layout, 96));
	/* (the settings: 4 pieces of 0xE00, the last 2,448 bytes) */
	CHECK(network_rules_settings_size(43) == 13200 && 13200 - 3 * 0xE00 == 2448);
	CHECK(network_rules_settings_size_taken(43, 13200) && !network_rules_settings_size_taken(43, 13136));
	CHECK(network_rules_host_settings_size(43, 0, 0) == 13200);
	/* (the join request: 0x70 + 236 = 0x15C, the version word last) */
	CHECK(NETWORK_RULES_JOIN_REQUEST_SIZE + network_rules_join_request_tail(43, tail, sizeof(tail)) == 0x15C);
	/* (big-endian, the packet codec's order: 00 2B) */
	CHECK(tail[234] == 0x00 && tail[235] == 0x2B);
	for (index = 0; index < 234; index++)
	{
		if (tail[index])
			break;
	}
	CHECK(index == 234);
	CHECK(network_rules_join_request_tail(43, tail, 235) == -1);
	CHECK(network_rules_join_request_tail(44, tail, sizeof(tail)) == 236 && tail[234] == 0 && tail[235] == 44);
	CHECK(network_rules_join_request_tail(41, tail, sizeof(tail)) == 234);
	CHECK(network_rules_join_request_tail(NETWORK_RULES_VERSION_UNKNOWN, tail, sizeof(tail)) == 0);
	/* (a host of 43: its own number taken, another refused; a request
	without the word, an older build's or a console's, not refused here) */
	network_rules_join_request_tail(43, tail, sizeof(tail));
	CHECK(!network_rules_join_request_refused(43, tail, 236));
	tail[235] = 42;
	CHECK(network_rules_join_request_refused(43, tail, 236));
	/* (the little-endian word, 0x2B00: refused) */
	tail[234] = 0x2B;
	tail[235] = 0x00;
	CHECK(network_rules_join_request_refused(43, tail, 236));
	/* (without the word: 41's full request and the plain one, refused by a
	host of 43; a host of 41 refuses none) */
	CHECK(network_rules_join_request_refused(43, tail, 234) && network_rules_join_request_refused(43, tail, 0));
	CHECK(network_rules_join_request_refused(43, NULL, 0));
	CHECK(!network_rules_join_request_refused(41, tail, 236) && !network_rules_join_request_refused(41, tail, 234));
	CHECK(!network_rules_join_request_refused(41, NULL, 0));
	CHECK(!network_rules_join_request_refused(NETWORK_RULES_VERSION_UNKNOWN, tail, 236));

	/* (41 and the build's own versions: none of them) */
	network_rules_layout(41, &layout);
	CHECK(layout.header_extra == 0 && layout.epoch_kind == 0 && layout.join_request_version_at == -1);
	CHECK(layout.unit_part_size[6] == 0 && layout.unit_part_size[7] == 0 && layout.rider_ejection_up == 0);
	CHECK(network_rules_rider_ejection_up(&layout) == 0.0f);
	/* (built in: the same without any table) */
	network_rules_set_layout_source(NULL);
	network_rules_layout(43, &layout);
	CHECK(layout.refused == 0 && layout.learned == 0 && layout.settings_size == 0x3390);
	CHECK(layout.header_extra == 4 && layout.epoch_kind == 96 && layout.join_request_version_at == 234);
	CHECK(layout.unit_part_size[7] == 28 && layout.unit_part_size[6] == 30 && layout.rider_ejection_up == 200);
	CHECK(network_rules_layout_drops(&layout, 107) && network_rules_layout_drops(&layout, 97));
	CHECK(network_rules_join_request_tail(43, tail, sizeof(tail)) == 236 && tail[234] == 0 && tail[235] == 43);
}

static void test_layouts_43_hostile(void)
{
	struct network_rules_layout layout;
	struct delta_layout_fact *fact;

	/* facts past 43's built-in layout (its epoch, word, parts): a boundary
	kind this build knows, a built-in part's bit, a part too long, one of no
	place, the header too long or shorter than the epoch, the version word
	past the request's end, the ejection past its limit */
	test_fact_count = 0;
	fact = test_fact(44);
	fact->epoch_kind = 91;
	fact->unit_part_size[2] = 4;
	fact->unit_part_at[2] = DELTA_LAYOUT_PART_LAST;
	fact->unit_part_size[7] = NETWORK_RULES_UNIT_PART_LIMIT + 1;
	fact->unit_part_at[7] = DELTA_LAYOUT_PART_LAST;
	fact->unit_part_size[6] = 4;
	fact->unit_part_at[6] = 3;
	fact = test_fact(45);
	fact->header_extra = NETWORK_RULES_HEADER_EXTRA_LIMIT + 1;
	fact->join_request_version_said = 1;
	fact->join_request_version_at = 235;
	fact->rider_ejection_up = NETWORK_RULES_RIDER_EJECTION_LIMIT + 1;
	fact = test_fact(46);
	fact->header_extra = 2;
	network_rules_set_layout_source(test_source);
	network_rules_layout(44, &layout);
	CHECK(layout.refused == 4 && layout.epoch_kind == 96 && layout.unit_part_size[2] == 0 &&
		layout.unit_part_size[7] == 28 && layout.unit_part_size[6] == 30 && layout.unit_part_at[6] == DELTA_LAYOUT_PART_LAST);
	network_rules_layout(46, &layout);
	CHECK(layout.refused == 8 && layout.header_extra == 4 && layout.join_request_version_at == 234 &&
		layout.rider_ejection_up == 200);
	/* 47: the header longer, a new kind for the boundary, the word moved */
	fact = test_fact(47);
	fact->header_extra = 8;
	fact->epoch_kind = 120;
	fact->join_request_version_said = 1;
	fact->join_request_version_at = 0;
	network_rules_layout(47, &layout);
	CHECK(layout.header_extra == 8 && layout.epoch_kind == 120 && layout.join_request_version_at == 0);
	/* 48: the request shorter, the word gone with it only if past it */
	fact = test_fact(48);
	fact->join_request_said = 1;
	fact->join_request_extra = 100;
	network_rules_layout(48, &layout);
	CHECK(layout.join_request_extra == 100 && layout.join_request_version_at == 0);
	network_rules_set_layout_source(NULL);
}

/* a message header word as the transport gives it (this machine's order):
the size in bits 4 to 15, the data type (2) in bits 2 and 3 */
static void test_header_word(uint8_t *message, int size)
{
	uint16_t word = (uint16_t)((size << 4) | (2 << 2));

	memcpy(message, &word, sizeof(word));
}

static int test_header_size(uint8_t const *message)
{
	uint16_t word;

	memcpy(&word, message, sizeof(word));
	return word >> 4;
}

static void test_header_extension(void)
{
	/* the shield effects message (12 bytes) to 16, its epoch at 8 */
	uint8_t message[12], wide[64], narrow[64], batch[64], batch_wide[96];
	uint32_t epoch = 0;
	int size, at;

	network_rules_shield_effects_write(message, 3, 0x01020304);
	test_header_word(message, 12);
	size = network_rules_header_expand(message, 12, wide, sizeof(wide), 4, 1, 0xA1B2C3D4u);
	CHECK(size == 16 && test_header_size(wide) == 16);
	CHECK(wide[2] == 91 && wide[3] == 1 && wide[4] == 0x04 && wide[7] == 0x01);
	CHECK(wide[8] == 0xD4 && wide[9] == 0xC3 && wide[10] == 0xB2 && wide[11] == 0xA1);
	CHECK(wide[12] == 3 && wide[13] == 0 && wide[14] == 0 && wide[15] == 0);
	CHECK(network_rules_header_strip(wide, 16, narrow, 4, 1, 0, 0, &epoch) == 12 && epoch == 0xA1B2C3D4u);
	CHECK(test_header_size(narrow) == 12 && !memcmp(narrow + 2, message + 2, 10));
	/* (no room, too short, an epoch without its bytes) */
	CHECK(network_rules_header_expand(message, 12, wide, 15, 4, 1, 1) == 0);
	CHECK(network_rules_header_expand(message, 7, wide, sizeof(wide), 4, 1, 1) == 0);
	CHECK(network_rules_header_expand(message, 12, wide, sizeof(wide), 2, 1, 1) == 0);
	CHECK(network_rules_header_strip(wide, 11, narrow, 4, 1, 0, 0, &epoch) == 0);
	/* (the message 96 boundary: the 12-byte header alone, no entries) */
	{
		uint8_t boundary[8] = { 0, 0, 96, 0, 0x10, 0, 0, 0 };

		test_header_word(boundary, 8);
		CHECK(network_rules_header_expand(boundary, 8, wide, sizeof(wide), 4, 1, 7) == 12);
		CHECK(wide[2] == 96 && wide[3] == 0 && wide[8] == 7 && test_header_size(wide) == 12);
		CHECK(network_rules_header_strip(wide, 12, narrow, 4, 1, 0, 0, &epoch) == 8 && epoch == 7);
		CHECK(network_rules_boundary_taken(8, 0, 1, 0, epoch, 0));
		CHECK(network_rules_boundary_taken(8, 0, 1, 0, 8, 7) && !network_rules_boundary_taken(8, 0, 1, 0, 7, 7));
		CHECK(!network_rules_boundary_taken(8, 0, 0, 0, 8, 7) && !network_rules_boundary_taken(8, 0, 1, 1, 8, 7));
		CHECK(!network_rules_boundary_taken(8, 1, 1, 0, 8, 7) && !network_rules_boundary_taken(12, 0, 1, 0, 8, 7));
		CHECK(!network_rules_boundary_taken(8, 0, 1, 0, 0, 0));
	}
	/* (epochs: past 0 on rollover, newer by the signed difference) */
	CHECK(network_rules_epoch_next(0) == 1 && network_rules_epoch_next(0xFFFFFFFFu) == 1);
	CHECK(network_rules_epoch_newer(1, 0xFFFFFFFFu) && !network_rules_epoch_newer(0xFFFFFFFFu, 1));
	CHECK(network_rules_epoch_newer(5, 0) && !network_rules_epoch_newer(5, 5));

	/* a batch: its header, then two messages (a size word, the header's 6
	bytes past the message header, entries) */
	memset(batch, 0, sizeof(batch));
	batch[2] = NETWORK_RULES_BATCH_KIND;
	batch[4] = 0x11;
	at = 8;
	batch[at] = 6 + 3; /* kind 2, one entry of 3 bytes */
	batch[at + 2] = 2;
	batch[at + 3] = 1;
	batch[at + 4] = 0x22;
	batch[at + 8] = 0xA;
	batch[at + 9] = 0xB;
	batch[at + 10] = 0xC;
	at += 2 + 9;
	batch[at] = 6 + 4; /* kind 13, 4 bytes */
	batch[at + 2] = 13;
	batch[at + 3] = 1;
	batch[at + 8] = 0xD;
	batch[at + 11] = 0xE;
	at += 2 + 10;
	test_header_word(batch, at);
	size = network_rules_header_expand(batch, at, batch_wide, sizeof(batch_wide), 4, 1, 9);
	CHECK(size == at + 12 && test_header_size(batch_wide) == size);
	CHECK(batch_wide[2] == NETWORK_RULES_BATCH_KIND && batch_wide[8] == 9);
	/* (the first message: its size 13, kind, its epoch, then its entry) */
	CHECK(batch_wide[12] == 13 && batch_wide[13] == 0 && batch_wide[14] == 2 && batch_wide[16] == 0x22);
	CHECK(batch_wide[20] == 9 && batch_wide[24] == 0xA && batch_wide[26] == 0xC);
	CHECK(batch_wide[27] == 14 && batch_wide[29] == 13 && batch_wide[35] == 9 && batch_wide[39] == 0xD &&
		batch_wide[42] == 0xE);
	CHECK(network_rules_header_strip(batch_wide, size, narrow, 4, 1, 9, 0, &epoch) == at && epoch == 9);
	CHECK(!memcmp(narrow, batch, (size_t)at));
	/* (a message of another epoch left out, but the kind kept) */
	batch_wide[35] = 8;
	CHECK(network_rules_header_strip(batch_wide, size, narrow, 4, 1, 9, 0, &epoch) == 8 + 11);
	CHECK(!memcmp(narrow + 2, batch + 2, 8 + 11 - 2));
	CHECK(network_rules_header_strip(batch_wide, size, narrow, 4, 1, 9, 13, &epoch) == at);
	CHECK(network_rules_header_strip(batch_wide, size, narrow, 4, 1, 7, 0, &epoch) == 8);
	/* (a size word past the end, one shorter than the header: read to it,
	the message before it kept) */
	batch_wide[35] = 9;
	batch_wide[27] = 200;
	CHECK(network_rules_header_strip(batch_wide, size, narrow, 4, 1, 9, 0, &epoch) == 8 + 11);
	batch_wide[27] = 9;
	CHECK(network_rules_header_strip(batch_wide, size, narrow, 4, 1, 9, 0, &epoch) == 8 + 11);
	CHECK(!memcmp(narrow + 2, batch + 2, 8 + 11 - 2));
	/* (the batch header's own epoch is not checked) */
	batch_wide[27] = 14;
	batch_wide[8] = 77;
	CHECK(network_rules_header_strip(batch_wide, size, narrow, 4, 1, 9, 0, &epoch) == at && epoch == 77);
	batch[19] = 200;
	CHECK(network_rules_header_expand(batch, at, batch_wide, sizeof(batch_wide), 4, 1, 9) == 0);
	/* (further bytes without an epoch: zeros) */
	batch[19] = 6 + 4;
	size = network_rules_header_expand(batch, at, batch_wide, sizeof(batch_wide), 2, 0, 9);
	CHECK(size == at + 6 && batch_wide[8] == 0 && batch_wide[9] == 0 && batch_wide[10] == 11);
	CHECK(network_rules_header_strip(batch_wide, size, narrow, 2, 0, 0, 0, &epoch) == at && epoch == 0);
	CHECK(!memcmp(narrow, batch, (size_t)at));
}

static void test_unit_parts(void)
{
	struct network_rules_layout layout;

	test_fact_count = 0;
	test_fact_43(test_fact(43));
	network_rules_set_layout_source(test_source);
	network_rules_layout(43, &layout);
	/* (the built-in parts alone: none further, anywhere) */
	CHECK(network_rules_unit_part_bytes(&layout, 0x3F, DELTA_LAYOUT_PART_AFTER_UP, 1, 1) == 0);
	CHECK(network_rules_unit_part_bytes(&layout, 0x3F, DELTA_LAYOUT_PART_LAST, 0, 0) == 0);
	/* (the deck after the up, alive and placed; the pose last, alive) */
	CHECK(network_rules_unit_part_bytes(&layout, 0xC0, DELTA_LAYOUT_PART_AFTER_UP, 1, 1) == 28);
	CHECK(network_rules_unit_part_bytes(&layout, 0xC0, DELTA_LAYOUT_PART_LAST, 1, 1) == 30);
	CHECK(network_rules_unit_part_bytes(&layout, 0x40, DELTA_LAYOUT_PART_AFTER_UP, 1, 0) == 0);
	CHECK(network_rules_unit_part_bytes(&layout, 0x40, DELTA_LAYOUT_PART_LAST, 1, 0) == 30);
	/* (unreadable: the deck not placed or dead, the pose dead) */
	CHECK(network_rules_unit_part_bytes(&layout, 0x80, DELTA_LAYOUT_PART_AFTER_UP, 1, 0) == -1);
	CHECK(network_rules_unit_part_bytes(&layout, 0x80, DELTA_LAYOUT_PART_AFTER_UP, 0, 1) == -1);
	CHECK(network_rules_unit_part_bytes(&layout, 0x40, DELTA_LAYOUT_PART_LAST, 0, 0) == -1);
	/* (the bound on one state: the old most, 28 and 30 more) */
	CHECK(layout.unit_part_size[6] + layout.unit_part_size[7] == 58);
	/* (a layout without them: a bit not built in is unreadable) */
	network_rules_layout(41, &layout);
	CHECK(network_rules_unit_part_bytes(&layout, 0x80, DELTA_LAYOUT_PART_AFTER_UP, 1, 1) == -1);
	CHECK(network_rules_unit_part_bytes(&layout, 0x01, DELTA_LAYOUT_PART_AFTER_UP, 1, 1) == 0);
	network_rules_set_layout_source(NULL);
}

/* a host announcing 43 sends its own machine the record at the game's start
and again at each machine's join; its own machine (joined at home, told no
version, noting "at least 35" when the newer options come) takes every one,
with 0, 1 and 2 remote machines joining */
static void test_host_own_machine_43(void)
{
	int joins;

	test_fact_count = 0;
	test_fact_43(test_fact(43));
	network_rules_set_layout_source(test_source);
	for (joins = 0; joins <= 2; joins++)
	{
		unsigned joined = NETWORK_RULES_VERSION_UNKNOWN;
		int send;

		/* (the first send, then one at each remote machine's join) */
		for (send = 0; send <= joins; send++)
		{
			int size = network_rules_host_settings_size(43, 0, 0);
			unsigned version = network_rules_client_settings_version(joined, 1, 43);
			int options = network_rules_settings_options_size(version, (unsigned)size);

			CHECK(size == 13200 && network_rules_settings_size_taken(version, (unsigned)size));
			CHECK(options == 0x2C);
			if (options > NETWORK_RULES_OPTIONS_SIZE)
				joined = network_rules_client_noted_version(joined, 1, NETWORK_RULES_EXTENDED_OPTIONS_VERSION);
			CHECK(joined == NETWORK_RULES_VERSION_UNKNOWN);
		}
	}
	/* (a remote client of a host not known: noted at least 35, as before) */
	CHECK(network_rules_client_noted_version(NETWORK_RULES_VERSION_UNKNOWN, 0, 35) == 35);
	CHECK(network_rules_client_noted_version(43, 0, 35) == 43);
	CHECK(network_rules_client_settings_version(43, 0, 41) == 43);
	/* (a host announcing 41: its own machine takes 41's record, as before) */
	CHECK(network_rules_settings_size_taken(network_rules_client_settings_version(35, 1, 41),
		(unsigned)network_rules_host_settings_size(41, 0, 0)));
	network_rules_set_layout_source(NULL);
}

/* a co-op client present across two reverts of a 43 host: at each revert
the host's epoch moves on and the boundary goes to the client; the client
takes it (newer), asks again with its ready, and the host's next ready
answer is the full snapshot, not "objects synchronized" alone; a second
ready at the same epoch is not; a client joining after a revert takes it
once */
/* a client's ready as the wire has it (stamped epoch), and what the host
makes of it: stripped, and taken */
static int test_host_takes_ready(uint32_t stamped, uint32_t host)
{
	uint8_t ready[8] = { 0, 0, NETWORK_RULES_CLIENT_READY_KIND, 0, 1, 0, 0, 0 };
	uint8_t wide[16], narrow[16];
	uint32_t epoch = 0;
	int size;

	test_header_word(ready, 8);
	size = network_rules_header_expand(ready, 8, wide, sizeof(wide), 4, 1, stamped);
	size = network_rules_header_strip(wide, size, narrow, 4, 1, host, NETWORK_RULES_CLIENT_READY_KIND, &epoch);
	return size == 8 && epoch == stamped && network_rules_host_takes(epoch, host, narrow[2], size, narrow[3], 1, 0);
}

static void test_client_across_reverts(void)
{
	uint32_t host = 0, client = 0, sent = 0;
	int never = 1, revert;

	host = network_rules_epoch_next(host);
	/* (its ready at the map's start: epoch 0 accepted, the boundary, then
	the snapshot; no revert's boundary before its ready) */
	CHECK(!network_rules_revert_boundary_to(1, 0, 0));
	CHECK(test_host_takes_ready(client, host));
	CHECK(network_rules_boundary_taken(8, 0, 1, 0, host, client));
	client = host;
	CHECK(network_rules_snapshot_due(never, 0, 0, sent, host));
	never = 0;
	sent = host;
	CHECK(!network_rules_snapshot_due(never, 0, 0, sent, host));
	for (revert = 0; revert < 2; revert++)
	{
		uint32_t late_sent = 0;

		/* (the revert: a new epoch, its boundary to the machine that said
		its ready, never to the host's own) */
		host = network_rules_epoch_next(host);
		CHECK(network_rules_revert_boundary_to(1, 0, 1) && !network_rules_revert_boundary_to(1, 1, 1));
		/* (meanwhile its other messages, of the old epoch, not taken) */
		CHECK(!network_rules_host_takes(client, host, 1, 20, 1, 1, 0));
		/* (a ready of an old epoch in a batch or a datagram: not taken) */
		CHECK(!network_rules_host_takes(client, host, NETWORK_RULES_CLIENT_READY_KIND, 8, 0, 1, 1));
		CHECK(!network_rules_host_takes(client, host, NETWORK_RULES_CLIENT_READY_KIND, 8, 0, 0, 0));
		/* (until its snapshot ends: its ready alone sent, and the objects'
		changes, their end and stream inventories alone taken) */
		CHECK(!network_rules_client_held_back(1, 1, 0, NETWORK_RULES_CLIENT_READY_KIND));
		CHECK(network_rules_client_held_back(1, 1, 0, 1) && network_rules_client_held_back(1, 1, 0, 18));
		CHECK(!network_rules_client_held_back(1, 1, 1, 1) && !network_rules_client_held_back(0, 1, 0, 1) &&
			!network_rules_client_held_back(1, 0, 0, 1));
		CHECK(network_rules_client_takes_unsynchronized(NETWORK_RULES_OBJECT_CHANGES_KIND, 0, 1));
		CHECK(network_rules_client_takes_unsynchronized(NETWORK_RULES_OBJECTS_SYNCHRONIZED_KIND, 1, 0));
		CHECK(network_rules_client_takes_unsynchronized(NETWORK_RULES_INVENTORIES_KIND, 1, 0));
		CHECK(!network_rules_client_takes_unsynchronized(NETWORK_RULES_INVENTORIES_KIND, 1, 1) &&
			!network_rules_client_takes_unsynchronized(NETWORK_RULES_INVENTORIES_KIND, 0, 0));
		CHECK(!network_rules_client_takes_unsynchronized(2, 1, 0) && !network_rules_client_takes_unsynchronized(88, 1, 0));
		CHECK(network_rules_boundary_taken(8, 0, 1, 0, host, client));
		client = host;
		/* (5.2: its ready again, of the new epoch, over its stream; taken,
		as one of an old epoch is too) */
		CHECK(test_host_takes_ready(client, host) && test_host_takes_ready(client - 1, host));
		CHECK(network_rules_host_takes(client, host, 1, 20, 1, 0, 1));
		/* (the boundary again at its ready: the same epoch, not taken) */
		CHECK(!network_rules_boundary_taken(8, 0, 1, 0, host, client));
		CHECK(network_rules_snapshot_due(never, 0, 0, sent, host));
		sent = host;
		CHECK(!network_rules_snapshot_due(never, 0, 0, sent, host));
		/* (a machine that joins right after the revert: its first) */
		CHECK(network_rules_snapshot_due(1, 0, 0, late_sent, host));
	}
	CHECK(host == 3 && client == 3);
}

/* a reliable message the header's 4 further bytes make too long: split on
its entries' boundaries when they are all one size, else not */
static void test_reliable_split(void)
{
	/* (4,092 bytes: 8 and 4 entries of 1,021; 4,096 with the 4) */
	CHECK(network_rules_reliable_split(4092, 4, 1021, 4, 0xFFF) == 2);
	/* (4,095: 61 entries of 67) */
	CHECK(network_rules_reliable_split(4095, 61, 67, 4, 0xFFF) == 30);
	/* (68 of 60, 4,088 bytes: fits with the 4; no further bytes: fits) */
	CHECK(network_rules_reliable_split(4088, 68, 60, 4, 0xFFF) == 0 && network_rules_reliable_split(4092, 4, 1021, 0, 0xFFF) == 0);
	/* (a prefix or entries of their own sizes: not split) */
	CHECK(network_rules_reliable_split(4092, 4, 0, 4, 0xFFF) == -1);
	CHECK(network_rules_reliable_split(4092, 4, 1020, 4, 0xFFF) == -1);
	CHECK(network_rules_reliable_split(4092, 1, 4084, 4, 0xFFF) == -1);
	CHECK(network_rules_reliable_split(4092, 0, 60, 4, 0xFFF) == -1);
	/* (two halves too long still: not split) */
	CHECK(network_rules_reliable_split(4095, 2, 4087 / 2, 4, 0xFFF) == -1);
}

/* a unit state's length: the built-in parts, and 43's deck and pose */
static void test_unit_state_length(void)
{
	struct network_rules_layout layout;
	uint8_t record[160];

	test_fact_count = 0;
	test_fact_43(test_fact(43));
	network_rules_set_layout_source(test_source);
	network_rules_layout(43, &layout);
	memset(record, 0, sizeof(record));
	/* (dead, a killer; dead, none) */
	record[1] = 0;
	record[2] = 32;
	CHECK(network_rules_unit_state_length(&layout, record, 160, 2) == 4);
	record[2] = 0;
	CHECK(network_rules_unit_state_length(&layout, record, 160, 2) == 3);
	/* (alive, placed, predicted, every built-in part: 63) */
	record[1] = 0x83;
	record[2] = 0x1F;
	CHECK(network_rules_unit_state_length(&layout, record, 160, 2) == 63);
	/* (and the deck and the pose: 63 + 28 + 30) */
	record[2] = 0xDF;
	CHECK(network_rules_unit_state_length(&layout, record, 160, 2) == 121);
	CHECK(network_rules_unit_state_length(&layout, record, 120, 2) == 0);
	/* (the deck not placed, the pose dead: unreadable) */
	record[1] = 0x01;
	record[2] = 0x80;
	CHECK(network_rules_unit_state_length(&layout, record, 160, 2) == 0);
	record[1] = 0;
	record[2] = 0x40;
	CHECK(network_rules_unit_state_length(&layout, record, 160, 2) == 0);
	/* (41: bits 6 and 7 unreadable) */
	network_rules_layout(41, &layout);
	record[1] = 0x03;
	record[2] = 0x80;
	CHECK(network_rules_unit_state_length(&layout, record, 160, 2) == 0);
	CHECK(network_rules_unit_state_length(&layout, record, 2, 2) == 0);
	/* (a later fact shrinking the header below an epoch's 4: refused) */
	{
		struct delta_layout_fact *fact = test_fact(44);

		fact->header_extra = 2;
		network_rules_layout(44, &layout);
		CHECK(layout.header_extra == 4 && layout.epoch_kind == 96 && layout.refused == 1);
	}
	network_rules_set_layout_source(NULL);
}

/* the join paths: a host joined by an advertisement (the LAN, the list, an
invite through its tunnel, all of which carry the host's number) is sent
the word at once; one joined by an address alone (no advertisement) is sent
the plain request, and once more with the newest word after a silence */
static void test_join_paths(void)
{
	uint8_t tail[NETWORK_RULES_JOIN_REQUEST_EXTRA_LIMIT];

	test_fact_count = 0;
	test_fact_43(test_fact(43));
	network_rules_set_layout_source(test_source);
	/* (an invite's host, advertised at 43 through the tunnel: the word) */
	CHECK(network_rules_join_request_tail(43, tail, sizeof(tail)) == 236 && tail[234] == 0 && tail[235] == 43);
	CHECK(!network_rules_join_request_refused(43, tail, 236));
	CHECK(network_rules_join_retry_version(43, 0, 60000) == 0);
	/* (an advertised 41 host: no word, never a retry) */
	CHECK(network_rules_join_request_tail(41, tail, sizeof(tail)) == 234);
	CHECK(network_rules_join_retry_version(41, 0, 60000) == 0);
	/* (no advertisement: the plain request, then once 43's after 3 s) */
	CHECK(network_rules_join_request_tail(NETWORK_RULES_VERSION_UNKNOWN, tail, sizeof(tail)) == 0);
	CHECK(network_rules_join_request_refused(43, tail, 0));
	CHECK(network_rules_join_retry_version(NETWORK_RULES_VERSION_UNKNOWN, 0, 2999) == 0);
	CHECK(network_rules_join_retry_version(NETWORK_RULES_VERSION_UNKNOWN, 0, 3000) == 43);
	CHECK(network_rules_join_request_tail(43, tail, sizeof(tail)) == 236 && !network_rules_join_request_refused(43, tail, 236));
	/* (the host's own machine: never; no table: never) */
	CHECK(network_rules_join_retry_version(NETWORK_RULES_VERSION_UNKNOWN, 1, 60000) == 0);
	/* (no table: 43 built in) */
	network_rules_set_layout_source(NULL);
	CHECK(network_rules_join_retry_version(NETWORK_RULES_VERSION_UNKNOWN, 0, 60000) == 43);
}

int main(void)
{
	test_aim_assist_message();
	test_input_entry();
	test_stamp_window();
	test_report_stamp();
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
	test_layouts_join_request();
	test_layout_record();
	test_layouts_43();
	test_layouts_43_hostile();
	test_header_extension();
	test_unit_parts();
	test_host_own_machine_43();
	test_client_across_reverts();
	test_reliable_split();
	test_unit_state_length();
	test_join_paths();
	if (failures)
	{
		printf("network_rules_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("network_rules_test: ok\n");
	return 0;
}
