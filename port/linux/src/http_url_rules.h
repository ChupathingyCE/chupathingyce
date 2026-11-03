/*
HTTP_URL_RULES.H

Which addresses the game's HTTP requests (posix_browser.c and its users)
may send a secret to: a token or key goes only to an https:// server, or
to a plain http:// one on this computer (127.0.0.1, localhost or [::1],
for tests), the host compared exactly, so that no other host whose name
merely starts the same way gets it.

Header only, so that every build that has the requests has it; tested by
port/linux/tests/http_url_rules_test.c (tools/test_http_url_rules.py).
*/

#ifndef HTTP_URL_RULES_H
#define HTTP_URL_RULES_H

#include <string.h>

/* whether text starts with prefix, ASCII letters in any case */
static inline int http_url_starts_with(const char *text, const char *prefix)
{
	for (; *prefix; text++, prefix++)
	{
		char a = *text, b = *prefix;

		if (a >= 'A' && a <= 'Z')
			a = (char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z')
			b = (char)(b - 'A' + 'a');
		if (a != b)
			return 0;
	}
	return 1;
}

/* whether url is https:// (with a host), or http:// to this computer: the
host exactly 127.0.0.1, localhost or [::1], then nothing, a path, or a
port of digits and then nothing or a path */
static inline int http_url_safe_for_secret(const char *url)
{
	static const char *const local_hosts[] = { "http://127.0.0.1", "http://localhost", "http://[::1]" };
	size_t index;

	if (!url)
		return 0;
	if (http_url_starts_with(url, "https://"))
		return url[8] && url[8] != '/' && url[8] != ':';
	for (index = 0; index < sizeof(local_hosts) / sizeof(local_hosts[0]); index++)
	{
		size_t length = strlen(local_hosts[index]);
		const char *rest = url + length;

		if (!http_url_starts_with(url, local_hosts[index]))
			continue;
		if (*rest == ':')
		{
			const char *digits = ++rest;

			while (*rest >= '0' && *rest <= '9')
				rest++;
			if (rest == digits || rest - digits > 5)
				return 0;
		}
		return !*rest || *rest == '/';
	}
	return 0;
}

#endif
