/**
 * The game's data folder on the device, and the disc image the player picks,
 * through expo-file-system.
 *
 * The data folder is the app's Documents folder, laid out as the Android
 * build's (port/android/README.md, "Game data"): maps and bink from the disc,
 * maps_ce, maps_md and maps_pc for Halo PC maps, save, logs and config.toml.
 * Documents is the folder iOS keeps (Caches it may empty), and the one the
 * Files app shows once the real app sets UIFileSharingEnabled.
 */
import { Directory, File, FileMode, Paths, type FileHandle } from 'expo-file-system';

import type { Destination, ImageReader, OutputFile } from './xiso';

export const DATA_FOLDERS = ['maps', 'maps_ce', 'maps_md', 'maps_pc', 'save', 'logs'] as const;

export function dataRoot(): Directory {
  return Paths.document;
}

/** makes the data folders, as the Android launcher does on its first start */
export function makeDataFolders(): void {
  for (const name of DATA_FOLDERS) {
    const folder = new Directory(dataRoot(), name);
    if (!folder.exists) folder.create({ intermediates: true, idempotent: true });
  }
}

/** whether the disc's maps are in place: maps/ui.map is the one the game needs first */
export function hasGameData(): boolean {
  return new File(dataRoot(), 'maps', 'ui.map').exists;
}

/** a size from the native side as a number: iOS's 64-bit ones come as BigInt,
whatever the types say, and a BigInt cannot be mixed with numbers */
export function bytes(value: number | bigint | null | undefined): number {
  return value == null ? 0 : Number(value);
}

export function availableSpace(): number {
  return bytes(Paths.availableDiskSpace);
}

function entry(path: string): string[] {
  return path.split('/');
}

/** the data folder as xiso.ts writes it */
export function dataDestination(): Destination {
  const root = dataRoot();
  return {
    makeDirectory(path) {
      new Directory(root, ...entry(path)).create({ intermediates: true, idempotent: true });
    },
    isDirectory(path) {
      return new Directory(root, ...entry(path)).exists;
    },
    list(path) {
      const folder = new Directory(root, ...entry(path));
      return folder.exists ? folder.list().map((item) => item.name) : [];
    },
    exists(path) {
      return new File(root, ...entry(path)).exists;
    },
    create(path): OutputFile {
      const file = new File(root, ...entry(path));
      file.create({ overwrite: true });
      const handle = file.open(FileMode.Truncate);
      return {
        write: (bytes) => handle.writeBytes(bytes),
        close: () => handle.close(),
      };
    },
    async move(from, to) {
      await new File(root, ...entry(from)).move(new File(root, ...entry(to)), { overwrite: true });
    },
    remove(path) {
      try {
        const file = new File(root, ...entry(path));
        if (file.exists) {
          file.delete();
          return;
        }
      } catch {
        /* a folder: the File constructor throws for one */
      }
      try {
        const folder = new Directory(root, ...entry(path));
        if (folder.exists && folder.list().length === 0) folder.delete();
      } catch {
        /* nothing there */
      }
    },
  };
}

/** a picked disc image, opened for reading; close it when done */
export interface OpenImage extends ImageReader {
  size: number;
  close(): void;
}

export function openImage(uri: string): OpenImage {
  const file = new File(uri);
  const handle: FileHandle = file.open(FileMode.ReadOnly);
  return {
    size: bytes(handle.size ?? file.size),
    async read(offset, length) {
      /* a read may return less than asked before the end: read on until
      length or the end of the image */
      handle.offset = offset;
      const first = await handle.readBytes(length);
      if (first.length === length || first.length === 0) return first;
      const bytes = new Uint8Array(length);
      bytes.set(first);
      let filled = first.length;
      while (filled < length) {
        const more = await handle.readBytes(length - filled);
        if (more.length === 0) break;
        bytes.set(more, filled);
        filled += more.length;
      }
      return bytes.subarray(0, filled);
    },
    close: () => handle.close(),
  };
}
