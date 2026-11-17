/*
POSIX_UPDATE.C

The Linux self-updater's system side (update.h): a download over HTTPS with
Mbed TLS (port/third_party/mbedtls), and the files of the running game
replaced and the game started again: in this process's place as it exits
(update_start_in_place: an execve at the end of exit's handlers), so the
new game keeps the process Steam and gamescope follow, or else as a process
of its own (update_launch).

The download is a plain HTTP/1.1 GET over TLS 1.2 or 1.3, following
redirects (GitHub sends release downloads to its file host). The server's
certificate must chain to one of the system's certificate authorities (the
bundle the distribution keeps for OpenSSL, curl and the rest) and name the
host; nothing is sent before that is checked. The file SSL_CERT_FILE names
is used only on a system with no bundle, with a warning on stderr: whatever
sets the environment would otherwise choose whom updates are trusted from.
(A build for the updater's test, HALO_UPDATE_TEST, which no release is,
trusts only the certificate HALO_UPDATE_TEST_CA names: tools/test_update_install.py.)

Built with the host's ABI, as the other posix_*.c.
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "update.h"
#include "build_identity.h"
#ifdef HALO_GAME_BROWSER
#include "browser_http.h"
#endif

#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/psa_util.h"
#include "mbedtls/platform.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>

#define MAXIMUM_REDIRECTS 8
#define TIMEOUT_MILLISECONDS 20000
#define MAXIMUM_HEADER_SIZE 16384
/* the most environment variables a started game is given (update_launch) */
#define MAXIMUM_ENVIRONMENT 1024

extern char **environ;

/* where distributions keep their certificate authorities */
static const char *const certificate_bundles[] =
{
	"/etc/ssl/certs/ca-certificates.crt", /* Debian, Ubuntu, Arch, Gentoo */
	"/etc/pki/tls/certs/ca-bundle.crt", /* Fedora, RHEL */
	"/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
	"/etc/ssl/ca-bundle.pem", /* openSUSE */
	"/etc/ssl/cert.pem", /* Alpine, Arch, Void */
};

static pthread_once_t certificates_once = PTHREAD_ONCE_INIT;
static mbedtls_x509_crt certificates;
static int certificates_loaded;
static int crypto_ready;

static void load_certificates(void)
{
	const char *environment = getenv("SSL_CERT_FILE");
	size_t index;

#if defined(__GLIBC__) && defined(MBEDTLS_PLATFORM_HAVE_DEV_RANDOM)
#if !__GLIBC_PREREQ(2, 25)
	/* (glibc before 2.25 on a kernel without getrandom, the Steam Link's 2.19
	on Linux 3.8: its /dev/random holds so little that this waited half a
	minute for it before the first connection; /dev/urandom is what
	getrandom would have drawn from) */
	mbedtls_platform_dev_random = "/dev/urandom";
#endif
#endif
	crypto_ready = psa_crypto_init() == PSA_SUCCESS;
	mbedtls_x509_crt_init(&certificates);
#ifdef HALO_UPDATE_TEST
	environment = getenv("HALO_UPDATE_TEST_CA");
	certificates_loaded = environment && *environment && mbedtls_x509_crt_parse_file(&certificates, environment) >= 0 &&
		certificates.version;
	return;
#endif
#ifdef HALO_CONSOLE_APP_CERTIFICATES
	/* a console whose own bundle is too old for today's servers: the app's,
	which its launch script names in SSL_CERT_FILE, trusted ahead of the
	system's (the app is what an update replaces, and the update is signed
	besides) */
	if (environment && *environment && mbedtls_x509_crt_parse_file(&certificates, environment) >= 0 &&
		certificates.version)
	{
		certificates_loaded = 1;
		return;
	}
#endif
	for (index = 0; index < sizeof(certificate_bundles) / sizeof(*certificate_bundles); index++)
	{
		/* (a bundle's certificates that do not parse are left out: the
		result counts them) */
		if (access(certificate_bundles[index], R_OK) == 0 &&
			mbedtls_x509_crt_parse_file(&certificates, certificate_bundles[index]) >= 0 && certificates.version)
		{
			certificates_loaded = 1;
			if (environment && *environment)
			{
				fprintf(stderr, PLATFORM_LOG_PREFIX "update: SSL_CERT_FILE is ignored; the update server is checked "
					"against %s\n", certificate_bundles[index]);
			}
			return;
		}
	}
	/* no bundle of the system's: the environment's, said loudly, since it
	decides which servers updates are taken from */
	if (environment && *environment && mbedtls_x509_crt_parse_file(&certificates, environment) >= 0 &&
		certificates.version)
	{
		certificates_loaded = 1;
		fprintf(stderr, PLATFORM_LOG_PREFIX "update: WARNING: no system certificate authorities were found; the update "
			"server is checked against SSL_CERT_FILE (%s) instead\n", environment);
	}
}

