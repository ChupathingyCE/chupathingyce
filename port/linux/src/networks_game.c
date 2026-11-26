/*
NETWORKS_GAME.C

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later

The networks this machine trusts (networks_game.h; delta-spec's link.md,
"Networks"), in the order the spec gives:
1. the home network: identity_key.h's domain and root keys, which nothing
   changes;
2. the newest signed list of networks this machine has (signed by
   networks_key.h's keys, never the legacy table's): the cache,
   delta_networks.signed in the save root, read at start, then
   network.browser_url's /v1/delta/networks and .sig, fetched in the
   background at start and every few hours. A list of an older or equal
   serial is never taken. While the list keys are all zero, no list is
   taken or fetched;
3. network.networks_trust: networks of the player's own, each a domain and
   its root keys;
4. network.networks_block: domains not trusted even when the list has
   them (never home).

Nothing here asks another network anything: a name is checked offline
against the root keys alone. Other networks' revocation lists aren't read
in this version: their device certificates' short life is the limit.
*/

#include "networks_game.h"
#include "halo_console_features.h"

#ifdef HALO_DELTA_PEER_IDENTITY

#include "platform.h"
#include "port_config.h"
#include "identity.h"
#include "identity_key.h"
#include "networks_key.h"
#include "browser_http.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NETWORKS_CACHE_NAME "delta_networks.signed"

/* (fetched this often, and this long after a failure, in milliseconds) */
#ifndef NETWORKS_REFRESH_INTERVAL
#define NETWORKS_REFRESH_INTERVAL (4 * 60 * 60 * 1000)
#endif
#ifndef NETWORKS_RETRY_INTERVAL
#define NETWORKS_RETRY_INTERVAL (30 * 60 * 1000)
#endif

static pthread_mutex_t networks_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t networks_once = PTHREAD_ONCE_INIT;

static struct
{
	char home[NETWORKS_DOMAIN_SIZE];
	unsigned char home_roots[NETWORKS_ROOTS_MOST][NETWORKS_KEY_SIZE];
	int home_root_count;
	/* the list in use (serial 0: none) */
	struct networks_list list;
	struct networks_trust trust;
	unsigned long generation;
	/* (the issuer certificates seen, their networks, and the looking's
	limits) */
	struct networks_memory memory;
} networks;

static int any_list_key(void)
{
	int index, byte;

	for (index = 0; index < NETWORKS_LIST_KEY_COUNT; index++)
	{
		for (byte = 0; byte < NETWORKS_KEY_SIZE; byte++)
		{
			if (networks_list_keys[index][byte])
				return 1;
		}
	}
	return 0;
}

static void say(void *context, const char *line)
{
	(void)context;
	platform_log("networks: %s", line);
}

/* trust made again from the home network, the list and the settings
(networks_lock held) */
static void networks_remake(void)
{
	char names[600];
	int index, used = 0;

	networks_trust_make(&networks.trust, networks.home, (const unsigned char (*)[NETWORKS_KEY_SIZE])networks.home_roots,
		networks.home_root_count, networks.list.serial ? &networks.list : NULL,
		config_string("network.networks_trust"), config_string("network.networks_block"), say, NULL);
	networks_memory_forget(&networks.memory);
	networks.generation++;
	names[0] = 0;
	for (index = 1; index < networks.trust.count && used < (int)sizeof(names) - 1; index++)
		used += snprintf(names + used, sizeof(names) - (size_t)used, "%s%s%s", index > 1 ? ", " : "",
			networks.trust.networks[index].domain, networks.trust.networks[index].source == 2 ? " (local)" : "");
	if (used >= (int)sizeof(names))
		used = (int)sizeof(names) - 1;
	platform_log("networks: home %s; list %lu; %d other%s%s%s", networks.home, networks.list.serial,
		networks.trust.count - 1, networks.trust.count == 2 ? "" : "s", networks.trust.count > 1 ? ": " : "", names);
}

static void cache_path(char *path, size_t size)
{
	snprintf(path, size, "%s/%s", platform_save_root(), NETWORKS_CACHE_NAME);
}

