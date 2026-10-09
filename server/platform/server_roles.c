/*
SERVER_ROLES.C

Delta Control's roles as the server keeps them (server_roles.h): the
moderators file, the accounts' and the site's keys, the audit file, the
actors' limits, under one lock.
*/

#include "../src/server_roles.h"
#include "control_roles.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* the platform layer's (xbox_files.c): plain types */
const char *platform_data_root(void);

/* ---------- constants */

enum
{
	MAXIMUM_MODERATORS = 256,
	MAXIMUM_ACCOUNT_KEYS = 64,
	MAXIMUM_SITE_KEYS = 256,
	MAXIMUM_SITE_HANDLES = 128,
	LINE_SIZE = 256,
	MAXIMUM_BINDS = 8,
	/* a bind request is kept this long, and its game's player asked this long */
	BIND_SECONDS = 5 * 60,
	BIND_ANSWER_SECONDS = 2 * 60,
};

#define MODERATORS_FILE "moderators.txt"
#define AUDIT_FILE "control_audit.log"

/* ---------- structures */

struct keyed_role
{
	int role;
	unsigned char key[CONTROL_KEY_BYTES];
	char name[CONTROL_DISPLAY_NAME_SIZE];
};

/* ---------- globals */

static pthread_mutex_t roles_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct
{
	int started;
	/* the moderators file's, as last read, and its time and size then */
	struct control_moderator moderators[MAXIMUM_MODERATORS];
	int moderator_count;
	int moderators_skipped;
	time_t modified;
	off_t size;
	int exists;
	time_t checked;
	struct keyed_role accounts[MAXIMUM_ACCOUNT_KEYS];
	int account_count;
	struct keyed_role site[MAXIMUM_SITE_KEYS];
	int site_count;
	unsigned int site_version;
	struct
	{
		char handle[CONTROL_DISPLAY_NAME_SIZE];
		int role;
	} site_handles[MAXIMUM_SITE_HANDLES];
	int site_handle_count;
	struct
	{
		unsigned int id;
		int state;
		int taken;
		int player_number;
		char account[CONTROL_DISPLAY_NAME_SIZE];
		unsigned char key[CONTROL_KEY_BYTES];
		time_t made;
	} binds[MAXIMUM_BINDS];
	unsigned int bind_sequence;
	int changed;
	/* (zeros: an empty limiter) */
	struct control_actor_limiter actors;
	void (*recorder)(const struct control_audit_event *event);
} roles;

/* ---------- private code (the lock held) */

static void data_path(const char *name, char *path, size_t size)
{
	const char *root = platform_data_root();

	snprintf(path, size, "%s/%s", root && root[0] ? root : ".", name);
}

/* the moderators file read again if it changed (looked at once a second) */
static void refresh_moderators(int force)
{
	char path[1024];
	char line[LINE_SIZE + 2];
	struct stat information;
	time_t now = time(NULL);
	FILE *file;
	int exists;

	if (!force && now == roles.checked)
		return;
	roles.checked = now;
	data_path(MODERATORS_FILE, path, sizeof(path));
	exists = !stat(path, &information);
	if (!force && exists == roles.exists && (!exists || (information.st_mtime == roles.modified &&
		information.st_size == roles.size)))
	{
		return;
	}
	roles.exists = exists;
	roles.moderator_count = 0;
	roles.moderators_skipped = 0;
	if (!exists)
		return;
	roles.modified = information.st_mtime;
	roles.size = information.st_size;
	file = fopen(path, "r");
	if (!file)
		return;
	while (fgets(line, sizeof(line), file))
	{
		struct control_moderator moderator;
		const char *reason = "";
		int result;

		/* (a line too long for the file: the rest of it skipped too) */
		if (!strchr(line, '\n') && !feof(file))
		{
			int character;

			while ((character = fgetc(file)) != EOF && character != '\n')
				;
			roles.moderators_skipped++;
			continue;
		}
		result = control_moderator_parse(line, &moderator, &reason);
		if (result < 0 || (result > 0 && roles.moderator_count >= MAXIMUM_MODERATORS))
		{
			roles.moderators_skipped++;
			continue;
		}
		if (result > 0)
			roles.moderators[roles.moderator_count++] = moderator;
	}
	fclose(file);
}

