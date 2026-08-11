/**
 * Minimal WebSocket ↔ TCP CBMS bridge (+ HTTP proxy for monster levels).
 * Usage:
 *   node scripts/ws-gateway.mjs [listenWsPort=19092] [tcpHost=127.0.0.1] [tcpPort=19090] [listenHost=0.0.0.0]
 * Env:
 *   PIX_GATEWAY_HOST / PIX_GATEWAY_PORT — listen bind (default 0.0.0.0:19092)
 *   PIX_TCP_HOST / PIX_TCP_PORT — mock-server (default 127.0.0.1:19090)
 *   PIX_ADMIN_HOST / PIX_ADMIN_PORT — admin HTTP for /api/* proxy
 *     (monster-levels, recharge/create, register; default same host as TCP, port 19091)
 *   PIX_GATEWAY_KEEPALIVE_MS — idle scene-poll interval (default 10000; 0=off)
 *   PIX_GATEWAY_KEEPALIVE_IDLE_MS — silence before synth poll (default 2200)
 *   PIX_GATEWAY_ORPHAN_MS — after WS drop (no DISCONNECT), keep TCP scene-poll
 *     for this clientId so mock presence does not heartbeat-timeout (default
 *     600000 = 10min; 0=off). Client LiveTransport reconnects with same id.
 *
 * Each CBMS request opens a short-lived TCP to the mock service (CBE host model).
 * Do NOT close the WebSocket on a single TCP failure — the browser session must
 * stay up so the client can retry login/select/role without "live: not connected".
 *
 * After the client arms SCENE_POLL (in-world), this process can run a Node
 * interval and synthesize idle scene-polls so Android WebView timer freezes do
 * not cause server heartbeat-timeout. See docs/re/2026-08-10-pixi-gateway-idle-keepalive.md.
 */
import http from "node:http";
import net from "node:net";
import os from "node:os";
import { WebSocketServer } from "ws";

const listenPort = Number(
  process.env.PIX_GATEWAY_PORT || process.argv[2] || 19092,
);
const tcpHost = process.env.PIX_TCP_HOST || process.argv[3] || "127.0.0.1";
const tcpPort = Number(process.env.PIX_TCP_PORT || process.argv[4] || 19090);
const listenHost =
  process.env.PIX_GATEWAY_HOST || process.argv[5] || "0.0.0.0";
const adminHost = process.env.PIX_ADMIN_HOST || tcpHost || "127.0.0.1";
const adminPort = Number(process.env.PIX_ADMIN_PORT || 19091);

async function exchangeOnce(reqBuf) {
  const socket = net.connect({ host: tcpHost, port: tcpPort });
  await new Promise((resolve, reject) => {
    socket.once("connect", resolve);
    socket.once("error", reject);
  });

  // Continuous buffer — do not attach one-shot `data` listeners inside pull();
  // localhost CBMR can arrive before the listener is registered and be lost.
  let pending = Buffer.alloc(0);
  let wait = null;
  let ended = null;

  const wake = () => {
    if (!wait) return;
    if (ended) {
      const { reject } = wait;
      wait = null;
      reject(ended);
      return;
    }
    if (pending.length >= wait.n) {
      const { n, resolve } = wait;
      wait = null;
      const out = pending.subarray(0, n);
      pending = pending.subarray(n);
      resolve(out);
    }
  };

  socket.on("data", (buf) => {
    pending = Buffer.concat([pending, buf]);
    wake();
  });
  socket.on("error", (e) => {
    ended = e instanceof Error ? e : new Error(String(e));
    wake();
  });
  socket.on("end", () => {
    ended = ended || new Error("tcp ended early");
    wake();
  });

  const pull = (n) =>
    new Promise((resolve, reject) => {
      if (pending.length >= n) {
        const out = pending.subarray(0, n);
        pending = pending.subarray(n);
        resolve(out);
        return;
      }
      if (ended) {
        reject(ended);
        return;
      }
      wait = { n, resolve, reject };
    });

  const cancelPull = (err) => {
    if (!wait) return;
    const { reject } = wait;
    wait = null;
    reject(err instanceof Error ? err : new Error(String(err)));
  };

  socket.write(reqBuf);

  const header = await pull(20);
  if (header.toString("ascii", 0, 4) !== "CBMR") {
    socket.destroy();
    throw new Error("not CBMR");
  }
  const bodyLen = header.readUInt32LE(12);
  const body = bodyLen ? await pull(bodyLen) : Buffer.alloc(0);
  const flags = header.readUInt32LE(8);
  const frames = [Buffer.concat([header, body])];

  // HAS_FOLLOWUP = 0x2 — read one more CBMR if present (best-effort short timeout).
  // Cancel the same pull waiter on timeout (no orphaned Promise.race loser).
  if (flags & 0x2) {
    const onTimeout = () => cancelPull(new Error("followup timeout"));
    try {
      socket.setTimeout(800);
      socket.once("timeout", onTimeout);
      const h2 = await pull(20);
      const l2 = h2.readUInt32LE(12);
      const b2 = l2 ? await pull(l2) : Buffer.alloc(0);
      frames.push(Buffer.concat([h2, b2]));
    } catch {
      /* ignore */
    } finally {
      socket.setTimeout(0);
      socket.off("timeout", onTimeout);
    }
  }
  socket.destroy();
  return frames;
}

