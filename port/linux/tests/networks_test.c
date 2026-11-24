/*
NETWORKS_TEST.C

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later

Tests of the networks a game trusts (port/linux/src/networks.c): the
shared vectors of delta-spec's reference/federation_vectors.json (also
port/linux/tests/federation_vectors.json, which tools/test_networks.py
checks these against), domains and names, the signed list and every way
it can be wrong, local additions and removals, and which network a
certificate belongs to. Built and run by tools/test_networks.py with
identity.c and Monocypher; exits nonzero on a failure.
*/

#include "../src/networks.h"
#include "../src/identity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

/* the vectors (TEST keys only; each seed is SHA-256 of its text) */
#define LIST_SEED_TEXT "open network test list key"
#define OTHER_DEVICE_SEED_TEXT "open network test device example.org"
#define VECTOR_NOW \
	1790200000UL
#define VECTOR_HOME_ROOT_PRIMARY \
	"12762975a2781f981d53c9bfd7625f10b56137eee3235fb5f3eb92c966f023fa"
#define VECTOR_HOME_ROOT_RECOVERY \
	"bd5644c3fd79b6e348885e4749c994bca61357f4dde6395da121bf68578bb8a5"
#define VECTOR_HOME_ISSUER_CERTIFICATE \
	"4349535301000000803bb16a80e2276bfc284aa53690a1b91984e67b1820c0a28452dafbc58ed107d40066ad47814d90" \
	"38cc84f430b86054d1ede8b53244a90bda00b6012d4df101a0e00f5d388af8193c95238b1498d9fbd9314d4226ca2e6d" \
	"40332529ea8163ad127e579cc26dcc0c"
#define VECTOR_HOME_DEVICE_CERTIFICATE \
	"434445560101070120c2b26aa0fcbb6a010203040506070848810d1682965a4631ea6b25b20006a252c816cd2077ff5b" \
	"f6bcb81b396cf7f3cb5519a437d3699adc4ed40f2e4d22f30c3bfecb6ffcca8e1f0000006d696c656e6b6f359728fbc6" \
	"34431278f9352410620d865f517ebad416d36e0c845cdc85613fb2e8867b8ff2b8fe26c1d7b5b9abb3d99c462edea4d8" \
	"2ad78c975c6b970f239b02"
#define VECTOR_OTHER_ROOT_PRIMARY \
	"8b2f06a8475780f3e0f47305f664da217859814835b40e8bc548be9618e8f50a"
#define VECTOR_OTHER_ROOT_RECOVERY \
	"206cda82c7d1924ba2df99f33dd49455e0c7fb757d6a87ee1260975628fc9ac7"
#define VECTOR_OTHER_ISSUER_CERTIFICATE \
	"4349535301000000803bb16a80e2276bc994e468a0f5cf4c5af13785917116fb52f828cd5a60090711822af2ec755280" \
	"91771b7a0cfe52aa8716738602d356b192faa091d4a9520869924adbb27bfbd404c648c57b008d35c76f0c19c1d652d1" \
	"e557ebb7c230e74b6a38e44484188c0c"
#define VECTOR_OTHER_DEVICE_CERTIFICATE \
	"434445560101070020c2b26aa0fcbb6a0102030405060708e37ef928f7aa307cbb21d3694880f956b7325d4f45314481" \
	"9e20e8816af54355816c94e57001567fb213513d77da48f71c6a34de6ea4b6781f0000006d696c656e6b6f8d88fe0925" \
	"66b3989c6444629759ab1d6072b640403e618fb6c8f165289de3088be37434afa82020f86b032a19b937937a61966336" \
	"a6835044f1191d9cd28207"
#define VECTOR_LIST_KEY_PUBLIC \
	"39efbe2698a0808e5924125b3a8d27a09ee55b985c4b755b6a1bbab492418b9f"
#define VECTOR_LIST_DOCUMENT \
	"{\n" \
	"  \"open_networks\": 1,\n" \
	"  \"serial\": 1,\n" \
	"  \"issued\": 1790000000,\n" \
	"  \"networks\": {\n" \
	"    \"chupathingy.org\": {\n" \
	"      \"roots\": [\n" \
	"        {\n" \
	"          \"key\": \"12762975a2781f981d53c9bfd7625f10b56137eee3235fb5f3eb92c966f023fa\",\n" \
	"          \"until\": 0\n" \
	"        },\n" \
	"        {\n" \
	"          \"key\": \"bd5644c3fd79b6e348885e4749c994bca61357f4dde6395da121bf68578bb8a5\",\n" \
	"          \"until\": 1893456000\n" \
	"        }\n" \
	"      ]\n" \
	"    },\n" \
	"    \"example.org\": {\n" \
	"      \"roots\": [\n" \
	"        {\n" \
	"          \"key\": \"8b2f06a8475780f3e0f47305f664da217859814835b40e8bc548be9618e8f50a\",\n" \
	"          \"until\": 0\n" \
	"        },\n" \
	"        {\n" \
	"          \"key\": \"206cda82c7d1924ba2df99f33dd49455e0c7fb757d6a87ee1260975628fc9ac7\",\n" \
	"          \"until\": 1893456000\n" \
	"        }\n" \
	"      ]\n" \
	"    }\n" \
	"  }\n" \
	"}\n"
#define VECTOR_LIST_SIGNATURE \
	"4a74245fa78dd40080c75b991faf0d407625655f4d096340a34d625545357225b4b527b8f58d74990f6f9a1b299f10fc" \
	"f8b484af8c8c0b9428a26c5de8e7de0a"
#define VECTOR_HOME_ACCOUNT \
	"31ea6b25b20006a252c816cd2077ff5b"
#define VECTOR_OTHER_ACCOUNT \
	"bb21d3694880f956b7325d4f45314481"

