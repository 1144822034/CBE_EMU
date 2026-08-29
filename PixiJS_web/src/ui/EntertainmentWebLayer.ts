/**
 * Entertainment web overlay — mounted on document.body (position:fixed),
 * geometry synced to #phone-wrap. Nesting inside #phone-wrap + overflow:hidden
 * remaps iframe touch hits on Chrome/WebView (prev/next / lyrics → wrong controls).
 */
import { Capacitor, registerPlugin } from "@capacitor/core";
import { LCD_HEIGHT, LCD_WIDTH, SOFTKEY_H } from "./constants";

interface EntertainmentHttpPlugin {
  resolve(opts: {
    url: string;
    preferJson?: boolean;
    timeoutMs?: number;
  }): Promise<{ url: string; body?: string }>;
}

const EntertainmentHttp = registerPlugin<EntertainmentHttpPlugin>("EntertainmentHttp");

/** Match SystemPanel mall list inset (LCD px → fraction of phone-wrap). */
const MALL_PANEL = {
  x: 10 / LCD_WIDTH,
  y: 48 / LCD_HEIGHT,
  w: (LCD_WIDTH - 20) / LCD_WIDTH,
  h: (LCD_HEIGHT - SOFTKEY_H - 64) / LCD_HEIGHT,
};

/** Near-full LCD — music player controls need room. */
const WIDE_PANEL = {
  x: 0,
  y: 0,
  w: 1,
  h: (LCD_HEIGHT - SOFTKEY_H) / LCD_HEIGHT,
};

export type EntertainmentPanelLayout = "mall" | "wide";

export type EntertainmentOpenOpts = {
  videoApi?: string;
  /** Parse JSON body for playable url/msg (抖音 etc.). */
  videoApiJson?: boolean;
  panel?: EntertainmentPanelLayout;
};

type PanelFrac = { x: number; y: number; w: number; h: number };

export class EntertainmentWebLayer {
  private root: HTMLDivElement;
  private panel: HTMLDivElement;
  private titleEl: HTMLSpanElement;
  private frame: HTMLIFrameElement;
  private video: HTMLVideoElement;
  private videoBar: HTMLDivElement;
  private contBtn: HTMLButtonElement;
  private muteBtn: HTMLButtonElement;
  private noteEl: HTMLDivElement;
  private chip: HTMLButtonElement;
  private expanded = false;
  private minimized = false;
  private currentUrl = "";
  private currentTitle = "";
  private videoApi = "";
  private videoMode = false;
  private videoApiJson = false;
  private videoLoadGen = 0;
  /** Last playable mp4 — skip immediate repeats from sticky redirect APIs. */
  private lastPlayableUrl = "";
  private panelLayout: EntertainmentPanelLayout = "mall";
  private autoNext = true;
  private noteTimer: ReturnType<typeof setTimeout> | null = null;
  private ro: ResizeObserver | null = null;
  private onWinResize: (() => void) | null = null;
  private mounted = false;

  onClose: (() => void) | null = null;
  onMinimize: (() => void) | null = null;
  onRestore: (() => void) | null = null;

