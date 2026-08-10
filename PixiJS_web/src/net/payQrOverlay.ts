/** Same-device QR: save to album + open WeChat/Alipay scan (cannot camera-scan own screen). */

import { Capacitor, registerPlugin } from "@capacitor/core";

type QrCtor = new (
  el: HTMLElement,
  opts: {
    text: string;
    width: number;
    height: number;
    colorDark: string;
    colorLight: string;
    correctLevel: unknown;
  },
) => { clear: () => void };

interface PayLauncherPlugin {
  savePngToGallery(options: { base64: string }): Promise<{ saved: boolean }>;
  openWeChatScan(): Promise<{ opened: boolean }>;
  openAlipayScan(): Promise<{ opened: boolean }>;
}

const PayLauncher = registerPlugin<PayLauncherPlugin>("PayLauncher");

declare global {
  interface Window {
    QRCode?: QrCtor & { CorrectLevel?: { M: unknown } };
  }
}

let scriptPromise: Promise<void> | null = null;
let overlay: HTMLDivElement | null = null;

function loadQrScript(): Promise<void> {
  if (typeof window.QRCode === "function") return Promise.resolve();
  if (scriptPromise) return scriptPromise;
  scriptPromise = new Promise((resolve, reject) => {
    const s = document.createElement("script");
    s.src = "./payment-qrcode.js";
    s.async = true;
    s.onload = () => resolve();
    s.onerror = () => {
      scriptPromise = null;
      reject(new Error("qr script load failed"));
    };
    document.head.appendChild(s);
  });
  return scriptPromise;
}

export function hidePayQrOverlay(): void {
  if (!overlay) return;
  overlay.remove();
  overlay = null;
}

