import { defaultLiveGatewayUrl } from "./Transport";

export type ChangePasswordResult =
  | { ok: true; account: string }
  | { ok: false; error: string };

/**
 * Derive account-center password URL from live gateway WS URL.
 * ws://host:19092 → http://host:19092/user/password
 * Optional `?admin=` / localStorage.pix_admin overrides the admin base.
 */
export function defaultPasswordUrl(gatewayWsUrl?: string): string {
  if (typeof window !== "undefined") {
    const params = new URLSearchParams(window.location.search);
    const fromQuery = params.get("admin") || params.get("password");
    if (fromQuery) return normalizePasswordUrl(fromQuery);
    try {
      const stored = window.localStorage.getItem("pix_admin");
      if (stored) return normalizePasswordUrl(stored);
    } catch {
      /* private mode */
    }
  }
  const ws = gatewayWsUrl || defaultLiveGatewayUrl();
  try {
    const u = new URL(ws);
    const httpProto = u.protocol === "wss:" ? "https:" : "http:";
    return `${httpProto}//${u.host}/user/password`;
  } catch {
    return "http://127.0.0.1:19092/user/password";
  }
}

function normalizePasswordUrl(raw: string): string {
  const s = raw.trim();
  if (!s) return s;
  if (s.includes("/user/password")) return s;
  return s.replace(/\/+$/, "") + "/user/password";
}

/** Mirror admin `/user/password` length rules before round-trip. */
export function validatePasswordChangeInput(
  currentPassword: string,
  newPassword: string,
  confirmPassword: string,
): string | null {
  if (!currentPassword) return "请输入当前密码";
  if (!newPassword) return "请输入新密码";
  if (!confirmPassword) return "请再次输入新密码";
  if (newPassword !== confirmPassword) return "两次输入的新密码不一致";
  if (newPassword.length < 6 || newPassword.length > 63) {
    return "新密码长度须为 6 至 63 个字符";
  }
  return null;
}

/**
 * Pixi JSON mode of account-center `POST /user/password`
 * (form includes `account` → JSON; browser session form unchanged).
 */
export async function changeAccountPassword(opts: {
  account: string;
  currentPassword: string;
  newPassword: string;
  confirmPassword: string;
  url?: string;
}): Promise<ChangePasswordResult> {
  const account = opts.account.trim();
  if (!account) return { ok: false, error: "账号参数不完整" };
  const localErr = validatePasswordChangeInput(
    opts.currentPassword,
    opts.newPassword,
    opts.confirmPassword,
  );
  if (localErr) return { ok: false, error: localErr };

  const target = opts.url || defaultPasswordUrl();
  const body = new URLSearchParams({
    account,
    current_password: opts.currentPassword,
    new_password: opts.newPassword,
    confirm_password: opts.confirmPassword,
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
    const data = (await res.json()) as Partial<ChangePasswordResult> & {
      error?: string;
      account?: string;
    };
    if (!data || typeof data !== "object") {
      return { ok: false, error: "修改密码服务响应无效" };
    }
    if (data.ok === true) {
      return {
        ok: true,
        account: String(data.account || account),
      };
    }
    const rawErr = (data.error && String(data.error)) || "密码修改失败";
    if (rawErr === "not-found") {
      return { ok: false, error: "修改密码接口未部署，请重启网关后再试" };
    }
    return { ok: false, error: rawErr };
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    console.warn(`[password] failed ${target}`, e);
    if (/abort/i.test(msg)) return { ok: false, error: "修改密码请求超时" };
    return { ok: false, error: "修改密码服务暂不可用" };
  }
}
