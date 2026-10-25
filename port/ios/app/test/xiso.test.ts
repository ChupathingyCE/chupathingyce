/**
 * Tests for src/lib/xiso.ts against disc images built here, in memory:
 * npm test (Node's own runner, which runs the TypeScript as it is).
 */
import assert from 'node:assert/strict';
import { test } from 'node:test';

import { extractMaps, ExtractError, isPlainName } from '../src/lib/xiso.ts';
import { bytes, memoryDestination, reader } from './memory.ts';

const SECTOR = 2048;

interface FakeFile {
  name: string;
  data?: Uint8Array;
  children?: FakeFile[];
}

/* lays out an XDVDFS volume at partition: the descriptor in sector 32, then
each directory table and file in sectors of their own. Each table is a
right-leaning tree (every entry's right subtree is the next entry), which
walkTree reads in order like any other. */
function buildImage(root: FakeFile[], partition = 0): Uint8Array {
  const chunks: { sector: number; bytes: Uint8Array }[] = [];
  let next = 33;
  const place = (bytes: Uint8Array) => {
    const sector = next;
    next += Math.max(1, Math.ceil(bytes.length / SECTOR));
    chunks.push({ sector, bytes });
    return sector;
  };

  const table = (files: FakeFile[]): [number, number] => {
    const located = files.map((file) => {
      if (file.children) {
        const [sector, size] = table(file.children);
        return { file, sector, size, directory: true };
      }
      const data = file.data ?? new Uint8Array();
      return { file, sector: place(data), size: data.length, directory: false };
    });
    const offsets: number[] = [];
    let length = 0;
    for (const item of located) {
      offsets.push(length);
      length = (length + 14 + item.file.name.length + 3) & ~3;
    }
    const bytes = new Uint8Array(length);
    const view = new DataView(bytes.buffer);
    located.forEach((item, index) => {
      const at = offsets[index];
      view.setUint16(at, 0, true);
      view.setUint16(at + 2, index + 1 < located.length ? offsets[index + 1] / 4 : 0, true);
      view.setUint32(at + 4, item.sector, true);
      view.setUint32(at + 8, item.size, true);
      bytes[at + 12] = item.directory ? 0x10 : 0x20;
      bytes[at + 13] = item.file.name.length;
      for (let i = 0; i < item.file.name.length; i++) bytes[at + 14 + i] = item.file.name.charCodeAt(i);
    });
    return [place(bytes), bytes.length];
  };

  const [rootSector, rootSize] = table(root);
  const image = new Uint8Array(partition + next * SECTOR);
  const descriptor = new DataView(image.buffer, partition + 32 * SECTOR, SECTOR);
  const magic = 'MICROSOFT*XBOX*MEDIA';
  for (let i = 0; i < magic.length; i++) descriptor.setUint8(i, magic.charCodeAt(i));
  descriptor.setUint32(20, rootSector, true);
  descriptor.setUint32(24, rootSize, true);
  for (const chunk of chunks) image.set(chunk.bytes, partition + chunk.sector * SECTOR);
  return image;
}

const big = new Uint8Array((1 << 20) * 2 + 123).map((_, i) => i * 7);

const halo = (): FakeFile[] => [
  { name: 'default.xbe', data: bytes('xbe') },
  {
    name: 'maps',
    children: [
      { name: 'ui.map', data: bytes('ui') },
      { name: 'a10.map', data: big },
      { name: 'sub', children: [{ name: 'inner.map', data: bytes('inner') }] },
    ],
  },
  { name: 'bink', children: [{ name: 'intro.bik', data: bytes('intro') }, { name: 'Credits.BIK', data: bytes('c') }] },
];

