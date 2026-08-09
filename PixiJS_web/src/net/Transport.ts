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
//export const FIXED_LIVE_GATEWAY = "ws://47.108.210.191:19092";
export const FIXED_LIVE_GATEWAY = "ws://192.1.1.2:19092";

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
 * Default is fixed production host; `?gateway=` / `localStorage.pix_gateway`
 * remain debug overrides only.
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

/** Live WS ↔ TCP gateway (`npm run gateway` on the game host). */
export class LiveTransport implements GameTransport {
  readonly mode = "live" as const;
  private ws: WebSocket | null = null;
  private connectPromise: Promise<void> | null = null;
  private exchangeChain: Promise<void> = Promise.resolve();
  clientId = (Math.random() * 0xffffff) >>> 0;
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
      };
    });
    return this.connectPromise;
  }

  /** Re-open gateway WS if it dropped (keeps the same clientId). */
  async ensureConnected(): Promise<void> {
    if (this.ws?.readyState === WebSocket.OPEN) return;
    this.ws = null;
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
  }

  private readOne(timeoutMs: number): Promise<CbmrFrame> {
    return new Promise((resolve, reject) => {
      if (!this.ws) {
        reject(new Error("no socket"));
        return;
      }
      const ws = this.ws;
      const timer = window.setTimeout(() => {
        cleanup();
        reject(new Error("read timeout"));
      }, timeoutMs);
      const onMsg = (ev: MessageEvent) => {
        cleanup();
        try {
          resolve(decodeCbmr(new Uint8Array(ev.data as ArrayBuffer)));
        } catch (e) {
          reject(e);
        }
      };
      const onErr = () => {
        cleanup();
        reject(new Error("socket error"));
      };
      const onClose = () => {
        cleanup();
        reject(new Error("socket closed"));
      };
      const cleanup = () => {
        window.clearTimeout(timer);
        ws.removeEventListener("message", onMsg);
        ws.removeEventListener("error", onErr);
        ws.removeEventListener("close", onClose);
      };
      ws.addEventListener("message", onMsg);
      ws.addEventListener("error", onErr);
      ws.addEventListener("close", onClose);
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