function lanIpv4s() {
  const out = [];
  for (const list of Object.values(os.networkInterfaces())) {
    for (const info of list || []) {
      if (info.family === "IPv4" && !info.internal) out.push(info.address);
    }
  }
  return out;
}

function sendJson(res, status, body, extraHeaders = {}) {
  const payload = typeof body === "string" ? body : JSON.stringify(body);
  res.writeHead(status, {
    "Content-Type": "application/json; charset=utf-8",
    "Content-Length": Buffer.byteLength(payload),
    "Access-Control-Allow-Origin": "*",
    "Cache-Control": "no-store",
    ...extraHeaders,
  });
  res.end(payload);
}

function readRequestBody(req, limit = 64 * 1024) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let total = 0;
    req.on("data", (c) => {
      total += c.length;
      if (total > limit) {
        reject(new Error("body-too-large"));
        req.destroy();
        return;
      }
      chunks.push(c);
    });
    req.on("end", () => resolve(Buffer.concat(chunks)));
    req.on("error", reject);
  });
}

/** Proxy admin HTTP API paths used by Pixi (CORS + body forward). */
function proxyAdminApi(req, res, adminPath, { methods }) {
  const allow = methods.join(", ");
  if (req.method === "OPTIONS") {
    res.writeHead(204, {
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Methods": `${allow}, OPTIONS`,
      "Access-Control-Allow-Headers": "Content-Type",
      "Access-Control-Max-Age": "86400",
    });
    res.end();
    return;
  }
  if (!methods.includes(req.method || "")) {
    sendJson(
      res,
      405,
      { ok: false, error: "method" },
      { Allow: `${allow}, OPTIONS` },
    );
    return;
  }

  const forward = async () => {
    let body = Buffer.alloc(0);
    if (req.method === "POST" || req.method === "PUT" || req.method === "PATCH") {
      try {
        body = await readRequestBody(req);
      } catch (err) {
        sendJson(res, 413, { ok: false, error: "body-too-large" });
        return;
      }
    }
    // timeout → destroy() emits "error"; both handlers must not writeHead twice
    // or Node throws and the whole gateway process exits.
    let responded = false;
    const replyJson = (status, payload) => {
      if (responded || res.headersSent || res.writableEnded) return;
      responded = true;
      sendJson(res, status, payload);
    };
    const replyRaw = (statusCode, contentType, out) => {
      if (responded || res.headersSent || res.writableEnded) return;
      responded = true;
      res.writeHead(statusCode || 502, {
        "Content-Type": contentType || "application/json; charset=utf-8",
        "Content-Length": out.length,
        "Access-Control-Allow-Origin": "*",
        "Cache-Control": "no-store",
      });
      res.end(out);
    };

    const headers = {
      "Content-Type":
        req.headers["content-type"] || "application/x-www-form-urlencoded",
      "Content-Length": body.length,
    };
    const upstream = http.request(
      {
        host: adminHost,
        port: adminPort,
        path: adminPath,
        method: req.method,
        headers,
        timeout: 15000,
      },
      (up) => {
        const chunks = [];
        up.on("data", (c) => chunks.push(c));
        up.on("end", () => {
          replyRaw(
            up.statusCode || 502,
            up.headers["content-type"] || "application/json; charset=utf-8",
            Buffer.concat(chunks),
          );
        });
      },
    );
    upstream.on("timeout", () => {
      upstream.destroy();
      replyJson(504, { ok: false, error: "admin-timeout" });
    });
    upstream.on("error", (err) => {
      console.error(`[ws-gateway] ${adminPath} proxy`, err.message || err);
      replyJson(502, { ok: false, error: "admin-unreachable" });
    });
    if (body.length) upstream.write(body);
    upstream.end();
  };

  void forward().catch((err) => {
    console.error(`[ws-gateway] ${adminPath} forward`, err.message || err);
  });
}

