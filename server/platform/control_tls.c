/*
CONTROL_TLS.C

HTTPS for the control API and web admin page (control_tls.h): the
certificate (the operator's, or the server's own, made once), one Mbed TLS
configuration, and connections over the control thread's nonblocking
sockets.

The configuration is held by a reference-counted context: a renewed
operator certificate gives new connections a new context, while those
already open keep the one they began with until they are freed.
*/

#include "control_tls.h"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/oid.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* (Linux's; macOS has SO_NOSIGPIPE instead, set on each socket) */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

enum
{
	PATH_SIZE = 1024,
	/* a PEM key's or certificate's text, at most, as made here */
	PEM_SIZE = 4096,
	/* the server's own certificate: valid this many years, from a day
	before it is made (a clock a little behind still takes it) */
	OWN_YEARS = 10,
	COMMON_NAME_SIZE = 64,
};

struct tls_context
{
	int references;
	mbedtls_ssl_config config;
	mbedtls_x509_crt certificate;
	mbedtls_pk_context key;
};

struct control_tls_connection
{
	struct tls_context *context;
	mbedtls_ssl_context ssl;
	int fd;
	short events;
	/* the first byte was looked at (a TLS record's, not plain HTTP's) */
	int checked;
};

/* a file as last read, to see a change */
struct file_stamp
{
	int valid;
	long long modified;
	long long size;
	long long inode;
};

static struct
{
	int started;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context drbg;
	struct tls_context *context;
	char fingerprint[CONTROL_TLS_FINGERPRINT_SIZE];
	int self_signed;
	/* the operator's files, and how they were */
	char cert_path[PATH_SIZE];
	char key_path[PATH_SIZE];
	struct file_stamp cert_stamp;
	struct file_stamp key_stamp;
	int64_t checked;
	int checked_once;
} tls;

/* the cipher suites: ECDHE (TLS 1.2) or TLS 1.3's, with AEAD ciphers */
static const int cipher_suites[] =
{
	MBEDTLS_TLS1_3_AES_256_GCM_SHA384,
	MBEDTLS_TLS1_3_CHACHA20_POLY1305_SHA256,
	MBEDTLS_TLS1_3_AES_128_GCM_SHA256,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
	MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
	MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
	MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
	0
};

static const char *alpn_protocols[] = { "http/1.1", NULL };

/* ---------- private code */

static void set_error(char *error, size_t size, const char *what, int code)
{
	char text[128];

	if (!error || !size)
		return;
	if (code)
	{
		mbedtls_strerror(code, text, sizeof(text));
		snprintf(error, size, "%s (%s)", what, text);
	}
	else
		snprintf(error, size, "%s", what);
}

static void path_in(const char *root, const char *name, char *out, size_t size)
{
	if (name[0] == '/')
		snprintf(out, size, "%s", name);
	else
		snprintf(out, size, "%s/%s", root && root[0] ? root : ".", name);
}

static void stamp_file(const char *path, struct file_stamp *stamp)
{
	struct stat information;

	memset(stamp, 0, sizeof(*stamp));
	if (stat(path, &information))
		return;
	stamp->valid = 1;
	stamp->modified = (long long)information.st_mtime;
	stamp->size = (long long)information.st_size;
	stamp->inode = (long long)information.st_ino;
}

static int stamps_differ(const struct file_stamp *a, const struct file_stamp *b)
{
	return a->valid != b->valid || a->modified != b->modified || a->size != b->size || a->inode != b->inode;
}

/* text into a file, whole (a new file beside it, then renamed over it),
readable as mode says: 1, else 0 */
static int write_file(const char *path, const char *text, mode_t mode)
{
	char temporary[PATH_SIZE + 8];
	size_t length = strlen(text);
	size_t done = 0;
	int fd;

	snprintf(temporary, sizeof(temporary), "%s.new", path);
	unlink(temporary);
	fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
	if (fd < 0)
		return 0;
	/* (whatever the umask) */
	if (fchmod(fd, mode))
	{
		close(fd);
		unlink(temporary);
		return 0;
	}
	while (done < length)
	{
		ssize_t count = write(fd, text + done, length - done);

		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
		{
			close(fd);
			unlink(temporary);
			return 0;
		}
		done += (size_t)count;
	}
	if (fsync(fd) || close(fd) || rename(temporary, path))
	{
		unlink(temporary);
		return 0;
	}
	return 1;
}

