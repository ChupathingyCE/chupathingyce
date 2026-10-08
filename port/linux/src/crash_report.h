/*
CRASH_REPORT.H

A crash report, as the game sends it to its site (network.browser_url's
POST /v1/crash) when crash_reports.upload says it may: the same on every
build that has one (port/windows/src/win32_crash.c, posix_crash.c). A crash
leaves a report file of "key value" lines, written by the build that
crashed:

	version 0.7.0b
	channel release
	commit 61eb5615
	platform Windows
	architecture x64
	os Windows 10.0.19045
	exception c0000005            (a Windows exception's code, or SIGSEGV)
	frame halo.exe+0x1a2b3c       (where it crashed, then its callers: a
	frame ntdll.dll+0x4f2a0        module and the offset in it, "?" for none)

and crash_report_body makes the request's body of it, JSON:

	{"format": 1, "version", "channel", "commit", "platform", "architecture",
	 "os", "exception", "frames": [...], "log": the log's last
	 CRASH_REPORT_LOG_LINES lines, "minidump": base64 (Windows; left out
	 without one, or past CRASH_REPORT_MAXIMUM_DUMP bytes)}

The log's IP addresses (a public one the log has whole: debug.log_addresses)
become "[address]" first. Nothing else of the player's goes: no names
beyond what the log shows, no hardware IDs.
*/

#ifndef CRASH_REPORT_H
#define CRASH_REPORT_H

#include <stddef.h>

enum
{
	CRASH_REPORT_FORMAT = 1,
	CRASH_REPORT_FRAMES = 32,
	CRASH_REPORT_LOG_LINES = 200,
	CRASH_REPORT_MAXIMUM_LOG = 64 * 1024,
	CRASH_REPORT_MAXIMUM_DUMP = 1024 * 1024,
	/* a report file's size, at most */
	CRASH_REPORT_MAXIMUM_FILE = 16 * 1024,
};

/* the request's path, after network.browser_url */
#define CRASH_REPORT_PATH "/v1/crash"

/* whether this build reports its crashes: a release's or a nightly (whose
commits are known), or any build with HALO_CRASH_REPORTS_ANY_BUILD set (for
testing); crash_reports.upload decides the rest */
int crash_reports_armed(void);

/* the body of a report (report: its file's text; the log, at most its last
CRASH_REPORT_LOG_LINES lines taken; dump: a minidump or NULL), malloc'd and
NUL terminated, with its length; NULL without memory */
char *crash_report_body(const char *report, const char *log, size_t log_size, const void *dump, size_t dump_size,
	size_t *length);

/* text with each public IP address in it "[address]": its length, written
to out (size bytes, NUL terminated; 3 * length + 1 is enough) */
size_t crash_report_redact(const char *text, size_t length, char *out, size_t size);

/* Linux and macOS (posix_crash.c): the crash handler, writing report files
into folder (made if need be), with log_path's size at the crash for the
log; how many reports wait there; sending them (on a thread of its own);
deleting them */
int posix_crash_install(const char *folder, const char *log_path);
int posix_crash_pending(void);
void posix_crash_send(void);
void posix_crash_discard(void);

#endif
