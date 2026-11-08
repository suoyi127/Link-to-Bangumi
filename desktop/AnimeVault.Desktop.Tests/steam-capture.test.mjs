import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';
import assert from 'node:assert/strict';

const source = readFileSync(new URL('../AnimeVault.Desktop/SteamLoginProtocol.cs', import.meta.url), 'utf8');
const script = source.split('internal const string CaptureScript = """')[1].split('""";')[0];
const key = 'A'.repeat(32);
const steamId = '76561198000000000';
const element = (innerText, shown = true) => ({ innerText, getClientRects: () => shown ? [1] : [] });
function capture({ origin = 'https://steamcommunity.com', path = '/dev/apikey', paragraphs = [element(`Key: ${key}`)], id = steamId, links = [] } = {}) {
  return runInNewContext(script, {
    location: { origin, pathname: path }, window: { g_steamID: id }, URL,
    getComputedStyle: () => ({ visibility: 'visible' }),
    document: { querySelectorAll: selector => selector === '#bodyContents_ex p' ? paragraphs : links }
  });
}
assert.equal(capture().apiKey, key);
assert.equal(capture().steamId, steamId);
assert.equal(capture({ origin: 'https://evil.test' }), null);
assert.equal(capture({ path: '/login' }), null);
assert.equal(capture({ paragraphs: [element(key)] }), null);
assert.equal(capture({ paragraphs: [element(`Other Key: ${key}`)] }), null);
assert.equal(capture({ paragraphs: [element(`Key: ${key}`, false)] }), null);
assert.equal(capture({ id: '123' }), null);
assert.equal(capture({ id: '', links: [{ ...element('profile'), href: `https://steamcommunity.com/profiles/${steamId}/` }] }).steamId, steamId);
assert.equal(capture({ id: '', links: [{ ...element('profile'), href: `https://evil.test/profiles/${steamId}/` }] }), null);
console.log('Steam capture script: 10 fixture checks passed');