/* a certificate's SHA-256 fingerprint, "AB:CD:..." */
static void fingerprint_text(const mbedtls_x509_crt *certificate, char *out, size_t size)
{
	static const char digits[] = "0123456789ABCDEF";
	unsigned char digest[32];
	size_t index;
	size_t length = 0;

	if (size < CONTROL_TLS_FINGERPRINT_SIZE)
	{
		if (size)
			out[0] = 0;
		return;
	}
	mbedtls_sha256(certificate->raw.p, certificate->raw.len, digest, 0);
	for (index = 0; index < sizeof(digest); index++)
	{
		if (index)
			out[length++] = ':';
		out[length++] = digits[digest[index] >> 4];
		out[length++] = digits[digest[index] & 15];
	}
	out[length] = 0;
}

/* "YYYYMMDDhhmmss", UTC */
static void x509_time(time_t when, char out[16])
{
	struct tm parts;

	gmtime_r(&when, &parts);
	snprintf(out, 16, "%04d%02d%02d%02d%02d%02d", parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
		parts.tm_hour, parts.tm_min, parts.tm_sec);
}

/* a name fit for a certificate's CN: letters, digits, space and -_. only */
static void common_name_text(const char *name, char *out, size_t size)
{
	size_t length = 0;

	for (; name && *name && length + 1 < size; name++)
	{
		char character = *name;

		if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') || character == ' ' || character == '-' || character == '_' ||
			character == '.')
		{
			out[length++] = character;
		}
	}
	while (length && out[length - 1] == ' ')
		length--;
	out[length] = 0;
	if (!length || out[0] == ' ')
		snprintf(out, size, "chupathingyce-server");
}