function qrHostToPngDataUrl(host: HTMLElement): string | null {
  const canvas = host.querySelector("canvas");
  if (canvas) {
    try {
      return canvas.toDataURL("image/png");
    } catch {
      /* tainted */
    }
  }
  const img = host.querySelector("img");
  if (img?.src?.startsWith("data:image")) return img.src;
  const table = host.querySelector("table");
  if (!table) return null;
  const rows = table.querySelectorAll("tr");
  const n = rows.length;
  if (!n) return null;
  const size = 440;
  const cell = size / n;
  const c = document.createElement("canvas");
  c.width = size;
  c.height = size;
  const ctx = c.getContext("2d");
  if (!ctx) return null;
  ctx.fillStyle = "#ffffff";
  ctx.fillRect(0, 0, size, size);
  rows.forEach((tr, y) => {
    const cells = tr.querySelectorAll("td");
    cells.forEach((td, x) => {
      const bg = (td as HTMLElement).style.backgroundColor || "";
      const dark = /0|rgb\(0|black|#000|#101828/i.test(bg);
      if (!dark) return;
      ctx.fillStyle = "#101828";
      ctx.fillRect(Math.floor(x * cell), Math.floor(y * cell), Math.ceil(cell), Math.ceil(cell));
    });
  });
  return c.toDataURL("image/png");
}

function isAlipayChannel(channel: string): boolean {
  return /支付宝|alipay/i.test(channel);
}

async function saveAndOpenPayApp(
  host: HTMLElement,
  statusEl: HTMLElement | null,
  channel: string,
): Promise<void> {
  const alipay = isAlipayChannel(channel);
  const appName = alipay ? "支付宝" : "微信";
  const dataUrl = qrHostToPngDataUrl(host);
  if (!dataUrl) {
    if (statusEl) statusEl.textContent = "导出二维码失败";
    return;
  }
  if (!Capacitor.isNativePlatform()) {
    const a = document.createElement("a");
    a.href = dataUrl;
    a.download = `jh_pay_${Date.now()}.png`;
    a.click();
    if (statusEl) statusEl.textContent = `已下载图片，请用${appName}相册识别`;
    return;
  }
  try {
    if (statusEl) statusEl.textContent = "正在保存到相册…";
    await PayLauncher.savePngToGallery({ base64: dataUrl });
    if (alipay) {
      /* Album recognize of qr.alipay.com can open「名片」— do not open 扫一扫. */
      if (statusEl) {
        statusEl.textContent = "已保存；请用另一台手机支付宝扫屏幕上的码";
      }
      return;
    }
    if (statusEl) statusEl.textContent = `已保存 · 正在打开${appName}…`;
    await PayLauncher.openWeChatScan();
    if (statusEl) {
      statusEl.textContent = `${appName}扫一扫 → 相册 → 选刚保存的码`;
    }
  } catch (e) {
    console.warn("[payQrOverlay] save/open", e);
    if (statusEl) statusEl.textContent = `保存或打开${appName}失败，请重试`;
  }
}

/**
 * Show pay QR with album-scan path for same-device payment.
 */
export async function showPayQrOverlay(opts: {
  payUrl: string;
  amountText?: string;
  channel?: string;
  /** V免签个人收款码（非商家码）。 */
  personalCode?: boolean;
  /** Alipay: do not auto open 扫一扫 (album path can land on 名片). */
  skipAutoAlbum?: boolean;
  /** Alipay Intent already opened — QR is backup. */
  dualInsurance?: boolean;
}): Promise<boolean> {
  const payUrl = (opts.payUrl || "").trim();
  if (!payUrl) return false;
  try {
    await loadQrScript();
  } catch (e) {
    console.warn("[payQrOverlay] script", e);
    return false;
  }
  const QRCode = window.QRCode;
  if (typeof QRCode !== "function") return false;

  hidePayQrOverlay();
  const channel = opts.channel || "微信";
  const alipay = isAlipayChannel(channel);
  const appName = alipay ? "支付宝" : "微信";
  const btnColor = alipay ? "#1677ff" : "#07c160";
  const alipayHint = opts.dualInsurance
    ? "已尝试打开支付宝。若未跳转或付款失败，请用下方二维码（另一台手机扫，或截图后扫）。" +
      "个人收款码须输入上方金额（完全一致）。勿用本机相册识码（易误开「名片」）。"
    : "V免签为个人收款码（非商家码）。请用支付宝扫此码，并输入上方金额（须完全一致）。" +
      "勿用本机相册识码（易误开「名片」）；可用另一台手机扫，或截图后给他人扫。";

  const root = document.createElement("div");
  root.id = "pix-pay-qr-overlay";
  root.style.cssText =
    "position:fixed;inset:0;z-index:10000;display:flex;align-items:center;justify-content:center;" +
    "background:rgba(0,0,0,0.72);padding:16px;box-sizing:border-box;";
  root.innerHTML =
    '<div style="width:min(320px,100%);background:#fff;border-radius:12px;padding:16px 14px 12px;' +
    'font:14px/1.45 system-ui,-apple-system,sans-serif;color:#17202a;text-align:center">' +
    `<div style="font-weight:700;font-size:16px;margin-bottom:4px">${channel}支付</div>` +
    (opts.amountText
      ? `<div style="color:#d92d20;font-weight:800;font-size:22px;margin:6px 0">${opts.amountText}</div>`
      : "") +
    '<div style="color:#b54708;font-size:12px;margin-bottom:8px;text-align:left;background:#fffaeb;' +
    'border:1px solid #fedf89;border-radius:8px;padding:8px 10px">' +
    (alipay
      ? alipayHint
      : `本机无法扫自己屏幕上的码。<br>请点下方「保存到相册并打开${appName}」，` +
        `再在${appName}：扫一扫 → 相册 → 选刚保存的二维码。`) +
    "</div>" +
    '<div id="pix-pay-qr-host" style="display:grid;place-items:center;margin:0 auto 8px;width:200px;height:200px"></div>' +
    '<div id="pix-pay-qr-status" style="color:#667085;font-size:12px;min-height:18px;margin-bottom:8px"></div>' +
    `<button type="button" id="pix-pay-qr-save" style="width:100%;border:0;border-radius:8px;` +
    `padding:11px;background:${btnColor};color:#fff;font-weight:700;cursor:pointer;margin-bottom:8px">` +
    (alipay ? "保存二维码到相册" : `保存到相册并打开${appName}`) +
    "</button>" +
    '<button type="button" id="pix-pay-qr-close" style="width:100%;border:1px solid #d0d5dd;border-radius:8px;' +
    'padding:10px;background:#fff;color:#475467;font-weight:700;cursor:pointer">关闭</button></div>';
  document.body.appendChild(root);
  overlay = root;

  const host = root.querySelector("#pix-pay-qr-host") as HTMLElement | null;
  const statusEl = root.querySelector("#pix-pay-qr-status") as HTMLElement | null;
  const saveBtn = root.querySelector("#pix-pay-qr-save");
  const closeBtn = root.querySelector("#pix-pay-qr-close");
  closeBtn?.addEventListener("click", () => hidePayQrOverlay());
  root.addEventListener("click", (ev) => {
    if (ev.target === root) hidePayQrOverlay();
  });
  saveBtn?.addEventListener("click", () => {
    if (host) void saveAndOpenPayApp(host, statusEl, channel);
  });

  if (!host) return false;
  try {
    new QRCode(host, {
      text: payUrl,
      width: 200,
      height: 200,
      colorDark: "#101828",
      colorLight: "#ffffff",
      correctLevel: QRCode.CorrectLevel?.M ?? 0,
    });
    // WeChat same-phone: auto album flow. Alipay album path can open 名片 — skip.
    if (Capacitor.isNativePlatform() && !alipay && !opts.skipAutoAlbum) {
      window.setTimeout(() => {
        if (host && overlay === root) void saveAndOpenPayApp(host, statusEl, channel);
      }, 350);
    }
    return true;
  } catch (e) {
    console.warn("[payQrOverlay] render", e);
    host.textContent = "二维码生成失败";
    return false;
  }
}
