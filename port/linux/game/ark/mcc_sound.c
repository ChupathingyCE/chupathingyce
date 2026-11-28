/*
MCC_SOUND.C

(port) The sounds of MCC's own Halo CE maps (mcc_sound.h).
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "cseries_windows.h"
#include "cache_file_formats.h"
#include "mcc_sound.h"
#include "fsb5.h"
#include "mcc_sound_list.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);
unsigned long long vita_host_time_us(void);

/* ---------- constants */

/* the banks' files and lists (MCC_SOUND_BANK_*) */
static char const *const bank_files[NUMBER_OF_MCC_SOUND_BANKS] =
{
	"sounds_adpcm.fsb",
	"sounds_dialogs.fsb",
	"sounds_music.fsb",
	"sounds_stream.fsb",
	"sounds_debug.fsb",
};
static char const *const bank_lists[NUMBER_OF_MCC_SOUND_BANKS] =
{
	"lst\\sounds_adpcm.lst.bin",
	"lst\\sounds_dialogs.lst.bin",
	"lst\\sounds_music.lst.bin",
	"lst\\sounds_stream.lst.bin",
	"lst\\sounds_debug.lst.bin",
};
/* the largest list file read (the dialogue's is 121 KB) */
#define LIST_MAXIMUM_BYTES (4u * 1024u * 1024u)

/* ---------- globals */

struct mcc_sound_bank
{
	/* the list (every bank's: a sound is looked for in all of them) */
	boolean list_loaded;
	struct mcc_sound_list list;
	/* the bank's file and its table (the ADPCM bank's opened with the
	lists; a CELT bank's the first time a sound is found in its list) */
	boolean tried;
	boolean opened;
	HANDLE file;
	struct fsb5 fsb;
};

static struct
{
	boolean opened;
	struct mcc_sound_bank banks[NUMBER_OF_MCC_SOUND_BANKS];
	boolean trace;
	/* when it opened (for the log) */
	unsigned long long opened_at;
} mcc_sound_globals;

/* the CELT banks' files for the decoding (mcc_sound_celt_read): opened
the first time, kept open */
static struct
{
	pthread_mutex_t lock;
	HANDLE files[NUMBER_OF_MCC_SOUND_BANKS];
	boolean tried[NUMBER_OF_MCC_SOUND_BANKS];
} mcc_sound_readers = { PTHREAD_MUTEX_INITIALIZER };

/* ---------- private code */

static int file_read(void *context, uint64_t offset, uint32_t size, void *buffer)
{
	uint32_t done = 0;

	if (offset > 0xFFFFFFFFull || (uint64_t)offset + size > 0xFFFFFFFFull)
		return 0;
	while (done < size)
	{
		OVERLAPPED position;
		DWORD request = size - done > 0x100000 ? 0x100000 : size - done;
		DWORD read = 0;

		memset(&position, 0, sizeof(position));
		position.Offset = (DWORD)offset + done;
		if (!ReadFile((HANDLE)context, (byte *)buffer + done, request, &read, &position) || !read)
			return 0;
		done += read;
	}
	return 1;
}

static boolean bank_path(int bank, char *path, size_t path_size)
{
	return snprintf(path, path_size, "%s%s", MCC_SOUND_DIRECTORY, bank_files[bank]) < (int)path_size;
}

/* a whole small file into a malloc'd buffer; NULL when it is not there or
larger than LIST_MAXIMUM_BYTES */
static uint8_t *file_load(char const *name, uint32_t *size)
{
	char path[256];
	HANDLE handle;
	DWORD bytes;
	uint8_t *buffer;

	if (snprintf(path, sizeof(path), "%s%s", MCC_SOUND_DIRECTORY, name) >= (int)sizeof(path))
		return NULL;
	handle = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (handle == INVALID_HANDLE_VALUE)
		return NULL;
	bytes = GetFileSize(handle, NULL);
	if (bytes == INVALID_FILE_SIZE || bytes > LIST_MAXIMUM_BYTES || !(buffer = (uint8_t *)malloc(bytes ? bytes : 1)))
	{
		CloseHandle(handle);
		return NULL;
	}
	if (!file_read(handle, 0, bytes, buffer))
	{
		free(buffer);
		CloseHandle(handle);
		return NULL;
	}
	CloseHandle(handle);
	*size = bytes;
	return buffer;
}

