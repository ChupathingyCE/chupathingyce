/// <reference types="node" />
/* what the tests read from and write to, in memory */
import assert from 'node:assert/strict';

import type { Destination, ImageReader } from '../src/lib/xiso.ts';

export function reader(image: Uint8Array): ImageReader {
  return { read: async (offset: number, length: number) => image.slice(offset, offset + length) };
}

/* a data folder in memory: files by path, folders as a set */
export function memoryDestination() {
  const files = new Map<string, Uint8Array>();
  const folders = new Set<string>(['']);
  const destination: Destination = {
    makeDirectory: (path) => void folders.add(path),
    isDirectory: (path) => folders.has(path),
    list: (path) =>
      [...files.keys(), ...folders]
        .filter((p) => p.startsWith(`${path}/`) && !p.slice(path.length + 1).includes('/'))
        .map((p) => p.slice(path.length + 1)),
    exists: (path) => files.has(path),
    create(path) {
      assert.ok(folders.has(path.split('/').slice(0, -1).join('/')), `no folder for ${path}`);
      const parts: Uint8Array[] = [];
      let open = true;
      files.set(path, new Uint8Array());
      return {
        write: async (bytes) => void parts.push(bytes.slice()),
        close: () => {
          assert.ok(open, `${path} closed twice`);
          open = false;
          files.set(path, new Uint8Array(Buffer.concat(parts)));
        },
      };
    },
    async move(from, to) {
      files.set(to, files.get(from)!);
      files.delete(from);
    },
    remove(path) {
      files.delete(path);
      folders.delete(path);
    },
  };
  return { files, folders, destination };
}

export const bytes = (text: string) => new TextEncoder().encode(text);
