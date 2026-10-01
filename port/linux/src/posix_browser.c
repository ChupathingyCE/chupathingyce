/*
POSIX_BROWSER.C

The game list server's requests (browser_http.h), for browser.c
(configure.py --game-browser): HTTP/1.0 over a TCP connection, with TLS
(Mbed TLS, as posix_update.c) for https:// addresses. HTTP/1.0 keeps the
response simple: the server sends the body as it is and closes the
connection when it is done.

The server's certificate must chain to one of the system's certificate
authorities and name the host, as for the updater.

Built with the host's ABI, as the other posix_*.c. The Windows build
compiles it too (tools/windows_build.py), on Winsock, with Windows' own
certificate authorities, so that the game list behaves the same on every
platform.
*/

#ifdef HALO_GAME_BROWSER

#include "browser_http.h"

#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/psa_util.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#define strncasecmp _strnicmp
#define close_socket closesocket
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <strings.h>
#define close_socket close
#endif
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BROWSER_USER_AGENT "chupathingyce-browser"
#define TIMEOUT_MILLISECONDS 10000

#ifndef _WIN32
/* where systems keep their certificate authorities */
static const char *const certificate_bundles[] =
{
	"/etc/ssl/certs/ca-certificates.crt", /* Debian, Ubuntu, Arch, Gentoo */
	"/etc/pki/tls/certs/ca-bundle.crt", /* Fedora, RHEL */
	"/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
	"/etc/ssl/ca-bundle.pem", /* openSUSE */
	"/etc/ssl/cert.pem", /* Alpine, Arch, Void, macOS */
};
#endif

static mbedtls_x509_crt certificates;
static int certificates_loaded;
static int crypto_ready;

static void load_certificates(void)
{
	const char *environment = getenv("SSL_CERT_FILE");
	size_t index;

	crypto_ready = psa_crypto_init() == PSA_SUCCESS;
	mbedtls_x509_crt_init(&certificates);
	if (environment && *environment && mbedtls_x509_crt_parse_file(&certificates, environment) >= 0)
	{
		certificates_loaded = 1;
		return;
	}
#ifdef _WIN32
	/* Windows' certificate authorities: its store's (a certificate it
	cannot read skipped) */
	{
		HCERTSTORE store = CertOpenSystemStoreW(0, L"ROOT");
		PCCERT_CONTEXT certificate = NULL;

		if (store)
		{
			while ((certificate = CertEnumCertificatesInStore(store, certificate)) != NULL)
			{
				if (certificate->dwCertEncodingType & X509_ASN_ENCODING &&
					mbedtls_x509_crt_parse_der(&certificates, certificate->pbCertEncoded,
						certificate->cbCertEncoded) == 0)
				{
					certificates_loaded = 1;
				}
			}
			CertCloseStore(store, 0);
		}
	}
	(void)index;
#else
#ifdef __ANDROID__
	/* Android's: a folder of them (the app runs the requests in its host,
	port/android) */
	if (mbedtls_x509_crt_parse_path(&certificates, "/system/etc/security/cacerts") >= 0)
	{
		certificates_loaded = 1;
		return;
	}
#endif
	for (index = 0; index < sizeof(certificate_bundles) / sizeof(certificate_bundles[0]); index++)
	{
		/* (a bundle with a few certificates it cannot read still counts) */
		if (mbedtls_x509_crt_parse_file(&certificates, certificate_bundles[index]) >= 0)
		{
			certificates_loaded = 1;
			return;
		}
	}
#endif
}

/* the certificate authorities and the crypto, set up once */
#ifdef _WIN32
static INIT_ONCE certificates_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK load_certificates_once(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
	WSADATA data;

	(void)once;
	(void)parameter;
	(void)context;
	/* (Winsock, for this file's sockets: counted, so the game's is kept) */
	WSAStartup(MAKEWORD(2, 2), &data);
	load_certificates();
	return TRUE;
}

static void set_up(void)
{
	InitOnceExecuteOnce(&certificates_once, load_certificates_once, NULL, NULL);
}
#else
static pthread_once_t certificates_once = PTHREAD_ONCE_INIT;

static void set_up(void)
{
	pthread_once(&certificates_once, load_certificates);
}
#endif

static void set_error(char *error, int error_size, const char *what, int code)
{
	char reason[128];

	if (code)
	{
		mbedtls_strerror(code, reason, sizeof(reason));
		snprintf(error, (size_t)error_size, "%s (%s)", what, reason);
	}
	else
	{
		snprintf(error, (size_t)error_size, "%s", what);
	}
}

