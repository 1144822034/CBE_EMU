import { defaultLiveGatewayUrl } from "./Transport";

export type ZangbaoSealedRoleSummary = {
  roleId: number;
  name: string;
  level: number;
  job: number;
  sex: number;
};

export type ZangbaoListing = {
  listingId: number;
  sellerRoleId: number;
  sellerName: string;
  itemId: number;
  itemCount: number;
  enhanceLevel: number;
  itemInstanceId?: number;
  /** Listing enhance_wash text (DB authority; panel resolves to attrs). */
  enhanceWash?: string;
  gemReflect?: number;
  gemCombo?: number;
  gemRevive?: number;
  priceKcoin: number;
  status: number;
  sealedRole?: ZangbaoSealedRoleSummary;
};

export type ZangbaoSealItemRow = {
  slot?: number;
  itemId: number;
  count: number;
  enhanceLevel: number;
  seq?: number;
  instanceId?: number;
  enhanceWash?: string;
  gemReflect?: number;
  gemCombo?: number;
  gemRevive?: number;
};

export type ZangbaoSealDetail = {
  ok: true;
  listingId: number;
  bagSeq?: number;
  /** True when bagSeq points at an unbound blank 857. */
  blank?: boolean;
  sealedRoleId: number;
  role: {
    name: string;
    level: number;
    job: number;
    sex: number;
    title: string;
    spouse: string;
    guild: string;
    hp: number;
    hpMax: number;
    mp: number;
    mpMax: number;
    charm: number;
    strength: number;
    agility: number;
    wisdom: number;
    attack: number;
    armor: number;
    dodge: number;
    hit: number;
    crit: number;
    resist: number;
    reflectPct: number;
    comboPct: number;
    revivePct: number;
    vitality: number;
    vitalityMax: number;
    money: number;
    exp: number;
    /** 酷宝（role.wcoin）。 */
    wcoin: number;
    /** 酷币（role.kcoin）。 */
    kcoin: number;
    scene: string;
  };
  equipment: ZangbaoSealItemRow[];
  backpack: ZangbaoSealItemRow[];
  warehouse: ZangbaoSealItemRow[];
};

function parseSealDetailPayload(
  data: Record<string, unknown>,
  fallbackListingId = 0,
): ZangbaoSealDetail {
  const roleRaw = (data.role as Record<string, unknown>) || {};
  const mapRows = (key: string): ZangbaoSealItemRow[] => {
    const arr = data[key];
    if (!Array.isArray(arr)) return [];
    return arr.map((raw) => {
      const it = raw as Record<string, unknown>;
      const enhanceWash =
        typeof it.enhanceWash === "string"
          ? it.enhanceWash
          : typeof it.enhance_wash === "string"
            ? it.enhance_wash
            : "";
      const gemReflect = Number(it.gemReflect ?? it.gem_reflect) || 0;
      const gemCombo = Number(it.gemCombo ?? it.gem_combo) || 0;
      const gemRevive = Number(it.gemRevive ?? it.gem_revive) || 0;
      const slot = Number(it.slot ?? it.slot_index);
      const seq = Number(it.seq ?? it.item_seq) || 0;
      const instanceId = Number(it.instanceId ?? it.item_instance_id) || 0;
      const count = Number(it.count ?? it.item_count) || 0;
      return {
        ...(Number.isFinite(slot) ? { slot: slot || 0 } : {}),
        itemId: Number(it.itemId ?? it.item_id) || 0,
        count,
        enhanceLevel: Number(it.enhanceLevel ?? it.enhance_level) || 0,
        ...(seq ? { seq } : {}),
        ...(instanceId ? { instanceId } : {}),
        ...(enhanceWash ? { enhanceWash } : {}),
        ...(gemReflect || gemCombo || gemRevive
          ? { gemReflect, gemCombo, gemRevive }
          : {}),
      };
    });
  };
  const bagSeq = Number(data.bagSeq) || 0;
  return {
    ok: true,
    listingId: Number(data.listingId) || fallbackListingId,
    ...(bagSeq ? { bagSeq } : {}),
    blank: data.blank === true,
    sealedRoleId: Number(data.sealedRoleId) || 0,
    role: {
      name: String(roleRaw.name || ""),
      level: Number(roleRaw.level) || 0,
      job: Number(roleRaw.job) || 0,
      sex: Number(roleRaw.sex) || 0,
      title: String(roleRaw.title || ""),
      spouse: String(roleRaw.spouse || ""),
      guild: String(roleRaw.guild || ""),
      hp: Number(roleRaw.hp) || 0,
      hpMax: Number(roleRaw.hpMax ?? roleRaw.hp_max) || 0,
      mp: Number(roleRaw.mp) || 0,
      mpMax: Number(roleRaw.mpMax ?? roleRaw.mp_max) || 0,
      charm: Number(roleRaw.charm) || 0,
      strength: Number(roleRaw.strength) || 0,
      agility: Number(roleRaw.agility) || 0,
      wisdom: Number(roleRaw.wisdom) || 0,
      attack: Number(roleRaw.attack) || 0,
      armor: Number(roleRaw.armor ?? roleRaw.defense) || 0,
      dodge: Number(roleRaw.dodge) || 0,
      hit: Number(roleRaw.hit) || 0,
      crit: Number(roleRaw.crit) || 0,
      resist: Number(roleRaw.resist) || 0,
      reflectPct: Number(roleRaw.reflectPct ?? roleRaw.reflect_pct) || 0,
      comboPct: Number(roleRaw.comboPct ?? roleRaw.combo_pct) || 0,
      revivePct: Number(roleRaw.revivePct ?? roleRaw.revive_pct) || 0,
      vitality: Number(roleRaw.vitality) || 0,
      vitalityMax: Number(roleRaw.vitalityMax ?? roleRaw.vitality_max) || 0,
      money: Number(roleRaw.money) || 0,
      exp: Number(roleRaw.exp) || 0,
      wcoin: Number(roleRaw.wcoin ?? roleRaw.coolmoney) || 0,
      kcoin: Number(roleRaw.kcoin) || 0,
      scene: String(roleRaw.scene || ""),
    },
    equipment: mapRows("equipment"),
    backpack: mapRows("backpack"),
    warehouse: mapRows("warehouse"),
  };
}

