/*
CABINET.C

Writes Halo PC's maps out of Microsoft cabinets (.cab files), for the
desktop ports' optional imports of a Halo PC disc image (pc_disc.c) and of
Custom Edition's installer (ce_installer.c). Both keep their maps in
cabinets: Halo PC's disc in FilesCab\Cab1.CAB to Cab6.CAB, Custom Edition's
installer in one cabinet inside the executable. This is the reader both use;
it is this port's own, written from Microsoft's published description of the
format ([MS-CAB], "Cabinet File Format") and of its LZX compression (the
Cabinet SDK's LZX description, and [MS-PATCH], "LZX DELTA Compression and
Decompression", whose LZX it extends).

A cabinet starts with a header:

	0	"MSCF", then 4 reserved bytes
	8	the cabinet's size in bytes (32 bits)
	16	where its file table starts (32 bits)
	24	the format's version: minor, major (1.3)
	26	the number of folders, then of files (16 bits each)
	30	flags (1: continued from a previous cabinet, 2: continued in a
		next one, 4: reserved areas follow)
	36	(with flag 4) the header's reserved area's size (16 bits), then
		each folder's and each data block's (8 bits each), then the
		header's reserved area

then (past any names of the previous and next cabinets, with flags 1 and 2)
a table of folders, 8 bytes each and their reserved area: where the
folder's first data block is (32 bits), how many blocks it has (16 bits),
and its compression (16 bits: 0 none, 1 MSZIP, 3 LZX with the window size's
power of two in bits 8-12). A "folder" is a compressed stream; the files are
runs of it. Each file's entry in the file table is its size (32 bits),
where it starts in its folder's stream (32 bits), its folder (16 bits), a
date, time and attributes (16 bits each), then its name (a path with
backslashes, NUL-terminated). A data block is a checksum (32 bits), its
compressed and uncompressed sizes (16 bits each), its reserved area, then
its data; each block uncompresses to 32768 bytes, but a folder's last.

Both of Halo PC's sets use LZX with a 2 MB window. LZX is LZ77 with Huffman
codes: the stream is read as 16-bit little-endian words, each from its
highest bit down. It opens with a bit for x86 call translation (below),
with its 32-bit file size after it when the bit is set. Then come blocks: 3
bits of type (1 verbatim, 2 aligned offset, 3 uncompressed) and 24 bits of
how many bytes it makes.

A verbatim block has two code trees: the main tree's 256 literals and 8
length headers per position slot (a slot being a range of match offsets,
50 of them for a 2 MB window), and the length tree's 249 longer lengths.
Each tree is sent as its symbols' code lengths, those coded by a pretree
of 20 symbols (4 bits each) sent before them: 0-16 is the change from the
length the symbol had in the previous block (as (old - n) mod 17), 17 and
18 are runs of zeros (4 + 4 bits' and 20 + 5 bits' worth), 19 a run of 4 + 1
bit's worth of the next symbol's change. The main tree comes in two halves
(the literals, then the rest), each with its own pretree, then the length
tree. Codes are canonical: shorter codes first, by symbol among the same
length, 16 bits at most.

A main symbol under 256 is a literal. Past it, its low 3 bits are the
match's length less 2 (7: add the next length symbol) and the rest its
position slot: slots 0-2 repeat the three most recent offsets (swapping
the one used to the front), and others are a base plus extra bits read
after it (none for slots 0-3, then 1, 1, 2, 2, ... up to 17), less 2. An
aligned offset block first sends an 8-symbol tree of 3-bit lengths; a slot
with 3 or more extra bits then has its lowest 3 read as an aligned symbol,
after the rest. An uncompressed block skips to the next 16-bit boundary
(a whole word when it is already on one), then has the three recent
offsets (32 bits each) and its bytes, and a padding byte when the count is
odd.

Every 32768 bytes made (a "frame", a cabinet's data block) the stream goes
to its next 16-bit boundary. When the translation bit was set, the frames
of the first 1 GB of the stream are then undone of the x86 translation:
every 0xE8 byte at least 10 bytes before a frame's end is followed by a
32-bit relative address that the compressor made absolute; one in range
(at least minus its position, under the file size) is made relative again,
and the four bytes are passed over.

A map is written to its file's name plus .partial in the folder, and given
its name once all of it is there, so an interrupted import leaves no map
behind half-written. A map the folder already has is left as it is.
*/

/* (the desktop ports only: the Android app imports the game data itself) */
#ifndef HALO_ANDROID

#include "cabinet.h"
#include "platform.h"
#include "posix.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

enum
{
	HEADER_SIZE = 36,
	FLAG_PREVIOUS = 1,
	FLAG_NEXT = 2,
	FLAG_RESERVE = 4,
	FOLDER_SIZE = 8,
	FILE_ENTRY_SIZE = 16,
	DATA_HEADER_SIZE = 8,
	/* a file's folder index past these: continued from or into another
	cabinet */
	FOLDER_CONTINUED = 0xfffd,
	MAXIMUM_FILES = 4096,
	MAXIMUM_NAME = 256,
	MAXIMUM_PATH = 1280,
	/* the most a data block holds: its uncompressed size, plus what LZX may
	add to it */
	MAXIMUM_BLOCK = 32768 + 6144,

