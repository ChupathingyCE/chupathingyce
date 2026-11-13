/*
IDENTITY_TEST.C

Tests of Delta's device identity (port/linux/src/identity.c): the shared
test vectors of docs/delta.md ("Device identity", also
port/linux/tests/identity_vectors.json, which tools/test_identity.py checks
these against), malformed certificates and lists, the chain with root keys
and without, the requests' text, the answers and the schedule. Built and
run by tools/test_identity.py with Monocypher; exits nonzero on a failure.
Every field is read byte by byte, so a big-endian host gets the same.
*/

#include "../src/identity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

/* the vectors (each seed or key is SHA-256 of its text) */
#define ROOT_SEED_TEXT "chupathingy test identity root"
#define ISSUER_SEED_TEXT "chupathingy test identity issuer"
#define DEVICE_SEED_TEXT "chupathingy test device key"
#define PLAYER_KEY_TEXT "chupathingy test player key"
#define VECTOR_ROOT_PUBLIC \
	"12762975a2781f981d53c9bfd7625f10b56137eee3235fb5f3eb92c966f023fa"
#define VECTOR_ISSUER_PUBLIC \
	"fc284aa53690a1b91984e67b1820c0a28452dafbc58ed107d40066ad47814d90"
#define VECTOR_ISSUER_KEY_ID \
	"48810d1682965a46"
#define VECTOR_DEVICE_PUBLIC \
	"f6bcb81b396cf7f3cb5519a437d3699adc4ed40f2e4d22f30c3bfecb6ffcca8e"
#define VECTOR_DEVICE_ID \
	"a0f42dfd380bf6aee133508172a12700"
#define VECTOR_PLAYER_KEY \
	"f79f4526c2651d192b184691cf7a3283819ab24220d7daaecc1a0c6defe8d4c3"
#define VECTOR_COPY_ID \
	"31ea6b25b20006a252c816cd2077ff5b"
#define VECTOR_ISSUER_CERTIFICATE \
	"4349535301000000803bb16a80e2276bfc284aa53690a1b91984e67b1820c0a28452dafbc58ed107d40066ad47814d90" \
	"38cc84f430b86054d1ede8b53244a90bda00b6012d4df101a0e00f5d388af8193c95238b1498d9fbd9314d4226ca2e6d" \
	"40332529ea8163ad127e579cc26dcc0c"
#define VECTOR_DEVICE_CERTIFICATE \
	"434445560101070120c2b26aa0fcbb6a010203040506070848810d1682965a4631ea6b25b20006a252c816cd2077ff5b" \
	"f6bcb81b396cf7f3cb5519a437d3699adc4ed40f2e4d22f30c3bfecb6ffcca8e1f0000006d696c656e6b6f359728fbc6" \
	"34431278f9352410620d865f517ebad416d36e0c845cdc85613fb2e8867b8ff2b8fe26c1d7b5b9abb3d99c462edea4d8" \
	"2ad78c975c6b970f239b02"
#define VECTOR_REVOKED \
	"435245560100000020c2b26a20c2b26aa013b46a48810d1682965a460200000011111111111111111111111111111111" \
	"a0f42dfd380bf6aee133508172a127001169fc31fd48dcb2b10d79abd24a09fc115f4323eae5feebbd1f7f9b97d24693" \
	"2b730bd2e50b6be603b751854cceefdf32c3311a4a20e8f48c9410e1c40ae50f"
#define VECTOR_AUDIENCE \
	"stats.chupathingy.org"
#define VECTOR_NONCE \
	"000102030405060708090a0b0c0d0e0f"
#define VECTOR_ENROLL_SIGNATURE \
	"cec504f992252432a5d499113cbb7469a791b22e743cd44f6f15b278df2d4882e89a5724ddcaffe73fb2a14fb8a74fd2" \
	"1f00160cf177e9e2f64561cdbd3b4305"
#define VECTOR_REFRESH_SIGNATURE \
	"f09ba64e6aa8ca29d8414d334dca2759d86d29f3757493a2b00be775a770f1ce0f7dc0388671683b6868a493416b50a2" \
	"7f11a1458bb1e29fc89541849b00170d"
#define VECTOR_TOKEN_SIGNATURE \
	"0c02b24be2e9caa6385ef49de7bf80cb0740c7a73085d8a267c1f7f87978f1e8331b870581b254a5a258c85465b3130d" \
	"5153e68646b300265f8ea304f0fd0901"
/* a proof in a game (Delta Peer's identity): the host's nonce 00 to 1f, its
binding the hex digits twice (an invite's 32), machine 3, local player 1,
the device certificate's serial, bound (flags 1), signed at 1790101000,
the name "Milenko"; and the same on a LAN (no binding, flags 0) */
#define VECTOR_PROOF_TIME 1790101000UL
#define VECTOR_PROOF_FLAGS 1UL
#define VECTOR_PROOF_LAN_FLAGS 0UL
#define VECTOR_PROOF_NONCE \
	"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
#define VECTOR_PROOF_BINDING \
	"0123456789abcdef0123456789abcdef"
#define VECTOR_PROOF_TAG \
	"Milenko"
#define VECTOR_PROOF_MESSAGE \
	"64656c7461206964656e746974792070726f6f662076310a000102030405060708090a0b0c0d0e0f1011121314151617" \
	"18191a1b1c1d1e1f20303132333435363738396162636465663031323334353637383961626364656603010102030405" \
	"0607080108c6b26a074d696c656e6b6f"

#define VECTOR_PROOF_SIGNATURE \
	"bd00fd2c414b9170b8f9d5af61cf38cf32758b5ab8cf133bf78bfe036a8c3e8e166ebe9da09384d446f9192d05d1aa72" \
	"35886d4af359f303c3c85c6028c7e400"

#define VECTOR_PROOF_LAN_SIGNATURE \
	"610f07b2e9ff47d104f2daf970467e09962e6e33db53f230054394fd05da9c51e898419ea5f76ef19792f6d6e9681eed" \
	"ac2aae3a06e3fd28e2126e843f9ad00a"

#define VECTOR_DEVICE_CODE \
	"DVG69E5J"
#define VECTOR_START_SIGNATURE \
	"14a41005d7d1e060eeb46b1af3de5cae53771df0ccf627ceb6d5c54b9a1ff6b1822ee2de4135bcaaa4a4ebf7b7cf743a" \
	"a6cbb5ba00a107ef660a1f798e8abc08"
#define VECTOR_CONFIRM_SIGNATURE \
	"0ad07709c097a5b192b6c0763ce5f0859ff801d3eccad5c1249b0ac07ebdfdbd9e0500b05b6c9ca512eb860968a70b1b" \
	"e1166a8720af25fae633431ee7fd1a02"
#define VECTOR_DECLINE_SIGNATURE \
	"0f0f2b8e63f0ef1136a0aefaf3f51437b5fd37fb2f82e4806271bb37b7f38402d5ba02d1f96430bac33a6e201d034f0f" \
	"e657f236fb84c77e6a737bf2991ab80f"
#define VECTOR_SIGNOUT_SIGNATURE \
	"f579b26e3db2c34573a7a4e9842d85ef6646513dbf73706aeae41c825a4478051dddea806dbe0ecfd7a39b803e3e5201" \
	"90750980b2c4ff3689f2c19c1a66e80b"
#define VECTOR_VALID_FROM 1790000000UL
#define VECTOR_VALID_UNTIL 1797776000UL
#define VECTOR_ISSUED 1790100000UL
#define VECTOR_EXPIRES 1790704800UL

