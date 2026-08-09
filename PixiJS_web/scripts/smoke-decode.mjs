/**
 * Offline smoke: resource unwrap + MAP parse for 01桃花岛_01.
 * Full GIF/LKV2 is exercised in the browser; here we only check type-1 strip.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(root, "../..");
const assetRoot = [
  path.join(repo, "web", "fs", "JHOnlineData"),
  path.join(repo, "bin", "JHOnlineData"),
].find((p) => fs.existsSync(p));

function readU32BE(buf, off) {
  return buf.readUInt32BE(off);
}
function readU32LE(buf, off) {
  return buf.readUInt32LE(off);
}

function decodeLzssStream(res) {
  const compressedLen = readU32BE(res, 1);
  const decodedLen = readU32BE(res, 5) & 0x7fffffff;
  if (9 + compressedLen > res.length) throw new Error("truncated");
  const src = res.subarray(9, 9 + compressedLen);
  const out = Buffer.alloc(decodedLen);
  let srcPos = 0,
    dstPos = 0;
  while (srcPos < compressedLen && dstPos < decodedLen) {
    const token = src[srcPos];
    if (token & 0x80) {
      let count = token & 0x7f;
      if (count > decodedLen - dstPos) count = decodedLen - dstPos;
      src.copy(out, dstPos, srcPos + 1, srcPos + 1 + count);
      srcPos += 1 + count;
      dstPos += count;
    } else {
      let count = token >> 1;
      if (count > decodedLen - dstPos) count = decodedLen - dstPos;
      const distance = ((token << 8) & 0x1ff) | src[srcPos + 1];
      const copySrc = dstPos - distance;
      for (let i = 0; i < count; i++) out[dstPos + i] = out[copySrc + i];
      srcPos += 2;
      dstPos += count;
    }
  }
  return out.subarray(0, dstPos);
}

function unwrap(raw) {
  const blockLen = raw.readUInt32LE(0);
  if (blockLen !== raw.length - 4) throw new Error("bad block_len");
  const type = raw[4];
  if (type === 1) return raw.subarray(5, 5 + blockLen - 1);
  if (type === 2) return decodeLzssStream(raw.subarray(4, 4 + blockLen));
  throw new Error("bad type " + type);
}

function parseMap(data) {
  let pos = 0;
  const imageCount = readU32LE(data, pos);
  pos += 4;
  const names = [];
  for (let i = 0; i < imageCount; i++) {
    const n = data[pos++];
    names.push(data.subarray(pos, pos + n).toString("ascii"));
    pos += n;
  }
  const width = readU32LE(data, pos);
  const height = readU32LE(data, pos + 4);
  const tw = readU32LE(data, pos + 8);
  const th = readU32LE(data, pos + 12);
  pos += 16;
  return {
    names,
    width,
    height,
    tw,
    th,
    cols: Math.ceil(width / tw),
    rows: Math.ceil(height / th),
  };
}

const sceneName = "01桃花岛_01.sce";
const sce = unwrap(fs.readFileSync(path.join(assetRoot, sceneName)));
const sce2 = sce.indexOf("SCE2");
const mapNameLen = sce[sce2 + 10];
const mapName = new TextDecoder("gbk").decode(
  sce.subarray(sce2 + 11, sce2 + 11 + mapNameLen),
);
const map = parseMap(unwrap(fs.readFileSync(path.join(assetRoot, mapName))));
console.log("map", map);

for (const name of map.names) {
  const payload = unwrap(fs.readFileSync(path.join(assetRoot, name)));
  const dict = payload.readUInt32BE(0);
  console.log(
    "gif",
    name,
    "payload",
    payload.length,
    "dict",
    dict,
    "flags",
    payload[4].toString(16),
  );
}

console.log("smoke OK");