const server = http.createServer((req, res) => {
  const path = (req.url || "/").split("?")[0];
  if (path === "/api/monster-levels") {
    proxyAdminApi(req, res, "/api/monster-levels", { methods: ["GET"] });
    return;
  }
  if (path === "/api/recharge/create") {
    proxyAdminApi(req, res, "/api/recharge/create", { methods: ["POST"] });
    return;
  }
  if (path === "/api/register") {
    proxyAdminApi(req, res, "/api/register", { methods: ["POST"] });
    return;
  }
  if (path === "/healthz") {
    sendJson(res, 200, { ok: true, service: "pix-ws-gateway" });
    return;
  }
  sendJson(res, 404, { ok: false, error: "not-found" });
});

const wss = new WebSocketServer({ server });
server.on("error", (err) => {
  console.error("[ws-gateway] http server", err.message || err);
});
wss.on("error", (err) => {
  console.error("[ws-gateway] wss", err.message || err);
});
server.listen(listenPort, listenHost, () => {
  const tips = lanIpv4s().map((ip) => `ws://${ip}:${listenPort}`);
  console.log(
    `[ws-gateway] listen ${listenHost}:${listenPort} -> tcp://${tcpHost}:${tcpPort}`,
  );
  console.log(
    `[ws-gateway] http /api/monster-levels -> http://${adminHost}:${adminPort}/api/monster-levels`,
  );
  console.log(
    `[ws-gateway] http /api/recharge/create -> http://${adminHost}:${adminPort}/api/recharge/create`,
  );
  console.log(
    `[ws-gateway] http /api/register -> http://${adminHost}:${adminPort}/api/register`,
  );
  if (tips.length) {
    console.log(`[ws-gateway] phone / LAN: ${tips.join("  ")}`);
  } else {
    console.log(`[ws-gateway] local: ws://127.0.0.1:${listenPort}`);
  }
});

// Keep the bridge up: one bad proxy/WS edge must not tear down the process.
process.on("uncaughtException", (err) => {
  console.error("[ws-gateway] uncaughtException", err?.stack || err);
});
process.on("unhandledRejection", (err) => {
  console.error("[ws-gateway] unhandledRejection", err?.stack || err);
});

const CBMS_FLAG_SCENE_POLL = 0x2;
const CBMS_FLAG_DISCONNECT = 0x4;
/**
 * Idle scene-poll (presence keepalive). Default 10000ms (~1/3 of ~30s kick).
 * Set PIX_GATEWAY_KEEPALIVE_MS=0 to disable. Unrelated to mock hangup 2/10.
 */