static unsigned char root_public[32], issuer_public[32], device_public[32], device_id[16], copy_id[16];
static unsigned char issuer_data[IDENTITY_ISSUER_SIZE], device_data[IDENTITY_DEVICE_MOST], revoked_data[256];
static unsigned char nonce[16];
static int device_size, revoked_size;
static unsigned char roots[2][IDENTITY_KEY_SIZE];

static int from_hex(const char *text, unsigned char *bytes, int size)
{
	return identity_unhex(text, (int)strlen(text), bytes, size);
}

static int same_hex(const unsigned char *bytes, int size, const char *text)
{
	char hex[1024];

	identity_hex(bytes, size, hex);
	return !strcmp(hex, text);
}

static void seed_of(const char *text, unsigned char *seed)
{
	identity_sha256(text, (int)strlen(text), seed);
}

static void load(void)
{
	device_size = (int)strlen(VECTOR_DEVICE_CERTIFICATE) / 2;
	revoked_size = (int)strlen(VECTOR_REVOKED) / 2;
	CHECK(from_hex(VECTOR_ROOT_PUBLIC, root_public, 32));
	CHECK(from_hex(VECTOR_ISSUER_PUBLIC, issuer_public, 32));
	CHECK(from_hex(VECTOR_DEVICE_PUBLIC, device_public, 32));
	CHECK(from_hex(VECTOR_DEVICE_ID, device_id, 16));
	CHECK(from_hex(VECTOR_COPY_ID, copy_id, 16));
	CHECK(from_hex(VECTOR_ISSUER_CERTIFICATE, issuer_data, IDENTITY_ISSUER_SIZE));
	CHECK(from_hex(VECTOR_DEVICE_CERTIFICATE, device_data, device_size));
	CHECK(from_hex(VECTOR_REVOKED, revoked_data, revoked_size));
	CHECK(from_hex(VECTOR_NONCE, nonce, 16));
	memcpy(roots[1], root_public, 32);
}

static void test_keys(void)
{
	unsigned char seed[32], public_key[32], id[16], key_id[8], player_key[32], digest[32];
	static const char abc_sha256[] = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	static const char long_sha256[] = "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
	const char *long_text = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	unsigned char data[200];

	identity_sha256("abc", 3, digest);
	CHECK(same_hex(digest, 32, abc_sha256));
	identity_sha256(long_text, (int)strlen(long_text), digest);
	CHECK(same_hex(digest, 32, long_sha256));
	seed_of(ROOT_SEED_TEXT, seed);
	identity_public(seed, public_key);
	CHECK(!memcmp(public_key, root_public, 32));
	seed_of(ISSUER_SEED_TEXT, seed);
	identity_public(seed, public_key);
	CHECK(!memcmp(public_key, issuer_public, 32));
	identity_key_id(public_key, key_id);
	CHECK(same_hex(key_id, 8, VECTOR_ISSUER_KEY_ID));
	seed_of(DEVICE_SEED_TEXT, seed);
	identity_public(seed, public_key);
	CHECK(!memcmp(public_key, device_public, 32));
	identity_device_id(public_key, id);
	CHECK(!memcmp(id, device_id, 16));
	seed_of(PLAYER_KEY_TEXT, player_key);
	CHECK(same_hex(player_key, 32, VECTOR_PLAYER_KEY));
	memcpy(data, "halo-ce-universal player id\n", 28);
	memcpy(data + 28, player_key, 32);
	identity_sha256(data, 60, digest);
	CHECK(!memcmp(digest, copy_id, 16));
}

static void test_messages(void)
{
	unsigned char seed[32], message[IDENTITY_MESSAGE_MOST], signature[64];
	int size;

	seed_of(DEVICE_SEED_TEXT, seed);
	size = identity_enroll_message(VECTOR_AUDIENCE, nonce, copy_id, device_public, message);
	CHECK(size == 29 + 1 + 21 + 16 + 16 + 32 && size <= IDENTITY_MESSAGE_MOST);
	identity_sign(seed, message, size, signature);
	CHECK(same_hex(signature, 64, VECTOR_ENROLL_SIGNATURE));
	CHECK(identity_verify(device_public, message, size, signature));
	message[0] ^= 1;
	CHECK(!identity_verify(device_public, message, size, signature));
	size = identity_refresh_message(VECTOR_AUDIENCE, nonce, device_id, message);
	identity_sign(seed, message, size, signature);
	CHECK(same_hex(signature, 64, VECTOR_REFRESH_SIGNATURE));
	size = identity_token_message(VECTOR_AUDIENCE, nonce, device_id, message);
	identity_sign(seed, message, size, signature);
	CHECK(same_hex(signature, 64, VECTOR_TOKEN_SIGNATURE));
}

