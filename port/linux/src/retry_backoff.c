/*
RETRY_BACKOFF.C

When to ask a web service again after it failed (retry_backoff.h).

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "retry_backoff.h"

#include <stdlib.h>
#include <string.h>

int retry_backoff_ready(const struct retry_backoff *backoff, unsigned long now)
{
	return !backoff->waiting || (long)(now - backoff->wait_until) >= 0;
}

unsigned long retry_backoff_left(const struct retry_backoff *backoff, unsigned long now)
{
	return retry_backoff_ready(backoff, now) ? 0 : backoff->wait_until - now;
}

void retry_backoff_failed(struct retry_backoff *backoff, unsigned long now, int status, long retry_after)
{
	unsigned long delay = backoff->delay ? backoff->delay * 2 : RETRY_BACKOFF_FIRST;

	if (delay > RETRY_BACKOFF_MOST)
		delay = RETRY_BACKOFF_MOST;
	/* (busy: as long as it asks, within reason) */
	if ((status == 429 || status >= 500) && retry_after > 0)
	{
		unsigned long asked = retry_after > RETRY_BACKOFF_LONGEST / 1000 ? RETRY_BACKOFF_LONGEST :
			(unsigned long)retry_after * 1000;

		if (asked > delay)
			delay = asked;
	}
	backoff->delay = delay;
	backoff->status = status;
	backoff->waiting = 1;
	backoff->wait_until = now + delay;
}

void retry_backoff_answered(struct retry_backoff *backoff)
{
	memset(backoff, 0, sizeof(*backoff));
}

long retry_backoff_after(const char *error)
{
	const char *at = error ? strstr(error, "Retry-After: ") : NULL;
	long seconds;

	if (!at)
		return 0;
	seconds = strtol(at + 13, NULL, 10);
	return seconds > 0 ? seconds : 0;
}
