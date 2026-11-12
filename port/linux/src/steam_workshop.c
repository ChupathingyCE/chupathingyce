/*
STEAM_WORKSHOP.C

The subscribed Steam Workshop items' Halo CE maps, found in place (steam_workshop.h).
*/
#ifndef HALO_MCC
#include "../include/halo_port_features.h"
#endif
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h, above): a console builds this to nothing */

#include "steam_workshop.h"
#include "mcc_manifest.h"
#include "mcc_json.h" /* port: our own JSON lookups, json_reader.h */
#include "port_config.h"
#include "posix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STEAM_APP "976730"
#define CONTENT_FOLDER "steamapps/workshop/content/" STEAM_APP
#define MAXIMUM_STEAM_ROOTS 10

static struct
{
	int scanned;
	int enabled;
	int count;
	struct steam_workshop_map maps[STEAM_WORKSHOP_MAXIMUM_MAPS];
	/* the maps that couldn't be listed, and why (steam_workshop_unlisted) */
	int unlisted_count;
	struct
	{
		char item_id[STEAM_WORKSHOP_ID_SIZE];
		char map[64];
		/* (a word or two for the item's page; the log's detail, file names and all) */
		char reason[STEAM_WORKSHOP_REASON_SIZE];
		char detail[STEAM_WORKSHOP_DETAIL_SIZE];
	} unlisted[STEAM_WORKSHOP_MAXIMUM_UNLISTED];
} catalog;

static int discovery_enabled(void)
{
#if defined(HALO_ANDROID) || defined(HALO_WEB)
	return 0;
#else
	return config_boolean("game.workshop_maps");
#endif
}

/* ---------- parsing */

static void copy_text(char *out, unsigned size, const char *text)
{
	unsigned length = (unsigned)strlen(text);

	if (length >= size)
	{
		length = size - 1;
		/* not inside a UTF-8 character */
		while (length && (((unsigned char)text[length]) & 0xC0) == 0x80)
			length--;
	}
	memcpy(out, text, length);
	out[length] = 0;
}

/* port: (mcc_json.h) a document of a size read, and one JSON object */
static int parse(const char *text, int length)
{
	return text && length > 0 && length <= STEAM_WORKSHOP_MAXIMUM_JSON && mcc_json_valid(text, length);
}

static const char *const title_path[] = { "Title", "Neutral", NULL };
static const char *const engine_path[] = { "Engine", NULL };
static const char *const campaign_path[] = { "GameModContents", "HasCampaign", NULL };
static const char *const maps_path[] = { "GameModContents", "MultiplayerMaps", NULL };
static const char *const scenario_path[] = { "ScenarioFile", NULL };
static const char *const thumbnail_path[] = { "Images", "Thumbnail", NULL };
static const char *const large_path[] = { "Images", "Large", NULL };

/* a plain item-relative path: not empty, not absolute or drive-lettered, no "..", no control characters */
static int path_is_plain(const char *path)
{
	const char *c;

	if (!path[0] || path[0] == '/' || path[0] == '\\' || path[1] == ':')
		return 0;
	for (c = path; *c; c++)
	{
		if ((unsigned char)*c < 0x20 || *c == 0x7F || (c[0] == '.' && c[1] == '.'))
			return 0;
	}
	return 1;
}

static void slashes(char *path)
{
	for (; *path; path++)
	{
		if (*path == '\\')
			*path = '/';
	}
}

static void neutral_title(const char *document, int length, char *out)
{
	char text[256];

	out[0] = 0;
	if (mcc_json_string(document, length, title_path, text, sizeof(text)))
		copy_text(out, STEAM_WORKSHOP_TITLE_SIZE, text);
}

