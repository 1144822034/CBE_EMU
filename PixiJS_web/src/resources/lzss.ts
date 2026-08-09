/**
 * CBE resource unwrap — mirrors admin `vm_mock_admin_load_data_payload`
 * and scene LZSS path.
 *
 * On-disk:
 *   LE u32 block_len (= fileSize - 4)
 *   u8 type
 *   type==1 (GIF):  payload = bytes after type (no LZSS)
 *   type==2 (SCE/MAP/actor): LZSS stream at `type` (same as GetStreamDataFormRes)
 */

import { readU32BE, readU32LE } from "./binary";

export function decodeLzssStream(res: Uint8Array): Uint8Array {
  if (res.length < 9) throw new Error("lzss: truncated header");
  const type = res[0]!;
  if (type !== 1 && type !== 2) throw new Error(`lzss: bad type ${type}`);

  const compressedLen = readU32BE(res, 1);
  const decodedLen = readU32BE(res, 5) & 0x7fffffff;
  if (
    compressedLen === 0 ||
    decodedLen === 0 ||
    9 + compressedLen > res.length
  ) {
    throw new Error(
      `lzss: bad lengths compressed=${compressedLen} decoded=${decodedLen} file=${res.length}`,
    );
  }

  const src = res.subarray(9, 9 + compressedLen);
  const out = new Uint8Array(decodedLen);
  let srcPos = 0;
  let dstPos = 0;

  while (srcPos < compressedLen && dstPos < decodedLen) {
    const token = src[srcPos]!;
    if ((token & 0x80) !== 0) {
      let count = token & 0x7f;
      const avail = decodedLen - dstPos;
      if (count > avail) count = avail;
      if (count === 0 || srcPos + 1 + count > compressedLen) {
        throw new Error("lzss: bad literal run");
      }
      out.set(src.subarray(srcPos + 1, srcPos + 1 + count), dstPos);
      srcPos += 1 + count;
      dstPos += count;
    } else {
      let count = token >> 1;
      const avail = decodedLen - dstPos;
      if (count > avail) count = avail;
      if (count === 0 || srcPos + 1 >= compressedLen) {
        throw new Error("lzss: bad backref");
      }
      const distance = (((token << 8) & 0x1ff) | src[srcPos + 1]!) >>> 0;
      if (distance === 0 || distance > dstPos) {
        throw new Error(`lzss: bad distance ${distance} at ${dstPos}`);
      }
      const copySrc = dstPos - distance;
      for (let i = 0; i < count; i++) {
        out[dstPos + i] = out[copySrc + i]!;
      }
      srcPos += 2;
      dstPos += count;
    }
  }

  return out.subarray(0, dstPos);
}

export function unwrapResource(raw: Uint8Array): Uint8Array {
  if (raw.length < 5) throw new Error("resource: too short");

  const blockLen = readU32LE(raw, 0);
  if (blockLen === raw.length - 4 && blockLen >= 1) {
    const type = raw[4]!;
    if (type === 1) {
      // GIF / LKV2: strip length+type only (web_admin_server.c).
      return raw.subarray(5, 5 + blockLen - 1);
    }
    if (type === 2) {
      return decodeLzssStream(raw.subarray(4, 4 + blockLen));
    }
    throw new Error(`resource: unknown type ${type}`);
  }

  // Bare stream fallbacks.
  if (raw[0] === 2) return decodeLzssStream(raw);
  if (raw[0] === 1) {
    // Bare type-1: treat remainder as payload if LZSS header looks invalid.
    try {
      return decodeLzssStream(raw);
    } catch {
      return raw.subarray(1);
    }
  }
  return raw;
}
