/*
HALO_IOS.H

The game's entry point for the iOS app (port/ios/src/ios_main.c), which the
app's native module calls (app/modules/halo-native).
*/

#ifndef __HALO_IOS_H
#define __HALO_IOS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the game, on the main thread (SDL's video and events must be
called there), with the game data in data_root (the folder holding maps/:
the app's Documents) and hardware_id as the device's id for online play (its
vendor identifier). Returns only if the game cannot start, with a message
for the player in error; once it has started, the game ends the process. */
int halo_ios_run(const char *data_root, const char *hardware_id, char *error, int error_size);

#ifdef __cplusplus
}
#endif

#endif
