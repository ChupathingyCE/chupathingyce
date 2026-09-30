/*
BROWSER_HTTP.H

The game list server's requests (browser.c, configure.py --new-networking),
on the host's C library: plain types only across this boundary.
*/

#ifndef __BROWSER_HTTP_H
#define __BROWSER_HTTP_H

/* one HTTP request to url (http:// or https://), with form as an
application/x-www-form-urlencoded body when it is not NULL (a POST, else a
GET); the response's body, NUL terminated and cut to response_size - 1
bytes, in response. Returns the HTTP status, or 0 with the reason in error
when there was no answer. Blocks for up to about ten seconds: call it from
a thread of its own. */
int posix_browser_request(const char *url, const char *form, char *response, int response_size, char *error,
	int error_size);

#endif
