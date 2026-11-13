/* Explicit Halo 1 campaign metadata, shared by local and Steam packages. */
#ifndef MCC_MANIFEST_H
#define MCC_MANIFEST_H

#include "steam_workshop.h"

/* Campaign progression retains its own bound, independent of multiplayer collections. */
enum { MCC_MANIFEST_MAXIMUM_MISSIONS = 64 };

/* The supported mission kind is an ASCII enum, independent of capitalization.
 * Callers still reject non-string values and preserve the original JSON bytes. */
int mcc_manifest_default_kind(const char *kind);

struct mcc_manifest
{
	char title[STEAM_WORKSHOP_TITLE_SIZE];
	int count;
	struct steam_workshop_map_text missions[MCC_MANIFEST_MAXIMUM_MISSIONS];
};

/* Host paths only. Returns 1 for a complete manifest, 0 when absent, -1
 * when invalid. Missing map binaries remain explicit entries: never skip a
 * mission to advance to a later one. reason is always NUL terminated. */
int mcc_manifest_load(const char *folder, struct mcc_manifest *manifest, char reason[128]);
/* Resolve the owning metadata folder through the existing MCC path mapping.
 * A local override never borrows its campaign from the Steam installation. */
int mcc_manifest_for_level(const char *level, struct mcc_manifest *manifest, char reason[128]);
/* Actual selected package root, shared by MCC metadata consumers only. */
int mcc_manifest_package_root(const char *level, char path[1024]);
/* OpenCE's metadata digest of the level's package (the settings record's
 * last 32 bytes on an MCC map, from their network 43 on): 1 with it in
 * digest, else 0 with digest all zero (a map outside a package folder, or a
 * plan this build doesn't make: a campaign level's) */
int mcc_manifest_metadata_digest(const char *level, unsigned char digest[32]);

#endif
