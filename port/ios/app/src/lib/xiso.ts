/**
 * Copies the maps folder out of an Xbox disc image (an "xiso", or a whole
 * disc's ".iso"), as the desktop games do: a TypeScript port of the Android
 * launcher's XisoExtractor.java, itself a port of port/linux/src/xiso.c in
 * chupathingyce, whose reader is based on the XISO reader in Milenko's
 * Theseus project (GPL-3.0-or-later). xiso.c describes the disc's file
 * system (XDVDFS): 2048-byte sectors, a volume descriptor in sector 32 that
 * gives the root directory's sector and size, and directories whose entries
 * form a binary tree (each entry: left and right subtree offsets in 4-byte
 * units, 0 for none; the start sector; the size; attributes; the name's
 * length and the name; 4-byte aligned, never across a sector, the rest of a
 * sector 0xff padding). Images made from a whole disc put the game partition
 * further in, at one of the offsets below.
 *
 * Every file in the maps folder, and in any folders inside it, is written to
 * maps.partial first and moved into maps once they all are, so an
 * interrupted extraction never leaves maps/ui.map behind.
 *
 * The .bik files in the disc's bink folder (the movies) that the
 * destination's bink folder lacks are copied after the maps, each as
 * <name>.partial first, and never over a file already there. A disc without
 * a bink folder, or a movie that cannot be copied, does not fail the
 * extraction.
 *
 * The disc is read and the files written through ImageReader and
 * Destination, so that this file knows nothing of expo-file-system and runs
 * under Node for the tests (storage.ts holds the app's side).
 */

const SECTOR_SIZE = 2048;
const VOLUME_SECTOR = 32;
const ENTRY_HEADER_SIZE = 14;
const ATTRIBUTE_DIRECTORY = 0x10;
/* directory tables are a few sectors; anything much larger is not one */
const MAXIMUM_DIRECTORY_SIZE = 4 << 20;
/* an AVL tree of a full table is under 30 deep; deeper is a loop */
const MAXIMUM_TREE_DEPTH = 64;
const MAXIMUM_FOLDER_DEPTH = 16;
const MAXIMUM_FILES = 4096;
export const COPY_BUFFER_SIZE = 1 << 20;
const VOLUME_MAGIC = 'MICROSOFT*XBOX*MEDIA';

/* where the game partition starts: an xiso, an original Xbox disc (XGD1),
and the Xbox 360's XGD2 and XGD3 discs */
const PARTITION_OFFSETS = [0, 0x18300000, 0x0fd90000, 0x02080000];

/** reads the disc image; may return fewer bytes than asked at its end */
export interface ImageReader {
  read(offset: number, length: number): Promise<Uint8Array>;
}

/** an open file being written */
export interface OutputFile {
  write(bytes: Uint8Array): Promise<void>;
  close(): void;
}

/** the data folder; paths are '/'-separated, below it */
export interface Destination {
  makeDirectory(path: string): void;
  isDirectory(path: string): boolean;
  /** the names in a folder, [] if there is none */
  list(path: string): string[];
  exists(path: string): boolean;
  /** creates the file, replacing one there */
  create(path: string): OutputFile;
  /** moves a file, replacing one at to */
  move(from: string, to: string): Promise<void>;
  /** deletes a file, or an empty folder; nothing if there is none */
  remove(path: string): void;
}

export type Progress = (file: string, done: number, total: number) => void;

/** a reason the player can act on */
export class ExtractError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'ExtractError';
  }
}

/** a table that does not hold together */
class DamagedError extends Error {}

interface Entry {
  /* the path below the directory listed, '/'-separated */
  path: string;
  sector: number;
  size: number;
  directory: boolean;
}

/**
 * whether a name is one to write as a file or folder name: no path, no "."
 * or "..", and only printable ASCII that every file system takes as a plain
 * name (a disc's maps folder holds .map files and loading.tga)
 */
export function isPlainName(name: string): boolean {
  if (name === '' || name === '.' || name === '..') return false;
  for (let index = 0; index < name.length; index++) {
    const c = name.charCodeAt(index);
    if (c < 0x20 || c > 0x7e || '/\\:*?"<>|'.includes(name[index])) return false;
  }
  return true;
}

function join(folder: string, name: string): string {
  return folder === '' ? name : `${folder}/${name}`;
}