int steam_workshop_read_item(const char *text, int length, struct steam_workshop_item_text *item)
{
	char engine[32];
	struct json found;
	int index, count;

	memset(item, 0, sizeof(*item));
	if (!parse(text, length))
		return 0;
	neutral_title(text, length, item->title);
	if (mcc_json_string(text, length, engine_path, engine, sizeof(engine)))
		item->halo1 = !strcmp(engine, "Halo1");
	mcc_json_boolean(text, length, campaign_path, &item->has_campaign);
	if (mcc_json_find(text, length, maps_path, &found))
	{
		count = mcc_json_strings(text, length, maps_path, item->map_json[0], sizeof(item->map_json[0]),
			sizeof(item->map_json[0]), STEAM_WORKSHOP_MAXIMUM_ITEM_MAPS);
		if (count == -2 && item->halo1)
			fprintf(stderr, "MCC package '%s': MultiplayerMaps has %d entries; the supported limit is %d. Split the collection into smaller packages.\n",
				item->title, mcc_json_count(text, length, maps_path), STEAM_WORKSHOP_MAXIMUM_ITEM_MAPS);
		if (count < 0)
		{
			memset(item, 0, sizeof(*item));
			return 0;
		}
		for (index = 0; index < count; index++)
		{
			char *path = item->map_json[index];

			if (!path_is_plain(path))
			{
				memset(item, 0, sizeof(*item));
				return 0;
			}
			slashes(path);
		}
		item->map_count = count;
	}
	return 1;
}

int steam_workshop_read_map(const char *text, int length, struct steam_workshop_map_text *map)
{
	char scenario[256], thumbnail[128];
	const char *name, *c;
	unsigned name_length;

	memset(map, 0, sizeof(*map));
	if (!parse(text, length))
		return 0;
	if (!mcc_json_string(text, length, scenario_path, scenario, sizeof(scenario)))
		return 0;
	name = scenario;
	for (c = scenario; *c; c++)
	{
		if (*c == '/' || *c == '\\')
			name = c + 1;
	}
	for (c = name; *c; c++)
	{
		/* the name becomes a file name: no separator, drive colon or control character */
		if ((unsigned char)*c < 0x20 || *c == 0x7F || *c == ':')
			break;
	}
	if (!name[0] || *c || strlen(name) >= sizeof(map->name) || !strcmp(name, ".") || !strcmp(name, ".."))
	{
		memset(map, 0, sizeof(*map));
		return 0;
	}
	strcpy(map->name, name);
	/* (the file's name, whether or not the manifest wrote its extension) */
	name_length = (unsigned)strlen(map->name);
	if (name_length > 4 && !strcmp(map->name + name_length - 4, ".map"))
		map->name[name_length - 4] = 0;
	neutral_title(text, length, map->title);
	if (mcc_json_string(text, length, thumbnail_path, thumbnail, sizeof(thumbnail)) && path_is_plain(thumbnail))
	{
		slashes(thumbnail);
		strcpy(map->thumbnail, thumbnail);
	}
	if (mcc_json_string(text, length, large_path, thumbnail, sizeof(thumbnail)) && path_is_plain(thumbnail))
	{
		slashes(thumbnail);
		strcpy(map->large, thumbnail);
	}
	return 1;
}

int steam_workshop_read_libraries(const char *text, int length, char (*paths)[STEAM_WORKSHOP_PATH_SIZE], int maximum)
{
	int count = 0, at = 0;

	/* "path"<white space>"value", the value's \\ and \" unescaped */
	while (count < maximum && at + 6 <= length)
	{
		int start;
		unsigned used = 0;

		if (memcmp(text + at, "\"path\"", 6))
		{
			at++;
			continue;
		}
		at += 6;
		while (at < length && (text[at] == ' ' || text[at] == '\t'))
			at++;
		if (at >= length || text[at] != '"')
			continue;
		start = ++at;
		while (at < length && text[at] != '"' && text[at] != '\n' && used + 1 < STEAM_WORKSHOP_PATH_SIZE)
		{
			if (text[at] == '\\' && at + 1 < length && (text[at + 1] == '\\' || text[at + 1] == '"'))
				at++;
			paths[count][used++] = text[at++];
		}
		paths[count][used] = 0;
		if (at < length && text[at] == '"' && at > start && used)
			count++;
	}
	return count;
}

/* ---------- files */

static char *read_file(const char *path, int *length)
{
	FILE *file = fopen(path, "rb");
	char *text;
	size_t got;

	*length = 0;
	if (!file)
		return (void *)0;
	text = malloc(STEAM_WORKSHOP_MAXIMUM_JSON + 2);
	if (!text)
	{
		fclose(file);
		return (void *)0;
	}
	got = fread(text, 1, STEAM_WORKSHOP_MAXIMUM_JSON + 1, file);
	fclose(file);
	if (!got)
	{
		free(text);
		return (void *)0;
	}
	text[got] = 0;
	/* one byte past the limit is "too large": the parser refuses it */
	*length = (int)got;
	return text;
}

