/*
TLS_TEST.C

A driver for control_tls.c (tools/test_server_tls.py runs it):

  tls_test start <data> [<cert> <key>] [<name>]
      control_tls_start once: "fingerprint <fp>" and "made <0|1>", or
      "error <why>" (exit 1)
  tls_test serve <data> [<cert> <key>]
      control_tls_start, then a nonblocking poll loop on 127.0.0.1 (a port
      the system gives: "port <n>"), as the control thread runs one: each
      connection's handshake, its request read whole, a response with the
      protocol agreed as its body ("served <version>"), close_notify. A
      plain HTTP request is answered plainly ("plain"). Standard input's
      lines: "reload <seconds>" (control_tls_reload_if_changed at that
      time: "reload <result> <fingerprint>"), "quit".
*/

#include "control_tls.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum
{
	CONNECTIONS = 8,
	REQUEST_SIZE = 4096,
};

enum
{
	FREE,
	HANDSHAKE,
	READING,
	WRITING,
	CLOSING,
	PLAIN,
};

struct connection
{
	int state;
	int fd;
	struct control_tls_connection *tls;
	char request[REQUEST_SIZE];
	size_t length;
	char response[512];
	size_t response_length;
	size_t sent;
};

static struct connection connections[CONNECTIONS];

static void finish(struct connection *connection)
{
	control_tls_free(connection->tls);
	close(connection->fd);
	memset(connection, 0, sizeof(*connection));
}

static void step(struct connection *connection)
{
	long result;

	for (;;)
	{
		switch (connection->state)
		{
		case HANDSHAKE:
			result = control_tls_handshake(connection->tls);
			if (result == CONTROL_TLS_NOT_TLS)
			{
				/* (the request read before the answer: a socket closed with
				bytes unread resets the connection, and the answer is lost) */
				while (recv(connection->fd, connection->request, sizeof(connection->request), 0) > 0)
					;
				connection->response_length = (size_t)snprintf(connection->response, sizeof(connection->response),
					"HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: 30\r\n"
					"\r\nThis port speaks HTTPS only.\n");
				connection->state = PLAIN;
				continue;
			}
			if (result != CONTROL_TLS_DONE)
			{
				if (result != CONTROL_TLS_WANT_READ && result != CONTROL_TLS_WANT_WRITE)
				{
					printf("handshake failed %ld\n", result);
					fflush(stdout);
					finish(connection);
				}
				return;
			}
			connection->state = READING;
			continue;
		case READING:
			result = control_tls_read(connection->tls, connection->request + connection->length,
				sizeof(connection->request) - 1 - connection->length);
			if (result < 0)
			{
				if (result != CONTROL_TLS_WANT_READ && result != CONTROL_TLS_WANT_WRITE)
					finish(connection);
				return;
			}
			connection->length += (size_t)result;
			connection->request[connection->length] = 0;
			if (!strstr(connection->request, "\r\n\r\n"))
			{
				if (connection->length >= sizeof(connection->request) - 1)
				{
					finish(connection);
					return;
				}
				continue;
			}
			{
				const char *version = control_tls_version(connection->tls);

				connection->response_length = (size_t)snprintf(connection->response, sizeof(connection->response),
					"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: %lu\r\n\r\n%s",
					(unsigned long)strlen(version), version);
				printf("served %s\n", version);
				fflush(stdout);
			}
			connection->state = WRITING;
			continue;
		case WRITING:
			result = control_tls_write(connection->tls, connection->response + connection->sent,
				connection->response_length - connection->sent);
			if (result < 0)
			{
				if (result != CONTROL_TLS_WANT_READ && result != CONTROL_TLS_WANT_WRITE)
					finish(connection);
				return;
			}
			connection->sent += (size_t)result;
			if (connection->sent < connection->response_length)
				continue;
			connection->state = CLOSING;
			continue;
		case CLOSING:
			result = control_tls_close_notify(connection->tls);
			if (result == CONTROL_TLS_WANT_READ || result == CONTROL_TLS_WANT_WRITE)
				return;
			finish(connection);
			return;
		case PLAIN:
		{
			ssize_t count = send(connection->fd, connection->response + connection->sent,
				connection->response_length - connection->sent, 0);

			if (count < 0 && (errno == EAGAIN || errno == EINTR))
				return;
			if (count > 0)
				connection->sent += (size_t)count;
			if (count <= 0 || connection->sent >= connection->response_length)
			{
				printf("plain\n");
				fflush(stdout);
				shutdown(connection->fd, SHUT_WR);
				finish(connection);
			}
			return;
		}
		default:
			return;
		}
	}
}

