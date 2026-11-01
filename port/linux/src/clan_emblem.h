/*
CLAN_EMBLEM.H

A clan's emblem (docs/delta.md, Clans): a background shape in the
background color, and a foreground shape in the primary and secondary
colors, from Halo's 18 armor colors. The pieces are the site's
(clan_emblem_table.h, made by tools/clan_emblem_table.py), so the game
draws what the site does.
*/

#ifndef __CLAN_EMBLEM_H
#define __CLAN_EMBLEM_H

struct clan_emblem
{
	/* the foreground and background shapes; the primary, secondary and
	background colors (armor color indexes) */
	int foreground;
	int background;
	int primary;
	int secondary;
	int background_color;
};

/* the emblem drawn size by size (at most CLAN_EMBLEM_MAXIMUM_SIZE) into
pixels (RGBA, 4 bytes a pixel, rows from the top, edges smoothed);
nothing that isn't an emblem's is drawn */
#define CLAN_EMBLEM_MAXIMUM_SIZE 160
void clan_emblem_render(const struct clan_emblem *emblem, int size, unsigned char *pixels);

/* a number for each different emblem (never 0) */
int clan_emblem_serial(const struct clan_emblem *emblem);

#endif
