/**
 * Pixi-only 物品竞拍 protocol (WT kind=42).
 * subtypes: 1 status, 2 list, 3 mine, 4 create, 5 bid, 6 cancel, 7 claim,
 * 8 event, 9 buyout, 10 claims-list.
 */
import {
  SeqReader,
  WtBuilder,
  decodeTaggedNumber,
  findField,
  type WtObject,
  type WtPacket,
} from "./wt";
import {
  readItemCommonExtra,
  type BagEnhanceAttr,
} from "./protocolExtras";

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

/** 与 mock_server_auction.c VM_MOCK_AUCTION_PAGE_SIZE 一致. */
export const AUCTION_PAGE_SIZE = 12;

export const AUCTION_DURATION_LABELS = ["1时", "6时", "12时", "24时"] as const;

/** claim.kind: 1退物 2卖家拍金 3出价退款 4赢家得物 */
export const AUCTION_CLAIM_KIND = {
  ITEM_RETURN: 1,
  SELLER_PAYOUT: 2,
  BID_REFUND: 3,
  WINNER_ITEM: 4,
} as const;

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
  /** Bag-identical common-extra attrs (wash bit7); for equip detail. */
  enhanceAttrs?: BagEnhanceAttr[];
  startPrice: number;
  currentPrice: number;
  /** 0 = 无一口价 */
  buyoutPrice: number;
  currency: number; // 0铜币 1酷宝
  feePercent: number;
  durationTier: number;
  remainSec: number;
  highBidderRoleId: number;
  itemName: string;
};

export type AuctionClaimRow = {
  claimId: number;
  kind: number;
  listingId: number;
  itemId: number;
  itemCount: number;
  enhanceLevel: number;
  moneyAmount: number;
  wcoinAmount: number;
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
  buyoutPrice: number;
  currency: number;
  durationTier: number;
}): Uint8Array {
  const b = new WtBuilder();
  b.object(1, AUCTION_WT_KIND, 4)
    .u32("bagSeq", opts.bagSeq >>> 0)
    .u32("count", opts.count >>> 0)
    .u32("startPrice", opts.startPrice >>> 0)
    .u32("buyoutPrice", opts.buyoutPrice >>> 0)
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

export function buildAuctionBuyoutRequest(listingId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, AUCTION_WT_KIND, 9).u32("listingId", listingId >>> 0);
  return b.finish();
}

export function buildAuctionClaimsListRequest(pageIndex = 0): Uint8Array {
  const b = new WtBuilder();
  b.object(1, AUCTION_WT_KIND, 10).u32("pageIndex", pageIndex >>> 0);
  return b.finish();
}

