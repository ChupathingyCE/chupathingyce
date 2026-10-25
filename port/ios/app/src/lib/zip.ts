/**
 * Reads the files of a .zip archive the player picks, through the same
 * ImageReader as the disc images: the central directory at its end (with
 * zip64's, for archives and files past 4 GB), then each file's data, stored
 * or deflated (fflate), checked against its CRC-32 as it is read.
 *
 * Encrypted files, and methods other than stored and deflate, are listed
 * with a reason and not read.
 */
import { Inflate } from 'fflate';

import type { ImageReader } from './xiso.ts';

const END_SIGNATURE = 0x06054b50;
const ZIP64_LOCATOR_SIGNATURE = 0x07064b50;
const ZIP64_END_SIGNATURE = 0x06064b50;
const CENTRAL_SIGNATURE = 0x02014b50;
const LOCAL_SIGNATURE = 0x04034b50;
const END_SIZE = 22;
const MAXIMUM_COMMENT = 0xffff;
const CENTRAL_HEADER_SIZE = 46;
const LOCAL_HEADER_SIZE = 30;
const ZIP64_EXTRA = 0x0001;
const STORED = 0;
const DEFLATE = 8;
const FLAG_ENCRYPTED = 1;
/* central directories of a few thousand files are well under this */
const MAXIMUM_DIRECTORY_SIZE = 64 << 20;
export const READ_CHUNK_SIZE = 1 << 20;

export class ZipError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'ZipError';
  }
}

export interface ZipEntry {
  /** the name as the archive has it, '/'-separated folders and all */
  name: string;
  directory: boolean;
  method: number;
  encrypted: boolean;
  crc: number;
  compressedSize: number;
  size: number;
  localOffset: number;
}

const CRC_TABLE = (() => {
  const table = new Int32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c;
  }
  return table;
})();

/** CRC-32 of bytes, continuing from crc (0 to start) */
export function crc32(bytes: Uint8Array, crc = 0): number {
  let c = ~crc;
  for (let index = 0; index < bytes.length; index++) c = CRC_TABLE[(c ^ bytes[index]) & 0xff] ^ (c >>> 8);
  return ~c >>> 0;
}

function view(bytes: Uint8Array): DataView {
  return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
}

/* a little-endian 64-bit field, as a number (exact below 2^53) */
function uint64(data: DataView, offset: number): number {
  return data.getUint32(offset, true) + data.getUint32(offset + 4, true) * 0x100000000;
}

async function readFully(reader: ImageReader, offset: number, length: number): Promise<Uint8Array> {
  const bytes = await reader.read(offset, length);
  if (bytes.length < length) throw new ZipError('The archive is cut off (is it complete?).');
  return bytes;
}

/* the central directory's offset, size and entry count */
async function findDirectory(reader: ImageReader, size: number): Promise<[number, number, number]> {
  const tailSize = Math.min(size, END_SIZE + MAXIMUM_COMMENT);
  const tail = await readFully(reader, size - tailSize, tailSize);
  const data = view(tail);
  for (let at = tailSize - END_SIZE; at >= 0; at--) {
    if (data.getUint32(at, true) !== END_SIGNATURE) continue;
    let count = data.getUint16(at + 10, true);
    let directorySize = data.getUint32(at + 12, true);
    let directoryOffset = data.getUint32(at + 16, true);
    /* zip64: the locator just before the end record gives its own */
    const locator = size - tailSize + at - 20;
    if (count === 0xffff || directorySize === 0xffffffff || directoryOffset === 0xffffffff) {
      if (locator < 0) throw new ZipError('The archive is damaged.');
      const located = view(await readFully(reader, locator, 20));
      if (located.getUint32(0, true) !== ZIP64_LOCATOR_SIGNATURE) throw new ZipError('The archive is damaged.');
      const end = view(await readFully(reader, uint64(located, 8), 56));
      if (end.getUint32(0, true) !== ZIP64_END_SIGNATURE) throw new ZipError('The archive is damaged.');
      count = uint64(end, 32);
      directorySize = uint64(end, 40);
      directoryOffset = uint64(end, 48);
    }
    if (directoryOffset + directorySize > size || directorySize > MAXIMUM_DIRECTORY_SIZE)
      throw new ZipError('The archive is damaged.');
    return [directoryOffset, directorySize, count];
  }
  throw new ZipError('This is not a .zip archive.');
}

