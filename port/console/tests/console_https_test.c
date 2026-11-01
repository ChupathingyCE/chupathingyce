/*
CONSOLE_HTTPS_TEST.C

The consoles' HTTPS client (port/console/https/console_https.c) on the host,
built with the consoles' Mbed TLS configuration, for
tools/test_console_https.py. Its port is BSD sockets that connect every
host to the loopback address and the test server's port, and the host's
random numbers; the trust is the test's own pins and roots in place of the
real ones (the real ones' host names kept, so the name check is the real
one).

  console_https_test PORT HOST [--pin HEX] [--root FILE.der] [--path PATH]
      [--body JSON] [--no-random] [--no-port] [--size BYTES] [--plain-port PORT]
      [--pins FILE] [--live] [--parse]

--plain-port is where the pin document is asked for over plain HTTP, and
--pins the file the newest one is kept in. --live asks the real host (PORT unused) with the real pins and roots. It
prints the result's number and words, the HTTP status, the arena's peak and
the body. --parse runs the response parser's own checks instead.
*/

#include "console_https.h"
#include "console_https_trust.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static unsigned short server_port;
static int random_works = 1;
/* (--live: the real host, by its name, and the real pins and roots) */
static int live;
/* (--plain-port: the plain HTTP server the pin document comes from) */
static unsigned short plain_port;

static unsigned long now_ms(void)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	return (unsigned long)now.tv_sec * 1000ul + (unsigned long)now.tv_usec / 1000ul;
}

static int wait_ready(int socket_value, int write, unsigned long timeout_ms)
{
	fd_set set;
	struct timeval wait;

	FD_ZERO(&set);
	FD_SET(socket_value, &set);
	wait.tv_sec = (long)(timeout_ms / 1000);
	wait.tv_usec = (long)(timeout_ms % 1000) * 1000;
	return select(socket_value + 1, write ? NULL : &set, write ? &set : NULL, NULL, &wait) > 0;
}

static int test_connect(const char *host, unsigned short port, unsigned long timeout_ms, console_https_socket *out)
{
	struct sockaddr_in target;
	int socket_value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	(void)timeout_ms;
	if (socket_value < 0)
		return 0;
	memset(&target, 0, sizeof(target));
	target.sin_family = AF_INET;
	target.sin_port = htons(live ? port : server_port);
	target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (live)
	{
		struct addrinfo hints, *found = NULL;

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		if (getaddrinfo(host, NULL, &hints, &found) || !found)
		{
			close(socket_value);
			return 0;
		}
		target.sin_addr = ((struct sockaddr_in *)found->ai_addr)->sin_addr;
		freeaddrinfo(found);
	}
	if (connect(socket_value, (struct sockaddr *)&target, sizeof(target)) != 0)
	{
		close(socket_value);
		return 0;
	}
	*out = (console_https_socket)(long)socket_value;
	return 1;
}

static int test_send(console_https_socket socket_handle, const unsigned char *data, int size, unsigned long timeout_ms)
{
	int socket_value = (int)(long)socket_handle;
	ssize_t count;

	if (!wait_ready(socket_value, 1, timeout_ms))
		return CONSOLE_HTTPS_IO_TIMEOUT;
	count = send(socket_value, data, (size_t)size, 0);
	return count > 0 ? (int)count : -1;
}

static int test_receive(console_https_socket socket_handle, unsigned char *buffer, int size, unsigned long timeout_ms)
{
	int socket_value = (int)(long)socket_handle;
	ssize_t count;

	if (!wait_ready(socket_value, 0, timeout_ms))
		return CONSOLE_HTTPS_IO_TIMEOUT;
	count = recv(socket_value, buffer, (size_t)size, 0);
	if (count < 0)
		return errno == ECONNRESET ? 0 : -1;
	return (int)count;
}

static void test_close(console_https_socket socket_handle)
{
	close((int)(long)socket_handle);
}

static int test_random(unsigned char *buffer, int size)
{
	FILE *file;
	int ok;

	if (!random_works)
		return 0;
	file = fopen("/dev/urandom", "rb");
	if (!file)
		return 0;
	ok = fread(buffer, 1, (size_t)size, file) == (size_t)size;
	fclose(file);
	return ok;
}

/* the pin document's plain HTTP GET: from the loopback address's plain_port */
static int test_plain_get(const char *host, const char *path, char *response, unsigned long size,
	unsigned long *received, unsigned long timeout_ms)
{
	struct sockaddr_in target;
	char request[512];
	int socket_value, length;

	*received = 0;
	if (!plain_port)
		return 0;
	length = snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path,
		host);
	socket_value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket_value < 0)
		return 0;
	memset(&target, 0, sizeof(target));
	target.sin_family = AF_INET;
	target.sin_port = htons(plain_port);
	target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(socket_value, (struct sockaddr *)&target, sizeof(target)) != 0 ||
		send(socket_value, request, (size_t)length, 0) != length)
	{
		close(socket_value);
		return 0;
	}
	while (*received < size && wait_ready(socket_value, 0, timeout_ms))
	{
		ssize_t count = recv(socket_value, response + *received, size - *received, 0);

		if (count <= 0)
			break;
		*received += (unsigned long)count;
	}
	close(socket_value);
	return *received > 0;
}

static void test_log(const char *text)
{
	fprintf(stderr, "%s\n", text);
}

