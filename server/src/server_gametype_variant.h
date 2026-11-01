/*
SERVER_GAMETYPE_VARIANT.H

Game type files (server_config.h's struct server_gametype) as the game's own
gametypes: a base's variant and PC options with a file's settings given to
them, and back. The dedicated server plays its admin/gametypes files by
them (server_admin.c), and the game hosts and saves the player's (its save
folder's gametypes/, port/linux/game/gametype_files.c) by the same code, so
a file means the same game wherever it is played.
*/

#ifndef SERVER_GAMETYPE_VARIANT_H
#define SERVER_GAMETYPE_VARIANT_H

struct game_variant;
struct game_variant_options;
struct server_gametype;

/* a game type's settings given to its base's variant and PC options (those
it does not give left as they are), as the game's gametype editor gives
them */
void server_gametype_variant_apply(struct server_gametype const *gametype, struct game_variant *variant,
	struct game_variant_options *options);

/* the game a game type makes: its base's variant and PC options, with its
settings given to them */
void server_gametype_variant_build(struct server_gametype const *gametype, struct game_variant *variant,
	struct game_variant_options *options);

/* a setting's value in a variant and its PC options, as a file gives it */
long server_gametype_variant_value(short field, struct game_variant const *variant,
	struct game_variant_options const *options);

/* the built-in game type a variant is closest to (its index): its game
engine's, played in teams as it is if there is one */
int server_gametype_variant_base(struct game_variant const *variant);

/* a variant and its PC options as a game type file: base the built-in it
is closest to, its name (title, printable ASCII, 11 characters at most; its
own if NULL), and the settings where it differs from that base, each in its
setting's range. What a file cannot hold is left as the base's. */
void server_gametype_variant_capture(struct game_variant const *variant, struct game_variant_options const *options,
	char const *title, struct server_gametype *gametype);

#endif
