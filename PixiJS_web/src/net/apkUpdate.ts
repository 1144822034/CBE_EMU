import { Capacitor, registerPlugin } from "@capacitor/core";

/**
 * Fixed release APK for forced client upgrades (codeVersion gate).
 * Upload newer APK to the same Gitee release asset path when shipping.
 */
export const FIXED_APK_UPDATE_URL =
  "https://gitee.com/liyuhailianxiying/jianghu-ol/releases/download/v1/jh_ol.apk";

interface ApkUpdaterPlugin {
  downloadAndInstall(options: { url: string }): Promise<{ ok: boolean; path?: string }>;
  addListener(
    eventName: "downloadProgress",
    listenerFunc: (event: { percent: number; bytes: number; total: number }) => void,
  ): Promise<{ remove: () => void }>;
}

const ApkUpdater = registerPlugin<ApkUpdaterPlugin>("ApkUpdater");

/** Server tip / login error for codeVersion mismatch (GBK「请更新客户端」). */
export function isClientUpdateRequiredError(message: string): boolean {
  const m = String(message || "");
  return m.includes("请更新客户端") || m.includes("更新客户端");
}

/**
 * Prompt on native Android; if confirmed, download FIXED_APK_UPDATE_URL and
 * open the system installer. No-op on web / Electron.
 */
export async function promptDownloadLatestApk(opts?: {
  onStatus?: (text: string, error?: boolean) => void;
}): Promise<boolean> {
  if (!Capacitor.isNativePlatform()) return false;

  const ok = window.confirm(
    "客户端版本过低，是否下载并安装最新版本？\n\n下载地址：Gitee 发布页 jh_ol.apk",
  );
  if (!ok) {
    opts?.onStatus?.("已取消更新", false);
    return false;
  }

  opts?.onStatus?.("正在下载更新…", false);
  let remove: (() => void) | undefined;
  try {
    const handle = await ApkUpdater.addListener("downloadProgress", (ev) => {
      if (ev.percent >= 0) {
        opts?.onStatus?.(`正在下载更新 ${ev.percent}%`, false);
      } else if (ev.bytes > 0) {
        const mb = (ev.bytes / (1024 * 1024)).toFixed(1);
        opts?.onStatus?.(`正在下载更新 ${mb}MB`, false);
      }
    });
    remove = () => {
      void handle.remove();
    };
    await ApkUpdater.downloadAndInstall({ url: FIXED_APK_UPDATE_URL });
    opts?.onStatus?.("已打开安装界面，请确认安装", false);
    return true;
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    opts?.onStatus?.(`更新失败：${msg}`, true);
    console.error("[apkUpdate]", e);
    return false;
  } finally {
    remove?.();
  }
}
