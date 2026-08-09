/**
 * WT packet codec — mirrors mock_server_core.c put_object_* / finish_wt_*.
 */

import { decodeGbk, encodeWireString } from "./gbk";

export type WtField = {
  name: string;
  raw: Uint8Array;
};

export type WtObject = {
  major: number;
  kind: number;
  subtype: number;
  fields: WtField[];
  payload: Uint8Array;
};

export type WtPacket = {
  objects: WtObject[];
  raw: Uint8Array;
};

function readU16BE(buf: Uint8Array, off: number): number {
  return ((buf[off]! << 8) | buf[off + 1]!) >>> 0;
}

function writeU16BE(buf: Uint8Array, off: number, v: number): void {
  buf[off] = (v >>> 8) & 0xff;
  buf[off + 1] = v & 0xff;
}

function writeU32BE(buf: Uint8Array, off: number, v: number): void {
  buf[off] = (v >>> 24) & 0xff;
  buf[off + 1] = (v >>> 16) & 0xff;
  buf[off + 2] = (v >>> 8) & 0xff;
  buf[off + 3] = v & 0xff;
}

export function decodeWtPacket(buf: Uint8Array): WtPacket {
  if (buf.length < 4 || buf[0] !== 0x57 || buf[1] !== 0x54) {
    throw new Error("wt: bad magic");
  }
  const total = readU16BE(buf, 2);
  if (total > buf.length) throw new Error("wt: truncated");
  if (total < 4) throw new Error("wt: short");

  // Response wire: count @4 + 6-byte object headers (pad@+3, BE16 len@+4).
  // Request wire: no count; 5-byte headers (BE16 len@+3). See WtBuilder.encode.
  const asResponse =
    total >= 11 &&
    buf[8] === 0 &&
    readU16BE(buf, 9) >= 6 &&
    5 + readU16BE(buf, 9) <= total;

  const objects: WtObject[] = [];
  if (asResponse) {
    const objectCount = buf[4]!;
    let pos = 5;
    for (let i = 0; i < objectCount; i++) {
      if (pos + 6 > total) throw new Error("wt: object header");
      const major = buf[pos]!;
      const kind = buf[pos + 1]!;
      const subtype = buf[pos + 2]!;
      const objectLen = readU16BE(buf, pos + 4);
      if (objectLen < 6 || pos + objectLen > total) {
        throw new Error("wt: bad objectLen");
      }
      const payload = buf.subarray(pos + 6, pos + objectLen);
      objects.push({
        major,
        kind,
        subtype,
        fields: decodeObjectFields(payload),
        payload,
      });
      pos += objectLen;
    }
  } else {
    let pos = 4;
    while (pos + 5 <= total) {
      const major = buf[pos]!;
      const kind = buf[pos + 1]!;
      const subtype = buf[pos + 2]!;
      const objectLen = readU16BE(buf, pos + 3);
      if (objectLen < 5 || pos + objectLen > total) {
        throw new Error("wt: bad request objectLen");
      }
      const payload = buf.subarray(pos + 5, pos + objectLen);
      objects.push({
        major,
        kind,
        subtype,
        fields: decodeObjectFields(payload),
        payload,
      });
      pos += objectLen;
    }
  }
  return { objects, raw: buf.subarray(0, total) };
}

function decodeObjectFields(payload: Uint8Array): WtField[] {
  const fields: WtField[] = [];
  let pos = 0;
  while (pos < payload.length) {
    const nameLen = payload[pos++]!;
    if (pos + nameLen + 2 > payload.length) break;
    const nameBytes = payload.subarray(pos, pos + nameLen);
    pos += nameLen;
    const name = new TextDecoder("ascii").decode(nameBytes);
    const valueLen = readU16BE(payload, pos);
    pos += 2;
    if (pos + valueLen > payload.length) break;
    fields.push({ name, raw: payload.subarray(pos, pos + valueLen) });
    pos += valueLen;
  }
  return fields;
}

export function findField(obj: WtObject, name: string): Uint8Array | null {
  const hit = obj.fields.find((f) => f.name === name);
  return hit ? hit.raw : null;
}

/** Decode put_object_u8 / ascii_digit / u16 / u32 tagged values. */
export function decodeTaggedNumber(raw: Uint8Array): number | null {
  if (raw.length >= 3 && raw[0] === 0x00 && raw[1] === 0x01) return raw[2]!;
  if (raw.length >= 4 && raw[0] === 0x00 && raw[1] === 0x02) {
    return (raw[2]! << 8) | raw[3]!;
  }
  if (raw.length >= 6 && raw[0] === 0x00 && raw[1] === 0x04) {
    return (
      ((raw[2]! << 24) | (raw[3]! << 16) | (raw[4]! << 8) | raw[5]!) >>> 0
    );
  }
  // ascii digit result '1'..'9'
  if (raw.length >= 3 && raw[0] === 0x00 && raw[1] === 0x01) {
    const c = raw[2]!;
    if (c >= 0x30 && c <= 0x39) return c - 0x30;
  }
  return null;
}

