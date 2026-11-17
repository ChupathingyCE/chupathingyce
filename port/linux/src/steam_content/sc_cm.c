/*
SC_CM.C

The Steam CM (connection manager) over a TLS WebSocket (sc_internal.h):
wss://<endpoint>/cmsocket/, one binary message per CM message, no channel
encryption of its own (TLS does it). A message is a little-endian u32
(its EMsg with the protobuf bit 0x80000000), a u32 header length, a
CMsgProtoBufHeader and the body. Multi (EMsg 1) carries several, gzipped
when size_unzipped is set, each a u32 length and the message.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h): a console builds this to nothing */

#include "sc_internal.h"
#include "sc_net.h"
#include "sc_proto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zlib.h"

#define PROTO_MASK 0x80000000u
/* individual account, desktop instance, public universe, account 0: the steamid before a logon */
#define LOGON_STEAMID ((1ULL << 56) | (1ULL << 52) | (1ULL << 32))
#define CM_LIST_HOST "api.steampowered.com"
#define CM_LIST_PATH "/ISteamDirectory/GetCMListForConnect/v1/?cellid=0&format=json"
#define CLIENT_NAME "Chupathingy"

enum
{
	CM_TRIES = 5,
	CALL_TIMEOUT = 30000,
	/* header fields */
	HEADER_STEAMID = 1,
	HEADER_SESSION = 2,
	HEADER_JOB_SOURCE = 10,
	HEADER_JOB_TARGET = 11,
	HEADER_JOB_NAME = 12,
	HEADER_ERESULT = 13,
	HEADER_ERROR = 14,
};

