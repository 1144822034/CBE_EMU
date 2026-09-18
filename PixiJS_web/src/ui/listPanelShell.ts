/**
 * Shared CBE-style list panel chrome: frame, title, tabs, rows, page nav.
 * Panels keep their own state / softkeys; redraw calls these paint helpers.
 */
import { Container, Graphics } from "pixi.js";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  drawCbeTitlePill,
  paintCbePanel,
} from "./constants";
import { attachRowPointerHit } from "./pointerHit";
import { attachPageNavButtons } from "./pageNav";
import {
  UI_WRAP_CHARS,
  UC3_CELL,
  measureUiTextBytes,
  measureUiTextWidth,
  uiCharByteWidth,
  wrapChars,
} from "./BitmapLabel";

export const LIST_SHELL_DEFAULT_PAGE_SIZE = 8;
/** Dual-line list row height (name + desc, UC3 14px cell). */
export const LIST_DUAL_LINE_ROW_H = 36;
/** Titles panel fits 7 dual-line rows in the social shell. */
export const LIST_TITLES_PAGE_SIZE = 7;
export const LIST_SHELL_TAB_BG = 0x375946;

/** Inset from row top for dual-line name / desc (px). */
const DUAL_LINE_NAME_Y = 3;
const DUAL_LINE_SUB_Y = 18;

export type ListShellCtx = {
  host: Container;
  box: Graphics;
  hits: Container[];
  addLabel: (text: string, x: number, y: number, color: number) => void;
  addLabelCentered?: (text: string, cx: number, cy: number, color: number) => void;
};

export type ListShellBounds = {
  x: number;
  y: number;
  w: number;
  h: number;
};

export type ListShellRow = {
  name: string;
  subtitle?: string;
  nameColor?: number;
  subtitleColor?: number;
};

/** Standard overlay panel inset (matches Social / Task). */
export function listShellBounds(): ListShellBounds {
  return {
    x: 8,
    y: 28,
    w: LCD_WIDTH - 16,
    h: LCD_HEIGHT - SOFTKEY_H - 40,
  };
}

export function listShellPageStart(cursor: number, pageSize: number): number {
  const ps = Math.max(1, pageSize | 0);
  return Math.floor(Math.max(0, cursor) / ps) * ps;
}

export function listShellPageInfo(
  total: number,
  cursor: number,
  pageSize: number,
): { page: number; pages: number; pageStart: number } {
  const ps = Math.max(1, pageSize | 0);
  const n = Math.max(0, total | 0);
  const pages = Math.max(1, Math.ceil(Math.max(1, n) / ps));
  const pageStart = listShellPageStart(cursor, ps);
  const page = n ? Math.floor(pageStart / ps) + 1 : 1;
  return { page, pages, pageStart };
}

/** Move cursor by one local page (−1 / +1), clamped. */
export function listShellStepPage(
  cursor: number,
  total: number,
  pageSize: number,
  dir: -1 | 1,
): number {
  const ps = Math.max(1, pageSize | 0);
  const n = Math.max(0, total | 0);
  if (n <= 0) return 0;
  const start = listShellPageStart(cursor, ps);
  if (dir < 0) return Math.max(0, start - ps);
  return Math.min(n - 1, start + ps);
}

export function paintListShellChrome(
  ctx: ListShellCtx,
  title: string,
  fill: number = CBE.bgDeep,
  bounds: ListShellBounds = listShellBounds(),
): ListShellBounds {
  const { x, y, w, h } = bounds;
  paintCbePanel(ctx.host, ctx.box, x, y, w, h, fill);
  const pillW = Math.min(120, Math.max(56, title.length * 12 + 24));
  const pillH = 20;
  drawCbeTitlePill(ctx.box, LCD_WIDTH / 2, y + 6, pillW, pillH);
  if (ctx.addLabelCentered) {
    ctx.addLabelCentered(title, LCD_WIDTH / 2, y + 6 + pillH / 2, CBE.ink);
  } else {
    const tw = measureUiTextWidth(title);
    ctx.addLabel(
      title,
      Math.round(LCD_WIDTH / 2 - tw / 2),
      y + 6 + Math.floor((pillH - UC3_CELL) / 2),
      CBE.ink,
    );
  }
  return bounds;
}

