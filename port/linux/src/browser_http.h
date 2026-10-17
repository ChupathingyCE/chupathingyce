/*
BROWSER_HTTP.H

The game list server's requests (browser.c, configure.py --game-browser),
on the host's C library: plain types only across this boundary.
*/

#ifndef __BROWSER_HTTP_H
#define __BROWSER_HTTP_H

#include <stddef.h>

/* one HTTP request to url (http:// or https://), with body as the body of
a POST when it is not NULL (of content_type, or form fields when that is
NULL), else a GET; the response's body, NUL terminated and cut to response_size - 1
bytes, in response. Returns the HTTP status, or 0 with the reason in error
when there was no answer. Blocks for up to about ten seconds: call it from
a thread of its own. */
int posix_browser_request(const char *url, const char *body, const char *content_type, char *response,
	int response_size, char *error, int error_size);
/* the same, as user_agent (the crash reports': build_identity_user_agent) */
int posix_browser_request_as(const char *url, const char *body, const char *content_type, const char *user_agent,
	char *response, int response_size, char *error, int error_size);
/* the same, with a body of body_length bytes (any bytes: a gzip member) when
body is not NULL, and more header lines ("Name: value\r\n" each) when
headers is not NULL */
int posix_browser_send(const char *url, const char *body, size_t body_length, const char *content_type,
	const char *headers, char *response, int response_size, char *error, int error_size);

/* a download (a GET of url, http:// or https://, redirects followed, as
user_agent) opened: its handle (1), with the body's length in *length (-1
when the server does not say); else 0, with the HTTP status in *status (0
when there was no answer) and why in error. One download at a time, and
no other request until it is closed (they wait): call it from a thread of
its own */
int posix_browser_download_open(const char *url, const char *user_agent, int *status, int *length, char *error,
	int error_size);
/* up to size more of the body's bytes into buffer: their count, 0 at its
end, negative on an error (each read waits about ten seconds at most) */
int posix_browser_download_read(int download, void *buffer, int size);
void posix_browser_download_close(int download);
/* taken around every use of Mbed TLS, which as built here has no locks of
its own: the requests above take it themselves; the self-updater's
downloads (posix_update.c) take it too */
void posix_browser_lock(void);
void posix_browser_unlock(void);

int posix_browser_private_key(const char *path, unsigned char *key, int size);
int posix_browser_replace_key(const char *path, const unsigned char *key, int size);

#endif
