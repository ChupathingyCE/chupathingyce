/*
BROWSER_HTTP.H

The game list server's requests (browser.c, configure.py --game-browser),
on the host's C library: plain types only across this boundary.
*/

#ifndef __BROWSER_HTTP_H
#define __BROWSER_HTTP_H

/* one HTTP request to url (http:// or https://), with body as the body of
a POST when it is not NULL (of content_type, or form fields when that is
NULL), else a GET; the response's body, NUL terminated and cut to response_size - 1
bytes, in response. Returns the HTTP status, or 0 with the reason in error
when there was no answer. Blocks for up to about ten seconds: call it from
a thread of its own. */
int posix_browser_request(const char *url, const char *body, const char *content_type, char *response,
	int response_size, char *error, int error_size);

int posix_browser_private_key(const char *path, unsigned char *key, int size);
int posix_browser_replace_key(const char *path, const unsigned char *key, int size);

#endif
