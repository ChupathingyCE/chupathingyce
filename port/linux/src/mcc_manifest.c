/* CampaignMaps order is authoritative. Independently implemented from the
 * public Excession package format and locally inspected Halo 1 manifests. */
#ifndef HALO_MCC
#include "../include/halo_port_features.h"
#endif
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h, above): a console builds this to nothing */
#include "mcc_manifest.h"
#include "mcc_json.h" /* port: our own JSON lookups, json_reader.h */
#include "../game/mcc_sha256.h"
#include "posix.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* xbox_files.c; only the existing virtual file service crosses this boundary. */
extern void platform_translate_path(const char *, char *, unsigned long);

static int equal(const char *a, const char *b)
{
	while (*a && *b)
	{
		int x = *a == '\\' ? '/' : tolower((unsigned char)*a);
		int y = *b == '\\' ? '/' : tolower((unsigned char)*b);
		if (x != y) return 0;
		a++; b++;
	}
	return !*a && !*b;
}

static int relative_path(const char *path)
{
	const char *part = path, *c;
	if (!*path || *path == '/' || *path == '\\') return 0;
	for (c = path; ; c++)
	{
		if (*c && ((unsigned char)*c < 32 || *c == 127 || strchr(":*?\"<>|", *c))) return 0;
		if (!*c || *c == '/' || *c == '\\')
		{
			size_t n = (size_t)(c - part);
			if (!n || (n == 1 && *part == '.') || (n == 2 && !memcmp(part, "..", 2))) return 0;
			if (!*c) break;
			part = c + 1;
		}
	}
	return 1;
}

static char *read_relative(const char *root, const char *relative, int *length)
{
	char path[1024], part[128], found[256], next[1024];
	const char *c = relative;
	char *text;
	FILE *file;
	size_t got;
	*length = 0;
	if (!relative_path(relative) || strlen(root) >= sizeof(path)) return NULL;
	strcpy(path, root);
	while (*c)
	{
		size_t n = strcspn(c, "/\\");
		if (n >= sizeof(part)) return NULL;
		memcpy(part, c, n); part[n] = 0;
		if (!posix_find_entry_case_insensitive(path, part, found, sizeof(found)) ||
			snprintf(next, sizeof(next), "%s/%s", path, found) >= (int)sizeof(next)) return NULL;
		strcpy(path, next);
		c += n;
		if (*c) c++;
	}
	file = fopen(path, "rb");
	if (!file) return NULL;
	text = malloc(STEAM_WORKSHOP_MAXIMUM_JSON + 1);
	if (!text) { fclose(file); return NULL; }
	got = fread(text, 1, STEAM_WORKSHOP_MAXIMUM_JSON + 1, file);
	if (ferror(file) || got > STEAM_WORKSHOP_MAXIMUM_JSON) { free(text); fclose(file); return NULL; }
	fclose(file);
	*length = (int)got;
	return text;
}

/* port: (mcc_json.h) the file's text, one JSON object, and its length */
static int document_read(const char *root, const char *relative, int *length, char **text)
{
	*text = read_relative(root, relative, length);
	if (!*text) return 0;
	if (!mcc_json_valid(*text, *length)) { free(*text); *text = NULL; return 0; }
	return 1;
}

static const char *const campaign_path[] = { "GameModContents", "HasCampaign", NULL };
static const char *const engine_path[] = { "Engine", NULL };
static const char *const missions_path[] = { "CampaignMaps", NULL };
static const char *const title_path[] = { "Title", "Neutral", NULL };
static const char *const kind_path[] = { "CampaignMapKind", NULL };
static const char *const scenario_path[] = { "ScenarioFile", NULL };

int mcc_manifest_default_kind(const char *kind)
{
	const char *expected = "default";
	while (*kind && *expected)
	{
		unsigned char c = (unsigned char)*kind++;
		if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		if (c != (unsigned char)*expected++) return 0;
	}
	return !*kind && !*expected;
}