static void cache_write(const char *signed_list, int size)
{
	char path[1024], temporary[1100];
	FILE *file;
	int ok;

	cache_path(path, sizeof(path));
	snprintf(temporary, sizeof(temporary), "%s.new", path);
	file = fopen(temporary, "wb");
	if (!file)
		return;
	ok = fwrite(signed_list, 1, (size_t)size, file) == (size_t)size;
	ok = fclose(file) == 0 && ok;
	/* (Windows does not rename over a file) */
	if (ok && rename(temporary, path) != 0)
	{
		remove(path);
		ok = rename(temporary, path) == 0;
	}
	if (!ok)
	{
		remove(temporary);
		platform_log("networks: the list of networks could not be cached");
	}
}

/* a signed list (signature, line feed, document) from source: taken if it
checks and is newer than the one in use (1), else 0 */
static int networks_take(const char *signed_list, int size, const char *source, int cache)
{
	static struct networks_list list;
	static pthread_mutex_t take_lock = PTHREAD_MUTEX_INITIALIZER;
	const char *why;
	int taken = 0;

	pthread_mutex_lock(&take_lock);
	if (!networks_signed_check(signed_list, size, networks_list_keys, networks_key_last_epochs,
		NETWORKS_LIST_KEY_COUNT, &list, &why))
	{
		platform_log("networks: dropped the list of networks from %s: %s", source, why);
	}
	else
	{
		pthread_mutex_lock(&networks_lock);
		if (list.serial > networks.list.serial)
		{
			networks.list = list;
			networks_remake();
			taken = 1;
		}
		pthread_mutex_unlock(&networks_lock);
		if (taken && cache)
			cache_write(signed_list, size);
	}
	pthread_mutex_unlock(&take_lock);
	return taken;
}

/* (a test's build alone: HALO_IDENTITY_TEST_HOME="domain key [key...]"
makes another network this copy's home, so one test build plays as a
player of either of two test networks) */
static void networks_test_home(void)
{
#ifdef HALO_IDENTITY_TEST_KEY
	const char *text = getenv("HALO_IDENTITY_TEST_HOME");
	struct networks_trust probe;
	static const unsigned char none[1][NETWORKS_KEY_SIZE];

	if (!text || !text[0])
		return;
	/* (read as a local addition is, then made home) */
	networks_trust_make(&probe, "home.invalid", none, 0, NULL, text, "", say, NULL);
	if (probe.count != 2)
	{
		platform_log("networks: HALO_IDENTITY_TEST_HOME is not a domain and its root keys: not used");
		return;
	}
	snprintf(networks.home, sizeof(networks.home), "%s", probe.networks[1].domain);
	networks.home_root_count = probe.networks[1].root_count;
	for (int index = 0; index < networks.home_root_count; index++)
		memcpy(networks.home_roots[index], probe.networks[1].roots[index].key, NETWORKS_KEY_SIZE);
	platform_log("networks: TEST home network %s (HALO_IDENTITY_TEST_HOME)", networks.home);
#endif
}

static void networks_load(void)
{
	char path[1024];
	char *file;
	size_t size = 0;
	int index;

	pthread_mutex_lock(&networks_lock);
	snprintf(networks.home, sizeof(networks.home), "%s", IDENTITY_HOME_DOMAIN);
	networks.home_root_count = IDENTITY_ROOT_KEY_COUNT < NETWORKS_ROOTS_MOST ? IDENTITY_ROOT_KEY_COUNT :
		NETWORKS_ROOTS_MOST;
	for (index = 0; index < networks.home_root_count; index++)
		memcpy(networks.home_roots[index], identity_root_keys[index], NETWORKS_KEY_SIZE);
	networks_test_home();
	networks_remake();
	pthread_mutex_unlock(&networks_lock);
	if (!any_list_key())
		return;
	cache_path(path, sizeof(path));
	file = config_file_read(path, &size);
	if (file)
	{
		networks_take(file, (int)size, "the cache", 0);
		free(file);
	}
}

