/*
HALO_PORT_WINDOW.H

Where the game's memory window is. The Xbox maps physical memory at virtual
0x80000000 + P, and the game data is written for that: a map file's tag
cache sits at 0x803a6000, and the addresses in it are addresses in the
window. In this port the window is always at 0x80000000, so these name the
constants the game data was written with and move nothing; the Android
Vulkan renderer (port/android/guest/d3d8_vk.c) includes this where it takes
an address out of the game's data, so that a port that moves the window can
translate it here.
*/

#ifndef __HALO_PORT_WINDOW_H
#define __HALO_PORT_WINDOW_H

#define PORT_WINDOW_BASE 0x80000000UL
#define PORT_IMAGE_SHIFT 0UL

#define PORT_WINDOW_ADDRESS(xbox_address) ((unsigned long)(xbox_address))
#define PORT_WINDOW_PHYSICAL_ADDRESS(xbox_address) ((unsigned long)(xbox_address) - 0x80000000UL)
#define PORT_WINDOW_SHIFT 0UL

/* an address the game data was written with, at where the window is */
#define PORT_WINDOW_REBASE(address) ((void *)(address))

#endif /* __HALO_PORT_WINDOW_H */