/* the moderators file written again, whole, from the moderators kept (a new
file beside it, then renamed over it, the comments at its top kept as they
are not: a heading of its own): 1, else 0 and why */
static int write_moderators(char *problem, int problem_size)
{
	char path[1024];
	char temporary[1040];
	char line[LINE_SIZE];
	int index;
	int fd;
	FILE *out;

	data_path(MODERATORS_FILE, path, sizeof(path));
	snprintf(temporary, sizeof(temporary), "%s.new", path);
	unlink(temporary);
	fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	out = fd >= 0 ? fdopen(fd, "w") : NULL;
	if (!out)
	{
		if (fd >= 0)
			close(fd);
		snprintf(problem, (size_t)problem_size, "cannot write %s.new in the data folder (%s): nothing changed",
			MODERATORS_FILE, strerror(errno));
		return 0;
	}
	fprintf(out,
		"# The ChupathingyCE Dedicated Server's moderators (server/docs/moderation.md).\n"
		"# A line a person: <role> <moderator key> [name]. Roles: moderator, admin, owner.\n"
		"# The key is the one sv_players shows once a player's game has proved it. The\n"
		"# name is for display only. Edits take effect within a second.\n");
	for (index = 0; index < roles.moderator_count; index++)
	{
		if (control_moderator_line(&roles.moderators[index], line, sizeof(line)) > 0)
			fprintf(out, "%s\n", line);
	}
	if (fflush(out) || fsync(fileno(out)) || fclose(out) || rename(temporary, path))
	{
		snprintf(problem, (size_t)problem_size, "cannot write %s in the data folder (%s): nothing changed",
			MODERATORS_FILE, strerror(errno));
		unlink(temporary);
		return 0;
	}
	refresh_moderators(1);
	return 1;
}

/* ---------- public code */

/* (server_roles.h's copies of control_roles.h's numbers) */
typedef char roles_numbers_match[(SERVER_ROLE_OWNER == CONTROL_ROLE_OWNER && SERVER_ROLE_ADMIN == CONTROL_ROLE_ADMIN &&
	SERVER_ROLE_MODERATOR == CONTROL_ROLE_MODERATOR && SERVER_PERMISSION_ALL == CONTROL_PERMISSION_ALL &&
	SERVER_PERMISSION_BAN_TIMED == CONTROL_PERMISSION_BAN_TIMED && SERVER_PERMISSION_ROLES == CONTROL_PERMISSION_ROLES &&
	SERVER_PERMISSION_CONSOLE == CONTROL_PERMISSION_CONSOLE && SERVER_PERMISSION_INVITE == CONTROL_PERMISSION_INVITE)
	? 1 : -1];

const char *server_role_name(int role)
{
	return control_role_name(role);
}

unsigned int server_role_permissions(int role)
{
	return control_role_permissions(role);
}

int server_role_parse(const char *text)
{
	return control_role_parse(text);
}

int server_key_parse(const char *text, unsigned char *key)
{
	return control_key_parse(text, key);
}

void server_key_short(const unsigned char *key, char *text)
{
	control_key_short(key, text);
}

unsigned int server_command_permission(const char *line, int *changes)
{
	return control_command_permission(line, changes);
}

void server_roles_start(void)
{
	pthread_mutex_lock(&roles_mutex);
	if (!roles.started)
	{
		roles.started = 1;
		refresh_moderators(1);
	}
	pthread_mutex_unlock(&roles_mutex);
}

int server_roles_key_role(const unsigned char *key, unsigned int *permissions, char *who, int who_size)
{
	int best = CONTROL_ROLE_NONE;
	int index;

	if (who && who_size > 0)
		who[0] = 0;
	pthread_mutex_lock(&roles_mutex);
	refresh_moderators(0);
	for (index = 0; index < roles.moderator_count; index++)
	{
		if (!memcmp(roles.moderators[index].key, key, CONTROL_KEY_BYTES) && roles.moderators[index].role > best)
		{
			best = roles.moderators[index].role;
			if (who && who_size > 0)
				snprintf(who, (size_t)who_size, "%s", roles.moderators[index].name);
		}
	}
	for (index = 0; index < roles.account_count; index++)
	{
		if (!memcmp(roles.accounts[index].key, key, CONTROL_KEY_BYTES) && roles.accounts[index].role > best)
		{
			best = roles.accounts[index].role;
			if (who && who_size > 0)
				snprintf(who, (size_t)who_size, "%s (account)", roles.accounts[index].name);
		}
	}
	for (index = 0; index < roles.site_count; index++)
	{
		if (!memcmp(roles.site[index].key, key, CONTROL_KEY_BYTES) && roles.site[index].role > best)
		{
			best = roles.site[index].role;
			if (who && who_size > 0)
				snprintf(who, (size_t)who_size, "%s (site)", roles.site[index].name);
		}
	}
	pthread_mutex_unlock(&roles_mutex);
	if (permissions)
		*permissions = control_role_permissions(best);
	return best;
}

