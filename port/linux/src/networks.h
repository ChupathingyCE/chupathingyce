/*
NETWORKS.H

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later

The networks whose players' names a game checks (docs/delta/federation.md;
delta-spec's link.md, "Networks"): each network is a domain with its own
root keys, which sign its issuer certificates (CISS) exactly as
identity_key.h's keys sign the home network's. A certificate's network is
the one whose root key signed its CISS; nothing in a certificate names it,
so certificates made before federation read as the home network's.

Here: a network's domain, names as handle:domain, the signed list of
networks (its signature, then its JSON, read strictly), and the set of
networks a game trusts: its home network (built in, never changed by a
list or a setting), the list's, local additions and local removals.

No SDL, no game, no files and no threads here (networks_game.c keeps the
list and the settings), so its tests build anywhere
(port/linux/tests/networks_test.c, tools/test_networks.py) and its parser
is a fuzz target (tools/tests/host_fuzz/networks).
*/

#ifndef NETWORKS_H
#define NETWORKS_H

enum
{
	NETWORKS_KEY_SIZE = 32,
	NETWORKS_SIGNATURE_SIZE = 64,
	/* a domain: 4 to 253 characters, and its end */
	NETWORKS_DOMAIN_LEAST = 4,
	NETWORKS_DOMAIN_MOST = 253,
	NETWORKS_DOMAIN_SIZE = NETWORKS_DOMAIN_MOST + 1,
	/* a network's root keys */
	NETWORKS_ROOTS_MOST = 4,
	/* a list's networks, and its document's bytes */
	NETWORKS_LIST_MOST = 64,
	NETWORKS_DOCUMENT_MOST = 16384,
	/* the signed form: the signature's 128 hex digits, a line feed, the
	document */
	NETWORKS_SIGNED_MOST = NETWORKS_SIGNATURE_SIZE * 2 + 1 + NETWORKS_DOCUMENT_MOST,
	/* local additions taken, and removals */
	NETWORKS_LOCAL_MOST = 16,
	NETWORKS_REMOVED_MOST = 64,
	/* every network a game may trust: its home, the list's, the local ones */
	NETWORKS_TRUSTED_MOST = 1 + NETWORKS_LIST_MOST + NETWORKS_LOCAL_MOST,
	/* a handle (identity.h's IDENTITY_HANDLE_MOST), a colon, a domain */
	NETWORKS_NAME_MOST = 20 + 1 + NETWORKS_DOMAIN_MOST,
};

/* a list's serial: 1 to this (0xFFFFFFFF is a machine taking none) */
#define NETWORKS_SERIAL_MOST 0xFFFFFFFEUL

struct networks_root
{
	unsigned char key[NETWORKS_KEY_SIZE];
	/* the last Unix second it vouches for an issuer certificate; 0: no end */
	unsigned long until;
};

struct networks_network
{
	char domain[NETWORKS_DOMAIN_SIZE];
	int root_count;
	struct networks_root roots[NETWORKS_ROOTS_MOST];
	/* (where it came from: 0 home, 1 the list, 2 a local addition) */
	int source;
};

struct networks_list
{
	unsigned long serial;
	unsigned long issued;
	int count;
	struct networks_network networks[NETWORKS_LIST_MOST];
};

struct networks_trust
{
	int count;
	/* the home network first, always */
	struct networks_network networks[NETWORKS_TRUSTED_MOST];
};

/* ---------- names */

/* whether text (length bytes) is a network's domain: lowercase ASCII, 4 to
253 characters, two labels or more, each 1 to 63 of a-z 0-9 - neither
starting nor ending with -, none starting with xn--, the last 2 to 63
letters (so never an address) */
int networks_domain_valid(const char *text, int length);
/* a name as typed (to find or add a player): spaces around it left out,
read in any capitals, "handle:domain", or a handle alone for home. 1 and
the lowercase handle (21 bytes) and domain (NETWORKS_DOMAIN_SIZE) out, or 0
for one that isn't */
int networks_name_parse(const char *typed, const char *home, char *handle, char *domain);
/* what shows for a verified player (name: the handle in its owner's
capitals): the name alone for home's players, name:domain for any other
network's. Its length (cut to size - 1) */
int networks_shown(const char *name, const char *domain, const char *home, char *out, int size);

/* ---------- the signed list of networks */

/* a signature as its file has it: 128 lowercase hex digits and one line
feed or none. 1 and its bytes (NETWORKS_SIGNATURE_SIZE of them, so signature
holds that many), or 0 */
int networks_signature_text(const char *text, int length, unsigned char *signature);
/* the document read (its signature checked elsewhere): 1 and the list, or 0
and why (a short phrase for the log) */
int networks_list_parse(const char *document, int size, struct networks_list *list, const char **why);
/* the document checked: no more than NETWORKS_DOCUMENT_MOST bytes, signed
(Ed25519 over "open network list v1\n" and the document) by one of the
list keys (count of them; an all-zero one is none), read, and its serial's
epoch (its top byte) at most that key's last (last_epochs, in the keys'
order). 1 and the list, or 0 and why */
int networks_list_check(const char *document, int size, const unsigned char *signature,
	const unsigned char (*keys)[NETWORKS_KEY_SIZE], const unsigned char *last_epochs, int key_count,
	struct networks_list *list, const char **why);