static unsigned char home_roots[2][32], other_roots[2][32], list_key[2][32], list_seed[2][32];
static unsigned char home_issuer[112], other_issuer[112], home_device[200], other_device[200];
static int home_device_size, other_device_size;
static const unsigned char last_epochs[2] = { 0, 255 };
static struct networks_list list;
static struct networks_trust trust;

static void unhex(const char *text, unsigned char *bytes, int size)
{
	if (!identity_unhex(text, (int)strlen(text), bytes, size))
	{
		printf("FAIL: a vector is not hex\n");
		exit(1);
	}
}

static int unhex_any(const char *text, unsigned char *bytes)
{
	int size = (int)strlen(text) / 2;

	unhex(text, bytes, size);
	return size;
}

static void load(void)
{
	static const char *const seeds[2] = { LIST_SEED_TEXT, "open network test list key recovery" };
	int index;

	unhex(VECTOR_HOME_ROOT_PRIMARY, home_roots[0], 32);
	unhex(VECTOR_HOME_ROOT_RECOVERY, home_roots[1], 32);
	unhex(VECTOR_OTHER_ROOT_PRIMARY, other_roots[0], 32);
	unhex(VECTOR_OTHER_ROOT_RECOVERY, other_roots[1], 32);
	unhex(VECTOR_HOME_ISSUER_CERTIFICATE, home_issuer, 112);
	unhex(VECTOR_OTHER_ISSUER_CERTIFICATE, other_issuer, 112);
	home_device_size = unhex_any(VECTOR_HOME_DEVICE_CERTIFICATE, home_device);
	other_device_size = unhex_any(VECTOR_OTHER_DEVICE_CERTIFICATE, other_device);
	for (index = 0; index < 2; index++)
	{
		identity_sha256(seeds[index], (int)strlen(seeds[index]), list_seed[index]);
		identity_public(list_seed[index], list_key[index]);
	}
}

/* a document signed with a list seed, as the signed form */
static int sign(const char *document, int seed, char *out)
{
	unsigned char message[NETWORKS_DOCUMENT_MOST + 64], signature[64];
	int label = (int)strlen("open network list v1\n"), size = (int)strlen(document);

	memcpy(message, "open network list v1\n", (size_t)label);
	memcpy(message + label, document, (size_t)size);
	identity_sign(list_seed[seed], message, label + size, signature);
	identity_hex(signature, 64, out);
	out[128] = '\n';
	memcpy(out + 129, document, (size_t)size);
	out[129 + size] = 0;
	return 129 + size;
}

/* a document's check, signed with the test list key */
static int takes(const char *document)
{
	static char signed_list[NETWORKS_SIGNED_MOST + 2];
	const char *why;
	int size = (int)strlen(document);

	if (size > NETWORKS_DOCUMENT_MOST + 1)
		return 0;
	sign(document, 0, signed_list);
	return networks_signed_check(signed_list, 129 + size, (const unsigned char (*)[32])list_key, last_epochs, 2,
		&list, &why);
}

static void test_vectors(void)
{
	unsigned char signature[64], roots[4][32], account[16];
	struct identity_issuer issuer;
	struct identity_device device;
	const char *why;
	int count, index;

	/* the list: the test key signed it, and both networks are in it */
	unhex(VECTOR_LIST_KEY_PUBLIC, signature, 32);
	CHECK(!memcmp(signature, list_key[0], 32));
	CHECK(networks_signature_text(VECTOR_LIST_SIGNATURE, 128, signature));
	CHECK(networks_list_check(VECTOR_LIST_DOCUMENT, (int)strlen(VECTOR_LIST_DOCUMENT), signature,
		(const unsigned char (*)[32])list_key, last_epochs, 2, &list, &why));
	CHECK(list.serial == 1 && list.issued == 1790000000UL && list.count == 2);
	CHECK(!strcmp(list.networks[0].domain, "chupathingy.org") && !strcmp(list.networks[1].domain, "example.org"));
	CHECK(list.networks[1].root_count == 2 && list.networks[1].roots[1].until == 1893456000UL);
	CHECK(!memcmp(list.networks[1].roots[0].key, other_roots[0], 32));
	/* home + list: each certificate's network, and its roots alone */
	networks_trust_make(&trust, "chupathingy.org", (const unsigned char (*)[32])home_roots, 2, &list, "", "", NULL,
		NULL);
	CHECK(trust.count == 2 && !strcmp(trust.networks[1].domain, "example.org"));
	CHECK(networks_issuer_network(&trust, home_issuer, 112, VECTOR_NOW, roots, &count) == 0);
	CHECK(count == 2 && !memcmp(roots, home_roots, sizeof(home_roots)));
	CHECK(identity_check_issuer(home_issuer, 112, (const unsigned char (*)[32])roots, count, VECTOR_NOW, &issuer) ==
		IDENTITY_OK);
	CHECK(identity_check_device(home_device, home_device_size, &issuer, VECTOR_NOW, &device) == IDENTITY_OK);
	unhex(VECTOR_HOME_ACCOUNT, account, 16);
	CHECK(!memcmp(device.account, account, 16) && !strcmp(device.handle, "milenko"));
	CHECK(networks_issuer_network(&trust, other_issuer, 112, VECTOR_NOW, roots, &count) == 1);
	CHECK(count == 2 && !memcmp(roots, other_roots, sizeof(other_roots)));
	CHECK(identity_check_issuer(other_issuer, 112, (const unsigned char (*)[32])roots, count, VECTOR_NOW, &issuer) ==
		IDENTITY_OK);
	CHECK(identity_check_device(other_device, other_device_size, &issuer, VECTOR_NOW, &device) == IDENTITY_OK);
	unhex(VECTOR_OTHER_ACCOUNT, account, 16);
	CHECK(!memcmp(device.account, account, 16) && !strcmp(device.handle, "milenko"));
	/* (example.org's issuer never checks against home's roots: the same
	refusal as before federation) */
	CHECK(identity_check_issuer(other_issuer, 112, (const unsigned char (*)[32])home_roots, 2, VECTOR_NOW, &issuer) ==
		IDENTITY_NOT_ROOT_SIGNED);
	/* home only: example.org's is no trusted network's */
	networks_trust_make(&trust, "chupathingy.org", (const unsigned char (*)[32])home_roots, 2, NULL, "", "", NULL,
		NULL);
	CHECK(trust.count == 1);
	CHECK(networks_issuer_network(&trust, other_issuer, 112, VECTOR_NOW, NULL, NULL) == -1);
	CHECK(networks_issuer_network(&trust, home_issuer, 112, VECTOR_NOW, NULL, NULL) == 0);
	/* a broken issuer certificate is no network's */
	for (index = 0; index < 112; index += 7)
	{
		unsigned char broken[112];

		memcpy(broken, home_issuer, sizeof(broken));
		broken[index] ^= 0x40;
		CHECK(networks_issuer_network(&trust, broken, 112, VECTOR_NOW, NULL, NULL) == -1);
	}
	CHECK(networks_issuer_network(&trust, home_issuer, 111, VECTOR_NOW, NULL, NULL) == -1);
}