const GATEWAY_KEEPALIVE_MS = Number(process.env.PIX_GATEWAY_KEEPALIVE_MS || 10000);
/** Only synth poll when client has been silent this long (avoid double-poll races). */
const GATEWAY_KEEPALIVE_IDLE_MS = Number(
  process.env.PIX_GATEWAY_KEEPALIVE_IDLE_MS || 2200,
);
/**
 * After abrupt WS drop (no CBMS DISCONNECT), keep TCP scene-poll for this
 * clientId so mock does not heartbeat-timeout while the phone reconnects.
 * Default 10 minutes; 0 disables orphan (old behavior: WS close → stop poll).
 */
const GATEWAY_ORPHAN_MS = Number(
  process.env.PIX_GATEWAY_ORPHAN_MS !== undefined
    ? process.env.PIX_GATEWAY_ORPHAN_MS
    : 600_000,
);

/** @type {Map<number, { timer: NodeJS.Timeout, busy: boolean, startedAt: number }>} */
const orphanKeepalives = new Map();

function stopOrphanKeepalive(clientId, reason) {
  const id = clientId >>> 0;
  const entry = orphanKeepalives.get(id);
  if (!entry) return;
  clearInterval(entry.timer);
  orphanKeepalives.delete(id);
  console.log(
    `[ws-gateway] orphan stop clientId=${id.toString(16)} reason=${reason}`,
  );
}

function startOrphanKeepalive(clientId) {
  const id = clientId >>> 0;
  if (GATEWAY_KEEPALIVE_MS <= 0 || GATEWAY_ORPHAN_MS <= 0 || !id) return;
  stopOrphanKeepalive(id, "restart");
  const startedAt = Date.now();
  const entry = { busy: false, startedAt, timer: null };
  entry.timer = setInterval(() => {
    if (Date.now() - startedAt >= GATEWAY_ORPHAN_MS) {
      stopOrphanKeepalive(id, "grace-expired");
      return;
    }
    if (entry.busy) return;
    entry.busy = true;
    const req = encodeScenePollCbms(id);
    // Presence only — no live WS to push responses to.
    void exchangeOnce(req)
      .catch((err) => {
        console.error(
          `[ws-gateway] orphan clientId=${id.toString(16)}`,
          err.message || err,
        );
      })
      .finally(() => {
        entry.busy = false;
      });
  }, GATEWAY_KEEPALIVE_MS);
  if (typeof entry.timer.unref === "function") entry.timer.unref();
  orphanKeepalives.set(id, entry);
  console.log(
    `[ws-gateway] orphan start clientId=${id.toString(16)} ` +
      `grace=${GATEWAY_ORPHAN_MS}ms interval=${GATEWAY_KEEPALIVE_MS}ms`,
  );
}

function parseCbmsHead(req) {
  if (!Buffer.isBuffer(req) || req.length < 24) return null;
  if (req.toString("ascii", 0, 4) !== "CBMS") return null;
  const flags = req.readUInt32LE(8);
  const metaLen = req.readUInt32LE(16);
  if (metaLen < 4 || req.length < 20 + 4) return null;
  const clientId = req.readUInt32LE(20) >>> 0;
  return { flags, clientId, metaLen };
}

function encodeScenePollCbms(clientId) {
  const out = Buffer.alloc(24);
  out.write("CBMS", 0, 4, "ascii");
  out.writeUInt32LE(1, 4);
  out.writeUInt32LE(CBMS_FLAG_SCENE_POLL, 8);
  out.writeUInt32LE(4, 12); // payload = meta only
  out.writeUInt32LE(4, 16); // metaLen
  out.writeUInt32LE(clientId >>> 0, 20);
  return out;
}

