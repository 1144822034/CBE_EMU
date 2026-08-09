import {
  CBMS_FLAG_PING,
  decodeCbmr,
  encodeCbms,
  encodeRequestMeta,
  type CbmrFrame,
} from "./cbms";

/**
 * Optional WebSocket bridge to mock-server (:19090 via scripts/ws-gateway.mjs).
 * Offline map viewer does not require this.
 */
export class NetClient {
  private ws: WebSocket | null = null;
  clientId = (Math.random() * 0xffffff) >>> 0;

  get connected(): boolean {
    return this.ws?.readyState === WebSocket.OPEN;
  }

  async connect(url = "ws://127.0.0.1:19092"): Promise<void> {
    if (this.ws) this.ws.close();
    await new Promise<void>((resolve, reject) => {
      const ws = new WebSocket(url);
      ws.binaryType = "arraybuffer";
      ws.onopen = () => {
        this.ws = ws;
        resolve();
      };
      ws.onerror = () => reject(new Error(`websocket failed: ${url}`));
    });
  }

  disconnect(): void {
    this.ws?.close();
    this.ws = null;
  }

  async ping(): Promise<CbmrFrame> {
    return this.exchange(CBMS_FLAG_PING, new Uint8Array(0));
  }

  async exchange(flags: number, wtBody: Uint8Array): Promise<CbmrFrame> {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      throw new Error("net: not connected");
    }
    const meta = encodeRequestMeta(this.clientId);
    const req = encodeCbms(flags, meta, wtBody);
    this.ws.send(req);
    const buf = await this.readOne();
    return decodeCbmr(new Uint8Array(buf));
  }

  private readOne(): Promise<ArrayBuffer> {
    return new Promise((resolve, reject) => {
      if (!this.ws) {
        reject(new Error("net: no socket"));
        return;
      }
      const onMsg = (ev: MessageEvent) => {
        cleanup();
        resolve(ev.data as ArrayBuffer);
      };
      const onErr = () => {
        cleanup();
        reject(new Error("net: socket error"));
      };
      const cleanup = () => {
        this.ws?.removeEventListener("message", onMsg);
        this.ws?.removeEventListener("error", onErr);
      };
      this.ws.addEventListener("message", onMsg);
      this.ws.addEventListener("error", onErr);
    });
  }
}
