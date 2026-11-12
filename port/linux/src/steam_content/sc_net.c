/*
SC_NET.C

The Steam content client's connections (sc_net.h). Sockets are
non-blocking: a wait is a poll() outside the TLS lock, and each Mbed TLS
call takes the lock only for its own short run.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h): a console builds this to nothing */

#include "sc_net.h"
#include "sc_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_type;
#define BAD_SOCKET INVALID_SOCKET
#define poll WSAPoll
#define close_socket closesocket
#define WOULD_BLOCK(e) ((e) == WSAEWOULDBLOCK || (e) == WSAEINPROGRESS)
#define socket_error() WSAGetLastError()
#else
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_type;
#define BAD_SOCKET (-1)
#define close_socket close
#define WOULD_BLOCK(e) ((e) == EINPROGRESS || (e) == EWOULDBLOCK || (e) == EAGAIN)
#define socket_error() errno
#endif

#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/psa_util.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

struct sc_connection
{
	socket_type fd;
	int tls;
	char host[256];
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config config;
};

/* (a send on a connection the server reset fails, not kills the program: Linux) */
#ifdef MSG_NOSIGNAL
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0
#endif

/* ---------- the lock and the certificate authorities */

static void lock(void)
{
	if (sc_host.tls_lock)
		sc_host.tls_lock();
}

static void unlock(void)
{
	if (sc_host.tls_unlock)
		sc_host.tls_unlock();
}

static mbedtls_x509_crt own_certificates;
static int own_certificates_state;

/* (the lock held) */
static mbedtls_x509_crt *certificates(void)
{
	static const char *const bundles[] = { "/etc/ssl/cert.pem", "/etc/ssl/certs/ca-certificates.crt",
		"/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/ca-bundle.pem", "/usr/local/share/certs/ca-root-nss.crt" };
	size_t index;

	if (sc_host.certificates)
		return (mbedtls_x509_crt *)sc_host.certificates();
	if (!own_certificates_state)
	{
		const char *named = getenv("SSL_CERT_FILE");

		own_certificates_state = -1;
		mbedtls_x509_crt_init(&own_certificates);
		/* (a bundle named in the environment first, as OpenSSL reads it: the
		tools and tests outside the game, which has the system's own) */
		if (named && named[0] && mbedtls_x509_crt_parse_file(&own_certificates, named) >= 0)
			own_certificates_state = 1;
		for (index = 0; index < sizeof(bundles) / sizeof(bundles[0]); index++)
		{
			if (mbedtls_x509_crt_parse_file(&own_certificates, bundles[index]) >= 0)
				own_certificates_state = 1;
		}
	}
	return own_certificates_state > 0 ? &own_certificates : NULL;
}

int sc_random(void *buffer, size_t length)
{
	int ok;

	lock();
	ok = psa_crypto_init() == PSA_SUCCESS && psa_generate_random((uint8_t *)buffer, length) == PSA_SUCCESS;
	unlock();
	return ok;
}

/* ---------- sockets */

static void set_nonblocking(socket_type fd)
{
#ifdef _WIN32
	u_long on = 1;

	ioctlsocket(fd, FIONBIO, &on);
#else
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#endif
}

/* 1 ready, 0 timed out, -1 failed */
static int wait_socket(socket_type fd, int write, int timeout_ms)
{
	struct pollfd entry;
	int result;

	entry.fd = fd;
	entry.events = write ? POLLOUT : POLLIN;
	entry.revents = 0;
	result = poll(&entry, 1, timeout_ms);
	if (result < 0)
		return socket_error() == EINTR ? 0 : -1;
	if (!result)
		return 0;
	return 1;
}

/* waits in slices so a cancel is seen within a quarter second */
static int wait_cancellable(socket_type fd, int write, int timeout_ms, const volatile int *cancel)
{
	long long end = sc_milliseconds() + timeout_ms;

	for (;;)
	{
		long long left = end - sc_milliseconds();
		int result;

		if (cancel && *cancel)
			return -1;
		if (left <= 0)
			return 0;
		result = wait_socket(fd, write, left > 250 ? 250 : (int)left);
		if (result)
			return result;
	}
}