  constructor() {
    this.root = document.createElement("div");
    this.root.id = "ent-web-layer";
    this.root.setAttribute("aria-hidden", "true");
    Object.assign(this.root.style, {
      display: "none",
      position: "fixed",
      // Above pay QR / touch chrome / canvas — stay visible over battle.
      zIndex: "2147483000",
      margin: "0",
      padding: "0",
      boxSizing: "border-box",
      fontFamily: 'SimSun, "Songti SC", "Microsoft YaHei", monospace',
      color: "#e8dcc8",
      touchAction: "manipulation",
      pointerEvents: "none",
      overflow: "visible",
    } as CSSStyleDeclaration);

    const dim = document.createElement("div");
    Object.assign(dim.style, {
      position: "absolute",
      inset: "0",
      background: "rgba(0,0,0,0.4)",
      pointerEvents: "auto",
      touchAction: "manipulation",
    } as CSSStyleDeclaration);
    dim.addEventListener("pointerdown", (e) => e.stopPropagation());

    this.panel = document.createElement("div");
    Object.assign(this.panel.style, {
      position: "absolute",
      display: "flex",
      flexDirection: "column",
      background: "#0e1b18",
      border: "1px solid #9eb5a8",
      borderRadius: "2px",
      boxSizing: "border-box",
      overflow: "hidden",
      pointerEvents: "auto",
      touchAction: "manipulation",
      // Isolate hit-testing from parent stacking quirks.
      transform: "translateZ(0)",
      willChange: "transform",
    } as CSSStyleDeclaration);

    const bar = document.createElement("div");
    Object.assign(bar.style, {
      display: "flex",
      alignItems: "center",
      gap: "3px",
      flexShrink: "0",
      height: "28px",
      padding: "0 4px",
      background: "#2a4840",
      borderBottom: "1px solid #6a8578",
      boxSizing: "border-box",
      touchAction: "manipulation",
    } as CSSStyleDeclaration);

    this.titleEl = document.createElement("span");
    Object.assign(this.titleEl.style, {
      flex: "1",
      minWidth: "0",
      overflow: "hidden",
      textOverflow: "ellipsis",
      whiteSpace: "nowrap",
      fontSize: "12px",
      lineHeight: "28px",
      paddingLeft: "2px",
    } as CSSStyleDeclaration);

    const openBtn = makeBarBtn("外开");
    openBtn.addEventListener("click", (e) => {
      e.preventDefault();
      e.stopPropagation();
      this.openExternal();
    });
    const minBtn = makeBarBtn("最小化");
    minBtn.addEventListener("click", (e) => {
      e.preventDefault();
      e.stopPropagation();
      this.minimize();
    });
    const closeBtn = makeBarBtn("关闭");
    closeBtn.addEventListener("click", (e) => {
      e.preventDefault();
      e.stopPropagation();
      this.close();
    });
    bar.append(this.titleEl, openBtn, minBtn, closeBtn);

    this.noteEl = document.createElement("div");
    Object.assign(this.noteEl.style, {
      display: "none",
      flexShrink: "0",
      padding: "3px 5px",
      fontSize: "10px",
      lineHeight: "1.3",
      background: "#1a3028",
      color: "#d5dba5",
      borderBottom: "1px solid #3a5a50",
    } as CSSStyleDeclaration);
    this.noteEl.textContent = "若空白：站点禁内嵌，点「外开」";

    this.frame = document.createElement("iframe");
    this.frame.title = "娱乐";
    this.frame.setAttribute(
      "allow",
      [
        "fullscreen *",
        "autoplay *",
        "encrypted-media *",
        "picture-in-picture *",
        "screen-wake-lock *",
      ].join("; "),
    );
    // Legacy allowfullscreen still helps some WebViews.
    this.frame.setAttribute("allowfullscreen", "true");
    this.frame.allowFullscreen = true;
    this.frame.referrerPolicy = "no-referrer-when-downgrade";
    Object.assign(this.frame.style, {
      flex: "1",
      width: "100%",
      height: "100%",
      border: "0",
      background: "#000",
      minHeight: "0",
      touchAction: "manipulation",
      // Avoid nested scroll/transform hit bugs on WebView.
      transform: "translateZ(0)",
    } as CSSStyleDeclaration);

    this.video = document.createElement("video");
    this.video.setAttribute("playsinline", "");
    this.video.setAttribute("webkit-playsinline", "");
    this.video.controls = true;
    this.video.autoplay = true;
    this.video.muted = true;
    this.video.preload = "auto";
    // CDN often blocks Capacitor https://localhost referrer → endless loading.
    this.video.setAttribute("referrerpolicy", "no-referrer");
    Object.assign(this.video.style, {
      display: "none",
      flex: "1",
      width: "100%",
      minHeight: "0",
      background: "#000",
      objectFit: "contain",
      touchAction: "manipulation",
    } as CSSStyleDeclaration);
    this.video.addEventListener("ended", () => {
      if (this.videoMode && this.autoNext) this.loadNextVideo();
    });
    this.video.addEventListener("error", () => {
      if (this.videoMode) {
        window.setTimeout(() => {
          if (this.videoMode && this.isActive()) this.loadNextVideo();
        }, 400);
      }
    });

    this.videoBar = document.createElement("div");
    Object.assign(this.videoBar.style, {
      display: "none",
      flexShrink: "0",
      gap: "4px",
      padding: "4px",
      background: "#1a3028",
      borderTop: "1px solid #3a5a50",
      boxSizing: "border-box",
      touchAction: "manipulation",
    } as CSSStyleDeclaration);
    this.contBtn = makeBarBtn("连续:开");
    this.contBtn.style.flex = "1";
    this.contBtn.addEventListener("click", (e) => {
      e.preventDefault();
      e.stopPropagation();
      this.autoNext = !this.autoNext;
      this.contBtn.textContent = this.autoNext ? "连续:开" : "连续:关";
    });
    const nextBtn = makeBarBtn("下一个");
    nextBtn.style.flex = "1";
    nextBtn.addEventListener("click", (e) => {
      e.preventDefault();
      e.stopPropagation();
      this.loadNextVideo();
    });
    this.muteBtn = makeBarBtn("有声");
    this.muteBtn.style.flex = "1";
    this.muteBtn.addEventListener("click", (e) => {
      e.preventDefault();
      e.stopPropagation();
      this.video.muted = !this.video.muted;
      this.syncMuteBtn();
      if (!this.video.muted) void this.video.play().catch(() => undefined);
    });
    this.videoBar.append(this.contBtn, nextBtn, this.muteBtn);

    this.panel.append(bar, this.noteEl, this.frame, this.video, this.videoBar);
    this.root.append(dim, this.panel);

    this.chip = document.createElement("button");
    this.chip.type = "button";
    this.chip.id = "ent-web-chip";
    this.chip.textContent = "娱";
    this.chip.title = "恢复娱乐窗口";
    Object.assign(this.chip.style, {
      display: "none",
      position: "fixed",
      zIndex: "2147483001",
      minWidth: "36px",
      height: "28px",
      padding: "0 8px",
      margin: "0",
      border: "1px solid #a8e8c0",
      borderRadius: "4px",
      background: "#102818",
      color: "#e8dcc8",
      fontSize: "13px",
      fontFamily: "inherit",
      cursor: "pointer",
      touchAction: "manipulation",
      boxShadow: "0 2px 6px rgba(0,0,0,0.45)",
    } as CSSStyleDeclaration);
    this.chip.addEventListener("click", (e) => {
      e.preventDefault();
      e.stopPropagation();
      this.restore();
    });
  }

