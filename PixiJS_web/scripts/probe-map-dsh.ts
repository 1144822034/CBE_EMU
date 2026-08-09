/**
 * Dump wMap / sMap / line DSH columns for world-map UI.
 */
import fs from "node:fs";
import { decodeGbk, readU32LE } from "../src/resources/binary.ts";

function parseDsh(path: string) {
  const raw = new Uint8Array(fs.readFileSync(path));
  const payloadLen = readU32LE(raw, 0);
  const cols = readU32LE(raw, 4);
  const rows = readU32LE(raw, 8);
  const headerBytes = readU32LE(raw, 12);
  console.log("\n===", path.split(/[/\\]/).pop(), `cols=${cols} rows=${rows} hdr=${headerBytes} len=${raw.length}/${payloadLen + 4}`);
  let pos = 16;
  const headers: string[] = [];
  for (let c = 0; c < cols && pos < raw.length; c++) {
    const fl = raw[pos++]!;
    const name = decodeGbk(raw.subarray(pos, pos + fl)).replace(/\0+$/, "");
    pos += fl;
    headers.push(name);
  }
  console.log("headers:", headers.join(" | "));
  // After field headers, official format uses headerBytes from offset 16 —
  // verify: if headerBytes includes field names, pos should be 16+headerBytes
  if (pos !== 16 + headerBytes) {
    console.log("note: pos after names", pos, "vs 16+headerBytes", 16 + headerBytes);
    pos = 16 + headerBytes; // match smap.ts
  }
  const maxShow = Math.min(rows, 8);
  for (let row = 0; row < maxShow && pos + 4 <= raw.length; row++) {
    const rowLen = readU32LE(raw, pos);
    let rp = pos + 4;
    const end = rp + rowLen;
    const cells: string[] = [];
    for (let c = 0; c < cols && rp < end; c++) {
      const vl = raw[rp++]!;
      if (rp + vl > end) break;
      const cell = decodeGbk(raw.subarray(rp, rp + vl)).replace(/\0+$/, "").trim();
      rp += vl;
      cells.push(cell);
    }
    pos = end;
    console.log(`r${row}:`, cells.map((v, i) => `${headers[i] ?? i}=${v}`).join(", "));
  }
}

const base = "../bin/JHOnlineData";
for (const f of ["wMap.dsh", "wMapLine.dsh", "sMap.dsh", "sMapLine.dsh"]) {
  parseDsh(`${base}/${f}`);
}