/* the server's own key and certificate, made and written: 1, else 0 */
static int make_own(const char *cert_path, const char *key_path, const char *common_name, char *error,
	size_t error_size)
{
	mbedtls_pk_context key;
	mbedtls_x509write_cert writer;
	mbedtls_asn1_sequence server_auth;
	unsigned char serial[16];
	char name[COMMON_NAME_SIZE];
	char subject[160];
	char not_before[16], not_after[16];
	char *pem = malloc(PEM_SIZE);
	time_t now = time(NULL);
	struct tm later;
	int result;
	int ok = 0;

	mbedtls_pk_init(&key);
	mbedtls_x509write_crt_init(&writer);
	if (!pem)
	{
		set_error(error, error_size, "no memory for a certificate", 0);
		goto done;
	}
	if ((result = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))) ||
		(result = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), mbedtls_ctr_drbg_random,
			&tls.drbg)))
	{
		set_error(error, error_size, "cannot make a key", result);
		goto done;
	}
	common_name_text(common_name, name, sizeof(name));
	snprintf(subject, sizeof(subject), "CN=%s,O=ChupathingyCE Dedicated Server", name);
	if ((result = mbedtls_ctr_drbg_random(&tls.drbg, serial, sizeof(serial))))
	{
		set_error(error, error_size, "no random bytes", result);
		goto done;
	}
	/* (a positive serial, with no leading zero byte) */
	serial[0] = (unsigned char)((serial[0] & 0x7F) | 0x40);
	x509_time(now - 24 * 60 * 60, not_before);
	gmtime_r(&now, &later);
	snprintf(not_after, sizeof(not_after), "%04d%02d%02d%02d%02d%02d", later.tm_year + 1900 + OWN_YEARS,
		later.tm_mon + 1, later.tm_mday > 28 && later.tm_mon == 1 ? 28 : later.tm_mday, later.tm_hour, later.tm_min,
		later.tm_sec);
	memset(&server_auth, 0, sizeof(server_auth));
	server_auth.buf.tag = MBEDTLS_ASN1_OID;
	server_auth.buf.p = (unsigned char *)MBEDTLS_OID_SERVER_AUTH;
	server_auth.buf.len = MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH);
	mbedtls_x509write_crt_set_version(&writer, MBEDTLS_X509_CRT_VERSION_3);
	mbedtls_x509write_crt_set_md_alg(&writer, MBEDTLS_MD_SHA256);
	mbedtls_x509write_crt_set_subject_key(&writer, &key);
	mbedtls_x509write_crt_set_issuer_key(&writer, &key);
	if ((result = mbedtls_x509write_crt_set_serial_raw(&writer, serial, sizeof(serial))) ||
		(result = mbedtls_x509write_crt_set_subject_name(&writer, subject)) ||
		(result = mbedtls_x509write_crt_set_issuer_name(&writer, subject)) ||
		(result = mbedtls_x509write_crt_set_validity(&writer, not_before, not_after)) ||
		(result = mbedtls_x509write_crt_set_basic_constraints(&writer, 0, -1)) ||
		(result = mbedtls_x509write_crt_set_key_usage(&writer, MBEDTLS_X509_KU_DIGITAL_SIGNATURE)) ||
		(result = mbedtls_x509write_crt_set_ext_key_usage(&writer, &server_auth)) ||
		(result = mbedtls_x509write_crt_set_subject_key_identifier(&writer)))
	{
		set_error(error, error_size, "cannot make a certificate", result);
		goto done;
	}
	/* the key first (a certificate with no key beside it is made again) */
	if ((result = mbedtls_pk_write_key_pem(&key, (unsigned char *)pem, PEM_SIZE)))
	{
		set_error(error, error_size, "cannot write the key", result);
		goto done;
	}
	if (!write_file(key_path, pem, 0600))
	{
		set_error(error, error_size, "cannot write " CONTROL_TLS_KEY_FILE " in the data folder", 0);
		goto done;
	}
	mbedtls_platform_zeroize(pem, PEM_SIZE);
	if ((result = mbedtls_x509write_crt_pem(&writer, (unsigned char *)pem, PEM_SIZE, mbedtls_ctr_drbg_random,
		&tls.drbg)))
	{
		set_error(error, error_size, "cannot write the certificate", result);
		goto done;
	}
	if (!write_file(cert_path, pem, 0644))
	{
		set_error(error, error_size, "cannot write " CONTROL_TLS_CERTIFICATE_FILE " in the data folder", 0);
		goto done;
	}
	ok = 1;

done:
	if (pem)
	{
		mbedtls_platform_zeroize(pem, PEM_SIZE);
		free(pem);
	}
	mbedtls_x509write_crt_free(&writer);
	mbedtls_pk_free(&key);
	return ok;
}

static void context_release(struct tls_context *context)
{
	if (!context || --context->references > 0)
		return;
	mbedtls_ssl_config_free(&context->config);
	mbedtls_x509_crt_free(&context->certificate);
	mbedtls_pk_free(&context->key);
	free(context);
}

/* a context from a certificate (with any chain after it) and its key:
NULL, and why in error, if they cannot be read, do not match, or the
certificate has expired */
static struct tls_context *context_load(const char *cert_path, const char *key_path, char *error, size_t error_size)
{
	struct tls_context *context = calloc(1, sizeof(*context));
	int result;