/* the same from the signed form (signature, line feed, document) */
int networks_signed_check(const char *signed_list, int size, const unsigned char (*keys)[NETWORKS_KEY_SIZE],
	const unsigned char *last_epochs, int key_count, struct networks_list *list, const char **why);

/* ---------- the networks a game trusts */

/* trust made: the home network (its domain and root keys, which nothing
changes), the list's networks (list may be NULL), then the local
additions (text: entries separated by commas, each a domain and 1 to 4
root keys of 64 hex digits, separated by spaces; the first
NETWORKS_LOCAL_MOST entries), less the local removals (text: domains
separated by commas; never home). A network whose domain is already
trusted, or with a root key another trusted network has, is left out.
say (may be NULL) is told each one left out, and why */
void networks_trust_make(struct networks_trust *trust, const char *home,
	const unsigned char (*home_roots)[NETWORKS_KEY_SIZE], int home_root_count, const struct networks_list *list,
	const char *additions, const char *removals, void (*say)(void *context, const char *line), void *context);
/* the trusted network one of whose root keys signed the issuer certificate,
that key in date at now (until 0 or now <= until): its index in trust (0 is
home), or -1 for none. roots and root_count, when not NULL, get that
network's root keys in date */
int networks_issuer_network(const struct networks_trust *trust, const unsigned char *issuer, int issuer_size,
	unsigned long now, unsigned char (*roots)[NETWORKS_KEY_SIZE], int *root_count);

/* ---------- what a game remembers of the issuer certificates it saw
(delta-spec's link.md, "How a game checks a name") */

enum
{
	NETWORKS_ISSUER_SIZE = 112,
	/* issuer certificates remembered (their network, or none) */
	NETWORKS_SEEN_MOST = 256,
	/* new issuer certificates looked for from one machine a minute */
	NETWORKS_LOOKS_A_MINUTE = 4,
	NETWORKS_MACHINES = 64,
	/* issuer keys refused for turning up under two networks */
	NETWORKS_REFUSED_MOST = 64,
	/* home's own issuer keys remembered (never refused) */
	NETWORKS_HOME_KEYS_MOST = 16,
	/* networks_issuer_lookup's answers past a network's index */
	NETWORKS_NONE = -1,
	NETWORKS_LIMITED = -2,
	NETWORKS_TWO_NETWORKS = -3,
};

struct networks_memory
{
	struct
	{
		unsigned char issuer[NETWORKS_ISSUER_SIZE];
		/* its network's index in trust (-1: none; NETWORKS_TWO_NETWORKS:
		another network's claim on one of home's issuer keys, refused) and
		the root key's */
		int network, root;
		int used;
	} seen[NETWORKS_SEEN_MOST];
	int next;
	unsigned char refused[NETWORKS_REFUSED_MOST][NETWORKS_KEY_SIZE];
	int refused_count;
	/* home's issuer keys proven so far (signed by a home root key in date,
	the certificate in date): no other network's claim on one is taken */
	unsigned char home_keys[NETWORKS_HOME_KEYS_MOST][NETWORKS_KEY_SIZE];
	int home_key_count, home_key_next;
	/* (each machine's looks this minute: minute is now / 60 + 1, 0 none) */
	struct
	{
		unsigned long minute;
		int count, said;
	} machines[NETWORKS_MACHINES];
	/* counts each issuer key refused */
	unsigned long changes;
};

/* (trust made again: what was found for it forgotten; the refused issuer
keys, home's issuer keys and the machines' counts are kept) */
void networks_memory_forget(struct networks_memory *memory);
/* networks_issuer_network for a proof or card from machine (0 to
NETWORKS_MACHINES - 1; any other shares the last's count), remembered: an
issuer certificate seen before is answered from memory; one not seen
before is looked for only NETWORKS_LOOKS_A_MINUTE times a minute for a
machine (past that NETWORKS_LIMITED). One issuer key, one network, and only
claims fully proven count (the root key in date, the signature good, the
certificate in date): another network's claim on one of home's issuer keys
is refused and home's stays (NETWORKS_TWO_NETWORKS for the claim); a key
proven under two other networks is refused for both from then on
(NETWORKS_TWO_NETWORKS). Home's own issuer is never refused. The network's
index, or NETWORKS_NONE */
int networks_issuer_lookup(const struct networks_trust *trust, struct networks_memory *memory,
	const unsigned char *issuer, int issuer_size, unsigned long now, int machine,
	unsigned char (*roots)[NETWORKS_KEY_SIZE], int *root_count);

#endif