int mcc_manifest_load(const char *folder, struct mcc_manifest *manifest, char reason[128])
{
	struct json value;
	char *text, paths[MCC_MANIFEST_MAXIMUM_MISSIONS][128], engine[32];
	char found[256];
	int length, count, index, other, enabled = 0;
	memset(manifest, 0, sizeof(*manifest)); reason[0] = 0;
	if (!posix_find_entry_case_insensitive(folder, "CampaignInfo.json", found, sizeof(found))) return 0;
	strcpy(reason, "Invalid or incomplete Halo 1 campaign metadata.");
	if (!document_read(folder, "ModInfo.json", &length, &text)) return -1;
	mcc_json_boolean(text, length, campaign_path, &enabled);
	if (!mcc_json_string(text, length, engine_path, engine, sizeof(engine))) engine[0] = 0;
	free(text);
	if (strcmp(engine, "Halo1") || !enabled) return -1;
	if (!document_read(folder, "CampaignInfo.json", &length, &text)) return -1;
	/* (an element that is not a string that fits refuses the manifest, as
	 * the loop below did one by one) */
	count = mcc_json_strings(text, length, missions_path, paths[0], sizeof(paths[0]), sizeof(paths[0]),
		MCC_MANIFEST_MAXIMUM_MISSIONS);
	if (count < 1) count = count == -3 ? -1 : 0;
	if (!mcc_json_string(text, length, title_path, manifest->title, sizeof(manifest->title)))
		manifest->title[0] = 0;
	for (index = 0; index < count; index++)
	{
		if (!relative_path(paths[index])) break;
		for (other = 0; other < index; other++) if (equal(paths[other], paths[index])) break;
		if (other != index) break;
	}
	free(text);
	if (count <= 0 || index != count) return -1;
	for (index = 0; index < count; index++)
	{
		char kind[32], scenario[256];
		int valid;
		text = read_relative(folder, paths[index], &length);
		valid = steam_workshop_read_map(text, length, &manifest->missions[index]);
		if (!valid) { free(text); break; }
		/* (no kind is the default; one that is there must be a string that says so) */
		valid = !mcc_json_find(text, length, kind_path, &value) ||
			(mcc_json_string(text, length, kind_path, kind, sizeof(kind)) && mcc_manifest_default_kind(kind));
		valid = valid && mcc_json_string(text, length, scenario_path, scenario, sizeof(scenario)) &&
			relative_path(scenario);
		free(text);
		if (!valid) { strcpy(reason, "Unsupported campaign mission kind or scenario path."); break; }
		for (other = 0; other < index; other++)
			if (equal(manifest->missions[other].name, manifest->missions[index].name)) break;
		if (other != index) { strcpy(reason, "Duplicate campaign mission filenames are ambiguous."); break; }
	}
	if (index != count) { memset(manifest, 0, sizeof(*manifest)); return -1; }
	manifest->count = count; reason[0] = 0;
	return 1;
}

int mcc_manifest_package_root(const char *level, char path[1024])
{
	char request[96], steam[STEAM_WORKSHOP_PATH_SIZE], package[64], *last;
	const char *separator;
	size_t n;
	path[0] = 0;
	if (!level || strncmp(level, "mcc_maps\\", 9) || !(separator = strchr(level + 9, '\\'))) return 0;
	n = (size_t)(separator - level - 9);
	if (!n || n >= sizeof(package) || strlen(level) > 63 || !relative_path(level)) return 0;
	memcpy(package, level + 9, n); package[n] = 0;
	/* Resolve the package itself: local Workshop maps may be one directory
	 * below their metadata, while flattened manual maps remain beside it. */
	snprintf(request, sizeof(request), "d:\\mcc_maps\\%s", package);
	platform_translate_path(request, path, 1024);
	for (last = path; *last; last++) if (*last == '\\') *last = '/';
	/* Compare the actual resolution, not just the item id: local overrides
     * must never inherit a different installation's order or titles. */
	if (steam_workshop_package_folder(package, steam, sizeof(steam)) && equal(path, steam))
	{
		last = strrchr(path, '/'); if (!last) return 0; *last = 0;
	}
	return 1;
}

/* SHA-256 of "MCC package metadata 1", then of ModInfo.json, CampaignInfo.json
 * when the package has a campaign, and the one multiplayer map json whose
 * ScenarioFile is the level, sorted by their paths (lowercase, '/'
 * separators), each as its path, a zero, its length (4 bytes, little-endian)
 * and its bytes: what OpenCE's clients compare with their own copy of the
 * package before they load it (docs/mcc.md) */