/* today's certificates: the home network is always first and its roots
come back as built, in order, so the check that follows is the one every
build made before */
static void test_home_unchanged(void)
{
	unsigned char roots[4][32];
	int count;

	networks_trust_make(&trust, "chupathingy.org", (const unsigned char (*)[32])home_roots, 2, NULL, "", "", NULL,
		NULL);
	CHECK(networks_issuer_network(&trust, home_issuer, 112, VECTOR_NOW, roots, &count) == 0);
	CHECK(count == 2 && !memcmp(roots[0], home_roots[0], 32) && !memcmp(roots[1], home_roots[1], 32));
	/* a list or a setting naming home, even with other keys, changes nothing */
	networks_trust_make(&trust, "chupathingy.org", (const unsigned char (*)[32])home_roots, 2, NULL,
		"chupathingy.org " VECTOR_OTHER_ROOT_PRIMARY, "chupathingy.org", NULL, NULL);
	CHECK(trust.count == 1 && trust.networks[0].root_count == 2);
	CHECK(networks_issuer_network(&trust, other_issuer, 112, VECTOR_NOW, NULL, NULL) == -1);
	CHECK(networks_issuer_network(&trust, home_issuer, 112, VECTOR_NOW, NULL, NULL) == 0);
}

static void test_domains(void)
{
	static const char *const good[] = {
		"chupathingy.org", "example.org", "a.co", "sub.example.co", "x-1.example.org", "1x.example.org",
		"example.museum",
	};
	static const char *const bad[] = {
		"", "org", "a.c", "example", "192.168.0.1", "localhost", "example.org.", ".example.org", "Example.org",
		"EXAMPLE.ORG", "example..org", "-bad.org", "bad-.org", "xn--exmple-cua.org", "sub.xn--p1ai.org",
		"example.x1", "example.o-g", "exa_mple.org", "exa mple.org", "example.org ", " example.org",
		/* a Kelvin sign, a full-width a, a Cyrillic a, a zero-width space,
		a soft hyphen, a dotless i: never ASCII, never folded to it */
		"\xE2\x84\xAA" "example.org", "ex\xEF\xBD\x81mple.org", "ex\xD0\xB0mple.org", "exa\xE2\x80\x8Bmple.org",
		"exa\xC2\xADmple.org", "\xC4\xB1nfo.org", "example.\xE2\x84\xAA" "z",
	};
	char long_label[80], long_domain[300];
	size_t index;

	for (index = 0; index < sizeof(good) / sizeof(*good); index++)
		CHECK(networks_domain_valid(good[index], (int)strlen(good[index])));
	for (index = 0; index < sizeof(bad) / sizeof(*bad); index++)
	{
		if (networks_domain_valid(bad[index], (int)strlen(bad[index])))
			printf("FAIL: \"%s\" read as a domain\n", bad[index]), failures++;
	}
	/* 63 a label, 64 not; 253 a domain, 254 not */
	memset(long_label, 'a', 63);
	memcpy(long_label + 63, ".org", 5);
	CHECK(networks_domain_valid(long_label, (int)strlen(long_label)));
	memset(long_label, 'a', 64);
	memcpy(long_label + 64, ".org", 5);
	CHECK(!networks_domain_valid(long_label, (int)strlen(long_label)));
	/* (three labels of 63, one of 57, ".org") */
	memset(long_domain, 'a', 192);
	long_domain[63] = long_domain[127] = long_domain[191] = '.';
	memset(long_domain + 192, 'b', 57);
	memcpy(long_domain + 249, ".org", 5);
	CHECK(strlen(long_domain) == 253 && networks_domain_valid(long_domain, 253));
	memmove(long_domain + 1, long_domain, 254);
	long_domain[0] = 'c';
	CHECK(!networks_domain_valid(long_domain, 254));
}

