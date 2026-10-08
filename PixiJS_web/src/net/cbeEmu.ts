import { Capacitor, registerPlugin } from "@capacitor/core";

interface CbeEmuPlugin {
  launch(): Promise<{
    ok: boolean;
    downloaded?: number;
    loadedFrom?: string;
    note?: string;
    syncError?: string;
  }>;
}

const CbeEmu = registerPlugin<CbeEmuPlugin>("CbeEmu");

type JhNative = {
  launchCbe?: () => Promise<{
    ok: boolean;
    error?: string;
    note?: string;
    pid?: number;
  }>;
};

function jhNative(): JhNative | undefined {
  return (window as unknown as { jhNative?: JhNative }).jhNative;
}

export function isCbeEmuAvailable(): boolean {
  return Capacitor.isNativePlatform() || typeof jhNative()?.launchCbe === "function";
}

export async function launchClassicCbeClient(opts?: {
  onStatus?: (text: string, error?: boolean) => void;
}): Promise<boolean> {
  const native = jhNative();
  if (!Capacitor.isNativePlatform() && typeof native?.launchCbe !== "function") {
    opts?.onStatus?.("经典怀旧仅安卓 App / PC 客户端内可用", true);
    return false;
  }
  const ok = window.confirm(
    "打开经典怀旧（CBE）？\n请勿与本客户端使用同一账号同时在线。",
  );
  if (!ok) {
    opts?.onStatus?.("已取消", false);
    return false;
  }
  if (typeof native?.launchCbe === "function") {
    opts?.onStatus?.("正在启动经典怀旧…", false);
    try {
      const ret = await native.launchCbe();
      if (!ret?.ok) {
        opts?.onStatus?.(`经典怀旧启动失败：${ret?.error || "未知错误"}`, true);
        return false;
      }
      opts?.onStatus?.(ret.note || "已打开经典怀旧窗口", false);
      return true;
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      opts?.onStatus?.(`经典怀旧启动失败：${msg}`, true);
      return false;
    }
  }
  opts?.onStatus?.("正在检查模拟器库…", false);
  try {
    const ret = await CbeEmu.launch();
    if (ret.note) opts?.onStatus?.(ret.note, false);
    else if ((ret.downloaded ?? 0) > 0) opts?.onStatus?.("模拟器库已更新", false);
    else opts?.onStatus?.("正在启动经典怀旧", false);
    return true;
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    opts?.onStatus?.(`经典怀旧启动失败：${msg}`, true);
    return false;
  }
}
