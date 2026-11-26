#include "harness.h"
#include <stddef.h>

#pragma GCC diagnostic ignored "-Wmultichar"
/* (the game's 16-bit wchar_t: -fshort-wchar) */
#define wchar_t unsigned short
#define csmemcpy memcpy
#define csmemmove memmove
#define csmemset memset
static int failed_allocations;
/* a check that halts in the game halts the test (a CHECK); one that doesn't is counted */
#define match_vassert(file, line, condition, message) CHECK(condition, "halt: %s (#%d)", message, line)
static void display_assert(const char *message, const char *file, int line, boolean halt)
{
	CHECK(!halt, "halt: %s (#%d)", message, line);
	failed_allocations++;
}
static void system_exit(int code) { exit(code); }
#include "config.inc"

static struct
{
	struct stack_memory_pool pool;
	struct stack_memory_pool_block *more_blocks[15];
} medium;
static struct stack_memory_pool *widget_memory_pool = &medium.pool;
struct widget_instance { struct { struct { wchar_t *text; } text_box; } parameters; };
#include "under_test.inc"

static unsigned long long pool_storage[32];

static wchar_t const *wide(const char *text)
{
	static wchar_t buffers[4][128];
	static int next;
	wchar_t *buffer = buffers[next++ % 4];
	int index;

	for (index = 0; text[index] && index < 127; index++)
		buffer[index] = (wchar_t)text[index];
	buffer[index] = 0;
	return buffer;
}

static unsigned long length_of(wchar_t const *text)
{
	unsigned long length = 0;

	while (text[length])
		length++;
	return length;
}

static void draw(struct widget_instance *widget, const char *text)
{
	wchar_t const *string = wide(text);

	widget_instance_text_box_text_set(widget, string, length_of(string));
}

static boolean shows(struct widget_instance *widget, const char *text)
{
	wchar_t const *string = wide(text);

	return widget->parameters.text_box.text &&
		!memcmp(widget->parameters.text_box.text, string, 2 * (length_of(string) + 1));
}

int main(int argc, char **argv)
{
	const char *case_name = argv[1];
	struct widget_instance widget = { { { NULL } } };

	medium.pool.name = "widget_memory_pool";
	medium.pool.base_address = (byte *)pool_storage;
	medium.pool.size = sizeof(pool_storage);
	medium.pool.maximum_block_count = 16;
	medium.pool.next_block_index = NONE;
	CASE("grows")
	{
		/* each frame's string, the block grown as it needs */
		draw(&widget, "ONLINE");
		CHECK(shows(&widget, "ONLINE"), "first text");
		draw(&widget, "ONLINE GAMES");
		CHECK(shows(&widget, "ONLINE GAMES"), "longer text");
		draw(&widget, "ONLINE GAMES");
		CHECK(shows(&widget, "ONLINE GAMES"), "same text again");
		CHECK(medium.pool.block_count == 1, "%lu blocks for one text box", medium.pool.block_count);
	}
	CASE("full")
	{
		/* the pool can't give a bigger block: the text box keeps its own,
		and draws on the next frames (#1832) */
		struct widget_instance other = { { { NULL } } };
		int frame;

		draw(&widget, "SYSTEM LINK");
		draw(&other, "Find and join games hosted over the internet, on the list.");
		draw(&widget, "Find and join games hosted over the internet, on the community's game list, here.");
		CHECK(failed_allocations, "the pool was not full");
		for (frame = 0; frame < 3; frame++)
		{
			draw(&widget, "ONLINE");
			CHECK(shows(&widget, "ONLINE"), "frame %d after the full pool", frame);
		}
		CHECK(medium.pool.block_count == 2, "%lu blocks for two text boxes", medium.pool.block_count);
	}
	CASE("spinner")
	{
		/* a spinner's item text, made and given back each frame: none made
		when the pool is full, and none given back (#1832) */
		struct widget_instance other = { { { NULL } } };
		wchar_t *item_text;

		draw(&other, "Find and join games hosted over the internet, on the list.");
		draw(&widget, "SYSTEM LINK");
		item_text = pool_new_pointer(widget_memory_pool, 200, __FILE__, __LINE__);
		CHECK(!item_text && failed_allocations, "the pool was not full");
		spinner_item_text_dispose(item_text);
		item_text = pool_new_pointer(widget_memory_pool, 8, __FILE__, __LINE__);
		CHECK(item_text, "no room for a short item");
		spinner_item_text_dispose(item_text);
		CHECK(medium.pool.block_count == 2, "%lu blocks after the spinner's", medium.pool.block_count);
	}
	CASE("kept")
	{
		/* (the text it had, until a block can be had) */
		struct widget_instance other = { { { NULL } } };

		draw(&widget, "SYSTEM LINK");
		draw(&other, "Find and join games hosted over the internet, on the list.");
		draw(&widget, "Find and join games hosted over the internet, on the community's game list, here.");
		CHECK(failed_allocations, "the pool was not full");
		CHECK(shows(&widget, "SYSTEM LINK"), "the text box lost its text");
	}
	return 0;
}
