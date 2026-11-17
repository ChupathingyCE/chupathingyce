/*
SHADER_KEYS_TEST.C

The shader key lists' reader and writer (port/linux/src/xgpu_shader_keys.c),
on this machine's byte order and word size: built and run by
tools/test_shader_keys.py, natively and, where it can, for a 32-bit and a
big-endian machine, which must read a list as a little-endian 64-bit one
does.

	shader_keys_test LIST MAP OUT

reads LIST's keys for MAP (a map's own list: "-"), writes each key a line
to stdout (the vertex code, flags and packed mask in hexadecimal, then the
pixel key's slots that are not 0, each as slot:value), then writes them
through the writer to OUT, reads OUT back and exits nonzero if it does not
give the same keys in the same order.

	shader_keys_test --append FILE LIST MAP

adds LIST's keys for MAP to FILE through the writer, as a later run adds
to a map's own list (whose last line a crash may have cut short).
*/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t DWORD;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define D3DRS_PS_MAX 57
#define HALO_SHADER_PREP 1
#define XGPU_SHADER_KEYS_TEST 1

#include "../src/xgpu_shader_keys.c"

static char *read_file(const char *path, unsigned long *size)
{
	FILE *file = fopen(path, "rb");
	char *text = NULL;
	long length;

	if (!file)
		return NULL;
	if (fseek(file, 0, SEEK_END) == 0 && (length = ftell(file)) >= 0 && fseek(file, 0, SEEK_SET) == 0 &&
		(text = malloc((size_t)length + 1)) != NULL && fread(text, 1, (size_t)length, file) == (size_t)length)
	{
		*size = (unsigned long)length;
	}
	else
	{
		free(text);
		text = NULL;
	}
	fclose(file);
	return text;
}

static void print_key(const struct xgpu_program_key *key)
{
	unsigned long slot;

	printf("%016llx %lx %lx", key->vertex_code, key->flags, key->packed_mask);
	for (slot = 0; slot <= _slot_last; slot++)
	{
		unsigned long value;

		if (slot >= D3DRS_PS_MAX && slot < _slot_texture_modes)
			continue;
		if ((value = slot_get(&key->pixel, slot)) != 0)
			printf(" %lu:%lx", slot, value);
	}
	printf("\n");
}

static BOOL keys_equal(const struct xgpu_program_key *a, const struct xgpu_program_key *b)
{
	unsigned long slot;

	if (a->vertex_code != b->vertex_code || a->flags != b->flags || a->packed_mask != b->packed_mask)
		return FALSE;
	for (slot = 0; slot <= _slot_last; slot++)
	{
		if ((slot < D3DRS_PS_MAX || slot >= _slot_texture_modes) && slot_get(&a->pixel, slot) != slot_get(&b->pixel, slot))
			return FALSE;
	}
	return TRUE;
}

int main(int argc, char **argv)
{
	struct xgpu_shader_key_list list = { NULL, 0, 0 }, again = { NULL, 0, 0 };
	struct xgpu_shader_key_writer *writer;
	const char *map;
	unsigned long size = 0, index;
	char *text;
	/* (the hash of a vertex program's words: the same on every machine) */
	static const DWORD words[] = { 0x00000000, 0x0020001b, 0x0836106c, 0x2f100ff8 };

	if (argc == 5 && !strcmp(argv[1], "--append"))
	{
		/* (shader_keys_test --append FILE LIST MAP: LIST's keys for MAP added
		to FILE, a map's own list, whatever its last line) */
		if (!(text = read_file(argv[3], &size)) || !xgpu_shader_keys_parse(text, size, argv[4], &list))
			return 1;
		free(text);
		if (!(writer = xgpu_shader_key_writer_open(argv[2])))
			return 1;
		for (index = 0; index < list.count; index++)
			xgpu_shader_key_writer_add(writer, &list.keys[index]);
		xgpu_shader_key_writer_close(writer);
		xgpu_shader_key_list_free(&list);
		return 0;
	}
	if (argc != 4)
	{
		fprintf(stderr, "usage: shader_keys_test LIST MAP OUT | --append FILE LIST MAP\n");
		return 2;
	}
	printf("code hash %016llx\n", xgpu_vertex_code_hash(words, 4));
	map = strcmp(argv[2], "-") ? argv[2] : NULL;
	if (!(text = read_file(argv[1], &size)) || !xgpu_shader_keys_parse(text, size, map, &list))
	{
		fprintf(stderr, "%s: not a shader key list\n", argv[1]);
		return 1;
	}
	free(text);
	for (index = 0; index < list.count; index++)
		print_key(&list.keys[index]);
	remove(argv[3]);
	if (!(writer = xgpu_shader_key_writer_open(argv[3])))
	{
		fprintf(stderr, "%s: cannot be written\n", argv[3]);
		return 1;
	}
	for (index = 0; index < list.count; index++)
		xgpu_shader_key_writer_add(writer, &list.keys[index]);
	/* (a key already written is not written again) */
	if (list.count)
		xgpu_shader_key_writer_add(writer, &list.keys[0]);
	xgpu_shader_key_writer_close(writer);
	/* (reopened, it keeps what it has, and adds nothing it had) */
	if (!(writer = xgpu_shader_key_writer_open(argv[3])))
		return 1;
	for (index = 0; index < list.count; index++)
		xgpu_shader_key_writer_add(writer, &list.keys[index]);
	xgpu_shader_key_writer_close(writer);
	if (!(text = read_file(argv[3], &size)) || !xgpu_shader_keys_parse(text, size, NULL, &again))
	{
		fprintf(stderr, "%s: not read back\n", argv[3]);
		return 1;
	}
	free(text);
	if (again.count != list.count)
	{
		fprintf(stderr, "%lu keys written, %lu read back\n", list.count, again.count);
		return 1;
	}
	for (index = 0; index < list.count; index++)
	{
		if (!keys_equal(&list.keys[index], &again.keys[index]))
		{
			fprintf(stderr, "key %lu read back differently\n", index);
			return 1;
		}
	}
	xgpu_shader_key_list_free(&list);
	xgpu_shader_key_list_free(&again);
	return 0;
}
