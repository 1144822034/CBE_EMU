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
  const why = input.why.toLowerCase();
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
        v: 1,
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
