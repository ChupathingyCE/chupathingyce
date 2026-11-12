/*
STEAM_NET_FUZZ.C

libFuzzer's entry for the Steam content client's own HTTP and WebSocket
readers (port/linux/src/steam_content/sc_net.c), which take a content
server's and the CM's bytes before anything else checks them (built with
SC_FUZZING by tools/test_steam_content.py). The input is what the other end
sends, over a local socket pair; the first byte picks the reader: 0, an
HTTP response (status, headers, chunked or sized body); 1, a WebSocket
opening answer and then frames; 2, WebSocket frames alone.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/

#include "../src/steam_content/sc_internal.h"
#include "../src/steam_content/sc_net.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef MSG_NOSIGNAL
#define FEED_FLAGS MSG_NOSIGNAL
#else
#define FEED_FLAGS 0
#endif

struct sc_connection *sc_fuzz_connection(int fd);
int sc_fuzz_http(struct sc_connection *connection, unsigned char **body, size_t *length, size_t maximum);

struct feed
{
	int fd;
	const uint8_t *data;
	size_t size;
};

/* the other end: the input written, then the socket shut for writing (the
reader sees the end), what the reader sends read and dropped */
static void *feed_thread(void *context)
{
	struct feed *feed = (struct feed *)context;
	size_t at = 0;

	while (at < feed->size)
	{
		ssize_t sent = send(feed->fd, feed->data + at, feed->size - at, FEED_FLAGS);

		if (sent <= 0)
			break;
		at += (size_t)sent;
	}
	shutdown(feed->fd, SHUT_WR);
	{
		char sink[4096];

		while (recv(feed->fd, sink, sizeof(sink), 0) > 0)
			;
	}
	return NULL;
}

int LLVMFuzzerTestOneInput(const uint8_t *input, size_t size)
{
	int pair[2];
	struct feed feed;
	pthread_t thread;
	struct sc_connection *connection;
	unsigned char *message = NULL;
	size_t length = 0;
	char error[160];
	int frames;

	if (!size || socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0)
		return 0;
#ifdef SO_NOSIGPIPE
	{
		int set = 1;

		setsockopt(pair[0], SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(set));
		setsockopt(pair[1], SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(set));
	}
#endif
	feed.fd = pair[1];
	feed.data = input + 1;
	feed.size = size - 1;
	if (pthread_create(&thread, NULL, feed_thread, &feed) != 0)
	{
		close(pair[0]);
		close(pair[1]);
		return 0;
	}
	connection = sc_fuzz_connection(pair[0]);
	if (connection)
	{
		switch (input[0] % 3)
		{
		case 0:
			sc_fuzz_http(connection, &message, &length, 1 << 20);
			free(message);
			break;
		case 1:
			if (sc_websocket_open(connection, "fuzz", "/cmsocket/", error, sizeof(error)) != 1)
				break;
			/* fall through */
		default:
			for (frames = 0; frames < 64; frames++)
			{
				message = NULL;
				if (sc_websocket_receive(connection, &message, &length, 200) != 1)
					break;
				free(message);
			}
			break;
		}
		/* (closed here: shut down so the feed's reading ends) */
		shutdown(pair[0], SHUT_RDWR);
		sc_close(connection);
	}
	else
	{
		shutdown(pair[0], SHUT_RDWR);
		close(pair[0]);
	}
	pthread_join(thread, NULL);
	close(pair[1]);
	return 0;
}
