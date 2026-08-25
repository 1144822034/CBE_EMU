/**
 * Mobile soft-keyboard / IME shell: keep the LCD fullscreen and stop the
 * browser from scrolling or shrinking the stage when the keyboard opens.
 *
 * Contract:
 * - On activate, snapshot #phone-wrap CSS size and layout viewport height
 *   *before* the keyboard resizes the WebView / 100dvh.
 * - Never shrink an existing lock (keyboard race / adjustResize).
 * - `.ime-open` CSS collapses D-pad to 左软|OK|右软 and pins #app to --ime-layout-h.
 */
let activeCount = 0;
let lockedCssW = 0;
let lockedCssH = 0;
let onViewport: (() => void) | null = null;

export type ImeShellLock = {
  /** CSS pixel size of #phone-wrap while IME is open (0 = not locked). */
  width: number;
  height: number;
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

function clearViewportOffset(): void {
  const wrap = document.getElementById("phone-wrap");
  if (wrap) wrap.style.transform = "";
  const app = document.getElementById("app");
  if (app) app.style.transform = "";
}

/**
 * Undo WebView adjustPan / visualViewport / IME translate residue.
 * Black-screen GL recover used to skip this → LCD content stayed shifted
 * toward the top-left and the HUD corner was clipped.
 */
export function resetShellViewportLayout(): void {
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
  }
}

function wireViewport(on: boolean): void {
  const vv = window.visualViewport;
  if (!vv) return;
  if (on && !onViewport) {
    onViewport = () => {
      if (activeCount <= 0) return;
      /* Keep layout viewport fixed; nudge wrap so focused field stays on-screen. */
      const ox = vv.offsetLeft || 0;
      const oy = vv.offsetTop || 0;
      const wrap = document.getElementById("phone-wrap");
      if (wrap) {
        wrap.style.transform =
          ox !== 0 || oy !== 0 ? `translate(${-ox}px, ${-oy}px)` : "";
      }
      window.scrollTo(0, 0);
    };
    vv.addEventListener("resize", onViewport);
    vv.addEventListener("scroll", onViewport);
    onViewport();
  } else if (!on && onViewport) {
    vv.removeEventListener("resize", onViewport);
    vv.removeEventListener("scroll", onViewport);
    onViewport = null;
  }
}
