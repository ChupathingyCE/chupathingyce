/*
CONSOLE_HTTPS.C

HTTPS for the consoles (console_https.h): Mbed TLS over the port's sockets,
the server's certificate checked by console_https_trust.c's pins and roots
and never by the clock, an HTTP/1.0 request and its whole response.

How a request goes:
1. The port's lock; an arena on this thread's stack becomes Mbed TLS's heap
   (mbedtls_memory_buffer_alloc_init), and PSA's crypto starts, seeded from
   the console's hardware random source (mbedtls_hardware_poll below).
2. A TCP connection through the port; the TLS handshake (1.3, else 1.2) with
   the host's name sent (SNI) and checked against the certificate.
3. The certificate is judged before a byte of the request is sent: the
   name must match; then a pinned key, or a chain to a pinned root. Dates
   are never looked at (MBEDTLS_HAVE_TIME_DATE is off), so a console with
   its clock unset or wrong is not refused.
4. The request, the response read to the server's close or its
   Content-Length, never past the caller's buffer.
5. Everything freed: PSA's state too, so the arena is empty when the stack
   frame goes, and the next request starts from nothing.

Mbed TLS is Copyright The Mbed TLS Contributors, Apache-2.0 OR
GPL-2.0-or-later (port/third_party/mbedtls/LICENSE).
*/

#include "console_https.h"
#include "console_https_trust.h"
#include "console_pins.h"

#include "mbedtls/build_info.h"
#include "mbedtls/entropy.h"
#include "mbedtls/memory_buffer_alloc.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/platform.h"
#include "mbedtls/psa_util.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#include <stdio.h>
#include <string.h>

#define REQUEST_SIZE 2048
/* the pin document asked for at most this often: after the site's key was
not one it pins, and otherwise */
#define PINS_RETRY (10ul * 60 * 1000)
#define PINS_DAY (24ul * 60 * 60 * 1000)
#define SITE_TIMEOUT 15000
/* the flags a certificate's dates set (never, as built: kept apart all the
same, in case a build turns the clock on) */
#define DATE_FLAGS (MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE)

#if defined(_MSC_VER) && !defined(__clang__)
#define snprintf _snprintf
#endif

static const struct console_https_port *https_port;
/* the site's pins from the newest pin document (console_pins.h), beside the
built-in ones; and when one was last asked for (0: not yet) */
static struct console_pins saved_pins;
static unsigned long pins_asked_at;
static unsigned long pins_started_at;
static int pins_asked;
/* (the last judge: the site's key was not one it pins) */
static int pin_missed;
static int pins_refusal_said;
static int pins_due(int missed);
static int pins_refresh(unsigned long timeout_ms);
static const struct console_https_trust *trust_table = console_https_hosts;
static int trust_count;
static int trust_count_set;
#ifdef CONSOLE_HTTPS_TESTS
static unsigned long arena_peak;
#endif

static const char *const error_strings[NUMBER_OF_CONSOLE_HTTPS_ERRORS] =
{
	"ok",
	"no HTTPS on this console yet",
	"not a host this console asks over HTTPS",
	"the request is too long",
	"could not connect",
	"no random numbers from the console",
	"out of memory",
	"the TLS handshake failed",
	"the certificate is not for this host",
	"the server's key is not one this console trusts",
	"could not send",
	"could not receive",
	"timed out",
	"the answer is too large",
	"the answer is not HTTP",
};

const char *console_https_error_string(int error)
{
	if (error < 0 || error >= NUMBER_OF_CONSOLE_HTTPS_ERRORS)
		return "unknown error";
	return error_strings[error];
}

static void log_line(const char *text);
static void pins_load(void);

void console_https_initialize(const struct console_https_port *port)
{
	https_port = port;
	pins_started_at = port ? port->milliseconds() : 0;
	pins_load();
}

int console_https_available(void)
{
	return https_port != NULL;
}

static void log_line(const char *text)
{
	if (https_port && https_port->log)
		https_port->log(text);
}

/* ---------- what Mbed TLS asks of the console */

