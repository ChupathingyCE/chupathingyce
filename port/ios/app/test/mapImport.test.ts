/**
 * Tests for src/lib/mapImport.ts and src/lib/zip.ts against maps and
 * archives built here, in memory: npm test.
 */
import assert from 'node:assert/strict';
import { test } from 'node:test';

import { zipSync } from 'fflate';

import { folderFor, importMaps, summary, type PickedFile } from '../src/lib/mapImport.ts';
import { crc32, listZip } from '../src/lib/zip.ts';
import { bytes, memoryDestination, reader } from './memory.ts';

/* a map: its cache header ('head', the version), then filler */
function map(version: number, size = 64, seed = 1): Uint8Array {
  const data = new Uint8Array(size);
  let x = seed;
  for (let i = 8; i < size; i++) data[i] = x = (x * 1103515245 + 12345) & 0xff;
  const view = new DataView(data.buffer);
  view.setUint32(0, 0x68656164, true);
  view.setUint32(4, version, true);
  return data;
}

function picked(name: string, data: Uint8Array): PickedFile {
  return { name, size: data.length, reader: reader(data) };
}

/* a stored-only archive, written by hand so that it can be zip64 (fflate
writes none) */
function storedZip(files: [string, Uint8Array][], zip64: boolean): Uint8Array {
  const parts: Uint8Array[] = [];
  const central: Uint8Array[] = [];
  let offset = 0;
  for (const [name, data] of files) {
    const local = new Uint8Array(30 + name.length);
    const lv = new DataView(local.buffer);
    lv.setUint32(0, 0x04034b50, true);
    lv.setUint32(14, crc32(data), true);
    lv.setUint32(18, data.length, true);
    lv.setUint32(22, data.length, true);
    lv.setUint16(26, name.length, true);
    local.set(bytes(name), 30);

    const extraLength = zip64 ? 4 + 24 : 0;
    const entry = new Uint8Array(46 + name.length + extraLength);
    const cv = new DataView(entry.buffer);
    cv.setUint32(0, 0x02014b50, true);
    cv.setUint32(16, crc32(data), true);
    cv.setUint32(20, zip64 ? 0xffffffff : data.length, true);
    cv.setUint32(24, zip64 ? 0xffffffff : data.length, true);
    cv.setUint16(28, name.length, true);
    cv.setUint16(30, extraLength, true);
    cv.setUint32(42, zip64 ? 0xffffffff : offset, true);
    entry.set(bytes(name), 46);
    if (zip64) {
      const at = 46 + name.length;
      cv.setUint16(at, 1, true);
      cv.setUint16(at + 2, 24, true);
      cv.setUint32(at + 4, data.length, true); /* size, compressed size, offset: 64-bit */
      cv.setUint32(at + 12, data.length, true);
      cv.setUint32(at + 20, offset, true);
    }
    parts.push(local, data);
    central.push(entry);
    offset += local.length + data.length;
  }
  const directoryOffset = offset;
  const directorySize = central.reduce((sum, c) => sum + c.length, 0);
  parts.push(...central);
  if (zip64) {
    const end64 = new Uint8Array(56);
    const ev = new DataView(end64.buffer);
    ev.setUint32(0, 0x06064b50, true);
    ev.setUint32(32, files.length, true);
    ev.setUint32(40, directorySize, true);
    ev.setUint32(48, directoryOffset, true);
    const locator = new Uint8Array(20);
    const loc = new DataView(locator.buffer);
    loc.setUint32(0, 0x07064b50, true);
    loc.setUint32(8, directoryOffset + directorySize, true);
    parts.push(end64, locator);
  }
  const end = new Uint8Array(22);
  const dv = new DataView(end.buffer);
  dv.setUint32(0, 0x06054b50, true);
  dv.setUint16(10, zip64 ? 0xffff : files.length, true);
  dv.setUint32(12, zip64 ? 0xffffffff : directorySize, true);
  dv.setUint32(16, zip64 ? 0xffffffff : directoryOffset, true);
  parts.push(end);
  return new Uint8Array(Buffer.concat(parts));
}

test('folderFor sorts maps by header and name', () => {
  assert.equal(folderFor('bloodgulch.map', map(5)), 'maps');
  assert.equal(folderFor('dangercanyon.map', map(609)), 'maps_ce');
  assert.equal(folderFor('bloodgulch_2.map', map(7)), 'maps_md');
  assert.equal(folderFor('bloodgulch.map', map(7)), 'maps_pc');
  assert.equal(folderFor('bitmaps.map', new Uint8Array()), 'maps_ce');
  assert.equal(folderFor('x.map', map(999)), null);
  assert.equal(folderFor('x.map', bytes('not a map at all')), null);
});