static void test_names(void)
{
	static const char *const valid[][3] = {
		{ "milenko:chupathingy.org", "milenko", "chupathingy.org" },
		{ "a_b-c:example.org", "a_b-c", "example.org" },
		{ "xyz:sub.example.co", "xyz", "sub.example.co" },
		{ "Milenko", "milenko", "chupathingy.org" },
		{ "  MILENKO:Example.ORG \n", "milenko", "example.org" },
	};
	static const char *const invalid[] = {
		"mi:example.org", "milenko:example", "milenko:192.168.0.1", "milenko:xn--exmple-cua.org",
		"milenko:example.org:x", "milenko:-bad.org", "milenko:example..org", "milenko:Example.org.", "", ":",
		"milenko:", ":example.org", "milenko::example.org", "averyveryverylonghandle:example.org",
		"milenko:\xE2\x84\xAA" "example.org", "mil\xE2\x84\xAA" "nko:example.org", "milenko:ex\xD0\xB0mple.org",
	};
	char handle[21], domain[NETWORKS_DOMAIN_SIZE], shown[300];
	size_t index;

	for (index = 0; index < sizeof(valid) / sizeof(*valid); index++)
	{
		CHECK(networks_name_parse(valid[index][0], "chupathingy.org", handle, domain));
		CHECK(!strcmp(handle, valid[index][1]) && !strcmp(domain, valid[index][2]));
	}
	for (index = 0; index < sizeof(invalid) / sizeof(*invalid); index++)
	{
		if (networks_name_parse(invalid[index], "chupathingy.org", handle, domain))
			printf("FAIL: \"%s\" read as a name\n", invalid[index]), failures++;
	}
	CHECK(networks_shown("Milenko", "chupathingy.org", "chupathingy.org", shown, (int)sizeof(shown)) == 7 &&
		!strcmp(shown, "Milenko"));
	CHECK(networks_shown("Milenko", "example.org", "chupathingy.org", shown, (int)sizeof(shown)) == 19 &&
		!strcmp(shown, "Milenko:example.org"));
	CHECK(networks_shown("Milenko", "example.org", "chupathingy.org", shown, 8) == 7 && !strcmp(shown, "Milenko"));
}

/* the vectors' document with one change (from, to: the first place) */
static const char *changed(const char *from, const char *to)
{
	static char document[NETWORKS_DOCUMENT_MOST * 2];
	const char *at = strstr(VECTOR_LIST_DOCUMENT, from);

	if (!at)
	{
		printf("FAIL: \"%s\" is not in the document\n", from);
		failures++;
		return "";
	}
	snprintf(document, sizeof(document), "%.*s%s%s", (int)(at - VECTOR_LIST_DOCUMENT), VECTOR_LIST_DOCUMENT, to,
		at + strlen(from));
	return document;
}

#define K1 "1111111111111111111111111111111111111111111111111111111111111111"

