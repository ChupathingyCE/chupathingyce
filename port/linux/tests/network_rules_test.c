/*
NETWORK_RULES_TEST.C

Tests of the rules of network versions 30 to 32 (port/linux/game/
network_rules.c): the bytes of the aim assist policy message and of a
player's input entry, the window of a datagram's stamp, what a client does
with the host's word on its player on an elevator, who rides one, and the
policy's reach. The expected values are those of the network's documented
behavior. tools/test_network_rules.py builds and runs them.
*/

#include "../game/network_rules.h"

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

int main(void)
{
	test_aim_assist_message();
	test_input_entry();
	test_stamp_window();
	test_elevator_correction();
	test_riding();
	test_aim_assist_policy();
	test_aim_device();
	if (failures)
	{
		printf("network_rules_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("network_rules_test: ok\n");
	return 0;
}
