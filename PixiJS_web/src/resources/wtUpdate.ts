/**
 * WT 18/7 named resource download (CBE clientmiss chunk path).
 * Used so Pixi can pull authoritative JHOnlineData .sce/.map from the live
 * mock server instead of only bundled/Vite static files.
 */
import { encodeWireString } from "../net/gbk";
import { decodeWtPacket, findField, WtBuilder, type WtPacket } from "../net/wt";

export type ResourceExchange = (
  flags: number,
  wtBody: Uint8Array,
) => Promise<Uint8Array[]>;

const CHUNK = 0x1000;

function fnvHashBytes(hash: number, data: Uint8Array): number {
  let h = hash >>> 0;
  if (h === 0) h = 2166136261;
  for (let i = 0; i < data.length; i++) {
    h ^= data[i]!;
    h = Math.imul(h, 16777619) >>> 0;
  }
  return h >>> 0;
}

/** Match mock `vm_net_mock_update_named_content_version` (wire name + file). */
export function namedResourceContentVersion(
  wireName: string,
  fileBytes: Uint8Array,
): number {
  const nameBytes = encodeWireString(wireName);
  let hash = fnvHashBytes(2166136261, nameBytes);
  hash = fnvHashBytes(hash, fileBytes);
  return ((hash % 65535) + 1) & 0xffff;
}

/** WT18/7 leaf: keep .sce/.map so SCE and MAP do not share one wire name. */
export function resourceWireName(assetName: string): string {
  const leaf = assetName.split(/[/\\]/).pop() || assetName;
  return leaf;
}

export function wantsLiveResourceSync(assetName: string): boolean {
  const leaf = assetName.split(/[/\\]/).pop() || assetName;
  const lower = leaf.toLowerCase();
  if (lower.endsWith(".sce") || lower.endsWith(".map")) return true;
  if (/^e_.+\.(actor|gif)$/i.test(leaf)) return true;
  /* Bare scene keys (CBE style) — only when caller already uses extensionless. */
  if (!leaf.includes(".")) return true;
  return false;
}

/** Local PixiJS redesign PNG/JPEG must not be overwritten by stock LKV2 via WT18/7. */
export function isLocalRasterRedesign(bytes: Uint8Array | null | undefined): boolean {
  if (!bytes || bytes.length < 8) return false;
  if (bytes[0] === 0x89 && bytes[1] === 0x50 && bytes[2] === 0x4e && bytes[3] === 0x47) {
    return true;
  }
  if (bytes[0] === 0xff && bytes[1] === 0xd8 && bytes[2] === 0xff) return true;
  return false;
}

function signedByteSum(data: Uint8Array, len: number): number {
  let sum = 0;
  const n = Math.min(len, data.length);
  for (let i = 0; i < n; i++) {
    let b = data[i]!;
    if (b >= 0x80) b -= 0x100;
    sum += b;
  }
  return sum >>> 0;
}

function readU16BE(buf: Uint8Array, off: number): number {
  return ((buf[off]! << 8) | buf[off + 1]!) >>> 0;
}

function readU32BE(buf: Uint8Array, off: number): number {
  return (
    ((buf[off]! << 24) |
      (buf[off + 1]! << 16) |
      (buf[off + 2]! << 8) |
      buf[off + 3]!) >>>
    0
  );
}

function fieldU32(packet: WtPacket, name: string): number {
  const obj = packet.objects.find((o) => o.kind === 0x12 && o.subtype === 7);
  if (!obj) return 0;
  const raw = findField(obj, name);
  if (!raw || raw.length < 2) return 0;
  if (raw.length >= 6 && raw[0] === 0 && raw[1] === 4) {
    return readU32BE(raw, 2);
  }
  if (raw.length >= 3 && raw[0] === 0 && raw[1] === 1) return raw[2]!;
  if (raw.length >= 4 && raw[0] === 0 && raw[1] === 2) {
    return readU16BE(raw, 2);
  }
  if (raw.length === 2) return readU16BE(raw, 0);
  if (raw.length === 4) return readU32BE(raw, 0);
  return 0;
}

