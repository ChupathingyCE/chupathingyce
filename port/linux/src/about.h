/*
ABOUT.H

The About screen's page (about.c): Chupathingy (its version, build,
platform, site and repository), the contributors, the thanks, and a row that
opens the licenses of what it uses. Drawn in the Guide's look
(ui_overlay_style.c) in a rectangle the caller gives: the Guide shows it in
every menu set (its SETTINGS' ABOUT, and the menus' ABOUT rows), the
launcher in its ABOUT section. Plain types only.
*/

#ifndef ABOUT_H
#define ABOUT_H

/* what the caller passes on (about_input) */
enum
{
	ABOUT_INPUT_UP,
	ABOUT_INPUT_DOWN,
	ABOUT_INPUT_A,
	ABOUT_INPUT_B,
};

struct about_view
{
	/* the first line shown, and the most it can be (set by about_render) */
	int scroll;
	int scroll_limit;
	/* the license list open in place of the page */
	int licenses;
	/* the LICENSES row's place when it was last drawn (none: x0 == x1) */
	float licenses_x0, licenses_y0, licenses_x1, licenses_y1;
};

/* back to the page's top */
void about_reset(struct about_view *view);
/* an input: nonzero if it was the page's (B on the license list goes back
to the page; B on the page is the caller's, which closes it) */
int about_input(struct about_view *view, int input);
/* the page between left and right, top and bottom, in the menus' 640x480,
with the look's clock set (ui_style_clock) */
void about_render(struct about_view *view, float left, float top, float right, float bottom);
/* the words of its prompts: A's (NULL: none) and B's */
const char *about_a_word(const struct about_view *view);
const char *about_b_word(const struct about_view *view, const char *close);

#endif
