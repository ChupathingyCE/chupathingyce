/*
VRR.C

display.vrr's rules, and whether the system says VRR is active (vrr.h).
The gamescope and DRM readers are the OpenH1 team's (goatbutt), moved
here from their Vulkan host so that every renderer has them.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-FileCopyrightText: 2026 goatbutt and the OpenH1 team
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "vrr.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) && !defined(__ANDROID__)
#define VRR_LINUX
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

int vrr_mode(const char *text)
{
	if (text && !strcmp(text, "on"))
		return VRR_MODE_ON;
	if (text && !strcmp(text, "auto"))
		return VRR_MODE_AUTO;
	return VRR_MODE_OFF;
}

int vrr_decide(int mode, int system, char *why, size_t size)
{
	const char *reason;
	int on;

	switch (mode)
	{
	case VRR_MODE_ON:
		on = 1;
		reason = "display.vrr is on";
		break;
	case VRR_MODE_AUTO:
		on = system == VRR_SYSTEM_GAMESCOPE_ON || system == VRR_SYSTEM_DRM_ON;
		reason = system == VRR_SYSTEM_GAMESCOPE_ON ? "auto: gamescope says VRR is active" :
			system == VRR_SYSTEM_GAMESCOPE_OFF ? "auto: gamescope says VRR is not active" :
			system == VRR_SYSTEM_DRM_ON ? "auto: the display's DRM output has VRR enabled" :
			system == VRR_SYSTEM_DRM_OFF ? "auto: no display's DRM output has VRR enabled" :
			"auto: the system says nothing of VRR (display.vrr = \"on\" for a G-SYNC or FreeSync display)";
		break;
	default:
		on = 0;
		reason = "display.vrr is off";
		break;
	}
	if (why && size)
		snprintf(why, size, "%s", reason);
	return on;
}

float vrr_frame_rate(float refresh, long max_fps)
{
	float rate = refresh > VRR_HEADROOM ? refresh - VRR_HEADROOM : 0.0f;

	if (max_fps > 0 && (rate <= 0.0f || (float)max_fps < rate))
		rate = (float)max_fps;
	return rate;
}

#ifdef VRR_LINUX
/* gamescope's VRR on its X server's root window: 1 (in use, or able and
asked for), 0 (not), -1 (no gamescope there, or no libX11) */
static int vrr_gamescope(void)
{
	typedef void *(*open_display_t)(const char *);
	typedef unsigned long (*intern_atom_t)(void *, const char *, int);
	typedef unsigned long (*root_window_t)(void *);
	typedef int (*get_property_t)(void *, unsigned long, unsigned long, long, long, int, unsigned long,
		unsigned long *, int *, unsigned long *, unsigned long *, unsigned char **);
	typedef int (*free_t)(void *);
	static int tried;
	static void *display;
	static intern_atom_t intern_atom;
	static root_window_t root_window;
	static get_property_t get_property;
	static free_t free_data;
	static const char *const names[] = { "GAMESCOPE_VRR_FEEDBACK", "GAMESCOPE_VRR_CAPABLE", "GAMESCOPE_VRR_ENABLED" };
	long values[3];
	int index, found = 0;

	if (!tried)
	{
		void *library;
		open_display_t open_display;

		tried = 1;
		if (!getenv("DISPLAY") || !(library = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL)))
			return -1;
		open_display = (open_display_t)dlsym(library, "XOpenDisplay");
		intern_atom = (intern_atom_t)dlsym(library, "XInternAtom");
		root_window = (root_window_t)dlsym(library, "XDefaultRootWindow");
		get_property = (get_property_t)dlsym(library, "XGetWindowProperty");
		free_data = (free_t)dlsym(library, "XFree");
		if (open_display && intern_atom && root_window && get_property && free_data)
			display = open_display(NULL);
	}
	if (!display)
		return -1;
	for (index = 0; index < 3; index++)
	{
		/* (only_if_exists: an X server that never had the atom is not
		gamescope's) */
		unsigned long atom = intern_atom(display, names[index], 1), type = 0, count = 0, after = 0;
		unsigned char *data = NULL;
		int format = 0;

		values[index] = -1;
		if (atom && get_property(display, root_window(display), atom, 0, 1, 0, 6 /* XA_CARDINAL */, &type,
			&format, &count, &after, &data) == 0 && data)
		{
			/* (a format 32 property comes as C longs, whatever their size) */
			if (format == 32 && count >= 1)
			{
				values[index] = *(long *)(void *)data;
				found = 1;
			}
			free_data(data);
		}
	}
	if (!found)
		return -1;
	return values[0] == 1 || (values[1] == 1 && values[2] == 1);
}