static void set_error(char *error, int error_size, const char *what, int code)
{
	char reason[160];

	if (!error || error_size <= 0)
		return;
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

/* https://host[:port][/path] */
static int parse_url(const char *url, char *host, size_t host_size, char *port, size_t port_size, char *path,
	size_t path_size)
{
	const char *start, *end, *colon;

	if (strncasecmp(url, "https://", 8) != 0)
		return 0;
	start = url + 8;
	end = start + strcspn(start, "/?#");
	colon = memchr(start, ':', (size_t)(end - start));
	if ((colon ? colon : end) == start || (size_t)((colon ? colon : end) - start) >= host_size)
		return 0;
	snprintf(host, host_size, "%.*s", (int)((colon ? colon : end) - start), start);
	if (colon)
		snprintf(port, port_size, "%.*s", (int)(end - colon - 1), colon + 1);
	else
		snprintf(port, port_size, "443");
	snprintf(path, path_size, "%s", *end == '/' ? end : "/");
	return 1;
}

struct connection
{
	mbedtls_net_context net;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config config;
	unsigned char buffer[16384];
	size_t start, end;
	int closed;
};

static void connection_free(struct connection *connection)
{
	mbedtls_ssl_close_notify(&connection->ssl);
	mbedtls_ssl_free(&connection->ssl);
	mbedtls_ssl_config_free(&connection->config);
	mbedtls_net_free(&connection->net);
}

static int connection_open(struct connection *connection, const char *host, const char *port, char *error,
	int error_size)
{
	int result;

	mbedtls_net_init(&connection->net);
	mbedtls_ssl_init(&connection->ssl);
	mbedtls_ssl_config_init(&connection->config);
	connection->start = connection->end = 0;
	connection->closed = 0;
	if ((result = mbedtls_net_connect(&connection->net, host, port, MBEDTLS_NET_PROTO_TCP)) != 0)
	{
		set_error(error, error_size, "could not connect", result);
		return 0;
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
		int result = mbedtls_ssl_write(&connection->ssl, (const unsigned char *)data, size);

		if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE)
			continue;
		if (result <= 0)
			return 0;
		data += result;
		size -= (size_t)result;
	}
	return 1;
}

/* more received into the buffer; 0 at the end of the stream, -1 on error */
static int connection_fill(struct connection *connection)
{
	int result;

	if (connection->closed)
		return 0;
	if (connection->start == connection->end)
		connection->start = connection->end = 0;
	if (connection->end == sizeof(connection->buffer))
	{
		memmove(connection->buffer, connection->buffer + connection->start, connection->end - connection->start);
		connection->end -= connection->start;
		connection->start = 0;
		if (connection->end == sizeof(connection->buffer))
			return -1;
	}
	for (;;)
	{
		result = mbedtls_ssl_read(&connection->ssl, connection->buffer + connection->end,
			sizeof(connection->buffer) - connection->end);
		if (result > 0)
		{
			connection->end += (size_t)result;
			return 1;
		}
		if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE ||
			result == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
		{
			continue;
		}
		if (result == 0 || result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || result == MBEDTLS_ERR_NET_CONN_RESET)
		{
			connection->closed = 1;
			return 0;
		}
		return -1;
	}
}

/* a line (without its CRLF) into line; 0 if there is none */
static int connection_read_line(struct connection *connection, char *line, size_t size)
{
	for (;;)
	{
		unsigned char *start = connection->buffer + connection->start;
		unsigned char *newline = memchr(start, '\n', connection->end - connection->start);

		if (newline)
		{
			size_t length = (size_t)(newline - start);

			if (length && start[length - 1] == '\r')
				length--;
			if (length >= size)
				return 0;
			memcpy(line, start, length);
			line[length] = 0;
			connection->start += (size_t)(newline - start) + 1;
			return 1;
		}
		if (connection_fill(connection) <= 0)
			return 0;
	}
}

