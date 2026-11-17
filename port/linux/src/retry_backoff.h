/*
RETRY_BACKOFF.H

When to ask a web service again after it failed: 2 seconds, doubling up to
a minute, back to none once it answers; a busy answer (429, or 5xx) waits
at least as long as its Retry-After says (up to an hour). The Workshop's
requests to Steam use it (workshop_browser.c), so that no network never
means a request every frame.

No game types, so that the build machine can test it
(port/linux/tests/retry_backoff_test.c).

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef RETRY_BACKOFF_H
#define RETRY_BACKOFF_H

enum
{
	RETRY_BACKOFF_FIRST = 2000,
	RETRY_BACKOFF_MOST = 60000,
	/* the longest a Retry-After is taken at */
	RETRY_BACKOFF_LONGEST = 3600000,
};

struct retry_backoff
{
	/* waiting until wait_until (milliseconds, the caller's clock), and the
	last wait's length */
	int waiting;
	unsigned long wait_until;
	unsigned long delay;
	/* the last failure's HTTP status (0: no answer) */
	int status;
};

/* whether a request may go now */
int retry_backoff_ready(const struct retry_backoff *backoff, unsigned long now);
/* the milliseconds left to wait (0: none) */
unsigned long retry_backoff_left(const struct retry_backoff *backoff, unsigned long now);
/* a request failed with status (0: no answer) and retry_after seconds (0:
none said) */
void retry_backoff_failed(struct retry_backoff *backoff, unsigned long now, int status, long retry_after);
/* a request was answered: no wait */
void retry_backoff_answered(struct retry_backoff *backoff);
/* the seconds a request's error text says to wait ("Retry-After: 120",
posix_browser.c's on a 429 or 5xx); 0 when it says none */
long retry_backoff_after(const char *error);

#endif
