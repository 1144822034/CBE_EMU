/**
 * Mobile soft-keyboard / IME shell: keep the LCD fullscreen and stop the
 * browser from scrolling or shrinking the stage when the keyboard opens.
 *
 * Contract:
 * - On activate, snapshot #phone-wrap CSS size and layout viewport height
 *   *before* the keyboard resizes the WebView / 100dvh.
 * - Never shrink an existing lock (keyboard race / adjustResize).
 * - `.ime-open` CSS collapses D-pad to 左软|OK|右软 and pins #app to --ime-layout-h.
 * - `#phone-wrap` is absolutely centered with `translate(-50%, -50%)`. IME pan
 *   and recover must restore that centering transform — clearing to "" lets
 *   Chromium/WebView compositor layers stay shifted after GL restore.
 */
let activeCount = 0;
let lockedCssW = 0;
let lockedCssH = 0;
let onViewport: (() => void) | null = null;

/** Stable #phone-wrap centering (absolute left/top 50% + this transform). */
export const SHELL_WRAP_CENTER_TRANSFORM = "translate(-50%, -50%)";

export type ImeShellLock = {
  /** CSS pixel size of #phone-wrap while IME is open (0 = not locked). */
  width: number;
  height: number;
};

export type ShellViewportSnapshot = {
  scrollX: number;
  scrollY: number;
  vvOffsetLeft: number;
  vvOffsetTop: number;
  vvWidth: number;
  vvHeight: number;
  wrapTransform: string;
  wrapW: number;
  wrapH: number;
  wrapLeft: number;
  wrapTop: number;
  stageW: number;
  stageH: number;
  stageLeft: number;
  stageTop: number;
  padL: number;
  padR: number;
  padT: number;
  padB: number;
  misaligned: boolean;
  imeActive: boolean;
};

export function imeShellIsActive(): boolean {
  return activeCount > 0;
}

export function imeShellLockedSize(): ImeShellLock | null {
  if (activeCount <= 0 || lockedCssW < 2 || lockedCssH < 2) return null;
  return { width: lockedCssW, height: lockedCssH };
}

/**
 * Enter / leave IME fullscreen mode. Nested (login fields / chat) use a refcount.
 * `lockSize` should be the wrap size after an immediate full-stage fit.
 */
export function setImeShellActive(
  active: boolean,
  lockSize?: { width: number; height: number },
): void {
  if (active) {
    activeCount += 1;
    if (activeCount === 1) {
      // Snapshot before keyboard / class-driven reflow can shrink the stage.
      seedLockFromWrap();
      pinLayoutHeight();
      if (lockSize && lockSize.width > 2 && lockSize.height > 2) {
        adoptLock(lockSize.width, lockSize.height);
      }
      document.getElementById("app")?.classList.add("ime-open");
      document.documentElement.classList.add("ime-open");
      window.scrollTo(0, 0);
      document.body.scrollTop = 0;
      document.documentElement.scrollTop = 0;
      wireViewport(true);
      window.dispatchEvent(new Event("ime-shell"));
    } else if (lockSize && lockSize.width > 2 && lockSize.height > 2) {
      adoptLock(lockSize.width, lockSize.height);
    }
    return;
  }
  if (activeCount <= 0) return;
  activeCount -= 1;
  if (activeCount === 0) {
    document.getElementById("app")?.classList.remove("ime-open");
    document.documentElement.classList.remove("ime-open");
    lockedCssW = 0;
    lockedCssH = 0;
    clearLayoutHeight();
    clearViewportOffset();
    wireViewport(false);
    window.dispatchEvent(new Event("ime-shell"));
  }
}

/** Apply lock after touch-bar hide + one full-stage fit. Never shrink. */
export function imeShellCaptureLockFromWrap(): void {
  const wrap = document.getElementById("phone-wrap");
  if (!wrap || activeCount <= 0) return;
  const w = wrap.clientWidth;
  const h = wrap.clientHeight;
  if (w > 2 && h > 2) adoptLock(w, h);
}