int mcc_manifest_metadata_digest(const char *level, unsigned char digest[32])
{
	char root[1024], paths[3][160], reason[128], *texts[3] = { NULL, NULL, NULL }, *c;
	const char *leaf;
	int sizes[3] = { 0, 0, 0 }, order[3] = { 0, 1, 2 };
	int count = 0, index, chosen = -1, matches = 0, made = 0;
	/* (on the heap: an item's text holds up to 1,024 map paths) */
	struct steam_workshop_item_text *item = NULL;
	struct mcc_manifest *manifest = NULL;
	struct mcc_sha256 sha;

	memset(digest, 0, 32);
	leaf = level ? strrchr(level, '\\') : NULL;
	if (!leaf || !mcc_manifest_package_root(level, root)) return 0;
	leaf++;
	item = (struct steam_workshop_item_text *)calloc(1, sizeof(*item));
	manifest = (struct mcc_manifest *)calloc(1, sizeof(*manifest));
	if (!item || !manifest) goto done;
	texts[0] = read_relative(root, "ModInfo.json", &sizes[0]);
	if (!texts[0] || !steam_workshop_read_item(texts[0], sizes[0], item) || !item->halo1) goto done;
	strcpy(paths[count++], "modinfo.json");
	if (item->has_campaign)
	{
		/* (a campaign level's json comes from the campaign's plan, not made here) */
		if (mcc_manifest_load(root, manifest, reason) > 0)
			for (index = 0; index < manifest->count; index++)
				if (!strcmp(manifest->missions[index].name, leaf)) goto done;
		if (!(texts[count] = read_relative(root, "CampaignInfo.json", &sizes[count]))) goto done;
		strcpy(paths[count++], "campaigninfo.json");
	}
	for (index = 0; index < item->map_count; index++)
	{
		struct steam_workshop_map_text map;
		int size = 0;
		char *json = read_relative(root, item->map_json[index], &size);

		memset(&map, 0, sizeof(map));
		if (json && steam_workshop_read_map(json, size, &map) && !strcmp(map.name, leaf))
		{
			matches++;
			if (chosen < 0) { chosen = index; texts[count] = json; sizes[count] = size; json = NULL; }
		}
		free(json);
	}
	/* (exactly one map entry is the level's) */
	if (matches != 1 || strlen(item->map_json[chosen]) >= sizeof(paths[count])) goto done;
	strcpy(paths[count], item->map_json[chosen]);
	for (c = paths[count]; *c; c++) *c = *c == '\\' ? '/' : (char)tolower((unsigned char)*c);
	count++;
	/* (by path, bytewise) */
	for (index = 1; index < count; index++)
	{
		int at = index, moved = order[index];

		while (at > 0 && strcmp(paths[order[at - 1]], paths[moved]) > 0) { order[at] = order[at - 1]; at--; }
		order[at] = moved;
	}
	mcc_sha256_begin(&sha);
	mcc_sha256_add(&sha, "MCC package metadata 1", 22);
	for (index = 0; index < count; index++)
	{
		int which = order[index];
		unsigned char length[4];

		length[0] = (unsigned char)sizes[which];
		length[1] = (unsigned char)(sizes[which] >> 8);
		length[2] = (unsigned char)(sizes[which] >> 16);
		length[3] = (unsigned char)(sizes[which] >> 24);
		mcc_sha256_add(&sha, paths[which], (int)strlen(paths[which]) + 1);
		mcc_sha256_add(&sha, length, 4);
		mcc_sha256_add(&sha, texts[which], sizes[which]);
	}
	mcc_sha256_end(&sha, digest);
	made = 1;
done:
	for (index = 0; index < 3; index++) free(texts[index]);
	free(item);
	free(manifest);
	return made;
}

int mcc_manifest_for_level(const char *level, struct mcc_manifest *manifest, char reason[128])
{
	char path[1024];
	memset(manifest, 0, sizeof(*manifest)); reason[0] = 0;
	return mcc_manifest_package_root(level, path) ? mcc_manifest_load(path, manifest, reason) : 0;
}
#endif /* HALO_MCC */
