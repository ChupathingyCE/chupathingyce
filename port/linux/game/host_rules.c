/*
HOST_RULES.C

The host's rules for its players' extras (docs/delta.md, Host rules). The
game plays as the Xbox did by default; an extra that changes what a player
sees of the others (names above heads, a follow camera on them while
waiting to respawn) is the host's to allow, so that one rule holds for
everyone in the game:

- **A host** says its rules over Delta Peer (HOST_RULES) to each client
  that agreed to host_rules, when they change and every few seconds. A
  player's host reads config.toml (network.allow_player_names,
  network.allow_follow_camera); a dedicated
  server its settings (host_rules_set_*).
- **A client** plays by its host's rules as last heard. A host that says
  nothing (OpenCE's, an older Chupathingy, a console without Delta) gets
  the Xbox's rules: no extras.
- **A game played alone** (split screen, a campaign) is its own host's.

HOST_RULES also carries the host's team change rule (team_change.c), so a
client knows before asking whether its host takes team changes, whether
late joiners wait for the next game (late_join.c), and whether the host's
joins are locked (sv_lock).
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "../src/delta_wire.h"
#ifdef HALO_GAME_BROWSER
#include "../src/delta_peer.h"
#define HOST_RULES_DELTA
#endif
#include "team_change.h"

#include "host_rules.h"

/* the platform layer's (port/linux/src/port_config.c) */
int config_boolean(char const *name);
/* (network_server_manager.c's: the host's game record) */
struct network_game *network_game_server_get_game(struct network_game_server *server);
#ifdef HOST_RULES_DELTA
/* (delta_peer_game.c's: MOD_STATE told again, the lock in it) */
void delta_peer_game_moderation_roles_changed(void);
#endif

enum
{
	/* a lock outlives its game this long, then joins open again: hosting
	ended, or nobody else is in (a map's change or a moment between games
	is shorter) */
	HOST_RULES_LOCK_EMPTY_MILLISECONDS = 10 * 1000,
};

static struct
{
	/* a dedicated server's rules (else config.toml's) */
	boolean player_names_set;
	boolean player_names;
	boolean follow_camera_set;
	boolean follow_camera;
	boolean late_join_hold_set;
	boolean late_join_hold;
	/* (the host's, any host's) */
	boolean joins_locked;
	/* when nobody it keeps out could matter began (0: not now) */
	unsigned long lock_empty_since;
} host_rules;

void host_rules_set_player_names(boolean allowed)
{
	host_rules.player_names_set = TRUE;
	host_rules.player_names = allowed ? TRUE : FALSE;
}

void host_rules_set_follow_camera(boolean allowed)
{
	host_rules.follow_camera_set = TRUE;
	host_rules.follow_camera = allowed ? TRUE : FALSE;
}

void host_rules_set_late_join_hold(boolean wait)
{
	host_rules.late_join_hold_set = TRUE;
	host_rules.late_join_hold = wait ? TRUE : FALSE;
}

void host_rules_set_joins_locked(boolean locked)
{
	host_rules.joins_locked = locked ? TRUE : FALSE;
	host_rules.lock_empty_since = 0;
}

boolean host_rules_lock_has_players(void)
{
	struct network_game_server *server = global_network_game_server_get();
	struct network_game *game = server ? network_game_server_get_game(server) : NULL;

	/* (hosting ended: nobody to keep anyone from) */
	if (!game)
		return FALSE;
#ifdef HALO_SERVER
	/* (a dedicated server: anyone in, Custom Edition players too) */
	return game->player_count > 0;
#else
	/* (a player's host: someone of another machine) */
	return game->machine_count > 1;
#endif
}

/* (each frame) a lock ends when hosting does, or once nobody is in for a
while: a lock is for the players in the game, and never outlives them */
static void host_rules_lock_update(void)
{
	unsigned long now;

	if (!host_rules.joins_locked)
		return;
	if (host_rules_lock_has_players())
	{
		host_rules.lock_empty_since = 0;
		return;
	}
	now = system_milliseconds();
	if (!now)
		now = 1;
	if (!host_rules.lock_empty_since)
	{
		host_rules.lock_empty_since = now;
		return;
	}
	if (now - host_rules.lock_empty_since < HOST_RULES_LOCK_EMPTY_MILLISECONDS)
		return;
	host_rules_set_joins_locked(FALSE);
	error(_error_silent, "host rules: joins unlocked (nobody left in the game)");
#ifdef HOST_RULES_DELTA
	delta_peer_game_moderation_roles_changed();
#endif
}

boolean host_rules_joins_locked(void)
{
	return host_rules.joins_locked;
}

int host_rules_joins_locked_now(void)
{
	return host_rules.joins_locked ? 1 : 0;
}

boolean host_rules_host_late_join_hold(void)
{
	if (host_rules.late_join_hold_set)
		return host_rules.late_join_hold;
	return config_boolean("network.late_join_hold") ? TRUE : FALSE;
}

