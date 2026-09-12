/**
 * Minimal WebSocket ↔ TCP CBMS bridge (+ HTTP proxy for monster levels).
 * Usage:
 *   node scripts/ws-gateway.mjs [listenWsPort=19092] [tcpHost=127.0.0.1] [tcpPort=19090] [listenHost=0.0.0.0]
 * Config file (optional): PixiJS_web/gateway.conf — see that file for keys.
 *   PIX_GATEWAY_CONFIG=/path/to/gateway.conf
 * Env (override config):
 *   PIX_GATEWAY_HOST / PIX_GATEWAY_PORT — listen bind (default 0.0.0.0:19092)
 *   PIX_TCP_HOST / PIX_TCP_PORT — mock-server (default 127.0.0.1:19090)
 *   PIX_ADMIN_HOST / PIX_ADMIN_PORT — admin HTTP for /api/* proxy
 *     (monster-levels, npc-scenes, zones, recharge/create, register; default same host as TCP, port 19091)
 *   PIX_GATEWAY_KEEPALIVE_MS — idle scene-poll interval (default 30000; 0=off)
 *   PIX_GATEWAY_KEEPALIVE_IDLE_MS — silence before synth poll (default 2200)
 *   PIX_GATEWAY_MOCK_TCP_TIMEOUT_MS — mock TCP read budget per CBMS (default 60000)
 *   PIX_GATEWAY_ORPHAN_MS — after WS drop (no DISCONNECT), keep TCP scene-poll
 *     for this clientId so mock presence does not heartbeat-timeout (default
 *     120000 = 2min; 0=off). Client LiveTransport reconnects with same id.
 *   PIX_GATEWAY_MOCK_TCP_MAX — max concurrent outbound mock TCP sockets (default
 *     48; prevents Windows ephemeral-port EADDRINUSE under login brute force).
 *   PIX_GATEWAY_WS_PER_IP_MAX — max new WS handshakes per IPv4 per 60s (default
 *     24; 0=off). Rejects excess with close code 1008 before opening mock TCP.
 *
 * Priority: CLI argv / env > gateway.conf > built-in defaults.
 */
import fs from "node:fs";
import http from "node:http";
import net from "node:net";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { WebSocketServer } from "ws";

/** Script dir (node .mjs) or exe dir (SEA). Avoid import.meta in CJS/SEA. */
function gatewayBaseDirs() {
  const dirs = [];
  try {
    if (typeof import.meta !== "undefined" && import.meta.url) {
      dirs.push(path.dirname(fileURLToPath(import.meta.url)));
    }
  } catch {
    /* SEA / CJS: import.meta empty */
  }
  if (typeof __dirname !== "undefined" && __dirname) {
    dirs.push(__dirname);
  }
  try {
    dirs.push(path.dirname(process.execPath));
  } catch {
    /* ignore */
  }
  dirs.push(process.cwd());
  return [...new Set(dirs.filter(Boolean))];
}

/** @returns {Record<string, string>} */
function loadGatewayConfFile() {
  const candidates = [];
  if (process.env.PIX_GATEWAY_CONFIG) {
    candidates.push(process.env.PIX_GATEWAY_CONFIG);
  }
  for (const dir of gatewayBaseDirs()) {
    candidates.push(path.join(dir, "gateway.conf"));
    candidates.push(path.join(dir, "..", "gateway.conf"));
  }
  /** @type {Record<string, string>} */
  const out = {};
  const seen = new Set();
  for (const file of candidates) {
    const abs = path.resolve(file);
    if (seen.has(abs)) continue;
    seen.add(abs);
    try {
      if (!fs.existsSync(abs)) continue;
      const text = fs.readFileSync(abs, "utf8");
      let applied = 0;
      for (const raw of text.split(/\r?\n/)) {
        const line = raw.trim();
        if (!line || line.startsWith("#") || line.startsWith(";")) continue;
        const eq = line.indexOf("=");
        if (eq <= 0) continue;
        const key = line.slice(0, eq).trim();
        const value = line.slice(eq + 1).trim();
        if (!key || !value) continue;
        out[key] = value;
        applied += 1;
      }
      console.log(
        `[ws-gateway] config_file path=${abs} applied=${applied} (env/argv still override)`,
      );
      break;
    } catch (err) {
      console.warn(
        `[ws-gateway] config_file read failed path=${abs}`,
        err?.message || err,
      );
    }
  }
  return out;
}