/* http[s]://host[:port][/path] */
static int parse_url(const char *url, int *secure, char *host, size_t host_size, char *port, size_t port_size,
	char *path, size_t path_size)
{
	const char *cursor;
	const char *slash;
	const char *colon;
	size_t length;

	if (!strncasecmp(url, "https://", 8))
	{
		*secure = 1;
		cursor = url + 8;
	}
	else if (!strncasecmp(url, "http://", 7))
	{
		*secure = 0;
		cursor = url + 7;
	}
	else
	{
		return 0;
	}
	slash = strchr(cursor, '/');
	length = slash ? (size_t)(slash - cursor) : strlen(cursor);
	colon = memchr(cursor, ':', length);
	if (colon)
	{
		if ((size_t)(colon - cursor) >= host_size || (size_t)(length - (colon + 1 - cursor)) >= port_size)
			return 0;
		memcpy(host, cursor, (size_t)(colon - cursor));
		host[colon - cursor] = 0;
		memcpy(port, colon + 1, length - (size_t)(colon + 1 - cursor));
		port[length - (size_t)(colon + 1 - cursor)] = 0;
	}
	else
	{
		if (length >= host_size)
			return 0;
		memcpy(host, cursor, length);
		host[length] = 0;
		snprintf(port, port_size, "%s", *secure ? "443" : "80");
	}
	snprintf(path, path_size, "%s", slash ? slash : "/");
	return host[0] != 0;
}

struct connection
{
	int secure;
	mbedtls_net_context net;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config config;
};

static void connection_free(struct connection *connection)
{
	if (connection->secure)
	{
		mbedtls_ssl_close_notify(&connection->ssl);
	}
	mbedtls_ssl_free(&connection->ssl);
	mbedtls_ssl_config_free(&connection->config);
	mbedtls_net_free(&connection->net);
}

/* a TCP connection over IPv4 alone: the game list matches a player's
address as their game's host saw it, which internet play (IPv4) has */
static int connect_ipv4(mbedtls_net_context *net, const char *host, const char *port)
{
	struct addrinfo hints, *addresses, *address;
#ifdef _WIN32
	SOCKET descriptor = INVALID_SOCKET;
#define NO_SOCKET INVALID_SOCKET
#else
	int descriptor = -1;
#define NO_SOCKET (-1)
#endif

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	if (getaddrinfo(host, port, &hints, &addresses) != 0)
		return MBEDTLS_ERR_NET_UNKNOWN_HOST;
	for (address = addresses; address && descriptor == NO_SOCKET; address = address->ai_next)
	{
		descriptor = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
		if (descriptor == NO_SOCKET)
			continue;
		if (connect(descriptor, address->ai_addr, (int)address->ai_addrlen) != 0)
		{
			close_socket(descriptor);
			descriptor = NO_SOCKET;
		}
	}
	freeaddrinfo(addresses);
	if (descriptor == NO_SOCKET)
		return MBEDTLS_ERR_NET_CONNECT_FAILED;
#ifndef _WIN32
	fcntl(descriptor, F_SETFD, FD_CLOEXEC);
#endif
	net->fd = (int)descriptor;
	return 0;
}

