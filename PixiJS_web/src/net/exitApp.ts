import { Capacitor, registerPlugin } from "@capacitor/core";

interface AppExitPlugin {
  exit(): Promise<void>;
}

const AppExit = registerPlugin<AppExitPlugin>("AppExit");

function isElectronShell(): boolean {
  if (typeof navigator === "undefined") return false;
  return /electron/i.test(navigator.userAgent);
}

/** Quit APK / Electron. Browser tabs cannot force-close. */
export async function exitHostApp(): Promise<{ ok: boolean; reason?: string }> {
  if (Capacitor.isNativePlatform()) {
    try {
      await AppExit.exit();
      return { ok: true };
    } catch (e) {
      const reason = e instanceof Error ? e.message : String(e);
      return { ok: false, reason };
    }
  }
  if (isElectronShell()) {
    try {
      window.close();
      return { ok: true };
    } catch (e) {
      const reason = e instanceof Error ? e.message : String(e);
      return { ok: false, reason };
    }
  }
  return { ok: false, reason: "请关闭页面退出" };
}
