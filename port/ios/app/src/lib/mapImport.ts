/**
 * Imports maps the player picks: .map files, or .zip archives of them. A
 * TypeScript port of the Android launcher's MapImporter.java, which also
 * takes Xbox maps here, so that a zip of the maps folder of the player's own
 * disc can stand in for the disc image.
 *
 * Each map goes in its family's folder (map_families.c in chupathingyce), by
 * its header: an Xbox map (cache version 5) in maps; a Custom Edition map
 * (609) in maps_ce; a Halo PC 1.0 map (7) in maps_md if it is named as
 * HaloMD's are (<name>_<number>.map), else in maps_pc. Custom Edition's
 * resource maps (bitmaps.map, sounds.map, loc.map) go in maps_ce. A map
 * replaces a file of the same name in its folder; it is written as
 * <name>.part first and renamed once whole, so the game never reads a
 * half-copied map. From a zip, only the .map files are taken, by their names
 * alone (never a folder from the archive).
 */
import { listZip, readEntry, ZipError, type ZipEntry } from './zip.ts';
import { isPlainName, type Destination, type ImageReader } from './xiso.ts';

const CACHE_SIGNATURE = 0x68656164; // 'head'
const XBOX_VERSION = 5;
const HALO_PC_VERSION = 7;
const CUSTOM_EDITION_VERSION = 609;
/** a map file, at most (the largest community maps are a few hundred MB) */
const MAXIMUM_MAP_SIZE = 2048 * 2 ** 20;

export type MapFolder = 'maps' | 'maps_ce' | 'maps_md' | 'maps_pc';

/** what an import did, for the player */
export interface Report {
  imported: string[];
  skipped: string[];
}

/** a picked file, open for reading */
export interface PickedFile {
  name: string;
  size: number;
  reader: ImageReader;
}

export type ImportProgress = (status: string, done: number, total: number) => void;

class ImportError extends Error {}

/** a file's name without any folders before it */
function baseName(name: string): string {
  return name.slice(Math.max(name.lastIndexOf('/'), name.lastIndexOf('\\')) + 1);
}

function littleEndian(bytes: Uint8Array, offset: number): number {
  return (bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) | (bytes[offset + 3] << 24)) >>> 0;
}

/** the folder a map belongs in, by its name and header; null to leave it out */
export function folderFor(name: string, header: Uint8Array): MapFolder | null {
  const lower = name.toLowerCase();
  if (lower === 'bitmaps.map' || lower === 'sounds.map' || lower === 'loc.map') return 'maps_ce';
  if (header.length < 8 || littleEndian(header, 0) !== CACHE_SIGNATURE) return null;
  switch (littleEndian(header, 4)) {
    case XBOX_VERSION:
      return 'maps';
    case CUSTOM_EDITION_VERSION:
      return 'maps_ce';
    case HALO_PC_VERSION:
      return /_[0-9]+\.map$/.test(lower) ? 'maps_md' : 'maps_pc';
    default:
      return null;
  }
}

export function summary(report: Report): string {
  let text =
    report.imported.length === 0
      ? 'No maps were imported.'
      : `Imported ${report.imported.length} ${report.imported.length === 1 ? 'map' : 'maps'}:\n` +
        report.imported.join('\n');
  if (report.skipped.length > 0) text += `\n\nLeft out:\n${report.skipped.join('\n')}`;
  return text;
}

/* one map's data, a chunk at a time, into its folder */
async function importMap(
  chunks: AsyncIterator<Uint8Array>,
  name: string,
  destination: Destination,
  report: Report,
  from: string,
  status: (text: string) => void,
): Promise<void> {
  /* (the header may come in pieces: the first 8 bytes decide) */
  const head: Uint8Array[] = [];
  let headLength = 0;
  let ended = false;
  while (headLength < 8) {
    const next = await chunks.next();
    if (next.done) {
      ended = true;
      break;
    }
    head.push(next.value);
    headLength += next.value.length;
  }
  const first = new Uint8Array(headLength);
  head.reduce((at, chunk) => (first.set(chunk, at), at + chunk.length), 0);

  const folder = folderFor(name, first);
  if (!folder) {
    await chunks.return?.();
    report.skipped.push(`${from}${name}: not a Halo map`);
    return;
  }

  if (!destination.isDirectory(folder)) destination.makeDirectory(folder);
  const target = `${folder}/${name}`;
  const partial = `${target}.part`;
  status(`Copying ${name} to ${folder}`);
  const output = destination.create(partial);
  let open = true;
  try {
    let written = first.length;
    await output.write(first);
    while (!ended) {
      const next = await chunks.next();
      if (next.done) break;
      written += next.value.length;
      if (written > MAXIMUM_MAP_SIZE) throw new ImportError('larger than any map');
      await output.write(next.value);
    }
    output.close();
    open = false;
    await destination.move(partial, target);
  } catch (e) {
    if (open) output.close();
    destination.remove(partial);
    await chunks.return?.();
    throw e;
  }
  report.imported.push(target);
}

async function* readWhole(file: PickedFile, consumed: (bytes: number) => void): AsyncGenerator<Uint8Array> {
  for (let offset = 0; offset < file.size; ) {
    const bytes = await file.reader.read(offset, Math.min(file.size - offset, 1 << 20));
    if (bytes.length === 0) throw new ImportError('it is cut off (is it complete?)');
    offset += bytes.length;
    consumed(bytes.length);
    yield bytes;
  }
}

function reason(e: unknown): string {
  return e instanceof Error ? e.message : String(e);
}

/**
 * Imports every .map in the picked files (a .map, or a .zip of them) into
 * its folder. The report says what went where and what was left out, and
 * why.
 */
export async function importMaps(
  files: PickedFile[],
  destination: Destination,
  progress: ImportProgress,
): Promise<Report> {
  const report: Report = { imported: [], skipped: [] };
  const total = files.reduce((sum, file) => sum + file.size, 0);
  let done = 0;
  let status = '';
  const consumed = (bytes: number) => {
    done += bytes;
    progress(status, done, total);
  };
  const show = (text: string) => {
    status = text;
    progress(status, done, total);
  };

  for (const file of files) {
    const name = baseName(file.name);
    const lower = name.toLowerCase();
    const start = done;
    try {
      if (lower.endsWith('.zip')) {
        show(`Reading ${name}`);
        let entries: ZipEntry[];
        try {
          entries = await listZip(file.reader, file.size);
        } catch (e) {
          throw new ImportError(e instanceof ZipError ? e.message : reason(e));
        }
        let found = 0;
        for (const entry of entries) {
          const entryName = baseName(entry.name);
          if (entry.directory || !entryName.toLowerCase().endsWith('.map')) continue;
          found++;
          if (!isPlainName(entryName)) {
            report.skipped.push(`${name}: ${entry.name}: not a name the app writes`);
            continue;
          }
          try {
            await importMap(readEntry(file.reader, entry, consumed), entryName, destination, report, `${name}: `, show);
          } catch (e) {
            report.skipped.push(`${name}: ${entryName}: ${reason(e)}`);
          }
        }
        if (found === 0) report.skipped.push(`${name}: no .map files in it`);
      } else if (lower.endsWith('.map')) {
        if (!isPlainName(name)) throw new ImportError('not a name the app writes');
        await importMap(readWhole(file, consumed), name, destination, report, '', show);
      } else {
        report.skipped.push(`${name}: not a .map or .zip file`);
      }
    } catch (e) {
      report.skipped.push(`${name}: ${reason(e)}`);
    }
    /* (what was skipped in it counts as done) */
    done = start + file.size;
    progress(status, done, total);
  }
  return report;
}
