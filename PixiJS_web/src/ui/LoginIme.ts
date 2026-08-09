/**
 * Mobile IME bridge for canvas login fields.
 * Canvas taps alone often fail to open the soft keyboard (Pixi preventDefault).
 * Real <input> overlays over the field boxes receive the touch directly.
 */
import { setImeShellActive } from "./imeShell";

export type LoginImeField = "user" | "pass";

export type LoginImeLcdRect = { x: number; y: number; w: number; h: number };

export class LoginIme {
  private userInput: HTMLInputElement;
  private passInput: HTMLInputElement;
  private host: HTMLElement | null = null;
  private visible = false;
  private shellHeld = false;
  private lastRects: { user: LoginImeLcdRect; pass: LoginImeLcdRect } | null = null;
  private onResize: (() => void) | null = null;
  private ro: ResizeObserver | null = null;

  onChange: ((field: LoginImeField, value: string) => void) | null = null;
  onSubmit: (() => void) | null = null;
  onFocusField: ((field: LoginImeField) => void) | null = null;

  constructor() {
    this.userInput = makeFieldInput("text", "username", "账号");
    this.passInput = makeFieldInput("password", "current-password", "密码");

    const wire = (el: HTMLInputElement, field: LoginImeField) => {
      el.addEventListener("focus", () => {
        this.onFocusField?.(field);
      });
      el.addEventListener("input", () => {
        this.onChange?.(field, el.value.slice(0, 32));
      });
      el.addEventListener("keydown", (ev) => {
        if (ev.key === "Enter") {
          ev.preventDefault();
          this.onSubmit?.();
        }
      });
    };
    wire(this.userInput, "user");
    wire(this.passInput, "pass");
  }

  /** Soft sync: never overwrite the focused field (caret / composition). */
  setValues(user: string, password: string): void {
    if (document.activeElement !== this.userInput) this.userInput.value = user;
    if (document.activeElement !== this.passInput) this.passInput.value = password;
  }

  /** Hard sync after programmatic edits (paste / non-IME keyboard). */
  forceValues(user: string, password: string): void {
    const active = document.activeElement;
    this.userInput.value = user;
    this.passInput.value = password;
    if (active === this.userInput || active === this.passInput) {
      const el = active as HTMLInputElement;
      try {
        const len = el.value.length;
        el.setSelectionRange(len, len);
      } catch {
        /* password on some WebViews */
      }
    }
  }

  isFocused(): boolean {
    return (
      document.activeElement === this.userInput ||
      document.activeElement === this.passInput
    );
  }

  /** Show / reposition overlays for login mode (LCD coords of the input boxes). */
  show(rects: { user: LoginImeLcdRect; pass: LoginImeLcdRect }): void {
    this.lastRects = rects;
    const host = document.getElementById("phone-wrap");
    if (!host) return;
    if (this.host !== host) {
      this.host = host;
      host.appendChild(this.userInput);
      host.appendChild(this.passInput);
    }
    this.visible = true;
    this.armInput(this.userInput, true);
    this.armInput(this.passInput, true);
    if (!this.shellHeld) {
      this.shellHeld = true;
      setImeShellActive(true);
    }
    this.layout();
    if (!this.onResize) {
      this.onResize = () => {
        if (this.visible) this.layout();
      };
      window.addEventListener("resize", this.onResize);
      window.addEventListener("orientationchange", this.onResize);
      window.addEventListener("ime-shell", this.onResize);
      if (typeof ResizeObserver !== "undefined") {
        this.ro = new ResizeObserver(this.onResize);
        this.ro.observe(host);
      }
    }
  }

  hide(): void {
    this.visible = false;
    this.blur();
    this.armInput(this.userInput, false);
    this.armInput(this.passInput, false);
    if (this.shellHeld) {
      this.shellHeld = false;
      setImeShellActive(false);
    }
  }