int server_roles_moderator_set(int role, const unsigned char *key, const char *name, char *problem, int problem_size)
{
	int index;
	int result;
	struct control_moderator old;
	int added = 0;

	if (role <= CONTROL_ROLE_NONE || role >= CONTROL_ROLE_COUNT)
	{
		snprintf(problem, (size_t)problem_size, "a role is moderator, admin or owner");
		return 0;
	}
	if (name && name[0] && !control_display_name_valid(name))
	{
		snprintf(problem, (size_t)problem_size, "a name is 1 to 31 printable ASCII characters, no quotes");
		return 0;
	}
	pthread_mutex_lock(&roles_mutex);
	refresh_moderators(1);
	if (roles.moderators_skipped)
	{
		pthread_mutex_unlock(&roles_mutex);
		snprintf(problem, (size_t)problem_size, "%s has lines that are no moderator's: fix them before it is changed "
			"here", MODERATORS_FILE);
		return 0;
	}
	for (index = 0; index < roles.moderator_count; index++)
	{
		if (!memcmp(roles.moderators[index].key, key, CONTROL_KEY_BYTES))
			break;
	}
	if (index >= roles.moderator_count)
	{
		if (roles.moderator_count >= MAXIMUM_MODERATORS)
		{
			pthread_mutex_unlock(&roles_mutex);
			snprintf(problem, (size_t)problem_size, "%d moderators at most", (int)MAXIMUM_MODERATORS);
			return 0;
		}
		memset(&roles.moderators[index], 0, sizeof(roles.moderators[index]));
		memcpy(roles.moderators[index].key, key, CONTROL_KEY_BYTES);
		roles.moderator_count++;
		added = 1;
	}
	old = roles.moderators[index];
	roles.moderators[index].role = role;
	if (name && name[0])
		snprintf(roles.moderators[index].name, sizeof(roles.moderators[index].name), "%s", name);
	result = write_moderators(problem, problem_size);
	if (!result)
	{
		if (added)
			roles.moderator_count--;
		else
			roles.moderators[index] = old;
	}
	pthread_mutex_unlock(&roles_mutex);
	return result;
}

int server_roles_moderator_remove(const char *which, char *found_name, int found_name_size, char *problem,
	int problem_size)
{
	int index;
	int found = -1;
	int matches = 0;
	size_t length = strlen(which);
	struct control_moderator removed;
	int result;

	pthread_mutex_lock(&roles_mutex);
	refresh_moderators(1);
	if (roles.moderators_skipped)
	{
		pthread_mutex_unlock(&roles_mutex);
		snprintf(problem, (size_t)problem_size, "%s has lines that are no moderator's: fix them before it is changed "
			"here", MODERATORS_FILE);
		return 0;
	}
	for (index = 0; index < roles.moderator_count; index++)
	{
		char key[CONTROL_KEY_TEXT + 1];
		int match;

		control_key_text(roles.moderators[index].key, key);
		match = (length >= 8 && length <= CONTROL_KEY_TEXT && !strncasecmp(key, which, length)) ||
			(roles.moderators[index].name[0] && !strcasecmp(roles.moderators[index].name, which));
		if (match)
		{
			found = index;
			matches++;
		}
	}
	if (matches != 1)
	{
		pthread_mutex_unlock(&roles_mutex);
		if (matches)
			snprintf(problem, (size_t)problem_size, "%d moderators match %s: give more of the key", matches, which);
		else
			snprintf(problem, (size_t)problem_size, "no moderator %s (sv_mod_list)", which);
		return 0;
	}
	removed = roles.moderators[found];
	memmove(&roles.moderators[found], &roles.moderators[found + 1],
		(size_t)(roles.moderator_count - found - 1) * sizeof(roles.moderators[0]));
	roles.moderator_count--;
	result = write_moderators(problem, problem_size);
	if (!result)
	{
		memmove(&roles.moderators[found + 1], &roles.moderators[found],
			(size_t)(roles.moderator_count - found) * sizeof(roles.moderators[0]));
		roles.moderators[found] = removed;
		roles.moderator_count++;
	}
	else if (found_name && found_name_size > 0)
	{
		char key[9];

		control_key_short(removed.key, key);
		snprintf(found_name, (size_t)found_name_size, "%s%s%s", removed.name[0] ? removed.name : key,
			removed.name[0] ? " " : "", removed.name[0] ? key : "");
	}
	pthread_mutex_unlock(&roles_mutex);
	return result;
}

