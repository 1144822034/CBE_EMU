/**
 * Pixi-only 物品竞拍 protocol (WT kind=42).
 * subtypes: 1 status, 2 list, 3 mine, 4 create, 5 bid, 6 cancel, 7 claim, 8 event.
 */
import {
  SeqReader,
  WtBuilder,
  decodeTaggedNumber,
  decodeWtPacket,
  findField,
  type WtObject,
  type WtPacket,
} from "./wt";

function fieldU32(obj: WtObject, name: string): number {
  const raw = findField(obj, name);
  if (!raw) return 0;
  try {
    const n = decodeTaggedNumber(raw);
    return n == null ? 0 : n >>> 0;
  } catch {
    return 0;
  }
}

function fieldStr(obj: WtObject, name: string): string {
  const raw = findField(obj, name);
  if (!raw) return "";
  try {
    return new SeqReader(raw).string().replace(/\0+$/g, "");
  } catch {
    return "";
  }
}

export const AUCTION_WT_KIND = 42;

export const AUCTION_DURATION_LABELS = ["1时", "6时", "12时", "24时"] as const;

export type AuctionStatus = {
  enabled: boolean;
  blink: boolean;
  pendingClaims: number;
  feePercents: number[];
  minBidStep: number;
  currencyMask: number;
};

export type AuctionListing = {
  listingId: number;
  sellerRoleId: number;
  sellerName: string;
  itemId: number;
  itemCount: number;
  enhanceLevel: number;
  startPrice: number;
  currentPrice: number;
  currency: number; // 0铜币 1酷宝
  feePercent: number;
  durationTier: number;
  remainSec: number;
  highBidderRoleId: number;
  itemName: string;
};

export type AuctionOpResult = {
  result: number;
  listingId: number;
  message: string;
};

export type AuctionClaimResult = {
  result: number;
  claimed: number;
  failed: number;
  pendingClaims: number;
  blink: boolean;
};

export type AuctionEvent = {
  listingId: number;
  eventKind: number;
  amount: number;
  blink: boolean;
};

export function buildAuctionStatusRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, AUCTION_WT_KIND, 1);
  return b.finish();
}

export function buildAuctionListRequest(pageIndex = 0): Uint8Array {
  const b = new WtBuilder();
  b.object(1, AUCTION_WT_KIND, 2).u32("pageIndex", pageIndex >>> 0);
  return b.finish();
}

export function buildAuctionMineRequest(pageIndex = 0): Uint8Array {
  const b = new WtBuilder();
  b.object(1, AUCTION_WT_KIND, 3).u32("pageIndex", pageIndex >>> 0);
  return b.finish();
}

export function buildAuctionCreateRequest(opts: {
  bagSeq: number;
  count: number;
  startPrice: number;
  currency: number;
  durationTier: number;
}): Uint8Array {
  const b = new WtBuilder();
  b.object(1, AUCTION_WT_KIND, 4)
    .u32("bagSeq", opts.bagSeq >>> 0)
    .u32("count", opts.count >>> 0)
    .u32("startPrice", opts.startPrice >>> 0)
    .u32("currency", opts.currency >>> 0)
    .u32("durationTier", opts.durationTier >>> 0);
  return b.finish();
}

export function buildAuctionBidRequest(
  listingId: number,
  amount: number,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, AUCTION_WT_KIND, 5)
    .u32("listingId", listingId >>> 0)
    .u32("amount", amount >>> 0);
  return b.finish();
}

export function buildAuctionCancelRequest(listingId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, AUCTION_WT_KIND, 6).u32("listingId", listingId >>> 0);
  return b.finish();
}

export function buildAuctionClaimRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, AUCTION_WT_KIND, 7);
  return b.finish();
}

function packetFromBody(body: Uint8Array): WtPacket | null {
  try {
    return decodeWtPacket(body);
  } catch {
    return null;
  }
}