function fieldVersion(packet: WtPacket): number {
  const obj = packet.objects.find((o) => o.kind === 0x12 && o.subtype === 7);
  if (!obj) return 0;
  const raw = findField(obj, "version");
  if (!raw) return 0;
  if (raw.length >= 4 && raw[0] === 0 && raw[1] === 2) {
    return readU16BE(raw, 2);
  }
  if (raw.length >= 2) return readU16BE(raw, 0);
  return 0;
}

function fieldBlob(packet: WtPacket, name: string): Uint8Array {
  const obj = packet.objects.find((o) => o.kind === 0x12 && o.subtype === 7);
  if (!obj) return new Uint8Array(0);
  const raw = findField(obj, name);
  if (!raw || raw.length < 2) return new Uint8Array(0);
  const len = readU16BE(raw, 0);
  if (2 + len > raw.length) return new Uint8Array(0);
  return raw.subarray(2, 2 + len);
}

/** Emulator `vm_named_resource_build_chunk_request` field set. */
export function buildNamedUpdateChunkRequest(
  wireName: string,
  start: number,
  version: number,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, 18, 7)
    .string("name", wireName)
    .u8("type", 0)
    .u32("start", start >>> 0)
    .u16("version", (version || 1) & 0xffff)
    .u8("clientmiss", 1);
  return b.finish();
}

export type NamedUpdateResult =
  | { kind: "uptodate"; version: number }
  | { kind: "payload"; bytes: Uint8Array; version: number }
  | { kind: "missing" };

/**
 * Download one named leaf via WT18/7 chunks (≤0x1000). Pass local content
 * version so the server can answer totalsize=0 (keep cache).
 */
export async function downloadNamedResourceViaWt(
  exchange: ResourceExchange,
  wireName: string,
  localVersion: number,
): Promise<NamedUpdateResult> {
  let start = 0;
  let total = 0;
  let version = localVersion || 1;
  const parts: Uint8Array[] = [];

  for (;;) {
    const req = buildNamedUpdateChunkRequest(wireName, start, version);
    const bodies = await exchange(0, req);
    let packet: WtPacket | null = null;
    for (const body of bodies) {
      try {
        const p = decodeWtPacket(body);
        if (p.objects.some((o) => o.kind === 0x12 && o.subtype === 7)) {
          packet = p;
          break;
        }
      } catch {
        /* ignore non-WT */
      }
    }
    if (!packet) return { kind: "missing" };

    const totalsize = fieldU32(packet, "totalsize");
    const crc = fieldU32(packet, "crc");
    const respVer = fieldVersion(packet) || version;
    version = respVer;
    const chunk = fieldBlob(packet, "data");

    if (totalsize === 0) {
      return { kind: "uptodate", version: respVer };
    }
    if (total === 0) total = totalsize;
    if (chunk.length === 0 && start < total) return { kind: "missing" };

    parts.push(chunk);
    const assembled = concatParts(parts);
    if (signedByteSum(assembled, assembled.length) !== crc) {
      console.warn(
        `[resource] wt18/7 crc mismatch name=${wireName} start=${start}`,
      );
      return { kind: "missing" };
    }
    start = assembled.length;
    if (start >= total) {
      return {
        kind: "payload",
        bytes: assembled.subarray(0, total),
        version: respVer,
      };
    }
    if (chunk.length === 0) return { kind: "missing" };
    if (chunk.length < CHUNK && start < total) {
      /* short last chunk is ok when start reaches total next loop */
    }
  }
}

function concatParts(parts: Uint8Array[]): Uint8Array {
  let n = 0;
  for (const p of parts) n += p.length;
  const out = new Uint8Array(n);
  let o = 0;
  for (const p of parts) {
    out.set(p, o);
    o += p.length;
  }
  return out;
}