  focus(field: LoginImeField, value: string): void {
    if (!this.visible) return;
    const el = field === "user" ? this.userInput : this.passInput;
    const other = field === "user" ? this.passInput : this.userInput;
    other.blur();
    this.armInput(el, true);
    el.value = value;
    el.focus({ preventScroll: true });
    try {
      const len = el.value.length;
      el.setSelectionRange(len, len);
    } catch {
      /* password on some WebViews */
    }
    window.scrollTo(0, 0);
  }

  blur(): void {
    this.userInput.blur();
    this.passInput.blur();
  }

  /** When off: hide + inert so D-pad taps cannot reopen the soft keyboard. */
  private armInput(el: HTMLInputElement, on: boolean): void {
    el.readOnly = !on;
    el.inputMode = on ? "text" : "none";
    el.tabIndex = on ? 0 : -1;
    el.style.pointerEvents = on ? "auto" : "none";
    el.style.display = on ? "block" : "none";
  }

  destroy(): void {
    if (this.onResize) {
      window.removeEventListener("resize", this.onResize);
      window.removeEventListener("orientationchange", this.onResize);
      window.removeEventListener("ime-shell", this.onResize);
      this.onResize = null;
    }
    this.ro?.disconnect();
    this.ro = null;
    this.hide();
    this.userInput.remove();
    this.passInput.remove();
    this.host = null;
  }

  private layout(): void {
    if (!this.host || !this.lastRects) return;
    const canvas = this.host.querySelector("canvas");
    if (!canvas) return;

    const wrapRect = this.host.getBoundingClientRect();
    const canvasRect = canvas.getBoundingClientRect();
    const screenW = Number(this.host.dataset.lcdScreenW || "0") || canvasRect.width;
    const screenH = Number(this.host.dataset.lcdScreenH || "0") || canvasRect.height;
    const lcdX = Number(this.host.dataset.lcdX || "0");
    const lcdY = Number(this.host.dataset.lcdY || "0");
    const lcdScale = Number(this.host.dataset.lcdScale || "1") || 1;
    const sx = canvasRect.width / Math.max(1, screenW);
    const sy = canvasRect.height / Math.max(1, screenH);
    const baseLeft = canvasRect.left - wrapRect.left;
    const baseTop = canvasRect.top - wrapRect.top;

    const place = (el: HTMLInputElement, r: LoginImeLcdRect) => {
      const left = baseLeft + (lcdX + r.x * lcdScale) * sx;
      const top = baseTop + (lcdY + r.y * lcdScale) * sy;
      const width = Math.max(48, r.w * lcdScale * sx);
      const height = Math.max(28, r.h * lcdScale * sy);
      Object.assign(el.style, {
        left: `${left}px`,
        top: `${top}px`,
        width: `${width}px`,
        height: `${height}px`,
      });
    };
    place(this.userInput, this.lastRects.user);
    place(this.passInput, this.lastRects.pass);
  }
}

function makeFieldInput(
  type: string,
  autocomplete: string,
  aria: string,
): HTMLInputElement {
  const el = document.createElement("input");
  el.type = type;
  el.setAttribute("autocomplete", autocomplete);
  el.setAttribute("autocapitalize", "off");
  el.setAttribute("autocorrect", "off");
  el.setAttribute("spellcheck", "false");
  el.setAttribute("aria-label", aria);
  el.setAttribute("enterkeyhint", type === "password" ? "go" : "next");
  el.inputMode = "text";
  // Transparent overlay: receives touch so IME opens; canvas paints the glyphs.
  el.style.position = "absolute";
  el.style.display = "none";
  el.style.margin = "0";
  el.style.padding = "4px 6px";
  el.style.border = "0";
  el.style.borderRadius = "0";
  el.style.outline = "none";
  el.style.background = "transparent";
  el.style.color = "transparent";
  el.style.caretColor = "transparent";
  el.style.opacity = "0.02";
  el.style.zIndex = "20";
  el.style.fontSize = "16px"; // avoid iOS zoom-on-focus
  el.style.setProperty("-webkit-text-fill-color", "transparent");
  el.style.pointerEvents = "auto";
  el.style.boxSizing = "border-box";
  return el;
}