function parseListing(it: Record<string, unknown>): ZangbaoListing {
  const sealedRaw = it.sealedRole as Record<string, unknown> | undefined;
  const sealedRole =
    sealedRaw && typeof sealedRaw === "object"
      ? {
          roleId: Number(sealedRaw.roleId) || 0,
          name: String(sealedRaw.name || ""),
          level: Number(sealedRaw.level) || 0,
          job: Number(sealedRaw.job) || 0,
          sex: Number(sealedRaw.sex) || 0,
        }
      : undefined;
  const itemId = Number(it.itemId) || 0;
  const enhanceWash =
    typeof it.enhanceWash === "string"
      ? it.enhanceWash
      : typeof it.enhance_wash === "string"
        ? it.enhance_wash
        : "";
  const gemReflect = Number(it.gemReflect ?? it.gem_reflect) || 0;
  const gemCombo = Number(it.gemCombo ?? it.gem_combo) || 0;
  const gemRevive = Number(it.gemRevive ?? it.gem_revive) || 0;
  return {
    listingId: Number(it.listingId) || 0,
    sellerRoleId: Number(it.sellerRoleId) || 0,
    sellerName: String(it.sellerName || ""),
    itemId,
    itemCount: Number(it.itemCount) || 0,
    enhanceLevel: Number(it.enhanceLevel) || 0,
    itemInstanceId: Number(it.itemInstanceId) || 0,
    ...(enhanceWash ? { enhanceWash } : {}),
    ...(gemReflect || gemCombo || gemRevive
      ? { gemReflect, gemCombo, gemRevive }
      : {}),
    priceKcoin: Number(it.priceKcoin) || 0,
    status: Number(it.status) || 0,
    ...(sealedRole && sealedRole.roleId ? { sealedRole } : {}),
  };
}

export type ZangbaoBuyResult =
  | {
      ok: true;
      payId: string;
      payUrl: string;
      priceCents: number;
      reallyPriceCents: number;
      priceKcoin: number;
      timeoutMinutes: number;
    }
  | { ok: false; error: string };

function gatewayHttpBase(gatewayWsUrl?: string): string {
  if (typeof window !== "undefined") {
    const params = new URLSearchParams(window.location.search);
    const fromQuery = params.get("admin") || params.get("recharge");
    if (fromQuery) {
      try {
        const u = new URL(fromQuery, window.location.href);
        return `${u.protocol}//${u.host}`;
      } catch {
        /* fall through */
      }
    }
    try {
      const stored = window.localStorage.getItem("pix_admin");
      if (stored) {
        const u = new URL(stored, window.location.href);
        return `${u.protocol}//${u.host}`;
      }
    } catch {
      /* private */
    }
  }
  const ws = gatewayWsUrl || defaultLiveGatewayUrl();
  try {
    const u = new URL(ws);
    const httpProto = u.protocol === "wss:" ? "https:" : "http:";
    return `${httpProto}//${u.host}`;
  } catch {
    return "http://127.0.0.1:19092";
  }
}

