/*
HOST_RULES.C

The host's rules for its players' extras (docs/delta.md, Host rules). The
game plays as the Xbox did by default; an extra that changes what a player
sees of the others (names above heads) is the host's to allow, so that one
rule holds for everyone in the game:

- **A host** says its rules over Delta Peer (HOST_RULES) to each client
  that agreed to host_rules, when they change and every few seconds. A
  player's host reads config.toml (network.allow_player_names); a dedicated
  server its settings (host_rules_set_*).
- **A client** plays by its host's rules as last heard. A host that says
  nothing (OpenCE's, an older Chupathingy, a console without Delta) gets
  the Xbox's rules: no extras.
- **A game played alone** (split screen, a campaign) is its own host's.

HOST_RULES also carries the host's team change rule (team_change.c), so a
client knows before asking whether its host takes team changes.
*/

#include "cseries.h"
#include "networking/network_game_globals.h"
#include "../src/delta_wire.h"
#ifdef HALO_GAME_BROWSER
#include "../src/delta_peer.h"
#define HOST_RULES_DELTA
#endif
#include "team_change.h"

#include "host_rules.h"

/* the platform layer's (port/linux/src/port_config.c) */
int config_boolean(char const *name);

static struct
{
	/* a dedicated server's rules (else config.toml's) */
	boolean player_names_set;
	boolean player_names;
} host_rules;

void host_rules_set_player_names(boolean allowed)
{
	host_rules.player_names_set = TRUE;
	host_rules.player_names = allowed ? TRUE : FALSE;
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

void host_rules_update(void)
{
#ifdef HOST_RULES_DELTA
	struct delta_wire_host_rules rules;
	int policy = team_change_host_policy();
	int margin = team_change_host_margin();

	csmemset(&rules, 0, sizeof(rules));
	if (host_rules_host_player_names())
		rules.flags |= DELTA_WIRE_HOST_PLAYER_NAMES;
	rules.team_change = (unsigned char)(policy == _team_change_allow ? DELTA_WIRE_HOST_TEAM_CHANGE_ALLOW :
		policy == _team_change_balanced ? DELTA_WIRE_HOST_TEAM_CHANGE_BALANCED : DELTA_WIRE_HOST_TEAM_CHANGE_OFF);
	rules.team_change_margin = (unsigned char)(margin < 0 ? 0 : margin > DELTA_WIRE_HOST_TEAM_CHANGE_MAXIMUM_MARGIN ?
		DELTA_WIRE_HOST_TEAM_CHANGE_MAXIMUM_MARGIN : margin);
	/* (sent only while this machine hosts: delta_peer_game.c's host frame;
	a client's Delta Peer logs what its host says) */
	delta_peer_game_set_host_rules(&rules);
#endif
}