static int is_directory(const char *path)
{
	struct posix_file_information information;

	return posix_stat(path, &information) == 0 && (information.flags & _posix_file_is_directory);
}

static int is_file(const char *path)
{
	struct posix_file_information information;

	return posix_stat(path, &information) == 0 && !(information.flags & _posix_file_is_directory);
}

static int join(char *out, unsigned size, const char *folder, const char *name)
{
	int written = snprintf(out, size, "%s/%s", folder, name);

	return written > 0 && (unsigned)written < size;
}

/* an item-relative path ('/' separators) to its host path, each part found case-blind: 1 if it exists */
static int resolve(const char *folder, const char *relative, char *out, unsigned size)
{
	char path[STEAM_WORKSHOP_PATH_SIZE], part[128], found[256], next[STEAM_WORKSHOP_PATH_SIZE];
	const char *c = relative;

	if (strlen(folder) >= sizeof(path))
		return 0;
	strcpy(path, folder);
	while (*c)
	{
		unsigned length = 0;

		while (*c == '/')
			c++;
		if (!*c)
			break;
		while (c[length] && c[length] != '/')
			length++;
		if (length >= sizeof(part))
			return 0;
		memcpy(part, c, length);
		part[length] = 0;
		c += length;
		if (!posix_find_entry_case_insensitive(path, part, found, sizeof(found)) ||
			!join(next, sizeof(next), path, found))
			return 0;
		strcpy(path, next);
	}
	if (!strcmp(path, folder) || strlen(path) >= size)
		return 0;
	strcpy(out, path);
	return 1;
}

void steam_workshop_unlisted(const char *item_id, const char *map, const char *reason, const char *detail)
{
	int index;

	if (!item_id || !map || !reason || !detail)
		return;
	for (index = 0; index < catalog.unlisted_count; index++)
	{
		if (!strcmp(catalog.unlisted[index].item_id, item_id) && !strcmp(catalog.unlisted[index].map, map) &&
			!strcmp(catalog.unlisted[index].reason, reason))
			return;
	}
	/* (each once a scan: the log says it the first time) */
	if (map[0])
		fprintf(stderr, "Workshop item %s: map %s can't be listed: %s (%s)\n", item_id, map, detail, reason);
	else
		fprintf(stderr, "Workshop item %s: %s (%s)\n", item_id, detail, reason);
	if (catalog.unlisted_count >= STEAM_WORKSHOP_MAXIMUM_UNLISTED)
		return;
	copy_text(catalog.unlisted[catalog.unlisted_count].item_id, STEAM_WORKSHOP_ID_SIZE, item_id);
	copy_text(catalog.unlisted[catalog.unlisted_count].map, 64, map);
	copy_text(catalog.unlisted[catalog.unlisted_count].reason, STEAM_WORKSHOP_REASON_SIZE, reason);
	copy_text(catalog.unlisted[catalog.unlisted_count].detail, STEAM_WORKSHOP_DETAIL_SIZE, detail);
	catalog.unlisted_count++;
}

int steam_workshop_item_unlisted(const char *item_id, int which, char *found_item, char *map, char *reason,
	char *detail)
{
	int index, count = 0;

	if (!catalog.scanned)
		steam_workshop_scan();
	for (index = 0; index < catalog.unlisted_count; index++)
	{
		if (item_id && strcmp(catalog.unlisted[index].item_id, item_id))
			continue;
		if (count++ == which)
		{
			if (found_item)
				strcpy(found_item, catalog.unlisted[index].item_id);
			if (map)
				strcpy(map, catalog.unlisted[index].map);
			if (reason)
				strcpy(reason, catalog.unlisted[index].reason);
			if (detail)
				strcpy(detail, catalog.unlisted[index].detail);
		}
	}
	return count;
}

static int has_item(const char *item_id)
{
	int index;

	for (index = 0; index < catalog.count; index++)
	{
		if (!strcmp(catalog.maps[index].item_id, item_id))
			return 1;
	}
	return 0;
}

static int digits(const char *text)
{
	unsigned length = (unsigned)strlen(text), index;

	if (!length || length >= STEAM_WORKSHOP_ID_SIZE)
		return 0;
	for (index = 0; index < length; index++)
	{
		if (text[index] < '0' || text[index] > '9')
			return 0;
	}
	return 1;
}

