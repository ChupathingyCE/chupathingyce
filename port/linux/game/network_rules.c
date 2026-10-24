/*
NETWORK_RULES.C

The rules of network_rules.h.
*/

#include "network_rules.h"
/* (the table's facts and DELTA_LEGACY_NEWEST: numbers alone, no calls) */
#include "../include/delta.h"

#include <string.h>

typedef char network_rules_layout_kinds_fit[sizeof(((struct network_rules_layout *)0)->dropped) ==
	DELTA_LAYOUT_KIND_BYTES ? 1 : -1];
typedef char network_rules_layout_head_fits[NETWORK_RULES_SETTINGS_HEAD_SIZE == 0x3320 ? 1 : -1];

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
	struct network_rules_layout layout;

	network_rules_layout(version, &layout);
	return layout.settings_size;
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
	network_rules_settings_fold(record, NETWORK_RULES_SETTINGS_EXTENDED_SIZE, NETWORK_RULES_OPTIONS_EXTENDED_SIZE,
		extended);
}

void network_rules_settings_fold(uint8_t *record, int total_size, int options_size, uint8_t *extended)
{
	/* (the older record's last 4 bytes are its local data, after the
	options' first 0x1C: there, in the newer, the further bytes begin) */
	uint8_t *further = record + NETWORK_RULES_SETTINGS_SIZE - 4;
	int growth = options_size - NETWORK_RULES_OPTIONS_SIZE;
	uint8_t local[4];
	int index;

	if (growth < NETWORK_RULES_EXTENDED_OPTIONS_SIZE ||
		total_size < NETWORK_RULES_SETTINGS_SIZE + growth || total_size > NETWORK_RULES_SETTINGS_MAXIMUM)
	{
		return;
	}
	for (index = 0; index < 4; index++)
		local[index] = further[growth + index];
	for (index = 0; index < NETWORK_RULES_EXTENDED_OPTIONS_SIZE; index++)
		extended[index] = further[index];
	for (index = 0; index < 4; index++)
		further[index] = local[index];
	for (index = NETWORK_RULES_SETTINGS_SIZE; index < total_size; index++)
		record[index] = 0;
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

char const *network_rules_kind_name(int kind, int engine, int short_form)
{
	static char const *const modes[][2] = {
		{ "SWAT", "HC SWAT" }, { "INFECTION", "HC INFECTION" }, { "GUN GAME", "HC GUN GAME" } };
	static char const *const modes_full[] = { "HARDCORE SWAT", "HARDCORE INFECTION", "HARDCORE GUN GAME" };
	static char const *const engines[][2] = {
		{ "HARDCORE CTF", "HC CTF" }, { "HARDCORE SLAYER", "HC SLAYER" }, { "HARDCORE ODDBALL", "HC ODDBALL" },
		{ "HARDCORE KING", "HC KING" }, { "HARDCORE RACE", "HC RACE" } };
	int mode = kind & 0x7F;
	int hardcore = (kind & 0x80) != 0;

	short_form = short_form != 0;
	if (engine == 2 && mode >= NETWORK_RULES_MODE_SWAT && mode < NETWORK_RULES_NUMBER_OF_MODES)
		return hardcore ? (short_form ? modes[mode - 1][1] : modes_full[mode - 1]) : modes[mode - 1][0];
	if (hardcore && engine >= 1 && engine <= 5)
		return engines[engine - 1][short_form];
	return NULL;
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

int network_rules_shield_effects_read(uint8_t const *message, int size, int from_datagram, int maximum,
	int *effects)
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
	if (maximum > NETWORK_RULES_SHIELD_EFFECTS_LIMIT)
		maximum = NETWORK_RULES_SHIELD_EFFECTS_LIMIT;
	if (get16(message + 8) > (unsigned)maximum || get16(message + 10) != 0)
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
	return waiting ? 0 : network_rules_settings_size(announce);
}

int network_rules_host_settings_options(unsigned announce, unsigned delta_version)
{
	struct network_rules_layout layout;

	if (announce < NETWORK_RULES_EXTENDED_OPTIONS_VERSION)
		return NETWORK_RULES_OPTIONS_SIZE;
	network_rules_layout(delta_version && delta_version < announce ? delta_version : announce, &layout);
	return layout.options_size;
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
	network_rules_settings_layout_copy(record, extended, NETWORK_RULES_OPTIONS_EXTENDED_SIZE, offset, length, out);
}

void network_rules_settings_layout_copy(uint8_t const *record, uint8_t const *extended, int options_size,
	int offset, int length, uint8_t *out)
{
	/* (where the further options go: in front of the older record's local
	data, its last 4 bytes) */
	int further = NETWORK_RULES_SETTINGS_SIZE - 4;
	int growth = options_size - NETWORK_RULES_OPTIONS_SIZE;
	int known = growth < NETWORK_RULES_EXTENDED_OPTIONS_SIZE ? (growth > 0 ? growth : 0) :
		NETWORK_RULES_EXTENDED_OPTIONS_SIZE;
	int index;

	if (growth < 0)
		growth = 0;
	for (index = 0; index < length; index++)
	{
		int at = offset + index;

		if (at < further)
			out[index] = record[at];
		else if (at < further + known)
			out[index] = extended[at - further];
		else if (at < further + growth)
			out[index] = 0;
		else if (at < further + growth + 4)
			out[index] = record[at - growth];
		else
			out[index] = 0;
	}
}

/* ---------- layouts */

static int (*network_rules_layout_source)(struct delta_layout_fact *facts, int count);

static void layout_drop(struct network_rules_layout *layout, unsigned kind)
{
	if (kind > 0 && kind < 8 * sizeof(layout->dropped))
		layout->dropped[kind >> 3] |= (uint8_t)(1 << (kind & 7));
}

void network_rules_layout_built_in(unsigned version, struct network_rules_layout *layout)
{
	unsigned rules = version > DELTA_LEGACY_NEWEST ? DELTA_LEGACY_NEWEST : version;

	memset(layout, 0, sizeof(*layout));
	layout->version = version;
	layout->shield_effects_maximum = NETWORK_RULES_SHIELD_EFFECTS_MAXIMUM;
	if (version == NETWORK_RULES_VERSION_UNKNOWN)
	{
		/* (either record; 36's message) */
		layout_drop(layout, NETWORK_RULES_SHIELD_EFFECTS_KIND);
		return;
	}
	if (rules >= NETWORK_RULES_EXTENDED_OPTIONS_VERSION)
	{
		layout->settings_size = NETWORK_RULES_SETTINGS_EXTENDED_SIZE;
		layout->options_size = NETWORK_RULES_OPTIONS_EXTENDED_SIZE;
	}
	else
	{
		layout->settings_size = NETWORK_RULES_SETTINGS_SIZE;
		layout->options_size = NETWORK_RULES_OPTIONS_SIZE;
	}
	if (rules >= NETWORK_RULES_SHIELD_EFFECTS_VERSION)
		layout_drop(layout, NETWORK_RULES_SHIELD_EFFECTS_KIND);
}

void network_rules_layout_apply(struct network_rules_layout *layout, struct delta_layout_fact const *facts,
	int count)
{
	int index;

	if (layout->version == NETWORK_RULES_VERSION_UNKNOWN || layout->version <= DELTA_LEGACY_NEWEST || !facts)
		return;
	for (index = 0; index < count; index++)
	{
		struct delta_layout_fact const *fact = &facts[index];
		int byte;

		if (fact->version <= DELTA_LEGACY_NEWEST || fact->version > layout->version)
			continue;
		if (fact->options_size)
		{
			/* (the options grow in front of the local data, the record with
			them) */
			int growth = (int)fact->options_size - layout->options_size;
			int settings = layout->settings_size + growth;

			if (growth >= 0 && settings <= NETWORK_RULES_SETTINGS_MAXIMUM)
			{
				layout->options_size = fact->options_size;
				layout->settings_size = settings;
				layout->learned++;
			}
			else
				layout->refused++;
		}
		if (fact->settings_size)
		{
			/* (the bytes after the local data, at least as many as before) */
			if ((int)fact->settings_size >= layout->settings_size &&
				fact->settings_size <= NETWORK_RULES_SETTINGS_MAXIMUM)
			{
				layout->settings_size = fact->settings_size;
				layout->learned++;
			}
			else
				layout->refused++;
		}
		if (fact->shield_effects_maximum)
		{
			if ((int)fact->shield_effects_maximum >= layout->shield_effects_maximum &&
				fact->shield_effects_maximum <= NETWORK_RULES_SHIELD_EFFECTS_LIMIT)
			{
				layout->shield_effects_maximum = fact->shield_effects_maximum;
				layout->learned++;
			}
			else
				layout->refused++;
		}
		for (byte = 0; byte < DELTA_LAYOUT_KIND_BYTES; byte++)
		{
			/* (kind 0 is none) */
			uint8_t bits = (uint8_t)(fact->dropped[byte] & (byte ? 0xFF : 0xFE));

			if (bits & ~layout->dropped[byte])
				layout->learned++;
			layout->dropped[byte] |= bits;
		}
	}
}

void network_rules_set_layout_source(int (*source)(struct delta_layout_fact *facts, int count))
{
	network_rules_layout_source = source;
}

void network_rules_layout(unsigned version, struct network_rules_layout *layout)
{
	network_rules_layout_built_in(version, layout);
	/* (the table asked only for a version it may extend) */
	if (network_rules_layout_source && version != NETWORK_RULES_VERSION_UNKNOWN && version > DELTA_LEGACY_NEWEST)
	{
		struct delta_layout_fact facts[DELTA_LAYOUT_FACTS];
		int count = network_rules_layout_source(facts, DELTA_LAYOUT_FACTS);

		network_rules_layout_apply(layout, facts, count < 0 ? 0 : count > DELTA_LAYOUT_FACTS ? DELTA_LAYOUT_FACTS :
			count);
	}
}

unsigned network_rules_layout_newest(void)
{
	unsigned newest = DELTA_LEGACY_NEWEST;

	if (network_rules_layout_source)
	{
		struct delta_layout_fact facts[DELTA_LAYOUT_FACTS];
		int count = network_rules_layout_source(facts, DELTA_LAYOUT_FACTS);
		int index;

		for (index = 0; index < count && index < DELTA_LAYOUT_FACTS; index++)
		{
			if (facts[index].version > newest && facts[index].version != NETWORK_RULES_VERSION_UNKNOWN)
				newest = facts[index].version;
		}
	}
	return newest;
}

int network_rules_layout_drops(struct network_rules_layout const *layout, unsigned kind)
{
	return kind > 0 && kind < 8 * sizeof(layout->dropped) && (layout->dropped[kind >> 3] >> (kind & 7)) & 1;
}

int network_rules_settings_options_size(unsigned version, unsigned total_size)
{
	struct network_rules_layout layout;

	network_rules_layout(version, &layout);
	if (layout.settings_size && total_size == (unsigned)layout.settings_size)
		return layout.options_size;
	return total_size == NETWORK_RULES_SETTINGS_EXTENDED_SIZE ? NETWORK_RULES_OPTIONS_EXTENDED_SIZE :
		NETWORK_RULES_OPTIONS_SIZE;
}
