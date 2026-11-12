/*
STEAM_CONTENT_LIVE.C

A hand-run check of the Steam content client (port/linux/src/steam_content)
against Steam itself, outside the game:

	steam_content_live <data folder> signin [qr text file]
	steam_content_live <data folder> get <item id> <library folder>
	steam_content_live <data folder> status
	steam_content_live <data folder> signout

signin prints the QR's text (and writes it to the file, to be drawn as an
image) and waits up to five minutes for the scan. Built by
tools/test_steam_content.py --live.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/

#include "../src/steam_content/steam_content.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void log_line(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	fputs("[log] ", stdout);
	vprintf(format, arguments);
	fputc('\n', stdout);
	fflush(stdout);
	va_end(arguments);
}

int main(int argc, char **argv)
{
	struct steam_content_host host;
	char persona[STEAM_CONTENT_PERSONA_SIZE];

	if (argc < 3)
	{
		fprintf(stderr, "usage: %s <data folder> signin|get|status|signout ...\n", argv[0]);
		return 2;
	}
	memset(&host, 0, sizeof(host));
	snprintf(host.data_folder, sizeof(host.data_folder), "%s", argv[1]);
	host.log = log_line;
	steam_content_init(&host);
	if (!strcmp(argv[2], "status"))
	{
		int linked = steam_content_linked(persona, sizeof(persona));

		printf("linked=%d persona=%s\n", linked, persona);
		return 0;
	}
	if (!strcmp(argv[2], "signout"))
	{
		steam_content_sign_out();
		/* (the revoke at Steam waited for) */
		steam_content_shutdown();
		return 0;
	}
	if (!strcmp(argv[2], "signin"))
	{
		char text[STEAM_CONTENT_QR_SIZE], shown[STEAM_CONTENT_QR_SIZE] = "";
		int state, error, scanned = 0;

		steam_content_sign_in_qr();
		for (;;)
		{
			state = steam_content_sign_in_poll(text, sizeof(text), &error);
			if ((state == STEAM_CONTENT_QR_WAITING || state == STEAM_CONTENT_QR_SCANNED) && strcmp(text, shown))
			{
				snprintf(shown, sizeof(shown), "%s", text);
				printf("QR %s\nSCAN THE QR\n", text);
				fflush(stdout);
				if (argc > 3)
				{
					FILE *file = fopen(argv[3], "w");

					if (file)
					{
						fputs(text, file);
						fclose(file);
					}
				}
			}
			if (state == STEAM_CONTENT_QR_SCANNED && !scanned)
			{
				scanned = 1;
				printf("scanned: approve on the phone\n");
				fflush(stdout);
			}
			if (state == STEAM_CONTENT_QR_DONE)
			{
				steam_content_linked(persona, sizeof(persona));
				printf("SIGNED IN (persona %s)\n", persona[0] ? "known" : "not given");
				return 0;
			}
			if (state == STEAM_CONTENT_QR_FAILED)
			{
				printf("FAILED: %s\n", steam_content_error_text(error));
				return 1;
			}
			usleep(200000);
		}
	}
	if (!strcmp(argv[2], "get") && argc >= 5)
	{
		unsigned long long id = strtoull(argv[3], NULL, 10);
		struct steam_content_progress progress;
		time_t last = 0;

		if (!steam_content_get_item(id, argv[4]))
		{
			printf("FAILED: not signed in, or a bad id\n");
			return 1;
		}
		for (;;)
		{
			steam_content_progress(id, &progress);
			if (time(NULL) != last)
			{
				last = time(NULL);
				printf("state %d: %llu / %llu\n", progress.state, progress.done, progress.total);
				fflush(stdout);
			}
			if (progress.state == STEAM_CONTENT_ITEM_INSTALLED)
			{
				printf("INSTALLED %llu bytes\n", progress.total);
				return 0;
			}
			if (progress.state == STEAM_CONTENT_ITEM_FAILED)
			{
				printf("FAILED: %s\n", steam_content_error_text(progress.error));
				return 1;
			}
			usleep(100000);
		}
	}
	fprintf(stderr, "unknown command\n");
	return 2;
}