/** put_object_string / blob: often 0x00 0x10 + BE16 len + bytes, OR BE16 len + bytes. */
export function decodeFieldString(raw: Uint8Array): string {
  if (raw.length >= 3 && raw[0] === 0x00 && raw[1] === 0x10) {
    const len = readU16BE(raw, 2);
    return decodeGbk(raw.subarray(4, 4 + len));
  }
  if (raw.length >= 2) {
    // blob form: BE16 dataLen + bytes (put_object_blob / put_object_string)
    const len = readU16BE(raw, 0);
    if (2 + len <= raw.length) return decodeGbk(raw.subarray(2, 2 + len));
  }
  return decodeGbk(raw);
}

export function decodeAsciiDigitResult(raw: Uint8Array): number | null {
  if (raw.length >= 3 && raw[0] === 0x00 && raw[1] === 0x01) {
    const c = raw[2]!;
    if (c >= 0x30 && c <= 0x39) return c - 0x30;
    return c;
  }
  return decodeTaggedNumber(raw);
}

/* ---------- builders ---------- */

type FieldWriter = { name: string; value: Uint8Array };

export class WtBuilder {
  private objects: { major: number; kind: number; subtype: number; fields: FieldWriter[] }[] =
    [];

  object(major: number, kind: number, subtype: number): WtObjectBuilder {
    const fields: FieldWriter[] = [];
    this.objects.push({ major, kind, subtype, fields });
    return new WtObjectBuilder(fields);
  }

  /**
   * Outbound request wire (CBE / PHP regression):
   * `WT` + BE16(total) + objects; each object is
   * major|kind|subtype|BE16(5+fields)|fields — no object-count byte.
   * Matches `vm_net_mock_next_request_object` / `get_wt_header_kind_subtype`.
   */
  finish(): Uint8Array {
    return this.encode(false);
  }

  /**
   * Synthetic response wire (mock_server finish_wt_*):
   * `WT` + BE16(total) + objectCount + objects with 6-byte headers
   * (pad byte + BE16 len at +4). Used by demoBuild* and client decodeWtPacket.
   */
  finishResponse(): Uint8Array {
    return this.encode(true);
  }

  private encode(asResponse: boolean): Uint8Array {
    const parts: Uint8Array[] = [];
    for (const obj of this.objects) {
      const fieldBytes: Uint8Array[] = [];
      let fieldLen = 0;
      for (const f of obj.fields) {
        const nameBytes = encodeAscii(f.name);
        const chunk = new Uint8Array(1 + nameBytes.length + 2 + f.value.length);
        chunk[0] = nameBytes.length;
        chunk.set(nameBytes, 1);
        writeU16BE(chunk, 1 + nameBytes.length, f.value.length);
        chunk.set(f.value, 1 + nameBytes.length + 2);
        fieldBytes.push(chunk);
        fieldLen += chunk.length;
      }
      if (asResponse) {
        const objectLen = 6 + fieldLen;
        const hdr = new Uint8Array(objectLen);
        hdr[0] = obj.major;
        hdr[1] = obj.kind;
        hdr[2] = obj.subtype;
        hdr[3] = 0;
        writeU16BE(hdr, 4, objectLen);
        let p = 6;
        for (const fb of fieldBytes) {
          hdr.set(fb, p);
          p += fb.length;
        }
        parts.push(hdr);
      } else {
        const objectLen = 5 + fieldLen;
        const hdr = new Uint8Array(objectLen);
        hdr[0] = obj.major;
        hdr[1] = obj.kind;
        hdr[2] = obj.subtype;
        writeU16BE(hdr, 3, objectLen);
        let p = 5;
        for (const fb of fieldBytes) {
          hdr.set(fb, p);
          p += fb.length;
        }
        parts.push(hdr);
      }
    }
    let total = asResponse ? 5 : 4;
    for (const p of parts) total += p.length;
    const out = new Uint8Array(total);
    out[0] = 0x57;
    out[1] = 0x54;
    writeU16BE(out, 2, total);
    let pos: number;
    if (asResponse) {
      out[4] = this.objects.length;
      pos = 5;
    } else {
      pos = 4;
    }
    for (const p of parts) {
      out.set(p, pos);
      pos += p.length;
    }
    return out;
  }
}

