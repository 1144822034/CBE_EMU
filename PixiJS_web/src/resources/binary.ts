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

export function rgb565ToRgba(pixel: number, out: Uint8ClampedArray, i: number): void {
  const r = ((pixel >> 11) & 0x1f) * 255 / 31;
  const g = ((pixel >> 5) & 0x3f) * 255 / 63;
  const b = (pixel & 0x1f) * 255 / 31;
  out[i] = r;
  out[i + 1] = g;
  out[i + 2] = b;
  out[i + 3] = pixel === 0 ? 0 : 255;
}