static void test_list_malformed(void)
{
	static char document[NETWORKS_DOCUMENT_MOST * 2];
	static char signed_list[NETWORKS_SIGNED_MOST + 2];
	const char *why;
	int index, size;

	CHECK(takes(VECTOR_LIST_DOCUMENT));
	CHECK(takes("{\"open_networks\":1,\"serial\":7,\"networks\":{}}") && list.count == 0 && list.serial == 7);
	/* unknown members anywhere are skipped */
	CHECK(takes(changed("\"serial\"", "\"later\": {\"a\": [1, -2, true, null, \"x\\u00e9\"]}, \"serial\"")));
	CHECK(takes(changed("\"until\": 0", "\"use\": \"primary\", \"until\": 0")));
	CHECK(takes(changed("\"roots\"", "\"note\": \"\xC3\xA9\", \"roots\"")));
	/* -0 is 0; escaped member names read as written */
	CHECK(takes(changed("\"until\": 0", "\"until\": -0")));
	CHECK(takes(changed("\"serial\"", "\"s\\u0065rial\"")) && list.serial == 1);
	/* each of these refuses the whole list */
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": 1, \"serial\": 2")));
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": 1, \"s\\u0065rial\": 1")));
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": 1, \"x\": 1, \"x\": 1")));
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": 1, \"x\": {\"y\": 1, \"\\u0079\": 2}")));
	CHECK(!takes(changed("\"open_networks\": 1", "\"open_networks\": 2")));
	CHECK(!takes(changed("\"open_networks\": 1", "\"open_networks\": true")));
	CHECK(!takes(changed("\"open_networks\": 1", "\"open_networks\": 1.0")));
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": 0")));
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": 4294967295")));
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": 01")));
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": 1e0")));
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": \"1\"")));
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": 1, \"x\": 0.5")));
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": 1, \"x\": NaN")));
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": 1, \"x\": Infinity")));
	CHECK(!takes(changed("\"issued\": 1790000000", "\"issued\": 4294967296")));
	CHECK(!takes(changed("\"issued\": 1790000000", "\"issued\": -1")));
	CHECK(!takes(changed("\"until\": 0", "\"until\": 4294967296")));
	CHECK(!takes(changed("\"until\": 0", "\"until\": null")));
	CHECK(!takes(changed("12762975a2781f981d53c9bfd7625f10b56137eee3235fb5f3eb92c966f023fa",
		"12762975A2781F981D53C9BFD7625F10B56137EEE3235FB5F3EB92C966F023FA")));
	CHECK(!takes(changed("12762975a2781f981d53c9bfd7625f10b56137eee3235fb5f3eb92c966f023fa",
		"12762975a2781f981d53c9bfd7625f10b56137eee3235fb5f3eb92c966f023f")));
	/* (a small-order key: the identity point) */
	CHECK(!takes(changed("12762975a2781f981d53c9bfd7625f10b56137eee3235fb5f3eb92c966f023fa",
		"0100000000000000000000000000000000000000000000000000000000000000")));
	/* the same root key twice, in one network or two */
	CHECK(!takes(changed("8b2f06a8475780f3e0f47305f664da217859814835b40e8bc548be9618e8f50a",
		"206cda82c7d1924ba2df99f33dd49455e0c7fb757d6a87ee1260975628fc9ac7")));
	CHECK(!takes(changed("8b2f06a8475780f3e0f47305f664da217859814835b40e8bc548be9618e8f50a",
		"12762975a2781f981d53c9bfd7625f10b56137eee3235fb5f3eb92c966f023fa")));
	/* domains that aren't */
	CHECK(!takes(changed("\"example.org\"", "\"Example.org\"")));
	CHECK(!takes(changed("\"example.org\"", "\"xn--exmple-cua.org\"")));
	CHECK(!takes(changed("\"example.org\"", "\"\\u212aexample.org\"")));
	CHECK(!takes(changed("\"example.org\"", "\"\xE2\x84\xAA" "example.org\"")));
	CHECK(!takes(changed("\"example.org\"", "\"ex\\u0430mple.org\"")));
	CHECK(!takes(changed("\"example.org\"", "\"exa\\u200bmple.org\"")));
	CHECK(!takes(changed("\"example.org\"", "\"127.0.0.1\"")));
	CHECK(!takes(changed("\"example.org\"", "\"chupathingy.org\"")));
	/* roots: none, five, not an array, an object without a key */
	CHECK(!takes("{\"open_networks\":1,\"serial\":1,\"networks\":{\"example.org\":{\"roots\":[]}}}"));
	CHECK(!takes("{\"open_networks\":1,\"serial\":1,\"networks\":{\"example.org\":{}}}"));
	CHECK(!takes("{\"open_networks\":1,\"serial\":1,\"networks\":{\"example.org\":{\"roots\":{}}}}"));
	CHECK(!takes("{\"open_networks\":1,\"serial\":1,\"networks\":{\"example.org\":{\"roots\":[{\"until\":0}]}}}"));
	CHECK(!takes("{\"open_networks\":1,\"serial\":1,\"networks\":{\"example.org\":[]}}"));
	CHECK(!takes("{\"open_networks\":1,\"serial\":1,\"networks\":[]}"));
	CHECK(!takes("{\"open_networks\":1,\"serial\":1}"));
	CHECK(!takes("{\"open_networks\":1,\"networks\":{}}"));
	CHECK(!takes("{\"serial\":1,\"networks\":{}}"));
	snprintf(document, sizeof(document), "{\"open_networks\":1,\"serial\":1,\"networks\":{\"example.org\":{\"roots\":["
		"{\"key\":\"%s\"},{\"key\":\"%s\"},{\"key\":\"%s\"},{\"key\":\"%s\"},{\"key\":\"%s\"}]}}}",
		VECTOR_HOME_ROOT_PRIMARY, VECTOR_HOME_ROOT_RECOVERY, VECTOR_OTHER_ROOT_PRIMARY, VECTOR_OTHER_ROOT_RECOVERY,
		VECTOR_LIST_KEY_PUBLIC);
	CHECK(!takes(document));
	/* 64 networks, then 65 (each its own key, the list key's points) */
	for (size = 64; size <= 65; size++)
	{
		int used = snprintf(document, sizeof(document), "{\"open_networks\":1,\"serial\":1,\"networks\":{");

		for (index = 0; index < size; index++)
		{
			unsigned char seed[32], key[32];
			char hex[65], text[32];

			snprintf(text, sizeof(text), "network %d", index);
			identity_sha256(text, (int)strlen(text), seed);
			identity_public(seed, key);
			identity_hex(key, 32, hex);
			used += snprintf(document + used, sizeof(document) - (size_t)used,
				"%s\"n%d.example.org\":{\"roots\":[{\"key\":\"%s\"}]}", index ? "," : "", index, hex);
		}
		snprintf(document + used, sizeof(document) - (size_t)used, "}}");
		CHECK(takes(document) == (size == 64));
	}
	/* depth: 8 is the most (the document 1) */
	CHECK(takes(changed("\"serial\": 1", "\"serial\": 1, \"x\": [[[[[[1]]]]]]")));
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": 1, \"x\": [[[[[[[1]]]]]]]")));
	CHECK(takes(changed("\"serial\": 1", "\"serial\": 1, \"x\": [[[[[[[]]]]]]]")));
	/* not JSON, not UTF-8, something after it, a byte order mark */
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": 1,")));
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": 1, \"x\": \"\xC0\xAF\"")));
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": 1, \"x\": \"\xED\xA0\x80\"")));
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": 1, \"x\": \"\xF4\x90\x80\x80\"")));
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": 1, \"x\": \"a\tb\"")));
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": 1, \"x\": \"\\x\"")));
	CHECK(!takes(changed("\"serial\": 1", "\"serial\": 1, \"x\": tru")));
	snprintf(document, sizeof(document), "%s{}", VECTOR_LIST_DOCUMENT);
	CHECK(!takes(document));
	snprintf(document, sizeof(document), "%s \n\t\r ", VECTOR_LIST_DOCUMENT);
	CHECK(takes(document));
	snprintf(document, sizeof(document), "\xEF\xBB\xBF%s", VECTOR_LIST_DOCUMENT);
	CHECK(!takes(document));
	CHECK(!takes("") && !takes("[]") && !takes("{}") && !takes("null"));
	/* the size: 16384 bytes the most */
	size = snprintf(document, sizeof(document), "{\"open_networks\":1,\"serial\":1,\"networks\":{},\"pad\":\"");
	memset(document + size, 'a', (size_t)(NETWORKS_DOCUMENT_MOST - size - 2));
	memcpy(document + NETWORKS_DOCUMENT_MOST - 2, "\"}", 3);
	CHECK(strlen(document) == NETWORKS_DOCUMENT_MOST && takes(document));
	memcpy(document + NETWORKS_DOCUMENT_MOST - 2, "a\"}", 4);
	CHECK(!takes(document));
	/* the signature: another key's, a byte changed, the text's form */
	size = sign(VECTOR_LIST_DOCUMENT, 0, signed_list);
	CHECK(networks_signed_check(signed_list, size, (const unsigned char (*)[32])list_key, last_epochs, 2, &list, &why));
	CHECK(!networks_signed_check(signed_list, size, (const unsigned char (*)[32])home_roots, last_epochs, 2, &list,
		&why) && list.count == 0);
	signed_list[size - 3] ^= 1;
	CHECK(!networks_signed_check(signed_list, size, (const unsigned char (*)[32])list_key, last_epochs, 2, &list,
		&why));
	signed_list[size - 3] ^= 1;
	signed_list[10] = signed_list[10] >= 'a' ? (char)(signed_list[10] - 'a' + 'A') : signed_list[10];
	if (signed_list[10] >= 'A' && signed_list[10] <= 'F')
		CHECK(!networks_signed_check(signed_list, size, (const unsigned char (*)[32])list_key, last_epochs, 2, &list,
			&why));
	CHECK(!networks_signed_check(signed_list, 128, (const unsigned char (*)[32])list_key, last_epochs, 2, &list, &why));
	{
		unsigned char signature[64];
		char text[132];

		memset(text, 'a', 128);
		CHECK(networks_signature_text(text, 128, signature));
		text[128] = '\n';
		CHECK(networks_signature_text(text, 129, signature));
		text[129] = '\n';
		CHECK(!networks_signature_text(text, 130, signature));
		text[128] = ' ';
		CHECK(!networks_signature_text(text, 129, signature));
		text[3] = 'A';
		CHECK(!networks_signature_text(text, 128, signature));
		CHECK(!networks_signature_text(text, 127, signature));
	}
	/* all-zero keys take no list */
	{
		static const unsigned char zero[2][32];

		CHECK(!networks_signed_check(signed_list, size, zero, last_epochs, 2, &list, &why));
	}
}