  isExpanded(): boolean {
    return this.expanded;
  }

  isMinimized(): boolean {
    return this.minimized;
  }

  isActive(): boolean {
    return this.expanded || this.minimized;
  }

  isOpen(): boolean {
    return this.isExpanded();
  }

  open(url: string, title: string, opts?: EntertainmentOpenOpts): void {
    this.ensureMounted();
    const nextApi = (opts?.videoApi || "").trim();
    const wantVideo = !!nextApi;
    const layout: EntertainmentPanelLayout = opts?.panel || "mall";
    const wantJson = !!opts?.videoApiJson;
    const same =
      this.isActive() &&
      this.videoMode === wantVideo &&
      this.videoApiJson === wantJson &&
      this.panelLayout === layout &&
      (wantVideo ? this.videoApi === nextApi : this.currentUrl === url);

    this.currentUrl = url;
    this.currentTitle = title || "娱乐";
    this.videoApi = nextApi;
    this.videoApiJson = wantJson;
    this.videoMode = wantVideo;
    this.panelLayout = layout;
    this.titleEl.textContent = this.currentTitle;
    this.chip.textContent =
      this.currentTitle.length > 2
        ? this.currentTitle.slice(0, 2)
        : this.currentTitle || "娱";
    this.noteEl.style.display = "none";
    if (this.noteTimer) {
      clearTimeout(this.noteTimer);
      this.noteTimer = null;
    }
    this.minimized = false;
    this.expanded = true;
    this.chip.style.display = "none";
    this.root.style.display = "block";
    this.root.style.visibility = "visible";
    this.root.style.pointerEvents = "none";
    this.root.setAttribute("aria-hidden", "false");
    this.setTouchPolicy(true);
    this.layoutToPhone();
    this.applyModeUi();

    if (wantVideo) {
      if (!same) this.loadNextVideo();
      else void this.video.play().catch(() => undefined);
    } else if (!same) {
      this.frame.src = "about:blank";
      window.setTimeout(() => {
        if (this.isActive() && !this.videoMode && this.currentUrl === url) {
          this.frame.src = url;
        }
      }, 0);
      // Skip blank-tip for music (wide) — it steals taps near the player chrome.
      if (layout !== "wide") {
        this.noteTimer = setTimeout(() => {
          this.noteTimer = null;
          if (this.expanded && !this.videoMode) this.noteEl.style.display = "block";
        }, 1800);
      }
    }
  }

