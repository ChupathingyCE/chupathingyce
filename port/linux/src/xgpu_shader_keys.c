/*
XGPU_SHADER_KEYS.C

Shader key lists (xgpu_shader_keys.h): read, and a map's own written as it
is played. The format, a line a record (port/assets/shaders/README.md):

	chupathingy shader keys 1
	p <number> <slot>:<value> ...
	map <name>
	k <vertex code> <flags> <packed mask> <pixel key number>

"p" gives a pixel shader key a number for the "k" lines after it in that
file: its slots that are not 0 (slot_get), in decimal, each value in
hexadecimal. A slot is a field of nv2a_pixel_shader_key by a number that
never changes, so a list stays readable as the key grows: a slot this
build does not know is left out, and a field the list does not give is 0.
"k" is a key (xgpu_program_key): the vertex program's code hash, its flags
and packed mask (hexadecimal), and its pixel key. "map" starts the keys of
one map, in a list of several (the stock maps'); a map's own list has none.
"#" starts a comment. Each value is a number written out, so a list is the
same on every machine, whatever its byte order or word size.
*/

#include "xgpu_shader_keys.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef HALO_SHADER_PREP

#include <stddef.h>

#define KEYS_HEADER "chupathingy shader keys 1"
/* the most pixel key numbers a list can use (its numbers' table, about
5 MB at the most, is as long as the largest number), and the largest list
read (a map's own is a few hundred KB) */
#define PIXEL_NUMBER_LIMIT (1UL << 14)
#define KEYS_FILE_LIMIT (8L << 20)
#define WRITER_BUCKETS 4096

/* ---------- a pixel shader key's slots */

/* slots 0 to D3DRS_PS_MAX - 1 are combiner_state's; the rest have numbers
of their own from 100, never reused (a new field takes the next) */
enum
{
	_slot_texture_modes = 100,
	_slot_sampler_type = 101, /* to 104 */
	_slot_alpha_kill = 105, /* to 108 */
	_slot_color_sign = 109, /* to 112 */
	_slot_alpha_test_function = 113,
	_slot_fog_enable,
	_slot_fog_table_mode,
	_slot_count_samples,
	_slot_coverage_alpha,
	_slot_per_pixel_lighting,
	_slot_alpha_test_samples,
	_slot_point_threshold,
	_slot_plasma_model,
	_slot_last = _slot_plasma_model
};

/* (a field added to nv2a_pixel_shader_key needs a slot here: the key's last
field is plasma_model, with no more than padding after it) */
typedef char pixel_key_slots_assert[sizeof(struct nv2a_pixel_shader_key) -
	offsetof(struct nv2a_pixel_shader_key, plasma_model) <= sizeof(unsigned long) ? 1 : -1];

static unsigned char *slot_byte(struct nv2a_pixel_shader_key *key, unsigned long slot)
{
	if (slot >= _slot_sampler_type && slot < _slot_sampler_type + 4)
		return &key->sampler_type[slot - _slot_sampler_type];
	if (slot >= _slot_alpha_kill && slot < _slot_alpha_kill + 4)
		return &key->alpha_kill[slot - _slot_alpha_kill];
	if (slot >= _slot_color_sign && slot < _slot_color_sign + 4)
		return &key->color_sign[slot - _slot_color_sign];
	switch (slot)
	{
	case _slot_fog_enable: return &key->fog_enable;
	case _slot_fog_table_mode: return &key->fog_table_mode;
	case _slot_count_samples: return &key->count_samples;
	case _slot_coverage_alpha: return &key->coverage_alpha;
	case _slot_per_pixel_lighting: return &key->per_pixel_lighting;
	case _slot_alpha_test_samples: return &key->alpha_test_samples;
	case _slot_point_threshold: return &key->point_threshold;
	case _slot_plasma_model: return &key->plasma_model;
	}
	return NULL;
}

static unsigned long slot_get(const struct nv2a_pixel_shader_key *key, unsigned long slot)
{
	const unsigned char *byte;

	if (slot < D3DRS_PS_MAX)
		return (unsigned long)key->combiner_state[slot];
	if (slot == _slot_texture_modes)
		return (unsigned long)key->texture_modes;
	if (slot == _slot_alpha_test_function)
		return key->alpha_test_function;
	byte = slot_byte((struct nv2a_pixel_shader_key *)key, slot);
	return byte ? *byte : 0;
}