int server_roles_moderator_count(void)
{
	int count;

	pthread_mutex_lock(&roles_mutex);
	refresh_moderators(0);
	count = roles.moderator_count;
	pthread_mutex_unlock(&roles_mutex);
	return count;
}

int server_roles_moderator_get(int index, int *role, char *key_text, int key_text_size, char *name, int name_size)
{
	int found = 0;

	pthread_mutex_lock(&roles_mutex);
	if (index >= 0 && index < roles.moderator_count)
	{
		char key[CONTROL_KEY_TEXT + 1];

		control_key_text(roles.moderators[index].key, key);
		*role = roles.moderators[index].role;
		snprintf(key_text, (size_t)key_text_size, "%s", key);
		snprintf(name, (size_t)name_size, "%s", roles.moderators[index].name);
		found = 1;
	}
	pthread_mutex_unlock(&roles_mutex);
	return found;
}

static int set_keyed(struct keyed_role *list, int maximum, int count, const unsigned char *keys, const int *list_roles,
	const char *const *names)
{
	int index;
	int kept = 0;

	for (index = 0; index < count && kept < maximum; index++)
	{
		if (list_roles[index] <= CONTROL_ROLE_NONE || list_roles[index] >= CONTROL_ROLE_COUNT)
			continue;
		list[kept].role = list_roles[index];
		memcpy(list[kept].key, keys + index * CONTROL_KEY_BYTES, CONTROL_KEY_BYTES);
		snprintf(list[kept].name, sizeof(list[kept].name), "%s", names && names[index] ? names[index] : "");
		kept++;
	}
	return kept;
}

void server_roles_set_accounts(int count, const unsigned char *keys, const int *list_roles, const char *const *names)
{
	pthread_mutex_lock(&roles_mutex);
	roles.account_count = set_keyed(roles.accounts, MAXIMUM_ACCOUNT_KEYS, count, keys, list_roles, names);
	roles.changed = 1;
	pthread_mutex_unlock(&roles_mutex);
}

void server_roles_set_site(int count, const unsigned char *keys, const int *list_roles, const char *const *names,
	unsigned int version)
{
	pthread_mutex_lock(&roles_mutex);
	roles.site_count = set_keyed(roles.site, MAXIMUM_SITE_KEYS, count, keys, list_roles, names);
	roles.site_version = version;
	roles.changed = 1;
	pthread_mutex_unlock(&roles_mutex);
}

void server_roles_set_site_handles(int count, const char *const *handles, const int *list_roles)
{
	int index;

	pthread_mutex_lock(&roles_mutex);
	roles.site_handle_count = 0;
	for (index = 0; index < count && roles.site_handle_count < MAXIMUM_SITE_HANDLES; index++)
	{
		if (list_roles[index] <= CONTROL_ROLE_NONE || list_roles[index] >= CONTROL_ROLE_COUNT)
			continue;
		snprintf(roles.site_handles[roles.site_handle_count].handle, sizeof(roles.site_handles[0].handle), "%s",
			handles[index]);
		roles.site_handles[roles.site_handle_count].role = list_roles[index];
		roles.site_handle_count++;
	}
	pthread_mutex_unlock(&roles_mutex);
}

int server_roles_site_handle_role(const char *handle)
{
	int index;
	int role = CONTROL_ROLE_NONE;

	pthread_mutex_lock(&roles_mutex);
	for (index = 0; index < roles.site_handle_count; index++)
	{
		if (!strcmp(roles.site_handles[index].handle, handle))
			role = roles.site_handles[index].role;
	}
	pthread_mutex_unlock(&roles_mutex);
	return role;
}

void server_audit(const char *via, const char *actor, int role, const char *action, const char *target,
	const char *reason, int ok, const char *detail)
{
	struct control_audit_event event;
	char line[1024];
	char path[1024];
	int length;

	memset(&event, 0, sizeof(event));
	event.time = (int64_t)time(NULL);
	event.via = via;
	event.actor = actor;
	event.role = role;
	event.action = action;
	event.target = target;
	event.reason = reason;
	event.ok = ok;
	event.detail = detail;
	length = control_audit_line(&event, line, sizeof(line));
	pthread_mutex_lock(&roles_mutex);
	if (length > 0)
	{
		int fd;

		data_path(AUDIT_FILE, path, sizeof(path));
		fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
		if (fd >= 0)
		{
			ssize_t written = write(fd, line, (size_t)length);

			(void)written;
			close(fd);
		}
	}
	if (roles.recorder)
		roles.recorder(&event);
	pthread_mutex_unlock(&roles_mutex);
}

