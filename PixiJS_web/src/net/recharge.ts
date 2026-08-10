import { defaultLiveGatewayUrl } from "./Transport";

export type RechargePayType = 1 | 2;

export type RechargeCreateResult =
  | {
      ok: true;
      payId: string;
      payType: number;
      priceCents: number;
      reallyPriceCents: number;
      wcoinAmount: number;
      payUrl: string;
      status: number;
      timeoutMinutes: number;
    }
  | { ok: false; error: string };

/**
 * Derive HTTP recharge create URL from live gateway WS URL.
 * ws://host:19092 → http://host:19092/api/recharge/create
 * Optional `?admin=` / localStorage.pix_admin overrides the admin base.
 */
export function defaultRechargeCreateUrl(gatewayWsUrl?: string): string {
  if (typeof window !== "undefined") {
    const params = new URLSearchParams(window.location.search);
    const fromQuery = params.get("admin") || params.get("recharge");
    if (fromQuery) return normalizeRechargeCreateUrl(fromQuery);
    try {
      const stored = window.localStorage.getItem("pix_admin");
      if (stored) return normalizeRechargeCreateUrl(stored);
    } catch {
      /* private mode */
    }
  }
  const ws = gatewayWsUrl || defaultLiveGatewayUrl();
  try {
    const u = new URL(ws);
    const httpProto = u.protocol === "wss:" ? "https:" : "http:";
    return `${httpProto}//${u.host}/api/recharge/create`;
  } catch {
    return "http://127.0.0.1:19092/api/recharge/create";
  }
}

function normalizeRechargeCreateUrl(raw: string): string {
  const s = raw.trim();
  if (!s) return s;
  if (s.includes("/api/recharge/create")) return s;
  return s.replace(/\/+$/, "") + "/api/recharge/create";
}

export async function createRechargeOrder(opts: {
  account: string;
  password: string;
  roleId: number;
  yuan: number;
  payType: RechargePayType;
  url?: string;
}): Promise<RechargeCreateResult> {
  const target = opts.url || defaultRechargeCreateUrl();
  const body = new URLSearchParams({
    account: opts.account,
    password: opts.password,
    role_id: String(opts.roleId >>> 0),
    yuan: String(opts.yuan >>> 0),
    pay_type: String(opts.payType),
  });
  try {
    const ctrl =
      typeof AbortController !== "undefined" ? new AbortController() : null;
    const timer =
      ctrl != null ? window.setTimeout(() => ctrl.abort(), 20000) : 0;
    const res = await fetch(target, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: body.toString(),
      signal: ctrl?.signal,
      cache: "no-store",
    });
    if (timer) window.clearTimeout(timer);
    const data = (await res.json()) as Partial<RechargeCreateResult> & {
      error?: string;
    };
    if (!data || typeof data !== "object") {
      return { ok: false, error: "赞助服务响应无效" };
    }
    if (data.ok === true && typeof data.payUrl === "string" && data.payUrl) {
      return {
        ok: true,
        payId: String(data.payId || ""),
        payType: Number(data.payType) || opts.payType,
        priceCents: Number(data.priceCents) || 0,
        reallyPriceCents: Number(data.reallyPriceCents) || 0,
        wcoinAmount: Number(data.wcoinAmount) || 0,
        payUrl: data.payUrl,
        status: Number(data.status) || 0,
        timeoutMinutes: Number(data.timeoutMinutes) || 5,
      };
    }
    return {
      ok: false,
      error: (data.error && String(data.error)) || "订单创建失败",
    };
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    console.warn(`[recharge] create failed ${target}`, e);
    if (/abort/i.test(msg)) return { ok: false, error: "赞助请求超时" };
    return { ok: false, error: "赞助服务暂不可用" };
  }
}

/** Format cents as ￥N.NN for tips. */
export function formatRechargeYuan(cents: number): string {
  const n = Math.max(0, cents | 0);
  return `￥${Math.floor(n / 100)}.${String(n % 100).padStart(2, "0")}`;
}