/* the map a json names, for the log and the item's page: its file's name without folders or ".json" */
static void json_map_name(const char *json, char name[64])
{
	const char *base = strrchr(json, '/');
	size_t length;

	base = base ? base + 1 : json;
	length = strlen(base);
	if (length > 5 && !strcmp(base + length - 5, ".json"))
		length -= 5;
	if (length > 63)
		length = 63;
	memcpy(name, base, length);
	name[length] = 0;
}

static void scan_item(const char *item_folder, const char *item_id)
{
	static struct steam_workshop_item_text item;
	static struct steam_workshop_map_text map;
	static struct mcc_manifest campaign;
	char path[STEAM_WORKSHOP_PATH_SIZE], maps_folder[STEAM_WORKSHOP_PATH_SIZE], found[256], file_name[96];
	char reason[128], name[64];
	char *text;
	int length, index, count, manifest = 0;

	if (!resolve(item_folder, "ModInfo.json", path, sizeof(path)))
		return;
	if (!(text = read_file(path, &length)))
	{
		steam_workshop_unlisted(item_id, "", "Item info unreadable", "its ModInfo.json can't be read");
		return;
	}
	index = steam_workshop_read_item(text, length, &item);
	free(text);
	if (!index)
	{
		steam_workshop_unlisted(item_id, "", "Item info unreadable", "its ModInfo.json can't be read");
		return;
	}
	/* (another game's item: not ours to list) */
	if (!item.halo1)
		return;
	if (!resolve(item_folder, "maps", maps_folder, sizeof(maps_folder)) || !is_directory(maps_folder))
	{
		steam_workshop_unlisted(item_id, "", "No maps", "it has no maps folder");
		return;
	}
	/* Campaign missions are always listed, as a maps_mcc package's are; game.mcc_campaigns
	only adds their order and progression (mcc_maps.c) */
	campaign.count = 0;
	if (item.has_campaign)
	{
		manifest = mcc_manifest_load(item_folder, &campaign, reason);
		if (manifest < 0)
		{
			fprintf(stderr, "MCC package '%s' (%s): campaign metadata rejected: %s\n",
				item.title, item_id, reason);
			steam_workshop_unlisted(item_id, "", "Campaign info unreadable", "its CampaignInfo.json or a mission's json was refused");
		}
		else if (!manifest)
			steam_workshop_unlisted(item_id, "", "Campaign info missing", "HasCampaign is set but there is no CampaignInfo.json");
	}
	count = item.map_count + campaign.count;
	for (index = 0; index < count; index++)
	{
		struct steam_workshop_map *entry = &catalog.maps[catalog.count];

		if (index < item.map_count)
		{
			json_map_name(item.map_json[index], name);
			if (!resolve(item_folder, item.map_json[index], path, sizeof(path)))
			{
				steam_workshop_unlisted(item_id, name, "Map info missing", "its json is missing");
				continue;
			}
			if (!(text = read_file(path, &length)))
			{
				steam_workshop_unlisted(item_id, name, "Map info unreadable", "its json can't be read");
				continue;
			}
			length = steam_workshop_read_map(text, length, &map);
			free(text);
			if (!length)
			{
				steam_workshop_unlisted(item_id, name, "Map info unreadable", "its json can't be read");
				continue;
			}
		}
		else
			map = campaign.missions[index - item.map_count];
		snprintf(file_name, sizeof(file_name), "%s.map", map.name);
		if (!posix_find_entry_case_insensitive(maps_folder, file_name, found, sizeof(found)) ||
			strlen(found) >= sizeof(entry->name) + 4)
		{
			steam_workshop_unlisted(item_id, map.name, "Map missing", "its .map file is not in the maps folder");
			continue;
		}
		if (catalog.count >= STEAM_WORKSHOP_MAXIMUM_MAPS)
		{
			steam_workshop_unlisted(item_id, map.name, "Too many maps", "the Workshop catalog is full");
			continue;
		}
		if (!join(entry->path, sizeof(entry->path), maps_folder, found) || !is_file(entry->path))
		{
			steam_workshop_unlisted(item_id, map.name, "Map unreadable", "its .map file can't be opened");
			continue;
		}
		/* the name as the file is spelled on disk */
		found[strlen(found) - 4] = 0;
		if (!found[0])
			continue;
		copy_text(entry->item_id, sizeof(entry->item_id), item_id);
		copy_text(entry->item_title, sizeof(entry->item_title), item.title);
		copy_text(entry->name, sizeof(entry->name), found);
		copy_text(entry->title, sizeof(entry->title), map.title[0] ? map.title : found);
		entry->preview[0] = 0;
		if (map.thumbnail[0] && !resolve(item_folder, map.thumbnail, entry->preview, sizeof(entry->preview)))
			entry->preview[0] = 0;
		catalog.count++;
	}
}