static socket_type connect_address(const struct addrinfo *address, int timeout_ms, const volatile int *cancel)
{
	socket_type fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
	int error = 0;
	socklen_t size = sizeof(error);

	if (fd == BAD_SOCKET)
		return BAD_SOCKET;
#ifdef SO_NOSIGPIPE
	/* (a send on a connection the server reset fails, not kills the program: macOS, BSD) */
	{
		int set = 1;

		setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(set));
	}
#endif
	set_nonblocking(fd);
	if (connect(fd, address->ai_addr, (socklen_t)address->ai_addrlen) != 0)
	{
		if (!WOULD_BLOCK(socket_error()) || wait_cancellable(fd, 1, timeout_ms, cancel) != 1 ||
			getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&error, &size) != 0 || error)
		{
			close_socket(fd);
			return BAD_SOCKET;
		}
	}
	{
		int on = 1;

		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(on));
	}
	return fd;
}

/* ---------- Mbed TLS's input and output, non-blocking */

static int bio_send(void *context, const unsigned char *data, size_t length)
{
	struct sc_connection *connection = (struct sc_connection *)context;
	int sent = (int)send(connection->fd, (const char *)data, (int)length, SEND_FLAGS);

	if (sent < 0)
		return WOULD_BLOCK(socket_error()) ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
	return sent;
}

static int bio_receive(void *context, unsigned char *data, size_t length)
{
	struct sc_connection *connection = (struct sc_connection *)context;
	int count = (int)recv(connection->fd, (char *)data, (int)length, 0);

	if (count < 0)
		return WOULD_BLOCK(socket_error()) ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
	if (count == 0)
		return MBEDTLS_ERR_NET_CONN_RESET;
	return count;
}

static void tls_error(char *error, int error_size, const char *what, int code)
{
	char reason[128];

	lock();
	mbedtls_strerror(code, reason, sizeof(reason));
	unlock();
	snprintf(error, (size_t)error_size, "%s (%s)", what, reason);
}

static int handshake(struct sc_connection *connection, int timeout_ms, const volatile int *cancel, char *error,
	int error_size)
{
	long long end = sc_milliseconds() + timeout_ms;
	mbedtls_x509_crt *authorities;
	int result;

	lock();
	authorities = (result = psa_crypto_init()) == PSA_SUCCESS ? certificates() : NULL;
	if (authorities)
	{
		mbedtls_ssl_init(&connection->ssl);
		mbedtls_ssl_config_init(&connection->config);
		result = mbedtls_ssl_config_defaults(&connection->config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
			MBEDTLS_SSL_PRESET_DEFAULT);
		if (!result)
		{
			mbedtls_ssl_conf_authmode(&connection->config, MBEDTLS_SSL_VERIFY_REQUIRED);
			mbedtls_ssl_conf_ca_chain(&connection->config, authorities, NULL);
			mbedtls_ssl_conf_rng(&connection->config, mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE);
			result = mbedtls_ssl_setup(&connection->ssl, &connection->config);
		}
		if (!result)
			result = mbedtls_ssl_set_hostname(&connection->ssl, connection->host);
		if (!result)
			mbedtls_ssl_set_bio(&connection->ssl, connection, bio_send, bio_receive, NULL);
	}
	unlock();
	if (!authorities)
	{
		snprintf(error, (size_t)error_size, "no certificate authorities");
		return 0;
	}
	connection->tls = 1;
	if (result)
	{
		tls_error(error, error_size, "TLS setup failed", result);
		return 0;
	}
	for (;;)
	{
		long long left = end - sc_milliseconds();

		lock();
		result = mbedtls_ssl_handshake(&connection->ssl);
		unlock();
		if (!result)
			return 1;
		if ((result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) || left <= 0 ||
			wait_cancellable(connection->fd, result == MBEDTLS_ERR_SSL_WANT_WRITE, (int)left, cancel) != 1)
		{
			unsigned flags;

			lock();
			flags = mbedtls_ssl_get_verify_result(&connection->ssl);
			unlock();
			if (flags && flags != (unsigned)-1)
				snprintf(error, (size_t)error_size, "%s's certificate was refused (%#x)", connection->host, flags);
			else if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE)
				snprintf(error, (size_t)error_size, "TLS handshake with %s timed out", connection->host);
			else
				tls_error(error, error_size, "TLS handshake failed", result);
			return 0;
		}
	}
}