	COMPRESSION_NONE = 0,
	COMPRESSION_LZX = 3,

	FRAME_SIZE = 32768,
	/* x86 translation stops after this many frames (1 GB) */
	TRANSLATION_FRAMES = 32768,
	MINIMUM_WINDOW_BITS = 15,
	MAXIMUM_WINDOW_BITS = 21,
	MAXIMUM_POSITION_SLOTS = 50,
	LITERALS = 256,
	MAIN_SYMBOLS = LITERALS + MAXIMUM_POSITION_SLOTS * 8,
	LENGTH_SYMBOLS = 249,
	ALIGNED_SYMBOLS = 8,
	PRETREE_SYMBOLS = 20,
	MAXIMUM_CODE_LENGTH = 16,
	/* codes this long or shorter are decoded by one lookup */
	FAST_BITS = 11,
	NO_SYMBOL = 0xffff,
	MINIMUM_MATCH = 2,
	/* the bytes the bit reader may read past what it uses */
	LZX_READ_AHEAD = 4,

	BLOCK_VERBATIM = 1,
	BLOCK_ALIGNED = 2,
	BLOCK_UNCOMPRESSED = 3,
};

static const unsigned char cabinet_magic[] = { 'M', 'S', 'C', 'F', 0, 0, 0, 0 };

/* ---------- reading */

static unsigned int read_u16(const unsigned char *bytes)
{
	return (unsigned int)bytes[0] | (unsigned int)bytes[1] << 8;
}

static unsigned int read_u32(const unsigned char *bytes)
{
	return (unsigned int)bytes[0] | (unsigned int)bytes[1] << 8 | (unsigned int)bytes[2] << 16 |
		(unsigned int)bytes[3] << 24;
}

int cabinet_fail(struct cabinet_import *import, const char *format, const char *detail)
{
	snprintf(import->error, (size_t)import->error_size, format, detail ? detail : "");
	return 0;
}

int cabinet_read(struct cabinet_import *import, unsigned long long offset, void *buffer, unsigned int size)
{
	posix_ulong low, high;
	unsigned char *at = buffer;

	if (offset > import->file_size || size > import->file_size - offset)
		return 0;
	if (posix_seek(import->descriptor, (posix_long)(offset & 0xffffffffull), (posix_long)(offset >> 32), SEEK_SET,
		&low, &high) != 0)
	{
		return 0;
	}
	while (size > 0)
	{
		ssize_t count = read(import->descriptor, at, size);

		if (count <= 0)
			return 0;
		at += count;
		size -= (unsigned int)count;
	}
	return 1;
}

int cabinet_import_open(struct cabinet_import *import, const char *path, const char *destination,
	const char *folder, cabinet_progress_proc progress, void *context, char *error, int error_size)
{
	posix_ulong low, high;

	memset(import, 0, sizeof(*import));
	import->error = error;
	import->error_size = error_size;
	import->progress = progress;
	import->context = context;
	snprintf(import->folder, sizeof(import->folder), "%s/%s", destination, folder);
	import->descriptor = open(path, O_RDONLY | O_LARGEFILE | O_CLOEXEC);
	if (import->descriptor < 0)
		return cabinet_fail(import, "Could not open %s.", path);
	if (posix_seek(import->descriptor, 0, 0, SEEK_END, &low, &high) != 0)
	{
		cabinet_import_close(import);
		return cabinet_fail(import, "Could not read %s.", path);
	}
	import->file_size = (unsigned long long)high << 32 | low;
	return 1;
}

void cabinet_import_close(struct cabinet_import *import)
{
	if (import->descriptor >= 0)
		close(import->descriptor);
	import->descriptor = -1;
}

/* makes the import's folder (when it cannot be, writing the first map in
it says so) */
static int folder_make(struct cabinet_import *import)
{
	posix_make_directory(import->folder);
	return 1;
}

/* ---------- the cabinet's tables */

struct cabinet_folder
{
	unsigned int first_block;
	unsigned int blocks;
	unsigned int compression;
	/* where its stream ends: the end of its last file */
	unsigned long long end;
};

struct cabinet_file
{
	char name[MAXIMUM_NAME];
	unsigned int size;
	unsigned int start;
	unsigned int folder;
	/* a map the folder lacks */
	int wanted;
};

struct cabinet
{
	struct cabinet_import *import;
	unsigned long long offset;
	unsigned int size;
	unsigned int block_reserve;
	struct cabinet_folder *folders;
	int folder_count;
	struct cabinet_file *files;
	int file_count;
};

static int same_name(const char *a, const char *b)
{
	for (;; a++, b++)
	{
		int x = *a >= 'A' && *a <= 'Z' ? *a - 'A' + 'a' : *a;
		int y = *b >= 'A' && *b <= 'Z' ? *b - 'A' + 'a' : *b;

		if (x != y)
			return 0;
		if (!x)
			return 1;
	}
}

