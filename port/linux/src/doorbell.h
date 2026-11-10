/*
DOORBELL.H

Delta's doorbell: a ring from the site when something changed for this
copy's profile (an invite, a friend request, a party, a friend coming
online), so the Guide asks for its friends at once instead of on the next
poll. The site publishes a ring (one byte: what kind, never who or what) to
this copy's own MQTT topic, doorbell/<32 hex>, on doorbell.chupathingy.org
and mqtt.chupathingy.org; the copy listens on one of them over TLS. The
topic comes from the site's friends state (a keyed hash the site keeps: it
cannot be guessed, and the brokers let a client read only the topic of its
own client ID). docs/delta.md, "The doorbell".

This unit is the protocol and the schedule only (MQTT 3.1.1, QoS 0, a
subscription and pings), over a transport given to it: no sockets, no TLS,
no threads, no game. browser.c runs it on a thread of its own over
posix_browser_stream_* (browser_http.h). Without a ring the polls go on as
before, so a broker that is down costs nothing but the speed.
*/

#ifndef __DOORBELL_H
#define __DOORBELL_H

enum
{
	/* a doorbell's name: 32 of 0-9 and a-f, and the NUL */
	DOORBELL_ID_SIZE = 33,
	DOORBELL_MAXIMUM_BROKERS = 4,
	DOORBELL_HOST_SIZE = 64,
	/* the most a packet from the broker may be (a ring is about 50 bytes) */
	DOORBELL_MAXIMUM_PACKET = 512,

	/* MQTT's keep alive, and a ping when this long went by without sending */
	DOORBELL_KEEP_ALIVE_SECONDS = 60,
	DOORBELL_PING_AFTER = 45000,
	/* how long the broker has to answer a connect, a subscribe or a ping */
	DOORBELL_ANSWER_WAIT = 15000,
	/* after a failure: the next broker this soon, and when all of them
	failed, this long, doubling each round up to DOORBELL_RETRY_MOST (and a
	quarter more at most, by the seed, so a broker's restart is not met by
	every copy at once) */
	DOORBELL_NEXT_BROKER_WAIT = 1000,
	DOORBELL_RETRY_FIRST = 5000,
	DOORBELL_RETRY_MOST = 5 * 60000,
};

/* rings' kinds (the payload's first byte; anything else is a ring all the
same) */
enum
{
	_doorbell_ring_any = 0,
	_doorbell_ring_friends = 1,
	_doorbell_ring_invite = 2,
	_doorbell_ring_party = 3,
	_doorbell_ring_presence = 4,
};

/* a connection to a broker: open gives its handle (> 0), or 0 with why in
error; write sends all of data (1) or fails (0); read gives up to size
bytes (their count), 0 when none came within wait milliseconds, negative
when the connection is over; close ends it */
struct doorbell_transport
{
	void *context;
	int (*open)(void *context, const char *host, const char *port, char *error, int error_size);
	int (*write)(void *context, int handle, const unsigned char *data, int size);
	int (*read)(void *context, int handle, unsigned char *buffer, int size, int wait);
	void (*close)(void *context, int handle);
};

enum
{
	_doorbell_idle,
	/* waiting for CONNACK, then SUBACK */
	_doorbell_connecting,
	_doorbell_subscribing,
	_doorbell_listening,
};

struct doorbell
{
	const struct doorbell_transport *transport;
	char hosts[DOORBELL_MAXIMUM_BROKERS][DOORBELL_HOST_SIZE];
	char ports[DOORBELL_MAXIMUM_BROKERS][8];
	int broker_count;
	unsigned long seed;

	/* the doorbell listened for (empty: none), and the one connected */
	char wanted[DOORBELL_ID_SIZE];
	char listening[DOORBELL_ID_SIZE];

	int state;
	int handle;
	int broker;
	unsigned long state_time;
	unsigned long sent_time;
	int ping_out;
	/* failures since the last subscription that held, and when to try again */
	int failures;
	unsigned long retry_time;
	int retrying;
	/* the broker refused the subscription (a doorbell the site no longer
	knows): no more tries until another doorbell is wanted */
	int refused;

	unsigned char input[DOORBELL_MAXIMUM_PACKET];
	int input_size;
	/* what happened, for the log (doorbell_take_news), and at which broker */
	int news;
	int news_broker;
	/* the kind of the last ring */
	int last_kind;
};

/* what doorbell_step saw happen, for the log: which broker, never what
the doorbell is */
enum
{
	_doorbell_news_none = 0,
	_doorbell_news_listening = 1 << 0,
	_doorbell_news_lost = 1 << 1,
	_doorbell_news_failed = 1 << 2,
	_doorbell_news_refused = 1 << 3,
};

/* brokers: "host:port" each, tried in order (failing over to the next);
seed: any number, different on each copy (the retries' spread) */
void doorbell_init(struct doorbell *doorbell, const struct doorbell_transport *transport,
	const char *const *brokers, int broker_count, unsigned long seed);
/* the doorbell to listen for (32 of 0-9 and a-f), or NULL / "" for none:
the connection is closed at the next step. Returns 1 if it changed */
int doorbell_want(struct doorbell *doorbell, const char *id);
/* one turn: connects, subscribes, pings, reads for up to wait
milliseconds; the rings heard (0 most of the time). now: milliseconds,
any start, wrapping */
int doorbell_step(struct doorbell *doorbell, unsigned long now, int wait);
/* the connection closed (at exit) */
void doorbell_stop(struct doorbell *doorbell);
/* the news since the last call (_doorbell_news_*), and the broker's host
it is about */
int doorbell_take_news(struct doorbell *doorbell, const char **host);
/* whether text is a doorbell's name */
int doorbell_id_valid(const char *text);

/* the packets (for the tests): their size in buffer, 0 when they do not fit */
int doorbell_connect_packet(unsigned char *buffer, int size, const char *client_id, int keep_alive);
int doorbell_subscribe_packet(unsigned char *buffer, int size, unsigned short identifier, const char *topic);
/* a packet's whole size from its start (its fixed header and the rest), 0
while more bytes are needed, -1 if it is not one */
int doorbell_packet_size(const unsigned char *data, int size);

#endif