/* (MBEDTLS_ENTROPY_HARDWARE_ALT: the only entropy source there is) */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t length, size_t *produced)
{
	(void)data;
	*produced = 0;
	if (!https_port || !https_port->random || length > 0x7fffffff || !https_port->random(output, (int)length))
		return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
	*produced = length;
	return 0;
}

/* ---------- the trust */

static int trust_entries(void)
{
	return trust_count_set ? trust_count : console_https_host_count;
}

static const struct console_https_trust *find_trust(const char *host)
{
	int count = trust_entries();
	int index;

	for (index = 0; index < count; index++)
	{
		if (!strcmp(trust_table[index].host, host))
			return &trust_table[index];
	}
	return NULL;
}

#ifdef CONSOLE_HTTPS_TESTS
void console_https_trust_for_tests(const struct console_https_trust *trust, int count)
{
	trust_table = trust;
	trust_count = count;
	trust_count_set = 1;
}

unsigned long console_https_arena_peak(void)
{
	return arena_peak;
}
#endif

/* whether the certificate's key is one of trust's pins */
static int key_pinned(const struct console_https_trust *trust, const mbedtls_x509_crt *certificate)
{
	unsigned char hash[CONSOLE_HTTPS_PIN_SIZE];
	int index;

	if (mbedtls_sha256(certificate->pk_raw.p, certificate->pk_raw.len, hash, 0) != 0)
		return 0;
	for (index = 0; index < trust->pin_count; index++)
	{
		if (!memcmp(hash, trust->pins[index], CONSOLE_HTTPS_PIN_SIZE))
			return 1;
	}
	/* (and the pin document's, for the site) */
	if (!strcmp(trust->host, CONSOLE_HTTPS_SITE))
	{
		for (index = 0; index < saved_pins.count; index++)
		{
			if (!memcmp(hash, saved_pins.pins[index], CONSOLE_HTTPS_PIN_SIZE))
				return 1;
		}
	}
	return 0;
}

/* the certificate's verdict, from the handshake's own check (name and,
for a host checked by roots, the chain) and the pins */
static int judge(const struct console_https_trust *trust, mbedtls_ssl_context *ssl)
{
	const mbedtls_x509_crt *certificate = mbedtls_ssl_get_peer_cert(ssl);
	uint32_t flags = mbedtls_ssl_get_verify_result(ssl);

	if (!certificate || flags == (uint32_t)-1)
		return CONSOLE_HTTPS_ERROR_KEY;
	flags &= ~(uint32_t)DATE_FLAGS;
	if (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH)
		return CONSOLE_HTTPS_ERROR_NAME;
	if (!trust->pin_count)
		return flags ? CONSOLE_HTTPS_ERROR_KEY : CONSOLE_HTTPS_OK;
	/* a pinned host: its pins first; then, for a host with a backstop, a
	chain to its roots (Let's Encrypt's, for the site), so that a console
	that missed a pin update is never stranded; either way, a key it does
	not pin is a reason to ask for the pin document */
	if (key_pinned(trust, certificate))
		return CONSOLE_HTTPS_OK;
	pin_missed = 1;
	if (trust->roots && !flags)
	{
		char line[128];

		snprintf(line, sizeof(line), "https: %s: its key is not pinned; taken by its chain to a root", trust->host);
		log_line(line);
		return CONSOLE_HTTPS_OK;
	}
	return CONSOLE_HTTPS_ERROR_KEY;
}

/* ---------- the socket, as Mbed TLS's BIO */

struct session
{
	console_https_socket socket;
	unsigned long deadline;
};

static unsigned long time_left(const struct session *session)
{
	unsigned long now = https_port->milliseconds();

	if ((long)(session->deadline - now) <= 0)
		return 0;
	return session->deadline - now;
}

static int bio_send(void *context, const unsigned char *data, size_t size)
{
	struct session *session = (struct session *)context;
	unsigned long left = time_left(session);
	int count;

	if (!left)
		return MBEDTLS_ERR_SSL_TIMEOUT;
	count = https_port->send(session->socket, data, size > 0x7fffffff ? 0x7fffffff : (int)size, left);
	if (count == CONSOLE_HTTPS_IO_TIMEOUT)
		return MBEDTLS_ERR_SSL_TIMEOUT;
	if (count <= 0)
		return MBEDTLS_ERR_NET_SEND_FAILED;
	return count;
}

