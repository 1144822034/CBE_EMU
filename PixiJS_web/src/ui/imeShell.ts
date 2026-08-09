/**
 * Mobile soft-keyboard / IME shell: keep the LCD fullscreen and stop the
 * browser from scrolling or shrinking the stage when the keyboard opens.
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
      document.getElementById("app")?.classList.add("ime-open");
      document.documentElement.classList.add("ime-open");
      window.scrollTo(0, 0);
      document.body.scrollTop = 0;
      document.documentElement.scrollTop = 0;
      if (lockSize && lockSize.width > 2 && lockSize.height > 2) {
        lockedCssW = lockSize.width;
        lockedCssH = lockSize.height;
      }
      wireViewport(true);
      window.dispatchEvent(new Event("ime-shell"));
    } else if (lockSize && lockSize.width > 2 && lockSize.height > 2) {
      lockedCssW = lockSize.width;
      lockedCssH = lockSize.height;
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
    clearViewportOffset();
    wireViewport(false);
    window.dispatchEvent(new Event("ime-shell"));
  }
}

/** Apply lock after touch-bar hide + one full-stage fit. */
export function imeShellCaptureLockFromWrap(): void {
  const wrap = document.getElementById("phone-wrap");
  if (!wrap || activeCount <= 0) return;
  const w = wrap.clientWidth;
  const h = wrap.clientHeight;
  if (w > 2 && h > 2) {
    lockedCssW = w;
    lockedCssH = h;
  }
}

function clearViewportOffset(): void {
  const wrap = document.getElementById("phone-wrap");
  if (wrap) wrap.style.transform = "";
  const app = document.getElementById("app");
  if (app) app.style.transform = "";
}

function wireViewport(on: boolean): void {
  const vv = window.visualViewport;
  if (!vv) return;
  if (on && !onViewport) {
    onViewport = () => {
      if (activeCount <= 0) return;
      /* Keep layout viewport fixed; nudge wrap so focused field stays on-screen. */
      const offset = vv.offsetTop || 0;
      const wrap = document.getElementById("phone-wrap");
      if (wrap) {
        wrap.style.transform = offset > 0 ? `translateY(${-offset}px)` : "";
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