/* up to size bytes of the body into buffer; 0 at the end, -1 on error */
static int connection_read(struct connection *connection, unsigned char *buffer, size_t size)
{
	size_t available;

	if (connection->start == connection->end)
	{
		int result = connection_fill(connection);

		if (result <= 0)
			return result;
	}
	available = connection->end - connection->start;
	if (available > size)
		available = size;
	memcpy(buffer, connection->buffer + connection->start, available);
	connection->start += available;
	return (int)available;
}

struct download
{
	FILE *file;
	update_progress_proc progress;
	void *context;
	unsigned long long received, total, maximum;
};

/* (a release is tens of megabytes: a body past this fills no disk) */
#define MAXIMUM_DOWNLOAD_SIZE (512ull * 1024 * 1024)

static int body_write(struct download *download, const unsigned char *data, size_t size)
{
	/* (no more than the caller expects: a body that goes on is refused) */
	if (size > download->maximum - download->received)
		return 0;
	if (fwrite(data, 1, size, download->file) != size)
		return 0;
	download->received += size;
	if (download->progress)
		download->progress(download->context, download->received, download->total);
	return 1;
}

/* the response's body, as long as its headers say, chunked or to the end */
static int read_body(struct connection *connection, struct download *download, int chunked,
	unsigned long long length, int have_length)
{
	unsigned char buffer[16384];

	if (chunked)
	{
		for (;;)
		{
			char line[128];
			char *after;
			unsigned long long remaining;

			if (!connection_read_line(connection, line, sizeof(line)))
				return 0;
			/* (a size in hexadecimal, perhaps with extensions after a ";": a
			line without one is no chunk, and not the last) */
			remaining = strtoull(line, &after, 16);
			if (after == line || (*after && *after != ';' && *after != ' ' && *after != '\t' &&
				*after != '\r' && *after != '\n'))
			{
				return 0;
			}
			if (!remaining)
				return 1;
			while (remaining)
			{
				int count = connection_read(connection, buffer,
					remaining < sizeof(buffer) ? (size_t)remaining : sizeof(buffer));

				if (count <= 0 || !body_write(download, buffer, (size_t)count))
					return 0;
				remaining -= (unsigned long long)count;
			}
			/* (the chunk's CRLF) */
			if (!connection_read_line(connection, line, sizeof(line)))
				return 0;
		}
	}
	for (;;)
	{
		size_t wanted = sizeof(buffer);
		int count;

		if (have_length)
		{
			if (download->received >= length)
				return 1;
			if (length - download->received < wanted)
				wanted = (size_t)(length - download->received);
		}
		count = connection_read(connection, buffer, wanted);
		if (count < 0)
			return 0;
		if (count == 0)
			return !have_length;
		if (!body_write(download, buffer, (size_t)count))
			return 0;
	}
}

/* one GET: the body into download on 200, the Location on a redirect;
the status, or 0 on failure */
static int https_get(const char *url, struct download *download, char *location, size_t location_size, char *error,
	int error_size)
{
	static struct connection connection;
	char host[256], port[16], path[2048];
	char request[3072];
	char line[MAXIMUM_HEADER_SIZE];
	unsigned long long length = 0;
	int have_length = 0, chunked = 0, status = 0;

	if (!parse_url(url, host, sizeof(host), port, sizeof(port), path, sizeof(path)))
	{
		snprintf(error, (size_t)error_size, "not an https:// address: %s", url);
		return 0;
	}
	if (!connection_open(&connection, host, port, error, error_size))
	{
		connection_free(&connection);
		return 0;
	}
	snprintf(request, sizeof(request),
		"GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\n"
		"Accept: */*\r\nConnection: close\r\n\r\n",
		path, host, build_identity_user_agent());
	if (!connection_write(&connection, request, strlen(request)) ||
		!connection_read_line(&connection, line, sizeof(line)) ||
		sscanf(line, "HTTP/%*d.%*d %d", &status) != 1)
	{
		snprintf(error, (size_t)error_size, "no answer from %s", host);
		connection_free(&connection);
		return 0;
	}
	location[0] = 0;
	/* the headers */
	for (;;)
	{
		char *value;

		if (!connection_read_line(&connection, line, sizeof(line)))
		{
			snprintf(error, (size_t)error_size, "a broken answer from %s", host);
			connection_free(&connection);
			return 0;
		}
		if (!line[0])
			break;
		value = strchr(line, ':');
		if (!value)
			continue;
		*value++ = 0;
		value += strspn(value, " \t");
		if (!strcasecmp(line, "Content-Length"))
		{
			length = strtoull(value, NULL, 10);
			have_length = 1;
			if (length > MAXIMUM_DOWNLOAD_SIZE)
			{
				snprintf(error, (size_t)error_size, "the download from %s is too large", host);
				connection_free(&connection);
				return 0;
			}
		}
		else if (!strcasecmp(line, "Transfer-Encoding") && strcasestr(value, "chunked"))
		{
			chunked = 1;
		}
		else if (!strcasecmp(line, "Location"))
		{
			snprintf(location, location_size, "%s", value);
		}
	}
	if (status == 200)
	{
		if (have_length && !chunked && length > download->maximum)
		{
			snprintf(error, (size_t)error_size, "the download from %s is larger than expected", host);
			connection_free(&connection);
			return 0;
		}
		download->total = have_length && !chunked ? length : 0;
		if (!read_body(&connection, download, chunked, length, have_length && !chunked))
		{
			snprintf(error, (size_t)error_size, "the download from %s broke off", host);
			status = 0;
		}
	}
	connection_free(&connection);
	return status;
}

