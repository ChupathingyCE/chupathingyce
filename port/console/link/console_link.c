/*
CONSOLE_LINK.C

Link Profile on a console without TLS (console_link.h). The pure parts
first (keys, the code, the messages, the answers), then the console's: the
state the panel shows, and the requests, one at a time on a thread of their
own.
*/

#include "console_link.h"

#include "qrcodegen.h"

#include <stdio.h>
#include <string.h>

/* (port/linux/src/p2p_crypto.c's, which every console builds for internet
play: Monocypher's Ed25519 and a SHA-256) */
void p2p_sha256(const void *data, int size, unsigned char *digest);
void p2p_ed25519_public(const unsigned char *seed, unsigned char *public_key, unsigned char *x25519_secret);
void p2p_ed25519_sign(const unsigned char *seed, const unsigned char *public_key, const void *message, int size,
	unsigned char *signature);

#define ID_LABEL "chupathingy console id v1\n"
#define COMMIT_LABEL "chupathingy console link commit v1\n"
#define CODE_LABEL "chupathingy console link code v1\n"
#define SIGNATURE_LABEL "chupathingy console link v1\n"

const char console_link_alphabet[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";

/* ---------- text */

static void wipe(void *data, int size)
{
	volatile unsigned char *bytes = data;

	while (size-- > 0)
		*bytes++ = 0;
}

/* text appended to buffer (of size bytes, NUL terminated): 1, or 0 if it
did not fit (the buffer then holds what did) */
static int append(char *buffer, int size, const char *text)
{
	int used = (int)strlen(buffer);
	int length = (int)strlen(text);

	if (used + length >= size)
	{
		memcpy(buffer + used, text, (size_t)(size - 1 - used));
		buffer[size - 1] = 0;
		return 0;
	}
	memcpy(buffer + used, text, (size_t)length + 1);
	return 1;
}

static int hex_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

void console_link_hex(const unsigned char *data, int size, char *text)
{
	static const char digits[] = "0123456789abcdef";
	int index;

	for (index = 0; index < size; index++)
	{
		text[2 * index] = digits[data[index] >> 4];
		text[2 * index + 1] = digits[data[index] & 15];
	}
	text[2 * size] = 0;
}

int console_link_unhex(const char *text, unsigned char *data, int size)
{
	int index;

	for (index = 0; index < size; index++)
	{
		int high = hex_value(text[2 * index]);
		int low = high >= 0 ? hex_value(text[2 * index + 1]) : -1;

		if (low < 0)
			return 0;
		data[index] = (unsigned char)(high << 4 | low);
	}
	return 1;
}

static int is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* a word of letters, digits, '_' and '-' at text into word (most size - 1
characters): where it ends */
static const char *take_word(const char *text, char *word, int size)
{
	int length = 0;

	while (*text == ' ')
		text++;
	while ((*text >= 'a' && *text <= 'z') || (*text >= 'A' && *text <= 'Z') || (*text >= '0' && *text <= '9') ||
		*text == '_' || *text == '-')
	{
		if (length < size - 1)
			word[length++] = *text;
		text++;
	}
	word[length] = 0;
	return text;
}

/* ---------- keys and the code */

void console_link_public_key(const unsigned char *seed, unsigned char *public_key)
{
	p2p_ed25519_public(seed, public_key, NULL);
}

void console_link_id(const unsigned char *public_key, unsigned char *id)
{
	unsigned char data[sizeof(ID_LABEL) - 1 + CONSOLE_LINK_KEY_SIZE];
	unsigned char digest[32];

	memcpy(data, ID_LABEL, sizeof(ID_LABEL) - 1);
	memcpy(data + sizeof(ID_LABEL) - 1, public_key, CONSOLE_LINK_KEY_SIZE);
	p2p_sha256(data, (int)sizeof(data), digest);
	memcpy(id, digest, CONSOLE_LINK_ID_SIZE);
}

void console_link_commitment(const unsigned char *secret, unsigned char *commitment)
{
	unsigned char data[sizeof(COMMIT_LABEL) - 1 + CONSOLE_LINK_SECRET_SIZE];

	memcpy(data, COMMIT_LABEL, sizeof(COMMIT_LABEL) - 1);
	memcpy(data + sizeof(COMMIT_LABEL) - 1, secret, CONSOLE_LINK_SECRET_SIZE);
	p2p_sha256(data, (int)sizeof(data), commitment);
	wipe(data, (int)sizeof(data));
}

void console_link_code(const unsigned char *public_key, const unsigned char *secret, const unsigned char *nonce,
	char *code)
{
	unsigned char data[sizeof(CODE_LABEL) - 1 + CONSOLE_LINK_KEY_SIZE + CONSOLE_LINK_SECRET_SIZE +
		CONSOLE_LINK_NONCE_SIZE];
	unsigned char digest[32];
	int at = (int)sizeof(CODE_LABEL) - 1, index;

	memcpy(data, CODE_LABEL, (size_t)at);
	memcpy(data + at, public_key, CONSOLE_LINK_KEY_SIZE);
	at += CONSOLE_LINK_KEY_SIZE;
	memcpy(data + at, secret, CONSOLE_LINK_SECRET_SIZE);
	at += CONSOLE_LINK_SECRET_SIZE;
	memcpy(data + at, nonce, CONSOLE_LINK_NONCE_SIZE);
	p2p_sha256(data, (int)sizeof(data), digest);
	for (index = 0; index < CONSOLE_LINK_CODE_LENGTH; index++)
		code[index] = console_link_alphabet[digest[index] % (sizeof(console_link_alphabet) - 1)];
	code[CONSOLE_LINK_CODE_LENGTH] = 0;
	wipe(data, (int)sizeof(data));
}

int console_link_confirm_message(const char *token, const char *handle, int accept, char *text, int size)
{
	if (size <= 0)
		return 0;
	text[0] = 0;
	if (!append(text, size, SIGNATURE_LABEL "confirm\n") || !append(text, size, token) || !append(text, size, "\n") ||
		!append(text, size, handle) || !append(text, size, accept ? "\naccept\n" : "\ndecline\n"))
	{
		return 0;
	}
	return (int)strlen(text);
}

int console_link_profile_message(const char *public_hex, const char *challenge, char *text, int size)
{
	if (size <= 0)
		return 0;
	text[0] = 0;
	if (!append(text, size, SIGNATURE_LABEL "profile\n") || !append(text, size, public_hex) ||
		!append(text, size, "\n") || !append(text, size, challenge) || !append(text, size, "\n"))
	{
		return 0;
	}
	return (int)strlen(text);
}

void console_link_sign(const unsigned char *seed, const void *message, int size, char *signature_hex)
{
	unsigned char public_key[CONSOLE_LINK_KEY_SIZE], signature[64];

	p2p_ed25519_public(seed, public_key, NULL);
	p2p_ed25519_sign(seed, public_key, message, size, signature);
	console_link_hex(signature, 64, signature_hex);
}

int console_link_parse_seed(const char *data, int size, unsigned char *seed)
{
	int start = 0, end = size, index, zero = 1;

	while (start < end && is_space(data[start]))
		start++;
	while (end > start && is_space(data[end - 1]))
		end--;
	if (end - start != 2 * CONSOLE_LINK_SEED_SIZE || !console_link_unhex(data + start, seed, CONSOLE_LINK_SEED_SIZE))
		return 0;
	for (index = 0; index < CONSOLE_LINK_SEED_SIZE; index++)
		zero &= seed[index] == 0;
	return !zero;
}

/* ---------- the site's answers */

int console_link_parse_start(const char *reply, unsigned char *nonce, int *seconds, char *token)
{
	char word[80];
	int value = 0, digits = 0;

	reply = take_word(reply, word, sizeof(word));
	if (strcmp(word, "ok"))
		return 0;
	reply = take_word(reply, word, sizeof(word));
	if (strlen(word) != 2 * CONSOLE_LINK_NONCE_SIZE || !console_link_unhex(word, nonce, CONSOLE_LINK_NONCE_SIZE))
		return 0;
	while (*reply == ' ')
		reply++;
	while (*reply >= '0' && *reply <= '9' && digits < 6)
	{
		value = value * 10 + (*reply++ - '0');
		digits++;
	}
	if (!digits || value <= 0 || *reply != ' ')
		return 0;
	reply = take_word(reply, word, sizeof(word));
	if (strlen(word) != CONSOLE_LINK_TOKEN_LENGTH || strspn(word, "0123456789abcdef") != CONSOLE_LINK_TOKEN_LENGTH)
		return 0;
	*seconds = value;
	memcpy(token, word, CONSOLE_LINK_TOKEN_LENGTH + 1);
	return 1;
}

static int handle_ok(const char *handle)
{
	int length = (int)strlen(handle);

	return length >= 3 && length <= 20 && (int)strspn(handle, "abcdefghijklmnopqrstuvwxyz0123456789_-") == length;
}

int console_link_parse_status(const char *reply, char *handle, char *previous)
{
	char word[16];

	handle[0] = 0;
	previous[0] = 0;
	reply = take_word(reply, word, sizeof(word));
	if (!strcmp(word, "pending"))
		return CONSOLE_LINK_STATUS_PENDING;
	if (!strcmp(word, "declined"))
		return CONSOLE_LINK_STATUS_DECLINED;
	if (!strcmp(word, "expired"))
		return CONSOLE_LINK_STATUS_EXPIRED;
	if (strcmp(word, "confirm") && strcmp(word, "connected"))
		return CONSOLE_LINK_STATUS_BAD;
	reply = take_word(reply, handle, CONSOLE_LINK_HANDLE_SIZE);
	if (!handle_ok(handle))
	{
		handle[0] = 0;
		return CONSOLE_LINK_STATUS_BAD;
	}
	if (!strcmp(word, "connected"))
		return CONSOLE_LINK_STATUS_CONNECTED;
	take_word(reply, previous, CONSOLE_LINK_HANDLE_SIZE);
	if (previous[0] && !handle_ok(previous))
		previous[0] = 0;
	return CONSOLE_LINK_STATUS_CONFIRM;
}

int console_link_parse_challenge(const char *reply, char *challenge)
{
	const char *at = strstr(reply, "\"challenge\"");

	if (!at)
		return 0;
	at += 11;
	while (*at == ' ' || *at == ':')
		at++;
	if (*at++ != '"')
		return 0;
	if (strspn(at, "0123456789abcdef") != 32 || at[32] != '"')
		return 0;
	memcpy(challenge, at, 32);
	challenge[32] = 0;
	return 1;
}

/* a line's rest after "<name> " into value (printable ASCII only, cut to size - 1) */
static void line_value(const char *text, char *value, int size)
{
	int length = 0;

	while (*text && *text != '\n' && *text != '\r')
	{
		if (length < size - 1)
			value[length++] = *text >= ' ' && *text <= '~' ? *text : '?';
		text++;
	}
	value[length] = 0;
}

int console_link_parse_profile(const char *reply, char *handle, char *display, char *tag)
{
	const char *line = reply;
	int linked = -1;

	handle[0] = display[0] = tag[0] = 0;
	while (*line)
	{
		if (!strncmp(line, "unlinked", 8) && (line[8] == '\n' || line[8] == '\r' || !line[8]))
			return 0;
		if (!strncmp(line, "linked ", 7))
		{
			take_word(line + 7, handle, CONSOLE_LINK_HANDLE_SIZE);
			linked = handle_ok(handle) ? 1 : -1;
		}
		else if (!strncmp(line, "display ", 8))
			line_value(line + 8, display, CONSOLE_LINK_DISPLAY_SIZE);
		else if (!strncmp(line, "tag ", 4))
			line_value(line + 4, tag, CONSOLE_LINK_TAG_SIZE);
		else if (!strncmp(line, "end", 3) && (line[3] == '\n' || line[3] == '\r' || !line[3]))
			return linked;
		line = strchr(line, '\n');
		if (!line)
			break;
		line++;
	}
	/* (no "end": cut short) */
	handle[0] = display[0] = tag[0] = 0;
	return -1;
}

/* ---------- the console */

#ifndef CONSOLE_LINK_PURE

#define POLL_INTERVAL 3000
#define REFRESH_INTERVAL 60000
#define REPLY_SIZE 1024

enum
{
	_job_none = 0,
	_job_start,
	_job_poll,
	_job_confirm,
	_job_profile
};

static const struct console_link_port *port;

/* (under the port's lock) */
static struct
{
	struct console_link_view view;
	/* a request's thread is running (one at a time); the attempt its
	answer is for: an answer to an earlier attempt (cancelled) is dropped */
	int busy;
	int attempt;
	unsigned long code_time, code_lifetime, poll_time, refresh_time;
	int refreshed;
	char token[CONSOLE_LINK_TOKEN_LENGTH + 1];
	int accept;
	int linked;
	unsigned char id[CONSOLE_LINK_ID_SIZE];
	int qr_size;
	unsigned char qr[CONSOLE_LINK_QR_MAXIMUM * CONSOLE_LINK_QR_MAXIMUM];
} linking;

/* the request's own (only its thread touches them while busy) */
static struct
{
	int kind;
	int attempt;
	char token[CONSOLE_LINK_TOKEN_LENGTH + 1];
	char handle[CONSOLE_LINK_HANDLE_SIZE];
	int accept;
	char body[512];
	char reply[REPLY_SIZE];
} job;

static void log_line(const char *text)
{
	if (port && port->write_log)
		port->write_log(text);
}

static int send_request(const char *path, const char *body, int *status)
{
	int (*request)(const char *, const char *, int *, char *, int) =
		port->https_request ? port->https_request : port->request;

	job.reply[0] = 0;
	*status = 0;
	if (!request(path, body, status, job.reply, sizeof(job.reply)))
		return 0;
	job.reply[sizeof(job.reply) - 1] = 0;
	return 1;
}

/* the device key's seed from its file: 1, or 0 if there is none */
static int read_seed(unsigned char *seed)
{
	char data[160];
	FILE *file = fopen(port->key_path, "rb");
	int size, ok;

	if (!file)
		return 0;
	size = (int)fread(data, 1, sizeof(data), file);
	fclose(file);
	ok = size > 0 && size < (int)sizeof(data) && console_link_parse_seed(data, size, seed);
	wipe(data, (int)sizeof(data));
	return ok;
}

/* the device key's seed, made and kept the first time: 1, or 0 */
static int device_seed(unsigned char *seed)
{
	char text[2 * CONSOLE_LINK_SEED_SIZE + 2];
	FILE *file;
	int ok, index, zero = 1;

	if (read_seed(seed))
		return 1;
	port->random(seed, CONSOLE_LINK_SEED_SIZE);
	for (index = 0; index < CONSOLE_LINK_SEED_SIZE; index++)
		zero &= seed[index] == 0;
	if (zero)
		return 0;
	console_link_hex(seed, CONSOLE_LINK_SEED_SIZE, text);
	strcat(text, "\n");
	file = fopen(port->key_path, "wb");
	if (!file)
	{
		wipe(text, (int)sizeof(text));
		return 0;
	}
	ok = fwrite(text, 1, strlen(text), file) == strlen(text);
	ok = fclose(file) == 0 && ok;
	wipe(text, (int)sizeof(text));
	if (!ok)
	{
		remove(port->key_path);
		return 0;
	}
	/* (read back: what is used is what was kept) */
	return read_seed(seed);
}

/* the linked profile's handle kept (or forgotten, an empty handle) */
static void keep_link(const char *handle)
{
	FILE *file;

	if (!handle[0])
	{
		remove(port->link_path);
		return;
	}
	file = fopen(port->link_path, "wb");
	if (!file)
		return;
	fputs("linked ", file);
	fputs(handle, file);
	fputs("\n", file);
	fclose(file);
}

/* (under the lock) the console linked to handle, or not (an empty handle) */
static void set_linked(const char *handle)
{
	unsigned char seed[CONSOLE_LINK_SEED_SIZE], public_key[CONSOLE_LINK_KEY_SIZE];

	linking.linked = 0;
	linking.view.linked = 0;
	if (handle[0] && read_seed(seed))
	{
		console_link_public_key(seed, public_key);
		console_link_id(public_key, linking.id);
		linking.linked = 1;
		linking.view.linked = 1;
	}
	wipe(seed, (int)sizeof(seed));
	if (strcmp(linking.view.handle, handle))
	{
		linking.view.display[0] = 0;
		linking.view.tag[0] = 0;
	}
	strncpy(linking.view.handle, linking.linked ? handle : "", CONSOLE_LINK_HANDLE_SIZE - 1);
	linking.view.handle[CONSOLE_LINK_HANDLE_SIZE - 1] = 0;
}

/* (under the lock) the panel's phase, with what to say */
static void set_phase(int phase, const char *message)
{
	linking.view.phase = phase;
	linking.view.message[0] = 0;
	if (message)
		append(linking.view.message, CONSOLE_LINK_MESSAGE_SIZE, message);
}

static void make_qr(const char *code)
{
	unsigned char qr[qrcodegen_BUFFER_LEN_FOR_VERSION(6)], work[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
	char page[96];
	int size = 0, x, y;

	page[0] = 0;
	append(page, sizeof(page), "https://" CONSOLE_LINK_PAGE "?code=");
	append(page, sizeof(page), code);
	if (qrcodegen_encodeText(page, work, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, 6, qrcodegen_Mask_AUTO, true))
		size = qrcodegen_getSize(qr);
	if (size > CONSOLE_LINK_QR_MAXIMUM)
		size = 0;
	port->lock();
	linking.qr_size = size;
	for (y = 0; y < size; y++)
	{
		for (x = 0; x < size; x++)
			linking.qr[y * size + x] = qrcodegen_getModule(qr, x, y) ? 1 : 0;
	}
	port->unlock();
}

/* a code: the key and the commitment, then the secret */
static void run_start(void)
{
	unsigned char seed[CONSOLE_LINK_SEED_SIZE], public_key[CONSOLE_LINK_KEY_SIZE];
	unsigned char secret[CONSOLE_LINK_SECRET_SIZE], commitment[32], nonce[CONSOLE_LINK_NONCE_SIZE];
	char public_hex[2 * CONSOLE_LINK_KEY_SIZE + 1], hex[2 * CONSOLE_LINK_SECRET_SIZE + 1];
	char token[CONSOLE_LINK_TOKEN_LENGTH + 1], code[CONSOLE_LINK_CODE_LENGTH + 1], shown[CONSOLE_LINK_CODE_LENGTH + 2];
	const char *failure = NULL;
	int status = 0, seconds = 0, index;

	token[0] = 0;
	shown[0] = 0;

	if (!device_seed(seed))
	{
		failure = "Could not make this console's key. Check that its drive has room.";
		goto done;
	}
	console_link_public_key(seed, public_key);
	wipe(seed, (int)sizeof(seed));
	console_link_hex(public_key, CONSOLE_LINK_KEY_SIZE, public_hex);
	port->random(secret, CONSOLE_LINK_SECRET_SIZE);
	console_link_commitment(secret, commitment);
	console_link_hex(commitment, 32, hex);
	job.body[0] = 0;
	append(job.body, sizeof(job.body), "{\"key\": \"");
	append(job.body, sizeof(job.body), public_hex);
	append(job.body, sizeof(job.body), "\", \"commit\": \"");
	append(job.body, sizeof(job.body), hex);
	append(job.body, sizeof(job.body), "\", \"platform\": \"");
	append(job.body, sizeof(job.body), port->platform);
	append(job.body, sizeof(job.body), "\", \"build\": \"");
	/* (the build's name, without what would end the string) */
	for (index = 0; port->build[index] && index < 48; index++)
	{
		char one[2];

		one[0] = port->build[index] == '"' || port->build[index] == '\\' || (unsigned char)port->build[index] < ' ' ?
			' ' : port->build[index];
		one[1] = 0;
		append(job.body, sizeof(job.body), one);
	}
	if (!append(job.body, sizeof(job.body), "\"}"))
	{
		failure = "Could not ask for a code.";
		goto done;
	}
	if (!send_request("/v1/console/link/start", job.body, &status))
	{
		failure = "Could not reach " CONSOLE_LINK_SITE ". Check your network settings.";
		goto done;
	}
	if (status == 429)
	{
		failure = "Too many codes for now. Try again later.";
		goto done;
	}
	if (status != 200 || !console_link_parse_start(job.reply, nonce, &seconds, token))
	{
		failure = "The site gave no code. Try again later.";
		goto done;
	}
	console_link_hex(secret, CONSOLE_LINK_SECRET_SIZE, hex);
	job.body[0] = 0;
	append(job.body, sizeof(job.body), "{\"token\": \"");
	append(job.body, sizeof(job.body), token);
	append(job.body, sizeof(job.body), "\", \"secret\": \"");
	append(job.body, sizeof(job.body), hex);
	append(job.body, sizeof(job.body), "\"}");
	wipe(hex, (int)sizeof(hex));
	if (!send_request("/v1/console/link/code", job.body, &status) || status != 200)
	{
		failure = status ? "The site did not take this console's code. Try again." :
			"Could not reach " CONSOLE_LINK_SITE ". Check your network settings.";
		goto done;
	}
	console_link_code(public_key, secret, nonce, code);
	memcpy(shown, code, 4);
	shown[4] = '-';
	memcpy(shown + 5, code + 4, CONSOLE_LINK_CODE_LENGTH - 4 + 1);
	make_qr(shown);

done:
	wipe(secret, (int)sizeof(secret));
	wipe(job.body, (int)sizeof(job.body));
	port->lock();
	if (job.attempt == linking.attempt)
	{
		if (failure)
			set_phase(CONSOLE_LINK_FAILED, failure);
		else
		{
			set_phase(CONSOLE_LINK_CODE_SHOWN, NULL);
			memcpy(linking.view.code, shown, sizeof(shown));
			memcpy(linking.token, token, sizeof(token));
			linking.code_time = port->milliseconds();
			linking.code_lifetime = (unsigned long)seconds * 1000;
			linking.poll_time = linking.code_time;
		}
	}
	port->unlock();
	log_line(failure ? "Delta link: no code" : "Delta link: a code is shown");
}

/* (under the lock) what a status or confirm answer means */
static void take_status(int answer, const char *handle, const char *previous)
{
	switch (answer)
	{
	case CONSOLE_LINK_STATUS_CONFIRM:
		if (linking.view.phase == CONSOLE_LINK_CODE_SHOWN)
		{
			set_phase(CONSOLE_LINK_CONFIRM, NULL);
			strcpy(linking.view.asker, handle);
			strcpy(linking.view.previous, previous);
		}
		break;
	case CONSOLE_LINK_STATUS_CONNECTED:
		set_linked(handle);
		keep_link(handle);
		linking.refresh_time = 0;
		linking.refreshed = 0;
		set_phase(CONSOLE_LINK_DONE, NULL);
		append(linking.view.message, CONSOLE_LINK_MESSAGE_SIZE, "Linked to @");
		append(linking.view.message, CONSOLE_LINK_MESSAGE_SIZE, handle);
		append(linking.view.message, CONSOLE_LINK_MESSAGE_SIZE, ". Your games here count toward your Delta profile.");
		break;
	case CONSOLE_LINK_STATUS_DECLINED:
		set_phase(CONSOLE_LINK_FAILED, "Not linked. Nothing changed.");
		break;
	case CONSOLE_LINK_STATUS_EXPIRED:
		set_phase(CONSOLE_LINK_FAILED, "The code ran out, so nothing changed. Press A for a new one.");
		break;
	default:
		break;
	}
}

static void run_poll(void)
{
	char handle[CONSOLE_LINK_HANDLE_SIZE], previous[CONSOLE_LINK_HANDLE_SIZE];
	int status = 0, answer = CONSOLE_LINK_STATUS_BAD;

	job.body[0] = 0;
	append(job.body, sizeof(job.body), "{\"token\": \"");
	append(job.body, sizeof(job.body), job.token);
	append(job.body, sizeof(job.body), "\"}");
	if (send_request("/v1/console/link/status", job.body, &status) && status == 200)
		answer = console_link_parse_status(job.reply, handle, previous);
	port->lock();
	if (job.attempt == linking.attempt)
	{
		linking.poll_time = port->milliseconds();
		take_status(answer, handle, previous);
	}
	port->unlock();
}

static void run_confirm(void)
{
	unsigned char seed[CONSOLE_LINK_SEED_SIZE];
	char message[200], signature[129], handle[CONSOLE_LINK_HANDLE_SIZE], previous[CONSOLE_LINK_HANDLE_SIZE];
	int status = 0, answer = CONSOLE_LINK_STATUS_BAD, length;

	length = console_link_confirm_message(job.token, job.handle, job.accept, message, sizeof(message));
	if (length && read_seed(seed))
	{
		console_link_sign(seed, message, length, signature);
		wipe(seed, (int)sizeof(seed));
		job.body[0] = 0;
		append(job.body, sizeof(job.body), "{\"token\": \"");
		append(job.body, sizeof(job.body), job.token);
		append(job.body, sizeof(job.body), job.accept ? "\", \"accept\": true, \"signature\": \"" :
			"\", \"accept\": false, \"signature\": \"");
		append(job.body, sizeof(job.body), signature);
		append(job.body, sizeof(job.body), "\"}");
		if (send_request("/v1/console/link/confirm", job.body, &status) && status == 200)
			answer = console_link_parse_status(job.reply, handle, previous);
	}
	port->lock();
	if (job.attempt == linking.attempt)
	{
		if (answer == CONSOLE_LINK_STATUS_BAD || answer == CONSOLE_LINK_STATUS_CONFIRM ||
			answer == CONSOLE_LINK_STATUS_PENDING)
		{
			set_phase(CONSOLE_LINK_FAILED, status ? "The site did not take the answer. Press A for a new code." :
				"Could not reach " CONSOLE_LINK_SITE ". Check your network settings.");
		}
		else
			take_status(answer, handle, previous);
	}
	port->unlock();
	log_line(answer == CONSOLE_LINK_STATUS_CONNECTED ? "Delta link: linked" : "Delta link: not linked");
}

/* the console's own profile, for a signed challenge */
static void run_profile(void)
{
	unsigned char seed[CONSOLE_LINK_SEED_SIZE], public_key[CONSOLE_LINK_KEY_SIZE];
	char public_hex[2 * CONSOLE_LINK_KEY_SIZE + 1], challenge[33], message[200], signature[129];
	char handle[CONSOLE_LINK_HANDLE_SIZE], display[CONSOLE_LINK_DISPLAY_SIZE], tag[CONSOLE_LINK_TAG_SIZE];
	int status = 0, linked = -1, length;

	if (!read_seed(seed))
		goto done;
	console_link_public_key(seed, public_key);
	console_link_hex(public_key, CONSOLE_LINK_KEY_SIZE, public_hex);
	if (!send_request("/v1/claim_challenge", NULL, &status) || status != 200 ||
		!console_link_parse_challenge(job.reply, challenge))
	{
		goto done;
	}
	length = console_link_profile_message(public_hex, challenge, message, sizeof(message));
	if (!length)
		goto done;
	console_link_sign(seed, message, length, signature);
	job.body[0] = 0;
	append(job.body, sizeof(job.body), "{\"key\": \"");
	append(job.body, sizeof(job.body), public_hex);
	append(job.body, sizeof(job.body), "\", \"challenge\": \"");
	append(job.body, sizeof(job.body), challenge);
	append(job.body, sizeof(job.body), "\", \"signature\": \"");
	append(job.body, sizeof(job.body), signature);
	append(job.body, sizeof(job.body), "\"}");
	if (send_request("/v1/console/profile", job.body, &status) && status == 200)
		linked = console_link_parse_profile(job.reply, handle, display, tag);

done:
	wipe(seed, (int)sizeof(seed));
	port->lock();
	if (linked >= 0)
	{
		linking.refreshed = 1;
		if (linked != linking.linked || (linked && strcmp(handle, linking.view.handle)))
		{
			/* (unlinked on the site, or linked to another profile there) */
			if (!linked)
				handle[0] = 0;
			set_linked(handle);
			keep_link(handle);
		}
		if (linked)
		{
			strcpy(linking.view.display, display);
			strcpy(linking.view.tag, tag);
		}
	}
	port->unlock();
	if (linked < 0)
		log_line("Delta link: the profile did not load");
}

static void job_thread(void *context)
{
	(void)context;
	switch (job.kind)
	{
	case _job_start: run_start(); break;
	case _job_poll: run_poll(); break;
	case _job_confirm: run_confirm(); break;
	case _job_profile: run_profile(); break;
	default: break;
	}
	port->lock();
	linking.busy = 0;
	port->unlock();
}

/* (under the lock) a request started, if none is running: 1, or 0 */
static int start_job(int kind)
{
	if (linking.busy)
		return 0;
	job.kind = kind;
	job.attempt = linking.attempt;
	memcpy(job.token, linking.token, sizeof(job.token));
	strcpy(job.handle, linking.view.asker);
	job.accept = linking.accept;
	linking.busy = 1;
	if (!port->start_thread(job_thread, NULL))
	{
		linking.busy = 0;
		return 0;
	}
	return 1;
}

void console_link_initialize(const struct console_link_port *the_port)
{
	char data[64], handle[CONSOLE_LINK_HANDLE_SIZE];
	FILE *file;

	port = the_port;
	memset(&linking, 0, sizeof(linking));
	handle[0] = 0;
	file = fopen(port->link_path, "rb");
	if (file)
	{
		int size = (int)fread(data, 1, sizeof(data) - 1, file);

		fclose(file);
		data[size > 0 ? size : 0] = 0;
		if (!strncmp(data, "linked ", 7))
		{
			take_word(data + 7, handle, sizeof(handle));
			if (!handle_ok(handle))
				handle[0] = 0;
		}
	}
	set_linked(handle);
}

void console_link_view(struct console_link_view *view)
{
	if (!port)
	{
		memset(view, 0, sizeof(*view));
		return;
	}
	port->lock();
	*view = linking.view;
	view->busy = linking.busy;
	if (linking.view.phase == CONSOLE_LINK_CODE_SHOWN)
	{
		unsigned long gone = port->milliseconds() - linking.code_time;

		view->code_left = gone < linking.code_lifetime ? linking.code_lifetime - gone : 0;
	}
	port->unlock();
}

void console_link_update(void)
{
	unsigned long now;

	if (!port)
		return;
	port->lock();
	now = port->milliseconds();
	if (linking.view.phase == CONSOLE_LINK_CODE_SHOWN)
	{
		if (now - linking.code_time >= linking.code_lifetime)
		{
			linking.attempt++;
			set_phase(CONSOLE_LINK_FAILED, "The code ran out, so nothing changed. Press A for a new one.");
		}
		else if (now - linking.poll_time >= POLL_INTERVAL)
			start_job(_job_poll);
	}
	port->unlock();
}

void console_link_begin(void)
{
	if (!port)
		return;
	port->lock();
	if (!linking.busy && linking.view.phase != CONSOLE_LINK_GETTING_CODE && linking.view.phase != CONSOLE_LINK_CONFIRMING)
	{
		linking.attempt++;
		linking.qr_size = 0;
		linking.view.code[0] = 0;
		linking.view.asker[0] = 0;
		linking.view.previous[0] = 0;
		set_phase(CONSOLE_LINK_GETTING_CODE, NULL);
		if (!start_job(_job_start))
			set_phase(CONSOLE_LINK_FAILED, "Could not start. Try again.");
	}
	port->unlock();
}

void console_link_answer(int accept)
{
	if (!port)
		return;
	port->lock();
	if (linking.view.phase == CONSOLE_LINK_CONFIRM && !linking.busy)
	{
		linking.accept = accept != 0;
		set_phase(CONSOLE_LINK_CONFIRMING, NULL);
		if (!start_job(_job_confirm))
			set_phase(CONSOLE_LINK_CONFIRM, NULL);
	}
	port->unlock();
}

void console_link_cancel(void)
{
	if (!port)
		return;
	port->lock();
	if (linking.view.phase != CONSOLE_LINK_CONFIRMING)
	{
		linking.attempt++;
		linking.qr_size = 0;
		wipe(linking.token, (int)sizeof(linking.token));
		set_phase(CONSOLE_LINK_IDLE, NULL);
	}
	port->unlock();
}

void console_link_refresh(void)
{
	unsigned char seed[CONSOLE_LINK_SEED_SIZE];
	int have;

	if (!port)
		return;
	/* (a console that never asked for a code sends nothing) */
	have = read_seed(seed);
	wipe(seed, (int)sizeof(seed));
	if (!have)
		return;
	port->lock();
	if (!linking.busy && (linking.view.phase == CONSOLE_LINK_IDLE || linking.view.phase == CONSOLE_LINK_DONE ||
		linking.view.phase == CONSOLE_LINK_FAILED) &&
		(!linking.refresh_time || port->milliseconds() - linking.refresh_time >= REFRESH_INTERVAL))
	{
		linking.refresh_time = port->milliseconds();
		if (!linking.refresh_time)
			linking.refresh_time = 1;
		start_job(_job_profile);
	}
	port->unlock();
}

int console_link_qr_size(void)
{
	int size;

	if (!port)
		return 0;
	port->lock();
	size = linking.view.phase == CONSOLE_LINK_CODE_SHOWN ? linking.qr_size : 0;
	port->unlock();
	return size;
}

int console_link_qr_dark(int x, int y)
{
	/* (read without the lock: the QR code changes only while no code is
	shown, and the panel asks only while one is) */
	if (x < 0 || y < 0 || x >= linking.qr_size || y >= linking.qr_size)
		return 0;
	return linking.qr[y * linking.qr_size + x];
}

int console_link_player_id(unsigned char *id)
{
	int linked;

	if (!port)
		return 0;
	port->lock();
	linked = linking.linked;
	if (linked)
		memcpy(id, linking.id, CONSOLE_LINK_ID_SIZE);
	port->unlock();
	return linked;
}

#endif