/* a CELT bank's file and table, opened the first time a sound is found in
its list; FALSE when it is not there or not whole (said once) */
static boolean celt_bank_open(int bank)
{
	struct mcc_sound_bank *entry = &mcc_sound_globals.banks[bank];
	char path[256];
	char detail[96];
	DWORD bytes;

	if (entry->tried)
		return entry->opened;
	entry->tried = TRUE;
	if (!bank_path(bank, path, sizeof(path)))
		return FALSE;
	entry->file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (entry->file == INVALID_HANDLE_VALUE)
	{
		entry->file = NULL;
		error(_error_silent, "mcc sound: %s is not in MCC's sound folder: its sounds are silent", bank_files[bank]);
		return FALSE;
	}
	bytes = GetFileSize(entry->file, NULL);
	if (bytes == INVALID_FILE_SIZE ||
		!fsb5_open(file_read, entry->file, bytes, 1, &entry->fsb, detail, sizeof(detail)) ||
		entry->fsb.format != _fsb5_format_celt)
	{
		error(_error_silent, "mcc sound: %s is refused (%s): its sounds are silent", bank_files[bank],
			bytes == INVALID_FILE_SIZE ? "no size" : entry->fsb.streams ? "not CELT" : detail);
		fsb5_close(&entry->fsb);
		CloseHandle(entry->file);
		entry->file = NULL;
		return FALSE;
	}
	entry->opened = TRUE;
	return TRUE;
}

/* the classic twin of a sound MCC's classic mode plays in its place (its
old_ folder's), or FALSE */
static boolean classic_twin(char const *path, char *twin, size_t twin_size)
{
	if (!_strnicmp(path, "sound\\old_", 10))
		return FALSE;
	if (!_strnicmp(path, "sound\\sfx\\", 10))
		return snprintf(twin, twin_size, "sound\\old_sfx\\%s", path + 10) < (int)twin_size;
	if (!_strnicmp(path, "sound\\", 6))
		return snprintf(twin, twin_size, "sound\\old_music\\%s", path + 6) < (int)twin_size;
	return FALSE;
}

static int source_find(void *context, char const *path, int *bank_found, uint32_t *first, uint32_t *count, int *elsewhere)
{
	char twin[MCC_SOUND_LIST_MAXIMUM_PATH_BYTES];
	char const *candidates[2];
	int candidate_count = 0;
	int candidate;

	(void)context;
	*elsewhere = 0;
	*bank_found = NONE;
	/* the classic sound first (the maps name the Anniversary sounds:
	sound\sfx\... in the ADPCM bank as sound\old_sfx\..., the music's
	sound\music\... and the cinematics' sound\sinomatixx\... in the CELT
	banks as sound\old_music\...), then the path's own */
	if (classic_twin(path, twin, sizeof(twin)))
		candidates[candidate_count++] = twin;
	candidates[candidate_count++] = path;
	for (candidate = 0; candidate < candidate_count; candidate++)
	{
		int bank;

		for (bank = 0; bank < NUMBER_OF_MCC_SOUND_BANKS; bank++)
		{
			struct mcc_sound_bank *entry = &mcc_sound_globals.banks[bank];
			struct mcc_sound_list_record const *record;

			if (!entry->list_loaded || !(record = mcc_sound_list_find(&entry->list, candidates[candidate])))
				continue;
			if (bank == MCC_SOUND_BANK_ADPCM ? entry->opened : celt_bank_open(bank))
			{
				*bank_found = bank;
				*first = record->first;
				*count = record->streams;
				return 1;
			}
			*elsewhere = 1;
		}
	}
	return 0;
}