const fileCfg = loadGatewayConfFile();

function cfg(key, envName, fallback) {
  if (process.env[envName] !== undefined && process.env[envName] !== "") {
    return process.env[envName];
  }
  if (fileCfg[key] !== undefined && fileCfg[key] !== "") {
    return fileCfg[key];
  }
  return fallback;
}

const listenPort = Number(
  process.env.PIX_GATEWAY_PORT || process.argv[2] || cfg("listen_port", "PIX_GATEWAY_PORT", "19092"),
);
const tcpHost =
  process.env.PIX_TCP_HOST ||
  process.argv[3] ||
  cfg("tcp_host", "PIX_TCP_HOST", "127.0.0.1");
const tcpPort = Number(
  process.env.PIX_TCP_PORT ||
    process.argv[4] ||
    cfg("tcp_port", "PIX_TCP_PORT", "19090"),
);
const listenHost =
  process.env.PIX_GATEWAY_HOST ||
  process.argv[5] ||
  cfg("listen_host", "PIX_GATEWAY_HOST", "0.0.0.0");
const adminHost =
  process.env.PIX_ADMIN_HOST ||
  cfg("admin_host", "PIX_ADMIN_HOST", "") ||
  tcpHost ||
  "127.0.0.1";
const adminPort = Number(
  process.env.PIX_ADMIN_PORT ||
    cfg("admin_port", "PIX_ADMIN_PORT", "19091"),
);

const CBMS_META_CLIENT_IPV4_TAG = 0xfe;

function parseIpv4String(host) {
  if (!host) return 0;
  const m = /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.exec(host);
  if (!m) return 0;
  const octets = m.slice(1, 5).map((part) => Number(part));
  if (octets.some((value) => value > 255)) return 0;
  return (
    ((octets[0] << 24) |
      (octets[1] << 16) |
      (octets[2] << 8) |
      octets[3]) >>>
    0
  );
}

function formatIpv4(ipv4) {
  if (!ipv4) return "0.0.0.0";
  return [
    (ipv4 >>> 24) & 0xff,
    (ipv4 >>> 16) & 0xff,
    (ipv4 >>> 8) & 0xff,
    ipv4 & 0xff,
  ].join(".");
}

function clientRemoteIpv4(ws) {
  const raw = ws?._socket?.remoteAddress;
  if (!raw) return 0;
  if (raw.startsWith("::ffff:")) return parseIpv4String(raw.slice(7));
  if (raw.includes(":")) return 0;
  return parseIpv4String(raw);
}

function cbmsMetaHasClientIp(meta) {
  return meta.length >= 5 && meta[meta.length - 5] === CBMS_META_CLIENT_IPV4_TAG;
}

function injectClientIpIntoCbms(req, remoteIpv4) {
  if (!remoteIpv4 || !Buffer.isBuffer(req) || req.length < 20) return req;
  if (req.toString("ascii", 0, 4) !== "CBMS") return req;
  const payloadLen = req.readUInt32LE(12);
  const metaLen = req.readUInt32LE(16);
  if (payloadLen < metaLen || 20 + payloadLen > req.length) return req;
  const meta = req.subarray(20, 20 + metaLen);
  if (cbmsMetaHasClientIp(meta)) return req;
  const ext = Buffer.alloc(5);
  ext[0] = CBMS_META_CLIENT_IPV4_TAG;
  ext.writeUInt32LE(remoteIpv4 >>> 0, 1);
  const newMeta = Buffer.concat([meta, ext]);
  const wtBody = req.subarray(20 + metaLen, 20 + payloadLen);
  const newPayloadLen = newMeta.length + wtBody.length;
  const out = Buffer.alloc(20 + newPayloadLen);
  req.copy(out, 0, 0, 16);
  out.writeUInt32LE(newPayloadLen, 12);
  out.writeUInt32LE(newMeta.length, 16);
  newMeta.copy(out, 20);
  wtBody.copy(out, 20 + newMeta.length);
  return out;
}

