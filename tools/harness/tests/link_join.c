/* p2p.c's link held for the menus (hold_link_join, p2p_take_link_join): an
opened invite link is held, taken once, a non-invite and a headless copy's
are not held, and holding never takes p2p_lock (the p2p thread holds it
while it hands a link over). test_link_join.py takes the code. */
#include "harness.h"
#include <pthread.h>
#include <stdarg.h>

enum { P2P_KEY_HASH_SIZE = 16, P2P_TOKEN_SIZE = 16 };

/* the p2p thread's lock, held while a link is handed over: an error-checking
mutex, so taking it again fails rather than hangs */
static pthread_mutex_t p2p_lock;
static int headless;

static int browser_headless(void) { return headless; }
static void platform_log(const char *format, ...) { (void)format; }
static const char *p2p_invite_log_text(const char *invite, char *text, int size)
{
	snprintf(text, (size_t)size, "%s", invite);
	return text;
}
/* an invite: halo://join/ and anything but "old" after it */
static int p2p_parse_invite(const char *text, unsigned char *hash, unsigned char *token, unsigned char *check)
{
	(void)hash; (void)token; (void)check;
	if (strncmp(text, "halo://join/", 12))
		return 0;
	return strcmp(text + 12, "old") ? 1 : -1;
}

#include "under_test.inc"

int main(int argc, char **argv)
{
	const char *case_name = argv[1];
	pthread_mutexattr_t attributes;
	char text[256];
	int open = 0;

	pthread_mutexattr_init(&attributes);
	pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_ERRORCHECK);
	pthread_mutex_init(&p2p_lock, &attributes);
	CASE("held")
	{
		CHECK(hold_link_join("halo://join/abc", 1), "an invite not held");
		CHECK(p2p_take_link_join(text, sizeof(text), &open), "the held link not taken");
		CHECK(!strcmp(text, "halo://join/abc") && open == 1, "taken as %s, open %d", text, open);
		CHECK(!p2p_take_link_join(text, sizeof(text), &open), "the link taken twice");
	}
	CASE("refused")
	{
		CHECK(!hold_link_join("hello", 0), "a non-invite held");
		CHECK(!hold_link_join("halo://join/old", 0), "an older version's invite held");
		headless = 1;
		CHECK(!hold_link_join("halo://join/abc", 0), "held on a headless copy");
		CHECK(!p2p_take_link_join(text, sizeof(text), &open), "something taken");
	}
	CASE("p2p_lock_held")
	{
		/* (as handoff_readable, Discord's Join and poll_invite_file call it) */
		CHECK(pthread_mutex_lock(&p2p_lock) == 0, "the lock");
		CHECK(hold_link_join("halo://join/abc", 0), "an invite not held under p2p_lock");
		CHECK(pthread_mutex_trylock(&p2p_lock) != 0, "p2p_lock let go");
		CHECK(pthread_mutex_unlock(&p2p_lock) == 0, "p2p_lock taken again inside");
		CHECK(p2p_take_link_join(text, sizeof(text), &open), "not taken");
	}
	return 0;
}
