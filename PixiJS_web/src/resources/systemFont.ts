/**
 * Optional CSS / softkey fallback stack. LCD canvas text is UC3 only.
 */

export const SYSTEM_FONT_FALLBACK =
  'SimSun, "Songti SC", "Microsoft YaHei", "PingFang SC", "Noto Sans SC", sans-serif';

/** Always false — LCD path is UC3 only. */
export function preferSystemFont(): boolean {
  return false;
}

export function setPreferSystemFont(_on: boolean): void {
  /* no-op: UC3 only */
}

/** CSS stack for HTML softkeys / rare non-UC3 paths. */
export function uiFontFamily(): string {
  return SYSTEM_FONT_FALLBACK;
}
