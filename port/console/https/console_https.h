/*
CONSOLE_HTTPS.H

HTTPS for the consoles (original Xbox, Xbox 360, Wii U): Mbed TLS
(port/third_party/mbedtls, built with console_mbedtls_config.h) over the
console's own sockets, which each port gives through struct
console_https_port. The Switch is a desktop build and keeps
port/linux/src/posix_browser.c.

Only the hosts this file names are asked, and each one's certificate is
checked without the console's clock (consoles often have none set):
- stats.chupathingy.org by its public key: the certificate's key must be
  one of the pinned keys (built in, console_https_trust.c, or from the
  newest signed pin document, console_pins.h), and the certificate must
  name the host. A key it does not pin makes it ask for the pin document
  and try again; failing that, a chain to Let's Encrypt's ISRG roots is
  taken as a backstop. Dates are never checked.
- raw.githubusercontent.com (the signed legacy table's copy) by its chain:
  it must lead to one of a few pinned roots and name the host. Its dates are
  not checked.
docs/console-https.md says how the keys are rotated.

A request is HTTP/1.0 (no chunked answers), never redirected, at most the
caller's buffer in size and within its time. Everything the TLS session
needs is allocated from an arena on the calling thread's stack, so the
game's heap is never touched; a thread that calls in needs
CONSOLE_HTTPS_STACK_SIZE bytes of stack beyond its own use. One request runs
at a time (the port's lock); others wait.

The requests block: call them from a thread of their own, never the game's.
port/console/tests/console_https_test.c (tools/test_console_https.py) runs
the client on the host against a local TLS server.
*/

#ifndef __CONSOLE_HTTPS_H
#define __CONSOLE_HTTPS_H

