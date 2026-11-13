/*
SC_NET.H

The Steam content client's connections (steam_content.h): TCP with TLS
(Mbed TLS, port/third_party/mbedtls; the certificate and host name always
checked), HTTP/1.1 requests on a kept-alive connection, and a WebSocket
client for the Steam CM (connection manager).

Mbed TLS as built here has no locks of its own, so every call into it is
made with the program's TLS lock held (steam_content_tls_lock), and only
for as long as the call takes: waiting on the network happens outside it.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/

#ifndef SC_NET_H
#define SC_NET_H

#include <stddef.h>

struct sc_connection;

/* host:port, with TLS: NULL with why in error (error[0] 'O' when the host
couldn't be reached at all: offline) */
struct sc_connection *sc_connect(const char *host, int port, int tls, int timeout_ms, const volatile int *cancel,
	char *error, int error_size);
void sc_close(struct sc_connection *connection);
int sc_send_all(struct sc_connection *connection, const void *data, size_t length);
/* bytes into buffer within timeout_ms: their count, 0 none yet, -1 the connection ended or failed */
int sc_receive(struct sc_connection *connection, void *buffer, size_t size, int timeout_ms);
/* exactly length bytes, or -1 (timeout_ms is for each wait) */
int sc_receive_exact(struct sc_connection *connection, void *buffer, size_t length, int timeout_ms);
/* the host it was opened to */
const char *sc_connection_host(const struct sc_connection *connection);

/* ---------- HTTP */

struct sc_http
{
	struct sc_connection *connection;
	char host[256];
	int tls;
	const volatile int *cancel;
};

/* one request on http's kept-alive connection (opened, or opened again for
another host): the status, the body malloc'ed in *body (NUL after it) with
its length; 0 status and why in error when there was no answer. Bodies
larger than maximum are refused */
int sc_http_request(struct sc_http *http, const char *host, int tls, const char *method, const char *path,
	const char *content_type, const void *request_body, size_t request_length, unsigned char **body, size_t *length,
	size_t maximum, char *error, int error_size);
void sc_http_close(struct sc_http *http);

/* ---------- WebSocket (client side: frames masked) */

/* the HTTP upgrade on an open connection: 1 when the server switched */
int sc_websocket_open(struct sc_connection *connection, const char *host, const char *path, char *error, int error_size);
/* one binary message */
int sc_websocket_send(struct sc_connection *connection, const void *data, size_t length);
/* the next whole binary message (pings answered, text passed over) within
timeout_ms: 1 with it in *message (malloc'ed), 0 none yet, -1 closed or failed */
int sc_websocket_receive(struct sc_connection *connection, unsigned char **message, size_t *length, int timeout_ms);

/* ---------- the program's side */

/* a random fill from Mbed TLS's generator (with the lock): 1 on success */
int sc_random(void *buffer, size_t length);

#endif