  minimize(): void {
    if (!this.expanded) return;
    this.expanded = false;
    this.minimized = true;
    // Keep display:block + visibility:hidden so iframe/video keep playing
    // under hangup / auto battle (display:none would stop media).
    this.root.style.visibility = "hidden";
    this.root.style.pointerEvents = "none";
    this.root.setAttribute("aria-hidden", "true");
    this.chip.style.display = "block";
    this.noteEl.style.display = "none";
    this.setTouchPolicy(false);
    this.layoutChip();
    if (this.noteTimer) {
      clearTimeout(this.noteTimer);
      this.noteTimer = null;
    }
    this.onMinimize?.();
  }

  restore(): void {
    if (!this.minimized || !this.currentUrl) return;
    this.ensureMounted();
    this.minimized = false;
    this.expanded = true;
    this.chip.style.display = "none";
    this.root.style.display = "block";
    this.root.style.visibility = "visible";
    this.root.style.pointerEvents = "none";
    this.root.setAttribute("aria-hidden", "false");
    this.setTouchPolicy(true);
    this.layoutToPhone();
    if (this.videoMode) void this.video.play().catch(() => undefined);
    this.onRestore?.();
  }

  close(): void {
    if (!this.isActive()) return;
    this.expanded = false;
    this.minimized = false;
    this.root.style.display = "none";
    this.root.style.visibility = "";
    this.root.style.pointerEvents = "none";
    this.root.setAttribute("aria-hidden", "true");
    this.chip.style.display = "none";
    this.setTouchPolicy(false);
    this.frame.src = "about:blank";
    this.video.pause();
    this.video.removeAttribute("src");
    this.video.load();
    this.currentUrl = "";
    this.currentTitle = "";
    this.videoApi = "";
    this.videoMode = false;
    this.videoApiJson = false;
    this.lastPlayableUrl = "";
    this.videoLoadGen++;
    this.noteEl.style.display = "none";
    if (this.noteTimer) {
      clearTimeout(this.noteTimer);
      this.noteTimer = null;
    }
    this.onClose?.();
  }

  private ensureMounted(): void {
    if (!this.mounted) {
      document.body.appendChild(this.root);
      document.body.appendChild(this.chip);
      this.mounted = true;
    }
    if (!this.onWinResize) {
      this.onWinResize = () => {
        if (this.expanded) this.layoutToPhone();
        else if (this.minimized) this.layoutChip();
      };
      window.addEventListener("resize", this.onWinResize);
      window.addEventListener("orientationchange", this.onWinResize);
      window.addEventListener("scroll", this.onWinResize, true);
      const wrap = document.getElementById("phone-wrap");
      if (wrap && typeof ResizeObserver !== "undefined") {
        this.ro = new ResizeObserver(() => this.onWinResize?.());
        this.ro.observe(wrap);
        const stage = document.getElementById("stage");
        if (stage) this.ro.observe(stage);
      }
    }
  }

  private layoutToPhone(): void {
    const wrap = document.getElementById("phone-wrap");
    if (!wrap) return;
    const r = wrap.getBoundingClientRect();
    Object.assign(this.root.style, {
      left: `${Math.round(r.left)}px`,
      top: `${Math.round(r.top)}px`,
      width: `${Math.round(r.width)}px`,
      height: `${Math.round(r.height)}px`,
    });
    const frac = this.panelFrac();
    Object.assign(this.panel.style, {
      left: `${Math.round(frac.x * r.width)}px`,
      top: `${Math.round(frac.y * r.height)}px`,
      width: `${Math.round(frac.w * r.width)}px`,
      height: `${Math.round(frac.h * r.height)}px`,
    });
  }

  private layoutChip(): void {
    const wrap = document.getElementById("phone-wrap");
    if (!wrap) return;
    const r = wrap.getBoundingClientRect();
    const soft = (SOFTKEY_H / LCD_HEIGHT) * r.height;
    // Keep clear of the bottom-right minimap / chrome hit target.
    this.chip.style.left = `${Math.round(r.right - 96)}px`;
    this.chip.style.top = `${Math.round(r.bottom - soft - 36)}px`;
    this.chip.style.right = "auto";
    this.chip.style.bottom = "auto";
  }

  private panelFrac(): PanelFrac {
    return this.panelLayout === "wide" ? WIDE_PANEL : MALL_PANEL;
  }

