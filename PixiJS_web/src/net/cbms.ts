/**
 * CBMS/CBMR framing stub — see server/README.md and mock_server_transport.c.
 * Browser cannot open raw TCP; use scripts/ws-gateway.mjs then NetClient.
 */

export const CBMS_FLAG_PING = 0x1;
export const CBMS_FLAG_SCENE_POLL = 0x2;
export const CBMS_FLAG_DISCONNECT = 0x4;

export const CBMR_FLAG_CLOSE_AFTER_DATA = 0x1;
export const CBMR_FLAG_HAS_FOLLOWUP = 0x2;

function writeU32LE(buf: Uint8Array, off: number, v: number): void {
  buf[off] = v & 0xff;
  buf[off + 1] = (v >>> 8) & 0xff;
  buf[off + 2] = (v >>> 16) & 0xff;
  buf[off + 3] = (v >>> 24) & 0xff;
}

function readU32LE(buf: Uint8Array, off: number): number {
  return (
    (buf[off]! |
      (buf[off + 1]! << 8) |
      (buf[off + 2]! << 16) |
      (buf[off + 3]! << 24)) >>>
    0
  );
}

export type CbmrFrame = {
  flags: number;
  eventType: number;
  body: Uint8Array;
};

export function encodeCbms(
  flags: number,
  meta: Uint8Array,
  wtBody: Uint8Array,
): Uint8Array {
  const payloadLen = meta.length + wtBody.length;
  const out = new Uint8Array(20 + payloadLen);
  out[0] = 0x43;
  out[1] = 0x42;
  out[2] = 0x4d;
  out[3] = 0x53; // CBMS
  writeU32LE(out, 4, 1);
  writeU32LE(out, 8, flags);
  writeU32LE(out, 12, payloadLen);
  writeU32LE(out, 16, meta.length);
  out.set(meta, 20);
  out.set(wtBody, 20 + meta.length);
  return out;
}

export function encodeRequestMeta(clientId: number, zoneName = ""): Uint8Array {
  const nameBytes = zoneName
    ? new TextEncoder().encode(zoneName.slice(0, 31))
    : new Uint8Array(0);
  const out = new Uint8Array(4 + (nameBytes.length ? 1 + nameBytes.length : 0));
  writeU32LE(out, 0, clientId >>> 0);
  if (nameBytes.length) {
    out[4] = nameBytes.length;
    out.set(nameBytes, 5);
  }
  return out;
}

export function decodeCbmr(buf: Uint8Array): CbmrFrame {
  if (buf.length < 20) throw new Error("cbmr: short");
  if (
    buf[0] !== 0x43 ||
    buf[1] !== 0x42 ||
    buf[2] !== 0x4d ||
    buf[3] !== 0x52
  ) {
    throw new Error("cbmr: bad magic");
  }
  const flags = readU32LE(buf, 8);
  const bodyLen = readU32LE(buf, 12);
  const eventType = readU32LE(buf, 16);
  if (20 + bodyLen > buf.length) throw new Error("cbmr: truncated body");
  return { flags, eventType, body: buf.subarray(20, 20 + bodyLen) };
}