	if (!context)
	{
		set_error(error, error_size, "no memory for TLS", 0);
		return NULL;
	}
	context->references = 1;
	mbedtls_ssl_config_init(&context->config);
	mbedtls_x509_crt_init(&context->certificate);
	mbedtls_pk_init(&context->key);
	if ((result = mbedtls_x509_crt_parse_file(&context->certificate, cert_path)))
	{
		set_error(error, error_size, "cannot read the certificate", result);
		goto failed;
	}
	if ((result = mbedtls_pk_parse_keyfile(&context->key, key_path, NULL, mbedtls_ctr_drbg_random, &tls.drbg)))
	{
		set_error(error, error_size, "cannot read the key", result);
		goto failed;
	}
	if ((result = mbedtls_pk_check_pair(&context->certificate.pk, &context->key, mbedtls_ctr_drbg_random, &tls.drbg)))
	{
		set_error(error, error_size, "the key is not the certificate's", result);
		goto failed;
	}
	if (mbedtls_x509_time_is_past(&context->certificate.valid_to))
	{
		set_error(error, error_size, "the certificate has expired", 0);
		goto failed;
	}
	if ((result = mbedtls_ssl_config_defaults(&context->config, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
		MBEDTLS_SSL_PRESET_DEFAULT)))
	{
		set_error(error, error_size, "cannot set up TLS", result);
		goto failed;
	}
	mbedtls_ssl_conf_rng(&context->config, mbedtls_ctr_drbg_random, &tls.drbg);
	mbedtls_ssl_conf_min_tls_version(&context->config, MBEDTLS_SSL_VERSION_TLS1_2);
	/* (TLS 1.2 alone: Mbed TLS's TLS 1.3 keeps its keys in PSA's global key
	slots, which this build of it does not lock, and the game list's thread
	(posix_browser.c) speaks TLS 1.3 at the same time. TLS 1.2 with ECDHE and
	AEAD suites only is as sound for this) */
	mbedtls_ssl_conf_max_tls_version(&context->config, MBEDTLS_SSL_VERSION_TLS1_2);
	mbedtls_ssl_conf_ciphersuites(&context->config, cipher_suites);
#if defined(MBEDTLS_SSL_RENEGOTIATION)
	mbedtls_ssl_conf_renegotiation(&context->config, MBEDTLS_SSL_RENEGOTIATION_DISABLED);
#endif
#if defined(MBEDTLS_SSL_PROTO_TLS1_3)
	mbedtls_ssl_conf_tls13_key_exchange_modes(&context->config, MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL);
#if defined(MBEDTLS_SSL_SESSION_TICKETS)
	mbedtls_ssl_conf_new_session_tickets(&context->config, 0);
#endif
#endif
	if ((result = mbedtls_ssl_conf_alpn_protocols(&context->config, alpn_protocols)) ||
		(result = mbedtls_ssl_conf_own_cert(&context->config, &context->certificate, &context->key)))
	{
		set_error(error, error_size, "cannot set up TLS", result);
		goto failed;
	}
	return context;

failed:
	context_release(context);
	return NULL;
}

static void use_context(struct tls_context *context)
{
	context_release(tls.context);
	tls.context = context;
	fingerprint_text(&context->certificate, tls.fingerprint, sizeof(tls.fingerprint));
}

/* ---------- public code */