static void test_proof(void)
{
	unsigned char seed[32], proof_nonce[32], serial[8], message[IDENTITY_PROOF_MESSAGE_MOST], signature[64];
	struct identity_device device;
	struct identity_revoked revoked;
	int size, index;
	unsigned long now = VECTOR_ISSUED + 1000;

	CHECK(from_hex(VECTOR_PROOF_NONCE, proof_nonce, 32));
	for (index = 0; index < 8; index++)
		serial[index] = (unsigned char)(index + 1);
	seed_of(DEVICE_SEED_TEXT, seed);
	size = identity_proof_message(proof_nonce, VECTOR_PROOF_BINDING, 32, 3, 1, serial, (int)VECTOR_PROOF_FLAGS,
		VECTOR_PROOF_TIME, VECTOR_PROOF_TAG, 7, message);
	CHECK(size == 24 + 32 + 1 + 32 + 1 + 1 + 8 + 1 + 4 + 1 + 7 && size <= IDENTITY_PROOF_MESSAGE_MOST);
	CHECK(same_hex(message, size, VECTOR_PROOF_MESSAGE));
	identity_sign(seed, message, size, signature);
	CHECK(same_hex(signature, 64, VECTOR_PROOF_SIGNATURE));
	if (getenv("IDENTITY_TEST_PRINT"))
	{
		char hex[1024];

		identity_hex(message, size, hex);
		printf("proof message %s\n", hex);
		identity_hex(signature, 64, hex);
		printf("proof signature %s\n", hex);
	}
	/* the host's check: the chain, the dates, play, the list, the signature */
	CHECK(identity_check_proof(issuer_data, IDENTITY_ISSUER_SIZE, device_data, device_size, roots, 2, NULL, now,
		message, size, signature, &device) == IDENTITY_OK);
	CHECK(!strcmp(device.handle, "milenko") && !memcmp(device.account, copy_id, 16));
	/* (another machine, local player, nonce, flags, time or name: not this
	signature) */
	message[size - 1] ^= 1;
	CHECK(identity_check_proof(issuer_data, IDENTITY_ISSUER_SIZE, device_data, device_size, roots, 2, NULL, now,
		message, size, signature, &device) == IDENTITY_NOT_DEVICE_SIGNED);
	message[size - 1] ^= 1;
	message[size - 9] ^= 1;
	CHECK(identity_check_proof(issuer_data, IDENTITY_ISSUER_SIZE, device_data, device_size, roots, 2, NULL, now,
		message, size, signature, &device) == IDENTITY_NOT_DEVICE_SIGNED);
	message[size - 9] ^= 1;
	/* (fresh: the host takes ten minutes, a card half an hour, and the
	grace either way) */
	CHECK(identity_proof_fresh(VECTOR_PROOF_TIME, VECTOR_PROOF_TIME + IDENTITY_PROOF_HOST_AGE + IDENTITY_GRACE,
		IDENTITY_PROOF_HOST_AGE));
	CHECK(!identity_proof_fresh(VECTOR_PROOF_TIME, VECTOR_PROOF_TIME + IDENTITY_PROOF_HOST_AGE + IDENTITY_GRACE + 1,
		IDENTITY_PROOF_HOST_AGE));
	CHECK(identity_proof_fresh(VECTOR_PROOF_TIME, VECTOR_PROOF_TIME + IDENTITY_PROOF_HOST_AGE + 1,
		IDENTITY_PROOF_CARD_AGE));
	CHECK(identity_proof_fresh(VECTOR_PROOF_TIME, VECTOR_PROOF_TIME - IDENTITY_GRACE, IDENTITY_PROOF_HOST_AGE));
	CHECK(!identity_proof_fresh(VECTOR_PROOF_TIME, VECTOR_PROOF_TIME - IDENTITY_GRACE - 1, IDENTITY_PROOF_HOST_AGE));
	/* (the device on the list) */
	CHECK(identity_parse_revoked(revoked_data, revoked_size, &revoked));
	CHECK(identity_check_proof(issuer_data, IDENTITY_ISSUER_SIZE, device_data, device_size, roots, 2, &revoked, now,
		message, size, signature, &device) == IDENTITY_REVOKED);
	/* (expired, or no root trusted: never taken on its word) */
	CHECK(identity_check_proof(issuer_data, IDENTITY_ISSUER_SIZE, device_data, device_size, roots, 2, NULL,
		VECTOR_EXPIRES + IDENTITY_GRACE + 1, message, size, signature, &device) == IDENTITY_EXPIRED);
	{
		unsigned char none[2][IDENTITY_KEY_SIZE];

		memset(none, 0, sizeof(none));
		CHECK(identity_check_proof(issuer_data, IDENTITY_ISSUER_SIZE, device_data, device_size, none, 2, NULL, now,
			message, size, signature, &device) == IDENTITY_NOT_ROOT_SIGNED);
	}
	/* on a LAN: no binding */
	size = identity_proof_message(proof_nonce, "", 0, 3, 1, serial, (int)VECTOR_PROOF_LAN_FLAGS, VECTOR_PROOF_TIME,
		VECTOR_PROOF_TAG, 7, message);
	identity_sign(seed, message, size, signature);
	CHECK(same_hex(signature, 64, VECTOR_PROOF_LAN_SIGNATURE));
	if (getenv("IDENTITY_TEST_PRINT"))
	{
		char hex[256];

		identity_hex(signature, 64, hex);
		printf("proof lan signature %s\n", hex);
	}
	/* fields out of range */
	CHECK(identity_proof_message(proof_nonce, "", 0, 3, 4, serial, 0, 0, "", 0, message) < 0);
	CHECK(identity_proof_message(proof_nonce, "", 65, 3, 0, serial, 0, 0, "", 0, message) < 0);
	CHECK(identity_proof_message(proof_nonce, "", 0, 3, 0, serial, 0, 0, "", 25, message) < 0);
	/* tags */
	CHECK(identity_tag_valid("Milenko", 7) && identity_tag_valid("The_Real-Milenko", 16));
	CHECK(identity_tag_valid("abcdefghijklmnopqrst", 20) && !identity_tag_valid("abcdefghijklmnopqrstu", 21));
	CHECK(!identity_tag_valid("ab", 2) && !identity_tag_valid("a bc", 4) && !identity_tag_valid("abc.", 4) &&
		!identity_tag_valid("ab<c", 4));
	CHECK(identity_tag_is_handle("Milenko", "milenko") && !identity_tag_is_handle("Milenk0", "milenko") &&
		!identity_tag_is_handle("Milenko", "") && !identity_tag_is_handle("Mile", "milenko"));
}

/* the look-alike keys (port/linux/tests/identity_vectors.json's
"lookalikes", which the site's tests check too) */
static const char *const lookalikes[][2] =
{
	{ "Milenko", "miienko" }, { "Mi1enk0", "miienko" }, { "MILENK0", "miienko" }, { "mi|enko", "miienko" },
	{ "milenko2", "miienko2" }, { "rnilenko", "miienko" }, { "\xD0\x9Cilenko", "miienko" },
	{ "vvarthog", "warthog" }, { "Sarge_99", "sarge99" }, { "T3x", "tex" }, { "@pollo", "apoiio" },
	{ "$arge", "sarge" }, { "Milenk\xC3\xB3", "miienko" }, { "Z\xC3\x84TCHBOT", "zatchbot" },
	{ "\xC5\x81ord", "iord" }, { "milenko1", "miienkoi" },
};

static void test_lookalikes(void)
{
	char key[IDENTITY_LOOKALIKE_MOST + 1];
	unsigned char account[16];
	int index;

	for (index = 0; index < (int)(sizeof(lookalikes) / sizeof(lookalikes[0])); index++)
	{
		identity_lookalike_key(lookalikes[index][0], key);
		if (strcmp(key, lookalikes[index][1]))
			printf("lookalike %s: %s, not %s\n", lookalikes[index][0], key, lookalikes[index][1]);
		CHECK(!strcmp(key, lookalikes[index][1]));
	}
	/* (long names stop at the key's size) */
	CHECK(identity_lookalike_key("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", key) ==
		IDENTITY_LOOKALIKE_MOST);
	/* delta_account.txt: guest, an account, the copy's own; a byte order
	mark and spaces left out; a file that is neither a guest but for the
	owner's profile */
	CHECK(identity_account_choice("guest\n", 0, account) == 0);
	CHECK(identity_account_choice("\xEF\xBB\xBF  31ea6b25b20006a252c816cd2077ff5b \r\n", 0, account) == 2 &&
		account[0] == 0x31 && account[15] == 0x5b);
	CHECK(identity_account_choice("\tguest\r\n", 1, account) == 0);
	CHECK(identity_account_choice("nonsense", 1, account) == 1);
	CHECK(identity_account_choice("nonsense", 0, account) == 0);
	CHECK(identity_account_choice("", 2, account) == 1 && identity_account_choice("", 0, account) == 0);
	CHECK(identity_account_choice("31ea6b25b20006a252c816cd2077ffzz", 0, account) == 0);
}