struct sc_connection *sc_connect(const char *host, int port, int tls, int timeout_ms, const volatile int *cancel,
	char *error, int error_size)
{
	struct addrinfo hints, *addresses = NULL, *address;
	struct sc_connection *connection;
	char service[16];
	int pass;

	error[0] = 0;
#ifdef _WIN32
	/* (Winsock started once by us too: the game has, a tool may not have) */
	{
		static LONG started;

		if (!InterlockedExchange(&started, 1))
		{
			WSADATA data;

			WSAStartup(MAKEWORD(2, 2), &data);
		}
	}
#endif
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	snprintf(service, sizeof(service), "%d", port);
	if (getaddrinfo(host, service, &hints, &addresses) != 0 || !addresses)
	{
		snprintf(error, (size_t)error_size, "Offline: %s could not be looked up", host);
		return NULL;
	}
	connection = (struct sc_connection *)calloc(1, sizeof(*connection));
	if (!connection)
	{
		freeaddrinfo(addresses);
		snprintf(error, (size_t)error_size, "out of memory");
		return NULL;
	}
	connection->fd = BAD_SOCKET;
	snprintf(connection->host, sizeof(connection->host), "%s", host);
	/* (IPv4 first: a computer with a broken IPv6 route would wait out every attempt) */
	for (pass = 0; pass < 2 && connection->fd == BAD_SOCKET; pass++)
	{
		for (address = addresses; address && connection->fd == BAD_SOCKET; address = address->ai_next)
		{
			if ((address->ai_family == AF_INET) == (pass == 0))
				connection->fd = connect_address(address, timeout_ms, cancel);
			if (cancel && *cancel)
				break;
		}
	}
	freeaddrinfo(addresses);
	if (connection->fd == BAD_SOCKET)
	{
		snprintf(error, (size_t)error_size, "Offline: could not connect to %s", host);
		free(connection);
		return NULL;
	}
	if (tls && !handshake(connection, timeout_ms, cancel, error, error_size))
	{
		sc_close(connection);
		return NULL;
	}
	return connection;
}

void sc_close(struct sc_connection *connection)
{
	if (!connection)
		return;
	if (connection->tls)
	{
		lock();
		mbedtls_ssl_close_notify(&connection->ssl);
		mbedtls_ssl_free(&connection->ssl);
		mbedtls_ssl_config_free(&connection->config);
		unlock();
	}
	if (connection->fd != BAD_SOCKET)
		close_socket(connection->fd);
	free(connection);
}

const char *sc_connection_host(const struct sc_connection *connection)
{
	return connection->host;
}

int sc_send_all(struct sc_connection *connection, const void *data, size_t length)
{
	const unsigned char *at = (const unsigned char *)data;

	while (length)
	{
		int sent;

		if (connection->tls)
		{
			lock();
			sent = mbedtls_ssl_write(&connection->ssl, at, length);
			unlock();
		}
		else
			sent = bio_send(connection, at, length);
		if (sent == MBEDTLS_ERR_SSL_WANT_WRITE || sent == MBEDTLS_ERR_SSL_WANT_READ)
		{
			if (wait_socket(connection->fd, sent == MBEDTLS_ERR_SSL_WANT_WRITE, 30000) != 1)
				return 0;
			continue;
		}
		if (sent <= 0)
			return 0;
		at += sent;
		length -= (size_t)sent;
	}
	return 1;
}

int sc_receive(struct sc_connection *connection, void *buffer, size_t size, int timeout_ms)
{
	long long end = sc_milliseconds() + timeout_ms;

	for (;;)
	{
		int count;
		long long left;

		if (connection->tls)
		{
			lock();
			count = mbedtls_ssl_read(&connection->ssl, (unsigned char *)buffer, size);
			unlock();
		}
		else
			count = bio_receive(connection, (unsigned char *)buffer, size);
		if (count > 0)
			return count;
		if (count != MBEDTLS_ERR_SSL_WANT_READ && count != MBEDTLS_ERR_SSL_WANT_WRITE)
			return -1;
		left = end - sc_milliseconds();
		if (left <= 0)
			return 0;
		if (wait_socket(connection->fd, count == MBEDTLS_ERR_SSL_WANT_WRITE, (int)left) < 0)
			return -1;
	}
}

