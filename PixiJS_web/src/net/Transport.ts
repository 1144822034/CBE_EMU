import {
  CBMS_FLAG_DISCONNECT,
  CBMS_FLAG_PING,
  CBMS_FLAG_SCENE_POLL,
  CBMR_FLAG_HAS_FOLLOWUP,
  decodeCbmr,
  encodeCbms,
  encodeRequestMeta,
  type CbmrFrame,
} from "./cbms";

/** Live gateway on this machine's LAN IP (PixiJS web / APK). */
//export const FIXED_LIVE_GATEWAY = "ws://116.211.150.66:19093";
export const FIXED_LIVE_GATEWAY = "ws://192.1.1.1:19092";

export type TransportMode = "live";

export interface GameTransport {
  readonly mode: TransportMode;
  connect(): Promise<void>;
  close(): void;
  /** Send CBMS and collect one or more CBMR bodies (WT bytes). */
  exchange(flags: number, wtBody: Uint8Array): Promise<Uint8Array[]>;
}

/**
 * Live WS gateway URL.
 * Priority: `?gateway=` / `localStorage.pix_gateway` (zone pick) /
 * FIXED_LIVE_GATEWAY (bootstrap + catalog-empty fallback).
 */
export function defaultLiveGatewayUrl(): string {
  if (typeof window === "undefined") return FIXED_LIVE_GATEWAY;
  const params = new URLSearchParams(window.location.search);
  const fromQuery = params.get("gateway") || params.get("ws");
  if (fromQuery) return fromQuery;
  try {
    const stored = window.localStorage.getItem("pix_gateway");
    if (stored) return stored;
  } catch {
    /* private mode */
  }
  return FIXED_LIVE_GATEWAY;
}

export {
  defaultZoneCatalogUrl,
  loadGatewayZoneCatalog,
  applyGatewayZone,
  resolveGatewayZoneSelection,
  readStoredZoneId,
  type GatewayZoneRow,
  type GatewayZoneCatalog,
} from "./zones";


/** Live WS ↔ TCP gateway (`npm run gateway` on the game host). */
export class LiveTransport implements GameTransport {
  readonly mode = "live" as const;
  private ws: WebSocket | null = null;
  private connectPromise: Promise<void> | null = null;
  private exchangeChain: Promise<void> = Promise.resolve();
  /**
   * Early responses for the in-flight exchange only.
   * Gateway keepalive pushes must NOT land here (would steal the next move/login).
   */
  private inbox: CbmrFrame[] = [];
  private waiters: Array<{
    resolve: (f: CbmrFrame) => void;
    reject: (e: Error) => void;
    timer: number;
  }> = [];
  /** True between send and finished read of one exchange. */
  private exchangeInFlight = false;
  /**
   * Unsolicited CBMR (gateway idle scene-poll). GameSession applies via handleBodies.
   */
  onPushBodies: ((bodies: Uint8Array[]) => void) | null = null;
  clientId = ((Math.random() * 0xffffff) >>> 0) || 1;
  constructor(private readonly url = defaultLiveGatewayUrl()) {}

  async connect(): Promise<void> {
    if (this.ws?.readyState === WebSocket.OPEN) return;
    if (this.connectPromise) return this.connectPromise;
    this.connectPromise = new Promise<void>((resolve, reject) => {
      const ws = new WebSocket(this.url);
      ws.binaryType = "arraybuffer";
      const timer = window.setTimeout(() => {
        ws.close();
        this.connectPromise = null;
        reject(new Error(`gateway timeout ${this.url}`));
      }, 8000);
      ws.onopen = () => {
        window.clearTimeout(timer);
        this.ws = ws;
        this.bindSocket(ws);
        this.connectPromise = null;
        resolve();
      };
      ws.onerror = () => {
        window.clearTimeout(timer);
        this.connectPromise = null;
        reject(new Error(`gateway failed ${this.url}`));
      };
      ws.onclose = () => {
        if (this.ws === ws) this.ws = null;
        this.rejectWaiters(new Error("socket closed"));
      };
    });
    return this.connectPromise;
  }

