/**
 * WT string codec. Live CBE wire is GBK; Chromium supports `TextDecoder('gbk')`.
 * Prefer UTF-8 only when the payload is unambiguously UTF-8 (round-trip).
 *
 * Encode: mapID/scene must be GBK on the wire so Windows fopen hits JHOnlineData.
 * We encode via a compact Unicode→GBK table built from TextDecoder (no iconv-lite /
 * Buffer — those break the Vite browser bundle).
 */

let gbkEncodeTable: Map<number, number> | null = null;

function buildGbkEncodeTable(): Map<number, number> {
  const table = new Map<number, number>();
  const dec = new TextDecoder("gbk");
  // GBK single-byte 0x00-0x7f
  for (let b = 0; b < 0x80; b++) table.set(b, b);
  // GBK double-byte lead 0x81-0xfe, trail 0x40-0xfe excluding 0x7f
  const bytes = new Uint8Array(2);
  for (let lead = 0x81; lead <= 0xfe; lead++) {
    bytes[0] = lead;
    for (let trail = 0x40; trail <= 0xfe; trail++) {
      if (trail === 0x7f) continue;
      bytes[1] = trail;
      const ch = dec.decode(bytes);
      if (ch.length !== 1) continue;
      const cp = ch.codePointAt(0)!;
      // Prefer first mapping when ambiguities exist.
      if (!table.has(cp)) table.set(cp, (lead << 8) | trail);
    }
  }
  return table;
}

function ensureGbkEncodeTable(): Map<number, number> {
  if (!gbkEncodeTable) gbkEncodeTable = buildGbkEncodeTable();
  return gbkEncodeTable;
}

/** Decode GBK/GB18030 wire strings; prefer UTF-8 only when bytes are valid UTF-8. */
export function decodeGbk(bytes: Uint8Array): string {
  if (bytes.length === 0) return "";
  let end = bytes.length;
  while (end > 0 && bytes[end - 1] === 0) end--;
  const slice = bytes.subarray(0, end);
  if (!slice.some((b) => b >= 0x80)) {
    return new TextDecoder("utf-8").decode(slice);
  }

  const asUtf8 = new TextDecoder("utf-8", { fatal: false }).decode(slice);
  const utf8Bytes = new TextEncoder().encode(asUtf8);
  const utf8RoundTrip =
    utf8Bytes.length === slice.length &&
    utf8Bytes.every((b, i) => b === slice[i]);
  let asGbk = asUtf8;
  try {
    asGbk = new TextDecoder("gbk").decode(slice);
  } catch {
    try {
      asGbk = new TextDecoder("gb18030").decode(slice);
    } catch {
      /* keep asUtf8 */
    }
  }

  const utf8HasCjk = /[\u4e00-\u9fff]/.test(asUtf8);
  const gbkHasCjk = /[\u4e00-\u9fff]/.test(asGbk);
  /*
   * Blind "UTF-8 if round-trip" is wrong for several GBK pairs that are also
   * valid UTF-8 2-byte sequences.  Example: 泰山 GBK CC A9 C9 BD round-trips
   * as U+0329/U+027D; encodeURIComponent then requests %CC%A9%C9%BD and Vite
   * 404s against the real Unicode filename 07泰山_02.sce.
   */
  if (gbkHasCjk && !utf8HasCjk) return asGbk;
  if (utf8HasCjk && utf8RoundTrip) return asUtf8;
  if (gbkHasCjk) return asGbk;
  if (utf8RoundTrip) return asUtf8;
  return asGbk;
}

/** Encode ASCII (or already-GBK bytes). Non-ASCII codepoints become `?`. */
export function encodeAscii(text: string): Uint8Array {
  const out = new Uint8Array(text.length);
  for (let i = 0; i < text.length; i++) {
    const c = text.charCodeAt(i);
    out[i] = c < 128 ? c : 0x3f;
  }
  return out;
}

/**
 * Encode text for WT string fields (GBK). ASCII stays single-byte.
 * Unknown codepoints become `?` (should not happen for SCE map names).
 */
export function encodeWireString(text: string): Uint8Array {
  let asciiOnly = true;
  for (let i = 0; i < text.length; i++) {
    if (text.charCodeAt(i) > 127) {
      asciiOnly = false;
      break;
    }
  }
  if (asciiOnly) return encodeAscii(text);

  const table = ensureGbkEncodeTable();
  const out: number[] = [];
  for (const ch of text) {
    const cp = ch.codePointAt(0)!;
    if (cp < 0x80) {
      out.push(cp);
      continue;
    }
    const packed = table.get(cp);
    if (packed == null) {
      out.push(0x3f);
      continue;
    }
    if (packed <= 0xff) out.push(packed);
    else {
      out.push((packed >> 8) & 0xff);
      out.push(packed & 0xff);
    }
  }
  return new Uint8Array(out);
}