int sc_receive_exact(struct sc_connection *connection, void *buffer, size_t length, int timeout_ms)
{
	unsigned char *at = (unsigned char *)buffer;

	while (length)
	{
		int count = sc_receive(connection, at, length, timeout_ms);

		if (count <= 0)
			return -1;
		at += count;
		length -= (size_t)count;
	}
	return 1;
}

/* ---------- HTTP */

/* a line of the response into line (the CRLF dropped), from the connection's pending bytes */
struct reader
{
	struct sc_connection *connection;
	unsigned char buffer[16384];
	size_t start;
	size_t end;
};

static int reader_fill(struct reader *reader)
{
	int count;

	if (reader->start == reader->end)
		reader->start = reader->end = 0;
	if (reader->end == sizeof(reader->buffer))
		return -1;
	count = sc_receive(reader->connection, reader->buffer + reader->end, sizeof(reader->buffer) - reader->end, 30000);
	if (count <= 0)
		return -1;
	reader->end += (size_t)count;
	return 1;
}

static int reader_line(struct reader *reader, char *line, size_t size)
{
	for (;;)
	{
		size_t index;

		for (index = reader->start; index + 1 < reader->end; index++)
		{
			if (reader->buffer[index] == '\r' && reader->buffer[index + 1] == '\n')
			{
				size_t length = index - reader->start;

				if (length >= size)
					length = size - 1;
				memcpy(line, reader->buffer + reader->start, length);
				line[length] = 0;
				reader->start = index + 2;
				return 1;
			}
		}
		if (reader->start)
		{
			memmove(reader->buffer, reader->buffer + reader->start, reader->end - reader->start);
			reader->end -= reader->start;
			reader->start = 0;
		}
		if (reader_fill(reader) < 0)
			return 0;
	}
}

static int reader_bytes(struct reader *reader, unsigned char *out, size_t length)
{
	while (length)
	{
		size_t take = reader->end - reader->start;

		if (!take)
		{
			/* (big bodies straight from the connection) */
			if (length >= sizeof(reader->buffer))
			{
				int count = sc_receive(reader->connection, out, length, 30000);

				if (count <= 0)
					return 0;
				out += count;
				length -= (size_t)count;
				continue;
			}
			if (reader_fill(reader) < 0)
				return 0;
			continue;
		}
		if (take > length)
			take = length;
		memcpy(out, reader->buffer + reader->start, take);
		reader->start += take;
		out += take;
		length -= take;
	}
	return 1;
}

static int starts_with(const char *text, const char *prefix)
{
	for (; *prefix; text++, prefix++)
	{
		char a = *text, b = *prefix;

		if (a >= 'A' && a <= 'Z')
			a = (char)(a + 32);
		if (b >= 'A' && b <= 'Z')
			b = (char)(b + 32);
		if (a != b)
			return 0;
	}
	return 1;
}

void sc_http_close(struct sc_http *http)
{
	sc_close(http->connection);
	http->connection = NULL;
	http->host[0] = 0;
}