static int bio_receive(void *context, unsigned char *buffer, size_t size, uint32_t timeout)
{
	struct session *session = (struct session *)context;
	unsigned long left = time_left(session);
	int count;

	(void)timeout;
	if (!left)
		return MBEDTLS_ERR_SSL_TIMEOUT;
	count = https_port->receive(session->socket, buffer, size > 0x7fffffff ? 0x7fffffff : (int)size, left);
	if (count == CONSOLE_HTTPS_IO_TIMEOUT)
		return MBEDTLS_ERR_SSL_TIMEOUT;
	if (count < 0)
		return MBEDTLS_ERR_NET_RECV_FAILED;
	return count;
}

/* ---------- the response */

static unsigned long header_end(const char *data, unsigned long size)
{
	unsigned long index;

	for (index = 1; index < size; index++)
	{
		if (data[index] == '\n' && (data[index - 1] == '\n' ||
			(index >= 2 && data[index - 1] == '\r' && data[index - 2] == '\n')))
			return index + 1;
	}
	return 0;
}

static int lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

/* Content-Length's value in the headers (data, size bytes), or -1 */
static long content_length(const char *data, unsigned long size)
{
	static const char name[] = "content-length:";
	unsigned long index = 0;

	while (index < size)
	{
		unsigned long start = index, length;

		while (index < size && data[index] != '\n')
			index++;
		length = index - start;
		index++;
		if (length > sizeof(name) - 1)
		{
			unsigned long at;
			long value = 0;
			int digits = 0;

			for (at = 0; at < sizeof(name) - 1 && lower(data[start + at]) == name[at]; at++)
				;
			if (at < sizeof(name) - 1)
				continue;
			at = start + at;
			while (at < start + length && (data[at] == ' ' || data[at] == '\t'))
				at++;
			while (at < start + length && data[at] >= '0' && data[at] <= '9' && digits < 9)
			{
				value = value * 10 + (data[at++] - '0');
				digits++;
			}
			return digits ? value : -1;
		}
	}
	return -1;
}

int console_https_parse_response(const char *data, unsigned long size, int *status, const char **body,
	unsigned long *body_size)
{
	unsigned long end = header_end(data, size);
	long length;
	int digit;

	*status = 0;
	*body = NULL;
	*body_size = 0;
	if (!end || size < 12 || memcmp(data, "HTTP/1.", 7) || data[8] != ' ')
		return 0;
	for (digit = 9; digit < 12; digit++)
	{
		if (data[digit] < '0' || data[digit] > '9')
			return 0;
		*status = *status * 10 + (data[digit] - '0');
	}
	length = content_length(data, end);
	*body = data + end;
	*body_size = size - end;
	if (length >= 0)
	{
		if ((unsigned long)length > *body_size)
			return 0;
		*body_size = (unsigned long)length;
	}
	return 1;
}

/* ---------- a request */

static int tls_write(mbedtls_ssl_context *ssl, const unsigned char *data, unsigned long size)
{
	while (size)
	{
		int count = mbedtls_ssl_write(ssl, data, size);

		if (count == MBEDTLS_ERR_SSL_WANT_READ || count == MBEDTLS_ERR_SSL_WANT_WRITE)
			continue;
		if (count == MBEDTLS_ERR_SSL_TIMEOUT)
			return CONSOLE_HTTPS_ERROR_TIMEOUT;
		if (count <= 0)
			return CONSOLE_HTTPS_ERROR_SEND;
		data += count;
		size -= (unsigned long)count;
	}
	return CONSOLE_HTTPS_OK;
}