/* Quick Connect (stage B): the code, its messages, bodies and answers */
static void test_quick_connect(void)
{
	unsigned char seed[32], message[IDENTITY_MESSAGE_MOST], signature[64], code_nonce[16];
	char code[IDENTITY_CODE_TEXT], body[1024], token[IDENTITY_POLL_TOKEN_SIZE];
	unsigned long interval = 0, lifetime = 0;
	int size;

	identity_device_code(device_public, nonce, code);
	CHECK(!strcmp(code, "DVG6-9E5J"));
	CHECK(identity_clean_code("dvg6 9e5j", code) && !strcmp(code, "DVG6-9E5J"));
	CHECK(identity_clean_code(VECTOR_DEVICE_CODE, code) && !strcmp(code, "DVG6-9E5J"));
	CHECK(!identity_clean_code("DVG6-9E5", code) && !code[0]);
	CHECK(!identity_clean_code("DVG6-9E5JX", code));
	CHECK(!identity_clean_code("DVG6-9E5O", code));
	/* (Link a device takes an older copy's 6-character code too) */
	CHECK(identity_approval_code("dvg6-9e5j", code) && !strcmp(code, "DVG69E5J"));
	CHECK(identity_approval_code("K7Q-4MD", code) && !strcmp(code, "K7Q4MD"));
	CHECK(!identity_approval_code("K7Q-4M", code) && !code[0]);
	CHECK(!identity_approval_code("K7Q-4MD2", code));
	CHECK(!identity_approval_code("K7Q-4M0", code));
	CHECK(!identity_approval_code("DVG69E5J2", code));
	seed_of(DEVICE_SEED_TEXT, seed);
	size = identity_start_message(VECTOR_AUDIENCE, nonce, device_public, message);
	CHECK(size == 28 + 1 + 21 + 16 + 32);
	identity_sign(seed, message, size, signature);
	CHECK(same_hex(signature, 64, VECTOR_START_SIGNATURE));
	size = identity_confirm_message(VECTOR_AUDIENCE, nonce, device_id, 1, "milenko", message);
	CHECK(size == 30 + 1 + 21 + 16 + 16 + 2 + 7 && size <= IDENTITY_MESSAGE_MOST);
	identity_sign(seed, message, size, signature);
	CHECK(same_hex(signature, 64, VECTOR_CONFIRM_SIGNATURE));
	size = identity_confirm_message(VECTOR_AUDIENCE, nonce, device_id, 0, "milenko", message);
	identity_sign(seed, message, size, signature);
	CHECK(same_hex(signature, 64, VECTOR_DECLINE_SIGNATURE));
	CHECK(identity_confirm_message(VECTOR_AUDIENCE, nonce, device_id, 1, "a_handle_far_too_long_x", message) < 0);
	size = identity_signout_message(VECTOR_AUDIENCE, nonce, device_id, message);
	identity_sign(seed, message, size, signature);
	CHECK(same_hex(signature, 64, VECTOR_SIGNOUT_SIGNATURE));
	/* the bodies */
	CHECK(identity_start_body(body, sizeof(body), device_public, VECTOR_NONCE, signature, "pc_linux", "0.9.1",
		"Desk \"x\"") > 0);
	CHECK(strstr(body, "\"device_key\": \"" VECTOR_DEVICE_PUBLIC "\"") && strstr(body, "\"name\": \"Desk x\"") &&
		strstr(body, "\"platform\": \"pc_linux\"") && strstr(body, "\"client_id\": \"" IDENTITY_CLIENT_ID "\""));
	CHECK(identity_start_body(body, 100, device_public, VECTOR_NONCE, signature, "pc_linux", "0.9.1", "") == -1);
	CHECK(identity_confirm_body(body, sizeof(body), "ab", device_id, VECTOR_NONCE, signature, 1) > 0);
	CHECK(strstr(body, "\"accept\": true") && strstr(body, "\"device_id\": \"" VECTOR_DEVICE_ID "\""));
	/* the start's answer */
	CHECK(identity_start_answer("{\"code_nonce\": \"" VECTOR_NONCE "\", \"poll_token\": \""
		"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\", \"interval\": 5, \"expires_in\": 600}",
		code_nonce, token, &interval, &lifetime));
	CHECK(!memcmp(code_nonce, nonce, 16) && interval == 5 && lifetime == 600 && strlen(token) == 64);
	CHECK(!identity_start_answer("{\"code_nonce\": \"" VECTOR_NONCE "\", \"poll_token\": \"XYZ\", \"interval\": 5, "
		"\"expires_in\": 600}", code_nonce, token, &interval, &lifetime) && !token[0]);
	CHECK(identity_start_answer("{\"code_nonce\": \"" VECTOR_NONCE "\", \"poll_token\": \""
		"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\", \"interval\": 0, \"expires_in\": 600}",
		code_nonce, token, &interval, &lifetime) && interval == 2);
}

/* stage C: when the player key may go */
static void test_key_allowed(void)
{
	struct identity_state state;
	struct identity_device device;
	unsigned long now = VECTOR_ISSUED + 100;

	memset(&state, 0, sizeof(state));
	CHECK(identity_key_allowed(&state, now));
	CHECK(identity_parse_device(device_data, device_size, &device));
	identity_have_certificate(&state, &device, 0, now);
	CHECK(!identity_key_allowed(&state, now));
	/* (a site that doesn't speak identity takes only the key) */
	CHECK(identity_answered(&state, 404, "", now) == IDENTITY_ANSWER_LATER);
	CHECK(identity_key_allowed(&state, now) && !identity_key_allowed(&state, now + IDENTITY_LATER));
	/* (revoked or signed out on the site: no key either, from then on) */
	memset(&state, 0, sizeof(state));
	identity_have_certificate(&state, &device, 0, now);
	CHECK(identity_answered(&state, 403, "{\"revoked\": true}", now) == IDENTITY_ANSWER_OFF);
	CHECK(state.signed_out && !identity_key_allowed(&state, now) && identity_due(&state, 1, now) == IDENTITY_STEP_NONE);
	/* (signed out by its player: neither, and nothing due) */
	memset(&state, 0, sizeof(state));
	state.signed_out = 1;
	CHECK(!identity_key_allowed(&state, now) && identity_due(&state, 1, now) == IDENTITY_STEP_NONE);
}

/* more than one account on a copy: delta_accounts.txt's slots */
/* a UCS-2 name from ASCII, and back, for the name tests */
static void wide_name(const char *text, unsigned short *out, int size)
{
	int index;

	for (index = 0; index < size - 1 && text[index]; index++)
		out[index] = (unsigned char)text[index];
	out[index] = 0;
}

static int shows_as(const unsigned short *name, const char *expected)
{
	unsigned short out[16];
	int index, length = identity_unverified_name(name, 12, out, 16);

	for (index = 0; expected[index]; index++)
	{
		if (index >= length || out[index] != (unsigned char)expected[index])
			return 0;
	}
	return index == length;
}

/* an unverified name can't carry the @ mark, wherever it puts it, nor read
as a verified handle, whole or cut to a game name's 11 letters */
static void test_unverified_names(void)
{
	static const char *const faked[] = { "@Milenko", "@@Milenko", " @Milenko", ".@Milenko", "Mile@nko@", " . @Milenko" };
	unsigned short name[16];
	int index;

	for (index = 0; index < (int)(sizeof(faked) / sizeof(faked[0])); index++)
	{
		wide_name(faked[index], name, 16);
		CHECK(shows_as(name, "Milenko"));
	}
	/* (the other @s, the middle dot and invisible marks in front) */
	name[0] = 0xB7, name[1] = '@', name[2] = 'M', name[3] = 'k', name[4] = 0;
	CHECK(shows_as(name, "Mk"));
	name[0] = 0x200B, name[1] = 0xFF20, name[2] = 'M', name[3] = 0xFE6B, name[4] = 'k', name[5] = 0;
	CHECK(shows_as(name, "Mk"));
	/* (the Delta mark, and the triangles that read as it, at the front) */
	name[0] = 0x394, name[1] = 'M', name[2] = 'k', name[3] = 0;
	CHECK(shows_as(name, "Mk"));
	name[0] = ' ', name[1] = 0x25B2, name[2] = ' ', name[3] = 0x2206, name[4] = 'M', name[5] = 'k', name[6] = 0;
	CHECK(shows_as(name, "Mk"));
	/* (a Delta inside a name is a letter: kept) */
	name[0] = 'M', name[1] = 0x394, name[2] = 'k', name[3] = 0;
	CHECK(identity_unverified_name(name, 12, name + 8, 8) == 3 && name[9] == 0x394);
	/* (names without them as they are; dots after the front kept) */
	wide_name("[RvB] Tex", name, 16);
	CHECK(shows_as(name, "[RvB] Tex"));
	wide_name("Mr. Tex.", name, 16);
	CHECK(shows_as(name, "Mr. Tex."));
	wide_name("@@ .", name, 16);
	CHECK(identity_unverified_name(name, 12, name + 8, 8) == 0);
	CHECK(identity_unverified_name(NULL, 0, name, 4) == 0 && !name[0]);

	/* (as written, as shown with the @ gone, and against the cut) */
	CHECK(identity_impersonates("Milenko", "milenko", 11));
	CHECK(identity_impersonates("Mi1enk0", "milenko", 11));
	CHECK(identity_impersonates("@Milenko", "milenko", 11));
	CHECK(identity_impersonates(" @Milenko", "milenko", 11));
	CHECK(identity_impersonates(".@Milenko", "milenko", 11));
	/* (a Delta typed in front: the mark is no letter of the key) */
	CHECK(identity_impersonates("\xCE\x94Milenko", "milenko", 11));
	CHECK(identity_impersonates("\xC2\xB7@Milenko", "milenko", 11));
	CHECK(identity_impersonates("\xEF\xBC\xA0Milenko", "milenko", 11));
	CHECK(identity_impersonates("S@rge", "sarge", 11));
	CHECK(identity_impersonates("MilenkoTheG", "milenkothegreat", 11));
	CHECK(identity_impersonates("MilenkoTheGreat", "milenkothegreat", 11));
	CHECK(!identity_impersonates("MilenkoTheG", "milenkothegreat", 0));
	CHECK(!identity_impersonates("Milenko", "milenkothegreat", 11));
	CHECK(!identity_impersonates("Sarge", "milenko", 11));
	CHECK(!identity_impersonates("M", "m", 11));
	CHECK(!identity_impersonates("Milenko", "", 11));
}