static int http_once(struct sc_http *http, const char *method, const char *path, const char *content_type,
	const void *request_body, size_t request_length, unsigned char **body, size_t *length, size_t maximum,
	char *error, int error_size, int *keep)
{
	char head[1024], line[1024];
	struct reader *reader;
	long long content_length = -1;
	int status = 0, chunked = 0, written;
	unsigned char *data = NULL;
	size_t size = 0;

	*keep = 0;
	written = snprintf(head, sizeof(head), "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Chupathingy\r\n"
		"Accept-Encoding: identity\r\nConnection: keep-alive\r\n", method, path, http->host);
	if (request_body)
	{
		written += snprintf(head + written, sizeof(head) - (size_t)written, "Content-Type: %s\r\n"
			"Content-Length: %zu\r\n", content_type ? content_type : "application/x-www-form-urlencoded",
			request_length);
	}
	if (written >= (int)sizeof(head) - 3)
	{
		snprintf(error, (size_t)error_size, "request too long");
		return 0;
	}
	strcat(head, "\r\n");
	if (!sc_send_all(http->connection, head, strlen(head)) ||
		(request_body && request_length && !sc_send_all(http->connection, request_body, request_length)))
	{
		snprintf(error, (size_t)error_size, "could not send to %s", http->host);
		return -1;
	}
	reader = (struct reader *)calloc(1, sizeof(*reader));
	if (!reader)
		return 0;
	reader->connection = http->connection;
	if (!reader_line(reader, line, sizeof(line)) || sscanf(line, "HTTP/1.%*d %d", &status) != 1)
	{
		snprintf(error, (size_t)error_size, "no answer from %s", http->host);
		free(reader);
		return -1;
	}
	*keep = 1;
	for (;;)
	{
		if (!reader_line(reader, line, sizeof(line)))
		{
			snprintf(error, (size_t)error_size, "the answer from %s ended early", http->host);
			free(reader);
			*keep = 0;
			return 0;
		}
		if (!line[0])
			break;
		if (starts_with(line, "content-length:"))
			content_length = strtoll(line + 15, NULL, 10);
		else if (starts_with(line, "transfer-encoding:") && strstr(line, "chunked"))
			chunked = 1;
		else if (starts_with(line, "connection:") && strstr(line, "close"))
			*keep = 0;
	}
	if (chunked)
	{
		for (;;)
		{
			unsigned long long piece;
			unsigned char *grown;

			if (!reader_line(reader, line, sizeof(line)))
				goto broken;
			piece = strtoull(line, NULL, 16);
			if (!piece)
			{
				/* (trailers, up to the blank line) */
				while (reader_line(reader, line, sizeof(line)) && line[0])
					;
				break;
			}
			if (piece > maximum || size + piece > maximum)
				goto too_big;
			grown = (unsigned char *)realloc(data, size + (size_t)piece + 1);
			if (!grown)
				goto too_big;
			data = grown;
			if (!reader_bytes(reader, data + size, (size_t)piece) || !reader_line(reader, line, sizeof(line)))
				goto broken;
			size += (size_t)piece;
		}
	}
	else if (content_length >= 0)
	{
		if ((unsigned long long)content_length > maximum)
			goto too_big;
		data = (unsigned char *)malloc((size_t)content_length + 1);
		if (!data)
			goto too_big;
		size = (size_t)content_length;
		if (!reader_bytes(reader, data, size))
			goto broken;
	}
	else
	{
		/* (to the connection's end) */
		*keep = 0;
		for (;;)
		{
			unsigned char *grown;
			unsigned char piece[16384];
			size_t take = reader->end - reader->start;

			if (take)
			{
				memcpy(piece, reader->buffer + reader->start, take);
				reader->start = reader->end;
			}
			else
			{
				int count = sc_receive(reader->connection, piece, sizeof(piece), 30000);

				if (count <= 0)
					break;
				take = (size_t)count;
			}
			if (size + take > maximum)
				goto too_big;
			grown = (unsigned char *)realloc(data, size + take + 1);
			if (!grown)
				goto too_big;
			data = grown;
			memcpy(data + size, piece, take);
			size += take;
		}
	}
	if (reader->start != reader->end)
		*keep = 0;
	free(reader);
	if (!data)
		data = (unsigned char *)malloc(1);
	if (!data)
		return 0;
	data[size] = 0;
	*body = data;
	*length = size;
	return status;
too_big:
	snprintf(error, (size_t)error_size, "the answer from %s is too large", http->host);
	goto failed;
broken:
	snprintf(error, (size_t)error_size, "the answer from %s ended early", http->host);
failed:
	free(reader);
	free(data);
	*keep = 0;
	return 0;
}

int sc_http_request(struct sc_http *http, const char *host, int tls, const char *method, const char *path,
	const char *content_type, const void *request_body, size_t request_length, unsigned char **body, size_t *length,
	size_t maximum, char *error, int error_size)
{
	int attempt;

	*body = NULL;
	*length = 0;
	error[0] = 0;
	if (http->connection && (strcmp(http->host, host) || http->tls != tls))
		sc_http_close(http);
	/* (a kept-alive connection the server has closed since: once more on a new one) */
	for (attempt = 0; attempt < 2; attempt++)
	{
		int fresh = !http->connection, status, keep;

		if (fresh)
		{
			http->connection = sc_connect(host, tls ? 443 : 80, tls, 15000, http->cancel, error, error_size);
			if (!http->connection)
				return 0;
			snprintf(http->host, sizeof(http->host), "%s", host);
			http->tls = tls;
		}
		status = http_once(http, method, path, content_type, request_body, request_length, body, length, maximum,
			error, error_size, &keep);
		if (!keep)
			sc_http_close(http);
		if (status != -1 || fresh)
			return status > 0 ? status : 0;
	}
	return 0;
}