/* the document and its signature from the home network's site: taken (1),
or 0 */
static int networks_fetch(void)
{
	static char document[NETWORKS_DOCUMENT_MOST + 2];
	static char signed_list[NETWORKS_SIGNED_MOST + 1];
	const char *base = config_string("network.browser_url");
	char url[600], signature[256], error[256];
	size_t length = strlen(base);
	int status, size;

	while (length && base[length - 1] == '/')
		length--;
	snprintf(url, sizeof(url), "%.*s/v1/delta/networks", (int)length, base);
	status = posix_browser_request(url, NULL, NULL, document, (int)sizeof(document), error, (int)sizeof(error));
	if (status != 200)
	{
		/* (404: the site publishes none, which is no fault) */
		if (status != 404)
			platform_log("networks: no list of networks from the site (%s)", status ? "the server refused" : error);
		return 0;
	}
	snprintf(url, sizeof(url), "%.*s/v1/delta/networks.sig", (int)length, base);
	status = posix_browser_request(url, NULL, NULL, signature, (int)sizeof(signature), error, (int)sizeof(error));
	if (status != 200)
	{
		platform_log("networks: no list of networks' signature from the site (%s)",
			status ? "the server refused" : error);
		return 0;
	}
	size = (int)strlen(document);
	if (strlen(signature) < 128 || size > NETWORKS_DOCUMENT_MOST)
	{
		platform_log("networks: dropped the list of networks from the site: it is not a signed list");
		return 0;
	}
	/* (the signed form: the signature's 128 hex digits, a line feed, the
	document; its signature file's own line feed or none) */
	if (!(strlen(signature) == 128 || (strlen(signature) == 129 && signature[128] == '\n')))
	{
		platform_log("networks: dropped the list of networks from the site: its signature is not 128 hex digits");
		return 0;
	}
	memcpy(signed_list, signature, 128);
	signed_list[128] = '\n';
	memcpy(signed_list + 129, document, (size_t)size);
	networks_take(signed_list, 129 + size, "the site", 1);
	return 1;
}

static void *networks_thread(void *unused)
{
	(void)unused;
	for (;;)
	{
		int interval = networks_fetch() ? NETWORKS_REFRESH_INTERVAL : NETWORKS_RETRY_INTERVAL;
		struct timespec wait = { interval / 1000, (long)(interval % 1000) * 1000000L };

		while (nanosleep(&wait, &wait) != 0)
			;
	}
	return NULL;
}

void networks_start(void)
{
	static int started;

	pthread_once(&networks_once, networks_load);
	if (started)
		return;
	started = 1;
	/* (no list key, nothing to check a list with; no site, the player's
	choice to talk to none) */
	if (any_list_key() && config_string("network.browser_url")[0])
	{
		pthread_t thread;

		if (pthread_create(&thread, NULL, networks_thread, NULL) == 0)
			pthread_detach(thread);
		else
			platform_log("networks: could not start the list of networks' thread");
	}
}

const unsigned char (*networks_home_roots(int *count))[NETWORKS_KEY_SIZE]
{
	pthread_once(&networks_once, networks_load);
	*count = networks.home_root_count;
	return (const unsigned char (*)[NETWORKS_KEY_SIZE])networks.home_roots;
}

int networks_check_issuer(const unsigned char *issuer, int machine, unsigned long now, char *domain,
	unsigned char (*roots)[NETWORKS_KEY_SIZE], int *root_count)
{
	int index;

	pthread_once(&networks_once, networks_load);
	pthread_mutex_lock(&networks_lock);
	index = networks_issuer_lookup(&networks.trust, &networks.memory, issuer, IDENTITY_ISSUER_SIZE, now, machine,
		roots, root_count);
	if (index == NETWORKS_LIMITED)
	{
		int bucket = machine >= 0 && machine < NETWORKS_MACHINES ? machine : NETWORKS_MACHINES - 1;

		if (!networks.memory.machines[bucket].said)
		{
			networks.memory.machines[bucket].said = 1;
			platform_log("networks: machine %d sent more new issuer certificates than %d a minute: the rest this "
				"minute are not checked", machine, NETWORKS_LOOKS_A_MINUTE);
		}
	}
	else if (index == NETWORKS_TWO_NETWORKS)
	{
		static unsigned long said;

		if (said != networks.memory.changes)
		{
			said = networks.memory.changes;
			platform_log("networks: an issuer key turned up under two networks' root keys: refused for every "
				"network but home");
		}
	}
	domain[0] = 0;
	if (index >= 0)
		snprintf(domain, NETWORKS_DOMAIN_SIZE, "%s", networks.trust.networks[index].domain);
	else
		*root_count = 0;
	pthread_mutex_unlock(&networks_lock);
	return index < 0 ? 0 : index == 0 ? 1 : 2;
}

unsigned long networks_generation(void)
{
	unsigned long generation;

	pthread_once(&networks_once, networks_load);
	pthread_mutex_lock(&networks_lock);
	generation = networks.generation + networks.memory.changes;
	pthread_mutex_unlock(&networks_lock);
	return generation;
}

#else

void networks_start(void)
{
}

#endif