wss.on("connection", (ws) => {
  // Serialize per-socket exchanges so overlapping browser sends cannot interleave.
  let chain = Promise.resolve();
  /** Last CBMS clientId — gateway keepalive reuses it. */
  let clientId = 0;
  /** Armed after client sends SCENE_POLL (in-world); cleared on DISCONNECT. */
  let pollArmed = false;
  /** True logout — do not orphan-keepalive after this WS closes. */
  let explicitDisconnect = false;
  let lastClientMsgAt = 0;
  let keepaliveBusy = false;

  const clearKeepalive = () => {
    if (ws._pixKeepaliveTimer) {
      clearInterval(ws._pixKeepaliveTimer);
      ws._pixKeepaliveTimer = null;
    }
  };

  const enqueueExchange = (req, via) => {
    const job = chain.then(async () => {
      if (ws.readyState !== ws.OPEN) return;
      try {
        const frames = await exchangeOnce(req);
        for (const frame of frames) {
          if (ws.readyState === ws.OPEN) ws.send(frame);
        }
      } catch (err) {
        console.error(`[ws-gateway] ${via}`, err.message || err);
        // Keep WS alive — client will time out / retry on the same session.
      }
    });
    chain = job.catch((err) => {
      console.error("[ws-gateway] chain", err.message || err);
    });
    return chain;
  };

  ws._pixKeepaliveTimer = null;
  if (GATEWAY_KEEPALIVE_MS > 0) {
    ws._pixKeepaliveTimer = setInterval(() => {
      if (ws.readyState !== ws.OPEN) {
        clearKeepalive();
        return;
      }
      if (!pollArmed || !clientId || keepaliveBusy) return;
      const idleFor = Date.now() - lastClientMsgAt;
      if (idleFor < GATEWAY_KEEPALIVE_IDLE_MS) return;
      keepaliveBusy = true;
      const req = encodeScenePollCbms(clientId);
      void enqueueExchange(req, `keepalive clientId=${clientId}`).finally(() => {
        keepaliveBusy = false;
      });
    }, GATEWAY_KEEPALIVE_MS);
    if (typeof ws._pixKeepaliveTimer.unref === "function") {
      ws._pixKeepaliveTimer.unref();
    }
  }

  ws.on("error", (err) => {
    console.error("[ws-gateway] ws", err.message || err);
  });

  ws.on("close", () => {
    clearKeepalive();
    // Abrupt drop (OEM kill / Wi-Fi blip): keep mock presence warm so the
    // same LiveTransport.clientId can reconnect without heartbeat-timeout.
    // Explicit DISCONNECT (logout) must not leave a ghost online.
    if (pollArmed && clientId && !explicitDisconnect) {
      startOrphanKeepalive(clientId);
    } else if (clientId && explicitDisconnect) {
      stopOrphanKeepalive(clientId, "disconnect");
    }
    pollArmed = false;
  });

  ws.on("message", (data) => {
    lastClientMsgAt = Date.now();
    const req = Buffer.isBuffer(data) ? data : Buffer.from(data);
    const head = parseCbmsHead(req);
    if (head) {
      clientId = head.clientId || clientId;
      if (head.flags & CBMS_FLAG_DISCONNECT) {
        pollArmed = false;
        explicitDisconnect = true;
        stopOrphanKeepalive(clientId, "disconnect");
      } else if (head.flags & CBMS_FLAG_SCENE_POLL) {
        pollArmed = true;
        explicitDisconnect = false;
        // Live WS reclaimed this clientId — stop orphan TCP polls.
        stopOrphanKeepalive(clientId, "reclaim");
      } else if (clientId) {
        // Any other traffic from this id also means the socket is live again.
        stopOrphanKeepalive(clientId, "reclaim");
      }
    }
    void enqueueExchange(req, "client");
  });
});

console.log(
  GATEWAY_KEEPALIVE_MS > 0
    ? `[ws-gateway] idle scene-poll keepalive ${GATEWAY_KEEPALIVE_MS}ms ` +
        `(idle≥${GATEWAY_KEEPALIVE_IDLE_MS}ms; PIX_GATEWAY_KEEPALIVE_MS)`
    : "[ws-gateway] idle scene-poll keepalive off (PIX_GATEWAY_KEEPALIVE_MS=0)",
);
console.log(
  GATEWAY_ORPHAN_MS > 0 && GATEWAY_KEEPALIVE_MS > 0
    ? `[ws-gateway] orphan keepalive after WS drop ${GATEWAY_ORPHAN_MS}ms ` +
        `(PIX_GATEWAY_ORPHAN_MS; 0=off)`
    : "[ws-gateway] orphan keepalive off",
);
