import { defaultLiveGatewayUrl } from "./Transport";

export type RegisterResult =
  | { ok: true; account: string }
  | { ok: false; error: string };

/**
 * Derive HTTP register URL from live gateway WS URL.
 * ws://host:19092 → http://host:19092/api/register
 * Optional `?admin=` / localStorage.pix_admin overrides the admin base.
 */
export function defaultRegisterUrl(gatewayWsUrl?: string): string {
  if (typeof window !== "undefined") {
    const params = new URLSearchParams(window.location.search);
    const fromQuery = params.get("admin") || params.get("register");
    if (fromQuery) return normalizeRegisterUrl(fromQuery);
    try {
      const stored = window.localStorage.getItem("pix_admin");
      if (stored) return normalizeRegisterUrl(stored);
    } catch {
      /* private mode */
    }
  }
  const ws = gatewayWsUrl || defaultLiveGatewayUrl();
  try {
    const u = new URL(ws);
    const httpProto = u.protocol === "wss:" ? "https:" : "http:";
    return `${httpProto}//${u.host}/api/register`;
  } catch {
    return "http://127.0.0.1:19092/api/register";
  }
}

function normalizeRegisterUrl(raw: string): string {
  const s = raw.trim();
  if (!s) return s;
  if (s.includes("/api/register")) return s;
  return s.replace(/\/+$/, "") + "/api/register";
}

/** Mirror admin `vm_mock_user_valid_registration` before round-trip. */
export function validateRegisterInput(
  account: string,
  password: string,
): string | null {
  const user = account.trim();
  if (user.length < 4 || user.length > 32) {
    return "账号名长度须为 4 至 32 个字符";
  }
  if (!/^[A-Za-z0-9_]+$/.test(user)) {
    return "账号名只能包含字母、数字和下划线";
  }
  if (password.length < 6 || password.length > 63) {
    return "密码长度须为 6 至 63 个字符";
  }
  return null;
}

export async function registerAccount(opts: {
  account: string;
  password: string;
  url?: string;
}): Promise<RegisterResult> {
  const localErr = validateRegisterInput(opts.account, opts.password);
  if (localErr) return { ok: false, error: localErr };

  const target = opts.url || defaultRegisterUrl();
  const body = new URLSearchParams({
    account: opts.account.trim(),
    password: opts.password,
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
    const data = (await res.json()) as Partial<RegisterResult> & {
      error?: string;
      account?: string;
    };
    if (!data || typeof data !== "object") {
      return { ok: false, error: "注册服务响应无效" };
    }
    if (data.ok === true) {
      return {
        ok: true,
        account: String(data.account || opts.account.trim()),
      };
    }
    const rawErr = (data.error && String(data.error)) || "账号注册失败";
    // Old gateway without /api/register proxy returns this exact token.
    if (rawErr === "not-found") {
      return { ok: false, error: "注册接口未部署，请重启网关后再试" };
    }
    return {
      ok: false,
      error: rawErr,
    };
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    console.warn(`[register] failed ${target}`, e);
    if (/abort/i.test(msg)) return { ok: false, error: "注册请求超时" };
    return { ok: false, error: "注册服务暂不可用" };
  }
}