static int download_file(const char *url, const char *path, unsigned long long maximum, update_progress_proc progress,
	void *context, char *error, int error_size)
{
	char current[2048];
	char location[2048];
	struct download download;
	int redirect;

	pthread_once(&certificates_once, load_certificates);
	if (!crypto_ready)
	{
		snprintf(error, (size_t)error_size, "could not start the cryptography");
		return 0;
	}
	if (!certificates_loaded)
	{
		snprintf(error, (size_t)error_size, "the system's certificate authorities were not found "
			"(install ca-certificates, or set SSL_CERT_FILE)");
		return 0;
	}
	memset(&download, 0, sizeof(download));
	download.progress = progress;
	download.context = context;
	download.maximum = maximum;
	download.file = fopen(path, "wb");
	if (!download.file)
	{
		snprintf(error, (size_t)error_size, "could not write %s (%s)", path, strerror(errno));
		return 0;
	}
	snprintf(current, sizeof(current), "%s", url);
	for (redirect = 0; redirect <= MAXIMUM_REDIRECTS; redirect++)
	{
		int status = https_get(current, &download, location, sizeof(location), error, error_size);

		if (status == 200)
		{
			if (fclose(download.file) != 0)
			{
				snprintf(error, (size_t)error_size, "could not write %s", path);
				unlink(path);
				return 0;
			}
			return 1;
		}
		if (status >= 300 && status < 400 && location[0])
		{
			/* (an address on the same host, or another's) */
			if (location[0] == '/')
			{
				char host[256], port[16], path_part[2048];

				parse_url(current, host, sizeof(host), port, sizeof(port), path_part, sizeof(path_part));
				snprintf(current, sizeof(current), "https://%s:%s%s", host, port, location);
			}
			else
			{
				snprintf(current, sizeof(current), "%s", location);
			}
			continue;
		}
		if (status)
			snprintf(error, (size_t)error_size, "the server answered %d", status);
		break;
	}
	if (redirect > MAXIMUM_REDIRECTS)
		snprintf(error, (size_t)error_size, "too many redirects");
	fclose(download.file);
	unlink(path);
	return 0;
}

int update_download(const char *url, const char *path, unsigned long long maximum, update_progress_proc progress,
	void *context, char *error, int error_size)
{
	int succeeded;

	/* (in turn with the game list's requests, which use the same Mbed TLS:
	it has no locks of its own, and two threads setting up its PSA crypto
	or drawing on its random generator at once crash it) */
#ifdef HALO_GAME_BROWSER
	posix_browser_lock();
#endif
	succeeded = download_file(url, path, maximum, progress, context, error, error_size);
#ifdef HALO_GAME_BROWSER
	posix_browser_unlock();
#endif
	return succeeded;
}

/* ---------- files and processes */

int update_executable_path(char *path, int size)
{
	const char *appimage = getenv("APPIMAGE");
	ssize_t length;

	/* (none for an AppImage, mounted read-only, whose files cannot be
	replaced: the AppImage file itself is, update_appimage_path) */
	if (appimage && *appimage)
		return 0;
	length = readlink("/proc/self/exe", path, (size_t)size - 1);
	if (length <= 0)
		return 0;
	path[length] = 0;
	return 1;
}

