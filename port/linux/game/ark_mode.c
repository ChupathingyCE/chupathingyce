/*
ARK_MODE.C

Project Ark, an early mode beside Vanilla, the packs, Stubbs and SPV3. Bring
your own copy: none of its files ship. They are read from the player's own
MCC install (Halo: The Master Chief Collection on Steam), from a maps_ark
folder beside maps or in place (port/linux/src/ark_files.c).

The mode is picked as a pack's menus are (game.pack_menus = "ark": the setup
screen's Play list, --mode ark, HALO_PACK_MENUS), but it has no ui.map, so
map_pack_menus() stays empty and the menus are the game's own. Built only with
HALO_ARK (configure.py --ark).

Everything here is an early first look, off unless picked: stock Halo, Custom
Edition, MCC's Halo 1 maps and the packs never take these paths. Offline and
split screen only for now, as Stubbs mode is.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "errors.h"
#include "halo_join_reasons.h"
#include "ark_mode.h"
#include "ark_names.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(HALO_ARK)

#define ARK_JOIN_FROM_ARK_MODE ARK_MODE_DISPLAY_NAME " doesn't join online games yet. " \
	"Pick another mode in the setup screen to play online."

char const *config_string(char const *name);
int ark_files_folder(char *path, unsigned long size);
void platform_log(char const *format, ...);
void platform_show_message(char const *title, char const *text);

boolean ark_mode_active(
	void)
{
	static int active = -1;

	if (active < 0)
	{
		char const *value = config_string("game.pack_menus");

		/* (the id, or its folder's name) */
		active = ARK_MODE_PLAYABLE && value &&
			(!_stricmp(value, ARK_MODE_PACK_ID) || !_stricmp(value, ARK_MODE_FOLDER));
		if (active && !ark_files_folder(NULL, 1024))
		{
			platform_log("ark: the mode is picked, but its files are not here (a " ARK_MODE_FOLDER
				" folder, or MCC's install): playing Halo");
			active = 0;
		}
		if (active)
			platform_log("ark: the mode is on (an early look: offline and split screen)");
	}
	return active ? TRUE : FALSE;
}

void ark_mode_log(
	char const *format,
	...)
{
	char text[512];
	va_list arguments;

	if (!ark_mode_active())
		return;
	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	error(_error_silent, "ark: %s", text);
}

boolean ark_mode_join_refused(
	char const *map_name)
{
	if (!ark_mode_active())
		return FALSE;
	platform_log("ark: not joining the game on %s: the mode joins no online games yet", map_name ? map_name : "?");
	platform_show_message(JOIN_FAILED_TITLE, ARK_JOIN_FROM_ARK_MODE);
	return TRUE;
}

boolean ark_mode_remote_machine_refused(
	void)
{
	if (!ark_mode_active())
		return FALSE;
	platform_log("ark: refusing a machine from elsewhere: the mode hosts no online games yet");
	return TRUE;
}

#else

/* (a build without the mode: nothing here, and nothing called) */

boolean ark_mode_active(
	void)
{
	return FALSE;
}

void ark_mode_log(
	char const *format,
	...)
{
	(void)format;
}

boolean ark_mode_join_refused(
	char const *map_name)
{
	(void)map_name;
	return FALSE;
}

boolean ark_mode_remote_machine_refused(
	void)
{
	return FALSE;
}

#endif
