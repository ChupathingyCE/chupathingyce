/*
NETWORK_RULES.C

The rules of network_rules.h.
*/

#include "network_rules.h"
/* (the table's facts and DELTA_LEGACY_NEWEST: numbers alone, no calls) */
#include "../include/delta.h"

#include <string.h>
#include "content_bytes.h"

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

int network_rules_report_stamp_taken(int32_t now, int32_t stamp, int32_t maximum_age)
{
	if (stamp == -1 || stamp > now)
		return 0;
	return (int64_t)now - (int64_t)stamp <= (int64_t)maximum_age;
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
	header = content_u16le(message);
	if ((header >> 4) != NETWORK_RULES_AIM_ASSIST_SIZE || ((header >> 2) & 3) != NETWORK_RULES_MESSAGE_TYPE_DATA ||
		message[2] != NETWORK_RULES_AIM_ASSIST_KIND || message[3] != 1)
	{
		return 0;
	}
	if (content_u16le(message + 8) > 1 || content_u16le(message + 10) != 0)
		return 0;
	*block = (int)content_u16le(message + 8);
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
	*tick = (int32_t)content_u32le(head + 4);
	*host_time = (int32_t)content_u32le(head + 8);
}

int network_rules_mouse_is_aim_device(uint64_t mouse_ms, uint64_t stick_ms, uint64_t touch_ms,
	int gamepad_connected)
{
	int mouse = stick_ms == 0 || mouse_ms >= stick_ms;

	if (mouse_ms == 0 && stick_ms == 0 && touch_ms == 0)
		return !gamepad_connected;

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

unsigned network_rules_modes_needed(uint8_t const *options, int engine, int teams)
{
	int hardcore = 0;
	int mode;
	unsigned needed = 0;

	if (engine == NETWORK_RULES_ENGINE_BOMB)
		return 0;
	mode = network_rules_extended_options_mode(options, engine == NETWORK_RULES_ENGINE_SLAYER, teams, &hardcore);
	if (mode == NETWORK_RULES_MODE_SWAT)
		needed |= NETWORK_RULES_MODES_SWAT;
	else if (mode == NETWORK_RULES_MODE_INFECTION)
		needed |= NETWORK_RULES_MODES_INFECTION;
	else if (mode == NETWORK_RULES_MODE_GUN_GAME)
		needed |= NETWORK_RULES_MODES_GUN_GAME;
	if (hardcore)
		needed |= NETWORK_RULES_MODES_HARDCORE;
	return needed;
}

int network_rules_modes_played(unsigned played, unsigned needed)
{
	return (needed & ~played) == 0;
}

int network_rules_modes_mode(unsigned needed, int *hardcore)
{
	*hardcore = (needed & NETWORK_RULES_MODES_HARDCORE) != 0;
	if (needed & NETWORK_RULES_MODES_SWAT)
		return NETWORK_RULES_MODE_SWAT;
	if (needed & NETWORK_RULES_MODES_INFECTION)
		return NETWORK_RULES_MODE_INFECTION;
	if (needed & NETWORK_RULES_MODES_GUN_GAME)
		return NETWORK_RULES_MODE_GUN_GAME;
	return NETWORK_RULES_MODE_NONE;
}

void network_rules_mode_options_host(uint8_t *options)
{
	network_rules_extended_options_sanitize(options);
	if ((options[NETWORK_RULES_OPTION_MODE] == NETWORK_RULES_MODE_SWAT &&
			!(NETWORK_RULES_MODES_PLAYED & NETWORK_RULES_MODES_SWAT)) ||
		(options[NETWORK_RULES_OPTION_MODE] == NETWORK_RULES_MODE_INFECTION &&
			!(NETWORK_RULES_MODES_PLAYED & NETWORK_RULES_MODES_INFECTION)) ||
		(options[NETWORK_RULES_OPTION_MODE] == NETWORK_RULES_MODE_GUN_GAME &&
			!(NETWORK_RULES_MODES_PLAYED & NETWORK_RULES_MODES_GUN_GAME)))
	{
		options[NETWORK_RULES_OPTION_MODE] = NETWORK_RULES_MODE_NONE;
	}
	if (!(NETWORK_RULES_MODES_PLAYED & NETWORK_RULES_MODES_HARDCORE))
		options[NETWORK_RULES_OPTION_HARDCORE] = 0;
}

void network_rules_mode_options_builtin(int mode, int hardcore, uint8_t *options)
{
	network_rules_extended_options_default(options);
	options[NETWORK_RULES_OPTION_MODE] = (uint8_t)mode;
	options[NETWORK_RULES_OPTION_HARDCORE] = hardcore != 0;
	network_rules_extended_options_sanitize(options);
}

int network_rules_kind_byte(int mode, int hardcore)
{
	if (mode < 0 || mode >= NETWORK_RULES_NUMBER_OF_MODES)
		mode = NETWORK_RULES_MODE_NONE;
	return (mode & 0x7F) | (hardcore ? 0x80 : 0);
}

unsigned network_rules_kind_needed(int kind, int engine)
{
	uint8_t options[NETWORK_RULES_EXTENDED_OPTIONS_SIZE];

	network_rules_extended_options_default(options);
	options[NETWORK_RULES_OPTION_MODE] = (uint8_t)(kind & 0x7F);
	options[NETWORK_RULES_OPTION_HARDCORE] = (kind & 0x80) != 0;
	network_rules_extended_options_sanitize(options);
	/* (a listing says nothing of teams: Infection and Gun Game are named
	for the mode byte alone, as the kind's name is) */
	return network_rules_modes_needed(options, engine,
		options[NETWORK_RULES_OPTION_MODE] == NETWORK_RULES_MODE_INFECTION);
}

int network_rules_mode_refusal(unsigned needed, int has_delta, int has_capability, unsigned machine_modes,
	unsigned announce)
{
	if (!needed)
		return NETWORK_RULES_MODE_PLAYS;
	if (!has_delta)
		return announce >= NETWORK_RULES_MODES_VERSION ? NETWORK_RULES_MODE_PLAYS : NETWORK_RULES_MODE_REFUSE_VERSION;
	if (!has_capability || !network_rules_modes_played(machine_modes, needed))
		return NETWORK_RULES_MODE_REFUSE_BUILD;
	return NETWORK_RULES_MODE_PLAYS;
}

char const *network_rules_modes_name(unsigned needed)
{
	if (needed & NETWORK_RULES_MODES_SWAT)
		return "SWAT";
	if (needed & NETWORK_RULES_MODES_INFECTION)
		return "Infection";
	if (needed & NETWORK_RULES_MODES_GUN_GAME)
		return "Gun Game";
	if (needed & NETWORK_RULES_MODES_HARDCORE)
		return "Hardcore";
	return "";
}

char const *network_rules_modes_unplayed(unsigned needed)
{
	unsigned unplayed = needed & ~(unsigned)NETWORK_RULES_MODES_PLAYED;

	return unplayed ? network_rules_modes_name(unplayed) : NULL;
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

/* appends text to text[*length], cut to size - 1 characters */
static void gametype_append(char *text, size_t size, size_t *length, const char *more)
{
	for (; *more && *length < size - 1; more++)
		text[(*length)++] = *more;
	text[*length] = 0;
}

void network_rules_engine_name(int engine, int teams, char *text, size_t size)
{
	static char const *const engines[] = { "CTF", "Slayer", "Oddball", "King", "Race", "Bomb" };
	size_t length = 0;

	if (!size)
		return;
	text[0] = 0;
	if (engine < 1 || engine > (int)(sizeof(engines) / sizeof(engines[0])))
		return;
	/* (Capture the Flag is played in teams alone) */
	if (teams && engine != 1)
		gametype_append(text, size, &length, "Team ");
	gametype_append(text, size, &length, engines[engine - 1]);
}

void network_rules_gametype_name(const char *name, int engine, int teams, int difficulty, char *text, size_t size)
{
	static char const *const difficulties[] = { "Easy", "Normal", "Heroic", "Legendary" };
	size_t length = 0;

	if (!size)
		return;
	/* (its own name: printable ASCII, no spaces at either end) */
	if (name)
	{
		while (*name == ' ')
			name++;
		for (; *name && length < size - 1; name++)
			text[length++] = *name >= 0x20 && *name < 0x7F ? *name : '?';
		while (length && text[length - 1] == ' ')
			length--;
	}
	text[length] = 0;
	/* (a co-op game: its name, "Co-op" for none, and its difficulty) */
	if (!engine && difficulty >= 0 && difficulty < (int)(sizeof(difficulties) / sizeof(difficulties[0])))
	{
		if (!length)
			gametype_append(text, size, &length, "Co-op");
		gametype_append(text, size, &length, " ");
		gametype_append(text, size, &length, difficulties[difficulty]);
		return;
	}
	if (!length)
		network_rules_engine_name(engine, teams, text, size);
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
	header = content_u16le(message);
	if ((header >> 4) != NETWORK_RULES_SHIELD_EFFECTS_SIZE || ((header >> 2) & 3) != NETWORK_RULES_MESSAGE_TYPE_DATA ||
		message[2] != NETWORK_RULES_SHIELD_EFFECTS_KIND || message[3] != 1)
	{
		return 0;
	}
	if (maximum > NETWORK_RULES_SHIELD_EFFECTS_LIMIT)
		maximum = NETWORK_RULES_SHIELD_EFFECTS_LIMIT;
	if (content_u16le(message + 8) > (unsigned)maximum || content_u16le(message + 10) != 0)
		return 0;
	*effects = (int)content_u16le(message + 8);
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

int network_rules_host_hello_matters(unsigned announce, unsigned oldest)
{
	struct network_rules_layout announced;
	unsigned version;

	if (announce < NETWORK_RULES_EXTENDED_OPTIONS_VERSION)
		return 0;
	network_rules_layout(announce, &announced);
	for (version = oldest ? oldest : 1; version < announce; version++)
	{
		struct network_rules_layout layout;

		if (version == NETWORK_RULES_VERSION_UNKNOWN)
			continue;
		network_rules_layout(version, &layout);
		if (layout.settings_size != announced.settings_size || layout.options_size != announced.options_size)
			return 1;
	}
	return 0;
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
	layout->join_request_version_at = -1;
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
	if (rules >= NETWORK_RULES_JOIN_REQUEST_VERSION)
		layout->join_request_extra = NETWORK_RULES_JOIN_REQUEST_EXTRA;
	if (rules >= NETWORK_RULES_VOTES_VERSION)
	{
		layout_drop(layout, NETWORK_RULES_VOTE_REQUEST_KIND);
		layout_drop(layout, NETWORK_RULES_VOTE_STATUS_KIND);
		layout_drop(layout, NETWORK_RULES_MAP_VOTE_STATUS_KIND);
		layout_drop(layout, NETWORK_RULES_MAP_VOTE_CHOICE_KIND);
	}
	if (rules >= NETWORK_RULES_EPOCH_VERSION)
	{
		/* (the MCC-only kinds: 97 to 100, 103 to 106) */
		static const unsigned mcc_kinds[] = { 97, 98, 99, 100, 103, 104, 105, 106 };
		unsigned index;

		layout->settings_size = NETWORK_RULES_EPOCH_SETTINGS_SIZE;
		layout->join_request_extra = NETWORK_RULES_EPOCH_JOIN_REQUEST_EXTRA;
		layout->join_request_version_at = NETWORK_RULES_EPOCH_JOIN_VERSION_AT;
		layout->header_extra = NETWORK_RULES_EPOCH_HEADER_EXTRA;
		layout->epoch_kind = NETWORK_RULES_BOUNDARY_KIND;
		layout->unit_part_size[NETWORK_RULES_POSE_PART_BIT] = NETWORK_RULES_POSE_PART_SIZE;
		layout->unit_part_at[NETWORK_RULES_POSE_PART_BIT] = DELTA_LAYOUT_PART_LAST;
		for (index = 0; index < sizeof(mcc_kinds) / sizeof(mcc_kinds[0]); index++)
			layout_drop(layout, mcc_kinds[index]);
	}
	if (rules >= NETWORK_RULES_DECK_VERSION)
	{
		layout->unit_part_size[NETWORK_RULES_DECK_PART_BIT] = NETWORK_RULES_DECK_PART_SIZE;
		layout->unit_part_at[NETWORK_RULES_DECK_PART_BIT] = DELTA_LAYOUT_PART_AFTER_UP;
		layout->rider_ejection_up = NETWORK_RULES_SIDE_EJECTION_UP;
		layout_drop(layout, NETWORK_RULES_RIDING_KIND);
	}
}

int network_rules_join_request_extra(unsigned version)
{
	struct network_rules_layout layout;

	if (version == NETWORK_RULES_VERSION_UNKNOWN)
		return 0;
	network_rules_layout(version, &layout);
	return layout.join_request_extra;
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
		if (fact->join_request_said)
		{
			/* (the join request's length past 0x70, longer or shorter, of
			zeros: another fill, or past the limit, is not taken) */
			if (fact->join_request_fill == DELTA_LAYOUT_FILL_ZEROS &&
				fact->join_request_extra <= NETWORK_RULES_JOIN_REQUEST_EXTRA_LIMIT)
			{
				layout->join_request_extra = fact->join_request_extra;
				/* (a version word no longer within it is gone) */
				if (layout->join_request_version_at + 2 > layout->join_request_extra)
					layout->join_request_version_at = -1;
				layout->learned++;
			}
			else
				layout->refused++;
		}
		if (fact->join_request_version_said)
		{
			/* (the version word within the request's bytes past 0x70) */
			if ((int)fact->join_request_version_at + 2 <= layout->join_request_extra)
			{
				layout->join_request_version_at = fact->join_request_version_at;
				layout->learned++;
			}
			else
				layout->refused++;
		}
		if (fact->header_extra)
		{
			/* (never fewer than an epoch has, once there is one) */
			if (fact->header_extra <= NETWORK_RULES_HEADER_EXTRA_LIMIT &&
				!(layout->epoch_kind && fact->header_extra < NETWORK_RULES_EPOCH_SIZE))
			{
				layout->header_extra = fact->header_extra;
				layout->learned++;
			}
			else
				layout->refused++;
		}
		if (fact->epoch_kind)
		{
			/* (the epoch is the header's first 4 further bytes; its
			boundary's kind one this build does not know) */
			if (layout->header_extra >= NETWORK_RULES_EPOCH_SIZE && fact->epoch_kind >= NETWORK_RULES_EPOCH_KIND_FIRST &&
				fact->epoch_kind <= 255)
			{
				layout->epoch_kind = fact->epoch_kind;
				layout->learned++;
			}
			else
				layout->refused++;
		}
		{
			int bit;

			for (bit = 0; bit < 8; bit++)
			{
				if (!fact->unit_part_size[bit] && !fact->unit_part_at[bit])
					continue;
				if (!(NETWORK_RULES_UNIT_PARTS_BUILT_IN & (1u << bit)) && fact->unit_part_size[bit] &&
					fact->unit_part_size[bit] <= NETWORK_RULES_UNIT_PART_LIMIT &&
					(fact->unit_part_at[bit] == DELTA_LAYOUT_PART_AFTER_UP || fact->unit_part_at[bit] == DELTA_LAYOUT_PART_LAST))
				{
					layout->unit_part_size[bit] = fact->unit_part_size[bit];
					layout->unit_part_at[bit] = fact->unit_part_at[bit];
					layout->learned++;
				}
				else
					layout->refused++;
			}
		}
		if (fact->rider_ejection_up)
		{
			if (fact->rider_ejection_up <= NETWORK_RULES_RIDER_EJECTION_LIMIT)
			{
				layout->rider_ejection_up = fact->rider_ejection_up;
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

/* (network_rules.h, "other projects' hosts") */
static const struct network_rules_fork network_rules_forks[NUMBER_OF_NETWORK_RULES_FORKS] =
{
	{
		"Content Evolved",
		0xFF00, 0xFF00, 24,
		44, 0x58,
		160, 167,
		{ { 6, "Firefight", 0 }, { 0, NULL, 0 } },
		{ { 0x40, "Tag Injector profiles", 1 }, { 0x20, "randomizer", 0 }, { 0x08, "skulls", 1 },
			{ 0x10, "skulls", 1 }, { 0, NULL, 0 } },
		0x2B,
		{ { 1, "Floodfection", 0 }, { 2, "Tag Gun Game", 0 }, { 0, NULL, 0 } },
		"modded games"
	}
};

struct network_rules_fork const *network_rules_fork_get(int fork)
{
	return fork >= 0 && fork < NUMBER_OF_NETWORK_RULES_FORKS ? &network_rules_forks[fork] : NULL;
}

char const *network_rules_fork_name(int fork)
{
	struct network_rules_fork const *row = network_rules_fork_get(fork);

	return row ? row->name : "";
}

int network_rules_fork_advertised(uint8_t const *reserved, int size, unsigned *base_version)
{
	int fork;

	if (!reserved || size < 2)
		return NETWORK_RULES_FORK_NONE;
	for (fork = 0; fork < NUMBER_OF_NETWORK_RULES_FORKS; fork++)
	{
		struct network_rules_fork const *row = &network_rules_forks[fork];

		if ((content_u16le(reserved) & row->modded_mask) == row->modded_mark)
		{
			if (base_version)
				*base_version = row->base_version_at + 2 <= size ? content_u16le(reserved + row->base_version_at) : 0;
			return fork;
		}
	}
	return NETWORK_RULES_FORK_NONE;
}

int network_rules_fork_settings(unsigned version, unsigned total_size)
{
	struct network_rules_layout layout;
	int fork;

	if (version == NETWORK_RULES_VERSION_UNKNOWN)
		return NETWORK_RULES_FORK_NONE;
	network_rules_layout(version, &layout);
	if (!layout.settings_size || total_size == (unsigned)layout.settings_size)
		return NETWORK_RULES_FORK_NONE;
	for (fork = 0; fork < NUMBER_OF_NETWORK_RULES_FORKS; fork++)
	{
		struct network_rules_fork const *row = &network_rules_forks[fork];
		/* (its options grow in front of the local data, the record with them) */
		int size = layout.settings_size + row->options_size - layout.options_size;

		if (version == row->version && row->options_size > layout.options_size &&
			size <= NETWORK_RULES_SETTINGS_MAXIMUM && total_size == (unsigned)size)
		{
			return fork;
		}
	}
	return NETWORK_RULES_FORK_NONE;
}

int network_rules_fork_options_size(int fork)
{
	struct network_rules_fork const *row = network_rules_fork_get(fork);

	return row ? row->options_size : 0;
}

int network_rules_fork_option_at(int fork)
{
	struct network_rules_fork const *row = network_rules_fork_get(fork);

	return row && row->option_values[0].name ? row->option_at : -1;
}

int network_rules_fork_drops(int fork, unsigned kind)
{
	struct network_rules_fork const *row = network_rules_fork_get(fork);

	return row && kind >= row->dropped_first && kind <= row->dropped_last;
}

/* text + what, no further than size bytes (ended) */
static void fork_append(char *text, int size, int *length, char const *what)
{
	while (*what && *length < size - 1)
		text[(*length)++] = *what++;
	text[*length] = 0;
}

char const *network_rules_fork_refusal(int fork, long engine, int modded, unsigned flags, int option_value,
	char *text, int size)
{
	struct network_rules_fork const *row = network_rules_fork_get(fork);
	struct network_rules_fork_mark const *found = NULL;
	char const *name = NULL;
	int plural = 1;
	int length = 0;
	int index;

	if (!row || !text || size <= 0)
		return NULL;
	for (index = 0; !found && index < (int)(sizeof(row->engines) / sizeof(row->engines[0])) && row->engines[index].name;
		index++)
	{
		if (engine >= 0 && (unsigned long)engine == row->engines[index].value)
			found = &row->engines[index];
	}
	for (index = 0; !found && modded && index < (int)(sizeof(row->flags) / sizeof(row->flags[0])) &&
		row->flags[index].name; index++)
	{
		if (flags & row->flags[index].value)
			found = &row->flags[index];
	}
	for (index = 0; !found && option_value > 0 &&
		index < (int)(sizeof(row->option_values) / sizeof(row->option_values[0])) && row->option_values[index].name;
		index++)
	{
		if ((unsigned)option_value == row->option_values[index].value)
			found = &row->option_values[index];
	}
	if (found)
	{
		name = found->name;
		plural = found->plural;
	}
	else if (modded)
		name = row->modded_name;
	else
		return NULL;
	text[0] = 0;
	fork_append(text, size, &length, row->name);
	fork_append(text, size, &length, "'s ");
	fork_append(text, size, &length, name);
	fork_append(text, size, &length, plural ? " aren't playable here yet." : " isn't playable here yet.");
	return text;
}

void network_rules_lobby_budget_reset(struct network_rules_lobby_budget *budget)
{
	memset(budget, 0, sizeof(*budget));
}

int network_rules_lobby_budget_take(struct network_rules_lobby_budget *budget, uint32_t now)
{
	const uint32_t full = NETWORK_RULES_LOBBY_BURST * 1000u;

	if (!budget->started)
	{
		budget->started = 1;
		budget->tokens = full;
	}
	else
	{
		/* (the time since, wrapping; a clock that went back adds none) */
		uint32_t elapsed = now - budget->time;

		if (elapsed > 0x7FFFFFFFu)
			elapsed = 0;
		/* (elapsed milliseconds at NETWORK_RULES_LOBBY_PER_SECOND a second
		are that many thousandths each) */
		if (elapsed >= full / NETWORK_RULES_LOBBY_PER_SECOND)
			budget->tokens = full;
		else
		{
			budget->tokens += elapsed * NETWORK_RULES_LOBBY_PER_SECOND;
			if (budget->tokens > full)
				budget->tokens = full;
		}
	}
	budget->time = now;
	if (budget->tokens < 1000u)
		return 0;
	budget->tokens -= 1000u;
	return 1;
}

int network_rules_address_is_local(uint32_t address)
{
	return address == 0 ||
		(address & 0xFF000000u) == 0x7F000000u ||
		(address & 0xFF000000u) == 0x0A000000u ||
		(address & 0xFFF00000u) == 0xAC100000u ||
		(address & 0xFFFF0000u) == 0xC0A80000u ||
		(address & 0xFFFF0000u) == 0xA9FE0000u;
}

int network_rules_address_full(uint32_t address, uint32_t const *others, int count, long maximum)
{
	int index;
	long same = 0;

	if (maximum <= 0 || network_rules_address_is_local(address))
		return 0;
	for (index = 0; index < count; index++)
	{
		if (others[index] == address)
			same++;
	}
	return same >= maximum;
}

int network_rules_address_joins_as_lan(uint32_t address)
{
	return network_rules_address_is_local(address) || (address & 0xFFC00000u) == 0x64400000u;
}

int network_rules_join_password_refused(uint32_t address, int has_password)
{
	return has_password && !network_rules_address_joins_as_lan(address);
}

int network_rules_search_answered(uint32_t address, int direct_ip_host, uint32_t now,
	struct network_rules_search_budget *budget)
{
	const uint32_t full = NETWORK_RULES_SEARCH_BURST * 1000u;

	if (network_rules_address_joins_as_lan(address))
		return 1;
	/* (the internet's: multicast, the reserved range and broadcast never) */
	if (!direct_ip_host || address >= 0xE0000000u)
		return 0;
	if (!budget->started)
	{
		budget->started = 1;
		budget->tokens = full;
	}
	else
	{
		/* (the time since, wrapping; a clock that went back adds none) */
		uint32_t elapsed = now - budget->time;

		if (elapsed > 0x7FFFFFFFu)
			elapsed = 0;
		if (elapsed >= full / NETWORK_RULES_SEARCH_PER_SECOND)
			budget->tokens = full;
		else
		{
			budget->tokens += elapsed * NETWORK_RULES_SEARCH_PER_SECOND;
			if (budget->tokens > full)
				budget->tokens = full;
		}
	}
	budget->time = now;
	if (budget->tokens < 1000u)
		return 0;
	budget->tokens -= 1000u;
	return 1;
}

/* ---------- chupa-43a's facts */

static void rules_put_word(uint8_t *bytes, unsigned value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

/* (the join request's version word: the packet codec's order, big-endian) */
static void rules_put_codec_word(uint8_t *bytes, unsigned value)
{
	bytes[0] = (uint8_t)(value >> 8);
	bytes[1] = (uint8_t)value;
}

static void rules_put_long(uint8_t *bytes, uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
}

/* the message header's size (in this machine's order, as the transport
gives it) set */
static void rules_header_size(uint8_t *message, int size)
{
	uint16_t header;

	memcpy(&header, message, sizeof(header));
	header = (uint16_t)((header & 0xF) | ((unsigned)size << 4));
	memcpy(message, &header, sizeof(header));
}

int network_rules_join_request_tail(unsigned version, uint8_t *tail, int room)
{
	struct network_rules_layout layout;

	if (version == NETWORK_RULES_VERSION_UNKNOWN)
		return 0;
	network_rules_layout(version, &layout);
	if (layout.join_request_extra > room)
		return -1;
	memset(tail, 0, (size_t)layout.join_request_extra);
	if (layout.join_request_version_at >= 0 && layout.join_request_version_at + 2 <= layout.join_request_extra)
		rules_put_codec_word(tail + layout.join_request_version_at, version);
	return layout.join_request_extra;
}

int network_rules_snapshot_due(int never_sent, int again_due, int players_changed, uint32_t sent_epoch,
	uint32_t epoch)
{
	return never_sent || again_due || players_changed || sent_epoch != epoch;
}

int network_rules_host_takes(uint32_t epoch, uint32_t host_epoch, int kind, int size, int count, int from_stream,
	int in_batch)
{
	if (epoch == host_epoch && host_epoch)
		return 1;
	return kind == NETWORK_RULES_CLIENT_READY_KIND && size == NETWORK_RULES_HEADER_SIZE && count >= 0 && count <= 1 &&
		from_stream && !in_batch;
}

int network_rules_client_held_back(int epoch_layout, int client, int synchronized, int kind)
{
	return epoch_layout && client && !synchronized && kind != NETWORK_RULES_CLIENT_READY_KIND;
}

int network_rules_client_takes_unsynchronized(int kind, int from_stream, int in_batch)
{
	return kind == NETWORK_RULES_OBJECT_CHANGES_KIND || kind == NETWORK_RULES_OBJECTS_SYNCHRONIZED_KIND ||
		(kind == NETWORK_RULES_INVENTORIES_KIND && from_stream && !in_batch);
}

int network_rules_reliable_split(int size, int count, int entry_size, int extra, int maximum)
{
	if (size + extra <= maximum)
		return 0;
	if (entry_size <= 0 || count < 2 || size - NETWORK_RULES_HEADER_SIZE != count * entry_size ||
		NETWORK_RULES_HEADER_SIZE + (count - count / 2) * entry_size + extra > maximum)
	{
		return -1;
	}
	return count / 2;
}

int network_rules_unit_state_length(struct network_rules_layout const *layout, uint8_t const *record, int size,
	int powerups)
{
	/* (network_distributed.c's bits: flags alive 0, placed 1, predicted 7;
	parts riding 0, up 1, damage 2, powerups 3, camouflage 4, killer 5) */
	int length = 3, alive, placed, after_up, last;
	unsigned parts;

	if (size < 3)
		return 0;
	alive = record[1] & 1;
	placed = (record[1] >> 1) & 1;
	parts = record[2];
	after_up = network_rules_unit_part_bytes(layout, parts, DELTA_LAYOUT_PART_AFTER_UP, alive, placed);
	last = network_rules_unit_part_bytes(layout, parts, DELTA_LAYOUT_PART_LAST, alive, placed);
	if (after_up < 0 || last < 0)
		return 0;
	if (alive)
	{
		length += 4;
		if (parts & 1)
			length += 4 + 2;
		if (placed)
			length += 12 + 6 + 6 + ((parts & 2) ? 6 : 0) + after_up;
		if (record[1] & 0x80)
			length += 2;
		length += 2 + 2;
		if (parts & 4)
			length += 8;
		if (parts & 8)
			length += 2 * powerups;
		if (parts & 16)
			length += 2;
		length += last;
	}
	else if (parts & 32)
		length += 1;
	return length <= size ? length : 0;
}

int network_rules_revert_boundary_to(int client, int local, int ready)
{
	return client && !local && ready;
}

unsigned network_rules_client_settings_version(unsigned joined, int hosting, unsigned announce)
{
	return hosting ? announce : joined;
}

unsigned network_rules_client_noted_version(unsigned joined, int hosting, unsigned at_least)
{
	return !hosting && joined == NETWORK_RULES_VERSION_UNKNOWN ? at_least : joined;
}

unsigned network_rules_join_retry_version(unsigned joined, int hosting, unsigned long waited)
{
	struct network_rules_layout layout;
	unsigned newest;

	if (joined != NETWORK_RULES_VERSION_UNKNOWN || hosting || waited < NETWORK_RULES_JOIN_RETRY_MS)
		return 0;
	newest = network_rules_layout_newest();
	network_rules_layout(newest, &layout);
	return layout.join_request_version_at >= 0 ? newest : 0;
}

int network_rules_join_request_refused(unsigned version, uint8_t const *tail, int tail_size)
{
	struct network_rules_layout layout;

	if (version == NETWORK_RULES_VERSION_UNKNOWN)
		return 0;
	network_rules_layout(version, &layout);
	if (!tail)
		tail_size = 0;
	if (layout.join_request_version_at < 0)
		return 0;
	if (tail_size != layout.join_request_extra || layout.join_request_version_at + 2 > tail_size)
		return 1;
	return content_u16be(tail + layout.join_request_version_at) != version;
}

int network_rules_header_expand(uint8_t const *message, int size, uint8_t *out, int room, int extra, int has_epoch,
	uint32_t epoch)
{
	int in = NETWORK_RULES_HEADER_SIZE, at;

	if (size < NETWORK_RULES_HEADER_SIZE || extra < 0 || extra > NETWORK_RULES_HEADER_EXTRA_LIMIT ||
		size + extra > room || (has_epoch && extra < NETWORK_RULES_EPOCH_SIZE))
	{
		return 0;
	}
	memcpy(out, message, NETWORK_RULES_HEADER_SIZE);
	memset(out + NETWORK_RULES_HEADER_SIZE, 0, (size_t)extra);
	if (has_epoch)
		rules_put_long(out + NETWORK_RULES_HEADER_SIZE, epoch);
	at = NETWORK_RULES_HEADER_SIZE + extra;
	if (message[2] != NETWORK_RULES_BATCH_KIND)
	{
		memcpy(out + at, message + in, (size_t)(size - in));
		at += size - in;
	}
	else
	{
		/* (each message: its size word, then its header past the message
		header (6 bytes), the further bytes, its entries) */
		while (in < size)
		{
			unsigned length;

			if (size - in < 2)
				return 0;
			length = content_u16le(message + in);
			if (length < NETWORK_RULES_HEADER_SIZE - 2 || (int)length > size - in - 2 ||
				at + 2 + (int)length + extra > room)
			{
				return 0;
			}
			rules_put_word(out + at, length + (unsigned)extra);
			memcpy(out + at + 2, message + in + 2, NETWORK_RULES_HEADER_SIZE - 2);
			memset(out + at + NETWORK_RULES_HEADER_SIZE, 0, (size_t)extra);
			if (has_epoch)
				rules_put_long(out + at + NETWORK_RULES_HEADER_SIZE, epoch);
			memcpy(out + at + NETWORK_RULES_HEADER_SIZE + extra, message + in + NETWORK_RULES_HEADER_SIZE,
				length - (NETWORK_RULES_HEADER_SIZE - 2));
			at += 2 + (int)length + extra;
			in += 2 + (int)length;
		}
	}
	if (at > 0xFFF)
		return 0;
	rules_header_size(out, at);
	return at;
}

int network_rules_header_strip(uint8_t const *message, int size, uint8_t *out, int extra, int has_epoch, uint32_t want,
	int keep_kind, uint32_t *epoch)
{
	int in, at = NETWORK_RULES_HEADER_SIZE;

	*epoch = 0;
	if (size < NETWORK_RULES_HEADER_SIZE + extra || (has_epoch && extra < NETWORK_RULES_EPOCH_SIZE))
		return 0;
	if (has_epoch)
		*epoch = content_u32le(message + NETWORK_RULES_HEADER_SIZE);
	memcpy(out, message, NETWORK_RULES_HEADER_SIZE);
	in = NETWORK_RULES_HEADER_SIZE + extra;
	if (message[2] != NETWORK_RULES_BATCH_KIND)
	{
		memcpy(out + at, message + in, (size_t)(size - in));
		at += size - in;
	}
	else
	{
		while (in < size)
		{
			unsigned length;
			uint32_t inner = 0;

			/* (reading stops at a size word past the end, or shorter than a
			message's header: the messages before it are kept) */
			if (size - in < 2)
				break;
			length = content_u16le(message + in);
			if ((int)length < NETWORK_RULES_HEADER_SIZE - 2 + extra || (int)length > size - in - 2)
				break;
			if (has_epoch)
				inner = content_u32le(message + in + NETWORK_RULES_HEADER_SIZE);
			if (!has_epoch || inner == want || (keep_kind && message[in + 2] == keep_kind))
			{
				rules_put_word(out + at, length - (unsigned)extra);
				memcpy(out + at + 2, message + in + 2, NETWORK_RULES_HEADER_SIZE - 2);
				memcpy(out + at + NETWORK_RULES_HEADER_SIZE, message + in + NETWORK_RULES_HEADER_SIZE + extra,
					length - (unsigned)extra - (NETWORK_RULES_HEADER_SIZE - 2));
				at += 2 + (int)length - extra;
			}
			in += 2 + (int)length;
		}
	}
	rules_header_size(out, at);
	return at;
}

int network_rules_share_body_valid(int kind, int body_size)
{
	switch (kind)
	{
	case 83:
		return body_size == 72;
	case 84:
		return body_size >= 68 && (body_size - 68) % 40 == 0 && (body_size - 68) / 40 <= 4;
	case 85:
		return body_size >= 4 && (body_size - 4) % 32 == 0;
	case 86:
		return body_size == 12;
	default:
		return 0;
	}
}

int network_rules_share_header_size(uint8_t const *message, int size, int extra)
{
	if (!message || size < NETWORK_RULES_HEADER_SIZE || extra < 0)
		return 0;
	if (extra && network_rules_share_body_valid(message[2], size - NETWORK_RULES_HEADER_SIZE - extra))
		return NETWORK_RULES_HEADER_SIZE + extra;
	if (network_rules_share_body_valid(message[2], size - NETWORK_RULES_HEADER_SIZE))
		return NETWORK_RULES_HEADER_SIZE;
	return 0;
}

int network_rules_epoch_newer(uint32_t epoch, uint32_t current)
{
	return (int32_t)(epoch - current) > 0;
}

uint32_t network_rules_epoch_next(uint32_t current)
{
	return current + 1 ? current + 1 : 1;
}

int network_rules_boundary_taken(int size, int count, int from_stream, int in_batch, uint32_t epoch,
	uint32_t current)
{
	return size == NETWORK_RULES_HEADER_SIZE && count == 0 && from_stream && !in_batch && epoch &&
		network_rules_epoch_newer(epoch, current);
}

int network_rules_unit_part_bytes(struct network_rules_layout const *layout, unsigned parts, int where, int alive,
	int placed)
{
	int bit, bytes = 0;

	for (bit = 0; bit < 8; bit++)
	{
		if (!(parts & (1u << bit)) || (NETWORK_RULES_UNIT_PARTS_BUILT_IN & (1u << bit)))
			continue;
		if (!layout->unit_part_size[bit])
			return -1;
		if (layout->unit_part_at[bit] != where)
			continue;
		if (!alive || (where == DELTA_LAYOUT_PART_AFTER_UP && !placed))
			return -1;
		bytes += layout->unit_part_size[bit];
	}
	return bytes;
}

float network_rules_rider_ejection_up(struct network_rules_layout const *layout)
{
	return (float)layout->rider_ejection_up / 1000.0f;
}
