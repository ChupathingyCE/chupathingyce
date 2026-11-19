/*
MISSING_MAPS_SCREEN.H

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef __MISSING_MAPS_SCREEN_H
#define __MISSING_MAPS_SCREEN_H
#pragma once

/* ---------- prototypes/MISSING_MAPS_SCREEN.C */

/* a console's: ui.map (map_name) not found. Says so on a screen drawn
without any map, with the folder looked in, until the player quits to the
system's menu; never returns (HALO_CONSOLE_MISSING_MAPS_SCREEN, else it does
nothing) */
void missing_maps_screen(char const *map_name);

#if defined(HALO_CONSOLE_MISSING_MAPS_SCREEN) && !defined(HALO_XBOX_CONSOLE)
/* a port's own way to write a host path as the player finds it on the
device (the Wii U's SD card as sd:/); optional: without it the path is
shown as the platform layer has it */
extern void console_path_shown(const char *path, char *shown, unsigned long size) __attribute__((weak));
#endif

#endif // __MISSING_MAPS_SCREEN_H
