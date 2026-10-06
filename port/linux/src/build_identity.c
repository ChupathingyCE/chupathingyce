/*
BUILD_IDENTITY.C

Which ChupathingyCE build this is (build_identity.h). The build gives this
file the version's defines, as it does updater.c (tools/linux_build.py
updater_defines): HALO_VERSION, HALO_CHANNEL, HALO_COMMIT and
HALO_BUILD_FLAVOR.
*/

#include "platform.h"
#include "port_config.h"
#include "halo_port_limits.h"
#include "delta.h"
#include "build_identity.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#ifndef HALO_VERSION
#define HALO_VERSION "dev"
#endif
#ifndef HALO_CHANNEL
#define HALO_CHANNEL "dev"
#endif
#ifndef HALO_COMMIT
#define HALO_COMMIT "unknown"
#endif
#ifndef HALO_BUILD_FLAVOR
#define HALO_BUILD_FLAVOR "release"
#endif

#ifdef HALO_SERVER
#define BUILD_IDENTITY_NAME "ChupathingyCE Dedicated Server"
#else
#define BUILD_IDENTITY_NAME "ChupathingyCE"
#endif

#if defined(HALO_ANDROID)
#define BUILD_IDENTITY_PLATFORM "Android"
#elif defined(_WIN32)
#define BUILD_IDENTITY_PLATFORM "Windows"
#elif defined(__APPLE__)
#define BUILD_IDENTITY_PLATFORM "macOS"
#else
#define BUILD_IDENTITY_PLATFORM "Linux"
#endif

/* (the Android game's code is arm64_32, on an arm64 device) */
#if defined(__aarch64__) || defined(__arm64__) || defined(HALO_ANDROID)
#define BUILD_IDENTITY_ARCHITECTURE "arm64"
#elif defined(__x86_64__) || defined(_M_X64)
#define BUILD_IDENTITY_ARCHITECTURE "x64"
#elif defined(__i386__) || defined(_M_IX86)
#define BUILD_IDENTITY_ARCHITECTURE "x86"
#else
#define BUILD_IDENTITY_ARCHITECTURE "unknown"
#endif

static pthread_once_t identity_once = PTHREAD_ONCE_INIT;
static char identity[256];
static char user_agent[128];

/* __DATE__ ("Oct  6 2026") as 2026-10-06 */
static void build_date(char *date, size_t size)
{
	static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
	const char *compiled = __DATE__;
	char name[4];
	const char *month;
	int day = (compiled[4] == ' ' ? 0 : compiled[4] - '0') * 10 + compiled[5] - '0';

	memcpy(name, compiled, 3);
	name[3] = '\0';
	month = strstr(months, name);
	snprintf(date, size, "%.4s-%02d-%02d", compiled + 7, month ? (int)(month - months) / 3 + 1 : 0, day);
}

static void identity_make(void)
{
	char date[16];

	build_date(date, sizeof(date));
	snprintf(identity, sizeof(identity), "%s %s (%s, %s config, commit %s, built %s) %s %s", BUILD_IDENTITY_NAME,
		HALO_VERSION, HALO_CHANNEL, HALO_BUILD_FLAVOR, HALO_COMMIT, date, BUILD_IDENTITY_PLATFORM,
		BUILD_IDENTITY_ARCHITECTURE);
	snprintf(user_agent, sizeof(user_agent), "ChupathingyCE/%s (%s %s)", HALO_VERSION, BUILD_IDENTITY_PLATFORM,
		BUILD_IDENTITY_ARCHITECTURE);
}

const char *build_identity(void)
{
	pthread_once(&identity_once, identity_make);
	return identity;
}

const char *build_identity_user_agent(void)
{
	pthread_once(&identity_once, identity_make);
	return user_agent;
}

void build_identity_log(void)
{
	static int logged;
	char network[256], table[32], path[1024];
	FILE *file;

	if (logged)
		return;
	logged = 1;
	if (delta_legacy_override())
		snprintf(table, sizeof(table), "override");
	else if (delta_legacy_serial())
		snprintf(table, sizeof(table), "serial %u", delta_legacy_serial());
	else
		snprintf(table, sizeof(table), "built in");
	snprintf(network, sizeof(network), "OpenCE network version %d (joins %d-%d); Delta %d, wire %s, legacy table: %s; "
		"protocol %s", delta_legacy_announce(), delta_legacy_minimum(), delta_legacy_maximum(), DELTA_MAJOR,
		DELTA_WIRE, table, config_string("network.protocol"));
	/* debug.txt: a line between this run and the one before, and (in the
	32-bit builds, whose platform_log is standard error's alone) the header */
	snprintf(path, sizeof(path), "%s/debug.txt", platform_data_root());
	file = fopen(path, "a");
	if (file)
	{
		fputs("\n", file);
#ifndef HALO_64BIT
		fprintf(file, PLATFORM_LOG_PREFIX "%s\n" PLATFORM_LOG_PREFIX "%s\n", build_identity(), network);
#endif
		fclose(file);
	}
	platform_log("%s", build_identity());
	platform_log("%s", network);
}
