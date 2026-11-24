#include "harness.h"

#include <signal.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wchar.h>

/* usprintf writes a string into a buffer its caller has not written yet
(game_engine_generate_title_string's score_string). Nothing may read the
buffer before it is written: here it is filled with non-zero characters
and ends at a page nobody may read. */

#include "config.inc"
/* (cseries.h's: a desktop build evaluates its assertions) */
#undef match_assert
#define match_assert(file, line, expr) do { if (!(expr)) { fprintf(stderr, "assertion %d: %s\n", line, #expr); exit(3); } } while (0)
/* (the game's three-argument vswprintf: the destination's room is the caller's to know) */
#define vswprintf(string, format, arglist) vswprintf(string, 64, format, arglist)
#include "under_test.inc"

static void read_past(int signal_number)
{
	static char const message[] = "check failed: usprintf read its destination before writing it\n";
	(void)signal_number;
	(void)!write(2, message, sizeof(message) - 1);
	_exit(1);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	long page = sysconf(_SC_PAGESIZE);
	unsigned char *pages = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	wchar_t *destination = (wchar_t *)(pages + page) - 16;
	int index;

	CHECK(pages != MAP_FAILED, "no memory");
	CHECK(!mprotect(pages + page, page, PROT_NONE), "no guard page");
	signal(SIGSEGV, read_past);
	for (index = 0; index < 16; index++)
		destination[index] = L'A';

	CASE("unwritten")
	{
		int result = usprintf(destination, L"%d", 25);

		CHECK(result == 2 && !wcscmp(destination, L"25"), "the score is written");
		return 0;
	}
	CASE("written")
	{
		destination[0] = 0;
		CHECK(usprintf(destination, L"%ls %d", L"kills", 7) == 7 && !wcscmp(destination, L"kills 7"),
			"a written destination is overwritten");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
