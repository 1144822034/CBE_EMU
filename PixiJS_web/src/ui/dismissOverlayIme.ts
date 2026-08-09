/**
 * Force-dismiss soft keyboard / overlay IME inputs (Android WebView).
 * Hidden login/chat <input> can keep focus; D-pad preventDefault then
 * re-triggers the IME on each press without stealing focus.
 */
import { setImeShellActive } from "./imeShell";

export function dismissOverlayIme(): void {
  const active = document.activeElement;
  if (active instanceof HTMLElement) {
    try {
      active.blur();
    } catch {
      /* ignore */
    }
  }

  const host = document.getElementById("phone-wrap");
  if (host) {
    for (const node of host.querySelectorAll("input, textarea")) {
      const el = node as HTMLInputElement;
      try {
        el.blur();
      } catch {
        /* ignore */
      }
      el.readOnly = true;
      el.inputMode = "none";
      el.tabIndex = -1;
      el.style.pointerEvents = "none";
      el.style.display = "none";
    }
  }

  setImeShellActive(false);
}