function encodeAscii(text: string): Uint8Array {
  const out = new Uint8Array(text.length);
  for (let i = 0; i < text.length; i++) out[i] = text.charCodeAt(i) & 0xff;
  return out;
}

export class WtObjectBuilder {
  constructor(private readonly fields: FieldWriter[]) {}

  u8(name: string, value: number): this {
    this.fields.push({ name, value: new Uint8Array([0x00, 0x01, value & 0xff]) });
    return this;
  }

  /** result as ascii digit (mock put_object_ascii_digit). */
  asciiDigit(name: string, digit: number): this {
    return this.u8(name, 0x30 + (digit & 0xf));
  }

  u16(name: string, value: number): this {
    const v = value & 0xffff;
    this.fields.push({
      name,
      value: new Uint8Array([0x00, 0x02, (v >>> 8) & 0xff, v & 0xff]),
    });
    return this;
  }

  u32(name: string, value: number): this {
    const v = value >>> 0;
    const valueBytes = new Uint8Array(6);
    valueBytes[0] = 0x00;
    valueBytes[1] = 0x04;
    writeU32BE(valueBytes, 2, v);
    this.fields.push({ name, value: valueBytes });
    return this;
  }

  /** put_object_string → blob with BE16 len + bytes (no type tag). */
  string(name: string, text: string): this {
    const data = encodeWireString(text);
    const value = new Uint8Array(2 + data.length);
    writeU16BE(value, 0, data.length);
    value.set(data, 2);
    this.fields.push({ name, value });
    return this;
  }

  /**
   * put_object_blob: field value = BE16(dataLen) + bytes.
   * Required for CBE moveinfo uploads (server get_object_blob_field).
   */
  blob(name: string, data: Uint8Array): this {
    const value = new Uint8Array(2 + data.length);
    writeU16BE(value, 0, data.length);
    value.set(data, 2);
    this.fields.push({ name, value });
    return this;
  }

  raw(name: string, data: Uint8Array): this {
    this.fields.push({ name, value: data });
    return this;
  }
}

/* ---------- seq tagged blob readers (actorinfo / npcinfo / battleinfo) ---------- */

export class SeqReader {
  pos = 0;
  constructor(public readonly buf: Uint8Array) {}

  remaining(): number {
    return this.buf.length - this.pos;
  }

  u8(): number {
    if (this.buf[this.pos] === 0x00 && this.buf[this.pos + 1] === 0x01) {
      const v = this.buf[this.pos + 2]!;
      this.pos += 3;
      return v;
    }
    return this.buf[this.pos++]!;
  }

  u32(): number {
    if (
      this.pos + 6 <= this.buf.length &&
      this.buf[this.pos] === 0x00 &&
      this.buf[this.pos + 1] === 0x04
    ) {
      const v =
        ((this.buf[this.pos + 2]! << 24) |
          (this.buf[this.pos + 3]! << 16) |
          (this.buf[this.pos + 4]! << 8) |
          this.buf[this.pos + 5]!) >>>
        0;
      this.pos += 6;
      return v;
    }
    throw new Error("seq: expected tagged u32");
  }

  i16(): number {
    if (
      this.pos + 4 <= this.buf.length &&
      this.buf[this.pos] === 0x00 &&
      this.buf[this.pos + 1] === 0x02
    ) {
      const v = (this.buf[this.pos + 2]! << 8) | this.buf[this.pos + 3]!;
      this.pos += 4;
      return v;
    }
    throw new Error("seq: expected tagged i16");
  }

  string(): string {
    if (this.pos + 2 > this.buf.length) return "";
    const len = readU16BE(this.buf, this.pos);
    this.pos += 2;
    const bytes = this.buf.subarray(this.pos, this.pos + len);
    this.pos += len;
    return decodeGbk(bytes);
  }
}

export class SeqWriter {
  private chunks: number[] = [];

  u8(v: number): this {
    this.chunks.push(0, 1, v & 0xff);
    return this;
  }

  u32(v: number): this {
    const x = v >>> 0;
    this.chunks.push(0, 4, (x >>> 24) & 0xff, (x >>> 16) & 0xff, (x >>> 8) & 0xff, x & 0xff);
    return this;
  }

  i16(v: number): this {
    const x = v & 0xffff;
    this.chunks.push(0, 2, (x >>> 8) & 0xff, x & 0xff);
    return this;
  }

  string(text: string): this {
    const data = encodeWireString(text);
    const len = data.length + 1; // include NUL like seq_put_string
    this.chunks.push((len >>> 8) & 0xff, len & 0xff);
    for (let i = 0; i < data.length; i++) this.chunks.push(data[i]!);
    this.chunks.push(0);
    return this;
  }

  finish(): Uint8Array {
    return Uint8Array.from(this.chunks);
  }
}