/** Generic equal-width tab strip. Returns Y just below the tabs. */
export function paintListShellTabs(
  ctx: ListShellCtx,
  x: number,
  y: number,
  w: number,
  h: number,
  tabs: string[],
  active: number,
  onPick: (idx: number) => void,
): number {
  const n = tabs.length;
  if (n <= 0) return y;
  const gap = 2;
  const tw = Math.floor((w - gap * (n - 1)) / n);
  for (let i = 0; i < n; i++) {
    const label = tabs[i] || `页${i}`;
    const tx = x + i * (tw + gap);
    const isActive = i === active;
    ctx.box
      .roundRect(tx, y, tw, h, 2)
      .fill({ color: isActive ? CBE.bgPanelDark : LIST_SHELL_TAB_BG })
      .stroke({
        width: 1,
        color: isActive ? CBE.borderBright : CBE.borderDim,
      });
    const text = isActive ? label.slice(0, 4) : label.slice(0, 2);
    if (ctx.addLabelCentered) {
      ctx.addLabelCentered(
        text,
        tx + tw / 2,
        y + h / 2,
        isActive ? CBE.ink : CBE.inkMint,
      );
    } else {
      ctx.addLabel(text, tx + 4, y + 3, isActive ? CBE.ink : CBE.inkMint);
    }
    const idx = i;
    ctx.hits.push(
      attachRowPointerHit(ctx.host, tx, y, tw, h, () => onPick(idx)),
    );
  }
  return y + h;
}

export type PaintListShellBodyOpts = {
  ctx: ListShellCtx;
  x: number;
  listTop: number;
  w: number;
  footY: number;
  rows: ListShellRow[];
  cursor: number;
  pageSize: number;
  rowH?: number;
  dualLine?: boolean;
  emptyText?: string;
  onRow: (absIndex: number) => void;
  /** Override default name/subtitle char caps (CBE LCD width). */
  nameMax?: number;
  subtitleMax?: number;
  /**
   * local = slice by cursor/pageSize
   * server = rows are already one server page
   * none = draw all rows, no page nav
   */
  paging?: "local" | "server" | "none";
  serverPage?: number;
  serverPages?: number;
  onPrevPage?: () => void;
  onNextPage?: () => void;
  footerHint?: string;
};

export type PaintListShellBodyResult = {
  page: number;
  pages: number;
  pageStart: number;
  navH: number;
};

