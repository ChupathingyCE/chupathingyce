/* Tests for src/lib/address.ts: npm test */
import assert from 'node:assert/strict';
import { test } from 'node:test';

import { checkAddress, downloadName } from '../src/lib/address.ts';

test('checkAddress takes web addresses only', () => {
  assert.ok(checkAddress('  https://example.com/maps.zip ') instanceof URL);
  assert.ok(checkAddress('http://192.168.1.5:8080/maps.zip') instanceof URL);
  assert.equal(checkAddress('example.com/maps.zip'), 'That is not a web address.');
  assert.equal(checkAddress('file:///etc/passwd'), 'The address must start with https://.');
  assert.equal(checkAddress('ftp://example.com/a.zip'), 'The address must start with https://.');
});

test('downloadName keeps .map and .zip names, and makes the rest a .zip', () => {
  const name = (text: string) => downloadName(new URL(text));
  assert.equal(name('https://example.com/0pBrlU.zip'), '0pBrlU.zip');
  assert.equal(name('https://example.com/a/dangercanyon.MAP?x=1'), 'dangercanyon.MAP');
  assert.equal(name('https://example.com/get?id=4'), 'get.zip');
  assert.equal(name('https://example.com/'), 'download.zip');
  assert.equal(name('https://example.com/my%20maps.zip'), 'my maps.zip');
  assert.equal(name('https://example.com/..%2F..%2Fx.zip'), '_.._x.zip');
  assert.equal(name('https://example.com/bad%E0%A4%A.zip'), 'bad_E0_A4_A.zip');
});
