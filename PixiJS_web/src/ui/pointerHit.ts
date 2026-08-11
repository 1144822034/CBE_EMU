import { Container, Rectangle } from "pixi.js";

/**
 * Transparent full-row hit target for softkey-style lists.
 * Pattern: set cursor → redraw (optional) → confirm() — same path as OK/left softkey.
 */
export function attachRowPointerHit(
  host: Container,
  x: number,
  y: number,
  w: number,
  h: number,
  onTap: () => void,
): Container {
  const hit = new Container();
  hit.position.set(x, y);
  hit.hitArea = new Rectangle(0, 0, Math.max(1, w), Math.max(1, h));
  hit.eventMode = "static";
  hit.cursor = "pointer";
  hit.on("pointertap", (e) => {
    e.stopPropagation();
    onTap();
  });
  host.addChild(hit);
  return hit;
}

/** Destroy previous row hits before redraw. */
export function clearPointerHits(hits: Container[]): void {
  for (const h of hits) {
    h.destroy({ children: true });
  }
  hits.length = 0;
}