static void test_epochs(void)
{
	static char signed_list[NETWORKS_SIGNED_MOST + 2];
	const char *document = changed("\"serial\": 1", "\"serial\": 16777217");
	char copy[NETWORKS_DOCUMENT_MOST + 1];
	const char *why;
	int size;

	snprintf(copy, sizeof(copy), "%s", document);
	/* epoch 1: the recovery key's to sign, never the primary's */
	size = sign(copy, 0, signed_list);
	CHECK(!networks_signed_check(signed_list, size, (const unsigned char (*)[32])list_key, last_epochs, 2, &list,
		&why));
	size = sign(copy, 1, signed_list);
	CHECK(networks_signed_check(signed_list, size, (const unsigned char (*)[32])list_key, last_epochs, 2, &list,
		&why) && list.serial == 16777217UL);
	/* the primary signs epoch 0's last */
	snprintf(copy, sizeof(copy), "%s", changed("\"serial\": 1", "\"serial\": 16777215"));
	size = sign(copy, 0, signed_list);
	CHECK(networks_signed_check(signed_list, size, (const unsigned char (*)[32])list_key, last_epochs, 2, &list,
		&why));
}

static int lines;

static void count_line(void *context, const char *line)
{
	(void)context;
	(void)line;
	lines++;
}

static void test_local(void)
{
	static char text[8192];
	int index, count;

	/* an addition: example.org trusted without a list */
	networks_trust_make(&trust, "chupathingy.org", (const unsigned char (*)[32])home_roots, 2, NULL,
		" Example.ORG " VECTOR_OTHER_ROOT_PRIMARY " " VECTOR_OTHER_ROOT_RECOVERY " ", "", NULL, NULL);
	CHECK(trust.count == 2 && trust.networks[1].source == 2 && !strcmp(trust.networks[1].domain, "example.org"));
	CHECK(networks_issuer_network(&trust, other_issuer, 112, VECTOR_NOW, NULL, &count) == 1);
	/* removed again: the list's and the addition's both */
	takes(VECTOR_LIST_DOCUMENT);
	networks_trust_make(&trust, "chupathingy.org", (const unsigned char (*)[32])home_roots, 2, &list,
		"example.org " VECTOR_OTHER_ROOT_PRIMARY, " EXAMPLE.org , other.net", NULL, NULL);
	CHECK(trust.count == 1 && networks_issuer_network(&trust, other_issuer, 112, VECTOR_NOW, NULL, NULL) == -1);
	/* a domain already trusted, or a key another has, is left out (and said) */
	lines = 0;
	networks_trust_make(&trust, "chupathingy.org", (const unsigned char (*)[32])home_roots, 2, &list,
		"example.org " K1 ", other.org " VECTOR_OTHER_ROOT_RECOVERY ", third.org " VECTOR_HOME_ROOT_PRIMARY,
		"", count_line, NULL);
	CHECK(trust.count == 2 && lines == 3);
	/* bad additions: each left out alone */
	lines = 0;
	networks_trust_make(&trust, "chupathingy.org", (const unsigned char (*)[32])home_roots, 2, NULL,
		"example.org, example.org zz, 127.0.0.1 " K1 ", xn--exmple-cua.org " K1 ", \xE2\x84\xAA.org " K1
		", example.org " K1 " " K1 ", a.org " K1 K1 ", b.org " K1 " " K1 " " K1 " " K1 " " K1
		", c.org 0100000000000000000000000000000000000000000000000000000000000000, ,, d.org " K1,
		"localhost, \xE2\x84\xAA" "example.org", count_line, NULL);
	CHECK(trust.count == 2 && !strcmp(trust.networks[1].domain, "d.org") && lines == 11);
	/* the first 16 entries alone */
	text[0] = 0;
	for (index = 0; index < 18; index++)
	{
		unsigned char seed[32], key[32];
		char hex[65], name[32];

		snprintf(name, sizeof(name), "local %d", index);
		identity_sha256(name, (int)strlen(name), seed);
		identity_public(seed, key);
		identity_hex(key, 32, hex);
		snprintf(text + strlen(text), sizeof(text) - strlen(text), "%sn%d.example.org %s", index ? "," : "", index,
			hex);
	}
	networks_trust_make(&trust, "chupathingy.org", (const unsigned char (*)[32])home_roots, 2, NULL, text, "", NULL,
		NULL);
	CHECK(trust.count == 17);
	/* a root key past its until vouches for nothing */
	takes(changed("\"key\": \"8b2f06a8475780f3e0f47305f664da217859814835b40e8bc548be9618e8f50a\",\n          \"until\": 0",
		"\"key\": \"8b2f06a8475780f3e0f47305f664da217859814835b40e8bc548be9618e8f50a\",\n          \"until\": 1790199999"));
	networks_trust_make(&trust, "chupathingy.org", (const unsigned char (*)[32])home_roots, 2, &list, "", "", NULL,
		NULL);
	CHECK(networks_issuer_network(&trust, other_issuer, 112, VECTOR_NOW, NULL, NULL) == -1);
	CHECK(networks_issuer_network(&trust, other_issuer, 112, 1790199999UL, NULL, NULL) == 1);
}

