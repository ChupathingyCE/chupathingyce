/*
XBOX_PORT.C

What the shared sources ask of the native builds' platform layer
(port/linux/src) that the console answers itself, until it has more of that
layer: the settings (their defaults: there is no config.toml yet), the log,
the screen (640x480, no widescreen), and none of the mouse, the high-res
HUD and text, or internet play.
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../linux/include/halo_ui_pointer.h"

unsigned long __cdecl DbgPrint(const char *format, ...);

/* ---------- the C runtime's newer functions */

int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments)
{
	int length;

	if (!size)
		return _vsnprintf(NULL, 0, format, arguments);
	length = _vsnprintf(buffer, size, format, arguments);
	/* (_vsnprintf leaves a full buffer unterminated, and says -1) */
	buffer[size - 1] = 0;
	return length < 0 ? (int)size - 1 : length;
}

int snprintf(char *buffer, size_t size, const char *format, ...)
{
	va_list arguments;
	int length;

	va_start(arguments, format);
	length = vsnprintf(buffer, size, format, arguments);
	va_end(arguments);
	return length;
}

/* round to the nearest integer, halves to even: adding and taking away 2^52
leaves the integer part, rounded as the FPU rounds (to nearest) */
double rint(double x)
{
	const double two52 = 4503599627370496.0;
	volatile double sum;

	if (x >= two52 || x <= -two52 || x != x)
		return x;
	if (x >= 0.0)
	{
		sum = x + two52;
		return sum - two52;
	}
	sum = x - two52;
	return sum + two52;
}

float rintf(float x)
{
	return (float)rint(x);
}

/* ---------- the log */

void platform_log(const char *format, ...)
{
	char text[1024];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	DbgPrint("halo: %s\n", text);
}

void platform_show_message(const char *title, const char *message)
{
	DbgPrint("halo: %s: %s\n", title, message);
}

/* ---------- the settings: their defaults (port/linux/src/port_config.c's)
for those the console's code reads */

static const struct
{
	const char *name;
	const char *value;
} settings[] =
{
	{ "debug.telnet_console", "false" },
	{ "debug.telnet_console_port", "2323" },
	{ "debug.network_test", "" },
	{ "debug.network_test_start", "15.0" },
	{ "debug.network_test_kill", "0.0" },
	{ "debug.network_test_score", "0" },
	{ "debug.network_test_shoot", "0.0" },
	{ "debug.network_test_vehicle", "0.0" },
	{ "debug.network_test_pickup", "0.0" },
	{ "debug.network_test_pickup_weapon", "" },
	{ "display.direct_camera", "true" },
	{ "game.console_log", "important" },
};

static const char *setting(const char *name)
{
	unsigned int index;

	for (index = 0; index < sizeof(settings) / sizeof(settings[0]); index++)
	{
		if (!strcmp(settings[index].name, name))
			return settings[index].value;
	}
	DbgPrint("halo: no setting %s on the console\n", name);
	return "";
}

int config_boolean(const char *name)
{
	return !strcmp(setting(name), "true");
}

long config_integer(const char *name)
{
	return atol(setting(name));
}

double config_real(const char *name)
{
	return atof(setting(name));
}

const char *config_string(const char *name)
{
	return setting(name);
}

/* ---------- the screen: the console's 640x480 */

long halo_screen_width(void)
{
	return 640;
}

long halo_screen_commit(void)
{
	return 640;
}

void halo_screen_ui_offset(unsigned char centered)
{
	(void)centered;
}

/* the game ticks at 30 Hz and draws its ticks: no frames between them */
int halo_interpolation_enabled(void)
{
	return 0;
}

/* ---------- input: the controller only */

int halo_ui_pointer_update(int menus_active, struct halo_ui_pointer *pointer)
{
	(void)menus_active;
	(void)pointer;
	return 0;
}

int halo_linux_mouse_aiming(short gamepad_index)
{
	(void)gamepad_index;
	return 0;
}

int halo_linux_mouse_look(short gamepad_index, float *yaw, float *pitch)
{
	(void)gamepad_index;
	(void)yaw;
	(void)pitch;
	return 0;
}

/* (the automated network tests' held action button) */
void test_input_hold_action(int hold)
{
	(void)hold;
}

/* ---------- the high-res HUD and text: none, the game's own bitmaps */

long hud_hires_asset_count(void)
{
	return 0;
}

long hud_hires_asset_bitmap(long asset)
{
	(void)asset;
	return -1;
}

long hud_hires_asset_fits(long asset, long width, long height)
{
	(void)asset;
	(void)width;
	(void)height;
	return 0;
}

const char *hud_hires_asset_tag(long asset)
{
	(void)asset;
	return "";
}

struct text_hires_glyph;

long text_hires_font(const char *tag_name, float cap_height)
{
	(void)tag_name;
	(void)cap_height;
	return -1;
}

int text_hires_covers(long font, unsigned long code)
{
	(void)font;
	(void)code;
	return 0;
}

int text_hires_glyph(long font, unsigned long code, struct text_hires_glyph *glyph)
{
	(void)font;
	(void)code;
	(void)glyph;
	return 0;
}

void text_hires_register_atlas(const unsigned long *texture, unsigned long width, unsigned long height)
{
	(void)texture;
	(void)width;
	(void)height;
}

/* ---------- internet play (p2p.c): none yet; system link only */

void p2p_hardware_id(char *hex, int size)
{
	if (size > 0)
		hex[0] = 0;
}

void p2p_hardware_id_sanitize(char *destination, int size, const char *source)
{
	if (size <= 0)
		return;
	strncpy(destination, source ? source : "", (size_t)size - 1);
	destination[size - 1] = 0;
}

void p2p_discord_identity(char *id, int id_size, char *name, int name_size)
{
	if (id_size > 0)
		id[0] = 0;
	if (name_size > 0)
		name[0] = 0;
}

void p2p_discord_sanitize(char *destination, int size, const char *source, int name)
{
	(void)name;
	p2p_hardware_id_sanitize(destination, size, source);
}

unsigned long p2p_peer_endpoint_address(unsigned long virtual_address)
{
	(void)virtual_address;
	return 0;
}

void p2p_set_game_player_counts(int count, int maximum)
{
	(void)count;
	(void)maximum;
}
