/*
CONTROL_TLS.H

HTTPS for the dedicated server's control API and web admin page when they
listen beyond the machine (server/docs/admin.md): Mbed TLS
(port/third_party/mbedtls), TLS 1.2 (control_tls.c says why not 1.3),
ECDHE and AEAD cipher suites only, no renegotiation, no session tickets, ALPN http/1.1.

The certificate is the operator's own (PEM files in the data folder, as
certbot writes them: HALO_DEDICATED_CONTROL_CERT and _KEY), or else one the
server makes once and keeps: a self-signed ECDSA P-256 certificate
(control_tls.crt) and its key (control_tls.key, readable by its owner only),
made again only if they cannot be read, do not match or have expired. Its
SHA-256 fingerprint is printed at start, for the operator to check against
what the browser shows (no certificate authority vouches for it).

Plain types only, no game headers: built with the host's ABI, as
server_control.c, whose thread alone calls it (it is not thread-safe). For
a nonblocking poll loop: every call that would block returns
CONTROL_TLS_WANT_READ or CONTROL_TLS_WANT_WRITE, and
control_tls_poll_events says what to wait for.
*/

#ifndef CONTROL_TLS_H
#define CONTROL_TLS_H

#include <stddef.h>
#include <stdint.h>

enum
{
	/* a SHA-256 fingerprint as browsers show it, "AB:CD:...", with its end */
	CONTROL_TLS_FINGERPRINT_SIZE = 32 * 3,
	/* the operator's certificate files are looked at for a change (a
	renewal) at most this often */
	CONTROL_TLS_RELOAD_SECONDS = 60,
};

/* what the calls below return, beside a count of bytes */
enum
{
	CONTROL_TLS_DONE = 0,
	/* wait for the socket to be readable (or writable), and call again */
	CONTROL_TLS_WANT_READ = -1,
	CONTROL_TLS_WANT_WRITE = -2,
	/* the connection is broken: free it */
	CONTROL_TLS_ERROR = -3,
	/* the peer closed the connection (close_notify, or the end of the
	stream) */
	CONTROL_TLS_CLOSED = -4,
	/* (control_tls_handshake, before anything is read) the peer speaks
	plain HTTP, not TLS: answer it with a plain response saying so, then
	free it */
	CONTROL_TLS_NOT_TLS = -5,
};

#define CONTROL_TLS_CERTIFICATE_FILE "control_tls.crt"
#define CONTROL_TLS_KEY_FILE "control_tls.key"

/* TLS for the listener: the operator's certificate and key (cert_path and
key_path, both or neither: absolute, or relative to data_root), else the
server's own in data_root, made now if need be, named common_name (NULL:
"chupathingyce-server"), with no connection open. 1, with the certificate's fingerprint in
fingerprint (CONTROL_TLS_FINGERPRINT_SIZE bytes) and, in *made, whether a
new certificate was made now (it may be NULL); else 0 and why in error */
int control_tls_start(const char *data_root, const char *cert_path, const char *key_path, const char *common_name,
	char *fingerprint, size_t fingerprint_size, int *made, char *error, size_t error_size);

/* everything freed: free every connection first (they use the random
generator freed here) */
void control_tls_stop(void);

/* the fingerprint of the certificate in use ("" before a start) */
const char *control_tls_fingerprint(void);

/* whether the certificate is the server's own (self-signed), not the
operator's */
int control_tls_self_signed(void);

/* the operator's files read again if they changed (now: seconds, any clock
that does not go back; looked at no more often than
CONTROL_TLS_RELOAD_SECONDS): 1 if a new certificate is used from now on
(its fingerprint in control_tls_fingerprint), 0 if nothing changed, -1 if
the new files could not be used (the old certificate stays; why in
error). Connections already open keep the certificate they began with */
int control_tls_reload_if_changed(int64_t now, char *error, size_t error_size);

/* ---------- connections */

struct control_tls_connection;

/* a TLS connection over an accepted, nonblocking socket (which the caller
still owns and closes): NULL if there is no memory, or before a start */
struct control_tls_connection *control_tls_accept(int fd);

/* the handshake, a step: CONTROL_TLS_DONE once done, WANT_READ or
WANT_WRITE until then, ERROR, CLOSED, or NOT_TLS (the first bytes are
plain HTTP's) */
int control_tls_handshake(struct control_tls_connection *connection);

/* decrypted bytes into buffer: how many (more than 0), or WANT_READ,
WANT_WRITE, CLOSED or ERROR */
long control_tls_read(struct control_tls_connection *connection, void *buffer, size_t size);

/* bytes sent: how many were taken (more than 0), or WANT_READ, WANT_WRITE
or ERROR. After WANT_*, call again with the same bytes */
long control_tls_write(struct control_tls_connection *connection, const void *data, size_t size);

/* whether decrypted bytes are waiting already (poll will not say so) */
int control_tls_pending(const struct control_tls_connection *connection);

/* the poll events (POLLIN, POLLOUT) the last call that returned WANT_* is
waiting for */
short control_tls_poll_events(const struct control_tls_connection *connection);

/* the TLS close_notify, a step: DONE, WANT_READ or WANT_WRITE, or ERROR */
int control_tls_close_notify(struct control_tls_connection *connection);

/* the protocol agreed ("TLSv1.3", "TLSv1.2"), after the handshake */
const char *control_tls_version(const struct control_tls_connection *connection);

void control_tls_free(struct control_tls_connection *connection);

#endif