#ifdef SC_FUZZING
/* the fuzz target's (steam_net_fuzz.c, built with SC_FUZZING only): a plain
connection over an open socket, and one HTTP exchange on it */
struct sc_connection *sc_fuzz_connection(int fd)
{
	struct sc_connection *connection = (struct sc_connection *)calloc(1, sizeof(*connection));

	if (connection)
	{
		connection->fd = (socket_type)fd;
		snprintf(connection->host, sizeof(connection->host), "fuzz");
	}
	return connection;
}

int sc_fuzz_http(struct sc_connection *connection, unsigned char **body, size_t *length, size_t maximum)
{
	struct sc_http http;
	char error[160];
	int keep;

	memset(&http, 0, sizeof(http));
	http.connection = connection;
	snprintf(http.host, sizeof(http.host), "fuzz");
	*body = NULL;
	*length = 0;
	return http_once(&http, "GET", "/", NULL, NULL, 0, body, length, maximum, error, sizeof(error), &keep);
}
#endif

/* ---------- WebSocket */

static const char base64_digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64(const unsigned char *data, size_t length, char *out)
{
	size_t index;

	for (index = 0; index < length; index += 3)
	{
		unsigned value = (unsigned)data[index] << 16;

		if (index + 1 < length)
			value |= (unsigned)data[index + 1] << 8;
		if (index + 2 < length)
			value |= data[index + 2];
		*out++ = base64_digits[(value >> 18) & 63];
		*out++ = base64_digits[(value >> 12) & 63];
		*out++ = index + 1 < length ? base64_digits[(value >> 6) & 63] : '=';
		*out++ = index + 2 < length ? base64_digits[value & 63] : '=';
	}
	*out = 0;
}

int sc_websocket_open(struct sc_connection *connection, const char *host, const char *path, char *error, int error_size)
{
	unsigned char nonce[16];
	char key[32], request[640], line[512];
	struct reader *reader;
	int status = 0;

	if (!sc_random(nonce, sizeof(nonce)))
		return 0;
	base64(nonce, sizeof(nonce), key);
	snprintf(request, sizeof(request), "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nUser-Agent: Chupathingy\r\n\r\n", path, host, key);
	if (!sc_send_all(connection, request, strlen(request)))
	{
		snprintf(error, (size_t)error_size, "could not send the upgrade to %s", host);
		return 0;
	}
	reader = (struct reader *)calloc(1, sizeof(*reader));
	if (!reader)
		return 0;
	reader->connection = connection;
	/* (byte by byte: nothing after the headers may be read into the buffer here) */
	{
		size_t used = 0;
		int ok = 0;

		while (used + 1 < sizeof(reader->buffer))
		{
			if (sc_receive_exact(connection, reader->buffer + used, 1, 15000) < 0)
				break;
			used++;
			if (used >= 4 && !memcmp(reader->buffer + used - 4, "\r\n\r\n", 4))
			{
				ok = 1;
				break;
			}
		}
		reader->buffer[used] = 0;
		if (ok)
		{
			size_t first = strcspn((char *)reader->buffer, "\r");

			if (first >= sizeof(line))
				first = sizeof(line) - 1;
			memcpy(line, reader->buffer, first);
			line[first] = 0;
			sscanf(line, "HTTP/1.%*d %d", &status);
		}
	}
	free(reader);
	if (status != 101)
	{
		snprintf(error, (size_t)error_size, "%s refused the WebSocket (HTTP %d)", host, status);
		return 0;
	}
	return 1;
}