static int tls_read_all(mbedtls_ssl_context *ssl, char *response, unsigned long size, unsigned long *received)
{
	unsigned long total = 0;
	int headers = 0;

	for (;;)
	{
		int count;

		if (*received >= size)
			return CONSOLE_HTTPS_ERROR_TOO_LARGE;
		count = mbedtls_ssl_read(ssl, (unsigned char *)response + *received, size - *received);
		if (count == MBEDTLS_ERR_SSL_WANT_READ || count == MBEDTLS_ERR_SSL_WANT_WRITE)
			continue;
		/* (TLS 1.3's tickets: none are kept) */
		if (count == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
			continue;
		/* (the end: a server that closes without telling TLS is taken as
		ended too, its Content-Length then the check) */
		if (count == 0 || count == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || count == MBEDTLS_ERR_SSL_CONN_EOF)
			return CONSOLE_HTTPS_OK;
		if (count == MBEDTLS_ERR_SSL_TIMEOUT)
			return CONSOLE_HTTPS_ERROR_TIMEOUT;
		if (count < 0)
			return CONSOLE_HTTPS_ERROR_RECEIVE;
		*received += (unsigned long)count;
		if (!headers)
		{
			unsigned long end = header_end(response, *received);

			if (end)
			{
				long length = content_length(response, end);

				headers = 1;
				total = length >= 0 ? end + (unsigned long)length : 0;
			}
		}
		if (headers && total && *received >= total)
			return CONSOLE_HTTPS_OK;
	}
}

static int request_locked(const struct console_https_trust *trust, const char *request, int request_length,
	char *response, unsigned long size, unsigned long *received, unsigned long timeout_ms)
{
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config config;
	mbedtls_x509_crt roots;
	struct session session;
	unsigned long started = 0, handshake = 0;
	int result = CONSOLE_HTTPS_OK;
	int code;

	mbedtls_ssl_init(&ssl);
	mbedtls_ssl_config_init(&config);
	mbedtls_x509_crt_init(&roots);
	if (psa_crypto_init() != PSA_SUCCESS)
	{
		mbedtls_ssl_free(&ssl);
		mbedtls_ssl_config_free(&config);
		mbedtls_psa_crypto_free();
		return CONSOLE_HTTPS_ERROR_RANDOM;
	}
	if (!trust->pin_count || trust->roots)
	{
		const struct console_https_root *list = trust->roots ? trust->roots : console_https_roots;
		int count = trust->roots ? trust->root_count : console_https_root_count;
		int index;

		for (index = 0; index < count; index++)
		{
			/* (a root that does not parse is left out, and logged) */
			if (mbedtls_x509_crt_parse_der_nocopy(&roots, list[index].der, list[index].size) != 0)
			{
				char line[128];

				snprintf(line, sizeof(line), "https: the root %s did not parse", list[index].name);
				log_line(line);
			}
		}
	}
	if ((code = mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
		MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
	{
		result = code == MBEDTLS_ERR_SSL_ALLOC_FAILED ? CONSOLE_HTTPS_ERROR_MEMORY : CONSOLE_HTTPS_ERROR_HANDSHAKE;
		goto done;
	}
	/* (optional: the verdict is judge's, made after the handshake and
	before anything is sent; the handshake itself still proves the server
	holds the certificate's key) */
	mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_OPTIONAL);
	if (!trust->pin_count || trust->roots)
		mbedtls_ssl_conf_ca_chain(&config, &roots, NULL);
	mbedtls_ssl_conf_rng(&config, mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE);
	if ((code = mbedtls_ssl_setup(&ssl, &config)) != 0 || (code = mbedtls_ssl_set_hostname(&ssl, trust->host)) != 0)
	{
		result = code == MBEDTLS_ERR_SSL_ALLOC_FAILED ? CONSOLE_HTTPS_ERROR_MEMORY : CONSOLE_HTTPS_ERROR_HANDSHAKE;
		goto done;
	}
	started = https_port->milliseconds();
	session.deadline = started + timeout_ms;
	if (!https_port->connect(trust->host, CONSOLE_HTTPS_PORT_NUMBER, timeout_ms, &session.socket))
	{
		result = CONSOLE_HTTPS_ERROR_CONNECT;
		goto done;
	}
	mbedtls_ssl_set_bio(&ssl, &session, bio_send, NULL, bio_receive);
	while ((code = mbedtls_ssl_handshake(&ssl)) != 0)
	{
		if (code == MBEDTLS_ERR_SSL_WANT_READ || code == MBEDTLS_ERR_SSL_WANT_WRITE)
			continue;
		result = code == MBEDTLS_ERR_SSL_TIMEOUT ? CONSOLE_HTTPS_ERROR_TIMEOUT :
			code == MBEDTLS_ERR_SSL_ALLOC_FAILED ? CONSOLE_HTTPS_ERROR_MEMORY :
			code == MBEDTLS_ERR_ENTROPY_SOURCE_FAILED ?
				CONSOLE_HTTPS_ERROR_RANDOM : CONSOLE_HTTPS_ERROR_HANDSHAKE;
		{
			char line[96];

			snprintf(line, sizeof(line), "https: %s: the handshake failed (-0x%04x)", trust->host,
				(unsigned)-code);
			log_line(line);
		}
		break;
	}
	if (result == CONSOLE_HTTPS_OK)
	{
		handshake = https_port->milliseconds() - started;
		result = judge(trust, &ssl);
		if (result != CONSOLE_HTTPS_OK)
		{
			char line[128];

			snprintf(line, sizeof(line), "https: %s: %s (certificate flags 0x%lx)", trust->host,
				console_https_error_string(result), (unsigned long)mbedtls_ssl_get_verify_result(&ssl));
			log_line(line);
		}
	}
	if (result == CONSOLE_HTTPS_OK)
		result = tls_write(&ssl, (const unsigned char *)request, (unsigned long)request_length);
	if (result == CONSOLE_HTTPS_OK)
		result = tls_read_all(&ssl, response, size, received);
	if (result == CONSOLE_HTTPS_OK)
	{
		char line[128];

		/* (how long a console takes, for its hardware tests) */
		snprintf(line, sizeof(line), "https: %s: %s, handshake %lu ms, %lu ms in all", trust->host,
			mbedtls_ssl_get_version(&ssl), handshake, https_port->milliseconds() - started);
		log_line(line);
	}
	if (code == 0)
		mbedtls_ssl_close_notify(&ssl);
	https_port->close(session.socket);

done:
	mbedtls_ssl_free(&ssl);
	mbedtls_ssl_config_free(&config);
	mbedtls_x509_crt_free(&roots);
	mbedtls_psa_crypto_free();
	return result;
}

/* ---------- the pin document (console_pins.h) */

static int build_get(char *request, int size, const char *host, const char *path)
{
	int length = snprintf(request, (size_t)size,
		"GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
		path, host, https_port->user_agent ? https_port->user_agent : "Warthog/1");

	return length > 0 && length < size ? length : 0;
}

/* whether the pin document is to be asked for: soon after the site's key
was not one it pins, else once a day */
static int pins_due(int missed)
{
	unsigned long now = https_port->milliseconds();

	if (!console_pins_have_key())
		return 0;
	if (!pins_asked)
		return missed || (long)(now - pins_started_at) >= (long)PINS_DAY;
	return (long)(now - pins_asked_at) >= (long)(missed ? PINS_RETRY : PINS_DAY);
}

/* one file of the document (path) from source: the whole answer into
buffer, its body at *body; 1 if a 200 came. plain: over the port's plain
HTTP from the console list's host, else over HTTPS from GitHub's raw files
(the lock held) */
static int pins_get(int plain, const char *path, char *buffer, unsigned long size, const char **body,
	unsigned long *body_size, unsigned long timeout_ms)
{
	unsigned long received = 0;
	int status;

	if (plain)
	{
		if (!https_port->plain_get ||
			!https_port->plain_get(CONSOLE_HTTPS_PINS_PLAIN_HOST, path, buffer, size, &received, timeout_ms))
			return 0;
	}
	else
	{
		const struct console_https_trust *github = find_trust(CONSOLE_HTTPS_GITHUB);
		char request[512];
		int length;

		if (!github || !(length = build_get(request, sizeof(request), CONSOLE_HTTPS_GITHUB, path)) ||
			request_locked(github, request, length, buffer, size, &received, timeout_ms) != CONSOLE_HTTPS_OK)
			return 0;
	}
	return console_https_parse_response(buffer, received, &status, body, body_size) && status == 200;
}

static void pins_save(const char *document, unsigned long size, const char *signature)
{
	char temporary[300];
	FILE *file;
	int ok;

	if (!https_port->pins_path || strlen(https_port->pins_path) + 5 > sizeof(temporary))
		return;
	snprintf(temporary, sizeof(temporary), "%s.new", https_port->pins_path);
	file = fopen(temporary, "wb");
	if (!file)
		return;
	ok = fwrite(signature, 1, CONSOLE_PINS_SIGNATURE_SIZE * 2, file) == CONSOLE_PINS_SIGNATURE_SIZE * 2 &&
		fputc('\n', file) != EOF && fwrite(document, 1, size, file) == size;
	ok = fclose(file) == 0 && ok;
	/* (a file system that renames over no file: the old one removed first) */
	if (ok && rename(temporary, https_port->pins_path) != 0)
	{
		remove(https_port->pins_path);
		ok = rename(temporary, https_port->pins_path) == 0;
	}
	if (!ok)
	{
		remove(temporary);
		log_line("https: the pin document could not be kept");
	}
}

static void pins_load(void)
{
	char signed_form[CONSOLE_PINS_SIGNED_SIZE + 1];
	struct console_pins pins;
	const char *why;
	unsigned long size;
	FILE *file;

	memset(&saved_pins, 0, sizeof(saved_pins));
	if (!https_port || !https_port->pins_path || !console_pins_have_key() || !(file = fopen(https_port->pins_path, "rb")))
		return;
	size = (unsigned long)fread(signed_form, 1, sizeof(signed_form), file);
	fclose(file);
	if (console_pins_check_signed(CONSOLE_HTTPS_SITE, signed_form, size, &pins, &why))
		saved_pins = pins;
	else
	{
		char line[128];

		snprintf(line, sizeof(line), "https: the kept pin document is not used: %s", why);
		log_line(line);
	}
}

/* the pin document asked for (plain HTTP first, then GitHub), checked, and
taken if its serial is past the one in use: 1 if new pins were taken */
static int pins_refresh(unsigned long timeout_ms)
{
	char document[CONSOLE_PINS_DOCUMENT_SIZE + 1024], signature[1024];
	struct console_pins pins;
	const char *document_body, *signature_body, *why = "it could not be had";
	unsigned long document_size, signature_size;
	int plain;

	pins_asked = 1;
	pins_asked_at = https_port->milliseconds();
	for (plain = 1; plain >= 0; plain--)
	{
		char line[160];

		if (!pins_get(plain, plain ? CONSOLE_HTTPS_PINS_PLAIN_PATH : CONSOLE_HTTPS_PINS_GITHUB_PATH, document,
				sizeof(document), &document_body, &document_size, timeout_ms) ||
			!pins_get(plain, plain ? CONSOLE_HTTPS_PINS_PLAIN_PATH ".sig" : CONSOLE_HTTPS_PINS_GITHUB_PATH ".sig",
				signature, sizeof(signature), &signature_body, &signature_size, timeout_ms))
		{
			why = "it could not be had";
			continue;
		}
		if (!console_pins_check(CONSOLE_HTTPS_SITE, document_body, document_size, signature_body, signature_size,
			&pins, &why))
			break;
		if (pins.serial <= saved_pins.serial)
		{
			why = "it is not newer than the one in use";
			break;
		}
		/* (the signature's hex digits sit before the document's in the kept
		form; the body is moved out of the answer's buffer first) */
		{
			char kept_signature[CONSOLE_PINS_SIGNATURE_SIZE * 2];

			memcpy(kept_signature, signature_body, sizeof(kept_signature));
			pins_save(document_body, document_size, kept_signature);
		}
		saved_pins = pins;
		snprintf(line, sizeof(line), "https: pin document %lu from %s taken: %d pins for %s", pins.serial,
			plain ? CONSOLE_HTTPS_PINS_PLAIN_HOST : CONSOLE_HTTPS_GITHUB, pins.count, CONSOLE_HTTPS_SITE);
		log_line(line);
		pins_refusal_said = 0;
		return 1;
	}
	/* (said once a run: a bad document changes nothing) */
	if (!pins_refusal_said)
	{
		char line[128];

		snprintf(line, sizeof(line), "https: no new pin document: %s", why);
		log_line(line);
		pins_refusal_said = 1;
	}
	return 0;
}

int console_https_request(const char *host, const char *path, const char *body, const char *content_type,
	char *response, unsigned long size, unsigned long *received, unsigned long timeout_ms)
{
	/* (Mbed TLS's heap: this thread's stack, aligned for any of its
	structures) */
	union
	{
		unsigned char bytes[CONSOLE_HTTPS_ARENA_SIZE];
		long long align_integer;
		double align_double;
		void *align_pointer;
	} arena;
	char request[REQUEST_SIZE];
	const struct console_https_trust *trust;
	int length;
	int result;

	*received = 0;
	if (!https_port)
		return CONSOLE_HTTPS_ERROR_UNAVAILABLE;
	trust = find_trust(host);
	if (!trust)
		return CONSOLE_HTTPS_ERROR_HOST;
	if (body)
		length = snprintf(request, sizeof(request),
			"POST %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\nAccept-Encoding: identity\r\n"
			"Content-Type: %s\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n%s", path, host,
			https_port->user_agent ? https_port->user_agent : "Warthog/1",
			content_type ? content_type : "application/json", (unsigned long)strlen(body), body);
	else
		length = snprintf(request, sizeof(request),
			"GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\nAccept-Encoding: identity\r\n"
			"Connection: close\r\n\r\n", path, host, https_port->user_agent ? https_port->user_agent : "Warthog/1");
	if (length <= 0 || length >= (int)sizeof(request))
		return CONSOLE_HTTPS_ERROR_REQUEST;

	if (https_port->lock)
		https_port->lock();
	mbedtls_memory_buffer_alloc_init(arena.bytes, sizeof(arena.bytes));
	pin_missed = 0;
	result = request_locked(trust, request, length, response, size, received, timeout_ms);
	/* (the site's key not pinned, or a day since the pin document was last
	asked for: asked for now, and with new pins the request tried again) */
	if (!strcmp(trust->host, CONSOLE_HTTPS_SITE) && pins_due(pin_missed) && pins_refresh(timeout_ms) &&
		result == CONSOLE_HTTPS_ERROR_KEY)
	{
		*received = 0;
		result = request_locked(trust, request, length, response, size, received, timeout_ms);
	}
#ifdef CONSOLE_HTTPS_TESTS
	{
		size_t used = 0, blocks = 0;

		mbedtls_memory_buffer_alloc_max_get(&used, &blocks);
		arena_peak = (unsigned long)used;
		mbedtls_memory_buffer_alloc_cur_get(&used, &blocks);
		/* (every byte given back: the arena goes with this frame) */
		if (used)
			log_line("https: the arena was not empty at the end");
	}
#endif
	mbedtls_memory_buffer_alloc_free();
	if (https_port->unlock)
		https_port->unlock();
	/* (nothing of the session is left on the stack) */
	mbedtls_platform_zeroize(arena.bytes, sizeof(arena.bytes));
	return result;
}

int console_https_call(const char *host, const char *path, const char *body, int *status, char *reply,
	int reply_size, unsigned long timeout_ms)
{
	unsigned long received = 0, body_size;
	const char *body_start;
	int result;

	*status = 0;
	if (reply_size < 2)
		return CONSOLE_HTTPS_ERROR_TOO_LARGE;
	reply[0] = 0;
	/* (the whole answer into reply, then its body moved to the start) */
	result = console_https_request(host, path, body, NULL, reply, (unsigned long)reply_size - 1, &received,
		timeout_ms);
	if (result != CONSOLE_HTTPS_OK)
	{
		reply[0] = 0;
		return result;
	}
	if (!console_https_parse_response(reply, received, status, &body_start, &body_size))
	{
		reply[0] = 0;
		return CONSOLE_HTTPS_ERROR_RESPONSE;
	}
	memmove(reply, body_start, body_size);
	reply[body_size] = 0;
	return CONSOLE_HTTPS_OK;
}

int console_https_site_request(const char *path, const char *body, int *status, char *reply, int reply_size)
{
	return console_https_call(CONSOLE_HTTPS_SITE, path, body, status, reply, reply_size, SITE_TIMEOUT) ==
		CONSOLE_HTTPS_OK;
}
