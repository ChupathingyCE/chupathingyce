/*
STUBBS_REGIONS.H
Stubbs mode: an object's region permutations past the eight Halo's object
holds (port/linux/game/stubbs_regions.c).
*/
#ifndef STUBBS_REGIONS_H
#define STUBBS_REGIONS_H

#include "cseries.h"
#include "halo_console_features.h"

enum
{
	STUBBS_MAXIMUM_REGIONS_PER_OBJECT = 16,
};

#ifndef HALO_CONSOLE_NO_STUBBS
/* the object's permutations, STUBBS_MAXIMUM_REGIONS_PER_OBJECT of them, in
Stubbs mode; NULL otherwise (Halo's object keeps its own) */
byte *stubbs_regions_permutations(long object_index);
/* a new object's row starts over */
void stubbs_regions_object_new(long object_index);
/* after a change: the object's own eight follow */
void stubbs_regions_changed(long object_index);
/* after a checkpoint revert or a saved game load: every row starts over from
its object's eight */
void stubbs_regions_forget(void);
#else
static __inline byte *stubbs_regions_permutations(long object_index) { (void)object_index; return NULL; }
static __inline void stubbs_regions_object_new(long object_index) { (void)object_index; }
static __inline void stubbs_regions_changed(long object_index) { (void)object_index; }
static __inline void stubbs_regions_forget(void) { }
#endif

#endif
