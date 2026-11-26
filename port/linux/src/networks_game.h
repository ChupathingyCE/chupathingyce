/*
NETWORKS_GAME.H

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later

The networks this machine trusts, kept for the game and the dedicated
server (networks_game.c; the rules are networks.c's): the home network
(identity_key.h's domain and root keys), the signed list of networks
(fetched from network.browser_url's /v1/delta/networks and .sig, cached as
delta_networks.signed in the save root), and the local settings
network.networks_trust and network.networks_block.
*/

#ifndef NETWORKS_GAME_H
#define NETWORKS_GAME_H

#include "networks.h"
/* (HALO_DELTA_PEER_IDENTITY: the builds that have this) */
#include "halo_console_features.h"

/* the list read and the settings taken, and (with list keys and a site)
the list fetched in the background from now on; once */
void networks_start(void);
/* the home network's root keys, for this copy's own certificates (count
of them into *count) */
const unsigned char (*networks_home_roots(int *count))[NETWORKS_KEY_SIZE];
/* the trusted network whose root key (in date at now) signed an issuer
certificate (IDENTITY_ISSUER_SIZE bytes) that came with machine's proof or
card: 1 for the home network, 2 for another, 0 for none (or past the
machine's new issuer certificates a minute, or an issuer key found under
two networks: networks.h's networks_issuer_lookup); its domain
(NETWORKS_DOMAIN_SIZE) and its root keys in date (NETWORKS_ROOTS_MOST at
most, count into *root_count) */
int networks_check_issuer(const unsigned char *issuer, int machine, unsigned long now, char *domain,
	unsigned char (*roots)[NETWORKS_KEY_SIZE], int *root_count);
/* a count that changes whenever the trusted networks do (a newer list, an
issuer key refused) */
unsigned long networks_generation(void);

#endif
