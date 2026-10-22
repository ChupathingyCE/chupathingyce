/*
PORT_CONFIG.H

The native ports' settings, read from config.toml (port_config.c): next to
the executable on the desktop, in the data folder (the one holding maps/)
on Android, beside default.xbe (D:\) on the original Xbox. A missing file
is written with the defaults, commented out, so that a setting the player
has not chosen follows the version's default (a file from before that is
updated once). Each setting can also be set for one run with its HALO_*
environment variable, which wins over the file (the tools and the Android
app pass settings that way; the console has none).

Settings are named "section.key", as in the file: "display.vsync".
*/

#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

#include <stddef.h>

int config_boolean(const char *name);
long config_integer(const char *name);
double config_real(const char *name);
/* never NULL; "" when unset */
const char *config_string(const char *name);
/* sets a setting from its value as text ("true", "60", "1.5", "all"), and
writes it into config.toml (only its line changes); 1 on success */
int config_write(const char *name, const char *value);
int config_write_boolean(const char *name, int value);
/* sets a setting for this run only, as its environment variable does
(config.toml is not changed); 1 on success */
int config_set(const char *name, const char *value);
/* a setting's value as text ("true", "60", "1.5", "all"); 0 if there is no
such setting */
int config_text(const char *name, char *text, size_t size);
/* the folder config.toml is in, with its separator */
void config_folder(char *path, size_t size);
/* a whole file (a UTF-8 path), NUL terminated, or NULL; free() it (the Android
guest has no SDL_LoadFile) */
char *config_file_read(const char *path, size_t *size);
/* a setting's default, as text; 0 if there is no such setting */
int config_default(const char *name, char *text, size_t size);
/* The settings an existing config.toml has that a new version's defaults
would change (display.anti_aliasing, display.shadow_resolution,
display.menus, audio.buffer_frames on Android), until the player is asked:
the count, and in text one line a change, in words. They keep their old
values meanwhile, and the file is left as it is. A new file has the new
defaults and none pending; so has the dedicated server. */
int config_new_defaults_pending(char *text, size_t size);
/* the player's answer, once: 1 to follow the new defaults (a value an older
version wrote beside the commented one is taken out), 0 to keep the old
values, written into config.toml so that they stay. Either way
game.defaults_answered is written, so the question does not come again */
void config_new_defaults_answer(int use_new);
/* how many times config_write has changed a setting: what keeps one reads
it again when this moves */
unsigned long config_changes(void);

#endif