int update_appimage_path(char *path, int size)
{
	const char *appimage = getenv("APPIMAGE");
	char resolved[PATH_MAX];

	/* (the AppImage runtime gives its own file's full path; a link to it is
	followed, so the file itself is replaced, not the link) */
	if (!appimage || !*appimage || !realpath(appimage, resolved) || strlen(resolved) >= (size_t)size)
		return 0;
	snprintf(path, (size_t)size, "%s", resolved);
	return 1;
}

int update_replace_file(const char *path, const char *new_path, const char *old_path)
{
	struct stat information;
	int existed = stat(path, &information) == 0;

	unlink(old_path);
	if (existed && rename(path, old_path) != 0)
		return 0;
	if (rename(new_path, path) != 0)
	{
		if (existed)
			rename(old_path, path);
		return 0;
	}
	/* (the new file as the old was: an executable stays executable) */
	chmod(path, existed ? (information.st_mode & 07777) : 0644);
	return 1;
}

int update_make_executable(const char *path)
{
	struct stat information;

	/* (executable by whoever may read it) */
	return stat(path, &information) == 0 &&
		chmod(path, (information.st_mode & 07777) | ((information.st_mode & 0444) >> 2)) == 0;
}

void update_delete_file(const char *path)
{
	if (unlink(path) != 0)
		rmdir(path);
}

int update_make_directory(const char *path)
{
	return mkdir(path, 0755) == 0 || errno == EEXIST;
}

#ifdef HALO_TVOS
/* tvOS starts no other process: an Apple TV app is updated by installing it
again */
int update_launch_with(const char *path, const char *argument)
{
	(void)path;
	(void)argument;
	return 0;
}

int update_can_start(const char *path, char *reason, int reason_size)
{
	(void)path;
	(void)reason;
	(void)reason_size;
	return 1;
}

int update_start_in_place(const char *path)
{
	(void)path;
	return 0;
}

int update_start_in_place_with(const char *path, const char *argument)
{
	(void)path;
	(void)argument;
	return 0;
}
#else
/* the environment for the new game: an AppImage's AppRun put its own
mount's libraries first in LD_LIBRARY_PATH, keeping the caller's in
CHUPATHINGYCE_HOST_LD_LIBRARY_PATH, and that mount goes when this game
quits; the new game's AppRun puts its own first again. The rest is this
process's, so Steam's (SteamAppId, its overlay) and gamescope's go on */
static char **launch_environment(void)
{
	static char *environment[MAXIMUM_ENVIRONMENT + 1];
	static char library_path[8192];
	const char *host_library_path = getenv("CHUPATHINGYCE_HOST_LD_LIBRARY_PATH");
	int count = 0, index;

	if (!host_library_path)
		return environ;
	for (index = 0; environ[index] && count < MAXIMUM_ENVIRONMENT - 1; index++)
	{
		if (!strncmp(environ[index], "LD_LIBRARY_PATH=", 16) ||
			!strncmp(environ[index], "CHUPATHINGYCE_HOST_LD_LIBRARY_PATH=", 35))
		{
			continue;
		}
		environment[count++] = environ[index];
	}
	if (*host_library_path)
	{
		snprintf(library_path, sizeof(library_path), "LD_LIBRARY_PATH=%s", host_library_path);
		environment[count++] = library_path;
	}
	environment[count] = NULL;
	return environment;
}

/* one past the highest file descriptor to let go of */
static int descriptor_limit(void)
{
	struct rlimit limit;

	if (getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY && limit.rlim_cur < 65536)
		return (int)limit.rlim_cur;
	return 4096;
}

int update_launch_with(const char *path, const char *argument)
{
	/* (made before the fork) */
	char **variables = launch_environment();
	pid_t child;

	child = fork();
	if (child == 0)
	{
		/* none of this process's files (its sockets hold the game's ports) */
		int descriptor, maximum = descriptor_limit();

		for (descriptor = 3; descriptor < maximum; descriptor++)
			close(descriptor);
		{
			char *const arguments[] = { (char *)path, (char *)argument, NULL };

			execve(path, arguments, variables);
		}
		_exit(127);
	}
	return child > 0;
}

