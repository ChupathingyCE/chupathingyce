/*
POSIX_CRASH.C

Crash reports on Linux and macOS (crash_report.h; Windows has its own,
port/windows/src/win32_crash.c). A crashing signal (SIGSEGV, SIGBUS,
SIGILL, SIGFPE, SIGABRT) writes a report file into crashes/ in the data
root from the signal handler, doing only what is safe there: text made on
the stack with write(2), and dladdr for each frame's module. The game then
ends as the signal would have ended it. The frames are the frame pointer
chain from the crashed context (every build keeps frame pointers), each
record checked readable first: a write(2) of it to a pipe fails with
EFAULT where a read would fault. (On arm64 a crash in a function that
calls nothing loses that function's caller, which only its link register
has.)

The next start sends the reports, on a thread of its own, as
crash_reports.upload says (sdl_platform.c asks the player first when it is
"ask"). The report's log is debug.txt as it was at the crash: the report
keeps its size then (log_size).

Built with the host's ABI, as the other posix_*.c. Android has none of this
yet (its game runs in a guest, port/android), nor has the dedicated server.
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "build_identity.h"
#include "crash_report.h"
#include "port_config.h"
#ifdef HALO_GAME_BROWSER
#include "browser_http.h"
#endif

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif

void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

enum
{
	PATH_SIZE = 1024,
	/* reports kept waiting, at most (as win32_crash.c's) */
	MAXIMUM_PENDING = 8,
	REPORT_NAME_SIZE = 64,
	/* debug.txt read back for a report's log: this much before its size at
	the crash */
	LOG_READ = 2 * CRASH_REPORT_MAXIMUM_LOG,
	ALTERNATE_STACK_SIZE = 64 * 1024,
	/* a handler that hangs (in dladdr, say) is ended by SIGALRM */
	HANDLER_SECONDS = 10,
};

static const int crash_signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };

static char crash_folder[PATH_SIZE];
static char crash_log_path[PATH_SIZE];
/* the report's first lines (crash_report.h), made at the start */
static char crash_header[1024];
static int probe_pipe[2] = { -1, -1 };
static char alternate_stack[ALTERNATE_STACK_SIZE];

/* ---------- the report, in the signal handler */

struct handler_text
{
	char data[8192];
	size_t length;
};

static void handler_append(struct handler_text *text, const char *data)
{
	size_t length = strlen(data);

	if (text->length + length >= sizeof(text->data))
		length = sizeof(text->data) - 1 - text->length;
	memcpy(text->data + text->length, data, length);
	text->length += length;
}

/* whether size bytes at address can be read: a pipe takes them, or fails
with EFAULT (and gives them back) */
static int readable(const void *address, size_t size)
{
	char drained[64];
	ssize_t written = write(probe_pipe[1], address, size);

	if (written > 0)
	{
		while (read(probe_pipe[0], drained, sizeof(drained)) > 0)
			;
	}
	return written == (ssize_t)size;
}

static const char *signal_name(int signal_number)
{
	switch (signal_number)
	{
	case SIGSEGV:
		return "SIGSEGV";
	case SIGBUS:
		return "SIGBUS";
	case SIGILL:
		return "SIGILL";
	case SIGFPE:
		return "SIGFPE";
	case SIGABRT:
		return "SIGABRT";
	default:
		return "SIGNAL";
	}
}

/* a frame's line: its module's file name and the offset in it */
static void frame_line(struct handler_text *text, uintptr_t address)
{
	Dl_info information;
	char line[160];

	if (dladdr((void *)address, &information) && information.dli_fname && information.dli_fbase)
	{
		const char *file = strrchr(information.dli_fname, '/');

		snprintf(line, sizeof(line), "frame %.64s+0x%llx\n", file ? file + 1 : information.dli_fname,
			(unsigned long long)(address - (uintptr_t)information.dli_fbase));
	}
	else
	{
		snprintf(line, sizeof(line), "frame ?+0x%llx\n", (unsigned long long)address);
	}
	handler_append(text, line);
}