function parseListings(obj: WtObject): AuctionListing[] {
  const count = fieldU32(obj, "count");
  const raw = findField(obj, "rows");
  const rows: AuctionListing[] = [];
  if (!raw) return rows;
  const r = new SeqReader(raw);
  for (let i = 0; i < count && r.remaining() > 8; i++) {
    try {
      const listingId = r.u32();
      const sellerRoleId = r.u32();
      const sellerName = r.string().replace(/\0+$/g, "");
      const itemId = r.u32();
      const itemCount = r.u32();
      const enhanceLevel = r.u32();
      /* After enhanceLevel: bag common-extra (milestones + wash). */
      const extra = readItemCommonExtra(r);
      rows.push({
        listingId,
        sellerRoleId,
        sellerName,
        itemId,
        itemCount,
        enhanceLevel,
        ...(extra.attrs.length ? { enhanceAttrs: extra.attrs } : {}),
        startPrice: r.u32(),
        currentPrice: r.u32(),
        buyoutPrice: r.u32(),
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

function parseClaimRows(obj: WtObject): AuctionClaimRow[] {
  const count = fieldU32(obj, "count");
  const raw = findField(obj, "rows");
  const rows: AuctionClaimRow[] = [];
  if (!raw) return rows;
  const r = new SeqReader(raw);
  for (let i = 0; i < count && r.remaining() > 8; i++) {
    try {
      rows.push({
        claimId: r.u32(),
        kind: r.u32(),
        listingId: r.u32(),
        itemId: r.u32(),
        itemCount: r.u32(),
        enhanceLevel: r.u32(),
        moneyAmount: r.u32(),
        wcoinAmount: r.u32(),
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
): {
  result: number;
  rows: AuctionListing[];
  pageIndex: number;
  pageSize: number;
  hasMore: boolean;
} | null {
  const obj = packet.objects.find(
    (o) => o.kind === AUCTION_WT_KIND && o.subtype === subtype,
  );
  if (!obj) return null;
  const pageSize = fieldU32(obj, "pageSize") || AUCTION_PAGE_SIZE;
  const rows = parseListings(obj);
  const hasMoreField = findField(obj, "hasMore");
  const hasMore =
    hasMoreField != null
      ? fieldU32(obj, "hasMore") !== 0
      : rows.length >= pageSize;
  return {
    result: fieldU32(obj, "result"),
    rows,
    pageIndex: fieldU32(obj, "pageIndex"),
    pageSize,
    hasMore,
  };
}

export function parseAuctionClaimsList(packet: WtPacket): {
  result: number;
  rows: AuctionClaimRow[];
  pageIndex: number;
  pageSize: number;
  hasMore: boolean;
  pendingClaims: number;
} | null {
  const obj = packet.objects.find(
    (o) => o.kind === AUCTION_WT_KIND && o.subtype === 10,
  );
  if (!obj) return null;
  const pageSize = fieldU32(obj, "pageSize") || AUCTION_PAGE_SIZE;
  const rows = parseClaimRows(obj);
  const hasMoreField = findField(obj, "hasMore");
  const hasMore =
    hasMoreField != null
      ? fieldU32(obj, "hasMore") !== 0
      : rows.length >= pageSize;
  return {
    result: fieldU32(obj, "result"),
    rows,
    pageIndex: fieldU32(obj, "pageIndex"),
    pageSize,
    hasMore,
    pendingClaims: fieldU32(obj, "pendingClaims"),
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

/** 1金 = 10000铜（与背包金/银/铜、财富榜同源）. */
export const COPPER_PER_GOLD = 10000;

export function copperToGold(copper: number): number {
  return Math.floor((copper >>> 0) / COPPER_PER_GOLD);
}

export function goldToCopper(gold: number): number {
  return ((gold >>> 0) * COPPER_PER_GOLD) >>> 0;
}

/** 设价单位：铜币档按「金」输入，酷宝仍按酷宝. */
export function auctionPriceUnit(currency: number): string {
  return currency === 1 ? "酷宝" : "金";
}

/** 列表/详情金额展示：铜→金，酷宝原样. */
export function formatAuctionPrice(amount: number, currency: number): string {
  if (currency === 1) return `${amount >>> 0}酷宝`;
  return `${copperToGold(amount)}金`;
}

/** 手续费展示：不足1金时回退银/铜，避免显示「0金」. */
export function formatAuctionFee(amount: number, currency: number): string {
  if (currency === 1) return `${amount >>> 0}酷宝`;
  const c = amount >>> 0;
  const g = copperToGold(c);
  if (g > 0) return `${g}金`;
  const yin = Math.floor(c / 100);
  if (yin > 0) return `${yin}银`;
  return `${c}铜`;
}

export function formatRemain(sec: number): string {
  const s = Math.max(0, sec | 0);
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  if (h > 0) return `${h}时${m}分`;
  if (m > 0) return `${m}分`;
  return `${s}秒`;
}

export function auctionClaimKindLabel(kind: number): string {
  switch (kind) {
    case AUCTION_CLAIM_KIND.ITEM_RETURN:
      return "退物";
    case AUCTION_CLAIM_KIND.SELLER_PAYOUT:
      return "拍金";
    case AUCTION_CLAIM_KIND.BID_REFUND:
      return "退价";
    case AUCTION_CLAIM_KIND.WINNER_ITEM:
      return "得物";
    default:
      return `领${kind}`;
  }
}

export function formatAuctionClaimRow(row: AuctionClaimRow): string {
  const tag = auctionClaimKindLabel(row.kind);
  if (
    row.kind === AUCTION_CLAIM_KIND.ITEM_RETURN ||
    row.kind === AUCTION_CLAIM_KIND.WINNER_ITEM
  ) {
    const name = row.itemName || `物品${row.itemId}`;
    const en = row.enhanceLevel > 0 ? `+${row.enhanceLevel}` : "";
    // 802/803：itemCount 是储量，可见件数恒 1
    if (row.itemId === 802 || row.itemId === 803) {
      return `${tag} ${name}${en}`;
    }
    return `${tag} ${name}${en}x${row.itemCount}`;
  }
  if (row.moneyAmount > 0) {
    return `${tag} ${formatAuctionPrice(row.moneyAmount, 0)}`;
  }
  if (row.wcoinAmount > 0) {
    return `${tag} ${formatAuctionPrice(row.wcoinAmount, 1)}`;
  }
  return `${tag} #${row.claimId}`;
}
