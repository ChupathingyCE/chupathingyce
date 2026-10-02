/*
HALO_XBOX_PREFIX.H

Force-included ahead of every game unit of the Xbox build (clang, through
OXDK; tools/xbox_build.py). The game compiles against the January SDK's
declarations (port/include/xdk), with only the C runtime's headers from the
XDK.
*/

#ifndef __HALO_XBOX_PREFIX_H
#define __HALO_XBOX_PREFIX_H

/* clang predeclares the MSVC _Interlocked* intrinsics with prototypes that
conflict with the SDK's; the game never calls them (xdk_win32.h) */
#define _InterlockedCompareExchange halo_xbox_InterlockedCompareExchange
#define _InterlockedDecrement halo_xbox_InterlockedDecrement
#define _InterlockedExchange halo_xbox_InterlockedExchange
#define _InterlockedExchangeAdd halo_xbox_InterlockedExchangeAdd
#define _InterlockedIncrement halo_xbox_InterlockedIncrement

/* rasterizer_xbox_debug.h declares rasterizer_debug_drawing_begin with a
second parameter the definition ignores; MSVC tolerates the two declarations
meeting, clang does not (as halo_linux_source_fixups.h) */
#define rasterizer_debug_drawing_begin(opaque, ...) (rasterizer_debug_drawing_begin)(opaque)

/* The DirectSound calls whose January form (mix bins as a mask) the SDK
library no longer takes: port/xbox/src/xbox_dsound.c translates them */
#define DirectSoundCreateBuffer halo_xbox_DirectSoundCreateBuffer
#define IDirectSound_CreateSoundBuffer halo_xbox_IDirectSound_CreateSoundBuffer
#define IDirectSound_CreateSoundStream halo_xbox_IDirectSound_CreateSoundStream
#define IDirectSound_DownloadEffectsImage halo_xbox_IDirectSound_DownloadEffectsImage
#define IDirectSound_SetMixBinHeadroom halo_xbox_IDirectSound_SetMixBinHeadroom
#define IDirectSoundStream_SetMixBins halo_xbox_IDirectSoundStream_SetMixBins
#define IDirectSoundStream_SetMixBinVolumes halo_xbox_IDirectSoundStream_SetMixBinVolumes

/* the shared sources' limits, as the native builds' prefix includes them
(the network's are the same on every machine; the capacity is the
console's: HALO_XBOX_CONSOLE) */
#include "../../linux/include/halo_port_limits.h"
/* a host's socket set holds a socket for every machine (the SDK's Winsock
headers default to 64) */
#define FD_SETSIZE HALO_PORT_FD_SETSIZE
/* the desktop builds' mouse in the menus, which the console has none of
(port/xbox/src/xbox_port.c) */
#include "../../linux/include/halo_ui_pointer.h"

#endif