static void slot_set(struct nv2a_pixel_shader_key *key, unsigned long slot, unsigned long value)
{
	unsigned char *byte;

	if (slot < D3DRS_PS_MAX)
		key->combiner_state[slot] = (DWORD)value;
	else if (slot == _slot_texture_modes)
		key->texture_modes = (DWORD)value;
	else if (slot == _slot_alpha_test_function)
		key->alpha_test_function = value;
	else if ((byte = slot_byte(key, slot)) != NULL)
		*byte = (unsigned char)value;
}

/* ---------- hashes */

unsigned long long xgpu_vertex_code_hash(const DWORD *words, unsigned long count)
{
	unsigned long long hash = 14695981039346656037ULL;
	unsigned long index;
	int shift;

	for (index = 0; index < count; index++)
	{
		unsigned long word = (unsigned long)words[index] & 0xffffffffUL;

		for (shift = 0; shift < 32; shift += 8)
			hash = (hash ^ ((word >> shift) & 0xff)) * 1099511628211ULL;
	}
	return hash;
}

/* (of a key in this machine's layout: the writer's, never written) */
static unsigned long long bytes_hash(const void *data, unsigned long size)
{
	const unsigned char *bytes = data;
	unsigned long long hash = 14695981039346656037ULL;
	unsigned long index;

	for (index = 0; index < size; index++)
		hash = (hash ^ bytes[index]) * 1099511628211ULL;
	return hash;
}

/* ---------- reading */

struct pixel_numbers
{
	struct nv2a_pixel_shader_key *keys;
	BOOL *given;
	unsigned long count;
};

static BOOL numbers_grow(struct pixel_numbers *numbers, unsigned long number)
{
	unsigned long count = numbers->count ? numbers->count : 256;
	struct nv2a_pixel_shader_key *keys;
	BOOL *given;

	if (number >= PIXEL_NUMBER_LIMIT)
		return FALSE;
	if (number < numbers->count)
		return TRUE;
	while (count <= number)
		count *= 2;
	keys = realloc(numbers->keys, count * sizeof(*keys));
	if (!keys)
		return FALSE;
	numbers->keys = keys;
	given = realloc(numbers->given, count * sizeof(*given));
	if (!given)
		return FALSE;
	numbers->given = given;
	memset(numbers->given + numbers->count, 0, (count - numbers->count) * sizeof(*given));
	numbers->count = count;
	return TRUE;
}

static BOOL list_add(struct xgpu_shader_key_list *list, const struct xgpu_program_key *key)
{
	if (list->count == list->capacity)
	{
		unsigned long capacity = list->capacity ? list->capacity * 2 : 256;
		struct xgpu_program_key *keys = realloc(list->keys, capacity * sizeof(*keys));

		if (!keys)
			return FALSE;
		list->keys = keys;
		list->capacity = capacity;
	}
	list->keys[list->count++] = *key;
	return TRUE;
}

static const char *skip_spaces(const char *at, const char *end)
{
	while (at < end && (*at == ' ' || *at == '\t'))
		at++;
	return at;
}

/* a number of the line (base 10 or 16), the end of it in *at; one too
large (decimal past 32 bits, hexadecimal past 16 digits) is all ones,
whatever its digits, so that no value wraps around into a small one: the
callers' limits refuse it */
static BOOL read_number(const char **at, const char *end, int base, unsigned long long *value)
{
	const char *cursor = skip_spaces(*at, end);
	unsigned long long result = 0;
	BOOL any = FALSE, too_large = FALSE;

	while (cursor < end)
	{
		int digit;

		if (*cursor >= '0' && *cursor <= '9')
			digit = *cursor - '0';
		else if (base == 16 && *cursor >= 'a' && *cursor <= 'f')
			digit = *cursor - 'a' + 10;
		else if (base == 16 && *cursor >= 'A' && *cursor <= 'F')
			digit = *cursor - 'A' + 10;
		else
			break;
		if (!too_large)
		{
			result = result * (unsigned long long)base + (unsigned long long)digit;
			/* (16 hex digits fit, a code hash's; the callers check the
			others' 32 bits) */
			too_large = base == 16 ? cursor - skip_spaces(*at, end) >= 16 : result > 0xffffffffULL;
		}
		any = TRUE;
		cursor++;
	}
	*at = cursor;
	*value = too_large ? ~0ULL : result;
	return any;
}

