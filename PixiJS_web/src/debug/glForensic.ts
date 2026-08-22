/**
 * Observe-only black-screen forensics (LCD blank, HTML D-pad still up).
 * Ring + class tags; does not change recover / TextureGC / resource policy.
 */

export type ForensicClass =
  | "CTX_LOST"
  | "BLANK_ALIVE"
  | "EMPTY_SCENE"
  | "PHASE_HIDE"
  | "BATTLE_BLACK"
  | "RENDER_CRASH"
  | "RENDER_STALL"
  | "UNKNOWN"
  | "OK";

export interface ForensicClassifyInput {
  why: string;
  phase: string;
  glLost: boolean;
  blank: boolean | null;
  viewRootVisible: boolean;
  battleVisible: boolean;
  mapW: number;
  mapH: number;
  hasMapSprite: boolean;
  /** When set, wins over heuristic (crash/stall injectors). */
  classOverride?: ForensicClass;
}

export interface ForensicEvent {
  t: number;
  iso: string;
  why: string;
  class: ForensicClass;
  blank: boolean | null;
  glLost: boolean;
  payload: Record<string, unknown>;
}

export interface BrokenTexHit {
  path: string;
  kind: string;
  label: string;
  visible: boolean;
  texUid?: number;
  texLabel?: string;
  sourceNull: boolean;
  sourceDestroyed: boolean;
  urlHint: string;
}

const RING_CAP = 80;
const OK_HEARTBEAT_KEEP_EVERY = 6;

/** Phases where the map band (or battle LCD) should show game pixels. */
export function isGameDisplayPhase(phase: string): boolean {
  return (
    phase === "world" ||
    phase === "menu" ||
    phase === "dialog" ||
    phase === "system" ||
    phase === "enhance" ||
    phase === "wash" ||
    phase === "social" ||
    phase === "quest" ||
    phase === "chat" ||
    phase === "trade" ||
    phase === "guild" ||
    phase === "map" ||
    phase === "battle"
  );
}

export function classifyForensic(input: ForensicClassifyInput): ForensicClass {
  if (input.classOverride) return input.classOverride;

  const why = input.why.toLowerCase();
  if (
    why.includes("render-crash") ||
    why.includes("alphamode") ||
    why.includes("texturesource")
  ) {
    return "RENDER_CRASH";
  }
  if (why.includes("render-stall") || why.startsWith("stall:")) {
    return "RENDER_STALL";
  }
  if (
    input.glLost ||
    why.includes("contextlost") ||
    why === "webglcontextlost"
  ) {
    return "CTX_LOST";
  }

  const worldish =
    input.phase !== "battle" && isGameDisplayPhase(input.phase);
  if (
    worldish &&
    input.viewRootVisible &&
    (!input.hasMapSprite || input.mapW <= 0 || input.mapH <= 0)
  ) {
    return "EMPTY_SCENE";
  }

  if (
    (worldish && !input.viewRootVisible) ||
    (input.phase === "battle" && !input.battleVisible)
  ) {
    return "PHASE_HIDE";
  }

  if (input.blank === true) {
    if (input.phase === "battle") return "BATTLE_BLACK";
    if (!input.glLost) return "BLANK_ALIVE";
    return "UNKNOWN";
  }

  return "OK";
}

function okPayloadKey(payload: Record<string, unknown>): string {
  return [
    payload.phase,
    payload.viewRootVisible,
    payload.battleVisible,
    payload.mapIdentity,
    payload.hasMapSprite,
    payload.mapW,
    payload.mapH,
    payload.peers,
    payload.peersWithCos,
    payload.plist,
    payload.msSinceOkRender,
  ].join("|");
}

export class GlForensicRing {
  private readonly events: ForensicEvent[] = [];
  private okHeartbeatSeq = 0;
  private lastOkKey = "";

  push(ev: ForensicEvent): void {
    if (ev.class === "OK" && ev.why.startsWith("heartbeat")) {
      this.okHeartbeatSeq += 1;
      const key = okPayloadKey(ev.payload);
      const changed = key !== this.lastOkKey;
      this.lastOkKey = key;
      if (!changed && this.okHeartbeatSeq % OK_HEARTBEAT_KEEP_EVERY !== 0) {
        return;
      }
    } else if (ev.class !== "OK") {
      this.okHeartbeatSeq = 0;
    }
    this.events.push(ev);
    while (this.events.length > RING_CAP) this.events.shift();
  }

  snapshot(): ForensicEvent[] {
    return this.events.slice();
  }

  dumpJson(): string {
    return JSON.stringify(
      {
        v: 2,
        n: this.events.length,
        events: this.events,
      },
      null,
      2,
    );
  }
}

export function isAlertForensicClass(c: ForensicClass): boolean {
  return c !== "OK";
}

export async function copyForensicText(text: string): Promise<boolean> {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch {
    return false;
  }
}

function urlHintFromTex(tex: {
  label?: string;
  source?: {
    label?: string;
    resource?: unknown;
    _source?: unknown;
  } | null;
}): string {
  const src = tex.source;
  const bits = [
    tex.label,
    src?.label,
    typeof (src as { resource?: { src?: string } } | null)?.resource?.src ===
    "string"
      ? (src as { resource: { src: string } }).resource.src
      : "",
  ].filter(Boolean);
  const s = bits.join("|");
  if (!s) return "";
  const leaf = s.replace(/^.*[/\\]/, "");
  return leaf.slice(0, 96);
}

type WalkNode = {
  label?: string;
  visible?: boolean;
  children?: WalkNode[];
  texture?: {
    uid?: number;
    label?: string;
    destroyed?: boolean;
    source?: { destroyed?: boolean; label?: string; resource?: unknown } | null;
  };
  constructor?: { name?: string };
};

/**
 * Find sprites/meshes still in the tree whose texture.source is null/destroyed
 * (typical cause of Batcher alphaMode TypeError → frozen LCD).
 */
export function scanBrokenTextures(
  root: WalkNode | null | undefined,
  maxHits = 24,
): BrokenTexHit[] {
  const hits: BrokenTexHit[] = [];
  if (!root) return hits;

  const walk = (node: WalkNode, path: string, depth: number): void => {
    if (hits.length >= maxHits || depth > 48) return;
    const kind = node.constructor?.name || "?";
    const label = String(node.label || "");
    const tex = kind.includes("Graphics") ? undefined : node.texture;
    if (tex) {
      let sourceNull = false;
      let sourceDestroyed = false;
      try {
        const src = tex.source;
        sourceNull = src == null;
        sourceDestroyed = !!(tex.destroyed || src?.destroyed);
      } catch {
        sourceNull = true;
      }
      if (sourceNull || sourceDestroyed) {
        hits.push({
          path,
          kind,
          label,
          visible: node.visible !== false,
          texUid: tex.uid,
          texLabel: tex.label,
          sourceNull,
          sourceDestroyed,
          urlHint: urlHintFromTex(tex),
        });
      }
    }
    const kids = node.children;
    if (!kids?.length) return;
    for (let i = 0; i < kids.length; i++) {
      const ch = kids[i];
      if (!ch) continue;
      const cn = ch.constructor?.name || "?";
      const cl = ch.label ? `(${ch.label})` : "";
      walk(ch, `${path}/${i}:${cn}${cl}`, depth + 1);
    }
  };

  walk(root, "stage", 0);
  return hits;
}

export function isTextureSourceNullError(err: unknown): boolean {
  const msg = err instanceof Error ? err.message : String(err ?? "");
  return (
    /alphaMode/i.test(msg) ||
    /textureSource/i.test(msg) ||
    /Cannot read properties of null/i.test(msg)
  );
}
