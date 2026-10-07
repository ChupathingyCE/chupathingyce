/*
DELTA_CHECK.C

A check of the legacy table's loader (port/linux/src/delta.c), built by
tools/test_delta.py with the platform layer's flags and a test key
(HALO_DELTA_TEST_KEY), and stand-ins for the settings and the log. It runs
the steps it is given, in order, and prints what each did:

	offer FILE    a signed table from another machine: "offer 1" if taken
	state         "state A MI MA serial S override O disabled D signed N"
	start         delta_legacy_start: the cache, and the fetching thread
	wait S        until the serial in use is S (up to ten seconds)
	sleep MS      that many milliseconds

The save root (where the cache is) is $DELTA_CHECK_ROOT, the local override
(network.legacy_table) $DELTA_CHECK_OVERRIDE, and the list server
(network.browser_url) $DELTA_CHECK_URL. The requests are answered from
files in the save root's "served" folder, named by the URL with every
character but letters, digits and dots made "_"; a request without one is
answered 404 (one to a host named "unreachable" cannot connect), and each
is logged ("request: URL"). The log goes to the standard
output, "log: " before each line.
*/

#include "port_config.h"
#include "halo_port_limits.h"
#include "delta.h"
#include "browser_http.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---------- stand-ins */

void platform_log(const char *format, ...)
{
	va_list arguments;

	printf("log: ");
	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
	printf("\n");
}

const char *platform_save_root(void)
{
	const char *root = getenv("DELTA_CHECK_ROOT");

	return root ? root : ".";
}

const char *config_string(const char *name)
{
	const char *value = !strcmp(name, "network.legacy_table") ? getenv("DELTA_CHECK_OVERRIDE") :
		!strcmp(name, "network.browser_url") ? getenv("DELTA_CHECK_URL") : NULL;

	return value ? value : "";
}

void config_folder(char *path, size_t size)
{
	snprintf(path, size, "%s/", platform_save_root());
}

char *config_file_read(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	char *data;
	long length;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)length + 1);
	if (data && fread(data, 1, (size_t)length, file) != (size_t)length)
	{
		free(data);
		data = NULL;
	}
	fclose(file);
	if (data)
	{
		data[length] = 0;
		*size = (size_t)length;
	}
	return data;
}

int posix_browser_request(const char *url, const char *body, const char *content_type, char *response,
	int response_size, char *error, int error_size)
{
	char path[1200];
	size_t used, index, size = 0;
	char *data;

	(void)body;
	(void)content_type;
	printf("request: %s\n", url);
	fflush(stdout);
	used = (size_t)snprintf(path, sizeof(path), "%s/served/", platform_save_root());
	for (index = 0; url[index] && used + 1 < sizeof(path); index++)
	{
		char c = url[index];

		path[used++] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ?
			c : '_';
	}
	path[used] = 0;
	if (strstr(url, "unreachable"))
	{
		snprintf(error, (size_t)error_size, "could not connect");
		return 0;
	}
	data = config_file_read(path, &size);
	if (!data)
		return 404;
	snprintf(response, (size_t)response_size, "%s", data);
	free(data);
	return 200;
}

/* ---------- the steps */

int main(int count, char **arguments)
{
	static char buffer[DELTA_LEGACY_SIGNED_SIZE];
	int index;

	for (index = 1; index < count; index++)
	{
		if (!strcmp(arguments[index], "offer") && index + 1 < count)
		{
			size_t size = 0;
			char *data = config_file_read(arguments[++index], &size);

			printf("offer %d\n", data ? delta_legacy_offer(data, (int)size) : -1);
			free(data);
		}
		else if (!strcmp(arguments[index], "state"))
		{
			int disabled = 0, capability;

			for (capability = 0; capability < NUMBER_OF_DELTA_CAPABILITIES; capability++)
				disabled |= delta_capability_disabled(capability) << capability;
			printf("state %d %d %d serial %u override %d disabled %d signed %d\n", delta_legacy_announce(),
				delta_legacy_minimum(), delta_legacy_maximum(), delta_legacy_serial(), delta_legacy_override(),
				disabled, delta_legacy_signed(buffer, sizeof(buffer)));
		}
		else if (!strcmp(arguments[index], "start"))
			delta_legacy_start();
		else if (!strcmp(arguments[index], "wait") && index + 1 < count)
		{
			unsigned int serial = (unsigned int)strtoul(arguments[++index], NULL, 10);
			int tries;

			for (tries = 0; tries < 1000 && delta_legacy_serial() != serial; tries++)
				usleep(10000);
			printf("wait %d\n", delta_legacy_serial() == serial);
		}
		else if (!strcmp(arguments[index], "sleep") && index + 1 < count)
			usleep((useconds_t)strtoul(arguments[++index], NULL, 10) * 1000);
		else
		{
			printf("unknown step %s\n", arguments[index]);
			return 2;
		}
		fflush(stdout);
	}
	return 0;
}
