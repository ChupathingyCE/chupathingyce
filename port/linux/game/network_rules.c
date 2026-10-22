/*
NETWORK_RULES.C

The rules of network_rules.h.
*/

#include "network_rules.h"

int network_rules_floor_contact(int airborne, int has_support_surface, int elevator_ticks)
{
	return !airborne && has_support_surface && elevator_ticks == NETWORK_RULES_ELEVATOR_CONTACT_TICKS;
}

int network_rules_riding_elevator(int elevator_is_machine, int elevator_moving, int floor_contact)
{
	return elevator_is_machine && (elevator_moving || floor_contact);
}

int network_rules_elevator_correction(int32_t recorded_elevator, int32_t current_elevator, float *error)
{
	float across_squared = error[0] * error[0] + error[1] * error[1];
	const float tolerance = NETWORK_RULES_LOCAL_CORRECTION_TOLERANCE;

	if (recorded_elevator != -1 && recorded_elevator == current_elevator &&
		across_squared <= tolerance * tolerance &&
		error[2] >= -NETWORK_RULES_ELEVATOR_PHASE_TOLERANCE && error[2] <= NETWORK_RULES_ELEVATOR_PHASE_TOLERANCE)
	{
		error[2] = 0.0f;
	}
	return error[0] * error[0] + error[1] * error[1] + error[2] * error[2] > tolerance * tolerance;
}

int network_rules_stamp_window(int32_t *latest, int32_t stamp, int from_stream, uint32_t window)
{
	uint32_t distance;

	if (*latest == -1)
	{
		*latest = stamp;
		return 1;
	}
	distance = stamp > *latest ? (uint32_t)stamp - (uint32_t)*latest : (uint32_t)*latest - (uint32_t)stamp;
	if (distance > window)
	{
		if (!from_stream)
			return 0;
		*latest = stamp;
		return 2;
	}
	if (stamp > *latest)
		*latest = stamp;
	return 1;
}

static void put16(uint8_t *at, unsigned value)
{
	at[0] = (uint8_t)(value & 0xFF);
	at[1] = (uint8_t)((value >> 8) & 0xFF);
}

static void put32(uint8_t *at, uint32_t value)
{
	put16(at, value & 0xFFFF);
	put16(at + 2, value >> 16);
}

static unsigned get16(uint8_t const *at)
{
	return (unsigned)at[0] | ((unsigned)at[1] << 8);
}

static uint32_t get32(uint8_t const *at)
{
	return (uint32_t)get16(at) | ((uint32_t)get16(at + 2) << 16);
}

void network_rules_aim_assist_write(uint8_t *message, int block, int32_t game_time)
{
	put16(message, (NETWORK_RULES_AIM_ASSIST_SIZE << 4) | (NETWORK_RULES_MESSAGE_TYPE_DATA << 2));
	message[2] = NETWORK_RULES_AIM_ASSIST_KIND;
	message[3] = 1;
	put32(message + 4, (uint32_t)game_time);
	put16(message + 8, block ? 1 : 0);
	put16(message + 10, 0);
}

int network_rules_aim_assist_read(uint8_t const *message, int size, int from_datagram, int *block)
{
	unsigned header;

	if (from_datagram || size != NETWORK_RULES_AIM_ASSIST_SIZE)
		return 0;
	header = get16(message);
	if ((header >> 4) != NETWORK_RULES_AIM_ASSIST_SIZE || ((header >> 2) & 3) != NETWORK_RULES_MESSAGE_TYPE_DATA ||
		message[2] != NETWORK_RULES_AIM_ASSIST_KIND || message[3] != 1)
	{
		return 0;
	}
	if (get16(message + 8) > 1 || get16(message + 10) != 0)
		return 0;
	*block = (int)get16(message + 8);
	return 1;
}

void network_rules_input_head_write(uint8_t *head, int player, int structure_bsp, int no_friendly_fire, int32_t tick,
	int32_t host_time)
{
	head[0] = (uint8_t)player;
	head[1] = (uint8_t)structure_bsp;
	head[2] = no_friendly_fire ? 1 : 0;
	head[3] = 0;
	put32(head + 4, (uint32_t)tick);
	put32(head + 8, (uint32_t)host_time);
}

void network_rules_input_head_read(uint8_t const *head, int *player, int *structure_bsp, int *no_friendly_fire,
	int32_t *tick, int32_t *host_time)
{
	*player = head[0];
	*structure_bsp = head[1];
	*no_friendly_fire = head[2] != 0;
	*tick = (int32_t)get32(head + 4);
	*host_time = (int32_t)get32(head + 8);
}

int network_rules_mouse_is_aim_device(uint64_t mouse_ms, uint64_t stick_ms, uint64_t touch_ms)
{
	int mouse = stick_ms == 0 || mouse_ms >= stick_ms;

	if (touch_ms != 0 && touch_ms >= mouse_ms && touch_ms >= stick_ms)
		mouse = 0;
	return mouse;
}

int network_rules_aim_assist_blocks(int game_in_progress, int main_menu_active, int is_host, int is_client,
	int host_setting, int client_policy_blocked)
{
	if (!game_in_progress || main_menu_active)
		return 0;
	if (is_host)
		return host_setting != 0;
	if (is_client)
		return client_policy_blocked != 0;
	return 0;
}