static void test_account_slots(void)
{
	static const char text[] = "player1 2\nOther Player 3\r\nbad -1\nfour 16\nplayer 1x\n";
	char key[64], cert[64];

	CHECK(identity_account_slot(text, "player1") == 2);
	CHECK(identity_account_slot(text, "Other Player") == 3);
	CHECK(identity_account_slot(text, "player") == 0);
	CHECK(identity_account_slot(text, "bad") == 0 && identity_account_slot(text, "four") == 0);
	CHECK(identity_account_slot(text, "nobody") == 0 && identity_account_slot("", "player1") == 0);
	identity_slot_files(0, key, cert, sizeof(key));
	CHECK(!strcmp(key, "delta_device.key") && !strcmp(cert, "delta_device.cert"));
	identity_slot_files(2, key, cert, sizeof(key));
	CHECK(!strcmp(key, "delta_device_2.key") && !strcmp(cert, "delta_device_2.cert"));
}

static void test_issuer(void)
{
	struct identity_issuer issuer;
	unsigned char broken[IDENTITY_ISSUER_SIZE + 1];

	CHECK(identity_parse_issuer(issuer_data, IDENTITY_ISSUER_SIZE, &issuer));
	CHECK(issuer.valid_from == VECTOR_VALID_FROM && issuer.valid_until == VECTOR_VALID_UNTIL);
	CHECK(!memcmp(issuer.public_key, issuer_public, 32) && same_hex(issuer.key_id, 8, VECTOR_ISSUER_KEY_ID));
	/* (the root key in the second place, an all-zero one in the first) */
	CHECK(identity_check_issuer(issuer_data, IDENTITY_ISSUER_SIZE, (const unsigned char (*)[32])roots, 2,
		VECTOR_VALID_FROM + 1, &issuer) == IDENTITY_OK);
	CHECK(identity_check_issuer(issuer_data, IDENTITY_ISSUER_SIZE, (const unsigned char (*)[32])roots, 1,
		VECTOR_VALID_FROM + 1, &issuer) == IDENTITY_NOT_ROOT_SIGNED);
	CHECK(identity_check_issuer(issuer_data, IDENTITY_ISSUER_SIZE, (const unsigned char (*)[32])roots, 2,
		VECTOR_VALID_UNTIL + IDENTITY_GRACE, &issuer) == IDENTITY_EXPIRED);
	CHECK(identity_check_issuer(issuer_data, IDENTITY_ISSUER_SIZE, (const unsigned char (*)[32])roots, 2,
		VECTOR_VALID_FROM - IDENTITY_GRACE - 1, &issuer) == IDENTITY_NOT_YET);
	memcpy(broken, issuer_data, sizeof(issuer_data));
	broken[20] ^= 1;
	CHECK(identity_check_issuer(broken, IDENTITY_ISSUER_SIZE, (const unsigned char (*)[32])roots, 2,
		VECTOR_VALID_FROM + 1, &issuer) == IDENTITY_NOT_ROOT_SIGNED);
	memcpy(broken, issuer_data, sizeof(issuer_data));
	CHECK(!identity_parse_issuer(broken, IDENTITY_ISSUER_SIZE - 1, &issuer));
	CHECK(!identity_parse_issuer(broken, IDENTITY_ISSUER_SIZE + 1, &issuer));
	broken[4] = 2;
	CHECK(!identity_parse_issuer(broken, IDENTITY_ISSUER_SIZE, &issuer));
	broken[4] = 1;
	broken[6] = 1;
	CHECK(!identity_parse_issuer(broken, IDENTITY_ISSUER_SIZE, &issuer));
	broken[6] = 0;
	broken[0] = 'X';
	CHECK(!identity_parse_issuer(broken, IDENTITY_ISSUER_SIZE, &issuer));
	CHECK(!identity_parse_issuer(NULL, 0, &issuer));
}

