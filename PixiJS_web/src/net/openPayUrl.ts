import { Capacitor, registerPlugin } from "@capacitor/core";
import { showPayQrOverlay } from "./payQrOverlay";

interface PayLauncherPlugin {
  open(options: { url: string }): Promise<{
    opened: boolean;
    needQr?: boolean;
    scheme?: string;
  }>;
}

const PayLauncher = registerPlugin<PayLauncherPlugin>("PayLauncher");

const SAFE_SCHEMES = /^(https?:|wxp:|weixin:|alipays?:|alipayqr:)/i;

function isSafePayUrl(url: string): boolean {
  const s = (url || "").trim();
  return !!s && SAFE_SCHEMES.test(s) && !/[\r\n]/.test(s);
}

export type OpenPayResult = {
  opened: boolean;
  showedQr: boolean;
  scheme: string;
};

function schemeOf(url: string): string {
  const i = url.indexOf(":");
  return i > 0 ? url.slice(0, i).toLowerCase() : "";
}

function isWechatScheme(scheme: string): boolean {
  return scheme === "wxp" || scheme === "weixin";
}

function isAlipayScheme(scheme: string): boolean {
  return scheme === "alipays" || scheme === "alipay" || scheme === "alipayqr";
}

function isAlipayPayUrl(url: string, scheme: string, channel?: string): boolean {
  if (isAlipayScheme(scheme)) return true;
  if (/支付宝|alipay/i.test(channel || "")) return true;
  return /alipay\.com/i.test(url);
}

/**
 * Open / present provider payUrl.
 *
 * V免签个人收款码：
 * - 支付宝 https://qr.alipay.com/fkx…：Intent 打开支付页，同时弹扫码层双保险。
 *   不自动相册识码（扫自己的码易开「名片」）；可用另一台手机扫屏幕码。
 * - 微信 wxp://：二维码 + 本机相册识码。
 */
export async function openPayUrl(
  url: string,
  opts?: { amountText?: string; channel?: string },
): Promise<OpenPayResult> {
  const target = (url || "").trim();
  const scheme = schemeOf(target);
  if (!isSafePayUrl(target)) {
    console.warn("[openPayUrl] rejected unsafe url scheme=" + scheme);
    return { opened: false, showedQr: false, scheme };
  }

  let opened = false;
  let needQr = scheme === "wxp";
  const channel =
    opts?.channel ||
    (isAlipayScheme(scheme)
      ? "支付宝"
      : isWechatScheme(scheme)
        ? "微信"
        : "支付");
  const alipay = isAlipayPayUrl(target, scheme, channel);

  if (Capacitor.isNativePlatform()) {
    try {
      if (scheme === "wxp") {
        needQr = true;
      } else {
        const r = await PayLauncher.open({ url: target });
        opened = !!r?.opened;
        if (r?.needQr) needQr = true;
      }
      console.info(
        `[openPayUrl] native scheme=${scheme} opened=${opened} needQr=${needQr} alipay=${alipay} url=${target.slice(0, 80)}`,
      );
    } catch (e) {
      console.warn("[openPayUrl] native open failed", e);
      if (isWechatScheme(scheme) || alipay) needQr = true;
    }
  } else {
    try {
      if (/^https?:/i.test(target)) {
        opened = !!window.open(target, "_blank", "noopener,noreferrer");
      } else if (scheme === "wxp") {
        needQr = true;
      } else {
        window.location.href = target;
        opened = true;
      }
    } catch (e) {
      console.warn("[openPayUrl] browser open failed", e);
    }
  }

  let showedQr = false;
  // Alipay: always show QR alongside Intent (dual insurance). WeChat: QR when needed.
  const wantQr =
    alipay || needQr || (!opened && isWechatScheme(scheme));
  if (wantQr) {
    showedQr = await showPayQrOverlay({
      payUrl: target,
      amountText: opts?.amountText,
      channel,
      personalCode: true,
      skipAutoAlbum: alipay,
      dualInsurance: alipay && opened,
    });
  }

  return { opened, showedQr, scheme };
}