void server_roles_set_recorder(void (*recorder)(const struct control_audit_event *event))
{
	pthread_mutex_lock(&roles_mutex);
	roles.recorder = recorder;
	pthread_mutex_unlock(&roles_mutex);
}

unsigned int server_roles_bind_request(const char *account, int player_number)
{
	time_t now = time(NULL);
	int index;
	int slot = -1;
	unsigned int id = 0;

	pthread_mutex_lock(&roles_mutex);
	for (index = 0; index < MAXIMUM_BINDS; index++)
	{
		if (!roles.binds[index].id || now - roles.binds[index].made >= BIND_SECONDS)
		{
			slot = index;
			break;
		}
	}
	if (slot >= 0)
	{
		memset(&roles.binds[slot], 0, sizeof(roles.binds[slot]));
		/* (never 0) */
		if (!++roles.bind_sequence)
			roles.bind_sequence = 1;
		id = roles.binds[slot].id = roles.bind_sequence;
		roles.binds[slot].state = SERVER_BIND_PENDING;
		roles.binds[slot].player_number = player_number;
		roles.binds[slot].made = now;
		snprintf(roles.binds[slot].account, sizeof(roles.binds[slot].account), "%s", account);
	}
	pthread_mutex_unlock(&roles_mutex);
	return id;
}

int server_roles_bind_take(unsigned int *request_id, int *player_number, char *account, int account_size)
{
	int index;
	int found = 0;

	pthread_mutex_lock(&roles_mutex);
	for (index = 0; index < MAXIMUM_BINDS && !found; index++)
	{
		if (roles.binds[index].id && !roles.binds[index].taken && roles.binds[index].state == SERVER_BIND_PENDING)
		{
			roles.binds[index].taken = 1;
			*request_id = roles.binds[index].id;
			*player_number = roles.binds[index].player_number;
			snprintf(account, (size_t)account_size, "%s", roles.binds[index].account);
			found = 1;
		}
	}
	pthread_mutex_unlock(&roles_mutex);
	return found;
}

void server_roles_bind_answer(unsigned int request_id, int state, const unsigned char *key)
{
	int index;

	pthread_mutex_lock(&roles_mutex);
	for (index = 0; index < MAXIMUM_BINDS; index++)
	{
		if (roles.binds[index].id != request_id || !request_id)
			continue;
		/* (an answer only to a request asked, and not yet answered) */
		if (roles.binds[index].state == SERVER_BIND_PENDING || roles.binds[index].state == SERVER_BIND_SENT)
		{
			roles.binds[index].state = state;
			if (state == SERVER_BIND_ACCEPTED && key)
				memcpy(roles.binds[index].key, key, CONTROL_KEY_BYTES);
		}
	}
	pthread_mutex_unlock(&roles_mutex);
}

int server_roles_bind_state(unsigned int request_id, char *account, int account_size, unsigned char *key)
{
	time_t now = time(NULL);
	int index;
	int state = -1;

	pthread_mutex_lock(&roles_mutex);
	for (index = 0; index < MAXIMUM_BINDS; index++)
	{
		if (!request_id || roles.binds[index].id != request_id || now - roles.binds[index].made >= BIND_SECONDS)
			continue;
		if (roles.binds[index].state == SERVER_BIND_SENT && now - roles.binds[index].made >= BIND_ANSWER_SECONDS)
			roles.binds[index].state = SERVER_BIND_EXPIRED;
		state = roles.binds[index].state;
		if (account)
			snprintf(account, (size_t)account_size, "%s", roles.binds[index].account);
		if (key && state == SERVER_BIND_ACCEPTED)
			memcpy(key, roles.binds[index].key, CONTROL_KEY_BYTES);
	}
	pthread_mutex_unlock(&roles_mutex);
	return state;
}

void server_roles_moderation_changed(void)
{
	pthread_mutex_lock(&roles_mutex);
	roles.changed = 1;
	pthread_mutex_unlock(&roles_mutex);
}

int server_roles_take_changed(void)
{
	int changed;

	pthread_mutex_lock(&roles_mutex);
	changed = roles.changed;
	roles.changed = 0;
	pthread_mutex_unlock(&roles_mutex);
	return changed;
}

int server_roles_actor_allowed(const char *actor, int *retry_after)
{
	struct timespec now;
	int64_t wait = 0;
	int allowed;

	clock_gettime(CLOCK_MONOTONIC, &now);
	pthread_mutex_lock(&roles_mutex);
	allowed = control_actor_allowed(&roles.actors, actor, (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000, &wait);
	pthread_mutex_unlock(&roles_mutex);
	if (retry_after)
		*retry_after = (int)wait;
	return allowed;
}