int update_can_start(const char *path, char *reason, int reason_size)
{
	struct stat information;
	struct statvfs file_system;

	if (stat(path, &information) != 0 || !S_ISREG(information.st_mode))
	{
		snprintf(reason, (size_t)reason_size, "it is not there");
		return 0;
	}
	/* (a folder on a file system mounted noexec runs nothing, whatever the
	file's mode says: a USB stick or SD card mounted so, or a /tmp) */
#ifdef ST_NOEXEC
	if (statvfs(path, &file_system) == 0 && (file_system.f_flag & ST_NOEXEC))
	{
		snprintf(reason, (size_t)reason_size, "its folder doesn't run programs (it is mounted noexec)");
		return 0;
	}
#else
	(void)file_system;
#endif
	if (access(path, X_OK) != 0)
	{
		snprintf(reason, (size_t)reason_size, "it isn't executable");
		return 0;
	}
	return 1;
}

/* the program this process becomes as it exits (update_start_in_place),
and its arguments: this one's own (/proc/self/cmdline), the first the new
program's path */
static char start_in_place_path[PATH_MAX];
static char start_in_place_command_line[8192];
static char *start_in_place_arguments[65];
/* whether a watcher waits to start the new version should this process end
without becoming it (start_in_place_watch) */
static int start_in_place_watched;
/* the argument a process of its own is started with should the execve fail
and no watcher waits (update_start_in_place_with's; none for an update) */
static const char *start_in_place_fallback_argument;

/* every file descriptor from first on closed (close-on-exec: on_exec), with
close_range where the kernel has it (Linux 5.9 and later; 5.11 for
close-on-exec), else one at a time */
static void descriptors_close_from(int first, int on_exec)
{
	int descriptor, maximum;

#ifdef __NR_close_range
	if (syscall(__NR_close_range, (unsigned int)first, ~0U, on_exec ? 4U /* CLOSE_RANGE_CLOEXEC */ : 0U) == 0)
		return;
#endif
	maximum = descriptor_limit();
	for (descriptor = first; descriptor < maximum; descriptor++)
	{
		if (on_exec)
		{
			int flags = fcntl(descriptor, F_GETFD);

			if (flags >= 0)
				fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC);
		}
		else
			close(descriptor);
	}
}

/* the new program's arguments: this process's own, by /proc/self/cmdline
(the first replaced by its path); just its path where there is none */
static void start_in_place_arguments_read(void)
{
	FILE *file = fopen("/proc/self/cmdline", "rb");
	size_t length = 0, offset;
	int count = 0;

	start_in_place_arguments[count++] = start_in_place_path;
	if (file)
	{
		length = fread(start_in_place_command_line, 1, sizeof(start_in_place_command_line) - 1, file);
		fclose(file);
	}
	start_in_place_command_line[length] = 0;
	/* (past the first, this one's own path) */
	offset = strlen(start_in_place_command_line) + 1;
	while (offset < length && count < (int)(sizeof(start_in_place_arguments) / sizeof(*start_in_place_arguments)) - 1)
	{
		start_in_place_arguments[count++] = start_in_place_command_line + offset;
		offset += strlen(start_in_place_command_line + offset) + 1;
	}
	start_in_place_arguments[count] = NULL;
}