static void test_device(void)
{
	struct identity_issuer issuer;
	struct identity_device device;
	unsigned char broken[IDENTITY_DEVICE_MOST + 1];
	unsigned char zero_roots[2][32];
	unsigned long now = VECTOR_ISSUED + 10;

	CHECK(identity_parse_device(device_data, device_size, &device));
	CHECK(device.platform == 1 && device.flags == IDENTITY_FLAG_LEGACY && !strcmp(device.handle, "milenko"));
	CHECK(device.issued == VECTOR_ISSUED && device.expires == VECTOR_EXPIRES && device.capabilities == 0x1F);
	CHECK(same_hex(device.serial, 8, "0102030405060708") && !memcmp(device.account, copy_id, 16));
	CHECK(!memcmp(device.public_key, device_public, 32) && !memcmp(device.device_id, device_id, 16));
	CHECK(device.body_size == device_size - 64);
	CHECK(identity_parse_issuer(issuer_data, IDENTITY_ISSUER_SIZE, &issuer));
	CHECK(identity_check_device(device_data, device_size, &issuer, now, &device) == IDENTITY_OK);
	CHECK(identity_check_device(device_data, device_size, &issuer, VECTOR_EXPIRES + IDENTITY_GRACE, &device) ==
		IDENTITY_EXPIRED);
	CHECK(identity_check_device(device_data, device_size, &issuer, VECTOR_EXPIRES + IDENTITY_GRACE - 1, &device) ==
		IDENTITY_OK);
	/* the chain, with the root keys and without */
	CHECK(identity_accept(device_data, device_size, issuer_data, IDENTITY_ISSUER_SIZE,
		(const unsigned char (*)[32])roots, 2, device_public, now, &device) == IDENTITY_OK);
	memset(zero_roots, 0, sizeof(zero_roots));
	CHECK(identity_accept(device_data, device_size, issuer_data, IDENTITY_ISSUER_SIZE,
		(const unsigned char (*)[32])zero_roots, 2, device_public, now, &device) == IDENTITY_OK);
	CHECK(identity_accept(device_data, device_size, issuer_data, IDENTITY_ISSUER_SIZE,
		(const unsigned char (*)[32])zero_roots, 2, issuer_public, now, &device) == IDENTITY_NOT_MINE);
	memcpy(zero_roots[0], issuer_public, 32);
	CHECK(identity_accept(device_data, device_size, issuer_data, IDENTITY_ISSUER_SIZE,
		(const unsigned char (*)[32])zero_roots, 2, device_public, now, &device) == IDENTITY_NOT_ROOT_SIGNED);
	/* (a saved one past its time, checked as of when it was issued) */
	CHECK(identity_accept(device_data, device_size, issuer_data, IDENTITY_ISSUER_SIZE,
		(const unsigned char (*)[32])roots, 2, device_public, VECTOR_EXPIRES + 86400, &device) == IDENTITY_EXPIRED);
	CHECK(identity_accept(device_data, device_size, issuer_data, IDENTITY_ISSUER_SIZE,
		(const unsigned char (*)[32])roots, 2, device_public, VECTOR_ISSUED, &device) == IDENTITY_OK);
	memcpy(broken, device_data, (size_t)device_size);
	broken[device_size - 1] ^= 1;
	CHECK(identity_accept(broken, device_size, issuer_data, IDENTITY_ISSUER_SIZE,
		(const unsigned char (*)[32])roots, 2, device_public, VECTOR_ISSUED, &device) ==
		IDENTITY_NOT_ISSUER_SIGNED);
		/* (a changed handle breaks the signature) */
	memcpy(broken, device_data, (size_t)device_size);
	broken[IDENTITY_DEVICE_HEADER] = 'n';
	CHECK(identity_check_device(broken, device_size, &issuer, now, &device) == IDENTITY_NOT_ISSUER_SIGNED);
	memcpy(broken, device_data, (size_t)device_size);
	broken[24] ^= 1;
	CHECK(identity_check_device(broken, device_size, &issuer, now, &device) == IDENTITY_OTHER_ISSUER);
	/* malformed: sizes, magic, format, handle length and characters, times */
	memcpy(broken, device_data, (size_t)device_size);
	CHECK(!identity_parse_device(broken, device_size - 1, &device));
	CHECK(!identity_parse_device(broken, device_size + 1, &device));
	broken[4] = 2;
	CHECK(!identity_parse_device(broken, device_size, &device));
	broken[4] = 1;
	broken[6] = 2;
	CHECK(!identity_parse_device(broken, device_size, &device));
	broken[6] = 21;
	CHECK(!identity_parse_device(broken, device_size, &device));
	broken[6] = 7;
	broken[IDENTITY_DEVICE_HEADER] = 'M';
	CHECK(!identity_parse_device(broken, device_size, &device));
	broken[IDENTITY_DEVICE_HEADER] = 'm';
	memcpy(broken + 12, broken + 8, 4);
	CHECK(!identity_parse_device(broken, device_size, &device));
	CHECK(!identity_parse_device(NULL, 0, &device));
	CHECK(!identity_parse_device(device_data, IDENTITY_DEVICE_HEADER + 63, &device));
}

static void test_revoked(void)
{
	struct identity_issuer issuer;
	struct identity_revoked revoked;
	unsigned char broken[256], other[16];

	CHECK(identity_parse_issuer(issuer_data, IDENTITY_ISSUER_SIZE, &issuer));
	/* (an issuer past its dates: only the list kept, never one fetched) */
	CHECK(identity_revoked_issuer_usable(IDENTITY_OK, 0) && identity_revoked_issuer_usable(IDENTITY_OK, 1));
	CHECK(identity_revoked_issuer_usable(IDENTITY_EXPIRED, 1) && !identity_revoked_issuer_usable(IDENTITY_EXPIRED, 0));
	CHECK(!identity_revoked_issuer_usable(IDENTITY_NOT_ROOT_SIGNED, 1) && !identity_revoked_issuer_usable(IDENTITY_MALFORMED, 0));
	CHECK(identity_check_revoked(revoked_data, revoked_size, &issuer, &revoked) == IDENTITY_OK);
	CHECK(revoked.serial == VECTOR_ISSUED && revoked.issued == VECTOR_ISSUED &&
		revoked.next_update == VECTOR_ISSUED + 86400 && revoked.count == 2);
	CHECK(identity_is_revoked(&revoked, device_id));
	memset(other, 0x11, sizeof(other));
	CHECK(identity_is_revoked(&revoked, other));
	other[0] = 0x12;
	CHECK(!identity_is_revoked(&revoked, other));
	memcpy(broken, revoked_data, (size_t)revoked_size);
	broken[40] ^= 1;
	CHECK(identity_check_revoked(broken, revoked_size, &issuer, &revoked) == IDENTITY_NOT_ISSUER_SIGNED);
	/* (out of order) */
	memcpy(broken, revoked_data, (size_t)revoked_size);
	memcpy(broken + 32, revoked_data + 48, 16);
	memcpy(broken + 48, revoked_data + 32, 16);
	CHECK(!identity_parse_revoked(broken, revoked_size, &revoked));
	memcpy(broken, revoked_data, (size_t)revoked_size);
	broken[28] = 3;
	CHECK(!identity_parse_revoked(broken, revoked_size, &revoked));
	broken[28] = 2;
	broken[30] = 1;
	CHECK(!identity_parse_revoked(broken, revoked_size, &revoked));
	CHECK(!identity_parse_revoked(revoked_data, 95, &revoked));
	/* (an issuer past its dates: its signature still checked, the issuer
	read, and the list it signed still checks against it: browser.c uses
	it, and says it is stale) */
	CHECK(identity_check_issuer(issuer_data, IDENTITY_ISSUER_SIZE, roots, 2, VECTOR_VALID_UNTIL + IDENTITY_GRACE + 1,
		&issuer) == IDENTITY_EXPIRED);
	CHECK(identity_check_revoked(revoked_data, revoked_size, &issuer, &revoked) == IDENTITY_OK);
	{
		unsigned char none[2][IDENTITY_KEY_SIZE];

		memset(none, 0, sizeof(none));
		CHECK(identity_check_issuer(issuer_data, IDENTITY_ISSUER_SIZE, none, 2, VECTOR_VALID_UNTIL + IDENTITY_GRACE + 1,
			&issuer) == IDENTITY_NOT_ROOT_SIGNED);
	}
}