static void scan_library(const char *library)
{
	char content[STEAM_WORKSHOP_PATH_SIZE], item_folder[STEAM_WORKSHOP_PATH_SIZE], name[256];
	void *directory;

	if (!join(content, sizeof(content), library, CONTENT_FOLDER) || !(directory = posix_directory_open(content)))
		return;
	while (posix_directory_next(directory, name, sizeof(name)))
	{
		/* an item already scanned is the same item seen through another path to its library */
		if (!digits(name) || has_item(name) || !join(item_folder, sizeof(item_folder), content, name) ||
			!is_directory(item_folder))
			continue;
		scan_item(item_folder, name);
	}
	posix_directory_close(directory);
}

#ifndef _WIN32
static int linux_root(char *path, const char *base, const char *suffix)
{
	int written;
	const char *c;

	/* Environment hints are literal absolute paths, never shell expressions.
	 * Reject long values rather than looking in a truncated, different folder. */
	if (!base || base[0] != '/')
		return 0;
	for (c = base; *c; c++)
	{
		if ((unsigned char)*c < 0x20 || *c == 0x7F)
			return 0;
	}
	written = snprintf(path, STEAM_WORKSHOP_PATH_SIZE, "%s%s%s", base, suffix[0] ? "/" : "", suffix);
	return written > 0 && written < STEAM_WORKSHOP_PATH_SIZE;
}

static int linux_roots(char (*paths)[STEAM_WORKSHOP_PATH_SIZE], int maximum)
{
	static const char *const places[] = { ".steam/steam", ".steam/root", ".local/share/Steam",
		".steam/debian-installation", ".var/app/com.valvesoftware.Steam/.local/share/Steam",
		"snap/steam/common/.local/share/Steam" };
	const char *home = getenv("HOME");
	int count = 0, index;

	/* Steam launch contexts can supply their client root. XDG relocates the
	 * user's data directory; old home locations remain available as fallbacks. */
	if (count < maximum && linux_root(paths[count], getenv("STEAM_COMPAT_CLIENT_INSTALL_PATH"), ""))
		count++;
	if (count < maximum && linux_root(paths[count], getenv("XDG_DATA_HOME"), "Steam"))
		count++;
	for (index = 0; index < (int)(sizeof(places) / sizeof(places[0])) && count < maximum; index++)
	{
		if (linux_root(paths[count], home, places[index]))
			count++;
	}
	return count;
}
#endif

const char *platform_data_root(void);

/* the Steam folders to look in, then the libraries those name: library paths, each once */
static char own_library[STEAM_WORKSHOP_PATH_SIZE];
/* MCC's shared files were asked for, and not found */
static int shared_missing;

void steam_workshop_set_own_library(const char *path)
{
	snprintf(own_library, sizeof(own_library), "%s", path ? path : "");
}

