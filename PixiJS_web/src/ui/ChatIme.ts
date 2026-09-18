/**
 * Canvas-aligned HTML input (chat / name / trade gold).
 * Activates ime-shell lock so soft keyboard does not shrink the LCD.
 */
import { setImeShellActive } from "./imeShell";

export type ChatImeLcdRect = { x: number; y: number; w: number; h: number };

export type ChatImeConfig = {
  maxLength?: number;
  /** HTML inputmode when armed (numeric → phone number pad). */
  inputMode?: "text" | "numeric" | "decimal" | "search" | "none";
  enterkeyhint?: string;
  ariaLabel?: string;
  /** Sanitize on every input; return value written back to the field. */
  sanitize?: (value: string) => string;
};

export class ChatIme {
  private input: HTMLInputElement;
  private host: HTMLElement | null = null;
  private visible = false;
  private shellHeld = false;
  private lastRect: ChatImeLcdRect | null = null;
  private onResize: (() => void) | null = null;
  private ro: ResizeObserver | null = null;
  private readonly config: Required<
    Pick<ChatImeConfig, "maxLength" | "inputMode" | "enterkeyhint" | "ariaLabel">
  > &
    Pick<ChatImeConfig, "sanitize">;

  onChange: ((value: string) => void) | null = null;
  onSubmit: (() => void) | null = null;

  constructor(config: ChatImeConfig = {}) {
    this.config = {
      maxLength: config.maxLength ?? 40,
      inputMode: config.inputMode ?? "text",
      enterkeyhint: config.enterkeyhint ?? "send",
      ariaLabel: config.ariaLabel ?? "输入",
      sanitize: config.sanitize,
    };
    this.input = document.createElement("input");
    this.input.type = "text";
    this.input.setAttribute("autocomplete", "off");
    this.input.setAttribute("autocapitalize", "off");
    this.input.setAttribute("autocorrect", "off");
    this.input.setAttribute("spellcheck", "false");
    this.input.setAttribute("aria-label", this.config.ariaLabel);
    this.input.setAttribute("enterkeyhint", this.config.enterkeyhint);
    this.input.maxLength = this.config.maxLength;
    this.input.inputMode = this.config.inputMode;
    if (this.config.inputMode === "numeric") {
      this.input.setAttribute("pattern", "[0-9]*");
      this.input.setAttribute("inputmode", "numeric");
    }
    Object.assign(this.input.style, {
      position: "absolute",
      display: "none",
      margin: "0",
      padding: "4px 6px",
      border: "0",
      borderRadius: "0",
      outline: "none",
      background: "transparent",
      color: "transparent",
      caretColor: "transparent",
      opacity: "0.02",
      zIndex: "22",
      fontSize: "16px",
      pointerEvents: "auto",
      boxSizing: "border-box",
    });
    this.input.style.setProperty("-webkit-text-fill-color", "transparent");
    this.input.addEventListener("input", () => {
      let v = this.input.value;
      if (this.config.sanitize) {
        v = this.config.sanitize(v);
        if (v !== this.input.value) this.input.value = v;
      }
      const max = this.config.maxLength;
      if (v.length > max) {
        v = v.slice(0, max);
        this.input.value = v;
      }
      this.onChange?.(v);
    });
    this.input.addEventListener("keydown", (ev) => {
      if (ev.key === "Enter") {
        ev.preventDefault();
        // Stop bubble to window keydown (main handleOk → confirm again).
        ev.stopPropagation();
        this.onSubmit?.();
      }
    });
    // Native tap on the overlay also focuses — lock shell then (not on show).
    this.input.addEventListener("focus", () => {
      if (this.visible) this.holdShell(true);
    });
  }

  show(rect: ChatImeLcdRect): void {
    this.lastRect = rect;
    const host = document.getElementById("phone-wrap");
    if (!host) return;
    if (this.host !== host) {
      this.host = host;
      host.appendChild(this.input);
    }
    this.visible = true;
    this.armInput(true);
    // Do not holdShell / focus here — ChatPanel opens without popping the keyboard.
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
    this.armInput(false);
    this.holdShell(false);
  }

  focus(value = ""): void {
    if (!this.visible) return;
    this.holdShell(true);
    this.armInput(true);
    let v = value;
    if (this.config.sanitize) v = this.config.sanitize(v);
    this.input.value = v.slice(0, this.config.maxLength);
    this.input.focus({ preventScroll: true });
    try {
      const len = this.input.value.length;
      this.input.setSelectionRange(len, len);
    } catch {
      /* ignore */
    }
    window.scrollTo(0, 0);
  }

  blur(): void {
    this.input.blur();
  }

  private armInput(on: boolean): void {
    const el = this.input;
    el.readOnly = !on;
    el.inputMode = on ? this.config.inputMode : "none";
    el.tabIndex = on ? 0 : -1;
    el.style.pointerEvents = on ? "auto" : "none";
    el.style.display = on ? "block" : "none";
  }

  /** Sync overlay text without focusing (create-role / list redraw). */
  setValue(value: string): void {
    let v = value;
    if (this.config.sanitize) v = this.config.sanitize(v);
    this.input.value = v.slice(0, this.config.maxLength);
  }

  clear(): void {
    this.input.value = "";
    this.onChange?.("");
  }

  get value(): string {
    return this.input.value;
  }

  isFocused(): boolean {
    return document.activeElement === this.input;
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
    this.input.remove();
    this.host = null;
  }

  private holdShell(on: boolean): void {
    if (on) {
      if (this.shellHeld) return;
      this.shellHeld = true;
      setImeShellActive(true);
    } else if (this.shellHeld) {
      this.shellHeld = false;
      setImeShellActive(false);
    }
  }

  private layout(): void {
    if (!this.host || !this.lastRect) return;
    const canvas = this.host.querySelector("canvas");
    if (!canvas) return;
    const wrapRect = this.host.getBoundingClientRect();
    const canvasRect = canvas.getBoundingClientRect();
    const screenW =
      Number(this.host.dataset.lcdScreenW || "0") || canvasRect.width;
    const screenH =
      Number(this.host.dataset.lcdScreenH || "0") || canvasRect.height;
    const lcdX = Number(this.host.dataset.lcdX || "0");
    const lcdY = Number(this.host.dataset.lcdY || "0");
    const lcdScale = Number(this.host.dataset.lcdScale || "1") || 1;
    const sx = canvasRect.width / Math.max(1, screenW);
    const sy = canvasRect.height / Math.max(1, screenH);
    const baseLeft = canvasRect.left - wrapRect.left;
    const baseTop = canvasRect.top - wrapRect.top;
    const r = this.lastRect;
    Object.assign(this.input.style, {
      left: `${baseLeft + (lcdX + r.x * lcdScale) * sx}px`,
      top: `${baseTop + (lcdY + r.y * lcdScale) * sy}px`,
      width: `${Math.max(48, r.w * lcdScale * sx)}px`,
      height: `${Math.max(28, r.h * lcdScale * sy)}px`,
    });
  }
}