#ifdef __linux__
/* a watcher of its own (a grandchild, so no process of the game's waits on
it, holding none of its files but one end of a pipe): it waits for the
other end, which closes as this process becomes the new version (it is
close-on-exec) or as it ends. If this process then still is, and is another
program, the new version is running, and it goes. If this process is gone
without becoming it (an exit handler crashed or killed it), the watcher
becomes the new version itself. 1 when it is watching */
static int start_in_place_watch(char **variables)
{
	struct stat old_program;
	char exe[64];
	pid_t game = getpid(), child;
	int ends[2];

	/* (the program by its file, device and inode, as /proc/<pid>/exe is
	compared below: an unchanged path is never taken for the new version) */
	if (stat("/proc/self/exe", &old_program) != 0 || pipe2(ends, O_CLOEXEC) != 0)
		return 0;
	snprintf(exe, sizeof(exe), "/proc/%ld/exe", (long)game);
	child = fork();
	if (child == 0)
	{
		static const char gone[] = "update: the game ended without starting the new version: starting it now\n";
		char byte;
		int tries;

		if (fork() != 0)
			_exit(0);
		/* (the watcher: the read end alone, as descriptor 3) */
		close(ends[1]);
		if (ends[0] != 3)
		{
			dup2(ends[0], 3);
			close(ends[0]);
		}
		fcntl(3, F_SETFD, 0);
		descriptors_close_from(4, 0);
		/* (until the write end closes: as the game becomes the new version,
		or ends) */
		for (;;)
		{
			ssize_t got = read(3, &byte, 1);

			if (got == 0 || (got < 0 && errno != EINTR))
				break;
		}
		for (tries = 0; tries < 50; tries++)
		{
			struct stat program;

			/* (gone, or a zombie, whose exe is no file: it never became the new
			version) */
			if (stat(exe, &program) != 0)
			{
				sigset_t signals;
				ssize_t written = write(2, gone, sizeof(gone) - 1);

				(void)written;
				/* (the new version starts as a new process would: no signal
				held back or ignored that the game had so) */
				signal(SIGTERM, SIG_DFL);
				signal(SIGINT, SIG_DFL);
				signal(SIGPIPE, SIG_DFL);
				sigemptyset(&signals);
				sigprocmask(SIG_SETMASK, &signals, NULL);
				execve(start_in_place_path, start_in_place_arguments, variables);
				_exit(127);
			}
			if (program.st_dev != old_program.st_dev || program.st_ino != old_program.st_ino)
				_exit(0);
			usleep(100000);
		}
		_exit(0);
	}
	close(ends[0]);
	if (child < 0)
	{
		close(ends[1]);
		return 0;
	}
	while (waitpid(child, NULL, 0) < 0 && errno == EINTR)
	{
	}
	/* (ends[1] stays open, close-on-exec, until this process becomes the new
	version or ends) */
	return 1;
}
#endif

/* the last of exit's handlers (registered before main, so after every
handler of the game's, which run first: the game list's withdrawal, UPnP's
release, the lobby's goodbye, the events' last upload): this process
becomes the new game. Its files are closed by the execve itself
(close-on-exec). If the execve fails, the watcher starts it once this
process ends (or, with no watcher, it is started as a process of its own,
as before); if a handler before this one crashes, the watcher starts it */
static void start_in_place_at_exit(void)
{
	char **variables;
	sigset_t signals;

	if (!start_in_place_path[0])
		return;
	variables = launch_environment();
	fflush(NULL);
	descriptors_close_from(3, 1);
	/* (the new game starts with no signal held back or ignored that this
	one had so, as a new process would) */
	signal(SIGTERM, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);
	sigemptyset(&signals);
	sigprocmask(SIG_SETMASK, &signals, NULL);
	execve(start_in_place_path, start_in_place_arguments, variables);
	fprintf(stderr, "update: could not start %s in place (%s): %s\n", start_in_place_path, strerror(errno),
		start_in_place_watched ? "the watcher starts it as this one ends" : "starting it as a process of its own");
	if (!start_in_place_watched)
		update_launch_with(start_in_place_path, start_in_place_fallback_argument);
}

__attribute__((constructor)) static void start_in_place_register(void)
{
	atexit(start_in_place_at_exit);
}

int update_start_in_place(const char *path)
{
	if (!path || !*path || strlen(path) >= sizeof(start_in_place_path))
		return 0;
	snprintf(start_in_place_path, sizeof(start_in_place_path), "%s", path);
	start_in_place_fallback_argument = NULL;
	start_in_place_arguments_read();
#ifdef __linux__
	start_in_place_watched = start_in_place_watch(launch_environment());
#endif
	return 1;
}

int update_start_in_place_with(const char *path, const char *argument)
{
	int count = 0;

	if (!path || !*path || strlen(path) >= sizeof(start_in_place_path) ||
		(argument && strlen(argument) >= sizeof(start_in_place_command_line)))
	{
		return 0;
	}
	snprintf(start_in_place_path, sizeof(start_in_place_path), "%s", path);
	start_in_place_arguments[count++] = start_in_place_path;
	if (argument)
	{
		snprintf(start_in_place_command_line, sizeof(start_in_place_command_line), "%s", argument);
		start_in_place_arguments[count++] = start_in_place_command_line;
	}
	start_in_place_arguments[count] = NULL;
	start_in_place_fallback_argument = argument ? start_in_place_command_line : NULL;
	/* (no watcher: it tells the new program from the old by its file, and
	this one is the same file, so it would start it again as the new one
	ends. Should the execve fail, it is started as a process of its own,
	start_in_place_at_exit) */
	start_in_place_watched = 0;
	return 1;
}
#endif

int update_launch(const char *path)
{
	return update_launch_with(path, NULL);
}