static int libraries(char (*paths)[STEAM_WORKSHOP_PATH_SIZE], int maximum)
{
	char roots[MAXIMUM_STEAM_ROOTS][STEAM_WORKSHOP_PATH_SIZE];
	int root_count = 0, count = 0, root, index;
	const char *setting = config_string("paths.steam");

	if (setting && setting[0])
		snprintf(roots[root_count++], STEAM_WORKSHOP_PATH_SIZE, "%s", setting);
	/* (the game's own library: its downloads, read before the Workshop tab has opened this run) */
	if (!own_library[0])
	{
		const char *downloads = config_string("paths.steam_downloads"), *data = platform_data_root();

		if (downloads && downloads[0])
			snprintf(own_library, sizeof(own_library), "%s", downloads);
		else if (data && data[0] && snprintf(own_library, sizeof(own_library), "%s/steam", data) >= (int)sizeof(own_library))
			own_library[0] = 0;
	}
	if (own_library[0])
		snprintf(roots[root_count++], STEAM_WORKSHOP_PATH_SIZE, "%s", own_library);
#ifdef _WIN32
	{
		const char *program_files[] = { getenv("ProgramFiles(x86)"), getenv("ProgramFiles") };

		/* Keep the configured root ahead of automatic discovery. */
		root_count += steam_workshop_windows_roots(roots + root_count, MAXIMUM_STEAM_ROOTS - root_count);
		for (index = 0; index < 2; index++)
		{
			if (program_files[index] && program_files[index][0] && root_count < MAXIMUM_STEAM_ROOTS)
				snprintf(roots[root_count++], STEAM_WORKSHOP_PATH_SIZE, "%s/Steam", program_files[index]);
		}
		if (root_count < MAXIMUM_STEAM_ROOTS)
			snprintf(roots[root_count++], STEAM_WORKSHOP_PATH_SIZE, "C:/Program Files (x86)/Steam");
	}
#else
	root_count += linux_roots(roots + root_count, MAXIMUM_STEAM_ROOTS - root_count);
#ifdef __APPLE__
	/* (macOS's Steam, after the Linux places a Mac never has) */
	if (root_count < MAXIMUM_STEAM_ROOTS &&
		linux_root(roots[root_count], getenv("HOME"), "Library/Application Support/Steam"))
	{
		root_count++;
	}
#endif
#endif
	for (root = 0; root < root_count && count < maximum; root++)
	{
		char vdf[STEAM_WORKSHOP_PATH_SIZE], (*named)[STEAM_WORKSHOP_PATH_SIZE];
		char *text;
		int length, named_count, other;

		/* the root itself is a library (a setting may name a library folder as well) */
		for (other = 0; other < count && strcmp(paths[other], roots[root]); other++)
			;
		if (other == count && is_directory(roots[root]))
			snprintf(paths[count++], STEAM_WORKSHOP_PATH_SIZE, "%s", roots[root]);
		if (!join(vdf, sizeof(vdf), roots[root], "steamapps/libraryfolders.vdf") ||
			!(text = read_file(vdf, &length)))
			continue;
		named = malloc(sizeof(*named) * STEAM_WORKSHOP_MAXIMUM_LIBRARIES);
		if (named)
		{
			named_count = steam_workshop_read_libraries(text, length, named, STEAM_WORKSHOP_MAXIMUM_LIBRARIES);
			for (index = 0; index < named_count && count < maximum; index++)
			{
				for (other = 0; other < count && strcmp(paths[other], named[index]); other++)
					;
				if (other == count && is_directory(named[index]))
					snprintf(paths[count++], STEAM_WORKSHOP_PATH_SIZE, "%s", named[index]);
			}
			free(named);
		}
		free(text);
	}
	return count;
}

int steam_workshop_libraries(char (*paths)[STEAM_WORKSHOP_PATH_SIZE], int maximum)
{
	return libraries(paths, maximum);
}

int steam_workshop_scan(void)
{
	char (*paths)[STEAM_WORKSHOP_PATH_SIZE];
	int count, index;

	catalog.scanned = 1;
	catalog.count = 0;
	catalog.unlisted_count = 0;
	catalog.enabled = discovery_enabled();
	if (!catalog.enabled)
		return 0;
	paths = malloc(sizeof(*paths) * 2 * STEAM_WORKSHOP_MAXIMUM_LIBRARIES);
	if (!paths)
		return 0;
	count = libraries(paths, 2 * STEAM_WORKSHOP_MAXIMUM_LIBRARIES);
	for (index = 0; index < count; index++)
		scan_library(paths[index]);
	free(paths);
	return catalog.count;
}

/* the maps folders of the Workshop items found (steam_workshop_scan's,
scanned now if not yet), each once, with its item's id: their count, at
most maximum; 0 when game.workshop_maps is off. For the map lists and their
folder watcher: these are the folders to watch */
int steam_workshop_map_folders(char (*folders)[STEAM_WORKSHOP_PATH_SIZE],
	char (*item_ids)[STEAM_WORKSHOP_ID_SIZE], int maximum)
{
	int index, count = 0, other;

	if (!catalog.scanned)
		steam_workshop_scan();
	for (index = 0; index < catalog.count && count < maximum; index++)
	{
		char folder[STEAM_WORKSHOP_PATH_SIZE], *separator;

		snprintf(folder, sizeof(folder), "%s", catalog.maps[index].path);
		slashes(folder);
		separator = strrchr(folder, '/');
		if (!separator)
			continue;
		*separator = 0;
		for (other = 0; other < count && strcmp(folders[other], folder); other++)
			;
		if (other < count)
			continue;
		snprintf(folders[count], STEAM_WORKSHOP_PATH_SIZE, "%s", folder);
		if (item_ids)
			snprintf(item_ids[count], STEAM_WORKSHOP_ID_SIZE, "%s", catalog.maps[index].item_id);
		count++;
	}
	return count;
}