/* adds the entries of the subtree at offset (in 4-byte units) to list, in
order, their paths after prefix; names that cannot be written as they are
(isPlainName) are left out */
function walkTree(table: DataView, offset: number, depth: number, prefix: string, list: Entry[]): void {
  const position = offset * 4;
  const size = table.byteLength;
  if (depth > MAXIMUM_TREE_DEPTH || position + ENTRY_HEADER_SIZE > size) throw new DamagedError();
  const length = table.getUint8(position + 13);
  if (length === 0 || position + ENTRY_HEADER_SIZE + length > size) throw new DamagedError();

  const left = table.getUint16(position, true);
  if (left !== 0) walkTree(table, left, depth + 1, prefix, list);

  let name = '';
  for (let index = 0; index < length; index++)
    name += String.fromCharCode(table.getUint8(position + ENTRY_HEADER_SIZE + index));
  if (isPlainName(name)) {
    if (list.length >= MAXIMUM_FILES) throw new DamagedError();
    list.push({
      path: join(prefix, name),
      sector: table.getUint32(position + 4, true),
      size: table.getUint32(position + 8, true),
      directory: (table.getUint8(position + 12) & ATTRIBUTE_DIRECTORY) !== 0,
    });
  }

  const right = table.getUint16(position + 2, true);
  if (right !== 0) walkTree(table, right, depth + 1, prefix, list);
}

/* the maps folder among the root's entries: "maps", or on a multi-language
European disc (no "maps", a "maps_xx" per language instead) the one for the
device's language, else English's, else the first */
function findMapsFolder(root: Entry[], language: string): Entry | undefined {
  const maps = root.find((entry) => entry.directory && entry.path.toLowerCase() === 'maps');
  if (maps) return maps;
  const wanted = `maps_${language.toLowerCase()}`;
  let english: Entry | undefined;
  let first: Entry | undefined;
  for (const entry of root) {
    const name = entry.path.toLowerCase();
    if (!entry.directory || name.length !== 7 || !name.startsWith('maps_')) continue;
    if (name === wanted) return entry;
    if (!english && name === 'maps_en') english = entry;
    if (!first) first = entry;
  }
  return english ?? first;
}

class Extractor {
  private partition = 0;
  private readonly image: ImageReader;
  private readonly destination: Destination;
  private readonly progress: Progress;

  constructor(image: ImageReader, destination: Destination, progress: Progress) {
    this.image = image;
    this.destination = destination;
    this.progress = progress;
  }

  /* reads length bytes at offset in the game partition */
  private async readAt(offset: number, length: number): Promise<Uint8Array> {
    const bytes = await this.image.read(this.partition + offset, length);
    if (bytes.length < length) throw new ExtractError('Could not read the disc image (is it complete?).');
    return bytes;
  }

  /* finds the game partition; returns the root directory's sector and size */
  private async findVolume(): Promise<[number, number]> {
    for (const offset of PARTITION_OFFSETS) {
      this.partition = offset;
      let descriptor: Uint8Array;
      try {
        descriptor = await this.readAt(VOLUME_SECTOR * SECTOR_SIZE, SECTOR_SIZE);
      } catch (e) {
        if (e instanceof ExtractError) continue;
        throw e;
      }
      let magic = true;
      for (let index = 0; index < VOLUME_MAGIC.length; index++)
        magic &&= descriptor[index] === VOLUME_MAGIC.charCodeAt(index);
      if (magic) {
        const view = new DataView(descriptor.buffer, descriptor.byteOffset, descriptor.byteLength);
        return [view.getUint32(20, true), view.getUint32(24, true)];
      }
    }
    throw new ExtractError('This is not an Xbox disc image.');
  }

  /* adds a directory's entries to list, and with recurse everything in the
  folders in it, after its own entries (so a folder always comes before what
  it holds) */
  private async listDirectory(
    sector: number,
    size: number,
    prefix: string,
    recurse: boolean,
    depth: number,
    list: Entry[],
    damaged: string,
  ): Promise<void> {
    if (depth > MAXIMUM_FOLDER_DEPTH || size > MAXIMUM_DIRECTORY_SIZE) throw new ExtractError(damaged);
    const first = list.length;
    if (size > 0) {
      try {
        const table = await this.readAt(sector * SECTOR_SIZE, size);
        walkTree(new DataView(table.buffer, table.byteOffset, table.byteLength), 0, 0, prefix, list);
      } catch (e) {
        if (e instanceof ExtractError || e instanceof DamagedError) throw new ExtractError(damaged);
        throw e;
      }
    }
    if (!recurse) return;
    const last = list.length;
    for (let index = first; index < last; index++) {
      const entry = list[index];
      if (entry.directory) await this.listDirectory(entry.sector, entry.size, entry.path, true, depth + 1, list, damaged);
    }
  }