#ifdef __cplusplus
extern "C" {
#endif

#define CONSOLE_HTTPS_SITE "stats.chupathingy.org"
#define CONSOLE_HTTPS_GITHUB "raw.githubusercontent.com"
#define CONSOLE_HTTPS_PORT_NUMBER 443
/* the pin document (console_pins.h): over plain HTTP from the console
list's host, else over HTTPS from GitHub's raw files */
#define CONSOLE_HTTPS_PINS_PLAIN_HOST "warthog.milenko.org"
#define CONSOLE_HTTPS_PINS_PLAIN_PATH "/v1/console/pins"
#define CONSOLE_HTTPS_PINS_GITHUB_PATH "/ChupathingyCE/chupathingyce/console-pins/console-pins.json"

/* the TLS session's arena, on the requesting thread's stack (the most a
handshake with a 4096-bit RSA chain and a 16 KB record buffer has been seen
to use, with room to spare: tools/test_console_https.py checks it), and
the stack a requesting thread needs for it and Mbed TLS's own frames */
#ifndef CONSOLE_HTTPS_ARENA_SIZE
#define CONSOLE_HTTPS_ARENA_SIZE (80 * 1024)
#endif
#define CONSOLE_HTTPS_STACK_SIZE (CONSOLE_HTTPS_ARENA_SIZE + 32 * 1024)

/* a port's receive and send: no byte in the time given */
#define CONSOLE_HTTPS_IO_TIMEOUT (-2)

typedef void *console_https_socket;

struct console_https_port
{
	/* a TCP connection to host's IPv4 address, port, within timeout_ms: 1
	and the socket, else 0 */
	int (*connect)(const char *host, unsigned short port, unsigned long timeout_ms, console_https_socket *socket);
	/* at most size bytes, within timeout_ms: how many were sent (at least
	1), CONSOLE_HTTPS_IO_TIMEOUT, or -1 */
	int (*send)(console_https_socket socket, const unsigned char *data, int size, unsigned long timeout_ms);
	/* at most size bytes, within timeout_ms: how many came (at least 1),
	0 when the server closed, CONSOLE_HTTPS_IO_TIMEOUT, or -1 */
	int (*receive)(console_https_socket socket, unsigned char *buffer, int size, unsigned long timeout_ms);
	void (*close)(console_https_socket socket);
	/* size bytes from the console's hardware random source: 1, or 0 if it
	has none to give (the request then fails) */
	int (*random)(unsigned char *buffer, int size);
	/* a millisecond counter (any start, wrapping) */
	unsigned long (*milliseconds)(void);
	/* one request at a time across the program's threads: NULL for none,
	when only one thread ever asks */
	void (*lock)(void);
	void (*unlock)(void);
	/* one line for the log (never an address, key or body is given); NULL
	for none */
	void (*log)(const char *text);
	/* "Warthog/1" */
	const char *user_agent;
	/* one plain HTTP GET of path from host (port 80): the whole response
	into response, at most size bytes (*received of them), within
	timeout_ms; 1 if one came. Only the pin document is asked for this way
	(it is signed). NULL: the pin document from GitHub alone */
	int (*plain_get)(const char *host, const char *path, char *response, unsigned long size,
		unsigned long *received, unsigned long timeout_ms);
	/* the newest pin document's file, beside config.toml; NULL: kept in
	memory alone */
	const char *pins_path;
};

enum
{
	CONSOLE_HTTPS_OK = 0,
	CONSOLE_HTTPS_ERROR_UNAVAILABLE,
	CONSOLE_HTTPS_ERROR_HOST,
	CONSOLE_HTTPS_ERROR_REQUEST,
	CONSOLE_HTTPS_ERROR_CONNECT,
	CONSOLE_HTTPS_ERROR_RANDOM,
	CONSOLE_HTTPS_ERROR_MEMORY,
	CONSOLE_HTTPS_ERROR_HANDSHAKE,
	CONSOLE_HTTPS_ERROR_NAME,
	CONSOLE_HTTPS_ERROR_KEY,
	CONSOLE_HTTPS_ERROR_SEND,
	CONSOLE_HTTPS_ERROR_RECEIVE,
	CONSOLE_HTTPS_ERROR_TIMEOUT,
	CONSOLE_HTTPS_ERROR_TOO_LARGE,
	CONSOLE_HTTPS_ERROR_RESPONSE,
	NUMBER_OF_CONSOLE_HTTPS_ERRORS
};

/* the port's hooks, kept (port must outlive every request); before it,
every request fails with CONSOLE_HTTPS_ERROR_UNAVAILABLE */
void console_https_initialize(const struct console_https_port *port);
int console_https_available(void);
const char *console_https_error_string(int error);

/* GET (body NULL) or POST body (content_type, "application/json" if NULL)
https://host/path: the whole response, headers and all, into response (at
most size bytes; *received of them), within timeout_ms. host is
CONSOLE_HTTPS_SITE or CONSOLE_HTTPS_GITHUB, else CONSOLE_HTTPS_ERROR_HOST */
int console_https_request(const char *host, const char *path, const char *body, const char *content_type,
	char *response, unsigned long size, unsigned long *received, unsigned long timeout_ms);

/* the same, the answer's status and body (NUL terminated) in reply: the
whole answer, headers and all, must fit in reply_size - 1 bytes, else
CONSOLE_HTTPS_ERROR_TOO_LARGE */
int console_https_call(const char *host, const char *path, const char *body, int *status, char *reply,
	int reply_size, unsigned long timeout_ms);

/* the console link's https_request (port/console/link/console_link.h): a
JSON call to CONSOLE_HTTPS_SITE within 15 seconds; 1 if an answer came */
int console_https_site_request(const char *path, const char *body, int *status, char *reply, int reply_size);

/* an HTTP/1.x response, size bytes (no terminator needed): its status, and
its body (pointing into data) as Content-Length says, else to the end; 1, or
0 if it is not whole */
int console_https_parse_response(const char *data, unsigned long size, int *status, const char **body,
	unsigned long *body_size);

#ifdef CONSOLE_HTTPS_TESTS
/* (the host test's: its own hosts' pins and roots in place of the real
ones, and the most of the arena the last request used) */
struct console_https_trust;
void console_https_trust_for_tests(const struct console_https_trust *trust, int count);
unsigned long console_https_arena_peak(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
