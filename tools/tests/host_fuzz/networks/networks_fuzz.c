/*
NETWORKS_FUZZ.C

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later

libFuzzer's entry for the networks a game trusts (port/linux/src/networks.c;
tools/test_host_fuzz.py builds it). Each input, in an allocation of its own
size (so that a read past it is caught), is read as everything networks.c
takes from outside: a list's document (networks_list_parse), the signed form
(networks_signed_check, with a test list key: it never verifies, but the
size and signature text are read first), the local settings
(networks_trust_make's additions and removals) and a typed name.

What it must hold, or the fuzzer traps: a list read has 0 to 64 networks,
each a valid domain with 1 to 4 root keys, no key twice, a serial in range;
trust has its home first, at most every network, each a domain no other
has; a name read is a handle and a domain; an issuer certificate looked up is
answered with a trusted network or why not.
*/

#include "networks.h"
#include "identity.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void check_list(const struct networks_list *list)
{
	int index, root, other, other_root;

	if (list->count < 0 || list->count > NETWORKS_LIST_MOST || list->serial < 1 ||
		list->serial > NETWORKS_SERIAL_MOST)
	{
		__builtin_trap();
	}
	for (index = 0; index < list->count; index++)
	{
		const struct networks_network *network = &list->networks[index];

		if (!networks_domain_valid(network->domain, (int)strlen(network->domain)) || network->root_count < 1 ||
			network->root_count > NETWORKS_ROOTS_MOST)
		{
			__builtin_trap();
		}
		for (root = 0; root < network->root_count; root++)
		{
			for (other = 0; other < list->count; other++)
			{
				for (other_root = 0; other_root < list->networks[other].root_count; other_root++)
				{
					if ((other != index || other_root != root) &&
						!memcmp(list->networks[other].roots[other_root].key, network->roots[root].key, NETWORKS_KEY_SIZE))
					{
						__builtin_trap();
					}
				}
			}
		}
	}
}

static void check_trust(const struct networks_trust *trust, const char *home)
{
	int index, other;

	if (trust->count < 1 || trust->count > NETWORKS_TRUSTED_MOST || strcmp(trust->networks[0].domain, home))
		__builtin_trap();
	for (index = 1; index < trust->count; index++)
	{
		if (!networks_domain_valid(trust->networks[index].domain, (int)strlen(trust->networks[index].domain)))
			__builtin_trap();
		for (other = 0; other < index; other++)
		{
			if (!strcmp(trust->networks[other].domain, trust->networks[index].domain))
				__builtin_trap();
		}
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static struct networks_list list;
	static struct networks_trust trust;
	static const unsigned char keys[2][32] = { { 0x39, 0xef, 0xbe, 0x26 }, { 0x20, 0x6c } };
	static const unsigned char epochs[2] = { 0, 255 };
	static const unsigned char home_roots[1][32] = { { 0x12, 0x76 } };
	char *copy = (char *)malloc(size ? size : 1), *text, handle[21], domain[NETWORKS_DOMAIN_SIZE];
	unsigned char signature[NETWORKS_SIGNATURE_SIZE];
	const char *why;

	if (!copy)
		return 0;
	memcpy(copy, data, size);
	if (networks_list_parse(copy, (int)size, &list, &why))
		check_list(&list);
	if (networks_signed_check(copy, (int)size, keys, epochs, 2, &list, &why))
		__builtin_trap();
	if (networks_signature_text(copy, (int)size, signature) && size != 128 && size != 129)
		__builtin_trap();
	free(copy);
	/* (as text: the settings and a typed name) */
	text = (char *)malloc(size + 1);
	if (!text)
		return 0;
	memcpy(text, data, size);
	text[size] = 0;
	{
		const char *half = text + strlen(text) / 2;

		networks_trust_make(&trust, "chupathingy.org", home_roots, 1, NULL, text, half, NULL, NULL);
		check_trust(&trust, "chupathingy.org");
	}
	if (networks_name_parse(text, "chupathingy.org", handle, domain) &&
		(strlen(handle) < 3 || strlen(handle) > 20 || !networks_domain_valid(domain, (int)strlen(domain))))
	{
		__builtin_trap();
	}
	networks_domain_valid(text, (int)strlen(text));
	free(text);
	/* (an issuer certificate from a machine, looked up and remembered: never
	a network it doesn't have) */
	if (size >= NETWORKS_ISSUER_SIZE)
	{
		static struct networks_memory memory;
		unsigned char roots[NETWORKS_ROOTS_MOST][NETWORKS_KEY_SIZE];
		int count = -1, found = networks_issuer_lookup(&trust, &memory, data, NETWORKS_ISSUER_SIZE,
			(unsigned long)size * 1000UL, data[0], roots, &count);

		if (found >= trust.count || found < NETWORKS_TWO_NETWORKS || (found >= 0 && (count < 0 || count > 4)))
			__builtin_trap();
	}
	return 0;
}
