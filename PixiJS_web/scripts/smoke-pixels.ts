import { loadSceneMap } from '../src/resources/loader';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const assetRoot = path.join(repo, 'web', 'fs', 'JHOnlineData');
globalThis.fetch = (async (input) => {
  const url = String(input);
  const name = decodeURIComponent(url.split('/JHOnlineData/')[1]);
  const buf = fs.readFileSync(path.join(assetRoot, name));
  return { ok: true, status: 200, arrayBuffer: async () => buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength) };
});
const s = await loadSceneMap('01桃花岛_01.sce');
let nonzero = 0, magenta = 0;
for (let i = 0; i < s.rgba.length; i += 4) {
  const r=s.rgba[i],g=s.rgba[i+1],b=s.rgba[i+2];
  if (r|g|b) nonzero++;
  if (r>240 && g<20 && b>240) magenta++;
}
console.log({w:s.width,h:s.height,nonzero,magenta,sample:[...s.rgba.slice(10000,10008)]});
