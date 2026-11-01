/*
CE_EFFECT_FLAGS.H

An effect tag's flags (effect_definitions.h) as a Custom Edition or HaloMD
map has them, read as the Xbox has them (ce_repairs.c). Gearbox's tools for
Halo PC kept the Xbox's "deleted when attachment deactivates" in bit 0, made
the Xbox's "must be deterministic" bit (1) one that Halo PC's engine never
reads ("required for gameplay"), and moved "must be deterministic" to bit 2.
Halo PC's engine (haloceded 1.0.10: effect_new and effect_update) tests bit 2
alone. Read as the Xbox reads them, every one of these maps' effects needed
no determinism, and a dedicated server, which sees nothing locally, never ran
those that set up a map's game types by their damage (ugoboom's Coldsnap).

So bit 2 is copied to bit 1, Gearbox's own bit 1 is cleared, and bits 2 and
up are kept (the Xbox's engine reads none of them). Reading the flags this
way twice gives what reading them once does.
*/

#ifndef __CE_EFFECT_FLAGS_H
#define __CE_EFFECT_FLAGS_H
#pragma once

/* ---------- constants */

enum
{
	/* (the effect's flags: the first field of its tag) */
	CE_EFFECT_FLAGS_OFFSET = 0x00,
	/* the Xbox's (effects.c: effect_definition_flags) */
	CE_EFFECT_XBOX_DELETED_WHEN_INACTIVE = 0x01,
	CE_EFFECT_XBOX_MUST_BE_DETERMINISTIC = 0x02,
	/* Gearbox's */
	CE_EFFECT_GEARBOX_DELETED_WHEN_INACTIVE = 0x01,
	CE_EFFECT_GEARBOX_REQUIRED_FOR_GAMEPLAY = 0x02,
	CE_EFFECT_GEARBOX_MUST_BE_DETERMINISTIC = 0x04,
};

/* ---------- inline code */

/* a Custom Edition or HaloMD effect's flags, as the Xbox's engine reads
them */
static inline unsigned long ce_effect_flags_from_gearbox(
	unsigned long flags)
{
	unsigned long result = flags & ~(unsigned long)CE_EFFECT_GEARBOX_REQUIRED_FOR_GAMEPLAY;

	if (flags & CE_EFFECT_GEARBOX_MUST_BE_DETERMINISTIC)
		result |= CE_EFFECT_XBOX_MUST_BE_DETERMINISTIC;
	return result;
}

#endif
