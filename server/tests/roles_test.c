/*
ROLES_TEST.C

Tests of a verified player's role (server/platform/server_roles.c): a
Delta account's line in moderators.txt, and the site's role list matched
by the account each entry carries, never by a handle alone. Built and run
by tools/test_server_roles.py, with the server's role units, in a folder
of its own (platform_data_root). Exits nonzero on a failure.
*/

#include "../src/server_roles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static const char *data_root;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

const char *platform_data_root(void)
{
	return data_root;
}

int main(int argc, char **argv)
{
	unsigned char account[16], other[16];
	char path[1024], who[64];
	const char *handles[2] = { "zatchbot", "milenko" };
	int list_roles[2] = { SERVER_ROLE_ADMIN, SERVER_ROLE_MODERATOR };
	unsigned char accounts[32];
	unsigned int permissions = 0;
	FILE *file;

	if (argc < 2)
		return 2;
	data_root = argv[1];
	memset(account, 0x31, sizeof(account));
	memset(other, 0x44, sizeof(other));

	/* a Delta account's line: its role, by the account alone */
	snprintf(path, sizeof(path), "%s/moderators.txt", data_root);
	file = fopen(path, "w");
	if (!file)
		return 2;
	fprintf(file, "moderator 31313131313131313131313131313131 Zatchbot\n");
	fclose(file);
	server_roles_start();
	CHECK(server_roles_account_role(account, "zatchbot", &permissions, who, sizeof(who)) == SERVER_ROLE_MODERATOR &&
		!strcmp(who, "Zatchbot") && permissions != 0);
	CHECK(server_roles_account_role(other, "zatchbot", NULL, NULL, 0) == 0);
	/* (an account line is no moderator key) */
	{
		unsigned char key[32];

		memset(key, 0, sizeof(key));
		memcpy(key, account, sizeof(account));
		CHECK(server_roles_key_role(key, NULL, NULL, 0) == 0);
	}

	/* the site's list: by the account each entry carries; an entry without
	one (an older site's) gives a verified player nothing, whatever the
	handle */
	memset(accounts, 0, sizeof(accounts));
	memcpy(accounts, other, 16);
	server_roles_set_site_handles(2, handles, list_roles, list_roles, NULL, accounts);
	CHECK(server_roles_account_role(other, "someone", NULL, who, sizeof(who)) == SERVER_ROLE_ADMIN &&
		!strcmp(who, "zatchbot (site)"));
	CHECK(server_roles_account_role((const unsigned char *)"\x77\x77\x77\x77\x77\x77\x77\x77\x77\x77\x77\x77\x77\x77\x77\x77",
		"milenko", NULL, NULL, 0) == 0);
	/* (the file's line still counts beside the list: the higher wins) */
	CHECK(server_roles_account_role(account, "milenko", NULL, NULL, 0) == SERVER_ROLE_MODERATOR);
	server_roles_set_site_handles(0, NULL, NULL, NULL, NULL, NULL);
	CHECK(server_roles_account_role(other, "zatchbot", NULL, NULL, 0) == 0);

	/* a verified player: a bound proof from a certificate that may
	moderate gets its account's role; unbound (joined by address, the name
	alone) or a certificate that may not, nothing */
	permissions = 0;
	CHECK(server_roles_verified_role(account, "zatchbot", 1, 1, &permissions, who, sizeof(who)) ==
		SERVER_ROLE_MODERATOR && permissions != 0 && !strcmp(who, "Zatchbot"));
	CHECK(server_roles_verified_role(account, "zatchbot", 0, 1, &permissions, NULL, 0) == 0 && !permissions);
	CHECK(server_roles_verified_role(account, "zatchbot", 1, 0, NULL, NULL, 0) == 0);

	if (failures)
	{
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("roles_test: ok\n");
	return 0;
}
