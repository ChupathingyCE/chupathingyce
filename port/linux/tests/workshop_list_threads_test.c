/*
WORKSHOP_LIST_THREADS_TEST.C

Steam's details and the list document read on several threads at once
(port/linux/src/workshop_list.c), as the Workshop's details, browse page,
picture and check threads do: each thread's answer stays its own. Built and
run by tools/test_workshop_list.py, under ThreadSanitizer where the compiler
has it; exits nonzero on a failure.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#define WORKSHOP_LIST_ONLY
#include "../src/workshop_browser.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

enum
{
	THREADS = 8,
	ROUNDS = 400,
};

static int failures[THREADS];

static void *reader(void *context)
{
	int number = (int)(long)context, round;
	char details[1024], list[512], title[64], description[64];
	struct workshop_browser_item item;

	snprintf(title, sizeof(title), "Title of thread %d", number);
	snprintf(description, sizeof(description), "Words of thread %d", number);
	snprintf(details, sizeof(details), "{\"response\":{\"publishedfiledetails\":[{\"publishedfileid\":\"%d\","
		"\"result\":1,\"consumer_app_id\":976730,\"title\":\"%s\",\"description\":\"[b]%s[/b]\","
		"\"tags\":[{\"tag\":\"Halo1\"}]}]}}", 100 + number, title, description);
	snprintf(list, sizeof(list), "{\"version\":1,\"items\":[{\"id\":\"%d\",\"title\":\"%s\",\"description\":\"%s\"}]}",
		200 + number, title, description);
	for (round = 0; round < ROUNDS; round++)
	{
		char id[16];

		snprintf(id, sizeof(id), "%d", 100 + number);
		if (workshop_browser_read_details(details, (int)strlen(details), &item, 1) != 1 || strcmp(item.id, id) ||
			strcmp(item.title, title) || strcmp(item.description, description))
		{
			failures[number]++;
		}
		snprintf(id, sizeof(id), "%d", 200 + number);
		if (workshop_browser_read_list(list, (int)strlen(list), &item, 1) != 1 || strcmp(item.id, id) ||
			strcmp(item.title, title) || strcmp(item.description, description))
		{
			failures[number]++;
		}
	}
	return NULL;
}

int main(void)
{
	pthread_t threads[THREADS];
	int index, total = 0;

	for (index = 0; index < THREADS; index++)
	{
		if (pthread_create(&threads[index], NULL, reader, (void *)(long)index) != 0)
		{
			printf("FAIL: no thread %d\n", index);
			return 1;
		}
	}
	for (index = 0; index < THREADS; index++)
		pthread_join(threads[index], NULL);
	for (index = 0; index < THREADS; index++)
	{
		if (failures[index])
			printf("FAIL thread %d: %d answers not its own\n", index, failures[index]);
		total += failures[index];
	}
	return total ? 1 : 0;
}