const MOCK_TCP_MAX = Number(process.env.PIX_GATEWAY_MOCK_TCP_MAX || 48);
const WS_PER_IP_MAX = Number(process.env.PIX_GATEWAY_WS_PER_IP_MAX || 24);
const WS_PER_IP_WINDOW_MS = 60000;

let mockTcpInflight = 0;
/** @type {Array<() => void>} */
const mockTcpWaiters = [];

function acquireMockTcpSlot() {
  if (mockTcpInflight < MOCK_TCP_MAX) {
    mockTcpInflight += 1;
    return Promise.resolve();
  }
  return new Promise((resolve) => {
    mockTcpWaiters.push(() => {
      mockTcpInflight += 1;
      resolve();
    });
  });
}

function releaseMockTcpSlot() {
  mockTcpInflight = Math.max(0, mockTcpInflight - 1);
  const next = mockTcpWaiters.shift();
  if (next) next();
}

/** @type {Map<number, { count: number, resetAt: number }>} */
const wsConnectBudgetByIpv4 = new Map();

function allowWsHandshake(remoteIpv4) {
  if (!WS_PER_IP_MAX || !remoteIpv4) return true;
  const now = Date.now();
  let entry = wsConnectBudgetByIpv4.get(remoteIpv4);
  if (!entry || now >= entry.resetAt) {
    entry = { count: 0, resetAt: now + WS_PER_IP_WINDOW_MS };
    wsConnectBudgetByIpv4.set(remoteIpv4, entry);
  }
  entry.count += 1;
  return entry.count <= WS_PER_IP_MAX;
}

async function exchangeOnce(reqBuf) {
  await acquireMockTcpSlot();
  try {
    const socket = net.connect({ host: tcpHost, port: tcpPort });
    await new Promise((resolve, reject) => {
      socket.once("connect", resolve);
      socket.once("error", reject);
    });

    const mockTcpTimeoutMs = Number(
      process.env.PIX_GATEWAY_MOCK_TCP_TIMEOUT_MS || 60000,
    );

    // Continuous buffer — do not attach one-shot `data` listeners inside pull();
    // localhost CBMR can arrive before the listener is registered and be lost.
    let pending = Buffer.alloc(0);
    let wait = null;
    let ended = null;

    const failSocket = (err) => {
      ended = err instanceof Error ? err : new Error(String(err));
      socket.destroy();
      wake();
    };

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
      failSocket(e);
    });
    socket.on("end", () => {
      failSocket(new Error("tcp ended early"));
    });
    if (mockTcpTimeoutMs > 0) {
      socket.setTimeout(mockTcpTimeoutMs);
      socket.on("timeout", () => {
        failSocket(new Error(`mock tcp timeout ${mockTcpTimeoutMs}ms`));
      });
    }

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
    socket.setTimeout(0);
    socket.destroy();
    return frames;
  } finally {
    releaseMockTcpSlot();
  }
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
  if (path === "/api/world-boss-active") {
    proxyAdminApi(req, res, "/api/world-boss-active", { methods: ["GET"] });
    return;
  }
  if (path === "/api/npc-scenes") {
    proxyAdminApi(req, res, "/api/npc-scenes", { methods: ["GET"] });
    return;
  }
  if (path === "/api/zones") {
    proxyAdminApi(req, res, "/api/zones", { methods: ["GET"] });
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
  if (path === "/user/password") {
    proxyAdminApi(req, res, "/user/password", { methods: ["POST"] });
    return;
  }
  if (path === "/healthz") {
    sendJson(res, 200, { ok: true, service: "pix-ws-gateway" });
    return;
  }
  sendJson(res, 404, { ok: false, error: "not-found" });
});