/* the crashed context's program counter and frame pointer */
static void context_registers(void *context, uintptr_t *pc, uintptr_t *frame)
{
	ucontext_t *ucontext = context;

#if defined(__APPLE__) && defined(__aarch64__)
	*pc = (uintptr_t)ucontext->uc_mcontext->__ss.__pc;
	*frame = (uintptr_t)ucontext->uc_mcontext->__ss.__fp;
#elif defined(__APPLE__) && defined(__x86_64__)
	*pc = (uintptr_t)ucontext->uc_mcontext->__ss.__rip;
	*frame = (uintptr_t)ucontext->uc_mcontext->__ss.__rbp;
#elif defined(__linux__) && defined(__aarch64__)
	*pc = (uintptr_t)ucontext->uc_mcontext.pc;
	*frame = (uintptr_t)ucontext->uc_mcontext.regs[29];
#elif defined(__linux__) && defined(__x86_64__)
	*pc = (uintptr_t)ucontext->uc_mcontext.gregs[REG_RIP];
	*frame = (uintptr_t)ucontext->uc_mcontext.gregs[REG_RBP];
#elif defined(__linux__) && defined(__i386__)
	*pc = (uintptr_t)ucontext->uc_mcontext.gregs[REG_EIP];
	*frame = (uintptr_t)ucontext->uc_mcontext.gregs[REG_EBP];
#else
	(void)ucontext;
	*pc = 0;
	*frame = 0;
#endif
}