int control_tls_start(const char *data_root, const char *cert_path, const char *key_path, const char *common_name,
	char *fingerprint, size_t fingerprint_size, int *made, char *error, size_t error_size)
{
	static const char personalization[] = "chupathingyce control tls";
	struct tls_context *context;
	int result;

	if (made)
		*made = 0;
	if (fingerprint && fingerprint_size)
		fingerprint[0] = 0;
	if (error && error_size)
		error[0] = 0;
	control_tls_stop();
	if ((cert_path && cert_path[0]) != (key_path && key_path[0]))
	{
		set_error(error, error_size, "give both the certificate and its key (HALO_DEDICATED_CONTROL_CERT and "
			"HALO_DEDICATED_CONTROL_KEY), or neither", 0);
		return 0;
	}
	mbedtls_entropy_init(&tls.entropy);
	mbedtls_ctr_drbg_init(&tls.drbg);
	if ((result = mbedtls_ctr_drbg_seed(&tls.drbg, mbedtls_entropy_func, &tls.entropy,
		(const unsigned char *)personalization, sizeof(personalization) - 1)))
	{
		set_error(error, error_size, "no random bytes", result);
		mbedtls_ctr_drbg_free(&tls.drbg);
		mbedtls_entropy_free(&tls.entropy);
		return 0;
	}
	tls.started = 1;
	if (cert_path && cert_path[0])
	{
		/* the operator's: used as they are, never written */
		path_in(data_root, cert_path, tls.cert_path, sizeof(tls.cert_path));
		path_in(data_root, key_path, tls.key_path, sizeof(tls.key_path));
		stamp_file(tls.cert_path, &tls.cert_stamp);
		stamp_file(tls.key_path, &tls.key_stamp);
		context = context_load(tls.cert_path, tls.key_path, error, error_size);
		if (!context)
		{
			control_tls_stop();
			return 0;
		}
		tls.self_signed = 0;
	}
	else
	{
		char own_cert[PATH_SIZE], own_key[PATH_SIZE];
		char problem[192];

		path_in(data_root, CONTROL_TLS_CERTIFICATE_FILE, own_cert, sizeof(own_cert));
		path_in(data_root, CONTROL_TLS_KEY_FILE, own_key, sizeof(own_key));
		context = context_load(own_cert, own_key, problem, sizeof(problem));
		if (!context)
		{
			if (!make_own(own_cert, own_key, common_name, error, error_size))
			{
				control_tls_stop();
				return 0;
			}
			context = context_load(own_cert, own_key, error, error_size);
			if (!context)
			{
				control_tls_stop();
				return 0;
			}
			if (made)
				*made = 1;
		}
		else
			chmod(own_key, 0600);
		tls.self_signed = 1;
	}
	use_context(context);
	if (fingerprint)
		snprintf(fingerprint, fingerprint_size, "%s", tls.fingerprint);
	return 1;
}

void control_tls_stop(void)
{
	context_release(tls.context);
	tls.context = NULL;
	if (tls.started)
	{
		mbedtls_ctr_drbg_free(&tls.drbg);
		mbedtls_entropy_free(&tls.entropy);
	}
	memset(&tls, 0, sizeof(tls));
}

const char *control_tls_fingerprint(void)
{
	return tls.fingerprint;
}

int control_tls_self_signed(void)
{
	return tls.context && tls.self_signed;
}

int control_tls_reload_if_changed(int64_t now, char *error, size_t error_size)
{
	struct file_stamp cert_stamp, key_stamp;
	struct tls_context *context;

	if (error && error_size)
		error[0] = 0;
	if (!tls.context || tls.self_signed)
		return 0;
	if (tls.checked_once && now - tls.checked < CONTROL_TLS_RELOAD_SECONDS)
		return 0;
	tls.checked_once = 1;
	tls.checked = now;
	stamp_file(tls.cert_path, &cert_stamp);
	stamp_file(tls.key_path, &key_stamp);
	if (!stamps_differ(&cert_stamp, &tls.cert_stamp) && !stamps_differ(&key_stamp, &tls.key_stamp))
		return 0;
	context = context_load(tls.cert_path, tls.key_path, error, error_size);
	if (!context)
	{
		/* (a renewal half written: looked at again next time) */
		return -1;
	}
	tls.cert_stamp = cert_stamp;
	tls.key_stamp = key_stamp;
	use_context(context);
	return 1;
}

/* ---------- connections */

static int bio_send(void *argument, const unsigned char *data, size_t size)
{
	struct control_tls_connection *connection = argument;
	ssize_t count = send(connection->fd, data, size, MSG_NOSIGNAL);

	if (count < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			return MBEDTLS_ERR_SSL_WANT_WRITE;
		return MBEDTLS_ERR_NET_SEND_FAILED;
	}
	return (int)count;
}

static int bio_receive(void *argument, unsigned char *buffer, size_t size)
{
	struct control_tls_connection *connection = argument;
	ssize_t count = recv(connection->fd, buffer, size, 0);

	if (count < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			return MBEDTLS_ERR_SSL_WANT_READ;
		return MBEDTLS_ERR_NET_RECV_FAILED;
	}
	return (int)count;
}