/* (drm_mode.h's, as the kernel has them: fixed-width fields, the same on
32-bit and 64-bit) */
struct vrr_card_resources
{
	uint64_t framebuffers, crtcs, connectors, encoders;
	uint32_t framebuffer_count, crtc_count, connector_count, encoder_count;
	uint32_t width_least, width_most, height_least, height_most;
};
struct vrr_object_properties
{
	uint64_t properties, values;
	uint32_t count, object, type;
};
struct vrr_property
{
	uint64_t values, enum_blobs;
	uint32_t property, flags;
	char name[32];
	uint32_t value_count, enum_blob_count;
};
#define VRR_GET_RESOURCES _IOWR('d', 0xA0, struct vrr_card_resources)
#define VRR_GET_PROPERTY _IOWR('d', 0xAA, struct vrr_property)
#define VRR_GET_OBJECT_PROPERTIES _IOWR('d', 0xB9, struct vrr_object_properties)
#define VRR_OBJECT_CRTC 0xccccccccu

/* a DRM card's CRTC on with VRR_ENABLED: 1, none 0, -1 not readable */
static int vrr_drm_card(int file)
{
	struct vrr_card_resources resources;
	uint32_t crtcs[16], crtc, count;
	int readable = 0;

	memset(&resources, 0, sizeof(resources));
	if (ioctl(file, VRR_GET_RESOURCES, &resources) || !resources.crtc_count)
		return -1;
	count = resources.crtc_count > 16 ? 16 : resources.crtc_count;
	memset(&resources, 0, sizeof(resources));
	resources.crtc_count = count;
	resources.crtcs = (uint64_t)(uintptr_t)crtcs;
	if (ioctl(file, VRR_GET_RESOURCES, &resources))
		return -1;
	if (resources.crtc_count > count)
		resources.crtc_count = count;
	for (crtc = 0; crtc < resources.crtc_count; crtc++)
	{
		struct vrr_object_properties object;
		uint32_t properties[64], index;
		uint64_t values[64];
		int active = 0, enabled = 0;

		memset(&object, 0, sizeof(object));
		object.object = crtcs[crtc];
		object.type = VRR_OBJECT_CRTC;
		object.count = 64;
		object.properties = (uint64_t)(uintptr_t)properties;
		object.values = (uint64_t)(uintptr_t)values;
		if (ioctl(file, VRR_GET_OBJECT_PROPERTIES, &object))
			continue;
		for (index = 0; index < object.count && index < 64; index++)
		{
			struct vrr_property property;

			memset(&property, 0, sizeof(property));
			property.property = properties[index];
			if (ioctl(file, VRR_GET_PROPERTY, &property))
				continue;
			property.name[sizeof(property.name) - 1] = 0;
			if (!strcmp(property.name, "ACTIVE"))
				active = values[index] != 0;
			else if (!strcmp(property.name, "VRR_ENABLED"))
			{
				readable = 1;
				enabled = values[index] != 0;
			}
		}
		if (active && enabled)
			return 1;
	}
	return readable ? 0 : -1;
}

/* any DRM card's: 1, 0, -1 (none readable) */
static int vrr_drm(void)
{
	int card, found = -1;

	for (card = 0; card < 8; card++)
	{
		char path[32];
		int file, result;

		snprintf(path, sizeof(path), "/dev/dri/card%d", card);
		file = open(path, O_RDONLY | O_CLOEXEC | O_NOCTTY);
		if (file < 0)
			continue;
		result = vrr_drm_card(file);
		close(file);
		if (result > 0)
			return 1;
		if (result == 0)
			found = 0;
	}
	return found;
}
#endif

int vrr_system(void)
{
#ifdef VRR_LINUX
	int gamescope = vrr_gamescope(), drm;

	if (gamescope >= 0)
		return gamescope ? VRR_SYSTEM_GAMESCOPE_ON : VRR_SYSTEM_GAMESCOPE_OFF;
	/* (under gamescope its word is the one: its DRM is its own) */
	drm = vrr_drm();
	if (drm >= 0)
		return drm ? VRR_SYSTEM_DRM_ON : VRR_SYSTEM_DRM_OFF;
#endif
	return VRR_SYSTEM_NONE;
}