static struct console_https_port test_port =
{
	test_connect, test_send, test_receive, test_close, test_random, now_ms, NULL, NULL, test_log, "Warthog/1 (test)",
	test_plain_get, NULL,
};

static int hex_digit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int check(int condition, const char *what)
{
	if (!condition)
		printf("FAIL: %s\n", what);
	return condition ? 0 : 1;
}

/* the response parser alone */
static int parse_checks(void)
{
	static const char whole[] = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello and more";
	static const char short_body[] = "HTTP/1.0 200 OK\r\ncontent-length: 50\r\n\r\nhello";
	static const char no_length[] = "HTTP/1.0 404 Not Found\n\nnothing";
	static const char not_http[] = "SSH-2.0-OpenSSH\r\n\r\n";
	static const char no_end[] = "HTTP/1.0 200 OK\r\nContent-Length: 5\r\n";
	const char *body;
	unsigned long size;
	int status, failures = 0;

	failures += check(console_https_parse_response(whole, sizeof(whole) - 1, &status, &body, &size) && status == 200 &&
		size == 5 && !memcmp(body, "hello", 5), "a whole response");
	failures += check(!console_https_parse_response(short_body, sizeof(short_body) - 1, &status, &body, &size),
		"a body cut short");
	failures += check(console_https_parse_response(no_length, sizeof(no_length) - 1, &status, &body, &size) &&
		status == 404 && size == 7, "a body to the end");
	failures += check(!console_https_parse_response(not_http, sizeof(not_http) - 1, &status, &body, &size), "not HTTP");
	failures += check(!console_https_parse_response(no_end, sizeof(no_end) - 1, &status, &body, &size),
		"headers without their end");
	failures += check(!strcmp(console_https_error_string(CONSOLE_HTTPS_ERROR_KEY),
		"the server's key is not one this console trusts") && !strcmp(console_https_error_string(-1), "unknown error"),
		"the errors' words");
	printf("parse: %s\n", failures ? "failed" : "ok");
	return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
	static unsigned char pins[8][CONSOLE_HTTPS_PIN_SIZE];
	static struct console_https_root roots[8];
	static unsigned char root_data[8][4096];
	struct console_https_trust trust[2];
	const char *host, *path = "/index.txt", *body = NULL;
	int pin_count = 0, root_count = 0;
	int status = 0, result, index;
	int reply_size = 4096;
	int port_given = 1;
	char *reply;

	if (argc == 2 && !strcmp(argv[1], "--parse"))
		return parse_checks();
	if (argc < 3)
	{
		fprintf(stderr, "usage: console_https_test PORT HOST [options]\n");
		return 2;
	}
	server_port = (unsigned short)atoi(argv[1]);
	host = argv[2];
	for (index = 3; index < argc; index++)
	{
		if (!strcmp(argv[index], "--pin") && index + 1 < argc && pin_count < 8)
		{
			const char *hex = argv[++index];
			int at;

			if (strlen(hex) != 64)
				return 2;
			for (at = 0; at < 32; at++)
				pins[pin_count][at] = (unsigned char)(hex_digit(hex[at * 2]) << 4 | hex_digit(hex[at * 2 + 1]));
			pin_count++;
		}
		else if (!strcmp(argv[index], "--root") && index + 1 < argc && root_count < 8)
		{
			FILE *file = fopen(argv[++index], "rb");

			if (!file)
				return 2;
			roots[root_count].name = argv[index];
			roots[root_count].der = root_data[root_count];
			roots[root_count].size = (unsigned long)fread(root_data[root_count], 1, sizeof(root_data[0]), file);
			fclose(file);
			root_count++;
		}
		else if (!strcmp(argv[index], "--path") && index + 1 < argc)
			path = argv[++index];
		else if (!strcmp(argv[index], "--body") && index + 1 < argc)
			body = argv[++index];
		else if (!strcmp(argv[index], "--size") && index + 1 < argc)
			reply_size = atoi(argv[++index]);
		else if (!strcmp(argv[index], "--no-random"))
			random_works = 0;
		else if (!strcmp(argv[index], "--plain-port") && index + 1 < argc)
			plain_port = (unsigned short)atoi(argv[++index]);
		else if (!strcmp(argv[index], "--pins") && index + 1 < argc)
			test_port.pins_path = argv[++index];
		else if (!strcmp(argv[index], "--live"))
			live = 1;
		else if (!strcmp(argv[index], "--no-port"))
			port_given = 0;
		else
			return 2;
	}

	/* (the real hosts' names, the test's keys and roots) */
	trust[0].host = CONSOLE_HTTPS_SITE;
	trust[0].pins = (const unsigned char (*)[CONSOLE_HTTPS_PIN_SIZE])pins;
	trust[0].pin_count = pin_count;
	trust[0].roots = roots;
	trust[0].root_count = root_count;
	trust[1] = trust[0];
	trust[1].host = CONSOLE_HTTPS_GITHUB;
	trust[1].pins = NULL;
	trust[1].pin_count = 0;
	if (!live)
		console_https_trust_for_tests(trust, 2);

	if (port_given)
		console_https_initialize(&test_port);
	reply = malloc((size_t)reply_size + 1);
	result = console_https_call(host, path, body, &status, reply, reply_size, 20000);
	printf("result %d %s\n", result, console_https_error_string(result));
	printf("status %d\n", status);
	printf("peak %lu\n", console_https_arena_peak());
	printf("body %s\n", result == CONSOLE_HTTPS_OK ? reply : "");
	free(reply);
	return 0;
}