static int send_frame(struct sc_connection *connection, int opcode, const void *data, size_t length)
{
	unsigned char head[14], mask[4];
	unsigned char *frame;
	size_t head_length = 2, index;
	int ok;

	if (!sc_random(mask, sizeof(mask)))
		return 0;
	head[0] = (unsigned char)(0x80 | opcode);
	if (length < 126)
		head[1] = (unsigned char)(0x80 | length);
	else if (length < 65536)
	{
		head[1] = 0x80 | 126;
		head[2] = (unsigned char)(length >> 8);
		head[3] = (unsigned char)length;
		head_length = 4;
	}
	else
	{
		head[1] = 0x80 | 127;
		for (index = 0; index < 8; index++)
			head[2 + index] = (unsigned char)((unsigned long long)length >> (56 - 8 * index));
		head_length = 10;
	}
	memcpy(head + head_length, mask, 4);
	head_length += 4;
	frame = (unsigned char *)malloc(head_length + length);
	if (!frame)
		return 0;
	memcpy(frame, head, head_length);
	for (index = 0; index < length; index++)
		frame[head_length + index] = ((const unsigned char *)data)[index] ^ mask[index & 3];
	ok = sc_send_all(connection, frame, head_length + length);
	free(frame);
	return ok;
}

int sc_websocket_send(struct sc_connection *connection, const void *data, size_t length)
{
	return send_frame(connection, 2, data, length);
}

enum { WEBSOCKET_MAXIMUM_MESSAGE = 32 * 1024 * 1024 };

int sc_websocket_receive(struct sc_connection *connection, unsigned char **message, size_t *length, int timeout_ms)
{
	unsigned char *whole = NULL;
	size_t size = 0;
	int first_wait = 1;

	*message = NULL;
	*length = 0;
	for (;;)
	{
		unsigned char head[2], extended[8];
		unsigned long long payload = 0;
		int opcode, final, index, got;
		unsigned char *data;

		/* (the first byte within timeout; the rest of a frame is on its way) */
		got = sc_receive(connection, head, 1, first_wait ? timeout_ms : 30000);
		if (got == 0 && first_wait && !whole)
			return 0;
		if (got <= 0 || sc_receive_exact(connection, head + 1, 1, 30000) < 0)
		{
			if (got != 0)
				sc_log("Steam: the connection to %s ended", connection->host);
			goto failed;
		}
		first_wait = 0;
		final = head[0] & 0x80;
		opcode = head[0] & 0x0F;
		payload = head[1] & 0x7F;
		if (payload == 126)
		{
			if (sc_receive_exact(connection, extended, 2, 30000) < 0)
				goto failed;
			payload = ((unsigned)extended[0] << 8) | extended[1];
		}
		else if (payload == 127)
		{
			if (sc_receive_exact(connection, extended, 8, 30000) < 0)
				goto failed;
			payload = 0;
			for (index = 0; index < 8; index++)
				payload = (payload << 8) | extended[index];
		}
		/* (a server never masks; one that does is refused) */
		if (head[1] & 0x80 || payload > WEBSOCKET_MAXIMUM_MESSAGE || size + payload > WEBSOCKET_MAXIMUM_MESSAGE)
			goto failed;
		data = (unsigned char *)malloc((size_t)payload + 1);
		if (!data || (payload && sc_receive_exact(connection, data, (size_t)payload, 30000) < 0))
		{
			free(data);
			goto failed;
		}
		if (opcode == 8)
		{
			sc_log("Steam: %s closed the WebSocket (%d)", connection->host, payload >= 2 ? data[0] << 8 | data[1] : 0);
			free(data);
			goto failed;
		}
		if (opcode == 9)
		{
			send_frame(connection, 10, data, (size_t)payload);
			free(data);
			continue;
		}
		if (opcode == 10 || opcode == 1)
		{
			free(data);
			continue;
		}
		if (opcode == 2 || opcode == 0)
		{
			unsigned char *grown = (unsigned char *)realloc(whole, size + (size_t)payload + 1);

			if (!grown)
			{
				free(data);
				goto failed;
			}
			whole = grown;
			memcpy(whole + size, data, (size_t)payload);
			size += (size_t)payload;
		}
		free(data);
		if (final && whole)
		{
			*message = whole;
			*length = size;
			return 1;
		}
	}
failed:
	free(whole);
	return -1;
}
#endif /* HALO_MCC */
