/*
ARK_NAMES.H

The one place Project Ark's name is spelled for players: the setup screen's
Play list, its tag there, and its messages all use these. Renaming the mode
is a change to these lines alone (tools/test_ark_mode.py checks no other
file spells it).
*/

#ifndef ARK_NAMES_H
#define ARK_NAMES_H

/* the mode's name as players see it */
#define ARK_MODE_DISPLAY_NAME "Project Ark: Stage 1"

/* its short tag in the Play list (four letters at most) */
#define ARK_MODE_DISPLAY_TAG "ARK"

/* whether the mode is offered at all: 0 until it plays (its levels come in
the following steps), so a build never lists a mode with nothing in it.
A local build may set it with -DARK_MODE_PLAYABLE=1 to try the setup
screen's row */
#ifndef ARK_MODE_PLAYABLE
#define ARK_MODE_PLAYABLE 0
#endif

#endif