static uint32_t read_u32(const unsigned char *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void write_u32(unsigned char *bytes, uint32_t value)
{
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
}

/* the "endpoint" of each websockets server in the Web API's list, in its order */
static int cm_list(char (*endpoints)[128], int maximum, const volatile int *cancel)
{
	struct sc_http http;
	unsigned char *body;
	size_t length;
	char error[160];
	const char *at;
	int status, count = 0;

	memset(&http, 0, sizeof(http));
	http.cancel = cancel;
	status = sc_http_request(&http, CM_LIST_HOST, 1, "GET", CM_LIST_PATH, NULL, NULL, 0, &body, &length, 1 << 20, error,
		sizeof(error));
	sc_http_close(&http);
	if (status != 200)
	{
		sc_log("Steam: the CM list was not given (%s%d)", error[0] ? error : "HTTP ", error[0] ? 0 : status);
		free(body);
		return status ? -1 : -2;
	}
	for (at = (const char *)body; count < maximum && (at = strchr(at, '{')) != NULL; at++)
	{
		const char *end = strchr(at + 1, '}'), *endpoint, *type;
		size_t span;

		if (!end)
			break;
		endpoint = strstr(at, "\"endpoint\"");
		type = strstr(at, "\"websockets\"");
		if (endpoint && endpoint < end && type && type < end)
		{
			endpoint = strchr(endpoint + 10, '"');
			if (endpoint && endpoint < end)
			{
				span = strcspn(endpoint + 1, "\"");
				if (span > 0 && span < sizeof(endpoints[0]))
				{
					memcpy(endpoints[count], endpoint + 1, span);
					endpoints[count][span] = 0;
					count++;
				}
			}
		}
	}
	free(body);
	return count;
}

int sc_cm_send(struct sc_cm *cm, unsigned emsg, const void *body, size_t length, const char *job_name, uint64_t job)
{
	unsigned char header[512];
	struct pb_writer writer;
	unsigned char *packet;
	int ok;

	pb_writer_init(&writer, header, sizeof(header));
	pb_put_fixed64(&writer, HEADER_STEAMID, cm->steamid);
	pb_put_varint(&writer, HEADER_SESSION, (uint64_t)(uint32_t)cm->session);
	if (job)
		pb_put_fixed64(&writer, HEADER_JOB_SOURCE, job);
	if (job_name)
		pb_put_string(&writer, HEADER_JOB_NAME, job_name);
	if (writer.failed || !cm->connection)
		return 0;
	packet = (unsigned char *)malloc(8 + writer.length + length);
	if (!packet)
		return 0;
	write_u32(packet, emsg | PROTO_MASK);
	write_u32(packet + 4, (uint32_t)writer.length);
	memcpy(packet + 8, header, writer.length);
	if (length)
		memcpy(packet + 8 + writer.length, body, length);
	ok = sc_websocket_send(cm->connection, packet, 8 + writer.length + length);
	free(packet);
	cm->last_send = sc_milliseconds();
	return ok;
}

int sc_cm_connect(struct sc_cm *cm, const volatile int *cancel)
{
	char endpoints[CM_TRIES][128];
	int count, index;

	memset(cm, 0, sizeof(*cm));
	cm->cancel = cancel;
	cm->steamid = LOGON_STEAMID;
	count = cm_list(endpoints, CM_TRIES, cancel);
	if (count <= 0)
		return count == -2 ? STEAM_CONTENT_OFFLINE : STEAM_CONTENT_FAILED;
	for (index = 0; index < count; index++)
	{
		char host[128], error[192], *colon;
		int port = 443;

		if (cancel && *cancel)
			return STEAM_CONTENT_CANCELLED;
		snprintf(host, sizeof(host), "%s", endpoints[index]);
		if ((colon = strrchr(host, ':')) != NULL)
		{
			port = atoi(colon + 1);
			*colon = 0;
		}
		cm->connection = sc_connect(host, port, 1, 10000, cancel, error, sizeof(error));
		if (cm->connection && sc_websocket_open(cm->connection, endpoints[index], "/cmsocket/", error, sizeof(error)))
		{
			unsigned char hello[16];
			struct pb_writer writer;

			pb_writer_init(&writer, hello, sizeof(hello));
			pb_put_varint(&writer, 1, SC_PROTOCOL_VERSION);
			if (sc_cm_send(cm, SC_EMSG_HELLO, hello, writer.length, NULL, 0))
				return STEAM_CONTENT_OK;
		}
		sc_log("Steam: CM %d of %d refused (%s)", index + 1, count, error);
		sc_close(cm->connection);
		cm->connection = NULL;
	}
	return STEAM_CONTENT_OFFLINE;
}

void sc_cm_close(struct sc_cm *cm)
{
	if (cm->connection && cm->session)
		sc_cm_send(cm, SC_EMSG_LOG_OFF, NULL, 0, NULL, 0);
	sc_close(cm->connection);
	cm->connection = NULL;
	free(cm->message);
	free(cm->multi);
	cm->message = cm->multi = NULL;
	cm->multi_length = cm->multi_at = 0;
}

/* a Multi's payload unpacked into cm->multi: 1, else 0 */
static int multi_take(struct sc_cm *cm, const unsigned char *body, size_t length)
{
	const unsigned char *payload;
	size_t payload_length;
	uint64_t unzipped = pb_get_number(body, length, 1, 0);

	if (!pb_get_bytes(body, length, 2, &payload, &payload_length))
		return 0;
	free(cm->multi);
	cm->multi = NULL;
	cm->multi_length = cm->multi_at = 0;
	if (!unzipped)
	{
		cm->multi = (unsigned char *)malloc(payload_length ? payload_length : 1);
		if (!cm->multi)
			return 0;
		memcpy(cm->multi, payload, payload_length);
		cm->multi_length = payload_length;
		return 1;
	}
	if (unzipped > 64u * 1024 * 1024)
		return 0;
	{
		z_stream stream;
		int result;

		cm->multi = (unsigned char *)malloc((size_t)unzipped);
		if (!cm->multi)
			return 0;
		memset(&stream, 0, sizeof(stream));
		if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
		{
			free(cm->multi);
			cm->multi = NULL;
			return 0;
		}
		stream.next_in = (Bytef *)payload;
		stream.avail_in = (uInt)payload_length;
		stream.next_out = cm->multi;
		stream.avail_out = (uInt)unzipped;
		result = inflate(&stream, Z_FINISH);
		cm->multi_length = (size_t)stream.total_out;
		inflateEnd(&stream);
		if (result != Z_STREAM_END)
		{
			free(cm->multi);
			cm->multi = NULL;
			cm->multi_length = 0;
			return 0;
		}
	}
	return 1;
}

/* a raw message into its parts: 1, else 0 (not a protobuf message: passed over) */
static int split(const unsigned char *data, size_t length, struct sc_message *message)
{
	uint32_t raw, header_length;

	if (length < 8)
		return 0;
	raw = read_u32(data);
	header_length = read_u32(data + 4);
	if (!(raw & PROTO_MASK) || header_length > length - 8)
		return 0;
	message->emsg = raw & ~PROTO_MASK;
	message->header = data + 8;
	message->header_length = header_length;
	message->body = data + 8 + header_length;
	message->body_length = length - 8 - header_length;
	return 1;
}

/* what the client keeps from messages it wasn't waiting for */
static void notice(struct sc_cm *cm, const struct sc_message *message)
{
	if (message->emsg == SC_EMSG_ACCOUNT_INFO)
		pb_get_string(message->body, message->body_length, 1, cm->persona, sizeof(cm->persona));
	else if (message->emsg == SC_EMSG_LOGGED_OFF)
	{
		cm->logged_off = (int)pb_get_number(message->body, message->body_length, 1, SC_RESULT_FAIL);
		sc_log("Steam: logged off by Steam (EResult %d)", cm->logged_off);
	}
}

int sc_cm_receive(struct sc_cm *cm, struct sc_message *message, int timeout_ms)
{
	long long end = sc_milliseconds() + timeout_ms;

	for (;;)
	{
		unsigned char *data;
		size_t length;
		long long left;
		int result, wait;

		/* (a Multi's inner messages first) */
		while (cm->multi && cm->multi_at + 4 <= cm->multi_length)
		{
			uint32_t inner = read_u32(cm->multi + cm->multi_at);
			const unsigned char *at = cm->multi + cm->multi_at + 4;

			if (inner > cm->multi_length - cm->multi_at - 4)
			{
				cm->multi_at = cm->multi_length;
				break;
			}
			cm->multi_at += 4 + inner;
			if (split(at, inner, message) && message->emsg != SC_EMSG_MULTI)
			{
				notice(cm, message);
				return 1;
			}
		}
		if (cm->session && cm->heartbeat_seconds &&
			sc_milliseconds() - cm->last_send >= (long long)cm->heartbeat_seconds * 1000)
		{
			sc_cm_send(cm, SC_EMSG_HEARTBEAT, NULL, 0, NULL, 0);
		}
		left = end - sc_milliseconds();
		if (left < 0)
			return 0;
		if (cm->cancel && *cm->cancel)
			return -1;
		/* (in slices: a cancel and the heartbeat are seen) */
		wait = left > 500 ? 500 : (int)left;
		result = sc_websocket_receive(cm->connection, &data, &length, wait);
		if (result < 0)
			return -1;
		if (result == 0)
		{
			if (left <= wait)
				return 0;
			continue;
		}
		free(cm->message);
		cm->message = data;
		if (!split(data, length, message))
			continue;
		if (message->emsg == SC_EMSG_MULTI)
		{
			if (!multi_take(cm, message->body, message->body_length))
				sc_log("Steam: a CM Multi could not be unpacked");
			continue;
		}
		notice(cm, message);
		return 1;
	}
}

int sc_cm_call(struct sc_cm *cm, const char *method, const void *body, size_t length, int authed,
	unsigned char **response, size_t *response_length, int *eresult)
{
	uint64_t job = ++cm->next_job;
	long long end;
	struct sc_message message;

	*response = NULL;
	*response_length = 0;
	*eresult = 0;
	if (!sc_cm_send(cm, authed ? SC_EMSG_SERVICE_CALL : SC_EMSG_SERVICE_CALL_NOT_AUTHED, body, length, method, job))
		return 0;
	end = sc_milliseconds() + CALL_TIMEOUT;
	for (;;)
	{
		long long left = end - sc_milliseconds();
		int result;

		if (left <= 0)
			return 0;
		result = sc_cm_receive(cm, &message, (int)left);
		if (result < 0)
			return 0;
		if (result == 0)
			continue;
		if (pb_get_number(message.header, message.header_length, HEADER_JOB_TARGET, 0) != job)
			continue;
		*eresult = (int)pb_get_number(message.header, message.header_length, HEADER_ERESULT, SC_RESULT_FAIL);
		if (*eresult != SC_RESULT_OK)
		{
			char why[128];

			pb_get_string(message.header, message.header_length, HEADER_ERROR, why, sizeof(why));
			sc_log("Steam: %s answered EResult %d %s", method, *eresult, why);
		}
		*response = (unsigned char *)malloc(message.body_length + 1);
		if (!*response)
			return 0;
		memcpy(*response, message.body, message.body_length);
		*response_length = message.body_length;
		return 1;
	}
}

size_t sc_cm_logon_body(void *buffer, size_t capacity, const char *account, const char *refresh_token,
	uint32_t login_id)
{
	unsigned char address[16];
	struct pb_writer writer, ip;

	if (!login_id || !account || !account[0] || !refresh_token || !refresh_token[0])
		return 0;
	pb_writer_init(&ip, address, sizeof(address));
	/* CMsgIPAddress.v4 */
	pb_put_fixed32(&ip, 1, login_id);
	pb_writer_init(&writer, buffer, capacity);
	pb_put_varint(&writer, 1, SC_PROTOCOL_VERSION);
	/* deprecated_obfustucated_private_ip: the LoginID's older copy, still read */
	pb_put_varint(&writer, 2, login_id);
	pb_put_varint(&writer, 3, 0);
	pb_put_string(&writer, 6, "english");
	/* client OS type: a desktop one */
	pb_put_varint(&writer, 7, 20);
	/* should_remember_password: needed for a refresh token logon */
	pb_put_varint(&writer, 8, 1);
	/* obfuscated_private_ip: the LoginID */
	pb_put_bytes(&writer, 11, ip.data, ip.length);
	pb_put_string(&writer, 50, account);
	pb_put_string(&writer, 96, CLIENT_NAME);
	/* access_token: the refresh token */
	pb_put_string(&writer, 108, refresh_token);
	return writer.failed ? 0 : writer.length;
}

int sc_cm_logon(struct sc_cm *cm, const char *account, const char *refresh_token, uint32_t login_id)
{
	unsigned char body[4096];
	size_t length = sc_cm_logon_body(body, sizeof(body), account, refresh_token, login_id);
	struct sc_message message;
	long long end;

	if (!length)
		return 0;
	cm->steamid = LOGON_STEAMID;
	if (!sc_cm_send(cm, SC_EMSG_LOGON, body, length, NULL, 0))
		return 0;
	end = sc_milliseconds() + CALL_TIMEOUT;
	for (;;)
	{
		long long left = end - sc_milliseconds();
		int result, eresult;

		if (left <= 0)
			return 0;
		result = sc_cm_receive(cm, &message, (int)left);
		if (result < 0)
			return cm->logged_off;
		if (result == 0 || message.emsg != SC_EMSG_LOGON_RESPONSE)
			continue;
		eresult = pb_int32(pb_get_number(message.body, message.body_length, 1, SC_RESULT_FAIL));
		if (eresult != SC_RESULT_OK)
			return eresult;
		cm->steamid = pb_get_number(message.header, message.header_length, HEADER_STEAMID, 0);
		cm->session = pb_int32(pb_get_number(message.header, message.header_length, HEADER_SESSION, 0));
		cm->cell_id = (uint32_t)pb_get_number(message.body, message.body_length, 7, 0);
		cm->heartbeat_seconds = pb_int32(pb_get_number(message.body, message.body_length, 3, 9));
		if (cm->heartbeat_seconds <= 0 || cm->heartbeat_seconds > 120)
			cm->heartbeat_seconds = 9;
		return SC_RESULT_OK;
	}
}

int sc_cm_depot_key(struct sc_cm *cm, uint32_t depot, uint32_t app, unsigned char key[32])
{
	unsigned char body[32];
	struct pb_writer writer;
	struct sc_message message;
	long long end;

	pb_writer_init(&writer, body, sizeof(body));
	pb_put_varint(&writer, 1, depot);
	pb_put_varint(&writer, 2, app);
	if (!sc_cm_send(cm, SC_EMSG_DEPOT_KEY, body, writer.length, NULL, 0))
		return 0;
	end = sc_milliseconds() + CALL_TIMEOUT;
	for (;;)
	{
		long long left = end - sc_milliseconds();
		const unsigned char *bytes;
		size_t length;
		int result, eresult;

		if (left <= 0)
			return 0;
		result = sc_cm_receive(cm, &message, (int)left);
		if (result < 0)
			return 0;
		if (result == 0 || message.emsg != SC_EMSG_DEPOT_KEY_RESPONSE)
			continue;
		eresult = pb_int32(pb_get_number(message.body, message.body_length, 1, SC_RESULT_FAIL));
		if (eresult != SC_RESULT_OK)
			return eresult;
		if (!pb_get_bytes(message.body, message.body_length, 3, &bytes, &length) || length != 32)
			return SC_RESULT_FAIL;
		memcpy(key, bytes, 32);
		return SC_RESULT_OK;
	}
}
#endif /* HALO_MCC */