async function postForm(
  path: string,
  fields: Record<string, string>,
): Promise<Record<string, unknown>> {
  const body = new URLSearchParams(fields);
  const ctrl =
    typeof AbortController !== "undefined" ? new AbortController() : null;
  const timer =
    ctrl != null ? window.setTimeout(() => ctrl.abort(), 25000) : 0;
  try {
    const res = await fetch(`${gatewayHttpBase()}${path}`, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: body.toString(),
      signal: ctrl?.signal,
      cache: "no-store",
    });
    return (await res.json()) as Record<string, unknown>;
  } finally {
    if (timer) window.clearTimeout(timer);
  }
}

async function getJson(
  path: string,
  fields: Record<string, string>,
): Promise<Record<string, unknown>> {
  const q = new URLSearchParams(fields);
  const ctrl =
    typeof AbortController !== "undefined" ? new AbortController() : null;
  const timer =
    ctrl != null ? window.setTimeout(() => ctrl.abort(), 15000) : 0;
  try {
    const res = await fetch(`${gatewayHttpBase()}${path}?${q.toString()}`, {
      method: "GET",
      signal: ctrl?.signal,
      cache: "no-store",
    });
    return (await res.json()) as Record<string, unknown>;
  } finally {
    if (timer) window.clearTimeout(timer);
  }
}

export async function zangbaoBrowse(
  auth: { account: string; password: string; roleId: number },
  page = 1,
): Promise<ZangbaoListing[]> {
  const data = await getJson("/api/zangbao/browse", {
    account: auth.account,
    password: auth.password,
    role_id: String(auth.roleId),
    page: String(page),
  });
  if (!data || data.ok !== true || !Array.isArray(data.items)) return [];
  return (data.items as Record<string, unknown>[]).map(parseListing);
}

export async function zangbaoMine(
  auth: { account: string; password: string; roleId: number },
  page = 1,
): Promise<ZangbaoListing[]> {
  const data = await getJson("/api/zangbao/mine", {
    account: auth.account,
    password: auth.password,
    role_id: String(auth.roleId),
    page: String(page),
  });
  if (!data || data.ok !== true || !Array.isArray(data.items)) return [];
  return (data.items as Record<string, unknown>[]).map(parseListing);
}

export async function zangbaoSealDetail(
  listingId: number,
  auth?: { account: string; password: string; roleId: number },
): Promise<ZangbaoSealDetail | { ok: false; error: string }> {
  const q: Record<string, string> = { listingId: String(listingId) };
  if (auth) {
    q.account = auth.account;
    q.password = auth.password;
    q.role_id = String(auth.roleId);
  }
  const data = await getJson("/api/zangbao/seal-detail", q);
  if (!data || data.ok !== true) {
    return { ok: false, error: String(data?.error || "无法查看封印角色") };
  }
  return parseSealDetailPayload(data, listingId);
}

/** Bag-bound 857 detail: resolve sealed role via backpack seq. */
export async function zangbaoSealBagDetail(
  bagSeq: number,
  auth: { account: string; password: string; roleId: number },
): Promise<ZangbaoSealDetail | { ok: false; error: string }> {
  const data = await getJson("/api/zangbao/seal-detail", {
    bagSeq: String(bagSeq),
    account: auth.account,
    password: auth.password,
    role_id: String(auth.roleId),
  });
  if (!data || data.ok !== true) {
    return { ok: false, error: String(data?.error || "无法查看封印角色") };
  }
  return parseSealDetailPayload(data);
}

export async function zangbaoListItem(opts: {
  account: string;
  password: string;
  roleId: number;
  bagSeq: number;
  priceKcoin: number;
  count?: number;
}): Promise<{ ok: true; listingId: number } | { ok: false; error: string }> {
  const data = await postForm("/api/zangbao/list", {
    account: opts.account,
    password: opts.password,
    role_id: String(opts.roleId),
    bag_seq: String(opts.bagSeq),
    price_kcoin: String(opts.priceKcoin),
    count: String(opts.count || 0),
  });
  if (data.ok === true)
    return { ok: true, listingId: Number(data.listingId) || 0 };
  return { ok: false, error: String(data.error || "上架失败") };
}

export async function zangbaoCancel(opts: {
  account: string;
  password: string;
  roleId: number;
  listingId: number;
}): Promise<{ ok: true } | { ok: false; error: string }> {
  const data = await postForm("/api/zangbao/cancel", {
    account: opts.account,
    password: opts.password,
    role_id: String(opts.roleId),
    listing_id: String(opts.listingId),
  });
  if (data.ok === true) return { ok: true };
  return { ok: false, error: String(data.error || "下架失败") };
}

