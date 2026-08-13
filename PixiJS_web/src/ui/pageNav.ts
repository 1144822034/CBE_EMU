import { Container, Graphics } from "pixi.js";
import { CBE } from "./constants";
import { attachRowPointerHit } from "./pointerHit";

export type PageNavDrawOpts = {
  host: Container;
  hits: Container[];
  box: Graphics;
  /** Left edge of the nav strip. */
  x: number;
  y: number;
  /** Total width for 上一页 | 下一页. */
  w: number;
  /** 1-based current page. */
  page: number;
  pages: number;
  addLabel: (text: string, x: number, y: number, color: number) => void;
  onPrev: () => void;
  onNext: () => void;
  /** Optional height (default 18). */
  h?: number;
};

/**
 * Draw tappable 上一页 / 下一页 when pages > 1.
 * Returns strip height (0 if not drawn).
 */
export function attachPageNavButtons(opts: PageNavDrawOpts): number {
  const pages = Math.max(1, opts.pages | 0);
  if (pages <= 1) return 0;
  const page = Math.min(Math.max(1, opts.page | 0), pages);
  const h = opts.h ?? 18;
  const gap = 4;
  const btnW = Math.max(48, Math.floor((opts.w - gap) / 2));
  const leftX = opts.x;
  const rightX = opts.x + btnW + gap;
  const y = opts.y;

  const prevOn = page > 1;
  opts.box
    .roundRect(leftX, y, btnW, h, 2)
    .fill({ color: prevOn ? CBE.bgPanelDark : 0x121c18 })
    .stroke({ width: 1, color: prevOn ? CBE.borderBright : CBE.borderDim });
  opts.addLabel("上一页", leftX + 8, y + 3, prevOn ? CBE.inkMint : 0x5a6a62);
  if (prevOn) {
    opts.hits.push(
      attachRowPointerHit(opts.host, leftX, y, btnW, h, opts.onPrev),
    );
  }

  const nextOn = page < pages;
  opts.box
    .roundRect(rightX, y, btnW, h, 2)
    .fill({ color: nextOn ? CBE.bgPanelDark : 0x121c18 })
    .stroke({ width: 1, color: nextOn ? CBE.borderBright : CBE.borderDim });
  opts.addLabel(
    `下一页 ${page}/${pages}`,
    rightX + 6,
    y + 3,
    nextOn ? CBE.inkMint : 0x5a6a62,
  );
  if (nextOn) {
    opts.hits.push(
      attachRowPointerHit(opts.host, rightX, y, btnW, h, opts.onNext),
    );
  }
  return h;
}
