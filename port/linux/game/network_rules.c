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

int network_rules_settings_size(unsigned version)
{
	if (version == NETWORK_RULES_VERSION_UNKNOWN)
		return 0;
	return version >= NETWORK_RULES_EXTENDED_OPTIONS_VERSION ? NETWORK_RULES_SETTINGS_EXTENDED_SIZE :
		NETWORK_RULES_SETTINGS_SIZE;
}

int network_rules_settings_size_taken(unsigned version, unsigned total_size)
{
	int size = network_rules_settings_size(version);

	if (size)
		return total_size == (unsigned)size;
	return total_size == NETWORK_RULES_SETTINGS_SIZE || total_size == NETWORK_RULES_SETTINGS_EXTENDED_SIZE;
}

void network_rules_settings_fold_options(uint8_t *record, uint8_t *extended)
{
	/* (the older record's last 4 bytes are its local data, after the
	options' first 0x1C: there, in the newer, the further 16 begin) */
	uint8_t *further = record + NETWORK_RULES_SETTINGS_SIZE - 4;
	uint8_t local[4];
	int index;

	for (index = 0; index < 4; index++)
		local[index] = further[NETWORK_RULES_EXTENDED_OPTIONS_SIZE + index];
	for (index = 0; index < NETWORK_RULES_EXTENDED_OPTIONS_SIZE; index++)
		extended[index] = further[index];
	for (index = 0; index < 4; index++)
		further[index] = local[index];
	for (index = 0; index < NETWORK_RULES_EXTENDED_OPTIONS_SIZE; index++)
		record[NETWORK_RULES_SETTINGS_SIZE + index] = 0;
}

/* a ranged byte: its default outside the range */
static uint8_t ranged(uint8_t value, uint8_t least, uint8_t most, uint8_t otherwise)
{
	return value < least || value > most ? otherwise : value;
}

void network_rules_extended_options_default(uint8_t *options)
{
	int index;

	for (index = 0; index < NETWORK_RULES_EXTENDED_OPTIONS_SIZE; index++)
		options[index] = 0;
	options[NETWORK_RULES_OPTION_FIRST_ZOMBIES] = 1;
	options[NETWORK_RULES_OPTION_ZOMBIE_SPEED] = 150;
	options[NETWORK_RULES_OPTION_SKULL_DAMAGE] = 200;
	options[NETWORK_RULES_OPTION_ZOMBIE_RESPAWN] = 5;
	options[NETWORK_RULES_OPTION_COUNTDOWN] = 10;
	options[NETWORK_RULES_OPTION_ZOMBIE_SHIELDS] = 1;
	options[NETWORK_RULES_OPTION_MELEE_DEMOTES] = 1;
}

void network_rules_extended_options_sanitize(uint8_t *options)
{
	int index;

	if (options[NETWORK_RULES_OPTION_MODE] >= NETWORK_RULES_NUMBER_OF_MODES)
		options[NETWORK_RULES_OPTION_MODE] = NETWORK_RULES_MODE_NONE;
	options[NETWORK_RULES_OPTION_HARDCORE] = options[NETWORK_RULES_OPTION_HARDCORE] != 0;
	options[NETWORK_RULES_OPTION_FIRST_ZOMBIES] = ranged(options[NETWORK_RULES_OPTION_FIRST_ZOMBIES], 1, 16, 1);
	options[NETWORK_RULES_OPTION_ZOMBIE_SPEED] = ranged(options[NETWORK_RULES_OPTION_ZOMBIE_SPEED], 100, 200, 150);
	options[NETWORK_RULES_OPTION_SKULL_DAMAGE] = ranged(options[NETWORK_RULES_OPTION_SKULL_DAMAGE], 100, 250, 200);
	options[NETWORK_RULES_OPTION_ZOMBIE_RESPAWN] = ranged(options[NETWORK_RULES_OPTION_ZOMBIE_RESPAWN], 1, 30, 5);
	options[NETWORK_RULES_OPTION_COUNTDOWN] = ranged(options[NETWORK_RULES_OPTION_COUNTDOWN], 0, 30, 10);
	options[NETWORK_RULES_OPTION_ZOMBIE_SHIELDS] = options[NETWORK_RULES_OPTION_ZOMBIE_SHIELDS] != 0;
	if (options[NETWORK_RULES_OPTION_WEAPON_ORDER] > 2)
		options[NETWORK_RULES_OPTION_WEAPON_ORDER] = 0;
	options[NETWORK_RULES_OPTION_MELEE_DEMOTES] = options[NETWORK_RULES_OPTION_MELEE_DEMOTES] != 0;
	for (index = NETWORK_RULES_OPTION_MELEE_DEMOTES + 1; index < NETWORK_RULES_EXTENDED_OPTIONS_SIZE; index++)
		options[index] = 0;
}

