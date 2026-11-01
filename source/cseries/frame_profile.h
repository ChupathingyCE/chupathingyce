/*
FRAME_PROFILE.H

port: a frame's stages named for a console's profile log in bring-up
(HALO_CONSOLE_BRINGUP_LOG, a console's developer tools: the port's
console_profile_stage); nothing on the other builds.
*/

#ifndef __FRAME_PROFILE_H
#define __FRAME_PROFILE_H
#pragma once

#ifdef HALO_CONSOLE_BRINGUP_LOG
void console_profile_stage(const char *name);
#define FRAME_STAGE(name) console_profile_stage(name)
#else
#define FRAME_STAGE(name) ((void)0)
#endif

#endif