static BOOL name_matches(const char *at, const char *end, const char *map)
{
	size_t length = strlen(map);

	while (end > at && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
		end--;
	if ((size_t)(end - at) != length)
		return FALSE;
	while (at < end)
	{
		if (tolower((unsigned char)*at++) != tolower((unsigned char)*map++))
			return FALSE;
	}
	return TRUE;
}

BOOL xgpu_shader_keys_parse(const char *text, unsigned long size, const char *map, struct xgpu_shader_key_list *list)
{
	const char *at = text, *end = text + size;
	struct pixel_numbers numbers = { NULL, NULL, 0 };
	BOOL first = TRUE, ours = TRUE;

	while (at < end)
	{
		const char *line_end = memchr(at, '\n', (size_t)(end - at));
		const char *cursor;

		if (!line_end)
			line_end = end;
		cursor = skip_spaces(at, line_end);
		if (first)
		{
			first = FALSE;
			if ((size_t)(line_end - cursor) < strlen(KEYS_HEADER) || memcmp(cursor, KEYS_HEADER, strlen(KEYS_HEADER)))
			{
				free(numbers.keys);
				free(numbers.given);
				return FALSE;
			}
		}
		else if (line_end - cursor > 4 && !memcmp(cursor, "map ", 4))
		{
			ours = map && name_matches(skip_spaces(cursor + 4, line_end), line_end, map);
		}
		else if (line_end - cursor > 2 && cursor[0] == 'p' && cursor[1] == ' ')
		{
			unsigned long long number, slot, value;

			cursor++;
			/* (checked before any narrowing: unsigned long is 32 bits on 64-bit Windows) */
			if (read_number(&cursor, line_end, 10, &number) && number < PIXEL_NUMBER_LIMIT &&
				numbers_grow(&numbers, (unsigned long)number))
			{
				struct nv2a_pixel_shader_key *key = &numbers.keys[number];

				memset(key, 0, sizeof(*key));
				numbers.given[number] = TRUE;
				while (read_number(&cursor, line_end, 10, &slot) && cursor < line_end && *cursor == ':')
				{
					cursor++;
					if (!read_number(&cursor, line_end, 16, &value))
						break;
					if (value <= 0xffffffffULL &&
						(slot < D3DRS_PS_MAX || (slot >= _slot_texture_modes && slot <= _slot_last)))
						slot_set(key, (unsigned long)slot, (unsigned long)value);
				}
			}
		}
		else if (ours && line_end - cursor > 2 && cursor[0] == 'k' && cursor[1] == ' ')
		{
			unsigned long long code, flags, packed, number;
			struct xgpu_program_key key;

			cursor++;
			if (read_number(&cursor, line_end, 16, &code) && read_number(&cursor, line_end, 16, &flags) &&
				read_number(&cursor, line_end, 16, &packed) && read_number(&cursor, line_end, 10, &number) &&
				flags <= 0xffffffffULL && packed <= 0xffffffffULL && code != ~0ULL &&
				number < PIXEL_NUMBER_LIMIT && number < numbers.count && numbers.given[number])
			{
				memset(&key, 0, sizeof(key));
				key.vertex_code = code;
				key.flags = (unsigned long)flags;
				key.packed_mask = (unsigned long)packed;
				key.pixel = numbers.keys[number];
				if (!list_add(list, &key))
					break;
			}
		}
		at = line_end < end ? line_end + 1 : end;
	}
	free(numbers.keys);
	free(numbers.given);
	return TRUE;
}

void xgpu_shader_key_list_free(struct xgpu_shader_key_list *list)
{
	free(list->keys);
	memset(list, 0, sizeof(*list));
}

/* ---------- writing */

struct writer_entry
{
	struct writer_entry *next;
	unsigned long long hash;
	/* a pixel key's number, or for a key -1 */
	long number;
	struct xgpu_program_key key;
};

struct xgpu_shader_key_writer
{
	FILE *file;
	struct writer_entry *pixels[WRITER_BUCKETS], *keys[WRITER_BUCKETS];
	unsigned long next_number;
	BOOL dirty;
};

static struct writer_entry *pixel_find(struct xgpu_shader_key_writer *writer, const struct nv2a_pixel_shader_key *key,
	unsigned long long hash)
{
	struct writer_entry *entry;

	for (entry = writer->pixels[hash % WRITER_BUCKETS]; entry; entry = entry->next)
	{
		if (entry->hash == hash && !memcmp(&entry->key.pixel, key, sizeof(*key)))
			return entry;
	}
	return NULL;
}

static struct writer_entry *key_find(struct xgpu_shader_key_writer *writer, const struct xgpu_program_key *key,
	unsigned long long hash)
{
	struct writer_entry *entry;

	for (entry = writer->keys[hash % WRITER_BUCKETS]; entry; entry = entry->next)
	{
		if (entry->hash == hash && !memcmp(&entry->key, key, sizeof(*key)))
			return entry;
	}
	return NULL;
}

static struct writer_entry *entry_add(struct writer_entry **buckets, const struct xgpu_program_key *key,
	unsigned long long hash, long number)
{
	struct writer_entry *entry = calloc(1, sizeof(*entry));

	if (!entry)
		return NULL;
	entry->hash = hash;
	entry->number = number;
	entry->key = *key;
	entry->next = buckets[hash % WRITER_BUCKETS];
	buckets[hash % WRITER_BUCKETS] = entry;
	return entry;
}

/* (a copy of key with its padding 0, so that equal keys hash alike) */
static void key_copy(struct xgpu_program_key *to, const struct xgpu_program_key *from)
{
	unsigned long slot;

	memset(to, 0, sizeof(*to));
	to->vertex_code = from->vertex_code;
	to->packed_mask = from->packed_mask;
	to->flags = from->flags;
	for (slot = 0; slot <= _slot_last; slot++)
	{
		if (slot < D3DRS_PS_MAX || slot >= _slot_texture_modes)
			slot_set(&to->pixel, slot, slot_get(&from->pixel, slot));
	}
}

/* the key's line, after its pixel key's if that is new to the file */
static void write_key(struct xgpu_shader_key_writer *writer, const struct xgpu_program_key *key, BOOL to_file)
{
	unsigned long long pixel_hash = bytes_hash(&key->pixel, sizeof(key->pixel));
	struct writer_entry *pixel = pixel_find(writer, &key->pixel, pixel_hash);
	unsigned long slot;

	if (!pixel)
	{
		pixel = entry_add(writer->pixels, key, pixel_hash, (long)writer->next_number++);
		if (!pixel)
			return;
		if (to_file)
		{
			fprintf(writer->file, "p %ld", pixel->number);
			for (slot = 0; slot <= _slot_last; slot++)
			{
				unsigned long value;

				if (slot >= D3DRS_PS_MAX && slot < _slot_texture_modes)
					continue;
				if ((value = slot_get(&key->pixel, slot)) != 0)
					fprintf(writer->file, " %lu:%lx", slot, value);
			}
			fputc('\n', writer->file);
		}
	}
	if (to_file)
	{
		fprintf(writer->file, "k %016llx %lx %lx %ld\n", key->vertex_code, key->flags, key->packed_mask,
			pixel->number);
		writer->dirty = TRUE;
	}
}

struct xgpu_shader_key_writer *xgpu_shader_key_writer_open(const char *path)
{
	struct xgpu_shader_key_writer *writer = calloc(1, sizeof(*writer));
	struct xgpu_shader_key_list list = { NULL, 0, 0 };
	BOOL fresh = TRUE, unfinished = FALSE;
	FILE *file;
	unsigned long index;

	if (!writer)
		return NULL;
	/* the keys the file has already (a file not of this format is
	started again) */
	if ((file = fopen(path, "rb")) != NULL)
	{
		long size;
		char *text = NULL;

		if (fseek(file, 0, SEEK_END) == 0 && (size = ftell(file)) > 0 && size < KEYS_FILE_LIMIT &&
			fseek(file, 0, SEEK_SET) == 0 && (text = malloc((size_t)size)) != NULL &&
			fread(text, 1, (size_t)size, file) == (size_t)size)
		{
			unsigned long whole = (unsigned long)size;

			/* (a line cut short, by a crash as it was written, is not read,
			and the next starts on a line of its own) */
			while (whole && text[whole - 1] != '\n')
				whole--;
			unfinished = whole != (unsigned long)size;
			fresh = !whole || !xgpu_shader_keys_parse(text, whole, NULL, &list);
		}
		free(text);
		fclose(file);
	}
	writer->file = fopen(path, fresh ? "wb" : "ab");
	if (!writer->file)
	{
		xgpu_shader_key_list_free(&list);
		free(writer);
		return NULL;
	}
	if (!fresh && unfinished)
		fputc('\n', writer->file);
	if (fresh)
	{
		fprintf(writer->file, KEYS_HEADER "\n# this map's shaders, made at its loading screen (xgpu_shader_keys.c)\n");
		writer->dirty = TRUE;
	}
	/* (a pixel key's number holds from its line to the next line giving that
	number: each run's lines give their own from 0 again) */
	writer->next_number = 0;
	for (index = 0; index < list.count; index++)
	{
		struct xgpu_program_key key;
		unsigned long long hash;

		key_copy(&key, &list.keys[index]);
		hash = bytes_hash(&key, sizeof(key));
		if (!key_find(writer, &key, hash))
			entry_add(writer->keys, &key, hash, -1);
	}
	xgpu_shader_key_list_free(&list);
	return writer;
}

void xgpu_shader_key_writer_add(struct xgpu_shader_key_writer *writer, const struct xgpu_program_key *key)
{
	struct xgpu_program_key copy;
	unsigned long long hash;

	if (!writer)
		return;
	key_copy(&copy, key);
	hash = bytes_hash(&copy, sizeof(copy));
	if (key_find(writer, &copy, hash))
		return;
	if (!entry_add(writer->keys, &copy, hash, -1))
		return;
	write_key(writer, &copy, TRUE);
}

void xgpu_shader_key_writer_flush(struct xgpu_shader_key_writer *writer)
{
	if (writer && writer->dirty)
	{
		fflush(writer->file);
		writer->dirty = FALSE;
	}
}

void xgpu_shader_key_writer_close(struct xgpu_shader_key_writer *writer)
{
	unsigned long bucket;

	if (!writer)
		return;
	fclose(writer->file);
	for (bucket = 0; bucket < WRITER_BUCKETS; bucket++)
	{
		struct writer_entry **lists[2] = { &writer->pixels[bucket], &writer->keys[bucket] };
		int which;

		for (which = 0; which < 2; which++)
		{
			while (*lists[which])
			{
				struct writer_entry *next = (*lists[which])->next;

				free(*lists[which]);
				*lists[which] = next;
			}
		}
	}
	free(writer);
}

/* ---------- the stock maps' list */

#ifndef XGPU_SHADER_KEYS_TEST
#include "zlib_prefixed.h"

/* (tools/embed_assets.py's: the list deflated, and its size inflated) */
extern const unsigned int xgpu_shader_keys_stock_data[];
extern const unsigned long xgpu_shader_keys_stock_data_size;
extern const unsigned long xgpu_shader_keys_stock_text_size;

const char *xgpu_shader_keys_stock(unsigned long *size)
{
	static char *text;
	static unsigned long text_size;
	static BOOL tried;

	if (!tried)
	{
		uLongf inflated = (uLongf)xgpu_shader_keys_stock_text_size;

		tried = TRUE;
		if (xgpu_shader_keys_stock_data_size && xgpu_shader_keys_stock_text_size &&
			(text = malloc(xgpu_shader_keys_stock_text_size)) != NULL)
		{
			if (uncompress((Bytef *)text, &inflated, (const Bytef *)xgpu_shader_keys_stock_data,
				(uLong)xgpu_shader_keys_stock_data_size) == Z_OK && inflated == xgpu_shader_keys_stock_text_size)
			{
				text_size = (unsigned long)inflated;
			}
			else
			{
				free(text);
				text = NULL;
			}
		}
	}
	*size = text_size;
	return text;
}

#endif

#endif