/* an Mbed TLS result as this unit's */
static int result_of(struct control_tls_connection *connection, int result)
{
	switch (result)
	{
	case MBEDTLS_ERR_SSL_WANT_READ:
		connection->events = POLLIN;
		return CONTROL_TLS_WANT_READ;
	case MBEDTLS_ERR_SSL_WANT_WRITE:
		connection->events = POLLOUT;
		return CONTROL_TLS_WANT_WRITE;
	case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
	case MBEDTLS_ERR_SSL_CONN_EOF:
		return CONTROL_TLS_CLOSED;
	default:
		return CONTROL_TLS_ERROR;
	}
}

struct control_tls_connection *control_tls_accept(int fd)
{
	struct control_tls_connection *connection;

	if (!tls.context)
		return NULL;
	connection = calloc(1, sizeof(*connection));
	if (!connection)
		return NULL;
	mbedtls_ssl_init(&connection->ssl);
	if (mbedtls_ssl_setup(&connection->ssl, &tls.context->config))
	{
		mbedtls_ssl_free(&connection->ssl);
		free(connection);
		return NULL;
	}
#ifdef SO_NOSIGPIPE
	{
		int one = 1;

		setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
	}
#endif
	connection->fd = fd;
	connection->events = POLLIN;
	connection->context = tls.context;
	tls.context->references++;
	mbedtls_ssl_set_bio(&connection->ssl, connection, bio_send, bio_receive, NULL);
	return connection;
}

int control_tls_handshake(struct control_tls_connection *connection)
{
	int result;

	/* (a browser sent to http:// on the TLS port: told so, plainly) */
	if (!connection->checked)
	{
		unsigned char first;
		ssize_t count = recv(connection->fd, &first, 1, MSG_PEEK);

		if (count == 0)
			return CONTROL_TLS_CLOSED;
		if (count < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			{
				connection->events = POLLIN;
				return CONTROL_TLS_WANT_READ;
			}
			return CONTROL_TLS_ERROR;
		}
		connection->checked = 1;
		/* (a TLS record of a handshake begins 0x16; an HTTP request with its
		method, in capitals) */
		if (first >= 'A' && first <= 'Z')
			return CONTROL_TLS_NOT_TLS;
	}
	result = mbedtls_ssl_handshake(&connection->ssl);
	return result ? result_of(connection, result) : CONTROL_TLS_DONE;
}

long control_tls_read(struct control_tls_connection *connection, void *buffer, size_t size)
{
	int result;

	for (;;)
	{
		result = mbedtls_ssl_read(&connection->ssl, buffer, size);
		if (result > 0)
			return result;
		if (result == 0)
			return CONTROL_TLS_CLOSED;
#if defined(MBEDTLS_SSL_PROTO_TLS1_3)
		/* (a TLS 1.3 message after the handshake, read and nothing else) */
		if (result == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
			continue;
#endif
		return result_of(connection, result);
	}
}

long control_tls_write(struct control_tls_connection *connection, const void *data, size_t size)
{
	int result = mbedtls_ssl_write(&connection->ssl, data, size);

	if (result > 0)
		return result;
	if (result == 0)
		return CONTROL_TLS_ERROR;
	return result_of(connection, result);
}

int control_tls_pending(const struct control_tls_connection *connection)
{
	return mbedtls_ssl_get_bytes_avail(&connection->ssl) > 0;
}

short control_tls_poll_events(const struct control_tls_connection *connection)
{
	return connection->events;
}

int control_tls_close_notify(struct control_tls_connection *connection)
{
	int result = mbedtls_ssl_close_notify(&connection->ssl);

	return result ? result_of(connection, result) : CONTROL_TLS_DONE;
}

const char *control_tls_version(const struct control_tls_connection *connection)
{
	return mbedtls_ssl_get_version(&connection->ssl);
}

void control_tls_free(struct control_tls_connection *connection)
{
	if (!connection)
		return;
	mbedtls_ssl_free(&connection->ssl);
	context_release(connection->context);
	free(connection);
}