int network_rules_extended_options_mode(uint8_t const *options, int slayer, int teams, int *hardcore)
{
	int mode = options[NETWORK_RULES_OPTION_MODE];

	*hardcore = options[NETWORK_RULES_OPTION_HARDCORE] != 0;
	if (!slayer || (mode == NETWORK_RULES_MODE_INFECTION && !teams) || (mode == NETWORK_RULES_MODE_GUN_GAME && teams) ||
		mode >= NETWORK_RULES_NUMBER_OF_MODES)
	{
		return NETWORK_RULES_MODE_NONE;
	}
	return mode;
}

void network_rules_shield_effects_write(uint8_t *message, int effects, int32_t game_time)
{
	put16(message, (NETWORK_RULES_SHIELD_EFFECTS_SIZE << 4) | (NETWORK_RULES_MESSAGE_TYPE_DATA << 2));
	message[2] = NETWORK_RULES_SHIELD_EFFECTS_KIND;
	message[3] = 1;
	put32(message + 4, (uint32_t)game_time);
	put16(message + 8, effects >= 0 && effects <= NETWORK_RULES_SHIELD_EFFECTS_MAXIMUM ? (unsigned)effects : 0);
	put16(message + 10, 0);
}

int network_rules_shield_effects_read(uint8_t const *message, int size, int from_datagram, int *effects)
{
	unsigned header;

	if (from_datagram || size != NETWORK_RULES_SHIELD_EFFECTS_SIZE)
		return 0;
	header = get16(message);
	if ((header >> 4) != NETWORK_RULES_SHIELD_EFFECTS_SIZE || ((header >> 2) & 3) != NETWORK_RULES_MESSAGE_TYPE_DATA ||
		message[2] != NETWORK_RULES_SHIELD_EFFECTS_KIND || message[3] != 1)
	{
		return 0;
	}
	if (get16(message + 8) > NETWORK_RULES_SHIELD_EFFECTS_MAXIMUM || get16(message + 10) != 0)
		return 0;
	*effects = (int)get16(message + 8);
	return 1;
}

float network_rules_static_event_reach(int area, float cutoff_radius)
{
	float reach = NETWORK_RULES_STATIC_EVENT_REACH;

	if (area && cutoff_radius > 0.0f)
		reach += cutoff_radius;
	return reach;
}

int network_rules_static_within(float distance_squared, float radius, float reach)
{
	float limit = radius + reach;

	return limit >= 0.0f && distance_squared <= limit * limit;
}

int network_rules_host_settings_size(unsigned announce, unsigned delta_version, int waiting)
{
	if (announce < NETWORK_RULES_EXTENDED_OPTIONS_VERSION)
		return NETWORK_RULES_SETTINGS_SIZE;
	if (delta_version)
		return network_rules_settings_size(delta_version < announce ? delta_version : announce);
	return waiting ? 0 : NETWORK_RULES_SETTINGS_EXTENDED_SIZE;
}

int network_rules_host_static_damage_reported(unsigned announce, unsigned delta_version)
{
	if (delta_version)
		return !(delta_version >= NETWORK_RULES_EXTENDED_OPTIONS_VERSION &&
			announce >= NETWORK_RULES_EXTENDED_OPTIONS_VERSION);
	return announce < NETWORK_RULES_STATIC_SHIELDS_VERSION;
}

void network_rules_settings_extended_copy(uint8_t const *record, uint8_t const *extended, int offset, int length,
	uint8_t *out)
{
	/* (where the further options go: in front of the older record's local
	data, its last 4 bytes) */
	int further = NETWORK_RULES_SETTINGS_SIZE - 4;
	int index;

	for (index = 0; index < length; index++)
	{
		int at = offset + index;

		if (at < further)
			out[index] = record[at];
		else if (at < further + NETWORK_RULES_EXTENDED_OPTIONS_SIZE)
			out[index] = extended[at - further];
		else
			out[index] = record[at - NETWORK_RULES_EXTENDED_OPTIONS_SIZE];
	}
}
