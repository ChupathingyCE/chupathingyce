/*
VRR.H

display.vrr: for a display of variable refresh (G-SYNC, FreeSync, VRR).
With it on, the game shows its frames with vsync and holds them to 3 a
second under the display's refresh rate (117 at 120 Hz, 141 at 144), or to
display.max_fps where that is lower, so the frames stay inside the
display's range and each is shown as soon as it is ready: no tearing, and
no vsync queue. The simulation is untouched: only how often a frame is
drawn changes.

"off" (the default) never; "on" always; "auto" only where the system says
VRR is active: gamescope's word (the Steam Deck's Game Mode on a VRR
display) or a Linux display's DRM output with VRR enabled. Elsewhere
(Windows, the Mac) auto says nothing, and stays off.

The rules have no SDL or game types, so the build machine tests them
(port/linux/tests/vrr_test.c). Based on work by goatbutt and the OpenH1
team.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-FileCopyrightText: 2026 goatbutt and the OpenH1 team
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef VRR_H
#define VRR_H

#include <stddef.h>

/* how far under the refresh rate the frames are held */
#define VRR_HEADROOM 3.0f
/* how often auto asks the system again, in milliseconds (a compositor
that turns VRR on once the game is fullscreen is followed) */
#define VRR_ASK_MS 2000u

/* display.vrr's words */
enum
{
	VRR_MODE_OFF,
	VRR_MODE_ON,
	VRR_MODE_AUTO,
};

/* what the system says (vrr_system) */
enum
{
	/* nothing: no gamescope, no readable DRM, not Linux */
	VRR_SYSTEM_NONE,
	VRR_SYSTEM_GAMESCOPE_ON,
	VRR_SYSTEM_GAMESCOPE_OFF,
	VRR_SYSTEM_DRM_ON,
	VRR_SYSTEM_DRM_OFF,
};

/* display.vrr's text as a mode; anything unknown is off */
int vrr_mode(const char *text);

/* whether VRR is on for a mode and what the system says, with the reason
written to why (for the log) */
int vrr_decide(int mode, int system, char *why, size_t size);

/* the frames a second with VRR on: the refresh rate less VRR_HEADROOM, or
max_fps where it is above 0 and lower; 0 when neither is known (vsync alone
paces the frames) */
float vrr_frame_rate(float refresh, long max_fps);

/* asks the system whether VRR is active (gamescope's X root window
properties, through libX11 where it loads; else the DRM cards' CRTCs). Only
on Linux (not Android); VRR_SYSTEM_NONE elsewhere */
int vrr_system(void);

#endif