test('a zip of a maps folder, deflated, goes into maps; others into their folders', async () => {
  const xbox = map(5, 3 * (1 << 20) + 17, 3); /* past several 1 MB reads */
  const archive = zipSync({
    'maps/': new Uint8Array(),
    'maps/ui.map': map(5, 200, 2),
    'maps/a10.map': xbox,
    'ce/dangercanyon.map': map(609),
    'bloodgulch_2.map': map(7),
    'hang.map': [map(7), { level: 0 }],
    'bitmaps.map': bytes('resources'),
    'junk.map': bytes('nothing'),
    'readme.txt': bytes('hi'),
  });
  const { files, destination } = memoryDestination();
  const reports: number[] = [];
  const report = await importMaps([picked('maps.zip', archive)], destination, (_, done) => reports.push(done));

  assert.deepEqual(files.get('maps/a10.map'), xbox);
  assert.deepEqual(files.get('maps/ui.map'), map(5, 200, 2));
  assert.deepEqual(files.get('maps_ce/dangercanyon.map'), map(609));
  assert.deepEqual(files.get('maps_md/bloodgulch_2.map'), map(7));
  assert.deepEqual(files.get('maps_pc/hang.map'), map(7));
  assert.deepEqual(files.get('maps_ce/bitmaps.map'), bytes('resources'));
  assert.ok(![...files.keys()].some((p) => p.endsWith('.part') || p.includes('junk') || p.includes('readme')));
  assert.equal(report.imported.length, 6);
  assert.deepEqual(report.skipped, ['maps.zip: junk.map: not a Halo map']);
  assert.equal(reports.at(-1), archive.length);
  assert.match(summary(report), /^Imported 6 maps:\nmaps\/ui\.map/);
});

test('a .map picked on its own, replacing one of the same name', async () => {
  const { files, folders, destination } = memoryDestination();
  folders.add('maps_ce');
  files.set('maps_ce/dangercanyon.map', bytes('old'));
  const report = await importMaps([picked('dangercanyon.map', map(609, 5000))], destination, () => {});
  assert.deepEqual(files.get('maps_ce/dangercanyon.map'), map(609, 5000));
  assert.deepEqual(report.imported, ['maps_ce/dangercanyon.map']);
});

test('a map that fails its CRC is left out, and nothing half-written is left', async () => {
  const archive = zipSync({ 'a.map': [map(609, 4000), { level: 0 }], 'b.map': [map(609), { level: 0 }] });
  /* a byte of a.map's data, after its 30-byte local header and name */
  archive[30 + 5 + 100] ^= 0xff;
  const { files, destination } = memoryDestination();
  const report = await importMaps([picked('m.zip', archive)], destination, () => {});
  assert.ok(!files.has('maps_ce/a.map') && !files.has('maps_ce/a.map.part'));
  assert.ok(files.has('maps_ce/b.map'));
  assert.match(report.skipped[0], /^m\.zip: a\.map: .*CRC/);
});

test('a deflated map that does not inflate is left out', async () => {
  const archive = zipSync({ 'a.map': map(609, 100000) });
  for (let i = 60; i < 400; i++) archive[i] = 0xff;
  const { files, destination } = memoryDestination();
  const report = await importMaps([picked('m.zip', archive)], destination, () => {});
  assert.ok(![...files.keys()].some((p) => p.startsWith('maps_ce/a.map')));
  assert.match(report.skipped[0], /m\.zip: a\.map: /);
});

test('zip64 archives', async () => {
  const archive = storedZip(
    [
      ['maps/ui.map', map(5, 300)],
      ['maps/b30.map', map(5, 70000, 9)],
    ],
    true,
  );
  const entries = await listZip(reader(archive), archive.length);
  assert.deepEqual(
    entries.map((e) => [e.name, e.size]),
    [
      ['maps/ui.map', 300],
      ['maps/b30.map', 70000],
    ],
  );
  const { files, destination } = memoryDestination();
  await importMaps([picked('maps.zip', archive)], destination, () => {});
  assert.deepEqual(files.get('maps/b30.map'), map(5, 70000, 9));
});

test('what is not a map or an archive is reported, and the rest still imported', async () => {
  const { files, destination } = memoryDestination();
  const report = await importMaps(
    [
      picked('notes.txt', bytes('x')),
      picked('broken.zip', bytes('this is no archive')),
      picked('empty.zip', zipSync({ 'readme.txt': bytes('x') })),
      picked('bad:name.map', map(609)),
      picked('ok.map', map(609)),
    ],
    destination,
    () => {},
  );
  assert.deepEqual(report.skipped, [
    'notes.txt: not a .map or .zip file',
    'broken.zip: This is not a .zip archive.',
    'empty.zip: no .map files in it',
    'bad:name.map: not a name the app writes',
  ]);
  assert.ok(files.has('maps_ce/ok.map'));
});

test('crc32', () => {
  assert.equal(crc32(bytes('123456789')), 0xcbf43926);
  assert.equal(crc32(bytes('56789'), crc32(bytes('1234'))), 0xcbf43926);
});