static void write_report(int signal_number, void *context)
{
	struct handler_text text;
	struct stat log;
	uintptr_t pc, frame;
	char line[PATH_SIZE + 64];
	int count = 1, file;

	text.length = 0;
	handler_append(&text, crash_header);
	snprintf(line, sizeof(line), "exception %s\n", signal_name(signal_number));
	handler_append(&text, line);
	if (!stat(crash_log_path, &log))
	{
		snprintf(line, sizeof(line), "log_size %lld\n", (long long)log.st_size);
		handler_append(&text, line);
	}
	context_registers(context, &pc, &frame);
	frame_line(&text, pc);
	/* the frame records: the next one, and the return address */
	while (count < CRASH_REPORT_FRAMES && frame && !(frame & (sizeof(uintptr_t) - 1)) &&
		readable((const void *)frame, 2 * sizeof(uintptr_t)))
	{
		const uintptr_t *record = (const uintptr_t *)frame;

		if (!record[1])
			break;
		frame_line(&text, record[1]);
		count++;
		if (record[0] <= frame)
			break;
		frame = record[0];
	}
	snprintf(line, sizeof(line), "%s/%lld-%d.txt", crash_folder, (long long)time(NULL), (int)getpid());
	file = open(line, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (file >= 0)
	{
		ssize_t written = write(file, text.data, text.length);

		(void)written;
		close(file);
		snprintf(line, sizeof(line), PLATFORM_LOG_PREFIX "crash report: %s, %d calls\n",
			signal_name(signal_number), count);
		written = write(STDERR_FILENO, line, strlen(line));
	}
}

static void crash_handler(int signal_number, siginfo_t *information, void *context)
{
	static volatile sig_atomic_t entered;
	struct sigaction action;

	memset(&action, 0, sizeof(action));
	action.sa_handler = SIG_DFL;
	sigemptyset(&action.sa_mask);
	sigaction(signal_number, &action, NULL);
	/* (a crash in the report: the signal's own end) */
	if (!entered)
	{
		entered = 1;
		alarm(HANDLER_SECONDS);
		write_report(signal_number, context);
	}
	/* the signal again, to its default action (macOS gives a signal sent
	with kill a fault's si_code: none can be told apart); a fault would
	also happen again as the handler returns */
	(void)information;
	raise(signal_number);
}

/* ---------- the start */

static void trim_reports(void);

/* the system's name and version, for the report: "macOS 26.0", "Linux
6.8.0 (Ubuntu 24.04 LTS)" */
static void system_version(char *text, size_t size)
{
	struct utsname name;
#ifdef __APPLE__
	char version[64];
	size_t length = sizeof(version);

	if (!sysctlbyname("kern.osproductversion", version, &length, NULL, 0))
	{
		snprintf(text, size, "macOS %.*s", (int)length, version);
		return;
	}
#else
	char line[256], pretty[128] = "";
	FILE *release = fopen("/etc/os-release", "r");

	if (release)
	{
		while (fgets(line, sizeof(line), release))
		{
			if (!strncmp(line, "PRETTY_NAME=", 12))
			{
				char *value = line + 12 + (line[12] == '"');

				value[strcspn(value, "\"\n")] = 0;
				snprintf(pretty, sizeof(pretty), " (%.100s)", value);
			}
		}
		fclose(release);
	}
#endif
	if (uname(&name))
	{
		snprintf(text, size, "unknown");
		return;
	}
#ifdef __APPLE__
	snprintf(text, size, "%s %s", name.sysname, name.release);
#else
	snprintf(text, size, "%s %s%s", name.sysname, name.release, pretty);
#endif
}

int posix_crash_install(const char *folder, const char *log_path)
{
	struct sigaction action;
	stack_t stack;
	char os[256];
	size_t index;

	if (probe_pipe[0] >= 0)
		return 1;
	snprintf(crash_folder, sizeof(crash_folder), "%s", folder);
	snprintf(crash_log_path, sizeof(crash_log_path), "%s", log_path);
	if (mkdir(crash_folder, 0700) && errno != EEXIST)
		return 0;
	system_version(os, sizeof(os));
	snprintf(crash_header, sizeof(crash_header), "version %s\nchannel %s\ncommit %s\nplatform %s\narchitecture %s\n"
		"os %s\n", build_identity_version(), build_identity_channel(), build_identity_commit(),
		build_identity_platform(), build_identity_architecture(), os);
	if (pipe(probe_pipe))
		return 0;
	for (index = 0; index < 2; index++)
	{
		fcntl(probe_pipe[index], F_SETFL, fcntl(probe_pipe[index], F_GETFL) | O_NONBLOCK);
		fcntl(probe_pipe[index], F_SETFD, FD_CLOEXEC);
	}
	/* (room for the handler after a stack overflow, on this thread) */
	stack.ss_sp = alternate_stack;
	stack.ss_size = sizeof(alternate_stack);
	stack.ss_flags = 0;
	sigaltstack(&stack, NULL);
	/* (dladdr's first call can take locks a crash may hold: made now) */
	{
		Dl_info information;

		dladdr((void *)posix_crash_install, &information);
	}
	memset(&action, 0, sizeof(action));
	action.sa_sigaction = crash_handler;
	action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
	sigemptyset(&action.sa_mask);
	for (index = 0; index < sizeof(crash_signals) / sizeof(crash_signals[0]); index++)
		sigaction(crash_signals[index], &action, NULL);
	trim_reports();
	return 1;
}

/* ---------- the reports waiting */

/* the reports in the folder: their names, at most count of them; how many
there are */
static int pending_reports(char names[][REPORT_NAME_SIZE], int count)
{
	DIR *folder = opendir(crash_folder);
	struct dirent *entry;
	int pending = 0;

	if (!folder)
		return 0;
	while ((entry = readdir(folder)) != NULL)
	{
		size_t length = strlen(entry->d_name);

		if (length <= 4 || length >= REPORT_NAME_SIZE || strcmp(entry->d_name + length - 4, ".txt") ||
			entry->d_name[0] < '0' || entry->d_name[0] > '9')
		{
			continue;
		}
		if (names && pending < count)
			memcpy(names[pending], entry->d_name, length + 1);
		pending++;
	}
	closedir(folder);
	return pending;
}

static void delete_report(const char *name)
{
	char path[PATH_SIZE + REPORT_NAME_SIZE];

	snprintf(path, sizeof(path), "%s/%s", crash_folder, name);
	unlink(path);
}

/* at most MAXIMUM_PENDING reports kept: the oldest go (the handler cannot
count them) */
static void trim_reports(void)
{
	char names[MAXIMUM_PENDING * 2][REPORT_NAME_SIZE];
	int count = pending_reports(names, MAXIMUM_PENDING * 2), index, other;

	if (count > MAXIMUM_PENDING * 2)
		count = MAXIMUM_PENDING * 2;
	/* (oldest first: the names start with the time) */
	for (index = 0; index < count; index++)
	{
		for (other = index + 1; other < count; other++)
		{
			if (atoll(names[other]) < atoll(names[index]))
			{
				char swap[REPORT_NAME_SIZE];

				memcpy(swap, names[index], REPORT_NAME_SIZE);
				memcpy(names[index], names[other], REPORT_NAME_SIZE);
				memcpy(names[other], swap, REPORT_NAME_SIZE);
			}
		}
	}
	for (index = 0; index < count - MAXIMUM_PENDING; index++)
		delete_report(names[index]);
}

int posix_crash_pending(void)
{
	return probe_pipe[0] >= 0 ? pending_reports(NULL, 0) : 0;
}

void posix_crash_discard(void)
{
	char names[MAXIMUM_PENDING * 2][REPORT_NAME_SIZE];
	int count = pending_reports(names, MAXIMUM_PENDING * 2), index;

	if (count > MAXIMUM_PENDING * 2)
		count = MAXIMUM_PENDING * 2;
	for (index = 0; index < count; index++)
		delete_report(names[index]);
}

/* a whole small file, NUL terminated, or NULL; free() it */
static char *read_report(const char *name)
{
	char path[PATH_SIZE + REPORT_NAME_SIZE];
	char *data;
	ssize_t length;
	int file;

	snprintf(path, sizeof(path), "%s/%s", crash_folder, name);
	file = open(path, O_RDONLY);
	if (file < 0)
		return NULL;
	data = malloc(CRASH_REPORT_MAXIMUM_FILE + 1);
	length = data ? read(file, data, CRASH_REPORT_MAXIMUM_FILE) : -1;
	close(file);
	if (length < 0)
	{
		free(data);
		return NULL;
	}
	data[length] = 0;
	return data;
}

/* debug.txt's last bytes before its size at the crash, or NULL (a log that
has been replaced since: shorter) */
static char *read_log(long long log_size, size_t *size)
{
	long long start = log_size > LOG_READ ? log_size - LOG_READ : 0;
	struct stat information;
	char *data;
	ssize_t length;
	int file = log_size > 0 ? open(crash_log_path, O_RDONLY) : -1;

	if (file < 0)
		return NULL;
	if (fstat(file, &information) || information.st_size < log_size ||
		!(data = malloc((size_t)(log_size - start) + 1)))
	{
		close(file);
		return NULL;
	}
	length = pread(file, data, (size_t)(log_size - start), (off_t)start);
	close(file);
	if (length <= 0)
	{
		free(data);
		return NULL;
	}
	data[length] = 0;
	*size = (size_t)length;
	return data;
}

/* sends one report; 1 when it is done with (sent, or refused for good) */
static int send_report(const char *name)
{
#ifdef HALO_GAME_BROWSER
	const char *base = config_string("network.browser_url");
	size_t base_length = strlen(base), log_length = 0, length = 0;
	char *report = read_report(name), *log = NULL, *body, *log_size;
	char url[1024], answer[256], error[256];
	int status;

	if (!report)
		return 1;
	/* (with or without the final slash, as browser.c's server_url) */
	while (base_length && base[base_length - 1] == '/')
		base_length--;
	if (!base_length)
	{
		free(report);
		return 0;
	}
	snprintf(url, sizeof(url), "%.*s" CRASH_REPORT_PATH, (int)base_length, base);
	if ((log_size = strstr(report, "\nlog_size ")) != NULL)
		log = read_log(atoll(log_size + 10), &log_length);
	body = crash_report_body(report, log, log_length, NULL, 0, &length);
	free(report);
	free(log);
	if (!body)
		return 0;
	status = posix_browser_request_as(url, body, "application/json", build_identity_user_agent(), answer,
		sizeof(answer), error, sizeof(error));
	free(body);
	answer[strcspn(answer, "\r\n")] = 0;
	if (status == 200)
	{
		platform_log("crash report: sent %s: %s", name, answer);
		return 1;
	}
	platform_log("crash report: the site answered %d to %s: %s", status, name, status ? answer : error);
	/* (no answer, a server error or too many reports: again later; any other
	refusal would be the same the next time) */
	return status >= 400 && status < 500 && status != 408 && status != 429;
#else
	(void)name;
	return 0;
#endif
}

static void *send_thread(void *argument)
{
	char names[MAXIMUM_PENDING * 2][REPORT_NAME_SIZE];
	int count = pending_reports(names, MAXIMUM_PENDING * 2), index;

	(void)argument;
	if (count > MAXIMUM_PENDING * 2)
		count = MAXIMUM_PENDING * 2;
	for (index = 0; index < count; index++)
	{
		if (send_report(names[index]))
			delete_report(names[index]);
	}
	return NULL;
}

void posix_crash_send(void)
{
	pthread_t thread;

	if (!pthread_create(&thread, NULL, send_thread, NULL))
		pthread_detach(thread);
}
