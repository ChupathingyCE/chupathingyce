/*
MAP_STOCK_PAIRS.H

The stock multiplayer maps that Halo PC retail (cache version 7) and Custom
Edition (cache version 609) both ship, whose simulation data is the same in
the two files, so that a game whose host plays one edition's file can be
joined with the other's. Each entry gives the map's file name and each
edition's file version (the first 32 bits of its whole-file hash, as a host
of network version 29 sends it: map_archive.h).

Only pairs checked with tools/compare_stock_maps.py are listed (docs/
stock-map-pairs.md has how, and the result for each map): the scenario, the
objects, units, weapons, vehicles, projectiles, damage effects, physics,
collision and BSP of both files are the same bytes once their pointers and
tag identifiers are set aside. Never a name alone. A map whose two files
differ in any of those is not listed: its game is joined with the host's
edition of the file only, and the player is told which one.
*/

#ifndef MAP_STOCK_PAIRS_H
#define MAP_STOCK_PAIRS_H

static struct
{
	char const *file;
	unsigned long halo_pc_version;
	unsigned long custom_edition_version;
} const map_stock_pairs[] =
{
	/* (none checked yet: the Halo PC retail files were not at hand when
	the comparison was written) */
	{ NULL, 0, 0 },
};

#endif