static void test_text(void)
{
	char text[1024], out[64];
	unsigned char signature[64], certificate[IDENTITY_DEVICE_MOST], issuer[IDENTITY_ISSUER_SIZE];
	unsigned char got_nonce[16];
	int length, certificate_size, issuer_size;
	unsigned long lifetime = 0;

	memset(signature, 0xab, sizeof(signature));
	length = identity_signed_body(text, sizeof(text), device_id, VECTOR_NONCE, signature);
	CHECK(length > 0 && strstr(text, "\"device_id\": \"" VECTOR_DEVICE_ID "\"") &&
		strstr(text, "\"nonce\": \"" VECTOR_NONCE "\""));
	CHECK(identity_signed_body(text, 40, device_id, VECTOR_NONCE, signature) < 0);
	length = identity_enroll_body(text, sizeof(text), VECTOR_PLAYER_KEY, device_public, VECTOR_NONCE, signature,
		"pc_windows", "0.9.1 \"x\"");
	CHECK(length > 0 && strstr(text, "\"key\": \"" VECTOR_PLAYER_KEY "\"") &&
		strstr(text, "\"device_key\": \"" VECTOR_DEVICE_PUBLIC "\"") && strstr(text, "\"build\": \"0.9.1 x\"") &&
		strstr(text, "\"client_id\": \"chupathingy\""));
	CHECK(identity_json_member(text, "platform", out, sizeof(out)) == 10 && !strcmp(out, "pc_windows"));

	/* the answers */
	CHECK(identity_nonce_answer("{\"nonce\": \"" VECTOR_NONCE "\", \"expires_in\": 300}", out, got_nonce) &&
		!memcmp(got_nonce, nonce, 16));
	CHECK(!identity_nonce_answer("{\"nonce\": \"0011\"}", out, got_nonce));
	CHECK(!identity_nonce_answer("ok\n", out, got_nonce));
	snprintf(text, sizeof(text), "{\"device_id\": \"%s\", \"certificate\": \"%s\", \"issuer\": \"%s\", "
		"\"account\": \"x\", \"handle\": \"milenko\", \"expires\": 1, \"refresh_after\": 302400}",
		VECTOR_DEVICE_ID, VECTOR_DEVICE_CERTIFICATE, VECTOR_ISSUER_CERTIFICATE);
	CHECK(identity_certificate_answer(text, certificate, &certificate_size, issuer, &issuer_size) &&
		certificate_size == device_size && !memcmp(certificate, device_data, (size_t)device_size) &&
		issuer_size == IDENTITY_ISSUER_SIZE && !memcmp(issuer, issuer_data, IDENTITY_ISSUER_SIZE));
	CHECK(!identity_certificate_answer("{\"certificate\": \"00\"}", certificate, &certificate_size, issuer,
		&issuer_size));
	CHECK(identity_token_answer("{\"token\": \"d1.abc_-X\", \"expires_in\": 3600, \"scopes\": [\"play\", \"x\"]}",
		out, sizeof(out), &lifetime) && !strcmp(out, "d1.abc_-X") && lifetime == 3600);
	CHECK(!identity_token_answer("{\"token\": \"a b\", \"expires_in\": 3600}", out, sizeof(out), &lifetime));
	CHECK(!identity_token_answer("{\"token\": \"ab\"}", out, sizeof(out), &lifetime));

	/* the JSON: nested values skipped, strings with escapes, members not found */
	CHECK(identity_json_member("{\"a\": {\"enroll\": true}, \"b\": \"x\\\"}\", \"enroll\" : true }", "enroll", out,
		sizeof(out)) == 4);
	CHECK(identity_json_true("{\"error\": \"unknown device\", \"enroll\": true}", "enroll"));
	CHECK(!identity_json_true("{\"error\": \"enroll\"}", "enroll"));
	CHECK(identity_json_member("{\"a\": [1, {\"b\": 2}], \"c\": 3}", "c", out, sizeof(out)) == 1 && out[0] == '3');
	CHECK(identity_json_member("{\"a\": \"cut", "a", out, sizeof(out)) < 0);
	CHECK(identity_json_member("not json", "a", out, sizeof(out)) < 0);
	CHECK(identity_json_member("{\"a\": \"long value\"}", "a", out, 5) == 4 && !strcmp(out, "long"));
}

static void test_audience(void)
{
	char audience[IDENTITY_AUDIENCE_MOST + 1], longest[300];
	unsigned char message[IDENTITY_MESSAGE_MOST];
	int index;

	CHECK(identity_audience("https://stats.chupathingy.org", audience, sizeof(audience)) == 21 &&
		!strcmp(audience, VECTOR_AUDIENCE));
	CHECK(identity_audience("https://Stats.Chupathingy.ORG:443/v1/?x#y", audience, sizeof(audience)) == 21 &&
		!strcmp(audience, VECTOR_AUDIENCE));
	CHECK(identity_audience("http://127.0.0.1:8080/", audience, sizeof(audience)) == 9 &&
		!strcmp(audience, "127.0.0.1"));
	CHECK(!identity_audience("stats.chupathingy.org", audience, sizeof(audience)) && !audience[0]);
	CHECK(!identity_audience("https://", audience, sizeof(audience)));
	CHECK(!identity_audience("https://user@host/", audience, sizeof(audience)));
	CHECK(!identity_audience("https://[::1]:80/", audience, sizeof(audience)));
	CHECK(!identity_audience("https://stats.chupathingy.org", audience, 10));
	/* (the longest audience still fits a message; one longer is none) */
	memset(longest, 'a', sizeof(longest));
	longest[IDENTITY_AUDIENCE_MOST] = 0;
	CHECK(identity_enroll_message(longest, nonce, copy_id, device_public, message) ==
		29 + 1 + IDENTITY_AUDIENCE_MOST + 64);
	longest[IDENTITY_AUDIENCE_MOST] = 'a';
	longest[IDENTITY_AUDIENCE_MOST + 1] = 0;
	CHECK(identity_enroll_message(longest, nonce, copy_id, device_public, message) < 0);
	CHECK(identity_token_message("", nonce, device_id, message) < 0);
	CHECK(identity_refresh_message("Stats.example", nonce, device_id, message) < 0);
	for (index = 0; index < 3; index++)
		CHECK(identity_token_message("a/b", nonce, device_id, message) < 0);
}