  private setTouchPolicy(open: boolean): void {
    document.documentElement.classList.toggle("ent-web-open", open);
    const canvas = document.querySelector("#phone-wrap canvas");
    if (canvas instanceof HTMLElement) {
      canvas.style.pointerEvents = open ? "none" : "";
    }
  }

  private applyModeUi(): void {
    if (this.videoMode) {
      this.frame.style.display = "none";
      this.frame.src = "about:blank";
      this.video.style.display = "block";
      this.videoBar.style.display = "flex";
      this.noteEl.style.display = "none";
      this.contBtn.textContent = this.autoNext ? "连续:开" : "连续:关";
    } else {
      this.video.pause();
      this.video.removeAttribute("src");
      this.video.load();
      this.video.style.display = "none";
      this.videoBar.style.display = "none";
      this.frame.style.display = "block";
    }
  }

  private loadNextVideo(): void {
    if (!this.videoApi) return;
    const gen = ++this.videoLoadGen;
    const keepMuted = this.video.muted;
    this.noteEl.textContent = "加载中…";
    this.noteEl.style.display = "block";
    this.video.pause();
    this.video.removeAttribute("src");
    this.video.load();

    void (async () => {
      try {
        const prev = this.lastPlayableUrl;
        let src = "";
        for (let attempt = 0; attempt < 6; attempt++) {
          const next = await resolvePlayableVideoUrl(this.videoApi, this.videoApiJson);
          if (gen !== this.videoLoadGen || !this.isActive() || !this.videoMode) return;
          if (!next) continue;
          // Drop cache-bust when comparing sticky CDN finals — not unresolved APIs.
          // Browser CORS often leaves us on video.php?_t=…; that endpoint must
          // rotate each request, so treating it as "same" blocks auto-next.
          const norm = stripCacheBust(next);
          const prevNorm = stripCacheBust(prev);
          const stillApi = sameVideoEndpoint(next, this.videoApi);
          if (prevNorm && norm === prevNorm && !stillApi) continue;
          src = next;
          break;
        }
        if (!src) throw new Error("empty video url");
        this.lastPlayableUrl = src;
        this.noteEl.style.display = "none";
        this.video.src = src;
        this.video.muted = keepMuted;
        this.syncMuteBtn();
        const play = () => void this.video.play().catch(() => {
          if (!this.video.muted) {
            this.video.muted = true;
            this.syncMuteBtn();
            void this.video.play().catch(() => undefined);
          }
        });
        // Guard endless spinner on APK when media never fires loadeddata.
        const failTimer = window.setTimeout(() => {
          if (gen !== this.videoLoadGen) return;
          if (this.video.readyState >= 2) return;
          this.noteEl.textContent = "加载超时，换下一个…";
          this.noteEl.style.display = "block";
          window.setTimeout(() => {
            if (gen === this.videoLoadGen && this.videoMode) this.loadNextVideo();
          }, 500);
        }, 10000);
        this.video.onloadeddata = () => {
          window.clearTimeout(failTimer);
          if (gen === this.videoLoadGen) this.noteEl.style.display = "none";
        };
        this.video.onerror = () => {
          window.clearTimeout(failTimer);
          if (gen !== this.videoLoadGen) return;
          this.noteEl.textContent = "播放失败，换下一个…";
          this.noteEl.style.display = "block";
          window.setTimeout(() => {
            if (gen === this.videoLoadGen && this.videoMode) this.loadNextVideo();
          }, 600);
        };
        play();
      } catch (e) {
        if (gen !== this.videoLoadGen) return;
        console.warn("[ent-web] resolve video failed", e);
        this.noteEl.textContent = "解析失败，换下一个…";
        this.noteEl.style.display = "block";
        window.setTimeout(() => {
          if (gen === this.videoLoadGen && this.videoMode) this.loadNextVideo();
        }, 700);
      }
    })();
  }

  private syncMuteBtn(): void {
    this.muteBtn.textContent = this.video.muted ? "有声" : "静音";
  }

  private openExternal(): void {
    if (!this.currentUrl) return;
    try {
      window.open(this.currentUrl, "_blank", "noopener,noreferrer");
    } catch {
      /* ignore */
    }
  }
}