static int connection_open(struct connection *connection, const char *host, const char *port, char *error,
	int error_size)
{
	int result;

	if ((result = connect_ipv4(&connection->net, host, port)) != 0)
	{
		set_error(error, error_size, "could not connect", result);
		return 0;
	}
	if (!connection->secure)
	{
		return 1;
	}
	if ((result = mbedtls_ssl_config_defaults(&connection->config, MBEDTLS_SSL_IS_CLIENT,
		MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
	{
		set_error(error, error_size, "could not set up TLS", result);
		return 0;
	}
	/* (the certificate and the host name checked, always) */
	mbedtls_ssl_conf_authmode(&connection->config, MBEDTLS_SSL_VERIFY_REQUIRED);
	mbedtls_ssl_conf_ca_chain(&connection->config, &certificates, NULL);
	mbedtls_ssl_conf_rng(&connection->config, mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE);
	mbedtls_ssl_conf_read_timeout(&connection->config, TIMEOUT_MILLISECONDS);
	if ((result = mbedtls_ssl_setup(&connection->ssl, &connection->config)) != 0 ||
		(result = mbedtls_ssl_set_hostname(&connection->ssl, host)) != 0)
	{
		set_error(error, error_size, "could not set up TLS", result);
		return 0;
	}
	mbedtls_ssl_set_bio(&connection->ssl, &connection->net, mbedtls_net_send, NULL, mbedtls_net_recv_timeout);
	while ((result = mbedtls_ssl_handshake(&connection->ssl)) != 0)
	{
		if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE)
		{
			unsigned int flags = mbedtls_ssl_get_verify_result(&connection->ssl);

			if (flags && flags != (unsigned int)-1)
			{
				char reason[256];

				mbedtls_x509_crt_verify_info(reason, sizeof(reason), "", flags);
				reason[strcspn(reason, "\n")] = 0;
				snprintf(error, (size_t)error_size, "the server's certificate was refused (%s)", reason);
			}
			else
			{
				set_error(error, error_size, "the TLS handshake failed", result);
			}
			return 0;
		}
	}
	return 1;
}

static int connection_write(struct connection *connection, const char *data, size_t size)
{
	while (size)
	{
		int written = connection->secure
			? mbedtls_ssl_write(&connection->ssl, (const unsigned char *)data, size)
			: mbedtls_net_send(&connection->net, (const unsigned char *)data, size);

		if (written == MBEDTLS_ERR_SSL_WANT_READ || written == MBEDTLS_ERR_SSL_WANT_WRITE)
			continue;
		if (written <= 0)
			return 0;
		data += written;
		size -= (size_t)written;
	}
	return 1;
}

/* some bytes, 0 at the end, negative on an error */
static int connection_read(struct connection *connection, unsigned char *buffer, size_t size)
{
	for (;;)
	{
		int count = connection->secure
			? mbedtls_ssl_read(&connection->ssl, buffer, size)
			: mbedtls_net_recv_timeout(&connection->net, buffer, size, TIMEOUT_MILLISECONDS);

		if (count == MBEDTLS_ERR_SSL_WANT_READ || count == MBEDTLS_ERR_SSL_WANT_WRITE)
			continue;
		/* (a server that closes without telling TLS: the end all the same) */
		if (count == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || count == MBEDTLS_ERR_NET_CONN_RESET)
			return 0;
		return count;
	}
}

int posix_browser_request(const char *url, const char *form, const char *content_type, char *response,
	int response_size, char *error, int error_size)
{
	char host[256], port[16], path[512];
	char request[4096];
	char *long_request = NULL;
	char *buffer;
	size_t capacity = 65536, used = 0;
	struct connection connection;
	int secure;
	int status = 0;
	int length;
	char *body;

	error[0] = 0;
	if (response_size > 0)
		response[0] = 0;
	if (!parse_url(url, &secure, host, sizeof(host), port, sizeof(port), path, sizeof(path)))
	{
		set_error(error, error_size, "not an http:// or https:// address", 0);
		return 0;
	}
	if (secure)
	{
		set_up();
		if (!crypto_ready || !certificates_loaded)
		{
			set_error(error, error_size, "no certificate authorities (set SSL_CERT_FILE)", 0);
			return 0;
		}
	}
	if (form)
	{
		/* (a long body, as a carnage report: a request of its own size) */
		size_t size = strlen(form) + 1024;

		long_request = malloc(size);
		if (!long_request)
		{
			set_error(error, error_size, "out of memory", 0);
			return 0;
		}
		length = snprintf(long_request, size,
			"POST %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: " BROWSER_USER_AGENT "\r\n"
			"Content-Type: %s\r\nContent-Length: %zu\r\n\r\n%s",
			path, host, content_type ? content_type : "application/x-www-form-urlencoded", strlen(form), form);
		if (length <= 0 || (size_t)length >= size)
		{
			free(long_request);
			set_error(error, error_size, "the request is too long", 0);
			return 0;
		}
	}
	else
	{
		length = snprintf(request, sizeof(request),
			"GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: " BROWSER_USER_AGENT "\r\n\r\n", path, host);
	}
	if (!long_request && (length <= 0 || (size_t)length >= sizeof(request)))
	{
		set_error(error, error_size, "the request is too long", 0);
		return 0;
	}
	memset(&connection, 0, sizeof(connection));
	connection.secure = secure;
	mbedtls_net_init(&connection.net);
	mbedtls_ssl_init(&connection.ssl);
	mbedtls_ssl_config_init(&connection.config);
	buffer = malloc(capacity + 1);
	if (!buffer)
	{
		set_error(error, error_size, "out of memory", 0);
		connection_free(&connection);
		free(long_request);
		return 0;
	}
	if (connection_open(&connection, host, port, error, error_size))
	{
		if (!connection_write(&connection, long_request ? long_request : request, (size_t)length))
		{
			set_error(error, error_size, "could not send the request", 0);
		}
		else
		{
			int count;

			while (used < capacity && (count = connection_read(&connection, (unsigned char *)buffer + used,
				capacity - used)) > 0)
			{
				used += (size_t)count;
			}
			buffer[used] = 0;
			if (sscanf(buffer, "HTTP/%*d.%*d %d", &status) != 1)
			{
				status = 0;
				set_error(error, error_size, "not an HTTP response", 0);
			}
			else if ((body = strstr(buffer, "\r\n\r\n")) != NULL && response_size > 0)
			{
				snprintf(response, (size_t)response_size, "%s", body + 4);
			}
		}
	}
	connection_free(&connection);
	free(buffer);
	free(long_request);
	return status;
}


#ifdef _WIN32

/* a UTF-8 path as Windows' */
static int wide_path(const char *path, wchar_t *wide, int size)
{
	return MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, size) > 0;
}

/* the game list's player key (browser.c): read from path, or made there
(random) the first time. The save folder (in the user's application data)
is the user's alone; the file must be a plain file, not a link */
int posix_browser_private_key(const char *path, unsigned char *key, int size)
{
	wchar_t wide[1024];
	int attempt;

	if (!wide_path(path, wide, 1024))
		return 0;
	for (attempt = 0; attempt < 3; attempt++)
	{
		DWORD done = 0;
		HANDLE file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
		int ok;

		if (file != INVALID_HANDLE_VALUE)
		{
			BY_HANDLE_FILE_INFORMATION information;

			ok = GetFileInformationByHandle(file, &information) &&
				!(information.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
				ReadFile(file, key, (DWORD)size, &done, NULL) && done == (DWORD)size;
			CloseHandle(file);
			return ok;
		}
		if (GetLastError() != ERROR_FILE_NOT_FOUND)
			return 0;
		file = CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		/* (another copy of the game made it first: read that one) */
		if (file == INVALID_HANDLE_VALUE)
			continue;
		ok = BCRYPT_SUCCESS(BCryptGenRandom(NULL, key, (ULONG)size, BCRYPT_USE_SYSTEM_PREFERRED_RNG)) &&
			WriteFile(file, key, (DWORD)size, &done, NULL) && done == (DWORD)size && FlushFileBuffers(file);
		CloseHandle(file);
		if (!ok)
			DeleteFileW(wide);
		return ok;
	}
	return 0;
}

/* a player key put in place of the one at path (a restored key): written
to a new file beside it, then moved over it */
int posix_browser_replace_key(const char *path, const unsigned char *key, int size)
{
	char temporary[1100];
	wchar_t wide[1024], wide_temporary[1100];
	DWORD done = 0;
	HANDLE file;
	int ok;

	snprintf(temporary, sizeof(temporary), "%s.new", path);
	if (!wide_path(path, wide, 1024) || !wide_path(temporary, wide_temporary, 1100))
		return 0;
	DeleteFileW(wide_temporary);
	file = CreateFileW(wide_temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return 0;
	ok = WriteFile(file, key, (DWORD)size, &done, NULL) && done == (DWORD)size && FlushFileBuffers(file);
	CloseHandle(file);
	if (!ok || !MoveFileExW(wide_temporary, wide, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		DeleteFileW(wide_temporary);
		return 0;
	}
	return 1;
}

#else

/* the game list's player key (browser.c): read from path, or made there
(random, readable by this user alone) the first time. As
posix_user_secret: only a file of the user's no one else can read, and not
a link */
int posix_browser_private_key(const char *path, unsigned char *key, int size)
{
	int attempt;

	for (attempt = 0; attempt < 3; attempt++)
	{
		struct stat status;
		int descriptor = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
		int ok;

		if (descriptor >= 0)
		{
			ok = fstat(descriptor, &status) == 0 && S_ISREG(status.st_mode) && status.st_uid == getuid() &&
				!(status.st_mode & 077) && read(descriptor, key, (size_t)size) == size;
			close(descriptor);
			return ok;
		}
		if (errno != ENOENT)
			return 0;
		descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
		/* (another copy of the game made it first: read that one) */
		if (descriptor < 0)
			continue;
		/* (the PSA crypto set up first: load_certificates does) */
		set_up();
		if (!crypto_ready || psa_generate_random(key, (size_t)size) != PSA_SUCCESS)
		{
			close(descriptor);
			unlink(path);
			return 0;
		}
		ok = write(descriptor, key, (size_t)size) == size && fsync(descriptor) == 0;
		close(descriptor);
		if (!ok)
			unlink(path);
		return ok;
	}
	return 0;
}

/* a player key put in place of the one at path (a restored key): written
to a new file beside it (readable by this user alone), then renamed over it */
int posix_browser_replace_key(const char *path, const unsigned char *key, int size)
{
	char temporary[1100];
	int descriptor;
	int ok;

	snprintf(temporary, sizeof(temporary), "%s.new", path);
	unlink(temporary);
	descriptor = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (descriptor < 0)
		return 0;
	ok = write(descriptor, key, (size_t)size) == size && fsync(descriptor) == 0;
	close(descriptor);
	if (!ok || rename(temporary, path) != 0)
	{
		unlink(temporary);
		return 0;
	}
	return 1;
}

#endif /* _WIN32 */

#endif