export async function zangbaoBuy(opts: {
  account: string;
  password: string;
  roleId: number;
  listingId: number;
  payType: 1 | 2;
}): Promise<ZangbaoBuyResult> {
  const data = await postForm("/api/zangbao/buy", {
    account: opts.account,
    password: opts.password,
    role_id: String(opts.roleId),
    listing_id: String(opts.listingId),
    pay_type: String(opts.payType),
  });
  if (data.ok === true && typeof data.payUrl === "string" && data.payUrl) {
    return {
      ok: true,
      payId: String(data.payId || ""),
      payUrl: data.payUrl,
      priceCents: Number(data.priceCents) || 0,
      reallyPriceCents: Number(data.reallyPriceCents) || 0,
      priceKcoin: Number(data.priceKcoin) || 0,
      timeoutMinutes: Number(data.timeoutMinutes) || 5,
    };
  }
  return { ok: false, error: String(data.error || "下单失败") };
}

export async function kcoinBalance(auth: {
  account: string;
  password: string;
  roleId: number;
}): Promise<{
  kcoin: number;
  kcoinPerYuan: number;
  hasPayee: boolean;
  payChannel: number;
}> {
  const data = await getJson("/api/kcoin/balance", {
    account: auth.account,
    password: auth.password,
    role_id: String(auth.roleId),
  });
  return {
    kcoin: Number(data.kcoin) || 0,
    kcoinPerYuan: Number(data.kcoinPerYuan) || 1,
    hasPayee: !!data.hasPayee,
    payChannel: Number(data.payChannel) || 0,
  };
}

/** Shrink phone photos so URL-encoded POST stays under admin/gateway body caps. */
export async function compressPayeeImageDataUrl(
  dataUrlOrFile: string | Blob,
  maxEdge = 720,
  quality = 0.72,
): Promise<string> {
  const blob =
    typeof dataUrlOrFile === "string"
      ? await (await fetch(dataUrlOrFile)).blob()
      : dataUrlOrFile;
  const bmp = await createImageBitmap(blob);
  try {
    const scale = Math.min(1, maxEdge / Math.max(bmp.width, bmp.height));
    const w = Math.max(1, Math.round(bmp.width * scale));
    const h = Math.max(1, Math.round(bmp.height * scale));
    const canvas = document.createElement("canvas");
    canvas.width = w;
    canvas.height = h;
    const ctx = canvas.getContext("2d");
    if (!ctx) throw new Error("canvas");
    ctx.drawImage(bmp, 0, 0, w, h);
    let out = canvas.toDataURL("image/jpeg", quality);
    // Keep form body under ~400 KiB after URLSearchParams encoding.
    let q = quality;
    while (out.length > 280000 && q > 0.4) {
      q -= 0.08;
      out = canvas.toDataURL("image/jpeg", q);
    }
    return out;
  } finally {
    bmp.close();
  }
}

export async function kcoinUploadPayee(opts: {
  account: string;
  password: string;
  roleId: number;
  payChannel: 1 | 2;
  imageB64: string;
}): Promise<{ ok: true } | { ok: false; error: string }> {
  let imageB64 = opts.imageB64;
  try {
    if (imageB64.startsWith("data:") || imageB64.length > 120000) {
      imageB64 = await compressPayeeImageDataUrl(imageB64);
    }
  } catch {
    /* keep original; server may still accept small images */
  }
  try {
    const data = await postForm("/api/kcoin/payee/upload", {
      account: opts.account,
      password: opts.password,
      role_id: String(opts.roleId),
      pay_channel: String(opts.payChannel),
      image_b64: imageB64,
    });
    if (data.ok === true) return { ok: true };
    return { ok: false, error: String(data.error || "上传失败") };
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    return { ok: false, error: msg || "上传失败" };
  }
}

export async function kcoinWithdrawCreate(opts: {
  account: string;
  password: string;
  roleId: number;
  amount: number;
}): Promise<{ ok: true } | { ok: false; error: string }> {
  const data = await postForm("/api/kcoin/withdraw/create", {
    account: opts.account,
    password: opts.password,
    role_id: String(opts.roleId),
    amount: String(opts.amount),
  });
  if (data.ok === true) return { ok: true };
  return { ok: false, error: String(data.error || "申请失败") };
}

export async function kcoinWithdrawList(auth: {
  account: string;
  password: string;
  roleId: number;
}): Promise<
  {
    withdrawId: number;
    amount: number;
    payChannel: number;
    status: number;
  }[]
> {
  const data = await getJson("/api/kcoin/withdraw/list", {
    account: auth.account,
    password: auth.password,
    role_id: String(auth.roleId),
  });
  if (!data || data.ok !== true || !Array.isArray(data.items)) return [];
  return (data.items as Record<string, unknown>[]).map((it) => ({
    withdrawId: Number(it.withdrawId) || 0,
    amount: Number(it.amount) || 0,
    payChannel: Number(it.payChannel) || 0,
    status: Number(it.status) || 0,
  }));
}