test('extracts the maps folder, folders inside it and the movies', async () => {
  const { files, folders, destination } = memoryDestination();
  const reports: number[] = [];
  await extractMaps(reader(buildImage(halo())), destination, (_, done) => reports.push(done));
  assert.deepEqual(files.get('maps/ui.map'), bytes('ui'));
  assert.deepEqual(files.get('maps/a10.map'), big);
  assert.deepEqual(files.get('maps/sub/inner.map'), bytes('inner'));
  assert.deepEqual(files.get('bink/intro.bik'), bytes('intro'));
  assert.ok(files.has('bink/Credits.BIK'));
  assert.ok(!files.has('default.xbe'));
  assert.ok(![...files.keys(), ...folders].some((p) => p.includes('partial')));
  assert.equal(reports.at(-1), 2 + big.length + 5 + 5 + 1);
});

test('finds the game partition of a whole disc (XGD3, after the others are tried)', async () => {
  const { files, destination } = memoryDestination();
  await extractMaps(reader(buildImage(halo(), 0x02080000)), destination, () => {});
  assert.deepEqual(files.get('maps/ui.map'), bytes('ui'));
});

test('never copies a movie over one already there, whatever its case', async () => {
  const { files, folders, destination } = memoryDestination();
  folders.add('bink');
  files.set('bink/INTRO.bik', bytes('mine'));
  await extractMaps(reader(buildImage(halo())), destination, () => {});
  assert.deepEqual(files.get('bink/INTRO.bik'), bytes('mine'));
  assert.ok(!files.has('bink/intro.bik'));
  assert.ok(files.has('bink/Credits.BIK'));
});

test('picks the language folder of a European disc, else English', async () => {
  const european = (languages: string[]): FakeFile[] =>
    languages.map((l) => ({ name: `maps_${l}`, children: [{ name: 'ui.map', data: bytes(l) }] }));
  for (const [languages, device, expected] of [
    [['fr', 'de', 'en'], 'de', 'de'],
    [['fr', 'en'], 'ja', 'en'],
    [['fr', 'de'], 'ja', 'fr'],
  ] as const) {
    const { files, destination } = memoryDestination();
    await extractMaps(reader(buildImage(european([...languages]))), destination, () => {}, device);
    assert.deepEqual(files.get('maps/ui.map'), bytes(expected));
  }
});

test('says what is wrong with an image that is not a Halo disc', async () => {
  const cases: [Uint8Array, RegExp][] = [
    [new Uint8Array(100 * SECTOR), /not an Xbox disc image/],
    [buildImage([{ name: 'other', children: [] }]), /no maps folder/],
    [buildImage([{ name: 'maps', children: [{ name: 'x.map', data: bytes('x') }] }]), /no ui\.map/],
  ];
  for (const [image, message] of cases) {
    const { destination } = memoryDestination();
    await assert.rejects(extractMaps(reader(image), destination, () => {}), (e: unknown) => {
      assert.ok(e instanceof ExtractError);
      assert.match(e.message, message);
      return true;
    });
  }
});

test('an image that ends inside a file fails with a message and leaves no maps folder', async () => {
  const image = buildImage(halo());
  const { files, destination } = memoryDestination();
  /* the second 1 MB read is a10.map's second chunk: it comes back short */
  let chunks = 0;
  const cut = {
    read: async (offset: number, length: number) =>
      length === 1 << 20 && ++chunks === 2 ? image.slice(offset, offset + 10) : image.slice(offset, offset + length),
  };
  await assert.rejects(extractMaps(cut, destination, () => {}), /Could not read a10\.map/);
  assert.ok(![...files.keys()].some((p) => p.startsWith('maps/')));
});

test('a table that loops is reported as damaged', async () => {
  const image = buildImage(halo());
  const view = new DataView(image.buffer);
  const root = view.getUint32(32 * SECTOR + 20, true);
  /* the second entry (maps, at byte 28) gets itself as its right subtree */
  view.setUint16(root * SECTOR + 28 + 2, 28 / 4, true);
  const { destination } = memoryDestination();
  await assert.rejects(extractMaps(reader(image), destination, () => {}), /damaged/);
});

test('isPlainName', () => {
  for (const name of ['ui.map', 'loading.tga', 'a b.map']) assert.ok(isPlainName(name));
  for (const name of ['', '.', '..', 'a/b', 'a\\b', 'c:', 'é.map', 'x\u0001']) assert.ok(!isPlainName(name));
});