/* an issuer certificate: head's 48 bytes signed by the root key whose seed
is SHA-256 of seed_text */
static void issuer_signed(const unsigned char *head, const char *seed_text, unsigned char *out)
{
	static const char label[] = "chupathingy identity issuer v1\n";
	unsigned char seed[32], message[128];

	identity_sha256(seed_text, (int)strlen(seed_text), seed);
	memcpy(message, label, sizeof(label) - 1);
	memcpy(message + sizeof(label) - 1, head, 48);
	memcpy(out, head, 48);
	identity_sign(seed, message, (int)(sizeof(label) - 1 + 48), out + 48);
}

static void write32(unsigned char *out, unsigned long value)
{
	out[0] = (unsigned char)value;
	out[1] = (unsigned char)(value >> 8);
	out[2] = (unsigned char)(value >> 16);
	out[3] = (unsigned char)(value >> 24);
}

static void test_memory(void)
{
	static struct networks_memory memory;
	unsigned char seed[32], twin[112], third[112], head[48], other[112], roots[4][32], third_root[32];
	char hex[65], local[128];
	int index, count;

	memset(&memory, 0, sizeof(memory));
	takes(VECTOR_LIST_DOCUMENT);
	networks_trust_make(&trust, "chupathingy.org", (const unsigned char (*)[32])home_roots, 2, &list, "", "", NULL,
		NULL);
	/* remembered: a certificate seen is answered without a new look */
	CHECK(networks_issuer_lookup(&trust, &memory, home_issuer, 112, VECTOR_NOW, 0, roots, &count) == 0 &&
		count == 2);
	for (index = 0; index < 10; index++)
		CHECK(networks_issuer_lookup(&trust, &memory, home_issuer, 112, VECTOR_NOW, 0, NULL, NULL) == 0);
	CHECK(networks_issuer_lookup(&trust, &memory, other_issuer, 112, VECTOR_NOW, 0, NULL, NULL) == 1);
	/* limited: 4 new ones from a machine a minute, then none until the next */
	for (index = 0; index < 6; index++)
	{
		unsigned char made[112];

		memcpy(made, home_issuer, sizeof(made));
		made[100] ^= (unsigned char)(index + 1);
		CHECK(networks_issuer_lookup(&trust, &memory, made, 112, VECTOR_NOW, 5, NULL, NULL) ==
			(index < 4 ? NETWORKS_NONE : NETWORKS_LIMITED));
		/* (another machine has its own count) */
		if (index == 5)
			CHECK(networks_issuer_lookup(&trust, &memory, made, 112, VECTOR_NOW, 6, NULL, NULL) == NETWORKS_NONE);
	}
	{
		unsigned char made[112];

		memcpy(made, home_issuer, sizeof(made));
		made[100] ^= 0x77;
		CHECK(networks_issuer_lookup(&trust, &memory, made, 112, VECTOR_NOW + 60, 5, NULL, NULL) == NETWORKS_NONE);
	}
	/* a machine number out of range shares the last's count */
	CHECK(networks_issuer_lookup(&trust, &memory, home_issuer, 111, VECTOR_NOW, 999, NULL, NULL) == NETWORKS_NONE);
	/* one issuer, one network: example.org's root signing home's issuer key
	as well is refused for example.org alone; home's issuer stays */
	issuer_signed(home_issuer, "open network test root example.org primary", twin);
	networks_trust_make(&trust, "chupathingy.org", (const unsigned char (*)[32])home_roots, 2, &list, "", "", NULL,
		NULL);
	CHECK(networks_issuer_network(&trust, twin, 112, VECTOR_NOW, NULL, NULL) == 1);
	CHECK(networks_issuer_lookup(&trust, &memory, twin, 112, VECTOR_NOW + 120, 1, NULL, NULL) ==
		NETWORKS_TWO_NETWORKS);
	CHECK(networks_issuer_lookup(&trust, &memory, home_issuer, 112, VECTOR_NOW + 120, 0, roots, &count) == 0 &&
		count == 2);
	CHECK(networks_issuer_lookup(&trust, &memory, twin, 112, VECTOR_NOW + 120, 1, NULL, NULL) ==
		NETWORKS_TWO_NETWORKS);
	CHECK(memory.changes == 1 && memory.refused_count == 0);
	/* example.org's own issuer is untouched */
	CHECK(networks_issuer_lookup(&trust, &memory, other_issuer, 112, VECTOR_NOW + 120, 0, NULL, NULL) == 1);
	/* forgotten with a new trust: home's issuer still stands, the claim still
	refused */
	networks_memory_forget(&memory);
	CHECK(networks_issuer_lookup(&trust, &memory, home_issuer, 112, VECTOR_NOW + 120, 2, NULL, NULL) == 0);
	CHECK(networks_issuer_lookup(&trust, &memory, twin, 112, VECTOR_NOW + 120, 2, NULL, NULL) ==
		NETWORKS_TWO_NETWORKS);
	/* the claim seen first: taken until home's own turns up, then refused,
	and home's taken */
	memset(&memory, 0, sizeof(memory));
	CHECK(networks_issuer_lookup(&trust, &memory, twin, 112, VECTOR_NOW, 1, NULL, NULL) == 1);
	CHECK(networks_issuer_lookup(&trust, &memory, home_issuer, 112, VECTOR_NOW, 0, NULL, NULL) == 0);
	CHECK(networks_issuer_lookup(&trust, &memory, twin, 112, VECTOR_NOW, 1, NULL, NULL) == NETWORKS_TWO_NETWORKS);
	CHECK(networks_issuer_lookup(&trust, &memory, home_issuer, 112, VECTOR_NOW, 0, NULL, NULL) == 0);
	CHECK(memory.changes == 1 && memory.refused_count == 0);
	/* two other networks' proven claims on one key refuse it for both */
	identity_sha256("open network test root third.org", 32, seed);
	identity_public(seed, third_root);
	identity_hex(third_root, 32, hex);
	snprintf(local, sizeof(local), "third.org %s", hex);
	networks_trust_make(&trust, "chupathingy.org", (const unsigned char (*)[32])home_roots, 2, &list, local, "", NULL,
		NULL);
	CHECK(trust.count == 3);
	issuer_signed(other_issuer, "open network test root third.org", third);
	memset(&memory, 0, sizeof(memory));
	CHECK(networks_issuer_lookup(&trust, &memory, other_issuer, 112, VECTOR_NOW, 0, NULL, NULL) == 1);
	CHECK(networks_issuer_lookup(&trust, &memory, third, 112, VECTOR_NOW, 1, NULL, NULL) == NETWORKS_TWO_NETWORKS);
	CHECK(networks_issuer_lookup(&trust, &memory, other_issuer, 112, VECTOR_NOW + 60, 2, NULL, NULL) ==
		NETWORKS_TWO_NETWORKS);
	CHECK(memory.changes == 1 && memory.refused_count == 1);
	CHECK(networks_issuer_lookup(&trust, &memory, home_issuer, 112, VECTOR_NOW, 0, NULL, NULL) == 0);
	/* ...but never home's: two other networks' claims on home's key refuse
	the claims, home's issuer stays */
	issuer_signed(home_issuer, "open network test root third.org", third);
	memset(&memory, 0, sizeof(memory));
	CHECK(networks_issuer_lookup(&trust, &memory, twin, 112, VECTOR_NOW, 0, NULL, NULL) == 1);
	CHECK(networks_issuer_lookup(&trust, &memory, third, 112, VECTOR_NOW, 1, NULL, NULL) == NETWORKS_TWO_NETWORKS);
	CHECK(networks_issuer_lookup(&trust, &memory, home_issuer, 112, VECTOR_NOW, 2, NULL, NULL) == 0);
	CHECK(networks_issuer_lookup(&trust, &memory, twin, 112, VECTOR_NOW, 0, NULL, NULL) == NETWORKS_TWO_NETWORKS);
	CHECK(networks_issuer_lookup(&trust, &memory, home_issuer, 112, VECTOR_NOW + 60, 2, NULL, NULL) == 0);
	/* a second claim not fully proven refuses nothing: a certificate out of
	date, or signed by a root key past its until */
	memcpy(head, other_issuer, 48);
	write32(head + 8, VECTOR_NOW - 200000);
	write32(head + 12, VECTOR_NOW - 100000);
	issuer_signed(head, "open network test root third.org", third);
	memset(&memory, 0, sizeof(memory));
	CHECK(networks_issuer_lookup(&trust, &memory, other_issuer, 112, VECTOR_NOW, 0, NULL, NULL) == 1);
	CHECK(networks_issuer_lookup(&trust, &memory, third, 112, VECTOR_NOW, 1, NULL, NULL) == 2);
	CHECK(networks_issuer_lookup(&trust, &memory, other_issuer, 112, VECTOR_NOW, 0, NULL, NULL) == 1);
	CHECK(memory.changes == 0 && memory.refused_count == 0);
	takes(changed("\"key\": \"8b2f06a8475780f3e0f47305f664da217859814835b40e8bc548be9618e8f50a\",\n          \"until\": 0",
		"\"key\": \"8b2f06a8475780f3e0f47305f664da217859814835b40e8bc548be9618e8f50a\",\n          \"until\": 1790199999"));
	networks_trust_make(&trust, "chupathingy.org", (const unsigned char (*)[32])home_roots, 2, &list, local, "", NULL,
		NULL);
	issuer_signed(other_issuer, "open network test root third.org", third);
	memset(&memory, 0, sizeof(memory));
	CHECK(networks_issuer_lookup(&trust, &memory, third, 112, VECTOR_NOW, 1, NULL, NULL) == 2);
	CHECK(networks_issuer_lookup(&trust, &memory, other_issuer, 112, VECTOR_NOW, 0, NULL, NULL) == NETWORKS_NONE);
	CHECK(networks_issuer_lookup(&trust, &memory, third, 112, VECTOR_NOW, 1, NULL, NULL) == 2);
	CHECK(memory.changes == 0 && memory.refused_count == 0);
	/* (a home claim out of date takes nothing either: example.org's claim on
	that key still goes through to the date check) */
	takes(VECTOR_LIST_DOCUMENT);
	networks_trust_make(&trust, "chupathingy.org", (const unsigned char (*)[32])home_roots, 2, &list, "", "", NULL,
		NULL);
	memset(&memory, 0, sizeof(memory));
	CHECK(networks_issuer_lookup(&trust, &memory, home_issuer, 112, 1800000000UL, 0, NULL, NULL) == 0);
	CHECK(memory.home_key_count == 0);
	/* a key past its until: found, but vouches for nothing */
	memset(&memory, 0, sizeof(memory));
	memcpy(other, other_issuer, sizeof(other));
	CHECK(networks_issuer_lookup(&trust, &memory, other, 112, VECTOR_NOW, 3, NULL, NULL) == 1);
}

int main(void)
{
	load();
	test_vectors();
	test_home_unchanged();
	test_domains();
	test_names();
	test_list_malformed();
	test_epochs();
	test_local();
	test_memory();
	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("networks: all passed\n");
	return 0;
}
