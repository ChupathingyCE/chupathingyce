/*
DIRECT_ADDRESS_TEST.C

port/linux/src/direct_address.h: the addresses a player types to join a game
directly (IPv4, IPv6 bare and in brackets, host names, ports), what isn't
one, and how an entered address is shown (never again as typed). Run by
tools/test_direct_address.py.
*/

#include "../src/direct_address.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { printf("%s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)

static void ipv4(const char *text, unsigned long expected, unsigned short port)
{
	struct direct_address address;
	enum direct_address_result result = direct_address_parse(text, &address);

	if (result != _direct_address_ok || address.kind != _direct_address_ipv4 || address.ipv4 != expected ||
		address.port != port)
	{
		printf("ipv4 \"%s\": result %d kind %d %08lx port %u\n", text, (int)result, (int)address.kind, address.ipv4,
			address.port);
		failures++;
	}
}

static void ipv6(const char *text, const char *hex, unsigned short port)
{
	struct direct_address address;
	enum direct_address_result result = direct_address_parse(text, &address);
	char got[33];
	int index;

	for (index = 0; index < 16; index++)
		snprintf(got + 2 * index, 3, "%02x", address.ipv6[index]);
	if (result != _direct_address_ok || address.kind != _direct_address_ipv6 || strcmp(got, hex) ||
		address.port != port)
	{
		printf("ipv6 \"%s\": result %d kind %d %s port %u\n", text, (int)result, (int)address.kind, got, address.port);
		failures++;
	}
}

static void name(const char *text, const char *host, unsigned short port)
{
	struct direct_address address;
	enum direct_address_result result = direct_address_parse(text, &address);

	if (result != _direct_address_ok || address.kind != _direct_address_name || strcmp(address.host, host) ||
		address.port != port)
	{
		printf("name \"%s\": result %d kind %d \"%s\" port %u\n", text, (int)result, (int)address.kind, address.host,
			address.port);
		failures++;
	}
}

static void refused(const char *text, enum direct_address_result expected)
{
	struct direct_address address;
	enum direct_address_result result = direct_address_parse(text, &address);

	if (result != expected)
	{
		printf("refused \"%s\": result %d, wanted %d (kind %d)\n", text ? text : "(null)", (int)result, (int)expected,
			(int)address.kind);
		failures++;
	}
}