/**
 * Clear size lock only (IME stays active) so one fitPhoneWrap can measure the
 * expanded stage after #touch-bar is hidden. Re-apply via capture / adopt.
 */
export function imeShellClearLockSize(): void {
  lockedCssW = 0;
  lockedCssH = 0;
}

/** Merge a candidate size; never replaces an existing lock with a smaller one. */
export function imeShellAdoptLock(width: number, height: number): void {
  if (activeCount <= 0) return;
  if (width > 2 && height > 2) adoptLock(width, height);
}

function seedLockFromWrap(): void {
  const wrap = document.getElementById("phone-wrap");
  if (!wrap) return;
  const w = wrap.clientWidth;
  const h = wrap.clientHeight;
  if (w > 2 && h > 2) adoptLock(w, h);
}

/** Prefer larger size — keyboard must not replace a good lock with a shrunk one. */
function adoptLock(w: number, h: number): void {
  if (lockedCssW < 2 || lockedCssH < 2) {
    lockedCssW = w;
    lockedCssH = h;
    return;
  }
  if (w >= lockedCssW && h >= lockedCssH) {
    lockedCssW = w;
    lockedCssH = h;
  }
}

function pinLayoutHeight(): void {
  const h = Math.max(
    window.innerHeight || 0,
    document.documentElement.clientHeight || 0,
    document.body?.clientHeight || 0,
  );
  if (h > 2) {
    document.documentElement.style.setProperty("--ime-layout-h", `${h}px`);
  }
}

function clearLayoutHeight(): void {
  document.documentElement.style.removeProperty("--ime-layout-h");
}

function applyWrapTransform(ox: number, oy: number): void {
  const wrap = document.getElementById("phone-wrap");
  if (!wrap) return;
  if (ox !== 0 || oy !== 0) {
    wrap.style.transform = `translate(calc(-50% - ${ox}px), calc(-50% - ${oy}px))`;
  } else {
    wrap.style.transform = SHELL_WRAP_CENTER_TRANSFORM;
  }
}

function clearViewportOffset(): void {
  applyWrapTransform(0, 0);
  const app = document.getElementById("app");
  if (app) {
    app.style.transform = "";
    /* ime-open uses CSS position:fixed — wipe any leftover inline pin. */
    if (activeCount <= 0) {
      app.style.position = "";
      app.style.top = "";
      app.style.left = "";
      app.style.width = "";
      app.style.height = "";
      app.style.maxHeight = "";
    }
  }
}

/**
 * Undo WebView adjustPan / visualViewport / IME translate residue.
 * Black-screen GL recover used to skip this → LCD stayed shifted toward
 * top-left; stage letterbox + fixed #touch-chrome-toggle looked like the
 * top-right "covered" the game.
 */
export function resetShellViewportLayout(): void {
  try {
    const ae = document.activeElement;
    if (
      ae &&
      ae !== document.body &&
      ae !== document.documentElement &&
      typeof (ae as HTMLElement).blur === "function"
    ) {
      (ae as HTMLElement).blur();
    }
  } catch {
    /* ignore */
  }
  clearViewportOffset();
  try {
    window.scrollTo(0, 0);
    if (document.body) document.body.scrollTop = 0;
    document.documentElement.scrollTop = 0;
    if (document.body) document.body.scrollLeft = 0;
    document.documentElement.scrollLeft = 0;
  } catch {
    /* ignore */
  }
  const vv = window.visualViewport;
  if (vv) {
    const scrollToVv = (vv as unknown as { scrollTo?: (x: number, y: number) => void })
      .scrollTo;
    if (typeof scrollToVv === "function") {
      try {
        scrollToVv.call(vv, 0, 0);
      } catch {
        /* ignore */
      }
    }
  }
  /* Stale pin after screen-off with IME dismissed by the system. */
  if (activeCount <= 0) {
    clearLayoutHeight();
    document.getElementById("app")?.classList.remove("ime-open");
    document.documentElement.classList.remove("ime-open");
    lockedCssW = 0;
    lockedCssH = 0;
    wireViewport(false);
  }
}

