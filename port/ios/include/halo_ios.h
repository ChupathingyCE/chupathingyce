/*
HALO_IOS.H

The game's entry point for the iOS app (port/ios/src/ios_main.c), which the
launcher calls (app/Sources/App.swift, Game.start).
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

/* An invite link (halo://join/..., or a player key's halo://key/...) the
app was opened with: before halo_ios_run, the game starts with it and joins
it; once the game runs, halo_ios_open_link hands it over, and returns 0 if
the game is not running yet */
void halo_ios_set_invite(const char *link);
int halo_ios_open_link(const char *link);

/* iPhone Duo's fold, for the touch controls (port/ios/src/ios_touch.m),
which keep off it: the app's function, given the overlay's view, puts the
fold's frame in the view's points (x, y, width, height) in frame and
returns 1 while the device is part open, else 0. halo_ios_fold_changed
asks the overlay to look again (the hinge moved). Nothing on the Apple TV */
typedef int (*halo_ios_fold_proc)(void *view, double *frame);
void halo_ios_set_fold_proc(halo_ios_fold_proc proc);
void halo_ios_fold_changed(void);

#ifdef __cplusplus
}
#endif

#endif