int main(void)
{
	char shown[64];
	char long_name[300];

	/* IPv4 */
	ipv4("192.168.1.20", 0xC0A80114UL, 0);
	ipv4("  10.0.0.5  ", 0x0A000005UL, 0);
	ipv4("127.0.0.1:5150", 0x7F000001UL, 5150);
	ipv4("203.0.113.45:1", 0xCB00712DUL, 1);
	ipv4("203.0.113.45:65535", 0xCB00712DUL, 65535);
	ipv4("0.0.0.0", 0, 0);
	ipv4("255.255.255.255", 0xFFFFFFFFUL, 0);
	ipv4("010.001.002.003", 0x0A010203UL, 0);
	/* (IPv4 in IPv6's form) */
	ipv4("::ffff:192.0.2.1", 0xC0000201UL, 0);
	ipv4("[::ffff:192.0.2.1]:2302", 0xC0000201UL, 2302);
	ipv4("[::ffff:c000:201]", 0xC0000201UL, 0);

	/* IPv6 */
	ipv6("::1", "00000000000000000000000000000001", 0);
	ipv6("::", "00000000000000000000000000000000", 0);
	ipv6("2001:db8::1", "20010db8000000000000000000000001", 0);
	ipv6("[2001:db8::1]:5150", "20010db8000000000000000000000001", 5150);
	ipv6("[2001:DB8:0:0:0:0:0:7]", "20010db8000000000000000000000007", 0);
	ipv6("fe80::1:2", "fe800000000000000000000000010002", 0);
	ipv6("1:2:3:4:5:6:7:8", "00010002000300040005000600070008", 0);
	ipv6("1::", "00010000000000000000000000000000", 0);
	ipv6("64:ff9b::192.0.2.33", "0064ff9b0000000000000000c0000221", 0);

	/* host names */
	name("halo.example.com", "halo.example.com", 0);
	name("Halo.Example.COM:5150", "halo.example.com", 5150);
	name("localhost", "localhost", 0);
	name("my-server", "my-server", 0);
	name("server1.lan.", "server1.lan", 0);
	name("3com.example", "3com.example", 0);

	/* garbage */
	refused("", _direct_address_empty);
	refused("   ", _direct_address_empty);
	refused(NULL, _direct_address_empty);
	refused("1.2.3", _direct_address_bad);
	refused("1.2.3.4.5", _direct_address_bad);
	refused("256.1.1.1", _direct_address_bad);
	refused("1.2.3.4:", _direct_address_bad_port);
	refused("1.2.3.4:0", _direct_address_bad_port);
	refused("1.2.3.4:65536", _direct_address_bad_port);
	refused("1.2.3.4:-1", _direct_address_bad_port);
	refused("1.2.3.4:12a", _direct_address_bad_port);
	refused("1.2.3.4:5150:1", _direct_address_bad);
	refused("[2001:db8::1", _direct_address_bad);
	refused("[2001:db8::1]5150", _direct_address_bad);
	refused("[2001:db8::1]:", _direct_address_bad_port);
	refused("[halo.example.com]", _direct_address_bad);
	refused("2001:db8::1::2", _direct_address_bad);
	refused("1:2:3:4:5:6:7:8:9", _direct_address_bad);
	refused("1:2:3:4:5:6:7", _direct_address_bad);
	refused("12345::1", _direct_address_bad);
	refused("g::1", _direct_address_bad);
	refused("-halo.example.com", _direct_address_bad);
	refused("halo-.example.com", _direct_address_bad);
	refused("halo..example.com", _direct_address_bad);
	refused("halo example.com", _direct_address_bad);
	refused("halo_example.com", _direct_address_bad);
	refused("halo://join/0123", _direct_address_bad);
	refused("http://1.2.3.4", _direct_address_bad);
	refused("1.2.3.4/24", _direct_address_bad);
	refused("123", _direct_address_bad);
	memset(long_name, 'a', sizeof(long_name) - 1);
	long_name[sizeof(long_name) - 1] = 0;
	refused(long_name, _direct_address_bad);
	/* (a label of 64) */
	memset(long_name, 'a', 64);
	strcpy(long_name + 64, ".com");
	refused(long_name, _direct_address_bad);

	/* shown: an address never again as typed; a host name as it is */
	direct_address_shown("203.0.113.45:5150", shown, sizeof(shown));
	CHECK(!strcmp(shown, "*****************"));
	CHECK(!strstr(shown, "203"));
	direct_address_shown("[2001:db8::1]", shown, sizeof(shown));
	CHECK(!strchr(shown, '2') && strlen(shown) == strlen("[2001:db8::1]"));
	direct_address_shown("halo.example.com:5150", shown, sizeof(shown));
	CHECK(!strcmp(shown, "halo.example.com:5150"));
	direct_address_shown("", shown, sizeof(shown));
	CHECK(!strcmp(shown, ""));
	direct_address_shown("203.0.113.45", shown, 4);
	CHECK(!strcmp(shown, "***"));
	/* (only a host name shown as typed: an address, with a bad port or a
	path too, and anything that is not a host name never) */
	CHECK(!direct_address_shown_as_typed("10.0.0.1"));
	CHECK(!direct_address_shown_as_typed("::1"));
	CHECK(direct_address_shown_as_typed("halo.example.com"));
	CHECK(!direct_address_shown_as_typed("not an address"));
	CHECK(!direct_address_shown_as_typed("203.0.113.45:99999"));
	CHECK(!direct_address_shown_as_typed("[2001:db8::1]:0"));
	CHECK(!direct_address_shown_as_typed("203.0.113.45:5150/x"));
	direct_address_shown("203.0.113.45:0", shown, sizeof(shown));
	CHECK(!strstr(shown, "203"));
	CHECK(direct_address_shown_as_typed("halo.example.com:99999"));
	/* (links, ports too many, IPv6-like text, too long: each may hold an
	address, so masked) */
	{
		static const char *const masked[] = {
			"http://203.0.113.45",
			"http://203.0.113.45:2302",
			"https://halo.example.com:2302/join",
			"halo://203.0.113.45",
			"1.2.3.4:5:6",
			"203.0.113.45:2302:1",
			"2001:db8::1::2",
			"2001:db8:zz::1",
			"[2001:db8::1]x",
			"[2001:db8::1",
			"fe80::1%en0",
			"203.0.113.45 2302",
		};
		size_t index;

		for (index = 0; index < sizeof(masked) / sizeof(masked[0]); index++)
		{
			if (direct_address_shown_as_typed(masked[index]))
			{
				printf("shown as typed: \"%s\"\n", masked[index]);
				failures++;
			}
			direct_address_shown(masked[index], shown, sizeof(shown));
			CHECK(!strstr(shown, "203") && !strstr(shown, "2001") && !strstr(shown, "fe80"));
		}
	}
	{
		char long_text[400];
		char long_shown[400];

		/* (an address after text too long to parse) */
		memset(long_text, 'a', sizeof(long_text));
		strcpy(long_text + sizeof(long_text) - 20, " 203.0.113.45");
		CHECK(!direct_address_shown_as_typed(long_text));
		direct_address_shown(long_text, long_shown, sizeof(long_shown));
		CHECK(!strstr(long_shown, "203"));
	}

	/* a saved one's name */
	direct_address_saved_name("", "203.0.113.45", 2, shown, sizeof(shown));
	CHECK(!strcmp(shown, "Saved server 2"));
	direct_address_saved_name("Friday LAN", "203.0.113.45", 2, shown, sizeof(shown));
	CHECK(!strcmp(shown, "Friday LAN"));
	direct_address_saved_name(NULL, "halo.example.com", 1, shown, sizeof(shown));
	CHECK(!strcmp(shown, "halo.example.com"));
	direct_address_saved_name(NULL, "http://203.0.113.45", 3, shown, sizeof(shown));
	CHECK(!strcmp(shown, "Saved server 3"));
	direct_address_saved_name(NULL, "1.2.3.4:5:6", 4, shown, sizeof(shown));
	CHECK(!strcmp(shown, "Saved server 4"));

	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("direct_address: all passed\n");
	return 0;
}
