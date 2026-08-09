import type { SoftkeyLabels } from "./SoftkeyBar";
import { dismissOverlayIme } from "./dismissOverlayIme";

export type TouchDir = "up" | "down" | "left" | "right";

/**
 * HTML bottom bar: L/R(/center) softkeys + D-pad for phones.
 * Canvas SoftkeyBar is hidden; labels sync here.
 */
export class TouchControls {
  private left: HTMLButtonElement;
  private right: HTMLButtonElement;
  private center: HTMLButtonElement;
  private status: HTMLElement;
  private held = new Set<TouchDir>();

  onSoftkey: ((which: "left" | "center" | "right") => void) | null = null;
  /** D-pad center OK — keyboard F, not left softkey (Q). */
  onOk: (() => void) | null = null;
  onDir: ((dir: TouchDir, down: boolean) => void) | null = null;

  constructor() {
    this.left = el("sk-left");
    this.right = el("sk-right");
    this.center = el("sk-center");
    this.status = document.getElementById("touch-status") ?? document.body;

    wireSoft(this.left, () => this.onSoftkey?.("left"));
    wireSoft(this.right, () => this.onSoftkey?.("right"));
    wireSoft(this.center, () => this.onSoftkey?.("center"));

    const okBtn = document.querySelector<HTMLButtonElement>(".dpad .ok");
    if (okBtn) {
      wireSoft(okBtn, () => {
        dismissOverlayIme();
        this.onOk?.();
      });
    }

    for (const btn of document.querySelectorAll<HTMLButtonElement>(".dpad .pad")) {
      const dir = btn.dataset.dir as TouchDir | undefined;
      if (!dir) continue;
      const down = (ev: Event) => {
        ev.preventDefault();
        // Stale login/chat <input> focus + preventDefault reopens Android IME.
        dismissOverlayIme();
        if (this.held.has(dir)) return;
        this.held.add(dir);
        btn.classList.add("is-down");
        this.onDir?.(dir, true);
      };
      const up = (ev: Event) => {
        ev.preventDefault();
        if (!this.held.has(dir)) return;
        this.held.delete(dir);
        btn.classList.remove("is-down");
        this.onDir?.(dir, false);
      };
      btn.addEventListener("pointerdown", down);
      btn.addEventListener("pointerup", up);
      btn.addEventListener("pointercancel", up);
      btn.addEventListener("pointerleave", up);
      btn.addEventListener("contextmenu", (e) => e.preventDefault());
    }

    window.addEventListener("blur", () => this.releaseAll());
  }

  setSoftkeys(labels: SoftkeyLabels): void {
    setBtn(this.left, labels.left);
    setBtn(this.right, labels.right);
    const c = labels.center?.trim();
    // Only expand center row when labeled — frees stage space otherwise.
    this.center.classList.toggle("is-on", !!c);
    this.center.style.pointerEvents = c ? "auto" : "none";
    setBtn(this.center, c || undefined);
  }

  setStatus(text: string, error = false): void {
    this.status.textContent = text;
    this.status.classList.toggle("error", error);
  }

  releaseAll(): void {
    for (const dir of [...this.held]) {
      this.held.delete(dir);
      this.onDir?.(dir, false);
    }
    for (const btn of document.querySelectorAll(".dpad .pad.is-down")) {
      btn.classList.remove("is-down");
    }
  }
}

function el(id: string): HTMLButtonElement {
  const n = document.getElementById(id);
  if (!(n instanceof HTMLButtonElement)) {
    throw new Error(`missing #${id}`);
  }
  return n;
}

function setBtn(btn: HTMLButtonElement, label: string | undefined): void {
  const t = label?.trim() ?? "";
  btn.disabled = !t;
  if (t) {
    btn.textContent = t;
    return;
  }
  if (btn.id === "sk-left") btn.textContent = "左软";
  else if (btn.id === "sk-right") btn.textContent = "右软";
  else if (btn.classList.contains("ok")) btn.textContent = "OK";
  else btn.textContent = "中软";
}

function wireSoft(btn: HTMLButtonElement, fn: () => void): void {
  btn.addEventListener("pointerdown", (ev) => {
    ev.preventDefault();
    if (btn.disabled) return;
    try {
      btn.setPointerCapture(ev.pointerId);
    } catch {
      /* ignore */
    }
    fn();
  });
  btn.addEventListener("contextmenu", (e) => e.preventDefault());
}