/** Layout/compositor forensics for black-screen recover shift. */
export function shellViewportSnapshot(): ShellViewportSnapshot {
  const wrap = document.getElementById("phone-wrap");
  const stage = document.getElementById("stage");
  const vv = window.visualViewport;
  const wr = wrap?.getBoundingClientRect();
  const sr = stage?.getBoundingClientRect();
  const padL = wr && sr ? wr.left - sr.left : 0;
  const padR = wr && sr ? sr.right - wr.right : 0;
  const padT = wr && sr ? wr.top - sr.top : 0;
  const padB = wr && sr ? sr.bottom - wr.bottom : 0;
  const misaligned =
    !!wr &&
    !!sr &&
    (Math.abs(padL - padR) > 3 ||
      Math.abs(padT - padB) > 3 ||
      wr.left < sr.left - 2 ||
      wr.top < sr.top - 2);
  return {
    scrollX: window.scrollX || 0,
    scrollY: window.scrollY || 0,
    vvOffsetLeft: vv?.offsetLeft || 0,
    vvOffsetTop: vv?.offsetTop || 0,
    vvWidth: vv?.width || 0,
    vvHeight: vv?.height || 0,
    wrapTransform: wrap?.style.transform || "",
    wrapW: wrap?.clientWidth || 0,
    wrapH: wrap?.clientHeight || 0,
    wrapLeft: wr?.left || 0,
    wrapTop: wr?.top || 0,
    stageW: stage?.clientWidth || 0,
    stageH: stage?.clientHeight || 0,
    stageLeft: sr?.left || 0,
    stageTop: sr?.top || 0,
    padL,
    padR,
    padT,
    padB,
    misaligned,
    imeActive: activeCount > 0,
  };
}

/**
 * After GL restore, Chromium sometimes keeps a shifted compositor layer even
 * when layout math is correct. Bounce transform + canvas display to repaint.
 */
export function kickShellCompositor(): void {
  const wrap = document.getElementById("phone-wrap");
  if (!wrap) return;
  const center = SHELL_WRAP_CENTER_TRANSFORM;
  wrap.style.transform = `${center} translateZ(0)`;
  void wrap.offsetHeight;
  applyWrapTransform(0, 0);
  const canvas = wrap.querySelector("canvas") as HTMLElement | null;
  if (canvas) {
    const prev = canvas.style.display;
    canvas.style.display = "none";
    void canvas.offsetHeight;
    canvas.style.display = prev || "";
  }
}

/**
 * Reset + compositor kick; returns true when geometry was (or still is) skewed.
 */
export function repairShellViewportAlignment(): boolean {
  const before = shellViewportSnapshot();
  resetShellViewportLayout();
  kickShellCompositor();
  const after = shellViewportSnapshot();
  return before.misaligned || after.misaligned;
}

function wireViewport(on: boolean): void {
  const vv = window.visualViewport;
  if (!vv) return;
  if (on && !onViewport) {
    onViewport = () => {
      if (activeCount <= 0) {
        applyWrapTransform(0, 0);
        return;
      }
      /* Keep layout viewport fixed; nudge wrap so focused field stays on-screen. */
      const ox = vv.offsetLeft || 0;
      const oy = vv.offsetTop || 0;
      applyWrapTransform(ox, oy);
      window.scrollTo(0, 0);
    };
    vv.addEventListener("resize", onViewport);
    vv.addEventListener("scroll", onViewport);
    onViewport();
  } else if (!on && onViewport) {
    vv.removeEventListener("resize", onViewport);
    vv.removeEventListener("scroll", onViewport);
    onViewport = null;
    applyWrapTransform(0, 0);
  }
}