boolean host_rules_host_follow_camera(void)
{
	if (host_rules.follow_camera_set)
		return host_rules.follow_camera;
	return config_boolean("network.allow_follow_camera") ? TRUE : FALSE;
}

boolean host_rules_host_player_names(void)
{
	if (host_rules.player_names_set)
		return host_rules.player_names;
	return config_boolean("network.allow_player_names") ? TRUE : FALSE;
}

/* whether this machine is a client of another's game (FALSE: it hosts, or
plays alone) */
static boolean host_rules_joined(void)
{
	return global_network_game_server_get() == NULL && global_network_game_client_get() != NULL;
}

#ifdef HOST_RULES_DELTA
/* the host's rules as its client heard them (FALSE: none heard) */
static boolean host_rules_heard(struct delta_wire_host_rules *rules)
{
	return delta_peer_game_host_rules(rules) != 0;
}
#endif

boolean host_rules_player_names(void)
{
#ifdef HOST_RULES_DELTA
	struct delta_wire_host_rules rules;
#endif

	if (!host_rules_joined())
		return host_rules_host_player_names();
#ifdef HOST_RULES_DELTA
	if (host_rules_heard(&rules))
		return (rules.flags & DELTA_WIRE_HOST_PLAYER_NAMES) != 0;
#endif
	return FALSE;
}

boolean host_rules_follow_camera(void)
{
#ifdef HOST_RULES_DELTA
	struct delta_wire_host_rules rules;
#endif

	if (!host_rules_joined())
		return host_rules_host_follow_camera();
#ifdef HOST_RULES_DELTA
	if (host_rules_heard(&rules))
		return (rules.flags & DELTA_WIRE_HOST_FOLLOW_CAMERA) != 0;
#endif
	return FALSE;
}

boolean host_rules_late_join_hold(void)
{
#ifdef HOST_RULES_DELTA
	struct delta_wire_host_rules rules;
#endif

	if (!host_rules_joined())
		return host_rules_host_late_join_hold();
#ifdef HOST_RULES_DELTA
	if (host_rules_heard(&rules))
		return (rules.flags & DELTA_WIRE_HOST_LATE_JOIN_HOLD) != 0;
#endif
	return FALSE;
}

boolean host_rules_host_says_locked(void)
{
#ifdef HOST_RULES_DELTA
	struct delta_wire_host_rules rules;

	if (host_rules_joined() && host_rules_heard(&rules))
		return (rules.flags & DELTA_WIRE_HOST_JOINS_LOCKED) != 0;
#endif
	return host_rules_joined() ? FALSE : host_rules.joins_locked;
}

boolean host_rules_team_change(int *policy)
{
#ifdef HOST_RULES_DELTA
	struct delta_wire_host_rules rules;

	if (host_rules_joined() && host_rules_heard(&rules))
	{
		*policy = rules.team_change == DELTA_WIRE_HOST_TEAM_CHANGE_ALLOW ? _team_change_allow :
			rules.team_change == DELTA_WIRE_HOST_TEAM_CHANGE_BALANCED ? _team_change_balanced : _team_change_off;
		return TRUE;
	}
#endif
	*policy = _team_change_off;
	return FALSE;
}

void host_rules_update(void)
{
	host_rules_lock_update();
	{
#ifdef HOST_RULES_DELTA
	struct delta_wire_host_rules rules;
	int policy = team_change_host_policy();
	int margin = team_change_host_margin();

	csmemset(&rules, 0, sizeof(rules));
	if (host_rules_host_player_names())
		rules.flags |= DELTA_WIRE_HOST_PLAYER_NAMES;
	if (host_rules_host_follow_camera())
		rules.flags |= DELTA_WIRE_HOST_FOLLOW_CAMERA;
	if (host_rules_host_late_join_hold())
		rules.flags |= DELTA_WIRE_HOST_LATE_JOIN_HOLD;
	if (host_rules.joins_locked)
		rules.flags |= DELTA_WIRE_HOST_JOINS_LOCKED;
	rules.team_change = (unsigned char)(policy == _team_change_allow ? DELTA_WIRE_HOST_TEAM_CHANGE_ALLOW :
		policy == _team_change_balanced ? DELTA_WIRE_HOST_TEAM_CHANGE_BALANCED : DELTA_WIRE_HOST_TEAM_CHANGE_OFF);
	rules.team_change_margin = (unsigned char)(margin < 0 ? 0 : margin > DELTA_WIRE_HOST_TEAM_CHANGE_MAXIMUM_MARGIN ?
		DELTA_WIRE_HOST_TEAM_CHANGE_MAXIMUM_MARGIN : margin);
	/* (sent only while this machine hosts: delta_peer_game.c's host frame;
	a client's Delta Peer logs what its host says) */
	delta_peer_game_set_host_rules(&rules);
#endif
	}
}