function parseListings(obj: WtObject): AuctionListing[] {
  const count = fieldU32(obj, "count");
  const raw = findField(obj, "rows");
  const rows: AuctionListing[] = [];
  if (!raw) return rows;
  const r = new SeqReader(raw);
  for (let i = 0; i < count && r.remaining() > 8; i++) {
    try {
      rows.push({
        listingId: r.u32(),
        sellerRoleId: r.u32(),
        sellerName: r.string().replace(/\0+$/g, ""),
        itemId: r.u32(),
        itemCount: r.u32(),
        enhanceLevel: r.u32(),
        startPrice: r.u32(),
        currentPrice: r.u32(),
        currency: r.u32(),
        feePercent: r.u32(),
        durationTier: r.u32(),
        remainSec: r.u32(),
        highBidderRoleId: r.u32(),
        itemName: r.string().replace(/\0+$/g, ""),
      });
    } catch {
      break;
    }
  }
  return rows;
}

export function parseAuctionStatus(packet: WtPacket): AuctionStatus | null {
  const obj = packet.objects.find(
    (o) => o.kind === AUCTION_WT_KIND && o.subtype === 1,
  );
  if (!obj) return null;
  return {
    enabled: fieldU32(obj, "enabled") !== 0,
    blink: fieldU32(obj, "blink") !== 0,
    pendingClaims: fieldU32(obj, "pendingClaims"),
    feePercents: [
      fieldU32(obj, "fee0"),
      fieldU32(obj, "fee1"),
      fieldU32(obj, "fee2"),
      fieldU32(obj, "fee3"),
    ],
    minBidStep: fieldU32(obj, "minBidStep"),
    currencyMask: fieldU32(obj, "currencyMask"),
  };
}

export function parseAuctionList(
  packet: WtPacket,
  subtype: 2 | 3,
): { result: number; rows: AuctionListing[]; pageIndex: number } | null {
  const obj = packet.objects.find(
    (o) => o.kind === AUCTION_WT_KIND && o.subtype === subtype,
  );
  if (!obj) return null;
  return {
    result: fieldU32(obj, "result"),
    rows: parseListings(obj),
    pageIndex: fieldU32(obj, "pageIndex"),
  };
}

export function parseAuctionOpResult(
  packet: WtPacket,
  subtype: number,
): AuctionOpResult | null {
  const obj = packet.objects.find(
    (o) => o.kind === AUCTION_WT_KIND && o.subtype === subtype,
  );
  if (!obj) return null;
  return {
    result: fieldU32(obj, "result"),
    listingId: fieldU32(obj, "listingId"),
    message: fieldStr(obj, "message"),
  };
}

export function parseAuctionClaim(packet: WtPacket): AuctionClaimResult | null {
  const obj = packet.objects.find(
    (o) => o.kind === AUCTION_WT_KIND && o.subtype === 7,
  );
  if (!obj) return null;
  return {
    result: fieldU32(obj, "result"),
    claimed: fieldU32(obj, "claimed"),
    failed: fieldU32(obj, "failed"),
    pendingClaims: fieldU32(obj, "pendingClaims"),
    blink: fieldU32(obj, "blink") !== 0,
  };
}

export function parseAuctionEvent(packet: WtPacket): AuctionEvent | null {
  const obj = packet.objects.find(
    (o) => o.kind === AUCTION_WT_KIND && o.subtype === 8,
  );
  if (!obj) return null;
  return {
    listingId: fieldU32(obj, "listingId"),
    eventKind: fieldU32(obj, "eventKind"),
    amount: fieldU32(obj, "amount"),
    blink: fieldU32(obj, "blink") !== 0,
  };
}

export function currencyLabel(currency: number): string {
  return currency === 1 ? "酷宝" : "铜币";
}

export function formatRemain(sec: number): string {
  const s = Math.max(0, sec | 0);
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  if (h > 0) return `${h}时${m}分`;
  if (m > 0) return `${m}分`;
  return `${s}秒`;
}
