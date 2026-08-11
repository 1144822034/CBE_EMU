/** Little/big-endian helpers for JHOnlineData payloads. */

export function readU16LE(buf: Uint8Array, off: number): number {
  return buf[off]! | (buf[off + 1]! << 8);
}

export function readU32LE(buf: Uint8Array, off: number): number {
  return (
    (buf[off]! |
      (buf[off + 1]! << 8) |
      (buf[off + 2]! << 16) |
      (buf[off + 3]! << 24)) >>>
    0
  );
}

export function readI32LE(buf: Uint8Array, off: number): number {
  return (
    buf[off]! |
    (buf[off + 1]! << 8) |
    (buf[off + 2]! << 16) |
    (buf[off + 3]! << 24)
  );
}

export function readU32BE(buf: Uint8Array, off: number): number {
  return (
    ((buf[off]! << 24) |
      (buf[off + 1]! << 16) |
      (buf[off + 2]! << 8) |
      buf[off + 3]!) >>>
    0
  );
}

export function asciiAt(buf: Uint8Array, off: number, len: number): string {
  let s = "";
  for (let i = 0; i < len; i++) s += String.fromCharCode(buf[off + i]!);
  return s;
}

/** Decode GBK / mixed bytes — shared with WT wire codec. */
export { decodeGbk } from "../net/gbk";

/**
 * Match mock_server `parse_dsh_u32` / `parse_dsh_s32`: leading digits, stop at
 * first non-digit. equip.dsh has dirty cells like `34:` / `70:` that `Number()`
 * turns into NaN → 0.
 */
export function parseDshInt(raw: string, fallback = 0): number {
  const s = (raw ?? "").trim();
  if (!s) return fallback;
  let pos = 0;
  let sign = 1;
  if (s.charCodeAt(pos) === 0x2d /* - */) {
    sign = -1;
    pos++;
  } else if (s.charCodeAt(pos) === 0x2b /* + */) {
    pos++;
  }
  let value = 0;
  let haveDigit = false;
  for (; pos < s.length; pos++) {
    const c = s.charCodeAt(pos);
    if (c < 0x30 || c > 0x39) break;
    haveDigit = true;
    value = value * 10 + (c - 0x30);
  }
  return haveDigit ? value * sign : fallback;
}

export function rgb565ToRgba(pixel: number, out: Uint8ClampedArray, i: number): void {
  const r = ((pixel >> 11) & 0x1f) * 255 / 31;
  const g = ((pixel >> 5) & 0x3f) * 255 / 63;
  const b = (pixel & 0x1f) * 255 / 31;
  out[i] = r;
  out[i + 1] = g;
  out[i + 2] = b;
  out[i + 3] = pixel === 0 ? 0 : 255;
}