static int source_stream(void *context, int bank, uint32_t index, struct mcc_sound_stream *stream)
{
	struct mcc_sound_bank const *entry;
	struct fsb5_stream const *fsb_stream;

	(void)context;
	if (bank < 0 || bank >= NUMBER_OF_MCC_SOUND_BANKS)
		return 0;
	entry = &mcc_sound_globals.banks[bank];
	if (!entry->opened || index >= entry->fsb.stream_count)
		return 0;
	fsb_stream = &entry->fsb.streams[index];
	if (fsb_stream->offset > 0x7FFFFFFFull)
		return 0;
	stream->bank = bank;
	stream->offset = (uint32_t)fsb_stream->offset;
	stream->bytes = fsb_stream->bytes;
	stream->samples = fsb_stream->samples;
	stream->rate = fsb_stream->rate;
	stream->channels = fsb_stream->channels;
	/* (the CELT banks' names carry no permutation numbers: "A10_010_CryoTech",
	"altlp1") */
	stream->name_range = bank == MCC_SOUND_BANK_ADPCM ? fsb_stream->name_range : -1;
	stream->name_permutation = bank == MCC_SOUND_BANK_ADPCM ? fsb_stream->name_permutation : -1;
	stream->name = fsb5_stream_name(&entry->fsb, index);
	return 1;
}

static void source_trace(void *context, char const *line)
{
	(void)context;
	platform_log("mcc sound: %s\n", line);
}

/* ---------- public code */

char const *mcc_sound_bank_name(int bank)
{
	return bank >= 0 && bank < NUMBER_OF_MCC_SOUND_BANKS ? bank_files[bank] : "?";
}

void mcc_sound_close(void)
{
	int bank;

	for (bank = 0; bank < NUMBER_OF_MCC_SOUND_BANKS; bank++)
	{
		struct mcc_sound_bank *entry = &mcc_sound_globals.banks[bank];

		mcc_sound_list_free(&entry->list);
		fsb5_close(&entry->fsb);
		if (entry->file)
			CloseHandle(entry->file);
	}
	memset(&mcc_sound_globals, 0, sizeof(mcc_sound_globals));
}

static void reason_set(char *reason, long reason_size, char const *format, char const *detail)
{
	if (reason && reason_size > 0)
		snprintf(reason, (size_t)reason_size, format, detail);
}

boolean mcc_sound_open(
	struct mcc_sound_source *source,
	uint32_t combined_offset,
	uint32_t bank_bytes,
	char *reason,
	long reason_size)
{
	struct mcc_sound_bank *adpcm = &mcc_sound_globals.banks[MCC_SOUND_BANK_ADPCM];
	char path[256];
	char detail[96];
	uint8_t *bytes;
	uint32_t size;
	int bank;

	unsigned long long started = vita_host_time_us();

	memset(source, 0, sizeof(*source));
	memset(&mcc_sound_globals, 0, sizeof(mcc_sound_globals));
	bank_path(MCC_SOUND_BANK_ADPCM, path, sizeof(path));
	adpcm->tried = TRUE;
	adpcm->file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (adpcm->file == INVALID_HANDLE_VALUE)
	{
		adpcm->file = NULL;
		reason_set(reason, reason_size, "%s is not in MCC's sound folder", MCC_SOUND_ADPCM_BANK);
		return FALSE;
	}
	if (!fsb5_open(file_read, adpcm->file, bank_bytes, 1, &adpcm->fsb, detail, sizeof(detail)))
	{
		reason_set(reason, reason_size, "sounds_adpcm.fsb: %s", detail);
		mcc_sound_close();
		return FALSE;
	}
	if (adpcm->fsb.format != _fsb5_format_ima_adpcm)
	{
		reason_set(reason, reason_size, "sounds_adpcm.fsb: %s", "not IMA ADPCM");
		mcc_sound_close();
		return FALSE;
	}
	adpcm->opened = TRUE;
	bytes = file_load(MCC_SOUND_ADPCM_LIST, &size);
	if (!bytes)
	{
		reason_set(reason, reason_size, "%s is not in MCC's sound folder", MCC_SOUND_ADPCM_LIST);
		mcc_sound_close();
		return FALSE;
	}
	if (!mcc_sound_list_parse(bytes, size, &adpcm->list, detail, sizeof(detail)))
	{
		free(bytes);
		reason_set(reason, reason_size, "sounds_adpcm.lst.bin: %s", detail);
		mcc_sound_close();
		return FALSE;
	}
	free(bytes);
	adpcm->list_loaded = TRUE;
	for (bank = MCC_SOUND_BANK_ADPCM + 1; bank < NUMBER_OF_MCC_SOUND_BANKS; bank++)
	{
		bytes = file_load(bank_lists[bank], &size);
		if (bytes)
		{
			mcc_sound_globals.banks[bank].list_loaded =
				mcc_sound_list_parse(bytes, size, &mcc_sound_globals.banks[bank].list, detail, sizeof(detail)) != 0;
			free(bytes);
		}
	}
	{
		char const *trace = getenv("HALO_MCC_SOUND_TRACE");

		mcc_sound_globals.trace = trace && trace[0] && trace[0] != '0';
	}
	mcc_sound_globals.opened_at = started;
	mcc_sound_globals.opened = TRUE;
	source->find = source_find;
	source->stream = source_stream;
	source->trace = mcc_sound_globals.trace ? source_trace : NULL;
	source->combined_offset = combined_offset;
	source->bank_bytes = bank_bytes;
	return TRUE;
}