export function paintListShellBody(
  opts: PaintListShellBodyOpts,
): PaintListShellBodyResult {
  const {
    ctx,
    x,
    listTop,
    w,
    footY,
    rows,
    cursor,
    dualLine = false,
    emptyText = "(空)",
    onRow,
  } = opts;
  const paging = opts.paging ?? "local";
  const pageSize = Math.max(1, opts.pageSize | 0);
  const rowH = opts.rowH ?? (dualLine ? LIST_DUAL_LINE_ROW_H : 20);
  const dualColMax = Math.max(6, Math.floor((w - 24) / UC3_CELL));
  const fullNameMax = Math.max(1, opts.nameMax ?? UI_WRAP_CHARS);
  const fullSubMax = Math.max(1, opts.subtitleMax ?? UI_WRAP_CHARS);
  const splitX = listShellSplitX(x);
  const nameColMax = listShellColMax(x, w, "name", opts.nameMax);
  const subtitleColMax = listShellColMax(x, w, "subtitle", opts.subtitleMax);

  let page = 1;
  let pages = 1;
  let pageStart = 0;
  let shown = rows;

  if (paging === "local") {
    const info = listShellPageInfo(rows.length, cursor, pageSize);
    page = info.page;
    pages = info.pages;
    pageStart = info.pageStart;
    shown = rows.slice(pageStart, pageStart + pageSize);
  } else if (paging === "server") {
    page = Math.max(1, opts.serverPage ?? 1);
    pages = Math.max(1, opts.serverPages ?? 1);
    pageStart = 0;
    shown = rows;
  }

  const navReserve = pages > 1 ? 22 : 0;
  const hintReserve = opts.footerHint ? 14 : 0;
  const listBottom = footY - navReserve - hintReserve;

  if (!shown.length) {
    ctx.addLabel(emptyText, x + 10, listTop + 8, CBE.inkMint);
  } else {
    const localCursor =
      paging === "local"
        ? cursor - pageStart
        : cursor < 0
          ? -1
          : Math.min(Math.max(0, cursor), shown.length - 1);
    let listY = listTop;
    shown.forEach((row, i) => {
      const abs = pageStart + i;
      const hi = i === localCursor;
      const nameMax = dualLine
        ? opts.nameMax ?? dualColMax
        : row.subtitle
          ? nameColMax
          : fullNameMax;
      const subtitleMax = dualLine
        ? opts.subtitleMax ?? dualColMax
        : subtitleColMax;
      const nameText = clipListCol(row.name, nameMax);
      const subText = row.subtitle ? clipListCol(row.subtitle, subtitleMax) : "";
      const effRowH = dualLine && !subText ? 20 : rowH;
      if (listY + effRowH > listBottom + 4) return;
      if (hi) {
        ctx.box.rect(x + 6, listY, w - 12, effRowH - 2).fill({ color: CBE.select });
      }
      const nameCol = hi ? CBE.ink : (row.nameColor ?? CBE.inkMint);
      const padX = x + 10;
      ctx.addLabel(nameText, padX, listY + (dualLine ? DUAL_LINE_NAME_Y : 2), nameCol);
      if (subText) {
        if (dualLine) {
          ctx.addLabel(
            subText,
            padX + 2,
            listY + DUAL_LINE_SUB_Y,
            row.subtitleColor ?? (hi ? CBE.inkMint : CBE.inkLabel),
          );
        } else {
          ctx.addLabel(
            subText,
            splitX,
            listY + 2,
            row.subtitleColor ?? CBE.inkLabel,
          );
        }
      }
      if (dualLine && subText && i < shown.length - 1) {
        ctx.box
          .moveTo(x + 8, listY + effRowH - 1)
          .lineTo(x + w - 8, listY + effRowH - 1)
          .stroke({ width: 1, color: CBE.borderDim, alpha: 0.35 });
      }
      ctx.hits.push(
        attachRowPointerHit(ctx.host, x + 6, listY, w - 12, effRowH - 2, () =>
          onRow(abs),
        ),
      );
      listY += effRowH;
    });
  }

  let navH = 0;
  if (pages > 1 && (opts.onPrevPage || opts.onNextPage)) {
    navH = attachPageNavButtons({
      host: ctx.host,
      hits: ctx.hits,
      box: ctx.box,
      x: x + 8,
      y: footY - (opts.footerHint ? 22 : 2),
      w: w - 16,
      page,
      pages,
      addLabel: ctx.addLabel,
      onPrev: () => opts.onPrevPage?.(),
      onNext: () => opts.onNextPage?.(),
    });
  }

  if (opts.footerHint) {
    ctx.addLabel(opts.footerHint, x + 10, footY, CBE.inkLabel);
  }

  return { page, pages, pageStart, navH };
}

/** Single-line clip for list columns — prevents two-column bleed / overlap. */
function clipToByteBudget(text: string, maxBytes: number): string {
  let bytes = 0;
  let out = "";
  for (const ch of text) {
    const bw = uiCharByteWidth(ch);
    if (bytes + bw > maxBytes) break;
    out += ch;
    bytes += bw;
  }
  return out;
}

function clipListCol(text: string, maxCols: number): string {
  const maxBytes = Math.max(2, maxCols * 2);
  const lines = wrapChars(text, maxCols);
  const first = (lines[0] ?? "").trimEnd();
  const truncated =
    lines.length > 1 || measureUiTextBytes(text) > measureUiTextBytes(first);
  if (!truncated) return first;
  const ellipsisBytes = uiCharByteWidth("…");
  if (maxBytes <= ellipsisBytes) return clipToByteBudget(text, maxBytes);
  return `${clipToByteBudget(text, maxBytes - ellipsisBytes)}…`;
}

function listShellSplitX(x: number): number {
  return x + 100;
}

function listShellColMax(
  x: number,
  w: number,
  side: "name" | "subtitle",
  override?: number,
): number {
  if (override != null) return Math.max(1, override | 0);
  const splitX = listShellSplitX(x);
  const px =
    side === "name"
      ? splitX - (x + 10) - 2
      : x + w - 10 - splitX;
  return Math.max(4, Math.floor(px / UC3_CELL));
}