/** the files and folders in the archive, in the order of its directory */
export async function listZip(reader: ImageReader, size: number): Promise<ZipEntry[]> {
  const [offset, directorySize, count] = await findDirectory(reader, size);
  const directory = await readFully(reader, offset, directorySize);
  const data = view(directory);
  const entries: ZipEntry[] = [];
  let at = 0;
  for (let index = 0; index < count; index++) {
    if (at + CENTRAL_HEADER_SIZE > directorySize || data.getUint32(at, true) !== CENTRAL_SIGNATURE)
      throw new ZipError('The archive is damaged.');
    const nameLength = data.getUint16(at + 28, true);
    const extraLength = data.getUint16(at + 30, true);
    const commentLength = data.getUint16(at + 32, true);
    const next = at + CENTRAL_HEADER_SIZE + nameLength + extraLength + commentLength;
    if (next > directorySize) throw new ZipError('The archive is damaged.');

    let name = '';
    for (let i = 0; i < nameLength; i++) name += String.fromCharCode(directory[at + CENTRAL_HEADER_SIZE + i]);
    const entry: ZipEntry = {
      name,
      directory: name.endsWith('/'),
      method: data.getUint16(at + 10, true),
      encrypted: (data.getUint16(at + 8, true) & FLAG_ENCRYPTED) !== 0,
      crc: data.getUint32(at + 16, true),
      compressedSize: data.getUint32(at + 20, true),
      size: data.getUint32(at + 24, true),
      localOffset: data.getUint32(at + 42, true),
    };

    /* zip64's extra field holds, in this order, those of the three that
    are 0xffffffff here */
    let extra = at + CENTRAL_HEADER_SIZE + nameLength;
    const extraEnd = extra + extraLength;
    while (extra + 4 <= extraEnd) {
      const id = data.getUint16(extra, true);
      const length = data.getUint16(extra + 2, true);
      if (id === ZIP64_EXTRA) {
        let field = extra + 4;
        const take = () => {
          if (field + 8 > extra + 4 + length) throw new ZipError('The archive is damaged.');
          const value = uint64(data, field);
          field += 8;
          return value;
        };
        if (entry.size === 0xffffffff) entry.size = take();
        if (entry.compressedSize === 0xffffffff) entry.compressedSize = take();
        if (entry.localOffset === 0xffffffff) entry.localOffset = take();
      }
      extra += 4 + length;
    }
    entries.push(entry);
    at = next;
  }
  return entries;
}

/**
 * An entry's data, a chunk at a time; reports the archive's bytes read as
 * it goes. Throws ZipError when the data does not inflate, is not the size
 * the directory gives, or fails its CRC.
 */
export async function* readEntry(
  reader: ImageReader,
  entry: ZipEntry,
  consumed: (bytes: number) => void = () => {},
): AsyncGenerator<Uint8Array> {
  if (entry.encrypted) throw new ZipError('it is encrypted');
  if (entry.method !== STORED && entry.method !== DEFLATE)
    throw new ZipError(`it is compressed with a method the app does not read (${entry.method})`);

  const local = view(await readFully(reader, entry.localOffset, LOCAL_HEADER_SIZE));
  if (local.getUint32(0, true) !== LOCAL_SIGNATURE) throw new ZipError('The archive is damaged.');
  let offset = entry.localOffset + LOCAL_HEADER_SIZE + local.getUint16(26, true) + local.getUint16(28, true);
  let remaining = entry.compressedSize;

  let crc = 0;
  let written = 0;
  let pending: Uint8Array[] = [];
  const inflate =
    entry.method === DEFLATE
      ? new Inflate((chunk) => {
          pending.push(chunk);
        })
      : null;

  try {
    while (remaining > 0) {
      const length = Math.min(remaining, READ_CHUNK_SIZE);
      const bytes = await readFully(reader, offset, length);
      offset += length;
      remaining -= length;
      if (inflate) inflate.push(bytes, remaining === 0);
      else pending.push(bytes);
      consumed(length);
      const out = pending;
      pending = [];
      for (const chunk of out) {
        written += chunk.length;
        if (written > entry.size) throw new ZipError('it is larger than the archive says');
        crc = crc32(chunk, crc);
        yield chunk;
      }
    }
    if (inflate && entry.compressedSize === 0) inflate.push(new Uint8Array(), true);
  } catch (e) {
    if (e instanceof ZipError) throw e;
    /* fflate's errors: the deflate stream is not one */
    throw new ZipError('it is damaged in the archive');
  }
  for (const chunk of pending) {
    written += chunk.length;
    crc = crc32(chunk, crc);
    yield chunk;
  }
  if (written !== entry.size) throw new ZipError('it is cut off in the archive');
  if (crc !== entry.crc) throw new ZipError('it is damaged in the archive (its CRC does not match)');
}