function makeBarBtn(label: string): HTMLButtonElement {
  const btn = document.createElement("button");
  btn.type = "button";
  btn.textContent = label;
  Object.assign(btn.style, {
    flexShrink: "0",
    height: "22px",
    minWidth: "32px",
    padding: "0 6px",
    margin: "0",
    border: "1px solid #7aa090",
    borderRadius: "3px",
    background: "#2a5052",
    color: "#e8dcc8",
    fontSize: "11px",
    fontFamily: "inherit",
    cursor: "pointer",
    touchAction: "manipulation",
  } as CSSStyleDeclaration);
  return btn;
}

/** Bust cache without breaking existing query string. */
function withCacheBust(url: string): string {
  return `${url}${url.includes("?") ? "&" : "?"}_t=${Date.now()}`;
}

function stripCacheBust(url: string): string {
  return url.replace(/([?&])_t=\d+(\.\d+)?/g, "").replace(/[?&]$/, "");
}

/** True when `url` is still the rotate API (not a final CDN mp4). */
function sameVideoEndpoint(url: string, api: string): boolean {
  if (!url || !api) return false;
  return stripCacheBust(url) === stripCacheBust(api);
}

function pickUrlFromJson(data: unknown): string | null {
  const row = Array.isArray(data) ? data[0] : data;
  if (!row || typeof row !== "object") return null;
  const o = row as Record<string, unknown>;
  for (const key of ["url", "msg", "video", "play_url", "playUrl"]) {
    const v = o[key];
    if (typeof v === "string" && /^https?:\/\//i.test(v)) return v;
  }
  const nested = o.data;
  if (nested && typeof nested === "object") {
    const d = nested as Record<string, unknown>;
    for (const key of ["url", "msg", "video"]) {
      const v = d[key];
      if (typeof v === "string" && /^https?:\/\//i.test(v)) return v;
    }
  }
  return null;
}

/**
 * APK WebView often hangs forever on `<video src=302-api>`.
 * Resolve to a final .mp4 (JSON or redirect follow) before assigning src.
 * On native APK, CapacitorHttp cannot talk to entertainment hosts with broken
 * TLS — use EntertainmentHttp plugin (narrow SSL trust) instead.
 */
async function resolvePlayableVideoUrl(
  api: string,
  preferJson: boolean,
): Promise<string> {
  const bust = withCacheBust(api);

  if (Capacitor.isNativePlatform()) {
    try {
      const native = await EntertainmentHttp.resolve({
        url: bust,
        preferJson,
        timeoutMs: 12000,
      });
      if (native.body) {
        const fromBody = pickUrlFromJson(
          (() => {
            try {
              return JSON.parse(native.body.replace(/^\uFEFF/, "").trim());
            } catch {
              return null;
            }
          })(),
        );
        if (fromBody) return fromBody;
        const m = native.body
          .replace(/\\\//g, "/")
          .match(/https?:\/\/[^\s"'<>\\]+?\.(?:mp4|m3u8)[^\s"'<>\\]*/i);
        if (m?.[0]) return m[0];
      }
      if (native.url && /^https?:\/\//i.test(native.url)) return native.url;
    } catch (e) {
      console.warn("[ent-web] native resolve failed", e);
    }
    // Do not fall through to CapacitorHttp — same broken TLS as above.
    throw new Error("native entertainment resolve failed");
  }

  const tryJson = async (url: string): Promise<string | null> => {
    const ctrl = new AbortController();
    const timer = window.setTimeout(() => ctrl.abort(), 10000);
    try {
      const res = await fetch(url, { redirect: "follow", signal: ctrl.signal });
      const text = (await res.text()).replace(/^\uFEFF/, "").trim();
      if (!text) return null;
      try {
        return pickUrlFromJson(JSON.parse(text));
      } catch {
        // Some gateways wrap JSON in HTML — extract first http(s) .mp4
        const m = text.match(/https?:\/\/[^\s"'<>\\]+?\.(?:mp4|m3u8)[^\s"'<>\\]*/i);
        return m?.[0] ?? null;
      }
    } finally {
      window.clearTimeout(timer);
    }
  };

  if (preferJson) {
    const fromJson = await tryJson(bust);
    if (fromJson) return fromJson;
  }

  // Browser: <video> can follow cross-origin 302; fetch cannot (no ACAO on
  // nrzj video.php). Probing only spams CORS errors and never yields a CDN URL.
  // Native APK uses EntertainmentHttp above; keep Range probe unused on web.
  return bust;
}