void mcc_sound_log(struct custom_edition_conversion_report const *report)
{
	long total = report->mcc_sounds_mapped + report->mcc_sounds_mismatched + report->mcc_sounds_in_celt_banks + report->mcc_sounds_unlisted;
	unsigned long memory_bytes = 0;
	char banks[160];
	int length = 0;
	int bank;

	banks[0] = '\0';
	for (bank = 0; bank < NUMBER_OF_MCC_SOUND_BANKS; bank++)
	{
		struct mcc_sound_bank const *entry = &mcc_sound_globals.banks[bank];

		memory_bytes += entry->fsb.stream_count * (unsigned long)sizeof(struct fsb5_stream) + entry->fsb.names_bytes +
			entry->list.count * 16ul + entry->list.path_bytes;
		if (bank != MCC_SOUND_BANK_ADPCM && entry->opened && length < (int)sizeof(banks))
			length += snprintf(banks + length, sizeof(banks) - (size_t)length, "%s%s", length ? ", " : "", bank_files[bank]);
	}
	error(_error_silent,
		"mcc sound: %ld of the map's %ld sounds play: %ld from sounds_adpcm.fsb (%ld permutations, %lu KB of Xbox ADPCM read from the bank as it lies), %ld from the CELT banks (%ld permutations, %lu KB of Xbox ADPCM made as they load; %s); still silent: %ld in CELT banks that are not there, %ld that no list names, %ld that do not match their streams; the banks' and lists' tables took %lu KB for %lu ms (freed now)",
		(long)report->mcc_sounds_mapped, total, (long)(report->mcc_sounds_mapped - report->mcc_sounds_celt_mapped),
		(long)(report->mcc_sound_permutations_mapped - report->mcc_sound_celt_permutations_mapped),
		(unsigned long)(report->mcc_sound_adpcm_bytes / 1024), (long)report->mcc_sounds_celt_mapped,
		(long)report->mcc_sound_celt_permutations_mapped, (unsigned long)(report->mcc_sound_celt_adpcm_bytes / 1024),
		banks[0] ? banks : "none opened", (long)report->mcc_sounds_in_celt_banks,
		(long)report->mcc_sounds_unlisted, (long)report->mcc_sounds_mismatched,
		(memory_bytes + 1023) / 1024,
		(unsigned long)((vita_host_time_us() - mcc_sound_globals.opened_at) / 1000));
}

int mcc_sound_celt_read(int bank, uint32_t offset, uint32_t size, void *buffer)
{
	HANDLE file;

	if (bank <= MCC_SOUND_BANK_ADPCM || bank >= NUMBER_OF_MCC_SOUND_BANKS)
		return 0;
	pthread_mutex_lock(&mcc_sound_readers.lock);
	if (!mcc_sound_readers.tried[bank])
	{
		char path[256];

		mcc_sound_readers.tried[bank] = TRUE;
		if (bank_path(bank, path, sizeof(path)))
		{
			file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
			mcc_sound_readers.files[bank] = file == INVALID_HANDLE_VALUE ? NULL : file;
		}
	}
	file = mcc_sound_readers.files[bank];
	pthread_mutex_unlock(&mcc_sound_readers.lock);
	/* (positioned reads: no lock while reading) */
	return file && file_read(file, offset, size, buffer);
}