  private bindSocket(ws: WebSocket): void {
    ws.addEventListener("message", (ev) => {
      try {
        const frame = decodeCbmr(new Uint8Array(ev.data as ArrayBuffer));
        const waiter = this.waiters.shift();
        if (waiter) {
          window.clearTimeout(waiter.timer);
          waiter.resolve(frame);
        } else if (this.exchangeInFlight) {
          // Response raced ahead of waiter registration.
          this.inbox.push(frame);
        } else {
          // Gateway keepalive (or other push) — do not poison exchange inbox.
          try {
            this.onPushBodies?.([frame.body]);
          } catch (e) {
            console.warn("[live] onPushBodies", e);
          }
        }
      } catch (e) {
        const waiter = this.waiters.shift();
        if (waiter) {
          window.clearTimeout(waiter.timer);
          waiter.reject(e instanceof Error ? e : new Error(String(e)));
        }
      }
    });
    ws.addEventListener("error", () => {
      this.rejectWaiters(new Error("socket error"));
    });
  }

  private rejectWaiters(err: Error): void {
    const pending = this.waiters.splice(0, this.waiters.length);
    for (const w of pending) {
      window.clearTimeout(w.timer);
      w.reject(err);
    }
  }

  /** Re-open gateway WS if it dropped (keeps the same clientId). */
  async ensureConnected(): Promise<void> {
    if (this.ws?.readyState === WebSocket.OPEN) return;
    this.ws = null;
    this.inbox = [];
    this.rejectWaiters(new Error("socket closed"));
    await this.connect();
  }

  close(): void {
    try {
      if (this.ws?.readyState === WebSocket.OPEN) {
        const meta = encodeRequestMeta(this.clientId);
        this.ws.send(encodeCbms(CBMS_FLAG_DISCONNECT, meta, new Uint8Array(0)));
      }
    } catch {
      /* ignore */
    }
    this.rejectWaiters(new Error("socket closed"));
    this.inbox = [];
    this.onPushBodies = null;
    this.ws?.close();
    this.ws = null;
  }

  async exchange(flags: number, wtBody: Uint8Array): Promise<Uint8Array[]> {
    const run = this.exchangeChain.then(() => this.exchangeUnlocked(flags, wtBody));
    this.exchangeChain = run.then(
      () => undefined,
      () => undefined,
    );
    return run;
  }

  private async exchangeUnlocked(
    flags: number,
    wtBody: Uint8Array,
  ): Promise<Uint8Array[]> {
    await this.ensureConnected();
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      throw new Error(`live: not connected（网关未就绪 ${this.url}）`);
    }
    const meta = encodeRequestMeta(this.clientId);
    const req = encodeCbms(flags, meta, wtBody);
    this.exchangeInFlight = true;
    try {
      this.ws.send(req);

      const frames: CbmrFrame[] = [];
      const first = await this.readOne(8000);
      frames.push(first);
      let guard = 0;
      let more = (first.flags & CBMR_FLAG_HAS_FOLLOWUP) !== 0;
      while (more && guard++ < 8) {
        try {
          const next = await this.readOne(600);
          frames.push(next);
          more = (next.flags & CBMR_FLAG_HAS_FOLLOWUP) !== 0;
        } catch {
          break;
        }
      }
      return frames.map((f) => f.body);
    } finally {
      this.exchangeInFlight = false;
    }
  }

  private readOne(timeoutMs: number): Promise<CbmrFrame> {
    if (this.inbox.length) {
      return Promise.resolve(this.inbox.shift()!);
    }
    if (!this.ws) {
      return Promise.reject(new Error("no socket"));
    }
    return new Promise<CbmrFrame>((resolve, reject) => {
      const timer = window.setTimeout(() => {
        const idx = this.waiters.findIndex((w) => w.timer === timer);
        if (idx >= 0) this.waiters.splice(idx, 1);
        reject(new Error("read timeout"));
      }, timeoutMs);
      this.waiters.push({ resolve, reject, timer });
    });
  }

  async ping(): Promise<void> {
    await this.exchange(CBMS_FLAG_PING, new Uint8Array(0));
  }

  async poll(): Promise<Uint8Array[]> {
    return this.exchange(CBMS_FLAG_SCENE_POLL, new Uint8Array(0));
  }
}

/** Connect Live only — no Demo fallback. */
export async function connectLiveTransport(
  url = defaultLiveGatewayUrl(),
): Promise<LiveTransport> {
  const live = new LiveTransport(url);
  try {
    await live.connect();
    await live.ping();
    return live;
  } catch (e) {
    live.close();
    const msg = e instanceof Error ? e.message : String(e);
    throw new Error(`实网连接失败（${url}）: ${msg}`);
  }
}

/** @deprecated use connectLiveTransport */
export async function connectPreferredTransport(
  _preferLive = true,
  _allowDemoFallback = false,
): Promise<GameTransport> {
  return connectLiveTransport();
}

export { CBMS_FLAG_SCENE_POLL };
