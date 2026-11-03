/*
HTTP_URL_RULES_TEST.C

Tests of which addresses may get a secret (port/linux/src/http_url_rules.h).
Built and run by tools/test_http_url_rules.py; exits nonzero on a failure.
*/

#include "../src/http_url_rules.h"

#include <stdio.h>

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

static void test_safe_for_secret(void)
{
	CHECK(http_url_safe_for_secret("https://stats.example.org"));
	CHECK(http_url_safe_for_secret("https://stats.example.org/v1/events"));
	CHECK(http_url_safe_for_secret("HTTPS://stats.example.org/"));
	CHECK(http_url_safe_for_secret("http://127.0.0.1"));
	CHECK(http_url_safe_for_secret("http://127.0.0.1/v1/events"));
	CHECK(http_url_safe_for_secret("http://127.0.0.1:8080/v1/events"));
	CHECK(http_url_safe_for_secret("http://localhost"));
	CHECK(http_url_safe_for_secret("http://localhost/"));
	CHECK(http_url_safe_for_secret("http://LOCALHOST:3000/v1/events"));
	CHECK(http_url_safe_for_secret("http://[::1]:8080/"));

	CHECK(!http_url_safe_for_secret(NULL));
	CHECK(!http_url_safe_for_secret(""));
	CHECK(!http_url_safe_for_secret("https://"));
	CHECK(!http_url_safe_for_secret("https:///path"));
	CHECK(!http_url_safe_for_secret("http://stats.example.org/v1/events"));
	CHECK(!http_url_safe_for_secret("http://localhost.example.org/v1/events"));
	CHECK(!http_url_safe_for_secret("http://localhost.example.org"));
	CHECK(!http_url_safe_for_secret("http://localhostexample.org/"));
	CHECK(!http_url_safe_for_secret("http://127.0.0.1.example.org/"));
	CHECK(!http_url_safe_for_secret("http://127.0.0.10/"));
	CHECK(!http_url_safe_for_secret("http://localhost@example.org/"));
	CHECK(!http_url_safe_for_secret("http://127.0.0.1@example.org/"));
	CHECK(!http_url_safe_for_secret("http://127.0.0.1:/"));
	CHECK(!http_url_safe_for_secret("http://127.0.0.1:80x/"));
	CHECK(!http_url_safe_for_secret("http://127.0.0.1:123456/"));
	CHECK(!http_url_safe_for_secret("http://localhost?x"));
	CHECK(!http_url_safe_for_secret("ftp://localhost/"));
}

int main(void)
{
	test_safe_for_secret();
	if (failures)
	{
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("ok\n");
	return 0;
}