  /* copies one file's sectors to path; returns done with its bytes added */
  private async copyFile(entry: Entry, path: string, done: number, total: number): Promise<number> {
    let offset = entry.sector * SECTOR_SIZE;
    let remaining = entry.size;
    let output: OutputFile;
    try {
      output = this.destination.create(path);
    } catch {
      throw new ExtractError(`Could not write ${path} (is the storage full?).`);
    }
    try {
      while (remaining > 0) {
        const count = Math.min(remaining, COPY_BUFFER_SIZE);
        let bytes: Uint8Array;
        try {
          bytes = await this.readAt(offset, count);
        } catch {
          throw new ExtractError(`Could not read ${entry.path} from the disc image (is it complete?).`);
        }
        try {
          await output.write(bytes);
        } catch {
          throw new ExtractError(`Could not write ${path} (is the storage full?).`);
        }
        offset += count;
        remaining -= count;
        done += count;
        this.progress(entry.path, done, total);
      }
    } finally {
      output.close();
    }
    return done;
  }

  /* the .bik files of the disc's bink folder that bink lacks (whatever the
  case of their names there); none if the disc has no bink folder */
  private async missingMovies(root: Entry[]): Promise<Entry[]> {
    const folder = root.find((entry) => entry.directory && entry.path.toLowerCase() === 'bink');
    if (!folder) return [];
    const all: Entry[] = [];
    try {
      await this.listDirectory(folder.sector, folder.size, '', false, 0, all, "The disc image's bink folder is damaged.");
    } catch (e) {
      if (e instanceof ExtractError) return [];
      throw e;
    }
    const present = this.destination.list('bink').map((name) => name.toLowerCase());
    return all.filter(
      (entry) =>
        !entry.directory && entry.path.toLowerCase().endsWith('.bik') && !present.includes(entry.path.toLowerCase()),
    );
  }

  private makeDirectory(path: string): void {
    try {
      if (!this.destination.isDirectory(path)) this.destination.makeDirectory(path);
    } catch {
      throw new ExtractError(`Could not create ${path}.`);
    }
  }

  async extract(language: string): Promise<void> {
    const [rootSector, rootSize] = await this.findVolume();

    /* the root's maps folder */
    const rootEntries: Entry[] = [];
    await this.listDirectory(rootSector, rootSize, '', false, 0, rootEntries, "The disc image's file system is damaged.");
    const maps = findMapsFolder(rootEntries, language);
    if (!maps) throw new ExtractError('The disc image has no maps folder: it is not a Halo disc.');

    /* everything in it */
    const entries: Entry[] = [];
    await this.listDirectory(maps.sector, maps.size, '', true, 0, entries, "The disc image's maps folder is damaged.");
    let total = 0;
    let hasUi = false;
    for (const entry of entries) {
      if (entry.directory) continue;
      total += entry.size;
      hasUi ||= entry.path.toLowerCase() === 'ui.map';
    }
    if (!hasUi) throw new ExtractError("The disc image's maps folder has no ui.map: it is not a Halo disc.");

    const movies = await this.missingMovies(rootEntries);
    for (const movie of movies) total += movie.size;

    const partial = 'maps.partial';
    const finished = 'maps';
    this.makeDirectory(partial);
    let done = 0;
    for (const entry of entries) {
      const path = join(partial, entry.path);
      if (entry.directory) {
        this.makeDirectory(path);
        continue;
      }
      done = await this.copyFile(entry, path, done, total);
    }

    /* (the maps folder may be there, empty: the app makes it) */
    this.makeDirectory(finished);
    for (const entry of entries) {
      const to = join(finished, entry.path);
      if (entry.directory) {
        this.makeDirectory(to);
        continue;
      }
      try {
        await this.destination.move(join(partial, entry.path), to);
      } catch {
        throw new ExtractError(`Could not move ${entry.path} into ${finished}.`);
      }
    }
    /* (the folders left behind, now empty: the deepest first) */
    for (let index = entries.length - 1; index >= 0; index--) {
      if (entries[index].directory) this.destination.remove(join(partial, entries[index].path));
    }
    this.destination.remove(partial);

    /* the movies; the maps are all there, so one that fails is left out (the
    game plays without it) */
    if (movies.length === 0) return;
    try {
      this.makeDirectory('bink');
    } catch {
      return;
    }
    for (const entry of movies) {
      const from = join('bink', `${entry.path}.partial`);
      const to = join('bink', entry.path);
      try {
        done = await this.copyFile(entry, from, done, total);
        if (this.destination.exists(to)) this.destination.remove(from);
        else await this.destination.move(from, to);
      } catch {
        this.destination.remove(from);
        return;
      }
    }
  }
}

/**
 * Extracts the game's maps (and the movies the data folder lacks) from a
 * disc image. language picks among a European disc's maps_xx folders.
 * Throws ExtractError with a message for the player.
 */
export function extractMaps(
  image: ImageReader,
  destination: Destination,
  progress: Progress,
  language = 'en',
): Promise<void> {
  return new Extractor(image, destination, progress).extract(language);
}