static int serve(void)
{
	struct sockaddr_in address;
	socklen_t length = sizeof(address);
	int listener = socket(AF_INET, SOCK_STREAM, 0);
	int one = 1;
	char line[256];
	size_t line_length = 0;

	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (listener < 0 || bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, 8) ||
		getsockname(listener, (struct sockaddr *)&address, &length))
	{
		printf("error cannot listen\n");
		return 1;
	}
	fcntl(listener, F_SETFL, fcntl(listener, F_GETFL) | O_NONBLOCK);
	printf("port %d\n", ntohs(address.sin_port));
	fflush(stdout);
	for (;;)
	{
		struct pollfd fds[CONNECTIONS + 2];
		int map[CONNECTIONS + 2];
		int count = 0;
		int index;

		fds[count].fd = STDIN_FILENO;
		fds[count].events = POLLIN;
		map[count++] = -1;
		fds[count].fd = listener;
		fds[count].events = POLLIN;
		map[count++] = -2;
		for (index = 0; index < CONNECTIONS; index++)
		{
			if (connections[index].state == FREE)
				continue;
			fds[count].fd = connections[index].fd;
			fds[count].events = connections[index].state == PLAIN ? POLLOUT :
				control_tls_poll_events(connections[index].tls);
			map[count++] = index;
		}
		if (poll(fds, (nfds_t)count, 1000) < 0 && errno != EINTR)
			return 1;
		for (index = 0; index < count; index++)
		{
			if (!fds[index].revents)
				continue;
			if (map[index] == -1)
			{
				char character;
				ssize_t got = read(STDIN_FILENO, &character, 1);

				if (got <= 0)
					return 0;
				if (character != '\n')
				{
					if (line_length + 1 < sizeof(line))
						line[line_length++] = character;
					continue;
				}
				line[line_length] = 0;
				line_length = 0;
				if (!strcmp(line, "quit"))
					return 0;
				if (!strncmp(line, "reload ", 7))
				{
					char error[256];
					int result = control_tls_reload_if_changed(atoll(line + 7), error, sizeof(error));

					printf("reload %d %s%s%s\n", result, control_tls_fingerprint(), error[0] ? " " : "", error);
					fflush(stdout);
				}
			}
			else if (map[index] == -2)
			{
				int fd = accept(listener, NULL, NULL);
				int slot;

				if (fd < 0)
					continue;
				for (slot = 0; slot < CONNECTIONS && connections[slot].state != FREE; slot++)
					;
				fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
				if (slot >= CONNECTIONS || !(connections[slot].tls = control_tls_accept(fd)))
				{
					close(fd);
					continue;
				}
				connections[slot].fd = fd;
				connections[slot].state = HANDSHAKE;
				step(&connections[slot]);
			}
			else
				step(&connections[map[index]]);
		}
		/* (bytes already decrypted: poll would not wake for them) */
		for (index = 0; index < CONNECTIONS; index++)
		{
			if (connections[index].state == READING && control_tls_pending(connections[index].tls))
				step(&connections[index]);
		}
	}
}

int main(int argc, char **argv)
{
	char fingerprint[CONTROL_TLS_FINGERPRINT_SIZE];
	char error[256];
	const char *cert = NULL, *key = NULL, *name = NULL;
	int made = 0;
	int ok;

	signal(SIGPIPE, SIG_IGN);
	if (argc < 3 || (strcmp(argv[1], "start") && strcmp(argv[1], "serve")))
	{
		fprintf(stderr, "usage: tls_test start|serve <data> [<cert> <key>] [<name>]\n");
		return 2;
	}
	if (argc >= 5)
	{
		cert = argv[3][0] ? argv[3] : NULL;
		key = argv[4][0] ? argv[4] : NULL;
	}
	if (argc >= 6)
		name = argv[5];
	ok = control_tls_start(argv[2], cert, key, name, fingerprint, sizeof(fingerprint), &made, error, sizeof(error));
	if (!ok)
	{
		printf("error %s\n", error);
		return 1;
	}
	if (strcmp(fingerprint, control_tls_fingerprint()) || strlen(fingerprint) != 95)
	{
		printf("error the fingerprint is not right\n");
		return 1;
	}
	printf("fingerprint %s\nmade %d\nself_signed %d\n", fingerprint, made, control_tls_self_signed());
	fflush(stdout);
	if (!strcmp(argv[1], "serve"))
	{
		int result = serve();
		int index;

		for (index = 0; index < CONNECTIONS; index++)
		{
			if (connections[index].state != FREE)
				finish(&connections[index]);
		}
		control_tls_stop();
		return result;
	}
	control_tls_stop();
	return 0;
}