int cabinet_map_name(const char *path, char *name, int name_size)
{
	const char *last = path, *previous = NULL, *at;
	char folder[8];
	size_t length;
	int index;

	for (at = path; *at; at++)
	{
		if (*at == '\\' || *at == '/')
		{
			previous = last;
			last = at + 1;
		}
	}
	/* (the folder holding it is named maps) */
	if (!previous || last - previous != 5)
		return 0;
	memcpy(folder, previous, 4);
	folder[4] = 0;
	length = strlen(last);
	if (!same_name(folder, "maps") || length < 5 || length >= (size_t)name_size || !same_name(last + length - 4, ".map"))
		return 0;
	for (index = 0; last[index]; index++)
	{
		unsigned char c = (unsigned char)last[index];

		/* only names every desktop file system takes as they are */
		if (c <= 0x20 || c > 0x7e || strchr("/\\:*?\"<>|", c))
			return 0;
		name[index] = (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
	}
	name[index] = 0;
	return 1;
}

int cabinet_is_cabinet(struct cabinet_import *import, unsigned long long offset, unsigned long long size)
{
	unsigned char header[HEADER_SIZE];

	if (size < HEADER_SIZE || !cabinet_read(import, offset, header, sizeof(header)))
		return 0;
	return !memcmp(header, cabinet_magic, sizeof(cabinet_magic)) && read_u32(header + 8) <= size &&
		read_u32(header + 8) >= HEADER_SIZE && read_u32(header + 16) < read_u32(header + 8) && header[25] == 1 &&
		header[24] == 3;
}

static void cabinet_close(struct cabinet *cabinet)
{
	free(cabinet->folders);
	free(cabinet->files);
	cabinet->folders = NULL;
	cabinet->files = NULL;
}

/* whether the import's folder has a file */
static int folder_has(struct cabinet_import *import, const char *name)
{
	char path[MAXIMUM_PATH];
	int descriptor;

	snprintf(path, sizeof(path), "%s/%s", import->folder, name);
	descriptor = open(path, O_RDONLY | O_CLOEXEC);
	if (descriptor < 0)
		return 0;
	close(descriptor);
	return 1;
}

/* reads a cabinet's folders and its maps (its other files left out) */
static int cabinet_open(struct cabinet *cabinet, struct cabinet_import *import, unsigned long long offset,
	unsigned long long size)
{
	unsigned char header[HEADER_SIZE + 4];
	unsigned char *table = NULL;
	unsigned int flags, files_offset, header_reserve = 0, folder_reserve = 0, position, table_size;
	unsigned int folder_count, file_count;
	int index;

	memset(cabinet, 0, sizeof(*cabinet));
	cabinet->import = import;
	cabinet->offset = offset;
	if (!cabinet_is_cabinet(import, offset, size) || !cabinet_read(import, offset, header, sizeof(header)))
		return cabinet_fail(import, "A cabinet in it is damaged.%s", NULL);
	cabinet->size = read_u32(header + 8);
	files_offset = read_u32(header + 16);
	folder_count = read_u16(header + 26);
	file_count = read_u16(header + 28);
	flags = read_u16(header + 30);
	if (flags & (FLAG_PREVIOUS | FLAG_NEXT))
		return cabinet_fail(import, "Its maps are split across cabinets, which this cannot read.%s", NULL);
	position = HEADER_SIZE;
	if (flags & FLAG_RESERVE)
	{
		header_reserve = read_u16(header + 36);
		folder_reserve = header[38];
		cabinet->block_reserve = header[39];
		position += 4 + header_reserve;
	}
	if (file_count > MAXIMUM_FILES || position >= cabinet->size)
		return cabinet_fail(import, "A cabinet in it is damaged.%s", NULL);

	/* everything from the folders to the cabinet's first data, at most */
	table_size = cabinet->size - position;
	if (table_size > (8u << 20))
		table_size = 8u << 20;
	table = malloc(table_size);
	cabinet->folders = calloc(folder_count ? folder_count : 1, sizeof(*cabinet->folders));
	cabinet->files = calloc(file_count ? file_count : 1, sizeof(*cabinet->files));
	if (!table || !cabinet->folders || !cabinet->files)
	{
		free(table);
		cabinet_close(cabinet);
		return cabinet_fail(import, "Out of memory.%s", NULL);
	}
	if (!cabinet_read(import, offset + position, table, table_size))
		goto damaged;

	for (index = 0; index < (int)folder_count; index++)
	{
		unsigned int at = (unsigned int)index * (FOLDER_SIZE + folder_reserve);
		struct cabinet_folder *folder = &cabinet->folders[index];

		if (at + FOLDER_SIZE > table_size)
			goto damaged;
		folder->first_block = read_u32(table + at);
		folder->blocks = read_u16(table + at + 4);
		folder->compression = read_u16(table + at + 6);
	}
	cabinet->folder_count = (int)folder_count;

	/* (the file table, relative to the table read) */
	if (files_offset < position)
		goto damaged;
	files_offset -= position;
	for (index = 0; index < (int)file_count; index++)
	{
		struct cabinet_file *file = &cabinet->files[cabinet->file_count];
		char path[MAXIMUM_NAME];
		size_t length;

		if (files_offset + FILE_ENTRY_SIZE >= table_size)
			goto damaged;
		file->size = read_u32(table + files_offset);
		file->start = read_u32(table + files_offset + 4);
		file->folder = read_u16(table + files_offset + 8);
		files_offset += FILE_ENTRY_SIZE;
		if (file->folder < folder_count &&
			(unsigned long long)file->start + file->size > cabinet->folders[file->folder].end)
		{
			cabinet->folders[file->folder].end = (unsigned long long)file->start + file->size;
		}
		length = strnlen((const char *)table + files_offset, table_size - files_offset);
		if (files_offset + length >= table_size)
			goto damaged;
		if (length < sizeof(path))
		{
			memcpy(path, table + files_offset, length + 1);
			if (file->folder < FOLDER_CONTINUED && cabinet_map_name(path, file->name, sizeof(file->name)))
			{
				if (file->folder >= folder_count)
					goto damaged;
				cabinet->file_count++;
			}
		}
		files_offset += (unsigned int)length + 1;
	}
	free(table);
	return 1;

damaged:
	free(table);
	cabinet_close(cabinet);
	return cabinet_fail(import, "A cabinet in it is damaged.%s", NULL);
}

int cabinet_count_maps(struct cabinet_import *import, unsigned long long offset, unsigned long long size)
{
	struct cabinet cabinet;
	int index;

	if (!cabinet_open(&cabinet, import, offset, size))
		return 0;
	for (index = 0; index < cabinet.file_count; index++)
		cabinet_count_plain_map(import, cabinet.files[index].name, cabinet.files[index].size);
	cabinet_close(&cabinet);
	return 1;
}

int cabinet_count_plain_map(struct cabinet_import *import, const char *name, unsigned long long size)
{
	import->found++;
	if (folder_has(import, name))
	{
		import->kept++;
		return 0;
	}
	import->total += size;
	return 1;
}

/* ---------- a folder's data blocks, as one stream of bytes */

struct block_stream
{
	struct cabinet *cabinet;
	unsigned long long next;
	unsigned int blocks_left;
	unsigned char data[MAXIMUM_BLOCK];
	unsigned int size;
	unsigned int position;
	/* bytes asked for past the folder's last block (or a damaged one) */
	unsigned int overrun;
};

static void block_stream_start(struct block_stream *stream, struct cabinet *cabinet,
	const struct cabinet_folder *folder)
{
	stream->cabinet = cabinet;
	stream->next = cabinet->offset + folder->first_block;
	stream->blocks_left = folder->blocks;
	stream->size = 0;
	stream->position = 0;
	stream->overrun = 0;
}

/* whether the stream ran out: LZX reads up to two words ahead of what it
uses */
static int block_stream_short(const struct block_stream *stream, unsigned int ahead)
{
	return stream->overrun > ahead;
}

/* the next byte of the stream; 0 past its end, counted in overrun */
static unsigned int block_stream_byte(struct block_stream *stream)
{
	while (stream->position == stream->size)
	{
		unsigned char header[DATA_HEADER_SIZE];
		unsigned int size;

		if (stream->overrun || !stream->blocks_left ||
			!cabinet_read(stream->cabinet->import, stream->next, header, sizeof(header)))
		{
			stream->overrun++;
			return 0;
		}
		size = read_u16(header + 4);
		stream->next += DATA_HEADER_SIZE + stream->cabinet->block_reserve;
		if (size > MAXIMUM_BLOCK || !cabinet_read(stream->cabinet->import, stream->next, stream->data, size))
		{
			stream->overrun++;
			return 0;
		}
		stream->next += size;
		stream->blocks_left--;
		stream->size = size;
		stream->position = 0;
	}
	return stream->data[stream->position++];
}

/* ---------- LZX */

struct lzx_tree
{
	int symbols;
	unsigned char lengths[MAIN_SYMBOLS];
	unsigned short fast[1 << FAST_BITS];
	/* canonical codes of each length: the first, how many, and where their
	symbols start in sorted */
	unsigned int first[MAXIMUM_CODE_LENGTH + 1];
	unsigned int count[MAXIMUM_CODE_LENGTH + 1];
	unsigned int start[MAXIMUM_CODE_LENGTH + 1];
	unsigned short sorted[MAIN_SYMBOLS];
	int empty;
};

struct lzx
{
	struct block_stream *input;
	/* the bits read and not yet used, from the top; and bytes of words read
	into bits and handed back for an uncompressed block */
	unsigned int bits;
	int bit_count;
	unsigned char returned[8];
	int returned_count;
	int returned_position;
	int damaged;

	unsigned char *window;
	unsigned int window_size;
	unsigned int position_slots;
	unsigned long long made;
	unsigned int r0, r1, r2;

	int header_read;
	unsigned int translation_size;
	int block_type;
	unsigned int block_remaining;
	unsigned int block_length;

	struct lzx_tree main;
	struct lzx_tree length;
	struct lzx_tree aligned;
	struct lzx_tree pretree;
};

static unsigned char extra_bits[MAXIMUM_POSITION_SLOTS];
static unsigned int position_base[MAXIMUM_POSITION_SLOTS];

static void lzx_tables(void)
{
	int slot;

	for (slot = 0; slot < MAXIMUM_POSITION_SLOTS; slot++)
	{
		int extra = slot < 4 ? 0 : (slot - 2) / 2;

		extra_bits[slot] = (unsigned char)(extra > 17 ? 17 : extra);
		position_base[slot] = slot ? position_base[slot - 1] + (1u << extra_bits[slot - 1]) : 0;
	}
}

static unsigned int lzx_input_byte(struct lzx *lzx)
{
	if (lzx->returned_position < lzx->returned_count)
		return lzx->returned[lzx->returned_position++];
	return block_stream_byte(lzx->input);
}

/* makes sure count bits (17 at most) are read */
static void lzx_need(struct lzx *lzx, int count)
{
	while (lzx->bit_count < count)
	{
		unsigned int low = lzx_input_byte(lzx);
		unsigned int word = low | lzx_input_byte(lzx) << 8;

		lzx->bits |= word << (16 - lzx->bit_count);
		lzx->bit_count += 16;
	}
}

static unsigned int lzx_bits(struct lzx *lzx, int count)
{
	unsigned int value;

	if (!count)
		return 0;
	lzx_need(lzx, count);
	value = lzx->bits >> (32 - count);
	lzx->bits = count < 32 ? lzx->bits << count : 0;
	lzx->bit_count -= count;
	return value;
}

/* to the stream's next 16-bit boundary (with whole, a whole word when it
is on one already) */
static void lzx_align(struct lzx *lzx, int whole)
{
	int partial = lzx->bit_count & 15;

	if (partial)
		lzx_bits(lzx, partial);
	else if (whole)
		lzx_bits(lzx, 16);
}

/* hands the words read ahead back as bytes, for an uncompressed block */
static void lzx_return_words(struct lzx *lzx)
{
	unsigned char bytes[sizeof(lzx->returned)];
	int count = 0;

	while (lzx->bit_count >= 16)
	{
		unsigned int word = lzx_bits(lzx, 16);

		bytes[count++] = (unsigned char)word;
		bytes[count++] = (unsigned char)(word >> 8);
	}
	/* (before any handed back earlier and not given out yet: those words
	were read from them) */
	while (lzx->returned_position < lzx->returned_count && count < (int)sizeof(bytes))
		bytes[count++] = lzx->returned[lzx->returned_position++];
	memcpy(lzx->returned, bytes, (size_t)count);
	lzx->returned_count = count;
	lzx->returned_position = 0;
	lzx->bits = 0;
	lzx->bit_count = 0;
}

/* builds a tree's codes from its lengths; 0 if they are no prefix code (an
empty tree is allowed, and fails when a symbol is read from it) */
static int lzx_build(struct lzx_tree *tree)
{
	unsigned int code = 0, used = 0;
	unsigned int filled[MAXIMUM_CODE_LENGTH + 1];
	int length, symbol;

	memset(tree->count, 0, sizeof(tree->count));
	for (symbol = 0; symbol < tree->symbols; symbol++)
		tree->count[tree->lengths[symbol]]++;
	tree->count[0] = 0;
	for (length = 1; length <= MAXIMUM_CODE_LENGTH; length++)
	{
		code = (code + tree->count[length - 1]) << 1;
		tree->first[length] = code;
		tree->start[length] = used;
		if (code + tree->count[length] > (1u << length))
			return 0;
		filled[length] = 0;
		used += tree->count[length];
	}
	tree->empty = used == 0;
	for (symbol = 0; symbol < tree->symbols; symbol++)
	{
		length = tree->lengths[symbol];
		if (length)
			tree->sorted[tree->start[length] + filled[length]++] = (unsigned short)symbol;
	}
	memset(tree->fast, 0xff, sizeof(tree->fast));
	for (length = 1; length <= FAST_BITS; length++)
	{
		unsigned int index;

		for (index = 0; index < tree->count[length]; index++)
		{
			unsigned int first = (tree->first[length] + index) << (FAST_BITS - length);
			unsigned int last = first + (1u << (FAST_BITS - length));

			for (; first < last; first++)
				tree->fast[first] = tree->sorted[tree->start[length] + index];
		}
	}
	return 1;
}

static unsigned int lzx_symbol(struct lzx *lzx, struct lzx_tree *tree)
{
	unsigned int code, symbol;
	int length;

	lzx_need(lzx, MAXIMUM_CODE_LENGTH);
	code = lzx->bits >> (32 - MAXIMUM_CODE_LENGTH);
	symbol = tree->fast[code >> (MAXIMUM_CODE_LENGTH - FAST_BITS)];
	if (symbol != NO_SYMBOL)
	{
		lzx_bits(lzx, tree->lengths[symbol]);
		return symbol;
	}
	for (length = FAST_BITS + 1; length <= MAXIMUM_CODE_LENGTH; length++)
	{
		unsigned int index = (code >> (MAXIMUM_CODE_LENGTH - length)) - tree->first[length];

		if (index < tree->count[length])
		{
			lzx_bits(lzx, length);
			return tree->sorted[tree->start[length] + index];
		}
	}
	lzx->damaged = 1;
	return 0;
}

/* reads the lengths of a tree's symbols first to last, as changes from the
ones before (see the top) */
static int lzx_read_lengths(struct lzx *lzx, unsigned char *lengths, int first, int last)
{
	int index = first;
	int symbol;

	lzx->pretree.symbols = PRETREE_SYMBOLS;
	for (symbol = 0; symbol < PRETREE_SYMBOLS; symbol++)
		lzx->pretree.lengths[symbol] = (unsigned char)lzx_bits(lzx, 4);
	if (!lzx_build(&lzx->pretree) || lzx->pretree.empty)
		return 0;
	while (index < last && !lzx->damaged)
	{
		unsigned int code = lzx_symbol(lzx, &lzx->pretree);
		int run = 1;
		int value = 0;

		if (code == 17)
			run = 4 + (int)lzx_bits(lzx, 4);
		else if (code == 18)
			run = 20 + (int)lzx_bits(lzx, 5);
		else if (code == 19)
		{
			run = 4 + (int)lzx_bits(lzx, 1);
			code = lzx_symbol(lzx, &lzx->pretree);
			if (code > 16)
				return 0;
			value = (lengths[index] + 17 - (int)code) % 17;
		}
		else
			value = (lengths[index] + 17 - (int)code) % 17;
		if (index + run > last)
			return 0;
		while (run--)
			lengths[index++] = (unsigned char)value;
	}
	return !lzx->damaged;
}

static int lzx_start(struct lzx *lzx, struct block_stream *input, unsigned int window_bits)
{
	static int tables_made;

	if (!tables_made)
	{
		lzx_tables();
		tables_made = 1;
	}
	if (window_bits < MINIMUM_WINDOW_BITS || window_bits > MAXIMUM_WINDOW_BITS)
		return 0;
	memset(lzx, 0, sizeof(*lzx));
	lzx->input = input;
	lzx->window_size = 1u << window_bits;
	lzx->window = malloc(lzx->window_size);
	if (!lzx->window)
		return 0;
	/* (2 slots a power of two up to 2^17, 8 a power past it) */
	lzx->position_slots = window_bits == 21 ? 50 : window_bits == 20 ? 42 : window_bits * 2;
	lzx->r0 = lzx->r1 = lzx->r2 = 1;
	lzx->main.symbols = LITERALS + (int)lzx->position_slots * 8;
	lzx->length.symbols = LENGTH_SYMBOLS;
	lzx->aligned.symbols = ALIGNED_SYMBOLS;
	return 1;
}

static int lzx_block_header(struct lzx *lzx)
{
	int index;

	/* (an odd uncompressed block's padding byte) */
	if (lzx->block_type == BLOCK_UNCOMPRESSED && (lzx->block_length & 1))
		lzx_input_byte(lzx);
	lzx->block_type = (int)lzx_bits(lzx, 3);
	lzx->block_length = lzx_bits(lzx, 16) << 8;
	lzx->block_length |= lzx_bits(lzx, 8);
	lzx->block_remaining = lzx->block_length;
	switch (lzx->block_type)
	{
	case BLOCK_ALIGNED:
		for (index = 0; index < ALIGNED_SYMBOLS; index++)
			lzx->aligned.lengths[index] = (unsigned char)lzx_bits(lzx, 3);
		if (!lzx_build(&lzx->aligned))
			return 0;
		/* (then the trees a verbatim block has) */
	case BLOCK_VERBATIM:
		if (!lzx_read_lengths(lzx, lzx->main.lengths, 0, LITERALS) ||
			!lzx_read_lengths(lzx, lzx->main.lengths, LITERALS, lzx->main.symbols) || !lzx_build(&lzx->main) ||
			lzx->main.empty || !lzx_read_lengths(lzx, lzx->length.lengths, 0, LENGTH_SYMBOLS) ||
			!lzx_build(&lzx->length))
		{
			return 0;
		}
		return 1;
	case BLOCK_UNCOMPRESSED:
	{
		unsigned char recent[12];

		lzx_align(lzx, 1);
		lzx_return_words(lzx);
		for (index = 0; index < 12; index++)
			recent[index] = (unsigned char)lzx_input_byte(lzx);
		lzx->r0 = read_u32(recent);
		lzx->r1 = read_u32(recent + 4);
		lzx->r2 = read_u32(recent + 8);
		return 1;
	}
	default:
		return 0;
	}
}

/* makes the stream's next frame of size bytes (32768 but the last), into
frame; 0 if the stream is damaged */
static int lzx_frame(struct lzx *lzx, unsigned char *frame, unsigned int size, unsigned long long frame_index)
{
	unsigned int mask = lzx->window_size - 1;
	unsigned int start = (unsigned int)(lzx->made & mask);
	unsigned long long end = lzx->made + size;

	if (!lzx->header_read)
	{
		if (lzx_bits(lzx, 1))
		{
			lzx->translation_size = lzx_bits(lzx, 16) << 16;
			lzx->translation_size |= lzx_bits(lzx, 16);
		}
		lzx->header_read = 1;
	}
	while (lzx->made < end)
	{
		unsigned int run;

		if (!lzx->block_remaining && !lzx_block_header(lzx))
			return 0;
		if (lzx->damaged || block_stream_short(lzx->input, LZX_READ_AHEAD))
			return 0;
		run = lzx->block_remaining;
		if (run > end - lzx->made)
			run = (unsigned int)(end - lzx->made);
		lzx->block_remaining -= run;

		if (lzx->block_type == BLOCK_UNCOMPRESSED)
		{
			for (; run; run--)
				lzx->window[lzx->made++ & mask] = (unsigned char)lzx_input_byte(lzx);
			continue;
		}
		while (run)
		{
			unsigned int symbol = lzx_symbol(lzx, &lzx->main);
			unsigned int length, slot, offset, from;

			if (lzx->damaged)
				return 0;
			if (symbol < LITERALS)
			{
				lzx->window[lzx->made++ & mask] = (unsigned char)symbol;
				run--;
				continue;
			}
			symbol -= LITERALS;
			length = symbol & 7;
			if (length == 7)
			{
				if (lzx->length.empty)
					return 0;
				length += lzx_symbol(lzx, &lzx->length);
			}
			length += MINIMUM_MATCH;
			slot = symbol >> 3;
			if (slot == 0)
				offset = lzx->r0;
			else if (slot == 1)
			{
				offset = lzx->r1;
				lzx->r1 = lzx->r0;
				lzx->r0 = offset;
			}
			else if (slot == 2)
			{
				offset = lzx->r2;
				lzx->r2 = lzx->r0;
				lzx->r0 = offset;
			}
			else
			{
				int extra = extra_bits[slot];

				if (lzx->block_type == BLOCK_ALIGNED && extra >= 3)
				{
					offset = lzx_bits(lzx, extra - 3) << 3;
					offset += lzx_symbol(lzx, &lzx->aligned);
				}
				else
					offset = lzx_bits(lzx, extra);
				offset += position_base[slot] - 2;
				lzx->r2 = lzx->r1;
				lzx->r1 = lzx->r0;
				lzx->r0 = offset;
			}
			/* (a match never runs past its block or frame, nor reaches back
			before the stream's start) */
			if (lzx->damaged || length > run || !offset || offset > lzx->made || offset > lzx->window_size)
				return 0;
			from = (unsigned int)((lzx->made - offset) & mask);
			run -= length;
			while (length--)
			{
				lzx->window[lzx->made++ & mask] = lzx->window[from];
				from = (from + 1) & mask;
			}
		}
	}
	if (lzx->damaged || block_stream_short(lzx->input, LZX_READ_AHEAD))
		return 0;
	/* (the next frame starts on a 16-bit boundary) */
	if (lzx->block_type != BLOCK_UNCOMPRESSED)
		lzx_align(lzx, 0);

	memcpy(frame, lzx->window + start, size);
	if (lzx->translation_size && frame_index < TRANSLATION_FRAMES && size > 10)
	{
		unsigned int at = 0;
		int frame_start = (int)(frame_index * FRAME_SIZE);

		while (at < size - 10)
		{
			int absolute, position;

			if (frame[at] != 0xe8)
			{
				at++;
				continue;
			}
			absolute = (int)read_u32(frame + at + 1);
			position = frame_start + (int)at;
			if (absolute >= -position && absolute < (int)lzx->translation_size)
			{
				unsigned int relative = (unsigned int)(absolute >= 0 ? absolute - position :
					absolute + (int)lzx->translation_size);

				frame[at + 1] = (unsigned char)relative;
				frame[at + 2] = (unsigned char)(relative >> 8);
				frame[at + 3] = (unsigned char)(relative >> 16);
				frame[at + 4] = (unsigned char)(relative >> 24);
			}
			at += 5;
		}
	}
	return 1;
}

/* ---------- writing the maps */

struct map_output
{
	struct cabinet_file *file;
	int descriptor;
	unsigned int written;
	char partial[MAXIMUM_PATH];
	char final[MAXIMUM_PATH];
};

static int output_open(struct cabinet_import *import, struct map_output *output, const char *name)
{
	snprintf(output->final, sizeof(output->final), "%s/%s", import->folder, name);
	snprintf(output->partial, sizeof(output->partial), "%s.partial", output->final);
	output->written = 0;
	output->descriptor = open(output->partial, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
	if (output->descriptor < 0)
		return cabinet_fail(import, "Could not write %s.", output->partial);
	platform_log("importing %s", output->final);
	return 1;
}

static int output_write(struct cabinet_import *import, struct map_output *output, const unsigned char *bytes,
	unsigned int count, const char *name)
{
	unsigned int written = 0;

	while (written < count)
	{
		ssize_t result = write(output->descriptor, bytes + written, count - written);

		if (result <= 0)
			return cabinet_fail(import, "Could not write %s (is the disk full?).", output->partial);
		written += (unsigned int)result;
	}
	output->written += count;
	import->done += count;
	if (import->progress)
		import->progress(import->context, name, import->done, import->total);
	return 1;
}

static int output_finish(struct cabinet_import *import, struct map_output *output)
{
	int closed = close(output->descriptor);

	output->descriptor = -1;
	if (closed != 0)
		return cabinet_fail(import, "Could not write %s (is the disk full?).", output->partial);
	if (rename(output->partial, output->final) != 0)
		return cabinet_fail(import, "Could not create %s.", output->final);
	return 1;
}

static void output_abandon(struct map_output *output)
{
	if (output->descriptor >= 0)
	{
		close(output->descriptor);
		output->descriptor = -1;
		remove(output->partial);
	}
}

static int compare_starts(const void *a, const void *b)
{
	const struct cabinet_file *x = *(struct cabinet_file *const *)a, *y = *(struct cabinet_file *const *)b;

	return x->start < y->start ? -1 : x->start > y->start;
}

/* uncompresses one folder up to the end of its last wanted map, writing
each of them as its bytes go by */
static int extract_folder(struct cabinet *cabinet, int folder_index, struct block_stream *stream,
	unsigned char *frame)
{
	struct cabinet_import *import = cabinet->import;
	const struct cabinet_folder *folder = &cabinet->folders[folder_index];
	struct cabinet_file *wanted[MAXIMUM_FILES];
	struct map_output output = { NULL, -1, 0, "", "" };
	struct lzx *lzx = NULL;
	unsigned long long made = 0, end = 0, frame_index = 0;
	unsigned int type = folder->compression & 0x0f;
	int count = 0, next = 0, result = 0;
	int index;

	for (index = 0; index < cabinet->file_count; index++)
	{
		struct cabinet_file *file = &cabinet->files[index];

		if (file->folder == (unsigned int)folder_index && file->wanted)
		{
			wanted[count++] = file;
			if ((unsigned long long)file->start + file->size > end)
				end = (unsigned long long)file->start + file->size;
		}
	}
	if (!count)
		return 1;
	qsort(wanted, (size_t)count, sizeof(wanted[0]), compare_starts);
	if (type != COMPRESSION_NONE && type != COMPRESSION_LZX)
		return cabinet_fail(import, "A cabinet in it uses a compression this cannot read.%s", NULL);

	block_stream_start(stream, cabinet, folder);
	if (type == COMPRESSION_LZX)
	{
		lzx = malloc(sizeof(*lzx));
		if (!lzx || !lzx_start(lzx, stream, (folder->compression >> 8) & 0x1f))
		{
			free(lzx);
			return cabinet_fail(import, "A cabinet in it uses a compression this cannot read.%s", NULL);
		}
	}

	while (made < end)
	{
		/* (a whole frame, even past the last map: x86 translation is by
		frames) */
		unsigned int size = folder->end - made < FRAME_SIZE ? (unsigned int)(folder->end - made) : FRAME_SIZE;
		unsigned long long frame_end;

		if (lzx)
		{
			if (!lzx_frame(lzx, frame, size, frame_index))
			{
				cabinet_fail(import, "A cabinet in it is damaged.%s", NULL);
				goto done;
			}
		}
		else
		{
			unsigned int at;

			for (at = 0; at < size; at++)
				frame[at] = (unsigned char)block_stream_byte(stream);
			if (block_stream_short(stream, 0))
			{
				cabinet_fail(import, "A cabinet in it is damaged.%s", NULL);
				goto done;
			}
		}
		frame_end = made + size;

		/* the bytes of the wanted maps in the frame (they never overlap) */
		while (next < count && made < frame_end)
		{
			struct cabinet_file *file = wanted[next];
			unsigned long long file_end = (unsigned long long)file->start + file->size;
			unsigned long long from, to;

			if (file->start >= frame_end)
				break;
			if (!output.file)
			{
				output.file = file;
				if (!output_open(import, &output, file->name))
					goto done;
			}
			from = file->start > made ? file->start : made;
			to = file_end < frame_end ? file_end : frame_end;
			if (to > from && !output_write(import, &output, frame + (from - made), (unsigned int)(to - from),
				file->name))
			{
				goto done;
			}
			if (to < file_end)
				break;
			if (!output_finish(import, &output))
				goto done;
			output.file = NULL;
			next++;
		}
		made = frame_end;
		frame_index++;
	}
	result = 1;

done:
	output_abandon(&output);
	if (lzx)
		free(lzx->window);
	free(lzx);
	return result;
}

int cabinet_extract_maps(struct cabinet_import *import, unsigned long long offset, unsigned long long size)
{
	struct cabinet cabinet;
	struct block_stream *stream = NULL;
	unsigned char *frame = NULL;
	int result = 0;
	int index;

	if (!cabinet_open(&cabinet, import, offset, size))
		return 0;
	if (!folder_make(import))
	{
		cabinet_close(&cabinet);
		return 0;
	}
	for (index = 0; index < cabinet.file_count; index++)
		cabinet.files[index].wanted = !folder_has(import, cabinet.files[index].name);
	stream = malloc(sizeof(*stream));
	frame = malloc(FRAME_SIZE);
	if (!stream || !frame)
	{
		cabinet_fail(import, "Out of memory.%s", NULL);
		goto done;
	}
	for (index = 0; index < cabinet.folder_count; index++)
	{
		if (!extract_folder(&cabinet, index, stream, frame))
			goto done;
	}
	result = 1;

done:
	free(stream);
	free(frame);
	cabinet_close(&cabinet);
	return result;
}

int cabinet_copy_plain_map(struct cabinet_import *import, const char *name, unsigned long long offset,
	unsigned long long size)
{
	struct map_output output = { NULL, -1, 0, "", "" };
	unsigned char *buffer;
	int result = 0;

	if (folder_has(import, name))
		return 1;
	if (!folder_make(import))
		return 0;
	buffer = malloc(1 << 20);
	if (!buffer)
		return cabinet_fail(import, "Out of memory.%s", NULL);
	if (!output_open(import, &output, name))
		goto done;
	while (size > 0)
	{
		unsigned int count = size < (1 << 20) ? (unsigned int)size : (1 << 20);

		if (!cabinet_read(import, offset, buffer, count))
		{
			cabinet_fail(import, "Could not read %s (is the file complete?).", name);
			goto done;
		}
		if (!output_write(import, &output, buffer, count, name))
			goto done;
		offset += count;
		size -= count;
	}
	result = output_finish(import, &output);

done:
	output_abandon(&output);
	free(buffer);
	return result;
}

#endif