static void test_schedule(void)
{
	struct identity_state state;
	struct identity_device device;
	char token[IDENTITY_TOKEN_SIZE];
	unsigned long now = 1000000;
	int index;

	memset(&state, 0, sizeof(state));
	/* no certificate: enroll, with a player key only */
	CHECK(identity_due(&state, 0, now) == IDENTITY_STEP_NONE);
	CHECK(identity_due(&state, 1, now) == IDENTITY_STEP_ENROLL);
	CHECK(!identity_token(&state, now, token, sizeof(token)) && !identity_token_due(&state, now));
	/* an older site (no identity paths): an hour, and no tokens to it */
	CHECK(identity_answered(&state, 404, "{\"error\": \"not found\"}", now) == IDENTITY_ANSWER_LATER);
	CHECK(identity_due(&state, 1, now + IDENTITY_LATER - 1) == IDENTITY_STEP_NONE);
	CHECK(identity_due(&state, 1, now + IDENTITY_LATER) == IDENTITY_STEP_ENROLL);
	CHECK(state.tokens_off_until == now + IDENTITY_LATER);
	now += IDENTITY_LATER;
	/* no answer: a minute, then two */
	CHECK(identity_answered(&state, 0, "", now) == IDENTITY_ANSWER_RETRY);
	CHECK(state.wait_until == now + 60);
	CHECK(identity_answered(&state, 500, "", now) == IDENTITY_ANSWER_RETRY);
	CHECK(state.wait_until == now + 120);
	/* busy: ten minutes, doubling, to an hour */
	CHECK(identity_answered(&state, 503, "{\"error\": \"busy\"}", now) == IDENTITY_ANSWER_BUSY);
	CHECK(state.wait_until == now + 600);
	CHECK(identity_answered(&state, 503, "", now) == IDENTITY_ANSWER_BUSY && state.wait_until == now + 1200);
	for (index = 0; index < 6; index++)
		identity_answered(&state, 503, "", now);
	CHECK(state.wait_until == now + IDENTITY_LATER);
	now += IDENTITY_LATER;
	CHECK(identity_answered(&state, 200, "{}", now) == IDENTITY_ANSWER_OK && !state.failures && !state.busy_failures);

	/* a certificate: refreshed after the site's refresh_after (a duration) */
	memset(&device, 0, sizeof(device));
	device.issued = now;
	device.expires = now + 7 * 86400;
	identity_have_certificate(&state, &device, 1000, now);
	CHECK(state.refresh_at == now + 1000);
	CHECK(identity_due(&state, 1, now + 999) == IDENTITY_STEP_NONE);
	CHECK(identity_due(&state, 1, now + 1000) == IDENTITY_STEP_REFRESH);
	/* (none given: half its life; never past its end) */
	identity_have_certificate(&state, &device, 0, now);
	CHECK(state.refresh_at == now + 7 * 43200);
	identity_have_certificate(&state, &device, 30 * 86400, now);
	CHECK(state.refresh_at == device.expires);
	/* an expired saved certificate refreshes, never enrolls */
	identity_have_certificate(&state, &device, 0, now);
	CHECK(identity_due(&state, 1, device.expires + 86400) == IDENTITY_STEP_REFRESH);

	/* a token: due, then used until near its end */
	CHECK(identity_token_due(&state, now));
	identity_set_token(&state, "d1.token", 3600, now);
	CHECK(!identity_token_due(&state, now) && identity_token_due(&state, now + 3600 - IDENTITY_TOKEN_MARGIN));
	CHECK(identity_token(&state, now, token, sizeof(token)) && !strcmp(token, "d1.token"));
	CHECK(!identity_token(&state, now + 3600 - 60, token, sizeof(token)));
	CHECK(!identity_token(&state, now, token, 4));
	/* a request answered: it stands, whatever it was, a 400 too */
	CHECK(identity_token_sent(&state, 200, "ok", now) == IDENTITY_SENT_DONE);
	CHECK(identity_token_sent(&state, 404, "{}", now) == IDENTITY_SENT_DONE);
	CHECK(identity_token_sent(&state, 0, "", now) == IDENTITY_SENT_DONE);
	CHECK(identity_token_sent(&state, 403, "{\"error\": \"locked\"}", now) == IDENTITY_SENT_DONE);
	CHECK(identity_token_sent(&state, 401, "{\"error\": \"Sign in first.\"}", now) == IDENTITY_SENT_DONE);
	CHECK(identity_token_sent(&state, 400, "bad claim\n", now) == IDENTITY_SENT_DONE);
	CHECK(identity_token_sent(&state, 400, "{\"error\": \"bad request\"}", now) == IDENTITY_SENT_DONE);
	CHECK(state.token[0] && identity_token(&state, now, token, sizeof(token)));
	/* a 401 for the token: dropped, a new one asked for */
	CHECK(identity_token_sent(&state, 401, "{\"error\": \"token\", \"token\": \"expired\"}", now) ==
		IDENTITY_SENT_NEW_TOKEN && !state.token[0] && identity_token_due(&state, now));
	identity_set_token(&state, "d1.token", 3600, now);
	CHECK(identity_token_sent(&state, 401, "{\"token\": \"unknown\"}", now) == IDENTITY_SENT_UNKNOWN_TOKEN &&
		!state.token[0]);
	identity_set_token(&state, "d1.token", 3600, now);
	/* an older site (its nonce path a 404): no tokens to it for an hour */
	CHECK(identity_answered(&state, 404, "", now) == IDENTITY_ANSWER_LATER);
	CHECK(!identity_token(&state, now + 1, token, sizeof(token)) && !identity_token_due(&state, now + 1));
	CHECK(identity_token_due(&state, now + IDENTITY_LATER));
	state.wait_until = 0;
	/* the site forgot the device: enroll again */
	CHECK(identity_answered(&state, 404, "{\"error\": \"unknown device\", \"enroll\": true}", now) ==
		IDENTITY_ANSWER_ENROLL);
	CHECK(identity_due(&state, 1, now) == IDENTITY_STEP_ENROLL && !identity_token_due(&state, now));
	identity_have_certificate(&state, &device, 0, now);
	/* revoked: the certificate and the token dropped, the key this run */
	identity_set_token(&state, "d1.token", 3600, now);
	CHECK(identity_answered(&state, 403, "{\"error\": \"x\", \"revoked\": true}", now) == IDENTITY_ANSWER_OFF);
	CHECK(state.off && !state.has_certificate && !state.token[0]);
	CHECK(identity_due(&state, 1, now + 100000) == IDENTITY_STEP_NONE && !identity_token(&state, now, token, 201));
	memset(&state, 0, sizeof(state));
	CHECK(identity_answered(&state, 403, "{\"signed_out\": true}", now) == IDENTITY_ANSWER_OFF);

	/* another copy's device key: a new key once; the second time, a day */
	memset(&state, 0, sizeof(state));
	CHECK(identity_answered(&state, 409, "{\"error\": \"x\", \"other_copy\": true}", now) == IDENTITY_ANSWER_NEW_KEY);
	CHECK(state.new_key_made && state.must_enroll && identity_due(&state, 1, now) == IDENTITY_STEP_ENROLL);
	CHECK(identity_answered(&state, 409, "{\"other_copy\": true}", now) == IDENTITY_ANSWER_DAY);
	CHECK(state.wait_until == now + IDENTITY_DAY);
	/* an unknown copy: within an hour, sooner after a confirmed game; never given up */
	memset(&state, 0, sizeof(state));
	for (index = 0; index < 2 * IDENTITY_DAY_TRIES; index++)
		CHECK(identity_answered(&state, 403, "{\"unknown_copy\": true}", now) == IDENTITY_ANSWER_UNKNOWN_COPY);
	CHECK(!state.off && !state.day_tries && state.wait_until == now + IDENTITY_LATER && state.waiting_for_game);
	identity_confirmed(&state, now + 10);
	CHECK(state.wait_until == now + 10 + IDENTITY_RETRY_FIRST && !state.waiting_for_game);
	identity_confirmed(&state, now + 20);
	CHECK(state.wait_until == now + 10 + IDENTITY_RETRY_FIRST);
	CHECK(identity_due(&state, 1, now + 10 + IDENTITY_RETRY_FIRST) == IDENTITY_STEP_ENROLL);
	/* locked, too many: a day, given up after a few */
	memset(&state, 0, sizeof(state));
	for (index = 0; index < IDENTITY_DAY_TRIES; index++)
	{
		CHECK(!state.off);
		CHECK(identity_answered(&state, 403, "{\"locked\": true}", now) == IDENTITY_ANSWER_DAY);
	}
	CHECK(state.off && identity_due(&state, 1, now + 10 * IDENTITY_DAY) == IDENTITY_STEP_NONE);
	memset(&state, 0, sizeof(state));
	CHECK(identity_answered(&state, 403, "{\"error\": \"locked\", \"locked\": true}", now) == IDENTITY_ANSWER_DAY);
	CHECK(identity_answered(&state, 409, "{\"too_many\": true}", now) == IDENTITY_ANSWER_DAY && !state.off);
	/* (a certificate in hand keeps it through a locked token answer) */
	memset(&state, 0, sizeof(state));
	identity_have_certificate(&state, &device, 0, now);
	CHECK(identity_answered(&state, 403, "{\"locked\": true}", now) == IDENTITY_ANSWER_DAY && state.has_certificate);
}

int main(void)
{
	load();
	test_keys();
	test_messages();
	test_proof();
	test_lookalikes();
	test_unverified_names();
	test_quick_connect();
	test_key_allowed();
	test_account_slots();
	test_issuer();
	test_device();
	test_revoked();
	test_text();
	test_audience();
	test_schedule();
	if (failures)
	{
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("identity: ok\n");
	return 0;
}