int steam_workshop_count(void)
{
	if (!catalog.scanned || catalog.enabled != discovery_enabled())
		steam_workshop_scan();
	return catalog.count;
}

const struct steam_workshop_map *steam_workshop_map(int index)
{
	return index >= 0 && index < steam_workshop_count() ? &catalog.maps[index] : NULL;
}

static const struct steam_workshop_map *item_map(const char *item_id)
{
	int index, count = steam_workshop_count();

	for (index = 0; item_id && index < count; index++)
	{
		if (!strcmp(catalog.maps[index].item_id, item_id))
			return &catalog.maps[index];
	}
	return (void *)0;
}

int steam_workshop_package_folder(const char *item_id, char *folder, unsigned long size)
{
	const struct steam_workshop_map *map = item_map(item_id);
	char *last;

	if (!map || strlen(map->path) >= size)
		return 0;
	strcpy(folder, map->path);
	last = strrchr(folder, '/');
	if (!last)
		last = strrchr(folder, '\\');
	if (!last)
		return 0;
	*last = 0;
	return 1;
}

/* Only the two resource names in MCC package/shared namespaces are redirected.
 * Keep validation, complete-reference matching and fingerprints in mcc_resources.c. */
/* port: the data root's own copy of relative (and resource in it, when
named) in maps_mcc or GulchCE's mcc_maps (mcc_maps.h) */
static int local_mcc(const char *data_root, const char *relative, const char *resource, char *found, unsigned size)
{
	static const char *const folders[] = { "maps_mcc", "mcc_maps" };
	char local[200];
	int index;

	for (index = 0; index < 2; index++)
	{
		if (resource)
			snprintf(local, sizeof(local), "%s/%s/%s", folders[index], relative, resource);
		else
			snprintf(local, sizeof(local), "%s/%s", folders[index], relative);
		if (resolve(data_root, local, found, size))
			return 1;
	}
	return 0;
}

/* MCC's install folder in a Steam library: its app manifest's installdir
(steamapps/appmanifest_976730.acf: any name, "Halo-MCC" too), else Steam's
default, "Halo The Master Chief Collection" */
static int mcc_install_folder(const char *library, char *out, unsigned size)
{
	char manifest[STEAM_WORKSHOP_PATH_SIZE], folder[256], common[STEAM_WORKSHOP_PATH_SIZE];
	char *text, *at;
	int length;

	snprintf(folder, sizeof(folder), "%s", "Halo The Master Chief Collection");
	if (join(manifest, sizeof(manifest), library, "steamapps/appmanifest_" STEAM_APP ".acf") &&
		(text = read_file(manifest, &length)))
	{
		/* "installdir"<white space>"<name>": a plain folder name, no
		separators, dots alone or control characters */
		at = strstr(text, "\"installdir\"");
		if (at)
		{
			unsigned used = 0;

			at += 12;
			while (*at == ' ' || *at == '\t')
				at++;
			if (*at == '"')
			{
				for (at++; *at && *at != '"' && *at != '\n' && used + 1 < sizeof(folder); at++)
				{
					if ((unsigned char)*at < 0x20 || *at == '/' || *at == '\\' || *at == ':')
						break;
					folder[used++] = *at;
				}
				if (*at == '"' && used && strspn(folder, ".") != used)
					folder[used] = 0;
				else
					snprintf(folder, sizeof(folder), "%s", "Halo The Master Chief Collection");
			}
		}
		free(text);
	}
	if (join(common, sizeof(common), library, "steamapps/common") && resolve(common, folder, out, size) &&
		is_directory(out))
		return 1;
	/* (steamcmd's force_install_dir: an install folder holding its own
	steamapps/appmanifest_976730.acf, the library itself or a folder in its
	steamapps/common, whatever its name) */
	if (join(manifest, sizeof(manifest), library, "halo1/maps") && is_directory(manifest) &&
		join(manifest, sizeof(manifest), library, "steamapps/appmanifest_" STEAM_APP ".acf") && is_file(manifest))
	{
		snprintf(out, size, "%s", library);
		return strlen(library) < size;
	}
	{
		void *directory = join(common, sizeof(common), library, "steamapps/common") ? posix_directory_open(common) : NULL;
		char name[256], candidate[STEAM_WORKSHOP_PATH_SIZE];
		int found = 0;

		while (directory && !found && posix_directory_next(directory, name, sizeof(name)))
		{
			if (name[0] == '.' || !join(candidate, sizeof(candidate), common, name) ||
				!join(manifest, sizeof(manifest), candidate, "steamapps/appmanifest_" STEAM_APP ".acf") ||
				!is_file(manifest) || strlen(candidate) >= size)
				continue;
			snprintf(out, size, "%s", candidate);
			found = 1;
		}
		if (directory)
			posix_directory_close(directory);
		return found;
	}
}