const wss = new WebSocketServer({ server });

function reportListenFailure(label, err) {
  const code = err?.code || "";
  if (code === "EADDRINUSE") {
    console.error(
      `[ws-gateway] ${label} EADDRINUSE ${listenHost}:${listenPort} — ` +
        "port already in use (another gateway may already be running).",
    );
    console.error(
      `[ws-gateway] Windows: netstat -ano | findstr :${listenPort}`,
    );
    console.error(
      "[ws-gateway] Stop the old gateway/node process, or use the existing listener.",
    );
  } else {
    console.error(`[ws-gateway] ${label}`, err?.message || err);
  }
  process.exitCode = 1;
  setTimeout(() => process.exit(1), 50);
}

server.on("error", (err) => {
  reportListenFailure("http server", err);
});
wss.on("error", (err) => {
  reportListenFailure("wss", err);
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
    `[ws-gateway] http /api/world-boss-active -> http://${adminHost}:${adminPort}/api/world-boss-active`,
  );
  console.log(
    `[ws-gateway] http /api/npc-scenes -> http://${adminHost}:${adminPort}/api/npc-scenes`,
  );
  console.log(
    `[ws-gateway] http /api/zones -> http://${adminHost}:${adminPort}/api/zones`,
  );
  console.log(
    `[ws-gateway] http /api/recharge/create -> http://${adminHost}:${adminPort}/api/recharge/create`,
  );
  console.log(
    `[ws-gateway] http /api/register -> http://${adminHost}:${adminPort}/api/register`,
  );
  console.log(
    `[ws-gateway] http /user/password -> http://${adminHost}:${adminPort}/user/password`,
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
 * Idle scene-poll (presence keepalive). Default 30s; set PIX_GATEWAY_KEEPALIVE_MS=0
 * to disable (client/native FGS still polls). Unrelated to mock hangup 2/10.
 */
const GATEWAY_KEEPALIVE_MS = Number(
  process.env.PIX_GATEWAY_KEEPALIVE_MS !== undefined
    ? process.env.PIX_GATEWAY_KEEPALIVE_MS
    : 30000,
);
/** Only synth poll when client has been silent this long (avoid double-poll races). */
const GATEWAY_KEEPALIVE_IDLE_MS = Number(
  process.env.PIX_GATEWAY_KEEPALIVE_IDLE_MS || 2200,
);
/**
 * After abrupt WS drop (no CBMS DISCONNECT), keep TCP scene-poll for this
 * clientId so mock does not heartbeat-timeout while the phone reconnects.
 * Default 2min; requires keepalive>0. Set PIX_GATEWAY_ORPHAN_MS=0 to disable
 * (WS close → immediate DISCONNECT flush).
 */
const GATEWAY_ORPHAN_MS = Number(
  process.env.PIX_GATEWAY_ORPHAN_MS !== undefined
    ? process.env.PIX_GATEWAY_ORPHAN_MS
    : 120000,
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

/** Same meta-only CBMS shape as SCENE_POLL, flag=DISCONNECT — triggers mock flush. */
function encodeDisconnectCbms(clientId) {
  const out = Buffer.alloc(24);
  out.write("CBMS", 0, 4, "ascii");
  out.writeUInt32LE(1, 4);
  out.writeUInt32LE(CBMS_FLAG_DISCONNECT, 8);
  out.writeUInt32LE(4, 12);
  out.writeUInt32LE(4, 16);
  out.writeUInt32LE(clientId >>> 0, 20);
  return out;
}

/**
 * Notify mock to flush + mark offline for this clientId.
 * Fire-and-forget; response is discarded (WS already gone).
 */
function notifyMockDisconnect(clientId, reason) {
  const id = clientId >>> 0;
  if (!id) return Promise.resolve();
  console.log(
    `[ws-gateway] disconnect→mock clientId=${id.toString(16)} reason=${reason}`,
  );
  return exchangeOnce(encodeDisconnectCbms(id))
    .then(() => {
      console.log(
        `[ws-gateway] disconnect ok clientId=${id.toString(16)} reason=${reason}`,
      );
    })
    .catch((err) => {
      console.error(
        `[ws-gateway] disconnect fail clientId=${id.toString(16)} reason=${reason}`,
        err.message || err,
      );
    });
}

wss.on("connection", (ws, req) => {
  const remoteIpv4 = clientRemoteIpv4(ws);
  if (!allowWsHandshake(remoteIpv4)) {
    console.warn(
      `[ws-gateway] ws rate-limited ip=${formatIpv4(remoteIpv4)} ` +
        `max=${WS_PER_IP_MAX}/${WS_PER_IP_WINDOW_MS}ms`,
    );
    ws.close(1008, "rate-limited");
    return;
  }
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
      const head = parseCbmsHead(req);
      const isDisconnect = !!(head && (head.flags & CBMS_FLAG_DISCONNECT));
      // DISCONNECT must still reach mock after WS close (logout race);
      // other traffic can drop — no live client to deliver the CBMR.
      if (ws.readyState !== ws.OPEN && !isDisconnect) return;
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
    const id = clientId >>> 0;
    const wasExplicit = explicitDisconnect;
    const wasArmed = pollArmed;
    pollArmed = false;
    if (!id) return;
    if (wasExplicit) {
      // Client already sent CBMS DISCONNECT (logout / LiveTransport.close).
      stopOrphanKeepalive(id, "disconnect");
      return;
    }
    // Opt-in orphan: keep presence warm across brief drops (legacy).
    if (GATEWAY_KEEPALIVE_MS > 0 && GATEWAY_ORPHAN_MS > 0 && wasArmed) {
      startOrphanKeepalive(id);
      return;
    }
    stopOrphanKeepalive(id, "ws-close");
    // Default: drain in-flight exchanges, then flush role data immediately
    // (do not wait for mock heartbeat-timeout ~30s).
    void chain
      .catch(() => undefined)
      .then(() => notifyMockDisconnect(id, "ws-close"));
  });

  ws.on("message", (data) => {
    lastClientMsgAt = Date.now();
    let req = Buffer.isBuffer(data) ? data : Buffer.from(data);
    const remoteIpv4 = clientRemoteIpv4(ws);
    if (remoteIpv4) req = injectClientIpIntoCbms(req, remoteIpv4);
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
        `(idle≥${GATEWAY_KEEPALIVE_IDLE_MS}ms; PIX_GATEWAY_KEEPALIVE_MS) ` +
        `mock_tcp_timeout=${Number(process.env.PIX_GATEWAY_MOCK_TCP_TIMEOUT_MS || 60000)}ms`
    : "[ws-gateway] idle scene-poll keepalive off (PIX_GATEWAY_KEEPALIVE_MS=0)",
);
console.log(
  `[ws-gateway] mock_tcp_max=${MOCK_TCP_MAX} ws_per_ip_max=${WS_PER_IP_MAX || "off"}`,
);
console.log(
  GATEWAY_ORPHAN_MS > 0 && GATEWAY_KEEPALIVE_MS > 0
    ? `[ws-gateway] orphan keepalive after WS drop ${GATEWAY_ORPHAN_MS}ms ` +
        `(PIX_GATEWAY_ORPHAN_MS; 0=off)`
    : "[ws-gateway] orphan keepalive off; WS close → immediate DISCONNECT flush",
);
