/*
STUBBS_LOOKS.H

Stubbs mode's looks (port/linux/game/stubbs_looks.c): what its maps' tags
need, beyond their layouts, to be drawn as the retail game drew them. Called
only in Stubbs mode.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef STUBBS_LOOKS_H
#define STUBBS_LOOKS_H

/* (HALO_CONSOLE_NO_STUBBS: which build has Stubbs mode, read here so
every unit sees the same side of the switch) */
#include "halo_console_features.h"

#ifndef HALO_CONSOLE_NO_STUBBS
/* Stubbs's tags just read and converted (cache_files.c, after
stubbs_mode_convert_tags): what the looks need noted or rewritten; nothing
outside Stubbs mode */
void stubbs_looks_convert_tags(void *tag_cache, unsigned long tag_data_size);

/* the image effect (imef tag index) a unit's (bipd or vehi tag index) HUD
interface names, read before any conversion: NONE for none */
long stubbs_looks_unit_image_effect(long unit_definition_index);

/* a scenery definition (scen tag index) that names a vegetation (vege tag
index), noted as the tags load (stubbs_vegetation.c); and none, for a new
map */
void stubbs_vegetation_note(long scenery_index, long vegetation_index);
void stubbs_vegetation_reset(void);

/* draws the vegetation (b10's corn) around each placed scenery that names
one (render_objects.c, with the objects): nothing outside Stubbs mode */
void stubbs_vegetation_render(void);

/* whether an object is lit where its body is (a unit: at its first node,
the pelvis) rather than at its origin, and refreshed as it moves
(object_lights.c, render_objects.c); and that point */
boolean stubbs_looks_object_lit_at_body(long object_index);
void stubbs_looks_object_body_point(long object_index, union real_point3d *point);

/* a new map's tags: the image effect the scripts left is cleared
(stubbs_image_effects.c) */
void stubbs_image_effects_reset(void);
#else
static __inline void stubbs_looks_convert_tags(void *tag_cache, unsigned long tag_data_size) { (void)tag_cache; (void)tag_data_size; }
static __inline long stubbs_looks_unit_image_effect(long unit_definition_index) { (void)unit_definition_index; return -1; }
static __inline void stubbs_image_effects_reset(void) { }
static __inline void stubbs_vegetation_note(long scenery_index, long vegetation_index) { (void)scenery_index; (void)vegetation_index; }
static __inline void stubbs_vegetation_reset(void) { }
static __inline void stubbs_vegetation_render(void) { }
static __inline boolean stubbs_looks_object_lit_at_body(long object_index) { (void)object_index; return FALSE; }
static __inline void stubbs_looks_object_body_point(long object_index, union real_point3d *point) { (void)object_index; (void)point; }
#endif

#endif