int steam_workshop_resource_path(const char *request, const char *data_root, char *path, unsigned long size)
{
	char relative[128], local[160], package[STEAM_WORKSHOP_PATH_SIZE];
	char found[STEAM_WORKSHOP_PATH_SIZE], resource[64], *separator;
	char (*paths)[STEAM_WORKSHOP_PATH_SIZE];
	const char *installed;
	unsigned index;
	int count, library, result = 0;

	if (!discovery_enabled() || strlen(request) >= sizeof(relative) ||
		strlen(data_root) >= STEAM_WORKSHOP_PATH_SIZE)
		return 0;
	for (index = 0; request[index]; index++)
	{
		char c = request[index];
		relative[index] = c == '\\' ? '/' : c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
	}
	relative[index] = 0;
	separator = strrchr(relative, '/');
	if (!separator || (strcmp(separator + 1, "bitmaps.map") && strcmp(separator + 1, "sounds.map")))
		return 0;
	strcpy(resource, separator + 1);
	*separator = 0;
	if (digits(relative))
	{
		/* A manually installed package, or any beside-level resource entry,
		 * stays authoritative even when incomplete, invalid or unreadable. */
		if (local_mcc(data_root, relative, NULL, found, sizeof(found)) ||
			!steam_workshop_package_folder(relative, package, sizeof(package)) ||
			resolve(package, resource, found, sizeof(found)))
			return 0;
		slashes(package);
		separator = strrchr(package, '/');
		if (!separator) return 0;
		*separator = 0;
		snprintf(local, sizeof(local), "halo1/maps/%s", resource);
		return resolve(package, local, path, size);
	}
	if (!strcmp(relative, "_shared/singleplayer"))
		installed = "halo1/maps";
	else if (!strcmp(relative, "_shared/multiplayer"))
		installed = "halo1/maps/custom_edition";
	else
		return 0;
	if (local_mcc(data_root, relative, resource, found, sizeof(found)))
		return 0;
	/* The same ordered Steam libraries as map discovery. An explicit local
	 * shared file wins; no other Workshop item's resources are searched. */
	paths = malloc(sizeof(*paths) * 2 * STEAM_WORKSHOP_MAXIMUM_LIBRARIES);
	if (!paths) return 0;
	count = libraries(paths, 2 * STEAM_WORKSHOP_MAXIMUM_LIBRARIES);
	for (library = 0; library < count; library++)
	{
		char install[STEAM_WORKSHOP_PATH_SIZE];

		if (mcc_install_folder(paths[library], install, sizeof(install)) &&
			resolve(install, installed, package, sizeof(package)) &&
			resolve(package, resource, path, size))
		{
			result = 1;
			break;
		}
	}
	free(paths);
	/* (an item that needs MCC's own shared files, and they aren't here) */
	if (!result)
		shared_missing = 1;
	return result;
}

int steam_workshop_shared_missing(void)
{
	return shared_missing;
}

int steam_workshop_item_title(const char *item_id, char *title, unsigned long size)
{
	const struct steam_workshop_map *map = item_map(item_id);

	if (!map)
		return 0;
	copy_text(title, (unsigned)size, map->item_title);
	return 1;
}
#endif /* HALO_MCC */
